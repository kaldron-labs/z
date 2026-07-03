//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC link API implementation

#ifndef Zquic_Link_HH
#define Zquic_Link_HH

#ifndef Zquic_HH
#include <zlib/Zquic.hh>
#endif

#ifndef Zquic_Stream_HH
#include <zlib/Zquic_Stream.hh>
#endif

namespace Zquic {

ZtEnumStruct(CxnTimer, int8_t,
  AckDelay,
  Loss,
  PTO,
  Idle,
  Close,
  KeyDiscard,
  PMTUD,
  PathValid);

template <typename App, typename Impl, typename TxBufAlloc_, typename Stream_>
class Link : public ZmPolymorph {
template <typename, typename>
friend class Server;
public:
  using Self = Link<App, Impl, TxBufAlloc_, Stream_>;
  using TxBufAlloc = TxBufAlloc_;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Streams = Streams_<Stream>;
  using StreamsRef = ZmRef<Streams>;
  using ClosedStreams =
    ZmHashKV<uint64_t, bool,
      ZmHashHeapID<"Zquic.Stream.ClosedHash">>;
  using ClosedStreamsRef = ZmRef<ClosedStreams>;
  using PathResponses =
    ZmQueue<ControlFrame,
      ZmQueueHeapID<"Zquic.Link.PathResponses">>;
  using StreamQueue =
    ZmQueue<StreamRef,
      ZmQueueHeapID<"Zquic.Link.StreamQueue">>;
  static constexpr unsigned OpenQueuedBatch = 32;
  // PATH_RESPONSE frames are concrete replies; cap queued payloads at the
  // per-packet sent-frame metadata limit.
  static constexpr unsigned PathResponseMax = SentPkt::MaxFrames;
  static constexpr unsigned RecoveryScanBatch = 256;
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Link(App *app, bool isServer = false) :
    m_app{app}, m_isServer{isServer},
    m_streams{new Streams}, m_closedStreams{new ClosedStreams} {
    for (unsigned i = 0; i < 3; ++i) {
      CryptoStream *crypto = &m_rxCrypto[i];
      crypto->dequeueFn([this, crypto]() {
	this->app()->rxRun([crypto]() {
	  crypto->dequeueRx_();
	});
      });
    }
    for (unsigned i = 0; i < AckManager::Spaces; ++i) {
      auto space = PktNumSpace::T(i);
      AckTracker *tracker = &m_rxAcks.tracker(space);
      tracker->dequeueFn([this, tracker]() {
	this->app()->rxRun([tracker]() {
	  tracker->dequeueRx_();
	});
      });
    }
  }
  ~Link() {
    clearCallbacks_();
    assert(!timersActive_());
  }

  void initCryptoDelivery_() {
    for (unsigned i = 0; i < 3; ++i) {
      auto space = PktNumSpace::T(i);
      CryptoStream *crypto = &m_rxCrypto[i];
      crypto->deliveryFn([this, space](ZuBSpan span) {
	size_t epoch = space == PktNumSpace::Initial ? 0 :
	  space == PktNumSpace::Handshake ? 2 : 3;
	if (!this->impl()->emitTLS_(epoch, span, m_rxCryptoAddr[space]))
	  this->tlsFailure_();
      });
    }
  }

  void clearCallbacks_() {
    for (auto &crypto : m_rxCrypto) {
      crypto.dequeueFn({});
      crypto.deliveryFn({});
    }
    for (unsigned i = 0; i < AckManager::Spaces; ++i)
      m_rxAcks.tracker(PktNumSpace::T(i)).dequeueFn({});
  }

  App *app() const { return m_app; }
  bool isServer() const { return m_isServer; }
  bool closed() const { return m_appClose.closed; }
  uint64_t closeError() const { return m_appClose.error; }
  uint64_t streamCount() const { return m_streams->count_(); }
  uint64_t peerStreamLimit(Zi::StreamType::T type) const {
    return m_peerLimit[type].limit();
  }
  uint64_t localStreamsOpened(Zi::StreamType::T type) const {
    return m_peerLimit[type].opened();
  }
  uint64_t queuedLocalStreams(Zi::StreamType::T type) const {
    return m_queued[type];
  }
  uint64_t localStreamLimit(Zi::StreamType::T type) const {
    return m_localLimit[type].limit();
  }
  uint64_t peerStreamsOpened(Zi::StreamType::T type) const {
    return m_localLimit[type].opened();
  }
  bool localStreamsBlocked(Zi::StreamType::T type) const {
    return m_queued[type] != 0;
  }
  unsigned queuedControlFrames() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue inspection outside Tx thread", return 0);
    unsigned n = 0;
    if (m_maxDataControl.queued) ++n;
    if (m_dataBlockedControl.queued) ++n;
    if (m_handshakeDoneControl.queued) ++n;
    if (m_pathChallengeControl.queued) ++n;
    for (unsigned i = 0; i < 2; ++i) {
      if (m_maxStreamsControl[i].queued) ++n;
      if (m_streamsBlockedControl[i].queued) ++n;
    }
    n += m_pathResponses.count_();
    auto iter = m_streams->citer();
    while (auto node = iter())
      n += node->data().queuedControlFrames();
    return n;
  }
  uint64_t rxDataCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_rxDataCredit.available(); }

  void setPeerStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    m_peerLimit[type].set(limit);
  }
  void setLocalStreamLimit(Zi::StreamType::T type, uint64_t limit) {
    m_localLimit[type].set(limit);
  }

  bool rxApplyMaxStreams_(const Frame &frame) {
    if (!validateMaxStreams_(frame)) return false;
    uint64_t value = frame.value;
    Zi::StreamType::T type = frame.streamType;
    app()->txRun([link = impl(), type, value]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxStreams_(type, value);
      link->flushTx_();
    });
    return true;
  }
  bool applyMaxData(const Frame &frame) {
    if (!validateMaxData_(frame)) return false;
    txApplyMaxData_(frame.value);
    return true;
  }
  bool applyMaxStreamData(const Frame &frame) {
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetSent() || stream->finDequeued()) {
	  ++m_rxDiag.streamMaxClosedRx;
	  return true;
	}
      } else if (closedStreamID_(frame.streamID) &&
	  canLocalSend_(frame.streamID)) {
	++m_rxDiag.streamMaxClosedRx;
	return true;
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData_(frame, stream)) {
      ++m_rxDiag.streamMaxInvalidRx;
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    txApplyMaxStreamData_(ZuMv(stream), frame.value);
    return true;
  }
  bool receiveDataBlocked(const Frame &frame) {
    return validateDataBlocked_(frame);
  }
  bool receiveStreamDataBlocked(const Frame &frame) {
    return receiveStreamDataBlocked_(frame);
  }
  bool receiveStreamsBlocked(const Frame &frame) {
    return validateStreamsBlocked_(frame);
  }
  bool frameLegal(PktNumSpace::T level, const Frame &frame) const {
    return packetFrameLegal_(level, frame);
  }

  StreamRef stream(Zi::StreamType::T type = Zi::StreamType::Duplex) {
    if (!m_peerLimit[type].open()) {
      ++m_queued[type];
      queueBlocked_(
	FrameType::StreamsBlocked, 0, m_peerLimit[type].limit(), type);
      impl()->streamsBlocked_(type, m_peerLimit[type].limit());
      return nullptr;
    }
    return openLocalStream_(type);
  }

  StreamRef acceptPeerStream(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (StreamID::server(id) == m_isServer) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (closedStreamID_(id)) return nullptr;
    Zi::StreamType::T type = StreamID::uni(id);
    uint64_t opened = StreamID::ordinal(id) + 1;
    if (!m_localLimit[type].allowsTo(opened)) return nullptr;

    StreamRef stream = newStream_(int64_t(id));
    if (!stream) return nullptr;
    if (!peerOpenedStreamID_(id))
      ZiAssert(m_localLimit[type].openTo(opened), "Zquic",
	(), "peer stream count advanced past local limit", return nullptr);
    impl()->streamed(stream);
    scheduleStreamWritable_(stream);
    return stream;
  }

  StreamRef findStream(int64_t id) const {
    return m_streams->find(id);
  }

  int receiveFrame(const Frame &frame, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending)
      return -1;
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) {
      if (frame.streamID <= uint64_t(INT64_MAX) &&
	  closedStreamID_(frame.streamID)) {
	++m_rxDiag.streamCtlClosedRx;
	return 0;
      }
      ++m_rxDiag.streamCtlInvalidRx;
      noteInvalidStreamActivity_(TransportError::StreamState);
      return -1;
    }
    if (frame.type == FrameType::ResetStream) {
      if (stream->resetReceived()) {
	if (stream->finalSize() == frame.length) return 0;
	++m_rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity_(TransportError::FinalSize, true, true);
	return -1;
      }
      uint64_t old = stream->rxCreditUsed();
      uint64_t novel = frame.length > old ? frame.length - old : 0;
      if (frame.length > stream->rxCreditLimit() ||
	  novel > m_rxDataCredit.available()) {
	++m_rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity_(TransportError::FlowControl, false, true);
	return -1;
      }
      if (!stream->receiveReset(frame)) {
	++m_rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity_(TransportError::FinalSize, false, true);
	return -1;
      }
      if (!stream->consumeRxCreditTo(frame.length) ||
	  !m_rxDataCredit.consume(novel)) {
	++m_rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity_(TransportError::FlowControl, false, true);
	return -1;
      }
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      impl()->streamResetReceived(stream, frame.errorCode, frame.length);
      reapStreamFromRx_(stream);
      return 0;
    }
    if (stream->resetSent()) return 0;
    if (stream->stopReceived()) return 0;
    if (stream->receiveStop(frame)) {
      impl()->streamStopSendingReceived(stream, frame.errorCode);
      return 0;
    }
    ++m_rxDiag.streamCtlInvalidRx;
    noteInvalidStreamActivity_(TransportError::StreamState);
    return -1;
  }

  int receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *immediateAck = nullptr) {
    if (frame.type != FrameType::Stream)
      return receiveFrame(frame, diag);
    StreamRef stream = findOrAccept_(frame.streamID);
    if (!stream) {
      if (frame.streamID > uint64_t(INT64_MAX) ||
	  !closedStreamID_(frame.streamID) ||
	  !canPeerSend_(frame.streamID)) {
	++m_rxDiag.streamDataInvalidRx;
	noteInvalidStreamActivity_(TransportError::StreamState);
	return -1;
      }
      if (immediateAck) *immediateAck = true;
      return 0;
    }
    bool rxClosed = stream->rxComplete() || stream->resetReceived();
    int rc = stream->processFrame(frame, m_rxDataCredit, ZuMv(packet), diag);
    if (rc >= 0) {
      if (!rc) ++m_rxDiag.streamNoDataRx;
      if ((rxClosed || !rc) && immediateAck) *immediateAck = true;
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      impl()->streamData(stream, frame.offset, frame.payload, frame.fin);
      if (frame.fin) reapStreamFromRx_(stream);
    } else {
      TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
	TransportError::StreamState : TransportError::FinalSize;
      if (error == TransportError::StreamState)
	++m_rxDiag.streamDataStateRx;
      else
	++m_rxDiag.streamDataFinalRx;
      noteInvalidStreamActivity_(error, false, true);
    }
    return rc;
  }
  void streamRxDequeued_(StreamRef stream) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx dequeue processing outside Rx thread", return);
    if (!stream) return;
    bool wasRxQueued = stream->rxPending() || stream->rxQueued();
    int rc = stream->processRx_();
    if (rc >= 0) {
      maybeExtendMaxData_();
      maybeExtendMaxStreamData_(stream);
      returnStreamCredit_(stream);
      if (wasRxQueued && !stream->rxPending() && !stream->rxQueued())
	reapStreamFromRx_(stream);
      return;
    }
    TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
      TransportError::StreamState : TransportError::FinalSize;
    if (error == TransportError::StreamState)
      ++m_rxDiag.streamRxDeqStateRx;
    else
      ++m_rxDiag.streamRxDeqFinalRx;
    noteInvalidStreamActivity_(error, false, true);
  }

  void streamWritable_(const StreamRef &stream) {
    if (!stream || stream->id() < 0 || stream->txQueued()) return;
    stream->txQueued(true);
    m_streamQueue.push(stream);
  }
  void scheduleStreamWritable_(StreamRef stream) {
    if (!stream || stream->id() < 0) return;
    app()->txInvoke(impl(), [
      link = impl(),
      stream = ZuMv(stream)
    ]() mutable {
      if (link->disconnecting_()) return link;
      if (link->streamTxPending_(stream)) {
	link->streamWritable_(stream);
	link->flushTx_();
      }
      return link;
    });
  }
  bool localResetStream_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueResetStream_(streamID, appError, finalSize);
  }
  bool localStopSending_(uint64_t streamID, uint64_t appError) {
    return queueStopSending_(streamID, appError);
  }
  void streamResetSent_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    impl()->streamResetSent(streamID, appError, finalSize);
  }
  void streamStopSendingSent_(uint64_t streamID, uint64_t appError) {
    impl()->streamStopSendingSent(streamID, appError);
  }
  void transportClose_(FrameType::T type, uint64_t errorCode) {
    impl()->transportClose(type, errorCode);
  }
  void newToken_(ZuBSpan) { }

protected:
  void closeState_(uint64_t errorCode = 0) {
    m_appClose.error = errorCode;
    m_appClose.closed = true;
    closeLinkState_();
    cancelTimers();
  }
  bool writeCloseFrame_(PktBuild &build, bool appClose) const {
    int n = appClose ?
      FrameCodec::writeApplicationClose(
	build.scratch(), build.scratchAvail(), m_appClose.error) :
      FrameCodec::writeConnectionClose(
	build.scratch(), build.scratchAvail(), m_appClose.error);
    return n > 0 && build.commitScratch(unsigned(n));
  }
  bool appCloseOnDisconnect_() const {
    return static_cast<const ZmEngine<App> *>(app())->stopping();
  }
  bool sendCloseFrame_(ZiSockAddr, bool = false) { return false; }

public:
  bool disconnect(bool peer = false) {
    if (m_disconnecting.xch(1)) return false;
    app()->txRun([link = impl(), peer]() mutable {
      link->delTimers_();
      static_cast<Link *>(link)->disconnected(peer);
    });
    return true;
  }
  void disconnecting_(bool disconnecting) {
    m_disconnecting = unsigned(disconnecting);
  }
  bool disconnecting_() const { return m_disconnecting.load_(); }
  void closeForStreamActivity_(TransportError::T error) {
    ++m_rxDiag.suspiciousStreamCloses;
    m_suspiciousStreamClosed = true;
    if (!m_appClose.closed) closeState_(error);
  }
  void noteInvalidStreamActivity_(
    TransportError::T error, bool closedStream = false, bool immediate = false) {
    ++m_rxDiag.invalidStreamFrames;
    if (closedStream) ++m_rxDiag.closedStreamFrames;
    if (m_suspiciousStreamClosed) return;
    if (immediate || ++m_suspiciousStreamFrames >= SuspiciousStreamThreshold)
      closeForStreamActivity_(error);
  }
  Zquic::LinkInfo linkInfo() const { return linkInfo_(); }

protected:
  struct InitialKeyDir { enum T { Client, Server }; };
  struct RuntimeCID { enum T { Initial, Local, Peer }; };
  enum { RuntimePNLength = 2 };
  // RFC9000 specifies only a minimum/default of 2 for active_connection_id_limit.
  // This is the local advertised policy cap for active peer-issued CIDs.
  static constexpr unsigned LocalActiveCxnIDLimit = 8;
  static constexpr unsigned SuspiciousStreamThreshold = 8;

  struct LinkCID {
    CxnID			id;
    uint64_t			sequence = 0;
    ResetToken	resetToken;
    CxnState::T		state = CxnState::Tombstone;
    bool			associated = false;
  };
  struct PathState {
    Path		path;
    Path		prev;
    CxnID		peerCID;
    uint64_t		peerSeq = 0;
    ZuTime		deadline;
    PathChallenge	challenge;
    bool		active = false;
  };
  struct AppClose {
    bool		closed = false;
    uint64_t		error = 0;
  };
  struct AckSnapshot {
    PktNumSpace::T	level = PktNumSpace::Initial;
    uint64_t		gen = 0;
    uint64_t		delay = 0;
    uint64_t		largestRxTime = 0;
    AckECN		ecn;
    unsigned		nRanges = 0;
    bool		due = false;
    AckRange		ranges[Frame::MaxAckRanges];
  };
  struct RxAckMeta {
    bool		ackEliciting = false;
    bool		immediateAck = false;
  };
  struct TxAckWork {
    AckSnapshot		ack;
    uint64_t		gen = 0;
    PktAckBatch		ackBatch;
    PktLossBatch	lossBatch;
    bool		ecnValidated = false;
    bool		lossPhase = false;
    bool		congestionOpened = false;
    bool		retransmit = false;
  };
  // Intentional Rx-to-Tx shared coalescing slot.  Rx overwrites this with
  // the latest ACK snapshot while one Tx post per packet space is pending.
  // Tx consumes the final value, avoiding one scheduler post per ACK update.
  struct AckPost {
    mutable ZmPLock	lock;
    AckSnapshot		ack;
    ZiSockAddr		addr;
    uint64_t		deadlineUS = 0;
    bool		deadline = false;
    bool		posted = false;
    bool		ready = false;
  };
  struct TxCryptoSnapshot {
    TrafficSecret	secrets[3];
    bool		installed[3] = {};
  };
  struct TxPktRefs {
    bool add(const SentFrameRef &ref, Stream *stream = nullptr) {
      if (ref.kind == SentFrameKind::None || n >= SentPkt::MaxFrames)
	return false;
      refs[n++] = ref;
      streams[n - 1] = stream;
      return true;
    }
    unsigned count() const { return n; }
    const SentFrameRef &operator [](unsigned i) const { return refs[i]; }
    Stream *stream(unsigned i) const { return streams[i]; }

    SentFrameRef	refs[SentPkt::MaxFrames];
    Stream		*streams[SentPkt::MaxFrames] = {};
    unsigned		n = 0;
  };
  struct PendingControl {
    ControlFrame	frame;
    bool		queued = false;
  };
  using LocalCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.LocalCID">>;
  using PeerCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.PeerCID">>;

  unsigned scheduledStreamCount_() const {
    return m_streamQueue.count_();
  }

  bool queueControl_(const ControlFrame &frame) {
    if (!frame || disconnecting_()) return false;
    app()->txInvoke(impl(), [link = impl(), frame]() mutable {
      if (link->disconnecting_()) return link;
      link->txQueueControl_(frame);
      return link;
    });
    return true;
  }
  bool txQueueControl_(const ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue mutation outside Tx thread", return false);
    if (!frame) return false;
    switch (frame.type) {
      case FrameType::MaxData:
	return queuePendingControl_(m_maxDataControl, frame);
      case FrameType::MaxStreams:
	return queuePendingControl_(
	  m_maxStreamsControl[streamTypeIndex_(frame.streamType)], frame);
      case FrameType::DataBlocked:
	return queuePendingControl_(m_dataBlockedControl, frame);
      case FrameType::StreamsBlocked:
	return queuePendingControl_(
	  m_streamsBlockedControl[streamTypeIndex_(frame.streamType)], frame);
      case FrameType::PathChallenge:
	return queuePendingControl_(m_pathChallengeControl, frame);
      case FrameType::PathResponse:
	m_pathResponses.push(frame);
	while (m_pathResponses.count_() > PathResponseMax) m_pathResponses.shift();
	return true;
      case FrameType::HandshakeDone:
	return queuePendingControl_(m_handshakeDoneControl, frame);
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending: {
	if (frame.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findStream(int64_t(frame.streamID));
	if (!stream) return false;
	bool queued = false;
	switch (frame.type) {
	  case FrameType::MaxStreamData:
	    queued = stream->queueMaxStreamData(frame.value);
	    break;
	  case FrameType::StreamDataBlocked:
	    queued = stream->queueStreamDataBlocked(frame.value);
	    break;
	  case FrameType::ResetStream:
	    queued = stream->queueResetStream(frame.errorCode, frame.value);
	    break;
	  case FrameType::StopSending:
	    queued = stream->queueStopSending(frame.errorCode);
	    break;
	  default:
	    break;
	}
	if (queued) streamWritable_(stream);
	return queued;
      }
      default:
	return false;
    }
  }
  bool queueFlowUpdate_(const FlowUpdate &update) {
    return update.needed() && queueControl_(ControlFrame::flowUpdate(update));
  }
  bool queueBlocked_(
    FrameType::T type, uint64_t streamID, uint64_t value,
    Zi::StreamType::T streamType = Zi::StreamType::Duplex) {
    ControlFrame frame = ControlFrame::blocked(
      type, streamID, value, streamType);
    if (!blockedFrameNeeded_(frame)) return false;
    if (!queueControl_(frame)) return false;
    noteBlockedQueued_(frame);
    ZquicLOG(app()->qlogTrace(), ([
      type,
      streamID,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      switch (type) {
	case FrameType::DataBlocked:
	  o.logCxnDataBlockedUpd(
	    BlockedEvent{
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::CxnFlowCtrl,
	      .linkInfo = linkInfo
	    },
	    time);
	  break;
	case FrameType::StreamDataBlocked:
	  o.logStreamDataBlockedUpd(
	    BlockedEvent{
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::StreamFlowCtrl,
	      .streamID = streamID,
	      .linkInfo = linkInfo
	    },
	    time);
	  break;
	case FrameType::StreamsBlocked:
	  o.logStreamDataBlockedUpd(
	    BlockedEvent{
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::StreamID,
	      .streamID = streamID,
	      .linkInfo = linkInfo
	    },
	    time);
	  break;
	default:
	  break;
      }
    }));
    return true;
  }
  bool queuePathResponse_(ZuBSpan data) {
    return queueControl_(ControlFrame::pathResponse(data));
  }
  bool queuePathChallenge_(ZuBSpan data) {
    return queueControl_(ControlFrame::pathChallenge(data));
  }
  bool queueHandshakeDone_() {
    return queueControl_(ControlFrame::handshakeDone());
  }
  bool queueResetStream_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueControl_(ControlFrame::resetStream(
      streamID, appError, finalSize));
  }
  bool queueStopSending_(uint64_t streamID, uint64_t appError) {
    return queueControl_(ControlFrame::stopSending(streamID, appError));
  }
  bool rxApplyMaxData_(const Frame &frame) {
    if (!validateMaxData_(frame)) return false;
    uint64_t value = frame.value;
    app()->txRun([link = impl(), value]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxData_(value);
      link->flushTx_();
    });
    return true;
  }
  bool rxApplyMaxStreamData_(const Frame &frame) {
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetSent() || stream->finDequeued()) {
	  ++m_rxDiag.streamMaxClosedRx;
	  return true;
	}
      } else if (closedStreamID_(frame.streamID) &&
	  canLocalSend_(frame.streamID)) {
	++m_rxDiag.streamMaxClosedRx;
	return true;
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData_(frame, stream)) {
      ++m_rxDiag.streamMaxInvalidRx;
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    uint64_t value = frame.value;
    app()->txRun([
      link = impl(),
      stream = ZuMv(stream),
      value
    ]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxStreamData_(ZuMv(stream), value);
      link->flushTx_();
    });
    return true;
  }
  bool txApplyMaxData_(uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_DATA processing outside Tx thread", return false);
    if (value <= m_txDataCredit.limit()) return true;
    ZquicLOG(app()->qlogTrace(), ([
      wasBlocked = m_txDataCredit.blocked(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (wasBlocked) {
	o.logCxnDataBlockedUpd(
	  BlockedEvent{
	    .oldState = BlockedState::Blocked,
	    .newState = BlockedState::Unblocked,
	    .reason = BlockedReason::CxnFlowCtrl,
	    .linkInfo = linkInfo
	  },
	  time);
      }
    }));
    m_txDataCredit.extend(value);
    m_dataBlockedControl = {};
    return true;
  }
  bool txApplyMaxStreamData_(StreamRef stream, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAM_DATA processing outside Tx thread", return false);
    if (!stream) return false;
    uint64_t streamID = uint64_t(stream->id());
    ZquicLOG(app()->qlogTrace(), ([
      streamID,
      wasBlocked = stream->txRangeCount() && !stream->txCreditAvailable(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (wasBlocked) {
	o.logStreamDataBlockedUpd(
	  BlockedEvent{
	    .oldState = BlockedState::Blocked,
	    .newState = BlockedState::Unblocked,
	    .reason = BlockedReason::StreamFlowCtrl,
	    .streamID = streamID,
	    .linkInfo = linkInfo
	  },
	  time);
      }
    }));
    stream->extendTxCredit(value);
    stream->clearControl(SentFrameRef::blocked(
      FrameType::StreamDataBlocked, streamID,
      stream->lastStreamDataBlocked(), Zi::StreamType::Duplex));
    if (streamTxPending_(stream)) streamWritable_(stream);
    return true;
  }
  bool txApplyMaxStreams_(Zi::StreamType::T type, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAMS processing outside Tx thread", return false);
    m_peerLimit[type].extend(value);
    openQueued_(type, OpenQueuedBatch);
    scheduleOpenQueued_(type);
    return true;
  }

  bool runtimeEstablished_() const {
    return m_linkState == LinkState::Established;
  }
  bool runtimeClosing_() const {
    return m_linkState == LinkState::Closing;
  }
  bool debugLog_() const {
#if defined(Zquic_DEBUG) && defined(ZiMultiplex_DEBUG)
    return app()->mx()->debug();
#else
    return false;
#endif
  }
  bool runtimeDraining_() const {
    return m_linkState == LinkState::Draining;
  }
  bool runtimeHandshakeStarted_() const {
    return m_linkState == LinkState::Handshaking ||
      m_linkState == LinkState::Established;
  }
  RuntimeRxDiag rxDiagSnapshot_() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return {});
    return m_rxDiag;
  }
  RuntimeTxDiag txDiagSnapshot_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx diagnostic snapshot outside Tx thread", return {});
    return txDiag_();
  }
  template <typename Fn>
  void runtimeDiag(Fn fn) const {
    auto link = const_cast<Link *>(this)->impl();
    if (rxInvoked_()) {
      runtimeDiagRx_(ZuMv(fn));
      return;
    }
    app()->rxRun([link, fn = ZuMv(fn)]() mutable {
      link->runtimeDiagRx_(ZuMv(fn));
    });
  }
  template <typename Fn>
  void runtimeDiagRx_(Fn fn) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return);
    auto link = const_cast<Link *>(this)->impl();
    RuntimeRxDiag rx = m_rxDiag;
    app()->txRun([link, rx, fn = ZuMv(fn)]() mutable {
      RuntimeDiag diag{rx, link->txDiagSnapshot_()};
      fn(diag);
    });
  }
  RuntimeDiag runtimeDiag_() const {
    return {m_rxDiag, txDiag_()};
  }
  template <typename Fn>
  void pathDiag(Fn fn) const {
    if (txInvoked_()) {
      PathDiag diag = pathDiag_();
      fn(diag);
      return;
    }
    auto link = const_cast<Link *>(this)->impl();
    app()->txRun([link, fn = ZuMv(fn)]() mutable {
      PathDiag diag = link->pathDiag_();
      fn(diag);
    });
  }
  PathDiag pathDiag_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path diagnostic snapshot outside Tx thread", return {});
    return m_path.diag();
  }
  bool pathValidated_() const { return m_path.validated(); }
  unsigned activePathMaxUDP_() const { return m_path.activeMaxUDP(); }
  const ZiSockAddr &activePathRemote_() const { return m_path.remote(); }
  uint64_t pathAntiAmplification_() const {
    return m_path.antiAmplificationRemaining();
  }
  const Crypto &crypto_() const { return m_crypto; }
  void snapshotTxCrypto_(TxCryptoSnapshot &snapshot) const {
    for (unsigned i = 0; i < 3; ++i) {
      auto level = PktNumSpace::T(i);
      snapshot.installed[i] = m_crypto.txTrafficSecretInstalled(level);
      if (snapshot.installed[i])
	snapshot.secrets[i] = m_crypto.txTrafficSecret(level);
    }
  }
  bool txInstallTrafficSecret_(
    PktNumSpace::T level, const TrafficSecret &secret) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx traffic secret install outside Tx thread", return false);
    if (!secret.valid()) return false;
    if (!m_txProt[level].init(secret, level, true)) {
      m_txProt[level].clear();
      return false;
    }
    return true;
  }
  bool installTxCrypto_(const TxCryptoSnapshot &snapshot) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx crypto snapshot install outside Tx thread", return false);
    for (unsigned i = 0; i < 3; ++i)
      if (snapshot.installed[i] &&
	  !txInstallTrafficSecret_(PktNumSpace::T(i), snapshot.secrets[i]))
	return false;
    return true;
  }
  bool txTrafficSecretInstalled_(PktNumSpace::T level) const {
    return m_txProt[level].valid();
  }
  const TrafficSecret &txTrafficSecret_(PktNumSpace::T level) const {
    return m_txProt[level].secret;
  }
  PktProtState &txProtState_(PktNumSpace::T level) {
    return m_txProt[level];
  }

  bool recordTxUnackd_(
    PktNumSpace::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	if (stream) {
	  if (stream->txStillUnackd(ref.offset, ref.length, ref.fin))
	    return true;
	  return stream->recordTxUnackd(ref.offset, ref.length, ref.fin);
	}
	StreamRef stream_ = findStream(int64_t(ref.streamID));
	return stream_ &&
	  (stream_->txStillUnackd(ref.offset, ref.length, ref.fin) ||
	    stream_->recordTxUnackd(ref.offset, ref.length, ref.fin));
      }
      case SentFrameKind::Crypto:
	if (txRangeStillUnackd_(
	      m_txCryptoUnackd[level], ref.offset, ref.length))
	  return true;
	return m_txCryptoUnackd[level].add(
	    new CryptoTxPQueue::Node{
	      TxUnackdRange{ref.offset, ref.length}}) != ZmPQResult::Invalid;
      default:
	return true;
    }
  }
  void recordTxUnackd_(PktNumSpace::T level, const TxPktRefs &refs) {
    for (unsigned i = 0; i < refs.count(); ++i)
      (void)recordTxUnackd_(level, refs[i], refs.stream(i));
  }
  void discardTxUnackd_(
    PktNumSpace::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return;
	if (stream) {
	  (void)stream->ackTxUnackd(ref.offset, ref.length, ref.fin);
	  return;
	}
	if (StreamRef stream_ = findStream(int64_t(ref.streamID)))
	  (void)stream_->ackTxUnackd(ref.offset, ref.length, ref.fin);
	break;
      }
      case SentFrameKind::Crypto:
	(void)m_txCryptoUnackd[level].clear(ref.offset, ref.length);
	break;
      default:
	break;
    }
  }
  void discardTxUnackd_(PktNumSpace::T level, const TxPktRefs &refs) {
    for (unsigned i = 0; i < refs.count(); ++i)
      discardTxUnackd_(level, refs[i], refs.stream(i));
  }
  void discardTxUnackd_(
    PktNumSpace::T level, ZuBSpan frame, const TxPktRefs *refs) {
    if (refs) {
      discardTxUnackd_(level, *refs);
      return;
    }
    SentFrameRef ref;
    bool ackEliciting = false;
    if (runtimeFrameRef(frame, ref, ackEliciting))
      discardTxUnackd_(level, ref);
  }
  bool ackTxFrame_(
    PktNumSpace::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	if (stream) return stream->ackTxUnackd(ref.offset, ref.length, ref.fin);
	StreamRef stream_ = findStream(int64_t(ref.streamID));
	return stream_ && stream_->ackTxUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return m_txCryptoUnackd[level].clear(ref.offset, ref.length);
      case SentFrameKind::Control: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream_;
	if (!stream) {
	  stream_ = findStream(int64_t(ref.streamID));
	  stream = stream_.ptr();
	}
	switch (ref.controlType) {
	  case FrameType::ResetStream: {
	    bool ackd = stream && stream->ackReset(ref.value, ref.length);
	    if (ackd) stream->clearControl(ref);
	    return ackd;
	  }
	  case FrameType::StopSending:
	    if (stream && stream->ackStop(ref.value)) {
	      stream->clearControl(ref);
	      return true;
	    }
	    return false;
	  default:
	    controlAckd_(ref);
	    return true;
	}
      }
      default:
	return true;
    }
  }
  bool txRangeStillUnackd_(
    const CryptoTxPQueue &queue, uint64_t offset, uint64_t length) const {
    if (!length) return false;
    bool found = false;
    queue.spans(offset, length, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool frameStillUnackd_(
    PktNumSpace::T level, const SentFrameRef &ref) const {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findStream(int64_t(ref.streamID));
	return stream &&
	  stream->txStillUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return txRangeStillUnackd_(
	  m_txCryptoUnackd[level], ref.offset, ref.length);
      case SentFrameKind::Control:
	return controlStillValid_(controlFrame_(ref));
      default:
	return true;
    }
  }
  static bool clipStreamRef_(
    SentFrameRef &ref, uint64_t first, uint64_t end) {
    constexpr uint64_t maxTxRangeField = UINT32_MAX;
    uint64_t dataEnd = ref.offset + ref.length;
    if (dataEnd < ref.offset) return false;
    uint64_t oldOffset = ref.offset;
    uint64_t oldLength = ref.length;
    uint64_t oldRangeOffset = ref.range.offset;
    if (first < oldOffset) first = oldOffset;
    if (first > dataEnd) first = dataEnd;
    uint64_t dataFirst = first;
    uint64_t dataLimit = end < dataEnd ? end : dataEnd;
    uint64_t newLength = dataLimit > dataFirst ? dataLimit - dataFirst : 0;
    if (newLength > maxTxRangeField) return false;
    bool newFin = ref.fin && end > dataEnd && dataLimit == dataEnd;
    ref.offset = dataFirst;
    ref.length = newLength;
    ref.fin = newFin;
    ref.range.streamOffset = dataFirst;
    ref.range.length = uint32_t(newLength);
    if (dataFirst >= oldOffset && dataFirst <= oldOffset + oldLength) {
      uint64_t advance = dataFirst - oldOffset;
      if (oldRangeOffset + advance <= maxTxRangeField)
	ref.range.offset = uint32_t(oldRangeOffset + advance);
    }
    return newLength || newFin;
  }
  bool clipStreamRetransmit_(SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Stream ||
	ref.streamID > uint64_t(INT64_MAX))
      return false;
    StreamRef stream = findStream(int64_t(ref.streamID));
    if (!stream) return false;
    uint64_t n = ref.length + (ref.fin ? 1 : 0);
    if (!n || n < ref.length) return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    stream->txUnackdQueue()->spans(
      ref.offset, n, [&found, &first, &end](const auto &span) {
	first = span.key();
	end = span.key() + span.length();
	found = end >= first;
	return false;
      });
    if (!found) return false;
    uint64_t oldEnd = ref.offset + n;
    if (oldEnd < ref.offset) return false;
    if (end < oldEnd) {
      tail = ref;
      if (!clipStreamRef_(tail, end, oldEnd)) tail = {};
    }
    return clipStreamRef_(ref, first, end);
  }
  bool clipCryptoRetransmit_(
    PktNumSpace::T level, SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Crypto ||
	m_txSpaceDiscarded[level] || !ref.length)
      return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    m_txCryptoUnackd[level].spans(
      ref.offset, ref.length, [&found, &first, &end](const auto &span) {
	first = span.key();
	end = span.key() + span.length();
	found = end >= first;
	return false;
      });
    if (!found) return false;
    uint64_t oldEnd = ref.offset + ref.length;
    if (oldEnd < ref.offset || end <= first) return false;
    if (end < oldEnd) {
      tail = ref;
      tail.offset = end;
      tail.length = oldEnd - end;
    }
    ref.offset = first;
    ref.length = end - first;
    return true;
  }
  bool clipRetransmit_(
    PktNumSpace::T level, SentFrameRef &ref, SentFrameRef &tail) {
    switch (ref.kind) {
      case SentFrameKind::Stream:
	return clipStreamRetransmit_(ref, tail);
      case SentFrameKind::Crypto:
	return clipCryptoRetransmit_(level, ref, tail);
      case SentFrameKind::Control:
	tail = {};
	return controlStillValid_(controlFrame_(ref));
      default:
	tail = {};
	return true;
    }
  }

  void resetRuntimeDiag_() {
    m_rxDiag = {};
    m_txDiag = {};
  }
  RuntimeTxDiag txDiag_() const {
    RuntimeTxDiag diag = m_txDiag;
    for (unsigned i = 0; i < RuntimeTxDiag::Spaces; ++i) {
      const PktTxSpace &space = m_txPkts[i];
      diag.pktBytesInFlight[i] = space.bytesInFlight();
      diag.sentPackets[i] = space.count();
      diag.retransmitPending[i] = space.retransmitPending();
      diag.retransmittable[i] = space.retransmittable();
    }
    diag.ptoTimerActive = !!m_ptoTimer;
    diag.lossTimerActive = !!m_lossTimer;
    diag.ptoBackoff = m_ptoBackoff.count();
    diag.ptoTimeoutUS =
      uint64_t(m_ptoBackoff.timeout(m_rtt, maxAckDelay_()).microsecs());
    return diag;
  }
  void updateCongestionDiag_() {
    m_txDiag.congestionWindow = m_congestion.cwnd();
    m_txDiag.congestionSSThresh = m_congestion.ssthresh();
    m_txDiag.congestionBytesInFlight = m_congestion.bytesInFlight();
  }
  void noteRxECN_(PktNumSpace::T level, EcnMark::T ecn) {
    AckECN &diag = m_rxDiag.ecnRx[level];
    switch (ecn) {
      case EcnMark::ECT0: ++diag.ect0; break;
      case EcnMark::ECT1: ++diag.ect1; break;
      case EcnMark::CE: ++diag.ce; break;
    }
  }
  unsigned congestionAllowance_() const {
    uint64_t cwnd = m_congestion.cwnd();
    uint64_t inFlight = m_congestion.bytesInFlight();
    if (inFlight >= cwnd) return 0;
    uint64_t allowance = cwnd - inFlight;
    return allowance > uint64_t(unsigned(-1)) ?
      unsigned(-1) : unsigned(allowance);
  }
  bool ecnDisabled_() const { return m_path.ecnDisabled(); }
  void setEcnDisabled_(bool b = true) { m_path.setEcnDisabled(b); }
  PktBudget sendBudget_() const {
    PktBudget budget;
    unsigned maxUDP = m_path.activeMaxUDP();
    budget.pmtu = maxUDP > TxStreamPktReserve ?
      maxUDP - TxStreamPktReserve : 0;
    budget.antiAmplification = m_path.sendAllowance();
    unsigned allowance = congestionAllowance_();
    budget.congestion = allowance < maxUDP ? allowance : maxUDP;
    return budget;
  }
  static bool sameAddr_(const ZiSockAddr &l, const ZiSockAddr &r) {
    if (!l || !r) return !l && !r;
    return l.m_sin.sin_family == r.m_sin.sin_family &&
      l.m_sin.sin_port == r.m_sin.sin_port &&
      l.m_sin.sin_addr.s_addr == r.m_sin.sin_addr.s_addr;
  }
  void resetPath_() {
    m_path = m_isServer ?
      Path::server(ZiSockAddr{}, ZiSockAddr{}) :
      Path::client(ZiSockAddr{}, ZiSockAddr{});
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
    if (!m_isServer) m_path.validated();
    m_validatingPath = {};
    m_pathChallengeControl = {};
  }
  void initClientPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->initClientPathTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void initServerPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->initServerPathTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void initClientPathTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC client path initialization outside Tx thread", return);
    m_path = Path::client(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
    m_path.validated();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Client,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathUpdated(event, time);
    }));
  }
  void initServerPathTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC server path initialization outside Tx thread", return);
    m_path = Path::server(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxUDP(app()->maxUDP());
    m_path.peerMaxUDP(app()->maxUDP());
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Server,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = false,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathUpdated(event, time);
    }));
  }
  void updatePeerPathMaxUDP_() {
    if (!m_crypto.peerTransportParamsReceived()) return;
    uint64_t maxUDP = m_crypto.peerTransportParams().maxUDPPayloadSize;
    if (maxUDP > BufSize) maxUDP = BufSize;
    app()->txRun([link = impl(), maxUDP = unsigned(maxUDP)]() mutable {
      if (link->disconnecting_()) return;
      link->m_path.peerMaxUDP(maxUDP);
    });
  }
  void validatePath_() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->validatePathTx_();
    });
  }
  void validatePathTx_() {
    m_path.validated();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Validated,
      reason = PathReason::Initial,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathUpdated(event, time);
    }));
  }
  void recordPathRx_(unsigned bytes) {
    app()->txRun([link = impl(), bytes]() mutable {
      if (link->disconnecting_()) return;
      link->recordPathRxTx_(bytes);
    });
  }
  void recordPathRxTx_(unsigned bytes) {
    m_path.received(bytes);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Received,
      reason = PathReason::Datagram,
		bytes,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

      o.logPathUpdated(event, time);
    }));
  }
  void observePathRx_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->observePathRxTx_(ZuMv(local), ZuMv(remote));
    });
  }
  void observePathRxTx_(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path receive observation outside Tx thread", return);
    ++m_txDiag.pathRxObserved;
    if (!remote) {
      ++m_txDiag.pathRxNull;
      return;
    }
    if (sameAddr_(remote, m_path.remote())) {
      ++m_txDiag.pathRxSame;
      return;
    }
    if (m_validatingPath.active &&
	sameAddr_(remote, m_validatingPath.path.remote())) {
      ++m_txDiag.pathValidationActive;
      return;
    }
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Observed,
      reason = PathReason::PeerAddrChange,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathUpdated(event, time);
    }));
    startPathValid_(ZuMv(local), ZuMv(remote));
  }
  bool startPathValid_(
    ZiSockAddr local, ZiSockAddr remote, bool armTimer = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path validation start outside Tx thread", return false);
    if (!remote || sameAddr_(remote, m_path.remote())) return false;
    PathState state;
    state.prev = m_path;
    state.path = m_isServer ?
      Path::server(ZuMv(local), remote) :
      Path::client(ZuMv(local), remote);
    state.path.configuredMaxUDP(m_path.configuredMaxUDP());
    state.path.peerMaxUDP(m_path.peerMaxUDP());
    selectPathCID_(state);
    if (!state.challenge.generate())
      return false;
    state.deadline = pathValidDeadline_();
    state.active = true;
    m_validatingPath = state;
    ++m_txDiag.pathValidationStarted;
    if (armTimer)
      schedulePathTimer_(state.deadline);
    txQueueControl_(ControlFrame::pathChallenge(
      m_validatingPath.challenge.bspan()));
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::ChallengeTx,
      reason = PathReason::PeerAddrChange,
      deadlineUS = qlogUS_(state.deadline),
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.deadlineUS = deadlineUS,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathValid(event, time);
    }));
    impl()->queueTxFlush_(m_validatingPath.path.remote());
    return true;
  }
  bool onPathResponse_(ZuBSpan data) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PATH_RESPONSE processing outside Tx thread", return false);
    if (!m_validatingPath.active ||
	!m_validatingPath.challenge.equals(data)) {
      ++m_txDiag.pathResponseUnknown;
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::ResponseUnk,
		reason = PathReason::Mismatch,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PathEvent event{
	  .kind = PathKind::PathValid,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPathValid(event, time);
      }));
      return false;
    }
    m_pathChallengeControl = {};
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::ResponseRx,
      reason = PathReason::Matched,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathValid(event, time);
    }));
    promotePath_();
    return true;
  }
  void receivePathResponse_(ZuBSpan data) {
    if (data.length() != PathChallenge::Length) return;
    uint8_t payload[PathChallenge::Length];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = data[i];
    app()->txRun([link = impl(), payload]() mutable {
      if (link->disconnecting_()) return;
      link->onPathResponse_(byteSpan(payload, sizeof(payload)));
    });
  }
  void promotePath_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path promotion outside Tx thread", return);
    if (!m_validatingPath.active) return;
    m_path = m_validatingPath.path;
    m_path.validated();
    bindPromotedCID_();
    m_validatingPath = {};
    ++m_txDiag.pathValidationPromoted;
    cancelPathTimer_();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Validated,
      reason = PathReason::Response,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathValid(event, time);
    }));
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Updated,
      reason = PathReason::Promoted,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathUpdated(event, time);
    }));
    impl()->pathPromoted_();
    const Path &path = m_path;
    impl()->pathUpdate(
      path.local(), path.remote(), path.validated(), path.activeMaxUDP());
    impl()->queueTxFlush_();
  }
  void failPathValid_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation failure outside Tx thread", return);
    if (!m_validatingPath.active) return;
    ZiSockAddr local = m_validatingPath.path.local();
    ZiSockAddr remote = m_validatingPath.path.remote();
    m_validatingPath = {};
    cancelPathTimer_();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Failed,
      reason = PathReason::Timeout,
      antiAmplification = m_path.antiAmplificationRemaining(),
      mtu = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.antiAmplification = antiAmplification,
		.mtu = mtu,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPathValid(event, time);
    }));
    impl()->migrationFailure(local, remote);
    impl()->queueTxFlush_();
  }
  void pathExpired_() { failPathValid_(); }
#ifdef Zquic_DEBUG
  void forceActivePathMTU_(unsigned size) {
    m_path.startProbe(size);
    m_path.probeAckd();
  }
  bool startPMTUDProbeChecked_(unsigned size) {
    return m_path.startProbeChecked(size);
  }
  void ackPMTUDProbe_(unsigned size) {
    onPMTUDProbeAckd_(size);
  }
  void losePMTUDProbe_(unsigned size) {
    onPMTUDProbeLost_(size);
  }
  void expirePMTUDProbe_() { pmtudExpired_(); }
  unsigned pathProbeSize_() const { return m_path.probeSize(); }
  bool pathProbeRetryPending_() const { return m_path.probeRetryPending(); }
  bool validatingPath_() const { return m_validatingPath.active; }
  bool startPathValidation_(ZiSockAddr local, ZiSockAddr remote) {
    if (!remote || sameAddr_(remote, m_path.remote()))
      return false;
    if (m_validatingPath.active &&
	sameAddr_(remote, m_validatingPath.path.remote()))
      return false;
    return startPathValid_(ZuMv(local), ZuMv(remote), false);
  }
  const ZiSockAddr &validatingRemote_() const {
    return m_validatingPath.path.remote();
  }
  ZuBSpan validatingChallenge_() const {
    return m_validatingPath.active ?
      m_validatingPath.challenge.bspan() : ZuBSpan{};
  }
  bool installAppDataKeys_(
    const TrafficSecret &rx, const TrafficSecret &tx, const CxnID &localCID) {
    m_localSCID = localCID;
    m_linkState = LinkState::Established;
    clearPeerKeyState_();
    return m_crypto.updateRxTrafficSecret(PktNumSpace::AppData, rx) &&
      txInstallTrafficSecret_(PktNumSpace::AppData, tx);
  }
  void discardPeerKeys_() { discardOldPeerKeys_(); }
#endif
  template <typename SendPkt>
  bool sendPathPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::AntiAmp,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return false;
    }
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
      DgramEvent event{.size = bytes, .linkInfo = linkInfo};
      event.ecn = EcnMark::N;
      o.logDgramSent(event, time);
    }));
    return m_path.reserveSend(bytes);
  }
  template <typename SendPkt>
  bool sendPathProbePkt_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::ProbeAdmit,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return false;
    }
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) return false;
    m_path.sent(bytes);
    ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
      DgramEvent event{.size = bytes, .linkInfo = linkInfo};
      event.ecn = EcnMark::N;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  template <typename SendPkt>
  bool sendPathPktApp_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::AntiAmp,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return false;
    }
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), sent)) return false;
    if (!sent) {
      ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::AppSend,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return true;
    }
    ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
      DgramEvent event{.size = bytes, .linkInfo = linkInfo};
      event.ecn = EcnMark::N;
      o.logDgramSent(event, time);
    }));
    return m_path.reserveSend(bytes);
  }
  template <typename SendPkt>
  bool sendPathProbePktApp_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_path.antiAmplificationRemaining(),
		mtu = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::ProbeAdmit,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return false;
    }
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), sent)) return false;
    if (!sent) {
      ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvent{
	    .packetSize = bytes,
	    .reason = PktEvent::Reason::AppSend,
	    .linkInfo = linkInfo
	  },
	  time);
      }));
      return true;
    }
    m_path.sent(bytes);
    ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
      DgramEvent event{.size = bytes, .linkInfo = linkInfo};
      event.ecn = EcnMark::N;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  unsigned txPNLength_(PktNumSpace::T level) const {
    uint64_t largestAckd = m_txLargestAckd[level];
    if (ZuCmp<uint64_t>::null(largestAckd)) return RuntimePNLength;
    return PktNumber::encodedLength(m_txPN[level], largestAckd);
  }
#ifdef Zquic_DEBUG
  void setTxPN_(PktNumSpace::T level, uint64_t pn) {
    m_txPN[level] = pn;
  }
#endif
  void resetRuntime_() {
    disconnecting_(false);
    closeStreamsRx_();
    cancelTimers();
    resetAckPosts_();
    resetLinkState_();
    m_initialDCID = {};
    m_origDCID = {};
    m_groupID = {};
    m_localSCID = {};
    m_peerCID = {};
    m_peerResetToken = {};
    clearCIDState_();
    m_transportParams = {};
    m_txDataCredit = {};
    m_rxDataCredit = {};
    m_lastDataBlocked = U64Null;
    m_lastStreamsBlocked[Zi::StreamType::Duplex] = U64Null;
    m_lastStreamsBlocked[Zi::StreamType::Simplex] = U64Null;
    m_crypto.resetTLS();
    resetPath_();
    app()->txInvoke(impl(), [link = impl()]() mutable {
      if (link->disconnecting_()) return link;
      link->resetTxRuntime_();
      return link;
    });
  }
  void resetTxRuntime_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx runtime reset outside Tx thread", return);
    ++m_txRuntimeGen;
    m_lossTimerOut = {};
    m_ptoTimerOut = {};
    m_ptoTimerLevel = PktNumSpace::Initial;
    m_idleTimeout = {};
    m_idleTimerOut = {};
    m_idleBase = {};
    m_idleAckElicitingSent = false;
    m_closeTimerOut = {};
    m_closeNextResponse = {};
    clearPendingControls_();
    resetPktRuntime_();
    m_congestion = NewReno{app()->maxUDP()};
    updateCongestionDiag_();
  }

  void closeRuntime_(uint64_t = 0) {
    closeStreamsRx_();
    cancelTimers();
    closeLinkState_();
    m_crypto.resetTLS();
  }

  void resetTLS_() {
    m_crypto.resetTLS();
  }

  void endpointReady_() { ++m_rxDiag.endpointReady; }
  void endpointFailure_() { ++m_rxDiag.failures; }
  void packetParseFailure_() { ++m_rxDiag.failures; }
	  void tlsFailure_() {
	    ++m_rxDiag.failures;
	    ZquicLOG(app()->qlogTrace(), ([linkInfo = linkInfo_()](auto &o, ZuTime time) {
	      SecEvent event{
		.kind = SecKind::TLS,
		.reason = SecReason::Handshake,
		.success = false,
		.linkInfo = linkInfo
	      };
      o.logTLSAlert(event, time);
    }));
  }
  void handshakeDoneTx_() { }
  void newTokenRx_() { ++m_rxDiag.newTokenRx; }
  void newTokenTx_() { ++m_txDiag.newTokenTx; }
  void pathPromoted_() { }
  void dataBlocked_(uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zi::StreamType::Duplex, maximum);
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zi::StreamType::Duplex, maximum);
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
  }
  void streamOpen(StreamRef, bool) { ++m_rxDiag.unhandledAppEvents; }
  void streamData(
    StreamRef, uint64_t, ZuBSpan, bool) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamResetReceived(
    StreamRef, uint64_t, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamResetSent(uint64_t, uint64_t, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void streamStopSendingReceived(StreamRef, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void streamStopSendingSent(uint64_t, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void flowBlocked(
    FrameType::T, uint64_t, Zi::StreamType::T, uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void transportClose(FrameType::T, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }
  void pathUpdate(
    const ZiSockAddr &, const ZiSockAddr &, bool, unsigned) {
    ++m_txDiag.unhandledAppEvents;
  }
  void migrationFailure(const ZiSockAddr &, const ZiSockAddr &) {
    ++m_txDiag.unhandledAppEvents;
  }
  void streamFrame(
    uint64_t, uint64_t, ZuBSpan, bool) { }
  void retiredLocalCID_(uint64_t, const CxnID &) { }
  void statelessReset() { ++m_rxDiag.unhandledAppEvents; }
  void disconnected(bool peer) {
    app()->rxRun([link = impl(), peer]() {
      link->Self::disconnected_0(peer);
    });
  }
  void disconnected_0(bool peer) {
    auto mx = app()->mx();
    if (mx && mx->running() && app()->txThread()) {
      ZiAssert(rxInvoked_(), "Zquic", (),
	"QUIC link disconnect completion outside Rx thread", return);
      closeStreamsRx_();
      closedLinkState_();
      m_crypto.resetTLS();
      app()->retireLinkRoutes_(impl());
      app()->txRun([link = impl(), peer]() mutable {
	link->app()->rxRun([link, peer]() mutable {
	  link->Self::disconnected_(peer);
	});
      });
      return;
    } else {
      closedLinkState_();
      m_crypto.resetTLS();
    }
  }
  void disconnected_(bool peer) {
    app()->disconnected(impl(), peer);
  }

  void setRuntimeCIDs_(
    const CxnID &initialDCID,
    const CxnID &localSCID,
    const CxnID &peerCID) {
    setRuntimeCIDs_(initialDCID, localSCID, peerCID, initialDCID, initialDCID);
  }

  void setRuntimeCIDs_(
    const CxnID &initialDCID,
    const CxnID &localSCID,
    const CxnID &peerCID,
    const CxnID &origDCID,
    const CxnID &groupID) {
    m_origDCID = origDCID;
    m_groupID = groupID;
    m_initialDCID = initialDCID;
    m_localSCID = localSCID;
    m_peerCID = peerCID;
    addLocalCID_(localSCID, 0);
    addPeerCID_(peerCID, 0);
    if (auto cid = findCID_(m_peerCIDs, peerCID))
      cid->associated = true;
  }

  void setPeerCIDFromHdrSCID_(const LongHdr &h) {
    if (h.scid) m_peerCID = h.scid;
  }

  void setPeerResetToken_(const ResetToken &token) {
    m_peerResetToken = token;
    if (m_peerCID)
      addPeerCID_(m_peerCID, 0, token);
  }

  bool addLocalCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID_(m_localCIDs, id, sequence, resetToken, true);
  }
  bool addPeerCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID_(m_peerCIDs, id, sequence, resetToken, false);
  }
  LinkCID *localCID_(uint64_t sequence) {
    return findCID_(m_localCIDs, sequence);
  }
  const LinkCID *localCID_(uint64_t sequence) const {
    return findCID_(m_localCIDs, sequence);
  }
  const LinkCID *peerCID_(uint64_t sequence) const {
    return findCID_(m_peerCIDs, sequence);
  }
  bool retireLocalCID_(uint64_t sequence, CxnID &id) {
    LinkCID *cid = localCID_(sequence);
    if (!cid || cid->state == CxnState::Tombstone) return false;
    id = cid->id;
    cid->state = CxnState::Retired;
    cid->associated = false;
    ZquicLOG(app()->qlogTrace(), ([
      action = CIDAction::Retired,
      reason = CIDReason::PeerRequest,
      cxnID = cid->id,
	      sequence = cid->sequence,
	      length = qlogCount_(cid->id.length()),
	      local = true,
	      associated = cid->associated,
	      resetToken = cid->resetToken.valid(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      CIDEvent event{
	        .kind = CIDKind::CxnID,
        .action = CIDAction::T(action),
        .reason = CIDReason::T(reason),
	.cxnID = cxnID,
	.sequence = sequence,
		.length = length,
		.local = local,
		.associated = associated,
		.resetToken = resetToken,
		.linkInfo = linkInfo
	      };

      o.logCIDUpdated(event, time);
    }));
    return true;
  }
  bool receiveNewCxnID_(const Frame &frame) {
    if (frame.type != FrameType::NewCxnID) return false;
    if (!frame.length || frame.length > CxnIDMax || !frame.resetToken.valid())
      return false;
    if (frame.offset > frame.value) return false;
    CxnID id{frame.payload};
    if (!id) return false;
    if (frame.offset > m_peerRetirePriorTo)
      retirePeerCIDsPriorTo_(frame.offset);
    if (frame.value < m_peerRetirePriorTo)
      return true;
    return addPeerCID_(id, frame.value, frame.resetToken);
  }
  bool receiveRetireCxnID_(const Frame &frame) {
    if (frame.type != FrameType::RetireCxnID) return false;
    CxnID id;
    if (!retireLocalCID_(frame.value, id)) return false;
    impl()->retiredLocalCID_(frame.value, id);
    return true;
  }
  void selectPathCID_(PathState &state) {
    state.peerCID = m_peerCID;
    for (auto &cid : m_peerCIDs) {
      if (cid.state != CxnState::Active || cid.associated)
	continue;
      state.peerCID = cid.id;
      state.peerSeq = cid.sequence;
      return;
    }
    if (auto cid = findCID_(m_peerCIDs, m_peerCID))
      state.peerSeq = cid->sequence;
  }
  void bindPromotedCID_() {
    for (auto &cid : m_peerCIDs)
      cid.associated = false;
    if (!m_validatingPath.peerCID) return;
    if (auto cid = findCID_(m_peerCIDs, m_validatingPath.peerSeq)) {
      if (cid->id == m_validatingPath.peerCID) {
	cid->associated = true;
	m_peerCID = cid->id;
	ZquicLOG(app()->qlogTrace(), ([
	  action = CIDAction::RouteBound,
	  reason = CIDReason::PathPromoted,
	  cxnID = cid->id,
	  sequence = cid->sequence,
		  length = qlogCount_(cid->id.length()),
		  local = false,
		  associated = cid->associated,
		  resetToken = cid->resetToken.valid(),
		  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
		  CIDEvent event{
	    .kind = CIDKind::CxnID,
	    .action = CIDAction::T(action),
	    .reason = CIDReason::T(reason),
	    .cxnID = cxnID,
	    .sequence = sequence,
		    .length = length,
		    .local = local,
		    .associated = associated,
		    .resetToken = resetToken,
		    .linkInfo = linkInfo
		  };

	  o.logCIDUpdated(event, time);
	}));
      }
    }
  }
  ZuTime pathValidDeadline_() const {
    ZuTime timeout = ptoTimeout_();
    int64_t usec = timeout.microsecs();
    if (usec < 1000000) usec = 1000000;
    if (usec > int64_t(-1) / 3) usec = int64_t(-1) / 3;
    return runtimeNow_() + timeUS(uint64_t(usec) * 3);
  }
  template <typename Routes>
  void installLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state != CxnState::Active || cid.associated) continue;
      if (!routes.add(cid.id, cid.sequence, impl(), cid.resetToken)) continue;
      cid.associated = true;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::RouteBound,
	reason = CIDReason::RouteInstall,
	cxnID = cid.id,
	sequence = cid.sequence,
		length = qlogCount_(cid.id.length()),
		local = true,
		associated = cid.associated,
		resetToken = cid.resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvent event{
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .cxnID = cxnID,
	  .sequence = sequence,
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken,
		  .linkInfo = linkInfo
		};

	o.logCIDUpdated(event, time);
      }));
    }
  }
  template <typename Routes>
  void retireLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.retire(cid.id);
      cid.associated = false;
      cid.state = CxnState::Retired;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Retired,
	reason = CIDReason::RouteRetire,
	cxnID = cid.id,
	sequence = cid.sequence,
		length = qlogCount_(cid.id.length()),
		local = true,
		associated = cid.associated,
		resetToken = cid.resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvent event{
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .cxnID = cxnID,
	  .sequence = sequence,
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken,
		  .linkInfo = linkInfo
		};

	o.logCIDUpdated(event, time);
      }));
    }
  }
  template <typename Routes>
  void tombstoneLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.tombstone(cid.id);
      cid.associated = false;
      cid.state = CxnState::Tombstone;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Tombstone,
	reason = CIDReason::RouteTombstone,
	cxnID = cid.id,
	sequence = cid.sequence,
		length = qlogCount_(cid.id.length()),
		local = true,
		associated = cid.associated,
		resetToken = cid.resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvent event{
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .cxnID = cxnID,
	  .sequence = sequence,
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken,
		  .linkInfo = linkInfo
		};

	o.logCIDUpdated(event, time);
      }));
    }
  }

  void clearCIDState_() {
    m_localCIDs.clear();
    m_peerCIDs.clear();
    m_peerRetirePriorTo = 0;
  }
  template <typename CIDs>
  static LinkCID *findCID_(CIDs &cids, uint64_t seq) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static const LinkCID *findCID_(const CIDs &cids, uint64_t seq) {
    for (const auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static LinkCID *findCID_(CIDs &cids, const CxnID &id) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.id == id)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  bool addCID_(
    CIDs &cids, const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken, bool local) {
    if (!id) return false;
    if (!local && !peerCIDTokenUnique_(cids, id, resetToken)) return false;
    if (auto cid = findCID_(cids, sequence)) {
      if (!(cid->id == id)) return false;
      if (resetToken.valid() && cid->resetToken.valid() &&
	  !(cid->resetToken == resetToken))
	return false;
      cid->resetToken = resetToken;
      cid->state = CxnState::Active;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Updated,
	reason = CIDReason::Sequence,
	cxnID = cid->id,
	sequence = cid->sequence,
		length = qlogCount_(cid->id.length()),
		local,
		associated = cid->associated,
		resetToken = cid->resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvent event{
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .cxnID = cxnID,
	  .sequence = sequence,
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken,
		  .linkInfo = linkInfo
		};

	o.logCIDUpdated(event, time);
      }));
      return true;
    }
    if (auto cid = findCID_(cids, id)) {
      if (cid->sequence != sequence) return false;
      if (resetToken.valid() && cid->resetToken.valid() &&
	  !(cid->resetToken == resetToken))
	return false;
      cid->resetToken = resetToken;
      cid->state = CxnState::Active;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Updated,
	reason = CIDReason::ID,
	cxnID = cid->id,
	sequence = cid->sequence,
		length = qlogCount_(cid->id.length()),
		local,
		associated = cid->associated,
		resetToken = cid->resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvent event{
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .cxnID = cxnID,
	  .sequence = sequence,
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken,
		  .linkInfo = linkInfo
		};

	o.logCIDUpdated(event, time);
      }));
      return true;
    }
    unsigned active = 0;
    LinkCID *slot = nullptr;
    for (auto &cid : cids) {
      if (cid.state == CxnState::Active) ++active;
      if (!slot && cid.state != CxnState::Active) slot = &cid;
    }
    uint64_t limit = local ?
      LocalActiveCxnIDLimit :
      m_transportParams.activeCxnIDLimit;
    if (active >= limit)
      return false;
    if (!slot && cids.length() < limit)
      slot = cids.push();
    if (!slot) return false;
    *slot = LinkCID{id, sequence, resetToken, CxnState::Active, false};
    ZquicLOG(app()->qlogTrace(), ([
      action = CIDAction::Issued,
      reason = resetToken.valid() ? CIDReason::ResetToken : CIDReason::None,
      cxnID = slot->id,
      sequence = slot->sequence,
	      length = qlogCount_(slot->id.length()),
	      local,
	      associated = slot->associated,
	      resetToken = slot->resetToken.valid(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      CIDEvent event{
        .kind = CIDKind::CxnID,
        .action = CIDAction::T(action),
        .reason = CIDReason::T(reason),
	.cxnID = cxnID,
	.sequence = sequence,
		.length = length,
		.local = local,
		.associated = associated,
		.resetToken = resetToken,
		.linkInfo = linkInfo
	      };

      o.logCIDUpdated(event, time);
    }));
    return true;
  }
  template <typename CIDs>
  bool peerCIDTokenUnique_(
    const CIDs &cids, const CxnID &id, const ResetToken &resetToken) const {
    if (!resetToken.valid()) return true;
    for (const auto &cid : cids) {
      if (cid.state == CxnState::Tombstone ||
	  !cid.resetToken.valid() || !(cid.resetToken == resetToken))
	continue;
      if (!(cid.id == id)) return false;
    }
    return true;
  }
  void retirePeerCIDsPriorTo_(uint64_t sequence) {
    m_peerRetirePriorTo = sequence;
    for (auto &cid : m_peerCIDs)
      if (cid.state == CxnState::Active && cid.sequence < sequence) {
	cid.state = CxnState::Retired;
	ZquicLOG(app()->qlogTrace(), ([
	  action = CIDAction::Retired,
	  reason = CIDReason::RetirePrior,
	  cxnID = cid.id,
	  sequence = cid.sequence,
		  length = qlogCount_(cid.id.length()),
		  local = false,
		  associated = cid.associated,
		  resetToken = cid.resetToken.valid(),
		  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
		  CIDEvent event{
	    .kind = CIDKind::CxnID,
	    .action = CIDAction::T(action),
	    .reason = CIDReason::T(reason),
	    .cxnID = cxnID,
	    .sequence = sequence,
		    .length = length,
		    .local = local,
		    .associated = associated,
		    .resetToken = resetToken,
		    .linkInfo = linkInfo
		  };

	  o.logCIDUpdated(event, time);
	}));
      }
  }

  const CxnID &runtimeCID_(RuntimeCID::T cid) const {
    switch (cid) {
      case RuntimeCID::Initial: return m_initialDCID;
      case RuntimeCID::Local: return m_localSCID;
      default: return m_peerCID;
    }
  }

  Zquic::LinkInfo linkInfo_() const {
    return Zquic::LinkInfo{
      .origDCID = m_origDCID.length() ? m_origDCID : m_initialDCID,
      .groupID = m_groupID.length() ? m_groupID : m_initialDCID,
      .dcid = m_initialDCID,
      .scid = m_localSCID
    };
  }

  template <typename AppLike>
  void configureLocalTransportParams_(AppLike *app) {
    m_transportParams.initialSCID = m_localSCID;
    m_transportParams.maxUDPPayloadSize = app->maxUDP();
    m_transportParams.initialMaxData = app->maxData();
    m_transportParams.initialMaxStreamDataBidiLocal =
      app->maxStreamData();
    m_transportParams.initialMaxStreamDataBidiRemote =
      app->maxStreamData();
    m_transportParams.initialMaxStreamDataUni = app->maxStreamData();
    m_transportParams.initialMaxStreamsBidi = app->maxStreamsBidi();
    m_transportParams.initialMaxStreamsUni = app->maxStreamsUni();
    m_transportParams.maxIdleTimeout = app->maxIdleTimeout();
    m_transportParams.activeCxnIDLimit = LocalActiveCxnIDLimit;
    m_rxDataCredit.set(m_transportParams.initialMaxData);
    m_localLimit[Zi::StreamType::Duplex].set(
      m_transportParams.initialMaxStreamsBidi);
    m_localLimit[Zi::StreamType::Simplex].set(
      m_transportParams.initialMaxStreamsUni);
  }

  bool loadServerTransportParams_(const ServerBootstrap &bootstrap) {
    if (bootstrap.transportParams(m_transportParams)) return true;
    tlsFailure_();
    return false;
  }

  bool deriveInitial_() {
    if (m_crypto.deriveInitial(m_initialDCID)) return true;
    tlsFailure_();
    return false;
  }

  bool initTLS_(CryptoConfig config) {
    config.localTransportParams = &m_transportParams;
    if (m_crypto.initTLS(config)) {
      ZquicLOG(app()->qlogTrace(), ([
	origDCID = m_transportParams.origDCID,
	initialSCID = m_transportParams.initialSCID,
	retrySCID = m_transportParams.retrySCID,
	statelessResetToken = m_transportParams.statelessResetToken,
	statelessResetTokenPresent =
	  m_transportParams.statelessResetTokenPresent,
	maxIdleTimeout = m_transportParams.maxIdleTimeout,
	maxUDPPayloadSize = m_transportParams.maxUDPPayloadSize,
	ackDelayExponent = m_transportParams.ackDelayExponent,
	maxAckDelay = m_transportParams.maxAckDelay,
	activeCxnIDLimit = m_transportParams.activeCxnIDLimit,
	initialMaxData = m_transportParams.initialMaxData,
	initialMaxStreamDataBidiLocal =
	  m_transportParams.initialMaxStreamDataBidiLocal,
	initialMaxStreamDataBidiRemote =
	  m_transportParams.initialMaxStreamDataBidiRemote,
		initialMaxStreamDataUni = m_transportParams.initialMaxStreamDataUni,
		initialMaxStreamsBidi = m_transportParams.initialMaxStreamsBidi,
		initialMaxStreamsUni = m_transportParams.initialMaxStreamsUni,
		disableActiveMigration = m_transportParams.disableActiveMigration,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	ParamsEvent event{
	  .initiator = Initiator::Local,
	  .origDCID = origDCID,
	  .initialSCID = initialSCID,
	  .retrySCID = retrySCID,
	  .statelessResetToken = statelessResetToken,
	  .maxIdleTimeout = maxIdleTimeout,
	  .maxUDPPayloadSize = maxUDPPayloadSize,
	  .ackDelayExponent = ackDelayExponent,
	  .maxAckDelay = maxAckDelay,
	  .activeCxnIDLimit = activeCxnIDLimit,
	  .initialMaxData = initialMaxData,
	  .initialMaxStreamDataBidiLocal = initialMaxStreamDataBidiLocal,
	  .initialMaxStreamDataBidiRemote = initialMaxStreamDataBidiRemote,
	  .initialMaxStreamDataUni = initialMaxStreamDataUni,
	  .initialMaxStreamsBidi = initialMaxStreamsBidi,
		  .initialMaxStreamsUni = initialMaxStreamsUni,
		  .statelessResetTokenPresent = statelessResetTokenPresent,
		  .disableActiveMigration = disableActiveMigration,
		  .linkInfo = linkInfo
		};
	o.logParamsSet(event, time);
      }));
      return true;
    }
    tlsFailure_();
    return false;
  }

  bool startRuntimeHandshake_() {
    return startHandshakeState_();
  }

  bool runtimeReadyToEstablish_() const {
    return m_linkState == LinkState::Handshaking &&
      m_crypto.oneRTTReady() &&
      m_crypto.txTrafficSecretInstalled(PktNumSpace::AppData) &&
      m_crypto.rxTrafficSecretInstalled(PktNumSpace::AppData);
  }

  bool validateServerTransportParams_(const ClientBootstrap &bootstrap) {
    if (!m_crypto.peerTransportParamsReceived() ||
	!bootstrap.validateServerTransportParams(
	  m_crypto.peerTransportParams(), m_peerCID))
      return false;
    const auto &params = m_crypto.peerTransportParams();
    if (params.statelessResetTokenPresent)
      m_peerResetToken = params.statelessResetToken;
    return true;
  }

  bool validateClientTransportParams_() const {
    return m_crypto.peerTransportParamsReceived() &&
      !m_crypto.peerTransportParams().statelessResetTokenPresent;
  }

  void establishRuntime_() {
    if (m_crypto.peerTransportParamsReceived()) {
      const auto &params = m_crypto.peerTransportParams();
      m_txDataCredit.set(params.initialMaxData);
      m_peerLimit[Zi::StreamType::Duplex].set(params.initialMaxStreamsBidi);
      m_peerLimit[Zi::StreamType::Simplex].set(params.initialMaxStreamsUni);
      ZquicLOG(app()->qlogTrace(), ([
	origDCID = params.origDCID,
	initialSCID = params.initialSCID,
	retrySCID = params.retrySCID,
	statelessResetToken = params.statelessResetToken,
	statelessResetTokenPresent = params.statelessResetTokenPresent,
	maxIdleTimeout = params.maxIdleTimeout,
	maxUDPPayloadSize = params.maxUDPPayloadSize,
	ackDelayExponent = params.ackDelayExponent,
	maxAckDelay = params.maxAckDelay,
	activeCxnIDLimit = params.activeCxnIDLimit,
	initialMaxData = params.initialMaxData,
	initialMaxStreamDataBidiLocal =
	  params.initialMaxStreamDataBidiLocal,
	initialMaxStreamDataBidiRemote =
	  params.initialMaxStreamDataBidiRemote,
		initialMaxStreamDataUni = params.initialMaxStreamDataUni,
		initialMaxStreamsBidi = params.initialMaxStreamsBidi,
		initialMaxStreamsUni = params.initialMaxStreamsUni,
		disableActiveMigration = params.disableActiveMigration,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	ParamsEvent event{
	  .initiator = Initiator::Remote,
	  .origDCID = origDCID,
	  .initialSCID = initialSCID,
	  .retrySCID = retrySCID,
	  .statelessResetToken = statelessResetToken,
	  .maxIdleTimeout = maxIdleTimeout,
	  .maxUDPPayloadSize = maxUDPPayloadSize,
	  .ackDelayExponent = ackDelayExponent,
	  .maxAckDelay = maxAckDelay,
	  .activeCxnIDLimit = activeCxnIDLimit,
	  .initialMaxData = initialMaxData,
	  .initialMaxStreamDataBidiLocal = initialMaxStreamDataBidiLocal,
	  .initialMaxStreamDataBidiRemote = initialMaxStreamDataBidiRemote,
	  .initialMaxStreamDataUni = initialMaxStreamDataUni,
	  .initialMaxStreamsBidi = initialMaxStreamsBidi,
		  .initialMaxStreamsUni = initialMaxStreamsUni,
		  .statelessResetTokenPresent = statelessResetTokenPresent,
		  .disableActiveMigration = disableActiveMigration,
		  .linkInfo = linkInfo
		};
	o.logParamsSet(event, time);
      }));
    }
	    ZquicLOG(app()->qlogTrace(), ([
	      alpn = ZeString{m_crypto.negotiatedProtocol()},
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      if (!alpn) return;
      SecEvent event{
	.kind = SecKind::ALPN,
		.trigger = SecTrigger::Selected,
		.alpn = ZuMv(alpn),
		.success = true,
		.linkInfo = linkInfo
	      };
      o.logALPNInfo(event, time);
    }));
    updatePeerPathMaxUDP_();
    establishState_();
    startIdleTimer_();
    discardPktNumSpace_(PktNumSpace::Initial);
    discardPktNumSpace_(PktNumSpace::Handshake);
    ++m_rxDiag.handshakeComplete;
  }

  auto negotiatedProtocol_() const {
    return m_crypto.negotiatedProtocol();
  }

  void resetPktRuntime_() {
    cancelTimers();
    for (auto &s : m_txCrypto) s.reset();
    for (auto &s : m_rxCrypto) s.reset();
    for (auto &p : m_txProt) p.clear();
    memset(m_txPN, 0, sizeof(m_txPN));
    for (auto &pn : m_txLargestAckd) pn = U64Null;
    memset(m_rxLargestPN, 0, sizeof(m_rxLargestPN));
    m_rxAcks.clear();
    clearPeerKeyState_();
    for (auto &ack : m_txAck) ack = {};
    for (auto &ecn : m_peerAckECN) ecn.reset();
    for (auto &p : m_txPkts) p.clear();
    m_coalesceInitial = nullptr;
    m_coalesceAddr = {};
    m_coalesceLong = false;
    memset(m_rxSpaceDiscarded, 0, sizeof(m_rxSpaceDiscarded));
    memset(m_txSpaceDiscarded, 0, sizeof(m_txSpaceDiscarded));
    m_rtt = {};
    m_ptoBackoff.reset();
    m_txKeyPhase = false;
  }

  void closeStreamsRx_() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream table Rx close outside Rx thread", return);
    auto i = m_streams->iter();
    while (auto stream = i()) stream->closeRx_();
  }

  void clearPeerKeyState_() {
    m_rxOldTrafficSecret.clear();
    m_rxOldProt.clear();
    m_rxNextTrafficSecret.clear();
    m_rxNextProt.clear();
    m_rxOldKeyDiscard = {};
    m_rxKeyPhase = false;
    m_rxOldKeyPhase = false;
  }
  bool ensureNextPeerKey_() {
    if (m_rxNextProt.valid()) return true;
    if (!PktProt::deriveNextTrafficSecret(
	  m_rxNextTrafficSecret,
	  m_crypto.rxTrafficSecret(PktNumSpace::AppData)))
      return false;
    if (!m_rxNextProt.init(
	  m_rxNextTrafficSecret, PktNumSpace::AppData, false)) {
      m_rxNextTrafficSecret.clear();
      return false;
    }
    return true;
  }
  bool commitPeerKeyUpdate_(const TrafficSecret &nextRxSecret) {
    m_rxOldTrafficSecret = m_crypto.rxTrafficSecret(PktNumSpace::AppData);
    if (!m_rxOldProt.init(
	  m_rxOldTrafficSecret, PktNumSpace::AppData, false)) {
      m_rxOldTrafficSecret.clear();
      return false;
    }
    m_rxOldKeyPhase = m_rxKeyPhase;
    if (!m_crypto.updateRxTrafficSecret(PktNumSpace::AppData, nextRxSecret))
      return false;
    m_rxKeyPhase = !m_rxKeyPhase;
    m_rxNextTrafficSecret.clear();
    m_rxNextProt.clear();
    m_rxOldKeyDiscard = keyDiscardDeadline_();
    ++m_rxDiag.peerKeyUpdates;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      keyPhase = uint64_t(m_rxKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvent event{
	.kind = SecKind::KeyUpdated,
	.packetSpace = level,
	.keyType = SecKeyType::RX,
	.trigger = SecTrigger::Remote,
		.reason = SecReason::KeyPhase,
		.value = keyPhase,
		.success = true,
		.linkInfo = linkInfo
	      };
      o.logKeyUpdated(event, time);
    }));
    schedulePeerKeyDiscard_(m_rxOldKeyDiscard);
    app()->txInvoke(impl(), [link = impl()]() mutable {
      if (link->disconnecting_()) return link;
      link->txInstallPeerKeyUpdate_();
      return link;
    });
    return true;
  }
  void schedulePeerKeyDiscard_(ZuTime deadline) {
    app()->txRun([link = impl(), deadline]() mutable {
      if (link->disconnecting_()) return;
      link->scheduleKeyDiscardTimer_(deadline);
    });
  }
  void discardOldPeerKeys_() {
    if (!m_rxOldProt.valid()) return;
    m_rxOldTrafficSecret.clear();
    m_rxOldProt.clear();
    m_rxOldKeyDiscard = {};
    ++m_rxDiag.keyDiscards;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      oldKeyPhase = uint64_t(m_rxOldKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvent event{
	.kind = SecKind::KeyRetired,
	.packetSpace = level,
	.keyType = SecKeyType::RXOld,
	.trigger = SecTrigger::Remote,
		.reason = SecReason::KeyUpdate,
		.value = oldKeyPhase,
		.success = true,
		.linkInfo = linkInfo
	      };
      o.logKeyRetired(event, time);
    }));
  }
  bool txInstallPeerKeyUpdate_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer key update outside Tx thread", return false);
    TrafficSecret nextTxSecret;
    if (!PktProt::deriveNextTrafficSecret(
	  nextTxSecret, txTrafficSecret_(PktNumSpace::AppData)) ||
	!txInstallTrafficSecret_(PktNumSpace::AppData, nextTxSecret))
      return false;
    m_txKeyPhase = !m_txKeyPhase;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      keyPhase = uint64_t(m_txKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvent event{
	.kind = SecKind::KeyUpdated,
	.packetSpace = level,
	.keyType = SecKeyType::TX,
	.trigger = SecTrigger::Local,
		.reason = SecReason::PeerUpdate,
		.value = keyPhase,
		.success = true,
		.linkInfo = linkInfo
	      };
      o.logKeyUpdated(event, time);
    }));
    return true;
  }

  bool checkStatelessReset_(ZuBSpan datagram, bool notify = false) {
    if (!StatelessRst::verify(datagram, m_peerResetToken)) return false;
	    ZquicLOG(app()->qlogTrace(), ([
	      bytes = datagram.length(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvent event{
		.kind = SecKind::StatelessRst,
		.value = bytes,
		.success = true,
		.linkInfo = linkInfo
	      };
      event.trigger = SecTrigger::Received;
      event.reason = SecReason::TokenMatch;
      o.logSecEvent(EventName::StatelessRst, event, time);
    }));
    m_linkState = LinkState::Draining;
    m_streamQueue.clean();
    for (auto &p : m_txPkts) p.clear();
    m_rxAcks.clear();
    ++m_rxDiag.failures;
    impl()->statelessReset();
    if (notify) impl()->disconnected(true);
    return true;
  }

  template <
    unsigned TLSBufSize_, typename SendFlights,
    typename MarkEstablished, typename AfterEstablished>
  bool advanceTLS_(
    size_t inEpoch, ZuBSpan input, ZiSockAddr addr,
    SendFlights sendFlights, MarkEstablished markEstablished,
    AfterEstablished afterEstablished) {
    ZmRef<ZiIOBuf> out =
      new CryptoTxBufAlloc<TLSBufSize_, TLSBufSize_>{this};
    size_t offsets[5] = {};
    int n =
      m_crypto.handleTLSMessage(out.ptr(), offsets, inEpoch, input);
    if (n < 0) {
      tlsFailure_();
      return false;
    }
    if (!sendFlights(out->data(), unsigned(n), offsets, addr))
      return false;
    markEstablished();
    return afterEstablished(ZuMv(addr));
  }

  StreamRef nextWritableStream_() {
    StreamRef stream = m_streamQueue.head();
    if (!stream) return nullptr;
    m_streamQueue.shift();
    stream->txQueued(false);
    return stream;
  }

  bool streamTxPending_(const StreamRef &stream) const {
    return stream &&
      (stream->controlQueued() ||
	(!stream->resetSent() && (stream->txRangeCount() || stream->finReady())));
  }

  SentFrameRef controlRef_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	return SentFrameRef::flowUpdate(
	  FlowUpdate{frame.type, frame.streamID, frame.value, frame.streamType});
      case FrameType::DataBlocked:
      case FrameType::StreamDataBlocked:
      case FrameType::StreamsBlocked:
	return SentFrameRef::blocked(
	  frame.type, frame.streamID, frame.value, frame.streamType);
      case FrameType::PathChallenge:
	return SentFrameRef::pathChallenge(
	  byteSpan(frame.payload, sizeof(frame.payload)));
      case FrameType::PathResponse:
	return SentFrameRef::pathResponse(
	  byteSpan(frame.payload, sizeof(frame.payload)));
      case FrameType::HandshakeDone:
	return SentFrameRef::handshakeDone();
      case FrameType::ResetStream:
	return SentFrameRef::resetStream(
	  frame.streamID, frame.errorCode, frame.value);
      case FrameType::StopSending:
	return SentFrameRef::stopSending(frame.streamID, frame.errorCode);
      default:
	return SentFrameRef::control();
    }
  }

  ControlFrame controlFrame_(const SentFrameRef &ref) const {
    ControlFrame frame;
    frame.type = ref.controlType;
    frame.streamID = ref.streamID;
    frame.value = ref.value;
    if (ref.controlType == FrameType::ResetStream) {
      frame.errorCode = ref.value;
      frame.value = ref.length;
    } else if (ref.controlType == FrameType::StopSending) {
      frame.errorCode = ref.value;
      frame.value = 0;
    }
    frame.streamType = ref.streamType;
    if (ref.controlType == FrameType::PathChallenge ||
	ref.controlType == FrameType::PathResponse)
      for (unsigned i = 0; i < sizeof(frame.payload); ++i)
	frame.payload[i] = ref.payload[i];
    return frame;
  }

  bool appendControl_(
    const ControlFrame &frame, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    if (!controlStillValid_(frame)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !assembly.addControl(budget, unsigned(n)) ||
	!build.commitScratch(unsigned(n)))
      return false;
    if (!refs.add(controlRef_(frame))) return false;
    sentControls[nSentControls++] = frame;
    return true;
  }

  bool appendQueuedControl_(
    PendingControl &slot, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    if (!slot.queued) return true;
    if (!controlStillValid_(slot.frame)) {
      slot = {};
      return true;
    }
    return appendControl_(
      slot.frame, build, budget, assembly, refs,
      sentControls, nSentControls);
  }

  bool appendQueuedControls_(
    PktBuild &build, PktBudget &budget, PktAssembly &assembly,
    TxPktRefs &refs, ControlFrame *sentControls,
    unsigned &nSentControls) {
    if (refs.count() >= SentPkt::MaxFrames) return true;
    if (!appendQueuedControl_(
	  m_maxDataControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    for (unsigned i = 0; i < 2 && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedControl_(
	    m_maxStreamsControl[i], build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_dataBlockedControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    for (unsigned i = 0; i < 2 && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedControl_(
	    m_streamsBlockedControl[i], build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_pathChallengeControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedControl_(
	  m_handshakeDoneControl, build, budget, assembly, refs,
	  sentControls, nSentControls))
      return false;
    auto iter = m_pathResponses.iter();
    while (refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame = iter();
      if (!frame) break;
      if (!appendControl_(
	    frame, build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
    }
    return true;
  }

  bool appendStreamControls_(
    const StreamRef &stream, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    ControlFrame *sentControls, unsigned &nSentControls) {
    while (stream && refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame;
      if (!stream->nextQueuedControl(frame)) return true;
      if (!stream->controlStillValid(frame)) {
	stream->controlSent(frame);
	continue;
      }
      if (!appendControl_(
	    frame, build, budget, assembly, refs,
	    sentControls, nSentControls))
	return false;
      stream->controlSent(frame);
      return true;
    }
    return true;
  }

  void controlSent_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::MaxData:
	pendingControlSent_(m_maxDataControl, frame);
	break;
      case FrameType::MaxStreams:
	pendingControlSent_(
	  m_maxStreamsControl[streamTypeIndex_(frame.streamType)], frame);
	break;
      case FrameType::DataBlocked:
	pendingControlSent_(m_dataBlockedControl, frame);
	break;
      case FrameType::StreamsBlocked:
	pendingControlSent_(
	  m_streamsBlockedControl[streamTypeIndex_(frame.streamType)], frame);
	break;
      case FrameType::PathChallenge:
	pendingControlSent_(m_pathChallengeControl, frame);
	break;
      case FrameType::PathResponse:
	m_pathResponses.shift();
	break;
      case FrameType::HandshakeDone:
	pendingControlSent_(m_handshakeDoneControl, frame);
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending:
	if (frame.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findStream(int64_t(frame.streamID)))
	    stream->controlSent(frame);
	break;
      default:
	break;
    }
    noteControlDequeued_(frame);
  }

  void pendingControlSent_(PendingControl &slot, const ControlFrame &frame) {
    if (slot.frame == frame) slot.queued = false;
  }
  void clearPendingControl_(PendingControl &slot, const ControlFrame &frame) {
    if (slot.frame == frame) slot = {};
  }
  void controlAckd_(const SentFrameRef &ref) {
    ControlFrame frame = controlFrame_(ref);
    switch (ref.controlType) {
      case FrameType::MaxData:
	clearPendingControl_(m_maxDataControl, frame);
	break;
      case FrameType::MaxStreams:
	clearPendingControl_(
	  m_maxStreamsControl[streamTypeIndex_(ref.streamType)], frame);
	break;
      case FrameType::DataBlocked:
	clearPendingControl_(m_dataBlockedControl, frame);
	break;
      case FrameType::StreamsBlocked:
	clearPendingControl_(
	  m_streamsBlockedControl[streamTypeIndex_(ref.streamType)], frame);
	break;
      case FrameType::PathChallenge:
	clearPendingControl_(m_pathChallengeControl, frame);
	break;
      case FrameType::HandshakeDone:
	clearPendingControl_(m_handshakeDoneControl, frame);
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending:
	if (ref.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findStream(int64_t(ref.streamID)))
	    stream->clearControl(ref);
	break;
      default:
	break;
    }
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedControlPkt_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control packetization outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    PktAssembly assembly;
    TxPktRefs refs;
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned ackBytes = build.bytes() - before;
    if (ackBytes && !budget.add(ackBytes)) return false;
    ControlFrame sentControls[SentPkt::MaxFrames];
    unsigned nSentControls = 0;
    if (!appendQueuedControls_(
	  build, budget, assembly, refs, sentControls, nSentControls))
      return false;
    if (refs.count()) {
      if (!sendPkt(build, ZuMv(addr), refs)) return false;
      for (unsigned i = 0; i < nSentControls; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return false;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushControlAndStreams_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    return flushControlAndStreamsTx_(
      ZuMv(addr),
      [&appendAck](PktBuild &build) { return appendAck(build); },
      [&sendPkt](
	  PktBuild &build, ZiSockAddr addr_,
	  const TxPktRefs &refs) {
	return sendPkt(build, ZuMv(addr_), refs);
      });
  }

  template <typename AppendAck, typename SendPkt>
  bool flushControlAndStreamsTx_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    TxPktRefs refs;
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned ackBytes = build.bytes() - before;
    if (ackBytes && !budget.add(ackBytes)) return false;
    ControlFrame sentControls[SentPkt::MaxFrames];
    unsigned nSentControls = 0;
    if (!appendQueuedControls_(
	  build, budget, assembly, refs, sentControls, nSentControls))
      return false;
    bool blocked = false;
    bool flushQueuedControls = false;
    while (scheduledStreamCount_() && refs.count() < SentPkt::MaxFrames) {
      StreamRef stream = nextWritableStream_();
      if (!stream || !streamTxPending_(stream)) continue;
      if (stream->controlQueued()) {
	if (!appendStreamControls_(
	      stream, build, budget, assembly, refs,
	      sentControls, nSentControls))
	  return false;
	if (streamTxPending_(stream) && stream->id() >= 0)
	  streamWritable_(stream);
	continue;
      }
      if (stream->txRangeCount() && m_txDataCredit.blocked()) {
	streamWritable_(stream);
	queueBlocked_(FrameType::DataBlocked, 0, m_txDataCredit.limit());
	blocked = true;
	break;
      }
      if (stream->txRangeCount() && !stream->txCreditAvailable()) {
	streamWritable_(stream);
	queueBlocked_(
	  FrameType::StreamDataBlocked, uint64_t(stream->id()),
	  stream->txCreditLimit());
	blocked = true;
	break;
      }
      StreamFrameInfo info;
      int n = StreamPktizer::writeNext(
	build.scratch(), build.scratchAvail(),
	budget, assembly, *stream, &info);
      if (n <= 0) {
	if (stream->id() >= 0) streamWritable_(stream);
	break;
      }
      if (!build.commitScratch(unsigned(n)) || !build.add(info.range))
	return false;
      SentFrameRef ref =
	SentFrameRef::stream(info.streamID, info.range, info.fin);
      if (!info.length) {
	ref.offset = info.offset;
	ref.length = 0;
      }
      if (!refs.add(ref, stream.ptr()))
	return false;
      if (info.length && !m_txDataCredit.consume(info.length)) return false;
      m_txDiag.streamBytesTx += info.length;
      ZquicLOG(app()->qlogTrace(), ([
		streamID = info.streamID,
		offset = info.offset,
		length = info.length,
		fin = info.fin,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	if (!length && !fin) return;
	StreamDataEvent event{
	  .from = StreamDataLoc::Transport,
	  .to = StreamDataLoc::Network,
	  .additionalInfo = fin ?
	    StreamDataInfo::T(StreamDataInfo::FinSet) :
	    StreamDataInfo::T(StreamDataInfo::None),
		  .streamID = streamID,
		  .offset = offset,
		  .length = length,
		  .linkInfo = linkInfo
		};
	o.logStreamDataMoved(event, time);
      }));
      flushQueuedControls |= returnStreamCredit_(stream);
      if (streamTxPending_(stream) && stream->id() >= 0)
	streamWritable_(stream);
    }
    if (refs.count()) {
      if (!sendPkt(build, ZuMv(addr), refs)) return false;
      for (unsigned i = 0; i < nSentControls; ++i)
	controlSent_(sentControls[i]);
      if (flushQueuedControls) impl()->flushTx_();
      return true;
    }
    return blocked;
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedStreamPkt_(
    StreamRef stream, ZiSockAddr addr, AppendAck appendAck,
    SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream packetization outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    if (stream->txRangeCount() && m_txDataCredit.blocked()) {
      streamWritable_(stream);
      queueBlocked_(FrameType::DataBlocked, 0, m_txDataCredit.limit());
      return false;
    }
    if (stream->txRangeCount() && !stream->txCreditAvailable()) {
      streamWritable_(stream);
      uint64_t limit = stream->txCreditLimit();
      queueBlocked_(FrameType::StreamDataBlocked, uint64_t(stream->id()), limit);
      return false;
    }
    unsigned before = build.bytes();
    if (!appendAck(build)) return false;
    unsigned controlBytes = build.bytes() - before;
    if (controlBytes && !assembly.addControl(budget, controlBytes))
      return false;
    StreamFrameInfo info;
    int n = StreamPktizer::writeNext(
      build.scratch(), build.scratchAvail(),
      budget, assembly, *stream, &info);
    if (n <= 0 || !build.commitScratch(unsigned(n)))
      return false;
    if (!build.add(info.range)) return false;
    SentFrameRef ref = SentFrameRef::stream(info.streamID, info.range, info.fin);
    if (!info.length) {
      ref.offset = info.offset;
      ref.length = 0;
    }
    TxPktRefs refs;
    if (!refs.add(ref, stream.ptr())) return false;
    if (!sendPkt(build, ZuMv(addr), refs)) return false;
    if (info.length && !m_txDataCredit.consume(info.length)) return false;
    m_txDiag.streamBytesTx += info.length;
    ZquicLOG(app()->qlogTrace(), ([
	      streamID = info.streamID,
	      offset = info.offset,
	      length = info.length,
	      fin = info.fin,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      if (!length && !fin) return;
      StreamDataEvent event{
	.from = StreamDataLoc::Transport,
	.to = StreamDataLoc::Network,
	.additionalInfo = fin ?
	  StreamDataInfo::T(StreamDataInfo::FinSet) :
	  StreamDataInfo::T(StreamDataInfo::None),
		.streamID = streamID,
		.offset = offset,
		.length = length,
		.linkInfo = linkInfo
	      };
      o.logStreamDataMoved(event, time);
    }));
    if (returnStreamCredit_(stream)) impl()->flushTx_();
    return true;
  }

  uint64_t localMaxAckDelayUS_() const {
    return uint64_t(m_transportParams.maxAckDelay) * 1000;
  }

  uint64_t nowUS_() const {
    return uint64_t(runtimeNow_().microsecs());
  }

  bool immediateAck_(PktNumSpace::T level) const {
    if (level != PktNumSpace::AppData) return true;
    const AckTracker &tracker = m_rxAcks.tracker(level);
    return tracker.multipleRanges();
  }

  void postAckSnapshot_(PktNumSpace::T level, ZiSockAddr addr) {
    PktNumSpace::T space = level;
    if (!m_rxAcks.pending(space)) return;
    AckSnapshot ack;
    ack.level = level;
    ack.gen = m_rxAcks.gen(space);
    ack.largestRxTime = m_rxAcks.largestRxTime(space);
    ack.due = m_rxAcks.immediate(space);
    ack.ecn = m_rxAcks.ackECN(space);
    const AckTracker &tracker = m_rxAcks.tracker(space);
    int nRanges = tracker.snapshot(ack.ranges, Frame::MaxAckRanges);
    if (nRanges < 0) return;
    ack.nRanges = unsigned(nRanges);
    bool deadline = m_rxAcks.deadlineSet(space);
    uint64_t deadlineUS = deadline ? m_rxAcks.deadline(space) : 0;
    bool post;
    AckPost &ackPost = m_ackPost[level];
    {
      ZmGuard guard(ackPost.lock);
      post = !ackPost.posted;
      if (post && !m_rxAcks.post(space)) return;
      ackPost.ack = ack;
      ackPost.addr = ZuMv(addr);
      ackPost.deadline = deadline;
      ackPost.deadlineUS = deadlineUS;
      ackPost.ready = true;
      if (post) ackPost.posted = true;
    }
    if (!post) return;
    ++m_rxDiag.ackSnapshotPostsRx;
    app()->txRun([link = impl(), level]() mutable {
      if (link->disconnecting_()) return;
      link->consumeAckSnapshotTx_(level);
    });
  }

  void noteAck_(
    PktNumSpace::T level, uint64_t pn, bool ackEliciting, ZiSockAddr addr,
    bool forceImmediate = false, EcnMark::T ecn = EcnMark::NotECT) {
    PktNumSpace::T space = level;
    uint64_t now = nowUS_();
    bool immediate = ackEliciting && (forceImmediate || immediateAck_(level));
    if (!m_rxAcks.received(
	space, pn, now, localMaxAckDelayUS_(), ackEliciting, immediate, ecn))
      return;
    ++m_rxDiag.ackCommitsRx;
    if (ackEliciting) ++m_rxDiag.ackElicitingRx;
    if (immediate) ++m_rxDiag.ackImmediateRx;
    noteRxECN_(level, ecn);
    if (pn > m_rxLargestPN[level])
      m_rxLargestPN[level] = pn;
    postAckSnapshot_(level, ZuMv(addr));
    if (level == PktNumSpace::AppData && runtimeEstablished_())
      notePeerPacketProcessed_();
  }

  bool noteAckTx_(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK snapshot install outside Tx thread", return false);
    bool due = m_txAck[ack.level].due || ack.due;
    m_txAck[ack.level] = ack;
    m_txAck[ack.level].due = due;
    ++m_txDiag.ackSnapshotInstallsTx;
    if (due) ++m_txDiag.ackDueInstallsTx;
    return due;
  }

  void resetAckPosts_() {
    for (unsigned i = 0; i < 3; ++i) {
      AckPost &ackPost = m_ackPost[i];
      ZmGuard guard(ackPost.lock);
      ackPost.ack = {};
      ackPost.addr.null();
      ackPost.deadlineUS = 0;
      ackPost.deadline = false;
      ackPost.posted = false;
      ackPost.ready = false;
    }
  }

  void consumeAckSnapshotTx_(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK snapshot consume outside Tx thread", return);
    AckSnapshot ack;
    ZiSockAddr addr;
    bool deadline = false;
    uint64_t deadlineUS = 0;
    AckPost &ackPost = m_ackPost[level];
    {
      ZmGuard guard(ackPost.lock);
      if (!ackPost.ready) {
	ackPost.posted = false;
	return;
      }
      ack = ackPost.ack;
      addr = ZuMv(ackPost.addr);
      deadline = ackPost.deadline;
      deadlineUS = ackPost.deadlineUS;
      ackPost.ready = false;
      ackPost.posted = false;
    }
    if (noteAckTx_(ack)) {
      cancelAckDelayTimer_();
      impl()->flushTx_(ZuMv(addr));
    } else if (deadline) {
      scheduleAckDelayTimer_(timeUS(deadlineUS));
    }
  }

  bool appendPendingAck_(
    PktNumSpace::T level, PktBuild &build, bool ackOnly = false) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK append outside Tx thread", return false);
    AckSnapshot &ack = m_txAck[level];
    ++m_txDiag.ackAppendTx;
    if (!ack.nRanges) {
      ++m_txDiag.ackAppendEmptyTx;
      return true;
    }
    if (ackOnly && !ack.due) {
      ++m_txDiag.ackAppendNotDueTx;
      return true;
    }
    uint64_t delay = 0;
    if (level == PktNumSpace::AppData) {
      uint64_t now = nowUS_();
      if (now > ack.largestRxTime)
	delay = (now - ack.largestRxTime) >>
	  m_transportParams.ackDelayExponent;
    }
    int n = !m_path.ecnDisabled() && ack.ecn.any() ?
      FrameCodec::writeAckECN(
	build.scratch(), build.scratchAvail(), ack.ranges, ack.nRanges,
	delay, ack.ecn) :
      FrameCodec::writeAckRanges(
	build.scratch(), build.scratchAvail(), ack.ranges, ack.nRanges,
	delay);
    if (n < 0) return false;
    if (!build.commitScratch(unsigned(n))) return false;
    build.markAck(unsigned(level));
    return true;
  }

  void ackSentTx_(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK commit outside Tx thread", return);
    AckSnapshot ack = m_txAck[level];
    if (!ack.nRanges) return;
    ++m_txDiag.ackSentTx;
    m_txAck[level].nRanges = 0;
    m_txAck[level].due = false;
    app()->rxRun([link = impl(), level, gen = ack.gen]() mutable {
      if (link->disconnecting_()) return;
      link->ackSentRx_(level, gen);
    });
  }

  void ackSentRx_(PktNumSpace::T level, uint64_t gen) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive-state commit outside Rx thread", return);
    m_rxAcks.sent(level, gen);
  }

  void scheduleAckDelayTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "ACK delay", CxnTimer::AckDelay,
      out, ZmScheduler::Advance, &m_ackDelayTimer);
  }
  void cancelAckDelayTimer_() { cancelTimer_("ACK delay", &m_ackDelayTimer); }

  void scheduleLossTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "loss time", CxnTimer::Loss,
      out, ZmScheduler::Update, &m_lossTimer);
  }
  ZuTime lossThreshold_() const { return m_rtt.timeThreshold(); }
  ZuTime persistentCongestionThreshold_() const {
    return ptoTimeout_() * ZuDecimal{3};
  }
  ZuTime nextLossTime_() const {
    ZuTime threshold = lossThreshold_();
    ZuTime out;
    bool have = false;
    for (unsigned i = 0; i < 3; ++i) {
      auto level = PktNumSpace::T(i);
      if (m_txSpaceDiscarded[level]) continue;
      ZuTime deadline =
	m_txPkts[level].nextLossTime(
	  m_txLargestAckd[level], threshold, RecoveryScanBatch);
      if (!*deadline) continue;
      if (!have || deadline < out) {
	out = deadline;
	have = true;
      }
    }
    return out;
  }
  bool detectLoss_(
    PktNumSpace::T level, ZuTime now, PktLossBatch &batch, bool &complete) {
    complete = true;
    if (m_txSpaceDiscarded[level]) return false;
    ZuTime threshold = lossThreshold_();
    if (!*threshold) return false;
    PktTxUpdate update;
    complete = m_txPkts[level].markTimeThresholdLossBatch(
      m_txLargestAckd[level], now, threshold, batch,
      RecoveryScanBatch, &update);
    if (!update.lostBytes)
      return false;
    applyLossUpdateTx_(update);
    ZquicLOG(app()->qlogTrace(), ([
      level,
      lostBytes = update.lostBytes,
      bytesInFlight = m_congestion.bytesInFlight(),
      lostFrames = update.lostFrames,
      nLostFrames = update.nLostFrames,
      reason = RecReason::TimeThreshold,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (!lostBytes) return;
      RecEvent event{
	.kind = RecKind::Aggregate,
	.packetSpace = level,
	.reason = RecReason::T(reason),
		.bytes = lostBytes,
		.bytesInFlight = bytesInFlight,
		.frameCount = qlogCount_(nLostFrames),
		.linkInfo = linkInfo
      };
      for (unsigned i = 0; i < nLostFrames && i < PktTxUpdate::MaxFrames; ++i)
	qlogAddTxFrame_(event, lostFrames[i]);

      o.logPktLost(event, time);
      o.logRecPktLost(event, time);
      if (event.frames)
	o.logMarkRetrans(event, time);
    }));
    if (complete && m_txPkts[level].persistentCongestion(
	persistentCongestionThreshold_(), RecoveryScanBatch))
      persistentCongestion_();
    updateCongestionDiag_();
    return true;
  }
  void detectLossTx_(
    unsigned level, ZuTime now, PktLossBatch batch, bool lost) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss scan outside Tx thread", return);
    while (level < 3) {
      PktNumSpace::T space = PktNumSpace::T(level);
      bool complete = true;
      lost |= detectLoss_(space, now, batch, complete);
      if (!complete) {
	app()->txRun([
	  link = impl(), level, now, batch, lost
	]() mutable {
	  if (link->disconnecting_()) return;
	  link->detectLossTx_(level, now, batch, lost);
	});
	return;
      }
      ++level;
      batch = {};
    }
    if (lost) {
      impl()->queueRetransmit_();
      schedulePTO_();
    }
    scheduleLossTimer_();
  }
  void scheduleLossTimer_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer schedule outside Tx thread", return);
    if (closed()) return;
    ZuTime out = nextLossTime_();
    if (!*out) {
      ++m_txDiag.lossCanceled;
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::Loss,
	space = PktNumSpace::N,
	reason = RecReason::Canceled,
	deadlineUS = uint64_t{0},
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason),
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logLossTimerUpd(event, time);
      }));
      cancelLossTimer_();
      return;
    }
    ZuTime now = runtimeNow_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_lossTimer && m_lossTimerOut == out) return;
    ++m_txDiag.lossArmed;
    m_lossTimerOut = out;
    ZquicLOG(app()->qlogTrace(), ([
      kind = RecKind::Loss,
      space = PktNumSpace::N,
      reason = RecReason::Armed,
      deadlineUS = qlogUS_(out),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecEvent event{
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason),
		.deadlineUS = deadlineUS,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
		.linkInfo = linkInfo
      };

      o.logLossTimerUpd(event, time);
    }));
    scheduleLossTimer_(out);
  }
  void cancelLossTimer_() {
    m_lossTimerOut = {};
    cancelTimer_("loss time", &m_lossTimer);
  }

  void schedulePTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO schedule before app initialization", return);
    if (closed()) return;
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->schedulePTO_();
    });
  }

  void schedulePMTUD() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PMTUD schedule before app initialization", return);
    if (closed()) return;
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->schedulePMTUD_();
    });
  }

  void schedulePTO_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO schedule before app initialization", return);
    if (closed()) return;
    ++m_txDiag.ptoSched;
    PktNumSpace::T level = PktNumSpace::Initial;
    if (!ptoLevel_(level)) {
      ++m_txDiag.ptoNoLevel;
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	space = PktNumSpace::N,
	reason = RecReason::NoLevel,
      deadlineUS = uint64_t{0},
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason),
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logLossTimerUpd(event, time);
      }));
      cancelPTO_();
      return;
    }
    ZuTime out = ptoDeadline_(level);
    ZuTime now = runtimeNow_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_ptoTimer && m_ptoTimerOut == out && m_ptoTimerLevel == level)
      return;
    if (debugLog_()) ZiLOG(Debug, "Zquic",
      ([level, bif = m_txPkts[level].bytesInFlight()](auto &s) {
	s << "PTO armed level=" << int(level) << " bytesInFlight=" << bif;
      }));
    ++m_txDiag.ptoArmed;
    m_ptoTimerOut = out;
    m_ptoTimerLevel = level;
    ZquicLOG(app()->qlogTrace(), ([
      kind = RecKind::PTO,
      level,
      reason = RecReason::Armed,
      deadlineUS = qlogUS_(out),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecEvent event{
	  .kind = RecKind::T(kind),
	  .packetSpace = level,
	  .reason = RecReason::T(reason),
		.deadlineUS = deadlineUS,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
		.linkInfo = linkInfo
      };

      o.logLossTimerUpd(event, time);
    }));
    schedulePTOTimer_(out);
    if (*m_idleTimeout) scheduleIdleTimer_();
  }

  void schedulePTOTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "PTO", CxnTimer::PTO,
      out, ZmScheduler::Update, &m_ptoTimer);
  }

  void cancelPTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO cancel before app initialization", return);
    cancelPTO_();
  }

  void cancelPTO_() {
    if (*m_ptoTimerOut)
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	level = m_ptoTimerLevel,
	reason = RecReason::Canceled,
		deadlineUS = uint64_t{0},
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::T(kind),
	  .packetSpace = level,
	  .reason = RecReason::T(reason),
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logLossTimerUpd(event, time);
      }));
    m_ptoTimerOut = {};
    app()->mx()->cancel(&m_ptoTimer);
  }

  void startIdleTimer_() {
    ZuTime timeout = negotiatedIdleTimeout_();
    app()->txRun([link = impl(), timeout]() mutable {
      if (link->disconnecting_()) return;
      link->startIdleTimerTx_(timeout);
    });
  }
  void startIdleTimerTx_(ZuTime timeout) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer start outside Tx thread", return);
    m_idleTimeout = timeout;
    m_idleBase = runtimeNow_();
    m_idleAckElicitingSent = false;
    scheduleIdleTimer_();
  }
  ZuTime effectiveIdleTimeout_() const {
    if (!*m_idleTimeout) return {};
    ZuTime minTimeout = closeDrainDelay_();
    return m_idleTimeout < minTimeout ? minTimeout : m_idleTimeout;
  }
  ZuTime idleDeadline_() const {
    ZuTime timeout = effectiveIdleTimeout_();
    if (!*timeout) return {};
    return m_idleBase + timeout;
  }
  void scheduleIdleTimer_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer schedule outside Tx thread", return);
    if (closed() || !runtimeEstablished_() || !*m_idleTimeout) {
      cancelIdleTimer_();
      return;
    }
    ZuTime out = idleDeadline_();
    if (!*out) {
      cancelIdleTimer_();
      return;
    }
    ZuTime now = runtimeNow_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_idleTimer && m_idleTimerOut == out) return;
    m_idleTimerOut = out;
    scheduleCxnTimer_(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void scheduleIdleTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void cancelIdleTimer_() {
    m_idleTimerOut = {};
    cancelTimer_("idle", &m_idleTimer);
  }
  void notePeerPacketProcessed_() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->notePeerPacketProcessedTx_();
    });
  }
  void notePeerPacketProcessedTx_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer activity outside Tx thread", return);
    if (closed() || !runtimeEstablished_() || !*m_idleTimeout) return;
    m_idleBase = runtimeNow_();
    m_idleAckElicitingSent = false;
    scheduleIdleTimer_();
  }
  void noteAckElicitingSentTx_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ack-eliciting send activity outside Tx thread", return);
    if (closed() || !runtimeEstablished_() || !*m_idleTimeout) return;
    if (!m_idleAckElicitingSent) {
      m_idleBase = runtimeNow_();
      m_idleAckElicitingSent = true;
    }
    scheduleIdleTimer_();
  }

  void scheduleCloseTimer_(ZuTime out) {
    ZuTime now = runtimeNow_();
    if (out <= now) out = now + RttEstimator::Granularity;
    m_closeTimerOut = out;
    scheduleCxnTimer_(
      "close", CxnTimer::Close,
      out, ZmScheduler::Update, &m_closeTimer);
  }
  void cancelCloseTimer_() {
    m_closeTimerOut = {};
    cancelTimer_("close", &m_closeTimer);
  }

  ZuTime peerCloseDeadline_() const {
    return runtimeNow_() + closeDrainDelay_();
  }
  void enterLocalClosingTx_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local closing outside Tx thread", return);
    cancelAckDelayTimer_();
    cancelLossTimer_();
    cancelPTO_();
    cancelIdleTimer_();
    cancelKeyDiscardTimer_();
    cancelPMTUDTimer_();
    cancelPathTimer_();
    m_closeNextResponse = {};
    scheduleCloseTimer_(runtimeNow_());
  }
  void enterPeerDraining_(uint64_t errorCode) {
    closeStreamsRx_();
    m_appClose.error = errorCode;
    m_appClose.closed = true;
    drainLinkState_();
    m_crypto.resetTLS();
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->enterPeerDrainingTx_();
    });
  }
  void enterPeerDrainingTx_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer draining outside Tx thread", return);
    cancelAckDelayTimer_();
    cancelLossTimer_();
    cancelPTO_();
    cancelIdleTimer_();
    cancelKeyDiscardTimer_();
    cancelPMTUDTimer_();
    cancelPathTimer_();
    m_streamQueue.clean();
    for (auto &p : m_txPkts) p.clear();
    if (!m_closeTimerOut)
      scheduleCloseTimer_(peerCloseDeadline_());
  }
  void noteClosingPacket_(ZiSockAddr addr) {
    app()->txRun([
      link = impl(),
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->sendCloseResponseTx_(ZuMv(addr));
    });
  }
  void sendCloseResponseTx_(ZiSockAddr addr) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close response outside Tx thread", return);
    if (!runtimeClosing_() || !addr) return;
    ZuTime now = runtimeNow_();
    if (*m_closeNextResponse && now < m_closeNextResponse) return;
    m_closeNextResponse = now + RttEstimator::Granularity;
    (void)impl()->sendCloseFrame_(ZuMv(addr), true);
  }

  void scheduleKeyDiscardTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "key discard", CxnTimer::KeyDiscard,
      out, ZmScheduler::Advance, &m_keyDiscardTimer);
  }
  void cancelKeyDiscardTimer_() {
    cancelTimer_("key discard", &m_keyDiscardTimer);
  }

  void schedulePMTUDTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "PMTUD", CxnTimer::PMTUD,
      out, ZmScheduler::Advance, &m_pmtudTimer);
  }
  void cancelPMTUDTimer_() { cancelTimer_("PMTUD", &m_pmtudTimer); }

  void schedulePMTUD_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD schedule outside Tx thread", return);
    if (closed() || !runtimeEstablished_() ||
	m_path.probePending() || m_pmtudTimer)
      return;
    if (!m_path.nextProbeSize()) return;
    impl()->queueTxFlush_();
  }

  void schedulePathTimer_(ZuTime out) {
    scheduleCxnTimer_(
      "path validation", CxnTimer::PathValid,
      out, ZmScheduler::Update, &m_pathTimer);
  }
  void cancelPathTimer_() {
    cancelTimer_("path validation", &m_pathTimer);
  }

  void cancelTimers() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC timer cancel before app initialization", return);
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->cancelTimers_();
      return link;
    });
  }
  void cancelTimers_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer cancel outside Tx thread", return);
    // Live-state cleanup only; disconnect/final teardown uses delTimers_().
    cancelAckDelayTimer_();
    cancelLossTimer_();
    cancelPTO_();
    cancelIdleTimer_();
    cancelCloseTimer_();
    cancelKeyDiscardTimer_();
    cancelPMTUDTimer_();
    cancelPathTimer_();
  }
  void delTimers_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer teardown outside Tx thread", return);
    m_lossTimerOut = {};
    m_ptoTimerOut = {};
    m_idleTimerOut = {};
    m_closeTimerOut = {};
    m_closeNextResponse = {};
    app()->mx()->del(&m_ackDelayTimer);
    app()->mx()->del(&m_lossTimer);
    app()->mx()->del(&m_ptoTimer);
    app()->mx()->del(&m_idleTimer);
    app()->mx()->del(&m_closeTimer);
    app()->mx()->del(&m_keyDiscardTimer);
    app()->mx()->del(&m_pmtudTimer);
    app()->mx()->del(&m_pathTimer);
  }
  bool timersActive_() const {
    return m_ackDelayTimer || m_lossTimer || m_ptoTimer ||
      m_idleTimer || m_closeTimer || m_keyDiscardTimer ||
      m_pmtudTimer || m_pathTimer;
  }

  void ackDelay_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!closed()) impl()->ackDelayExpired_();
  }
  void lossTime_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!closed()) impl()->lossTimeExpired_();
  }
  void closeTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close timer outside Tx thread", return);
    if (disconnecting_()) return;
    m_closeTimerOut = {};
    impl()->closeExpired_();
  }
  void idleTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (*m_idleTimeout) {
      if (closed() || !runtimeEstablished_()) return;
      ZuTime out = idleDeadline_();
      ZuTime now = runtimeNow_();
      if (*out && out > now) {
	scheduleIdleTimer_();
	return;
      }
    }
    impl()->idleExpired_();
  }
  void keyDiscard_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!closed()) impl()->keyDiscardExpired_();
  }
  void pmtudTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!closed()) impl()->pmtudExpired_();
  }
  void pathTimeout_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!closed()) impl()->pathExpired_();
  }

  void ackDelayExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay expiry outside Tx thread", return);
    m_txAck[PktNumSpace::AppData].due = true;
    impl()->flushTx_();
  }
  void lossTimeExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer expired outside Tx thread", return);
    if (closed()) return;
    ++m_txDiag.lossExpired;
    ZuTime now = runtimeNow_();
    detectLossTx_(0, now, {}, false);
  }
  void closeExpired_() { }
  void idleExpired_() { impl()->closeExpired_(); }
  void keyDiscardExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard expiry outside Tx thread", return);
    app()->rxRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->clearExpiredKeysRx_();
    });
  }
  void clearExpiredKeysRx_() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC key discard outside Rx thread", return);
    discardOldPeerKeys_();
  }
  void pmtudExpired_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD expiry outside Tx thread", return);
    if (!m_path.probeExpired()) return;
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Expired,
	      size = m_path.probeSize(),
	      reason = PathReason::Timeout,
	      activeMaxUDP = m_path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.mtu = size ? size : activeMaxUDP,
		.validated = validated,
		.linkInfo = linkInfo
	      };

      o.logPMTUDUpdated(event, time);
    }));
    schedulePMTUD_();
  }
  void applyPathHint_(PathHint hint) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path hint outside Tx thread", return);
    if (!hint) return;
    m_path.applyHint(hint);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Hint,
	      size = m_path.activeMaxUDP(),
	      reason = PathReason::PathHint,
	      activeMaxUDP = m_path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.mtu = size ? size : activeMaxUDP,
		.validated = validated,
		.linkInfo = linkInfo
	      };

      o.logPMTUDUpdated(event, time);
    }));
    if (!m_path.probePending()) cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void onPMTUDProbeAckd_(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD ACK outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeAckd();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Acked,
	      size,
	      reason = PathReason::Probe,
	      activeMaxUDP = m_path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.mtu = size ? size : activeMaxUDP,
		.validated = validated,
		.linkInfo = linkInfo
	      };

      o.logPMTUDUpdated(event, time);
    }));
    cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void onPMTUDProbeLost_(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD loss outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeLost();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Lost,
	      size,
	      reason = PathReason::Probe,
	      activeMaxUDP = m_path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.mtu = size ? size : activeMaxUDP,
		.validated = validated,
		.linkInfo = linkInfo
	      };

      o.logPMTUDUpdated(event, time);
    }));
    cancelPMTUDTimer_();
    schedulePMTUD_();
  }
  void persistentCongestion_() {
    m_congestion.persistentCongestion();
    ++m_txDiag.persistentCongestion;
    updateCongestionDiag_();
  }

  void discardPktNumSpace_(PktNumSpace::T level) {
    if (level == PktNumSpace::AppData) return;
    m_rxSpaceDiscarded[level] = true;
    m_rxCrypto[level].reset();
    m_rxAcks.sent(PktNumSpace::T(level));
	    ZquicLOG(app()->qlogTrace(), ([level, linkInfo = linkInfo_()](auto &o, ZuTime time) {
	      SecEvent event{
		.kind = SecKind::KeyRetired,
		.keyType = SecKeyType::RX,
		.trigger = SecTrigger::HSComplete,
		.reason = SecReason::PacketSpace,
		.success = true,
		.linkInfo = linkInfo
	      };
      event.packetSpace = level;
      o.logKeyRetired(event, time);
    }));
    app()->txRun([link = impl(), level]() mutable {
      if (link->disconnecting_()) return;
      link->discardTxPktNumSpace_(level);
    });
  }

  void discardTxPktNumSpace_(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC sent-packet discard outside Tx thread", return);
    if (level == PktNumSpace::AppData) return;
    m_txSpaceDiscarded[level] = true;
    m_congestion.release(m_txPkts[level].bytesInFlight());
    m_txPkts[level].clear();
    m_txCrypto[level].reset();
    m_txCryptoUnackd[level].clean();
    m_txAck[level].nRanges = 0;
	    ZquicLOG(app()->qlogTrace(), ([level, linkInfo = linkInfo_()](auto &o, ZuTime time) {
	      SecEvent event{
		.kind = SecKind::KeyRetired,
		.keyType = SecKeyType::TX,
		.trigger = SecTrigger::HSComplete,
		.reason = SecReason::PacketSpace,
		.success = true,
		.linkInfo = linkInfo
	      };
      event.packetSpace = level;
      o.logKeyRetired(event, time);
    }));
    updateCongestionDiag_();
    scheduleLossTimer_();
  }

  bool reclaimPTO_(PktNumSpace::T &level, unsigned &probes) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO reclaim outside Tx thread", return false);
    probes = 0;
    if (!ptoLevel_(level)) return false;
    unsigned n = m_txPkts[level].reclaimOnPTO(2);
    if (n) {
      probes = n;
      ++m_txDiag.ptoCount;
      m_ptoBackoff.expired();
      ZquicLOG(app()->qlogTrace(), ([
	level,
	probes = n,
		backoff = m_ptoBackoff.count(),
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::PTO,
	  .packetSpace = level,
	  .reason = RecReason::Backoff,
	  .value = backoff,
	  .bytes = probes,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};
	o.logLossTimerUpd(event, time);
      }));
      if (debugLog_()) ZiLOG(Debug, "Zquic", ([level, n](auto &s) {
	  s << "PTO fired level=" << int(level) << " probes=" << n;
	}));
    }
    return n;
  }
  bool reclaimPTO_() {
    PktNumSpace::T level = PktNumSpace::Initial;
    unsigned probes = 0;
    return reclaimPTO_(level, probes);
  }
  void notePTOExpired_() {
    ++m_txDiag.ptoExpired;
    ZquicLOG(app()->qlogTrace(), ([
      level = [this]() -> PktNumSpace::T {
	PktNumSpace::T level_ = PktNumSpace::Initial;
	return ptoLevel_(level_) ? level_ : PktNumSpace::N;
      }(),
      backoff = m_ptoBackoff.count(),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecEvent event{
	.kind = RecKind::PTO,
	.packetSpace = level,
	.reason = RecReason::Expired,
		.value = backoff,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
		.linkInfo = linkInfo
      };
      o.logLossTimerUpd(event, time);
    }));
  }
  void notePTOFlush_() { ++m_txDiag.ptoFlush; }
  void notePTORetx_() { ++m_txDiag.ptoRetx; }
  void notePTOProbe_(PktNumSpace::T level, unsigned probes) {
    ++m_txDiag.ptoProbe;
    ZquicLOG(app()->qlogTrace(), ([
      level,
      probes,
      backoff = m_ptoBackoff.count(),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecEvent event{
	.kind = RecKind::PTO,
	.packetSpace = level,
	.reason = RecReason::Probe,
	.value = backoff,
		.bytes = probes,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
		.linkInfo = linkInfo
      };
      o.logLossTimerUpd(event, time);
    }));
  }
  uint64_t txPackets_() const { return m_txDiag.packetsTx; }

  bool nextRetransmit_(PktNumSpace::T &level, SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC retransmit selection outside Tx thread", return false);
    for (unsigned i = 0; i < 3; ++i) {
      PktNumSpace::T l = PktNumSpace::T(i);
      if (m_txSpaceDiscarded[l]) continue;
      do {
	if (!m_txPkts[l].nextRetransmit(ref)) goto nextSpace;
	SentFrameRef tail;
	if (!clipRetransmit_(l, ref, tail)) continue;
	if (tail) (void)requeueRetransmit_(l, tail);
	break;
      } while (true);
      level = l;
      ++m_txDiag.retransmittedFrames;
      if (ref.kind == SentFrameKind::Stream)
	if (debugLog_()) ZiLOG(Debug, "Zquic", ([
	  level, ref
	](auto &s) {
	  s << "stream retransmit selected level=" << int(level) <<
	    " streamID=" << ref.streamID <<
	    " offset=" << ref.offset <<
	    " length=" << ref.length <<
	    " fin=" << int(ref.fin) <<
	    " rangeOffset=" << ref.range.offset <<
	    " rangeLength=" << ref.range.length <<
	    " hasBuf=" << int(!!ref.range.buf);
	}));
      if (debugLog_()) ZiLOG(Debug, "Zquic", ([level, ref](auto &s) {
	  s << "retransmit queued level=" << int(level) <<
	    " kind=" << int(ref.kind) <<
	    " streamID=" << ref.streamID <<
	    " offset=" << ref.offset <<
	    " length=" << ref.length;
      }));
      return true;
nextSpace:
      continue;
    }
    return false;
  }

  bool requeueRetransmit_(PktNumSpace::T level, const SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zquic",
      (), "QUIC retransmit requeue outside Tx thread", return false);
    if (m_txSpaceDiscarded[level]) return false;
    if (!frameStillUnackd_(level, ref)) return false;
    return m_txPkts[level].requeueRetransmit(ref);
  }

  bool buildRetransmitStream_(PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Stream) return false;
    build.reset();
    if (!appendPendingAck_(PktNumSpace::AppData, build)) return false;
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), ref.streamID, ref.offset,
      ref.length, ref.fin);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return build.add(ref.range);
  }

  bool buildRetransmitControl_(PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Control ||
	ref.controlType == FrameType::Unknown ||
	ref.controlType == FrameType::PathResponse)
      return false;
    ControlFrame frame = controlFrame_(ref);
    if (!controlStillValid_(frame)) return false;
    build.reset();
    if (!appendPendingAck_(PktNumSpace::AppData, build)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  bool buildPingProbe_(PktBuild &build) {
    build.reset();
    if (!appendPendingAck_(PktNumSpace::AppData, build)) return false;
    int n = FrameCodec::writePing(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  template <typename SendProbe>
  bool sendPMTUDProbe_(ZiSockAddr addr, SendProbe sendProbe) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD probe send outside Tx thread", return false);
    if (!runtimeEstablished_() || m_path.probePending()) return false;
    unsigned size = m_path.nextProbeSize();
    if (!size || !m_path.canSendProbe(size) || !m_congestion.canSend(size)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
		size,
		reason = PathReason::Admission,
		activeMaxUDP = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::PMTUD,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .mtu = size ? size : activeMaxUDP,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPMTUDUpdated(event, time);
      }));
      return false;
    }
    m_path.startProbe(size);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Sent,
      size,
      reason = PathReason::Probe,
      activeMaxUDP = m_path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvent event{
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.mtu = size ? size : activeMaxUDP,
		.validated = validated,
		.linkInfo = linkInfo
      };

      o.logPMTUDUpdated(event, time);
    }));
    PktBuild build;
    if (!buildPingProbe_(build) || !sendProbe(build, ZuMv(addr), size)) {
      m_path.probeLost();
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Lost,
		size,
		reason = PathReason::SendFail,
		activeMaxUDP = m_path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvent event{
	  .kind = PathKind::PMTUD,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .mtu = size ? size : activeMaxUDP,
	  .validated = validated,
	  .linkInfo = linkInfo
		};

	o.logPMTUDUpdated(event, time);
      }));
      return false;
    }
    schedulePMTUDTimer_(runtimeNow_() + ptoTimeout_());
    return true;
  }

  bool buildRetransmitCrypto_(
    PktNumSpace::T level, PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Crypto || m_txSpaceDiscarded[level])
      return false;
    ZuBSpan payload;
    if (!m_txCrypto[level].txPayload(ref.offset, ref.length, payload))
      return false;
    build.reset();
    if (!appendPendingAck_(level, build)) return false;
    int n = FrameCodec::writeCryptoPrefix(
      build.scratch(), build.scratchAvail(), ref.offset, payload.length());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return build.add(payload);
  }

  bool rxPktSeen_(PktNumSpace::T level, uint64_t pn) const {
    return m_rxAcks.tracker(level).contains(pn);
  }

  bool recordTxPkt_(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan frame,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    SentFrameRef ref;
    bool ackEliciting = false;
    if (!runtimeFrameRef(frame, ref, ackEliciting)) return true;
    return recordTxPkt_(
      level, pn, bytes, ref, ackEliciting, false, 0, ackLevel, ackLargest);
  }

  bool recordTxPkt_(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const SentFrameRef &ref, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    TxPktRefs refs;
    if (ref.kind != SentFrameKind::None) refs.add(ref);
    return recordTxPkt_(
      level, pn, bytes, refs, ackEliciting, pmtudProbe, pmtudSize,
      ackLevel, ackLargest);
  }

  bool recordTxPkt_(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const TxPktRefs &refs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    if (m_txSpaceDiscarded[level]) return true;
    if (!ackEliciting && !refs.count()) return true;
    SentPkt packet;
    packet.pn = pn;
    packet.space = level;
    packet.bytes = bytes;
    packet.sentTime = runtimeNow_();
    packet.ackEliciting = ackEliciting;
    packet.inFlight = ackEliciting;
    packet.pmtudProbe = pmtudProbe;
    packet.pmtudSize = pmtudSize;
    packet.ackLevel = ackLevel;
    packet.ackLargest = ackLargest;
    for (unsigned i = 0; i < refs.count(); ++i)
      packet.addFrame(refs[i], refs.stream(i));
    if (!m_txPkts[level].add(packet)) return false;
    recordTxUnackd_(level, refs);
    if (ackEliciting) {
      m_congestion.sent(bytes);
      updateCongestionDiag_();
      scheduleLossTimer_();
    }
    return true;
  }

  void processAckFrame_(PktNumSpace::T level, const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive processing outside Rx thread", return);
    if (!frame.ackRanges) return;
    AckSnapshot ack;
    ack.level = level;
    ack.delay = frame.value;
    ack.ecn = frame.ackECN;
    ack.nRanges = frame.ackRanges.length();
    for (unsigned i = 0; i < ack.nRanges; ++i)
      ack.ranges[i] = frame.ackRanges[i];
    app()->txRun([link = impl(), ack]() mutable {
      if (link->disconnecting_()) return;
      link->processAckFrameTx_(ack);
    });
  }

  void queueAckReap_(
    Stream **streams, unsigned &nStreams, Stream *stream) {
    if (!stream) return;
    for (unsigned i = 0; i < nStreams; ++i)
      if (streams[i] == stream) return;
    if (nStreams < PktTxUpdate::MaxFrames) streams[nStreams++] = stream;
  }
  void reapAckedStreams_(PktTxUpdate &update, Stream **streams, unsigned n) {
    update.clearAckdFrames();
    for (unsigned i = 0; i < n; ++i)
      reapStream_(streams[i]);
  }
  void reapStreamFromRx_(const StreamRef &stream) {
    if (!stream || stream->id() < 0) return;
    int64_t id = stream->id();
    app()->txRun([link = impl(), id]() mutable {
      if (link->disconnecting_()) return;
      link->reapStreamByIDTx_(id);
    });
  }
  bool reapStreamByIDTx_(int64_t id) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream reap outside Tx thread", return false);
    Stream *stream = nullptr;
    {
      StreamRef streamRef = findStream(id);
      stream = streamRef.ptr();
    }
    return reapStream_(stream);
  }
  void applyAckUpdateTx_(
    const PktTxUpdate &update, bool &congestionOpened,
    Stream **reapStreams, unsigned &nReapStreams) {
    applyAckOfAckTx_(update);
    for (unsigned i = 0; i < update.nAckdFrames; ++i) {
      const SentFrameRef &ref = update.ackdFrames[i];
      StreamRef streamRef;
      Stream *stream = static_cast<Stream *>(update.ackdOwners[i]);
      if ((ref.kind == SentFrameKind::Stream ||
	  ref.kind == SentFrameKind::Control) &&
	  ref.streamID <= uint64_t(INT64_MAX)) {
	streamRef = findStream(int64_t(ref.streamID));
	stream = streamRef.ptr();
      }
      if (ackTxFrame_(update.level, ref, stream))
	switch (ref.kind) {
	  case SentFrameKind::Stream:
	    if (ref.fin) queueAckReap_(reapStreams, nReapStreams, stream);
	    break;
	  case SentFrameKind::Control:
	    if (ref.controlType == FrameType::ResetStream)
	      queueAckReap_(reapStreams, nReapStreams, stream);
	    break;
	  default:
	    break;
	}
    }
    if (update.normalAckdBytes) {
      m_congestion.ackd(update.normalAckdBytes);
      congestionOpened = true;
      ZquicLOG(app()->qlogTrace(), ([
      reason = RecReason::Ack,
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason),
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logCongStateUpd(event, time);
      }));
    }
    if (update.pmtudAckdBytes) {
      m_congestion.ackd(update.pmtudAckdBytes, true);
      onPMTUDProbeAckd_(update.pmtudAckdSize);
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDAck,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason),
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logCongStateUpd(event, time);
      }));
    }
  }
  void applyAckOfAckTx_(const PktTxUpdate &update) {
    for (unsigned i = 0; i < 3; ++i) {
      if (!update.ackdAck[i]) continue;
      auto level = PktNumSpace::T(i);
      uint64_t largest = update.ackLargest[i];
      app()->rxRun([link = impl(), level, largest]() mutable {
	if (link->disconnecting_()) return;
	link->ackAckdRx_(level, largest);
      });
    }
  }
  void ackAckdRx_(PktNumSpace::T level, uint64_t largest) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK-of-ACK trim outside Rx thread", return);
    m_rxAcks.tracker(level).ackdByPeer(largest);
  }
  void applyLossUpdateTx_(const PktTxUpdate &update) {
    if (update.normalLostBytes) {
      m_congestion.lostAt(
	update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()));
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::Loss,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason),
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logCongStateUpd(event, time);
      }));
    }
    if (update.pmtudLostBytes) {
      m_congestion.lostAt(
	update.pmtudLostBytes,
	uint64_t(update.pmtudLostSentTime.microsecs()), true);
      onPMTUDProbeLost_(update.pmtudLostSize);
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDLoss,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecEvent event{
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason),
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .linkInfo = linkInfo
		};

	o.logCongStateUpd(event, time);
      }));
    }
  }

  void processAckFrameTx_(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    TxAckWork work;
    work.ack = ack;
    work.gen = m_txRuntimeGen;
    processAckFrameTx_(ZuMv(work));
  }
  void processAckFrameTx_(TxAckWork work) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    AckSnapshot &ack = work.ack;
    if (work.gen != m_txRuntimeGen) return;
    if (!ack.nRanges || m_txSpaceDiscarded[ack.level]) return;
    if (!ackFrameValidTx_(ack)) {
      ++m_txDiag.failures;
      return;
    }
    if (!work.ecnValidated) {
      validateAckECN_(ack);
      for (unsigned i = 0; i < ack.nRanges; ++i) {
	uint64_t largest = ack.ranges[i].largest;
	if (ZuCmp<uint64_t>::null(m_txLargestAckd[ack.level]) ||
    largest > m_txLargestAckd[ack.level])
	  m_txLargestAckd[ack.level] = largest;
      }
      work.ecnValidated = true;
    }
	    PktTxUpdate update;
	    Stream *reapStreams[PktTxUpdate::MaxFrames] = {};
	    unsigned nReapStreams = 0;
	    if (!work.lossPhase) {
	      if (!m_txPkts[ack.level].ackBatch(
		  ack.ranges, ack.nRanges, work.ackBatch, RecoveryScanBatch,
		  ack.level, &update)) {
		applyAckUpdateTx_(
		  update, work.congestionOpened, reapStreams, nReapStreams);
		ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  largestAcked = ack.nRanges ? ack.ranges[ack.nRanges - 1].largest : 0,
	  ackDelayUS = ack.level == PktNumSpace::AppData ?
    qlogUS_(ackDelay_(ack.delay)) : 0,
	  ackedBytes = update.ackdBytes,
	  lostBytes = update.lostBytes,
	  nRanges = ack.nRanges,
	  nAckdFrames = update.nAckdFrames,
	  nLostFrames = update.nLostFrames,
	  packetNumbers = qlogAckedPNs_(update),
	  packetNumbersTruncated = update.ackedPNsTruncated,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) mutable {
	  if (!packetNumbers) return;
	  AckEvent event{
    .largestAcked = largestAcked,
    .ackDelayUS = ackDelayUS,
    .ackedBytes = ackedBytes,
    .lostBytes = lostBytes,
    .rangeCount = qlogCount_(nRanges),
    .ackedFrames = qlogCount_(nAckdFrames),
	    .lostFrames = qlogCount_(nLostFrames),
	    .packetNumbersTruncated = packetNumbersTruncated,
	    .packetNumbers = ZuMv(packetNumbers),
	    .linkInfo = linkInfo
	  };
		  event.packetSpace = level;
		  o.logPktsAcked(event, time);
		}));
		reapAckedStreams_(update, reapStreams, nReapStreams);
		app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
		  if (link->disconnecting_()) return;
		  link->processAckFrameTx_(ZuMv(work));
		});
		return;
	      }
	      applyAckUpdateTx_(
		update, work.congestionOpened, reapStreams, nReapStreams);
	      ZquicLOG(app()->qlogTrace(), ([
	level = ack.level,
	largestAcked = ack.nRanges ? ack.ranges[ack.nRanges - 1].largest : 0,
	ackDelayUS = ack.level == PktNumSpace::AppData ?
	  qlogUS_(ackDelay_(ack.delay)) : 0,
	ackedBytes = update.ackdBytes,
	lostBytes = update.lostBytes,
	nRanges = ack.nRanges,
	nAckdFrames = update.nAckdFrames,
		nLostFrames = update.nLostFrames,
		packetNumbers = qlogAckedPNs_(update),
		packetNumbersTruncated = update.ackedPNsTruncated,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) mutable {
	if (!packetNumbers) return;
	AckEvent event{
	  .largestAcked = largestAcked,
	  .ackDelayUS = ackDelayUS,
	  .ackedBytes = ackedBytes,
	  .lostBytes = lostBytes,
	  .rangeCount = qlogCount_(nRanges),
	  .ackedFrames = qlogCount_(nAckdFrames),
	  .lostFrames = qlogCount_(nLostFrames),
	  .packetNumbersTruncated = packetNumbersTruncated,
	  .packetNumbers = ZuMv(packetNumbers),
	  .linkInfo = linkInfo
		};
		event.packetSpace = level;
		o.logPktsAcked(event, time);
	      }));
	      reapAckedStreams_(update, reapStreams, nReapStreams);
	      work.lossPhase = true;
	    } else {
	      applyAckUpdateTx_(
		update, work.congestionOpened, reapStreams, nReapStreams);
	      reapAckedStreams_(update, reapStreams, nReapStreams);
	    }
    if (work.ackBatch.haveAckForLoss) {
      PktTxUpdate lossUpdate;
      if (!m_txPkts[ack.level].markPktThresholdLossBatch(
	  work.ackBatch.largestAckdForLoss, 3, work.lossBatch,
	  RecoveryScanBatch, &lossUpdate)) {
	applyLossUpdateTx_(lossUpdate);
	ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  lostBytes = lossUpdate.lostBytes,
	  bytesInFlight = m_congestion.bytesInFlight(),
	  lostFrames = lossUpdate.lostFrames,
	  nLostFrames = lossUpdate.nLostFrames,
	  reason = RecReason::PacketThreshold,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  if (!lostBytes) return;
	  RecEvent event{
    .kind = RecKind::Aggregate,
    .packetSpace = level,
	    .reason = RecReason::T(reason),
	    .bytes = lostBytes,
	    .bytesInFlight = bytesInFlight,
	    .frameCount = qlogCount_(nLostFrames),
	    .linkInfo = linkInfo
	  };
	  for (unsigned i = 0; i < nLostFrames && i < PktTxUpdate::MaxFrames; ++i)
    qlogAddTxFrame_(event, lostFrames[i]);

	  o.logPktLost(event, time);
	  o.logRecPktLost(event, time);
	  if (event.frames)
    o.logMarkRetrans(event, time);
	}));
	if (lossUpdate.lostBytes) work.retransmit = true;
	app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
	  if (link->disconnecting_()) return;
	  link->processAckFrameTx_(ZuMv(work));
	});
	return;
      }
      applyLossUpdateTx_(lossUpdate);
      ZquicLOG(app()->qlogTrace(), ([
	level = ack.level,
	lostBytes = lossUpdate.lostBytes,
		bytesInFlight = m_congestion.bytesInFlight(),
		lostFrames = lossUpdate.lostFrames,
		nLostFrames = lossUpdate.nLostFrames,
		reason = RecReason::PacketThreshold,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	if (!lostBytes) return;
	RecEvent event{
	  .kind = RecKind::Aggregate,
	  .packetSpace = level,
	  .reason = RecReason::T(reason),
	  .bytes = lostBytes,
	  .bytesInFlight = bytesInFlight,
	  .frameCount = qlogCount_(nLostFrames),
	  .linkInfo = linkInfo
		};
	for (unsigned i = 0; i < nLostFrames && i < PktTxUpdate::MaxFrames; ++i)
	  qlogAddTxFrame_(event, lostFrames[i]);

	o.logPktLost(event, time);
	o.logRecPktLost(event, time);
	if (event.frames)
	  o.logMarkRetrans(event, time);
      }));
      if (lossUpdate.lostBytes) work.retransmit = true;
    }
    if (work.lossBatch.lost) {
      if (m_txPkts[ack.level].persistentCongestion(
	  persistentCongestionThreshold_(), RecoveryScanBatch))
	persistentCongestion_();
    }
    if (work.ackBatch.ackd) {
      ZuTime now = runtimeNow_();
      if (*work.ackBatch.latestSentTime && work.ackBatch.latestSentTime < now) {
	m_rtt.sample(
	  now - work.ackBatch.latestSentTime,
	  ack.level == PktNumSpace::AppData ? ackDelay_(ack.delay) : ZuTime{0},
	  ack.level == PktNumSpace::AppData);
	ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  latestRTTUS = qlogUS_(m_rtt.latest()),
	  smoothedRTTUS = qlogUS_(m_rtt.smoothed()),
	  rttVarianceUS = qlogUS_(m_rtt.variance()),
	  minRTTUS = qlogUS_(m_rtt.min()),
	  cwnd = m_congestion.cwnd(),
	  ssthresh = m_congestion.ssthresh(),
	  bytesInFlight = m_congestion.bytesInFlight(),
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  RecEvent event{
    .kind = RecKind::RTT,
    .packetSpace = level,
    .latestRTTUS = latestRTTUS,
    .smoothedRTTUS = smoothedRTTUS,
    .rttVarianceUS = rttVarianceUS,
	    .minRTTUS = minRTTUS,
	    .cwnd = cwnd,
	    .ssthresh = ssthresh,
	    .bytesInFlight = bytesInFlight,
	    .linkInfo = linkInfo
	  };

	  o.logMetricsUpd(event, time);
	}));
      }
      m_ptoBackoff.reset();
    }
    updateCongestionDiag_();
    schedulePTO_();
    scheduleLossTimer_();
    if (work.congestionOpened || congestionAllowance_())
      impl()->flushTx_();
    if (work.retransmit) impl()->queueRetransmit_();
  }

  bool ackFrameValidTx_(const AckSnapshot &ack) const {
    for (unsigned i = 0; i < ack.nRanges; ++i)
      if (ack.ranges[i].largest >= m_txPN[ack.level]) return false;
    return true;
  }

  bool validateAckECN_(const AckSnapshot &ack) {
    if (!ack.ecn.any()) return true;
    unsigned i = unsigned(ack.level);
    const AckECN &last = m_peerAckECN[i];
    bool fail = false;
    if (ack.ecn.ect0 < last.ect0) {
      fail = true;
    }
    if (ack.ecn.ect1 < last.ect1) {
      fail = true;
    }
    if (ack.ecn.ce < last.ce) {
      fail = true;
    }
    uint64_t total = ack.ecn.ect0 + ack.ecn.ect1;
    if (total < ack.ecn.ect0) {
      fail = true;
    }
    uint64_t withCE = total + ack.ecn.ce;
    if (withCE < total) {
      fail = true;
    }
    uint64_t largest = 0;
    if (ack.nRanges) {
      largest = ack.ranges[ack.nRanges - 1].largest;
      if (withCE > largest + 1) {
	fail = true;
      }
    }
    if (fail) {
      m_path.setEcnDisabled();
      ++m_txDiag.ecnValidationFailures;
      ZquicLOG(app()->qlogTrace(), ([
	level = ack.level,
	state = ECNState::Failed,
	ect0 = ack.ecn.ect0,
	ect1 = ack.ecn.ect1,
	ce = ack.ecn.ce,
	previousECT0 = last.ect0,
		previousECT1 = last.ect1,
		previousCE = last.ce,
		largestAcked = largest,
		disabled = true,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	ECNReason::T reason = ECNReason::AckECN;
	if (ect0 < previousECT0)
	  reason = ECNReason::ECT0Decrease;
	if (ect1 < previousECT1)
	  reason = ECNReason::ECT1Decrease;
	if (ce < previousCE)
	  reason = ECNReason::CEDecrease;
	uint64_t total_ = ect0 + ect1;
	if (total_ < ect0)
	  reason = ECNReason::ECTOverflow;
	uint64_t withCE_ = total_ + ce;
	if (withCE_ < total_)
	  reason = ECNReason::CEOverflow;
	if (withCE_ > largestAcked + 1)
	  reason = ECNReason::CounterExceedsAck;
	ECNEvent event{
	  .packetSpace = level,
	  .state = ECNState::T(state),
	  .reason = reason,
	  .ect0 = ect0,
	  .ect1 = ect1,
	  .ce = ce,
	  .previousECT0 = previousECT0,
	  .previousECT1 = previousECT1,
	  .previousCE = previousCE,
	  .largestAcked = largestAcked,
	  .disabled = disabled,
	  .linkInfo = linkInfo
		};

	o.logECNStateUpd(event, time);
      }));
      return false;
    }
    m_peerAckECN[i] = ack.ecn;
    m_txDiag.peerAckECN[i] = ack.ecn;
    ZquicLOG(app()->qlogTrace(), ([
      level = ack.level,
      state = ECNState::Capable,
      ect0 = ack.ecn.ect0,
      ect1 = ack.ecn.ect1,
      ce = ack.ecn.ce,
      previousECT0 = last.ect0,
      previousECT1 = last.ect1,
      previousCE = last.ce,
      largestAcked = largest,
      disabled = false,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      ECNEvent event{
	  .packetSpace = level,
	  .state = ECNState::T(state),
	  .reason = ECNReason::AckECN,
	.ect0 = ect0,
	.ect1 = ect1,
	.ce = ce,
	.previousECT0 = previousECT0,
	.previousECT1 = previousECT1,
		.previousCE = previousCE,
		.largestAcked = largestAcked,
		.disabled = disabled,
		.linkInfo = linkInfo
      };

      o.logECNStateUpd(event, time);
    }));
    return true;
  }

  static ZuTime runtimeNow_() { return Zm::now(); }
  ZuTime maxAckDelay_() const {
    if (!m_crypto.peerTransportParamsReceived()) return ZuTime{0};
    return timeUS(m_crypto.peerTransportParams().maxAckDelay * 1000);
  }
  static ZuTime transportParamMS_(uint64_t ms) {
    if (!ms) return ZuTime{0};
    if (ms > UINT64_MAX / 1000) ms = UINT64_MAX / 1000;
    return timeUS(ms * 1000);
  }
  ZuTime negotiatedIdleTimeout_() const {
    uint64_t local = m_transportParams.maxIdleTimeout;
    uint64_t peer = m_crypto.peerTransportParamsReceived() ?
      m_crypto.peerTransportParams().maxIdleTimeout : 0;
    if (!local) return transportParamMS_(peer);
    if (!peer) return transportParamMS_(local);
    return transportParamMS_(local < peer ? local : peer);
  }
  ZuTime ackDelay_(uint64_t delay) const {
    if (!m_crypto.peerTransportParamsReceived()) return ZuTime{0};
    const auto &params = m_crypto.peerTransportParams();
    ZuTime maxAckDelay = maxAckDelay_();
    uint64_t maxAckUsec = uint64_t(maxAckDelay.microsecs());
    if (delay > (maxAckUsec >> params.ackDelayExponent))
      return maxAckDelay;
    delay <<= params.ackDelayExponent;
    ZuTime ackDelay = timeUS(delay);
    return ackDelay < maxAckDelay ? ackDelay : maxAckDelay;
  }
  ZuTime ptoTimeout_() const {
    return m_ptoBackoff.timeout(m_rtt, maxAckDelay_());
  }
  ZuTime closeDrainDelay_() const {
    return ptoTimeout_() * ZuDecimal{3};
  }
  ZuTime keyDiscardDeadline_() const {
    return runtimeNow_() + closeDrainDelay_();
  }
  bool buildPayload_(
    PktNumSpace::T level, PktBuild &build, ZuBSpan frame) {
    build.reset();
    return appendPendingAck_(level, build) && build.add(frame);
  }

  bool buildPayload_(
    PktNumSpace::T level, PktBuild &build,
    ZuBSpan prefix, ZuBSpan payload) {
    build.reset();
    return appendPendingAck_(level, build) &&
      build.add(prefix) && build.add(payload);
  }

  template <typename SendPkt>
  bool holdInitialForCoalesce_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_coalesceLong || m_coalesceInitial)
      return sendPkt(ZuMv(buf), ZuMv(addr));
    ZquicLOG(app()->qlogTrace(), ([
      packetBytes = buf->length,
      reason = PktEvent::Reason::Coalescing,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PktEvent event{
	.packetSize = packetBytes
      };
      event.packetType = PktType::Initial;
      event.packetSpace = PktNumSpace::Initial;
      event.reason = reason;
      event.linkInfo = linkInfo;
      o.logPktBuf(event, time);
    }));
    m_coalesceInitial = ZuMv(buf);
    m_coalesceAddr = ZuMv(addr);
    return true;
  }

  template <typename SendPkt>
  bool sendHandshakeCoalesced_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_coalesceInitial) return sendPkt(ZuMv(buf), ZuMv(addr));
    ZmRef<ZiIOBuf> initial = ZuMv(m_coalesceInitial);
    ZiSockAddr initialAddr = ZuMv(m_coalesceAddr);
    if (initial->avail() >= buf->length) {
      initial->append(buf->cspan());
      return sendPkt(ZuMv(initial), ZuMv(initialAddr));
    }
    if (!sendPkt(ZuMv(initial), ZuMv(initialAddr))) return false;
    return sendPkt(ZuMv(buf), ZuMv(addr));
  }

  template <typename SendPkt>
  bool flushCoalescedInitial_(SendPkt sendPkt) {
    if (!m_coalesceInitial) return true;
    ZmRef<ZiIOBuf> initial = ZuMv(m_coalesceInitial);
    ZiSockAddr addr = ZuMv(m_coalesceAddr);
    return sendPkt(ZuMv(initial), ZuMv(addr));
  }
  void beginLongCoalesce_() { m_coalesceLong = true; }
  void endLongCoalesce_() { m_coalesceLong = false; }

  template <typename SendCryptoPkt>
  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    unsigned chunkMax, ZiSockAddr addr, SendCryptoPkt sendCryptoPkt) {
    return sendRuntimeCryptoFlights(
      m_txCrypto, m_txDiag,
      data, len, offsets, chunkMax, ZuMv(addr),
      [sendCryptoPkt](
	  PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) mutable {
	return sendCryptoPkt(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
  }

  template <
    typename BuildPayload,
    typename SendInitial, typename SendHandshake, typename SendShort>
  bool sendCryptoPkt_(
    PktNumSpace::T level, ZuBSpan frame, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PktBuild build;
    if (!buildPayload(level, build, frame)) return false;
    if (level == PktNumSpace::Initial)
      return sendInitial(build, ZuMv(addr), frame);
    if (level == PktNumSpace::Handshake)
      return sendHandshake(build, ZuMv(addr), frame);
    return sendShort(build, ZuMv(addr), frame);
  }

  template <
    typename BuildPayload,
    typename SendInitial, typename SendHandshake, typename SendShort>
  bool sendCryptoPkt_(
    PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr,
    BuildPayload buildPayload,
    SendInitial sendInitial, SendHandshake sendHandshake, SendShort sendShort) {
    PktBuild build;
    if (!buildPayload(level, build, prefix, payload)) return false;
    if (level == PktNumSpace::Initial)
      return sendInitial(build, ZuMv(addr), {}, &ref, true);
    if (level == PktNumSpace::Handshake)
      return sendHandshake(build, ZuMv(addr), {}, &ref, true);
    return sendShort(build, ZuMv(addr), {}, &ref, true);
  }

  void noteTxControlFrame_(FrameType::T type) {
    switch (type) {
      case FrameType::MaxData: ++m_txDiag.maxDataTx; break;
      case FrameType::MaxStreamData: ++m_txDiag.maxStreamDataTx; break;
      case FrameType::MaxStreams: ++m_txDiag.maxStreamsTx; break;
      case FrameType::DataBlocked: ++m_txDiag.dataBlockedTx; break;
      case FrameType::StreamDataBlocked: ++m_txDiag.streamDataBlockedTx; break;
      case FrameType::StreamsBlocked: ++m_txDiag.streamsBlockedTx; break;
      case FrameType::ResetStream: ++m_txDiag.resetStreamTx; break;
      case FrameType::StopSending: ++m_txDiag.stopSendingTx; break;
      case FrameType::PathChallenge: ++m_txDiag.pathChallengeTx; break;
      case FrameType::PathResponse: ++m_txDiag.pathResponseTx; break;
      case FrameType::HandshakeDone: ++m_txDiag.handshakeDoneTx; break;
      default: break;
    }
  }

  void noteTxPktDiag_(const TxPktRefs *refs, uint8_t ackLevel) {
    bool hasAck = ackLevel < 3;
    bool hasStream = false;
    bool hasControl = false;
    bool hasCrypto = false;
    if (refs) {
      for (unsigned i = 0, n = refs->count(); i < n; ++i) {
	const SentFrameRef &ref = (*refs)[i];
	switch (ref.kind) {
	  case SentFrameKind::Stream:
    ++m_txDiag.streamFramesTx;
    hasStream = true;
    break;
	  case SentFrameKind::Control:
    ++m_txDiag.controlFramesTx;
    hasControl = true;
    noteTxControlFrame_(ref.controlType);
    break;
	  case SentFrameKind::Crypto:
    ++m_txDiag.cryptoFramesTx;
    hasCrypto = true;
    break;
	  default:
    break;
	}
      }
    }
    if (hasStream && hasControl && hasAck)
      ++m_txDiag.ackStreamControlPacketsTx;
    else if (hasStream && hasControl)
      ++m_txDiag.streamControlPacketsTx;
    else if (hasStream && hasAck)
      ++m_txDiag.ackStreamPacketsTx;
    else if (hasControl && hasAck)
      ++m_txDiag.ackControlPacketsTx;
    else if (hasStream)
      ++m_txDiag.streamOnlyPacketsTx;
    else if (hasControl)
      ++m_txDiag.controlOnlyPacketsTx;
    else if (hasAck)
      ++m_txDiag.ackOnlyPacketsTx;
    else if (hasCrypto)
      ++m_txDiag.cryptoPacketsTx;
    else
      ++m_txDiag.otherPacketsTx;
  }

  bool recordProtPktTx_(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = 3, uint64_t ackLargest = 0) {
    bool recorded = recordRefs ?
      recordTxPkt_(
	level, pn, bytes, *recordRefs, ackEliciting, pmtudProbe, pmtudSize,
	ackLevel, ackLargest) :
      recordTxPkt_(level, pn, bytes, recordFrame, ackLevel, ackLargest);
    if (!recorded) return false;
    ++m_txPN[level];
    ++m_txDiag.packetsTx;
    m_txDiag.bytesTx += bytes;
    noteTxPktDiag_(recordRefs, ackLevel);
    ZquicLOG(app()->qlogTrace(), ([
      level, pn,
      packetBytes = bytes,
      bytesInFlight = m_congestion.bytesInFlight(),
      ackEliciting,
      ackLevel, ackLargest,
      ackRanges = qlogAckRanges_(ackLevel),
      hasRecordRefs = bool(recordRefs),
      recordRefs = recordRefs ? *recordRefs : TxPktRefs{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      unsigned frameCount = hasRecordRefs ? recordRefs.count() : 0;
      if (ackLevel < 3) ++frameCount;
      PktEvent event{
	.packetNumber = pn,
		.packetSize = packetBytes,
		.bytesInFlight = bytesInFlight,
		.frameCount = qlogCount_(frameCount),
		.ackEliciting = ackEliciting,
		.linkInfo = linkInfo
      };
      event.packetType = pktTypeFromPktNumSpace(level);
      event.packetSpace = level;
      event.ecn = EcnMark::N;
      qlogAddAckFrame_(event, ackLevel, ackLargest, ackRanges);
      if (hasRecordRefs)
	for (unsigned i = 0, n = recordRefs.count(); i < n; ++i)
	  qlogAddTxFrame_(event, recordRefs[i]);
      o.logPktSent(event, time);
    }));
    if (debugLog_()) ZiLOG(Debug, "Zquic",
      ([level, pn, bytes, ackEliciting](auto &s) {
	s << "packet sent level=" << int(level) <<
	" pn=" << pn <<
	  " bytes=" << bytes <<
	  " ackEliciting=" << ackEliciting;
    }));
    if (ackEliciting) {
      if (level == PktNumSpace::AppData && runtimeEstablished_())
	noteAckElicitingSentTx_();
      schedulePTO();
    }
    return true;
  }

  void discardProtPktTx_(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, bool ackEliciting,
    ZuBSpan recordFrame, const TxPktRefs *recordRefs) {
    m_txPkts[level].discard(pn);
    discardTxUnackd_(level, recordFrame, recordRefs);
    if (ackEliciting) {
      m_congestion.release(bytes);
      updateCongestionDiag_();
    }
  }

  static uint64_t qlogUS_(ZuTime t) {
    return uint64_t(t.microsecs());
  }

  static uint8_t qlogCount_(unsigned n) {
    return n > 255 ? 255 : uint8_t(n);
  }

  static void qlogAddAckRange_(
    ZquicLog_::FrameEvent &frame, uint64_t first, uint64_t largest) {
    if (frame.ackRanges.length() >= ZquicLog_::AckRangeMax) return;
    new (frame.ackRanges.push()) ZquicLog_::QAckRange{
      .first = first,
      .largest = largest
    };
  }

  static void qlogAddFrame_(
    ZquicLog_::PktEvent &event, const ZquicLog_::FrameEvent &frame) {
    if (event.frames.length() >= ZquicLog_::FrameMax) {
      event.framesTruncated = true;
      return;
    }
    event.frames.push(frame);
  }

  static void qlogAddFrame_(
    ZquicLog_::RecEvent &event, const ZquicLog_::FrameEvent &frame) {
    if (event.frames.length() < ZquicLog_::FrameMax)
      event.frames.push(frame);
  }

  static ZuArray<uint64_t, ZquicLog_::AckPacketMax> qlogAckedPNs_(
    const PktTxUpdate &update) {
    ZuArray<uint64_t, ZquicLog_::AckPacketMax> packetNumbers;
    for (unsigned i = 0; i < update.nAckedPNs; ++i)
      new (packetNumbers.push()) uint64_t(update.ackedPNs[i]);
    return packetNumbers;
  }

  ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax> qlogAckRanges_(
    uint8_t ackLevel) const {
    ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax> ranges;
    if (ackLevel >= 3) return ranges;
    const AckSnapshot &ack = m_txAck[ackLevel];
    for (unsigned i = 0, n = ack.nRanges; i < n; ++i) {
      if (ranges.length() >= ZquicLog_::AckRangeMax) break;
      new (ranges.push()) ZquicLog_::QAckRange{
	.first = ack.ranges[i].first,
	.largest = ack.ranges[i].largest
      };
    }
    return ranges;
  }

  static void qlogAddAckFrame_(
    ZquicLog_::PktEvent &event, uint8_t ackLevel, uint64_t ackLargest,
    const ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax>
      &ackRanges) {
    if (ackLevel >= 3) return;
    ZquicLog_::FrameEvent frame{
      .type = FrameType::Ack,
      .largestAcked = ackLargest,
      .rangeCount = qlogCount_(ackRanges.length())
    };
    for (unsigned i = 0, n = ackRanges.length(); i < n; ++i)
      qlogAddAckRange_(frame, ackRanges[i].first, ackRanges[i].largest);
    qlogAddFrame_(event, frame);
  }

  void qlogAddRxFrame_(
    ZquicLog_::PktEvent &event, PktNumSpace::T level,
    const Frame &frame_) {
    event.frameCount = qlogCount_(event.frameCount + 1);

    ZquicLog_::FrameEvent frame{.type = frame_.type};
    switch (frame_.type) {
      case FrameType::Ack:
	frame.largestAcked = frame_.offset;
	frame.ackDelayUS =
	  level == PktNumSpace::AppData ? qlogUS_(ackDelay_(frame_.value)) : 0;
	frame.rangeCount = qlogCount_(frame_.ackRanges.length());
	for (unsigned i = 0, n = frame_.ackRanges.length(); i < n; ++i)
	  qlogAddAckRange_(
    frame, frame_.ackRanges[i].first, frame_.ackRanges[i].largest);
	frame.ect0 = frame_.ackECN.ect0;
	frame.ect1 = frame_.ackECN.ect1;
	frame.ce = frame_.ackECN.ce;
	break;
      case FrameType::Crypto:
	frame.offset = frame_.offset;
	frame.length = frame_.length;
	break;
      case FrameType::Stream:
	frame.streamID = frame_.streamID;
	frame.offset = frame_.offset;
	frame.length = frame_.length;
	frame.fin = frame_.fin;
	break;
      case FrameType::ResetStream:
	frame.streamID = frame_.streamID;
	frame.errorCode = frame_.errorCode;
	frame.length = frame_.length;
	break;
      case FrameType::StopSending:
	frame.streamID = frame_.streamID;
	frame.errorCode = frame_.errorCode;
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
	frame.streamID = frame_.streamID;
	frame.value = frame_.value;
	break;
      case FrameType::MaxData:
      case FrameType::DataBlocked:
      case FrameType::RetireCxnID:
	frame.value = frame_.value;
	break;
      case FrameType::MaxStreams:
      case FrameType::StreamsBlocked:
	frame.value = frame_.value;
	frame.streamType = ZquicLog_::StreamType::T(frame_.streamType);
	break;
      case FrameType::NewCxnID:
	frame.offset = frame_.offset;
	frame.value = frame_.value;
	frame.length = frame_.length;
	frame.cxnID = Zquic::CxnID{frame_.payload};
	frame.resetToken = frame_.resetToken;
	break;
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	frame.errorCode = frame_.errorCode;
	frame.length = frame_.payload.length();
	break;
      case FrameType::NewToken:
      case FrameType::PathChallenge:
      case FrameType::PathResponse:
	frame.length = frame_.payload.length();
	break;
      default:
	break;
    }
    qlogAddFrame_(event, frame);
  }

  template <typename Event>
  static void qlogAddTxFrame_(Event &event, const SentFrameRef &ref) {
    switch (ref.kind) {
      case SentFrameKind::Stream:
	qlogAddFrame_(event, ZquicLog_::FrameEvent{
	  .type = FrameType::Stream,
	  .streamID = ref.streamID,
	  .offset = ref.offset,
	  .length = ref.length,
	  .fin = ref.fin
	});
	return;
      case SentFrameKind::Crypto:
	qlogAddFrame_(event, ZquicLog_::FrameEvent{
	  .type = FrameType::Crypto,
	  .offset = ref.offset,
	  .length = ref.length
	});
	return;
      case SentFrameKind::Control:
	qlogAddFrame_(event, ZquicLog_::FrameEvent{
	  .type = ref.controlType,
	  .streamID = ref.streamID,
	  .offset = ref.offset,
	  .length = ref.length,
	  .value = ref.value,
	  .errorCode =
    ref.controlType == FrameType::ResetStream ||
    ref.controlType == FrameType::StopSending ? ref.value : 0,
	  .streamType = ZquicLog_::StreamType::T(ref.streamType)
	});
	return;
      default:
	return;
    }
  }

  bool txAckMeta_(
    PktNumSpace::T level, PktBuild &payload,
    uint8_t &ackLevel, uint64_t &ackLargest) const {
    ackLevel = 3;
    ackLargest = 0;
    if (!payload.ack(level)) return false;
    const AckSnapshot &ack = m_txAck[level];
    if (!ack.nRanges) return false;
    ackLevel = uint8_t(level);
    ackLargest = ack.ranges[ack.nRanges - 1].largest;
    return true;
  }

  const auto &initialKeys_(InitialKeyDir::T dir) const {
    return dir == InitialKeyDir::Client ?
      m_crypto.initialKeys().client :
      m_crypto.initialKeys().server;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtInitialPkt_(
    InitialKeyDir::T keyDir, RuntimeCID::T dcid, RuntimeCID::T scid,
    unsigned pnLength, ZuBSpan token, bool padInitial, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Initial packet protection outside Tx thread", return false);
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    const auto &initialKeys = initialKeys_(keyDir);
    int headerLen = -1;
    unsigned targetPlainLen = payload.bytes();
    for (unsigned i = 0; i < 4; ++i) {
      headerLen = Pkt::writeInitial(
	buf->data_(), buf->size, runtimeCID_(dcid), runtimeCID_(scid),
	token, targetPlainLen + InitialSecret::TagLen, pnLength);
      if (headerLen < 0) return false;
      if (!padInitial) break;
      unsigned minPlainLen = MinUDPPayload -
	unsigned(headerLen) - pnLength - InitialSecret::TagLen;
      if (minPlainLen <= targetPlainLen) break;
      targetPlainLen = minPlainLen;
    }
    if (padInitial && !payload.padTo(targetPlainLen)) return false;
    if (PktNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[PktNumSpace::Initial], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[PktNumSpace::Initial];
    int n = InitialPktProt::protectLongV(
      buf->data_(), buf->size, initialKeys, pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Initial,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvent event{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false,
	  .linkInfo = linkInfo
		};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvent(EventName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(PktNumSpace::Initial, payload, ackLevel, ackLargest);
    if (!recordProtPktTx_(
	  PktNumSpace::Initial, pn, unsigned(n), recordFrame, recordRefs,
	  ackEliciting, false, 0, ackLevel, ackLargest))
      return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
      discardProtPktTx_(
	PktNumSpace::Initial, pn, unsigned(n), ackEliciting,
	recordFrame, recordRefs);
      return false;
    }
    if (payload.ack(PktNumSpace::Initial)) ackSentTx_(PktNumSpace::Initial);
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtHandshakePkt_(
    RuntimeCID::T dcid, RuntimeCID::T scid, unsigned pnLength,
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Handshake packet protection outside Tx thread", return false);
    if (!txTrafficSecretInstalled_(PktNumSpace::Handshake)) {
      ++m_txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Handshake,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvent event{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false,
	  .linkInfo = linkInfo
		};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvent(EventName::PktProtectFail, event, time);
      }));
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    int headerLen = Pkt::writeHandshake(
      buf->data_(), buf->size, runtimeCID_(dcid), runtimeCID_(scid),
      payload.bytes() +
	txTrafficSecret_(PktNumSpace::Handshake).tagLen,
      pnLength);
    if (headerLen < 0 ||
	PktNumber::encode(
	  buf->data_() + headerLen, buf->size - unsigned(headerLen),
	  m_txPN[PktNumSpace::Handshake], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_txPN[PktNumSpace::Handshake];
    int n = PktProt::protectLongV(
      buf->data_(), buf->size,
      txProtState_(PktNumSpace::Handshake), pn,
      byteSpan(buf->data_(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Handshake,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvent event{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false,
	  .linkInfo = linkInfo
		};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvent(EventName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(PktNumSpace::Handshake, payload, ackLevel, ackLargest);
    if (!recordProtPktTx_(
	  PktNumSpace::Handshake, pn, unsigned(n), recordFrame, recordRefs,
	  ackEliciting, false, 0, ackLevel, ackLargest))
      return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
      discardProtPktTx_(
	PktNumSpace::Handshake, pn, unsigned(n), ackEliciting,
	recordFrame, recordRefs);
      return false;
    }
    if (payload.ack(PktNumSpace::Handshake))
      ackSentTx_(PktNumSpace::Handshake);
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtShortPkt_(
    RuntimeCID::T dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Short packet protection outside Tx thread", return false);
    if (!runtimeEstablished_() &&
		!txTrafficSecretInstalled_(PktNumSpace::AppData)) {
      ++m_txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvent event{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false,
	  .linkInfo = linkInfo
		};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvent(EventName::PktProtectFail, event, time);
      }));
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    int headerLen = Pkt::writeShort(
      buf->data_(), buf->size, runtimeCID_(dcid),
      m_txPN[PktNumSpace::AppData], pnLength, m_txKeyPhase);
    if (headerLen < 0) return false;
    if (!payload.padForProtSample(
	  unsigned(headerLen) - pnLength, pnLength,
	  txTrafficSecret_(PktNumSpace::AppData).tagLen))
      return false;
    if (pmtudSize) {
      unsigned tagLen = txTrafficSecret_(PktNumSpace::AppData).tagLen;
      unsigned headerBytes = unsigned(headerLen);
      if (pmtudSize <= headerBytes + tagLen) return false;
      if (!payload.padTo(pmtudSize - headerBytes - tagLen)) return false;
    }
    uint64_t pn = m_txPN[PktNumSpace::AppData];
    int n = PktProt::protectShortV(
      buf->data_(), buf->size,
      txProtState_(PktNumSpace::AppData), pn,
      byteSpan(buf->data_(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - pnLength, pnLength);
    if (n < 0) {
      ++m_txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvent event{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false,
	  .linkInfo = linkInfo
		};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvent(EventName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->skip = 0;
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta_(PktNumSpace::AppData, payload, ackLevel, ackLargest);
    if (!recordProtPktTx_(
	  PktNumSpace::AppData, pn, unsigned(n), recordFrame, recordRefs,
	  ackEliciting, pmtudSize != 0, pmtudSize, ackLevel, ackLargest))
      return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
      discardProtPktTx_(
	PktNumSpace::AppData, pn, unsigned(n), ackEliciting,
	recordFrame, recordRefs);
      return false;
    }
    if (payload.ack(PktNumSpace::AppData)) ackSentTx_(PktNumSpace::AppData);
    return true;
  }

  template <typename ReceiveLong, typename ReceiveShort>
  void receiveDatagram_(
    Datagram d, ReceiveLong receiveLong, ReceiveShort receiveShort) {
    ++m_rxDiag.datagramsRx;
    if (!d.buf) {
      ++m_rxDiag.failures;
      return;
    }
    m_rxDiag.bytesRx += d.buf->length;
    recordPathRx_(d.buf->length);
    bool ok = true;
    unsigned offset = 0;
    while (offset < d.buf->length) {
      ZuBSpan packet{
	d.buf->data_() + offset, d.buf->length - offset};
      if (Pkt::isLong(packet)) {
	LongHdr h;
	if (Pkt::parseLong(packet, h) < 0) {
	  ZquicLOG(app()->qlogTrace(), ([
	    level = PktNumSpace::Initial,
	    packetBytes = packet.length(),
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    PktEvent event{.packetSize = packetBytes};
	    event.packetType = PktType::N;
	    event.packetSpace = level;
	    event.ecn = EcnMark::N;
	    event.reason = PktEvent::Reason::ParseLong;
	    event.linkInfo = linkInfo;
	    o.logPktDrop(event, time);
	  }));
	  ok = false;
	  break;
	}
	uint64_t packetLen = uint64_t(h.pnOffset) + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset ||
    packetLen > UINT_MAX) {
	  ZquicLOG(app()->qlogTrace(), ([
	    packetType = h.type,
	    level = PktNumSpace::Initial,
	    packetBytes = packet.length(),
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    PktEvent event{.packetSize = packetBytes};
	    event.packetType = packetType;
	    event.packetSpace = level;
	    event.ecn = EcnMark::N;
	    event.reason = PktEvent::Reason::PacketLength;
	    event.linkInfo = linkInfo;
	    o.logPktDrop(event, time);
	  }));
	  ok = false;
	  break;
	}
	if (!receiveLong(d, offset, unsigned(packetLen))) ok = false;
	offset += unsigned(packetLen);
	continue;
      }
      if (!receiveShort(d, offset, d.buf->length - offset)) ok = false;
      break;
    }
    if (!ok) ++m_rxDiag.failures;
    ZquicLOG(app()->qlogTrace(), ([
      bytes = d.buf->length,
      ecn = d.ecn,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvent event{.size = bytes, .linkInfo = linkInfo};
      event.ecn = ecn;
      o.logDgramRecv(event, time);
    }));
  }

  template <typename PrepareLong, typename ConsumeFrames>
  bool receiveProtLongPkt_(
    InitialKeyDir::T keyDir, Datagram &d, unsigned packetOffset, unsigned packetLen,
    PrepareLong prepareLong, ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuBSpan packet{base, packetLen};
    auto datagram = d.buf->cspan();
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) {
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Initial,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = PktType::N;
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::ParseLong;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    if (!prepareLong(h, d)) {
      ZquicLOG(app()->qlogTrace(), ([
		packetType = h.type,
		level = PktNumSpace::Initial,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = packetType;
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::PrepareLong;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    PktNumSpace::T level =
      h.type == PktType::Initial ? PktNumSpace::Initial :
      h.type == PktType::Handshake ? PktNumSpace::Handshake :
      PktNumSpace::AppData;
    if (h.type != PktType::Initial && h.type != PktType::Handshake) {
      ZquicLOG(app()->qlogTrace(), ([
		packetType = h.type,
		packetSpace = h.type == PktType::ZeroRTT ?
	  PktNumSpace::AppData : PktNumSpace::N,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = packetType;
		event.packetSpace = packetSpace;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::UnsupportedLongType;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    if (m_rxSpaceDiscarded[level]) {
      ZquicLOG(app()->qlogTrace(), ([
		level, packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::DiscardedSpace;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return true;
    }
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = -1;
    if (level == PktNumSpace::Initial)
      plainLen = InitialPktProt::unprotectLong(
	base, packetLen, initialKeys_(keyDir),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_crypto.rxTrafficSecretInstalled(PktNumSpace::Handshake)) {
	++m_rxDiag.failures;
		ZquicLOG(app()->qlogTrace(), ([
	  level, packetBytes = packetLen,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  PktEvent event{.packetSize = packetBytes};
	  event.packetType = pktTypeFromPktNumSpace(level);
	  event.packetSpace = level;
	  event.ecn = EcnMark::N;
	  event.reason = PktEvent::Reason::MissingKeys;
	  event.linkInfo = linkInfo;
	  o.logPktDrop(event, time);
	  SecEvent security{
	    .kind = SecKind::PktProtect,
	    .reason = SecReason::MissingKeys,
	    .success = false,
	    .linkInfo = linkInfo
	  };
	  security.packetSpace = level;
	  security.trigger = SecTrigger::RX;
	  o.logSecEvent(
    EventName::PktProtectFail, security, time);
	}));
	return false;
      }
      plainLen = PktProt::unprotectLong(
	base, packetLen,
	m_crypto.rxProtState(PktNumSpace::Handshake),
	m_rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      if (checkStatelessReset_(datagram, true)) return true;
      ++m_rxDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level, packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::Protection;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
		SecEvent security{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protection,
	  .success = false,
	  .linkInfo = linkInfo
		};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvent(
	  EventName::PktProtectFail, security, time);
      }));
      return false;
    }
    if (rxPktSeen_(level, pn)) {
      ++m_rxDiag.duplicatePacketsRx;
      ZquicLOG(app()->qlogTrace(), ([
		level, packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::Duplicate;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return true;
    }
    ++m_rxDiag.packetsRx;
    if (runtimeDraining_()) return true;
    if (runtimeClosing_()) {
      noteClosingPacket_(d.addr);
      return true;
    }
    RxAckMeta ack;
    ZiSockAddr ackAddr = d.addr;
    ZuElem<ZquicLog_::PktEvent> qlog;
    ZquicLog_::PktEvent *qlog_ = nullptr;
    if (ZquicLogger::enabled()) {
      new (&qlog.v) ZquicLog_::PktEvent{};
      qlog_ = &qlog.v;
    }
    ZuGuard qlogGuard{[qlog_]() {
      if (qlog_) qlog_->~PktEvent();
    }};
    if (!consumeFrames(
      level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr, d.buf, ack, qlog_))
      return false;
    ZquicLOG(app()->qlogTrace(), ([
      level, pn,
      packetBytes = packetLen,
      payloadBytes = unsigned(plainLen),
      ecn = d.ecn,
      event = qlog_ ? ZuMv(*qlog_) : PktEvent{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = pktTypeFromPktNumSpace(level);
      event.packetSpace = level;
      event.ecn = ecn;
      event.packetNumber = pn;
      event.packetSize = packetBytes;
      event.payloadSize = payloadBytes;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    noteAck_(
      level, pn, ack.ackEliciting, ZuMv(ackAddr), ack.immediateAck, d.ecn);
    return true;
  }

  template <typename ConsumeFrames>
  bool receiveProtShortPkt_(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data_() + packetOffset;
    ZuBSpan packet{base, packetLen};
    auto datagram = d.buf->cspan();
    if (!m_crypto.rxTrafficSecretInstalled(PktNumSpace::AppData)) {
      if (checkStatelessReset_(datagram, true)) return true;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::MissingKeys;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
		SecEvent security{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false,
	  .linkInfo = linkInfo
		};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvent(
	  EventName::PktProtectFail, security, time);
      }));
      return false;
    }
    ShortHdr h;
    if (Pkt::parseShort(packet, m_localSCID.length(), h) < 0 ||
		!(h.dcid == m_localSCID)) {
      if (checkStatelessReset_(datagram, true)) return true;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::ParseShort;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PktProt::unprotectShort(
      base, packetLen,
      m_crypto.rxProtState(PktNumSpace::AppData),
      m_rxLargestPN[PktNumSpace::AppData],
      h.pnOffset, pn, payloadOffset);
    if (plainLen < 0) {
      if (m_rxOldProt.valid()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_rxOldProt,
	  m_rxLargestPN[PktNumSpace::AppData],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  if (!((base[0] & 0x04) == (m_rxOldKeyPhase ? 0x04 : 0))) {
    ++m_rxDiag.invalidKeyPhases;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::AppData,
	      packetBytes = packetLen,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      PktEvent event{.packetSize = packetBytes};
	      event.packetType = pktTypeFromPktNumSpace(level);
	      event.packetSpace = level;
	      event.ecn = EcnMark::N;
	      event.reason = PktEvent::Reason::BadKeyPhase;
	      event.linkInfo = linkInfo;
	      o.logPktDrop(event, time);
	      SecEvent security{
			.kind = SecKind::PktProtect,
			.reason = SecReason::BadKeyPhase,
			.success = false,
			.linkInfo = linkInfo
	      };
      security.packetSpace = level;
      security.trigger = SecTrigger::RX;
      o.logSecEvent(
		EventName::PktProtectFail, security, time);
    }));
    return false;
	  }
	  ++m_rxDiag.oldKeysAccepted;
	}
      }
      if (plainLen < 0 && ensureNextPeerKey_()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_rxNextProt,
	  m_rxLargestPN[PktNumSpace::AppData],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  bool phase = base[0] & 0x04;
	  if (phase == m_rxKeyPhase ||
      m_rxOldProt.valid() ||
      pn <= m_rxLargestPN[PktNumSpace::AppData]) {
    ++m_rxDiag.invalidKeyPhases;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::AppData,
	      packetBytes = packetLen,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      PktEvent event{.packetSize = packetBytes};
	      event.packetType = pktTypeFromPktNumSpace(level);
	      event.packetSpace = level;
	      event.ecn = EcnMark::N;
	      event.reason = PktEvent::Reason::BadKeyPhase;
	      event.linkInfo = linkInfo;
	      o.logPktDrop(event, time);
	      SecEvent security{
			.kind = SecKind::PktProtect,
			.reason = SecReason::BadKeyPhase,
			.success = false,
			.linkInfo = linkInfo
	      };
      security.packetSpace = level;
      security.trigger = SecTrigger::RX;
      o.logSecEvent(
		EventName::PktProtectFail, security, time);
    }));
    return false;
	  }
	  if (!commitPeerKeyUpdate_(m_rxNextTrafficSecret))
    return false;
	}
      }
      if (plainLen < 0) {
	if (checkStatelessReset_(datagram, true)) return true;
	++m_rxDiag.failures;
		ZquicLOG(app()->qlogTrace(), ([
	  level = PktNumSpace::AppData,
	  packetBytes = packetLen,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  PktEvent event{.packetSize = packetBytes};
	  event.packetType = pktTypeFromPktNumSpace(level);
	  event.packetSpace = level;
	  event.ecn = EcnMark::N;
	  event.reason = PktEvent::Reason::Protection;
	  event.linkInfo = linkInfo;
	  o.logPktDrop(event, time);
	  SecEvent security{
	    .kind = SecKind::PktProtect,
	    .reason = SecReason::Protection,
	    .success = false,
	    .linkInfo = linkInfo
	  };
	  security.packetSpace = level;
	  security.trigger = SecTrigger::RX;
	  o.logSecEvent(
    EventName::PktProtectFail, security, time);
	}));
	return false;
      }
    } else if ((base[0] & 0x04) != (m_rxKeyPhase ? 0x04 : 0)) {
      ++m_rxDiag.invalidKeyPhases;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::BadKeyPhase;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
		SecEvent security{
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::BadKeyPhase,
	  .success = false,
	  .linkInfo = linkInfo
		};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvent(
	  EventName::PktProtectFail, security, time);
      }));
      return false;
    }
    if (rxPktSeen_(PktNumSpace::AppData, pn)) {
      ++m_rxDiag.duplicatePacketsRx;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvent event{.packetSize = packetBytes};
		event.packetType = pktTypeFromPktNumSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = PktEvent::Reason::Duplicate;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return true;
    }
    ++m_rxDiag.packetsRx;
    if (runtimeDraining_()) return true;
    if (runtimeClosing_()) {
      noteClosingPacket_(d.addr);
      return true;
    }
    RxAckMeta ack;
    ZiSockAddr ackAddr = d.addr;
    ZuElem<ZquicLog_::PktEvent> qlog;
    ZquicLog_::PktEvent *qlog_ = nullptr;
    if (ZquicLogger::enabled()) {
      new (&qlog.v) ZquicLog_::PktEvent{};
      qlog_ = &qlog.v;
    }
    ZuGuard qlogGuard{[qlog_]() {
      if (qlog_) qlog_->~PktEvent();
    }};
    if (!consumeFrames(
      PktNumSpace::AppData, pn,
      byteSpan(base + payloadOffset, unsigned(plainLen)),
      d.addr, d.buf, ack, qlog_))
      return false;
    ZquicLOG(app()->qlogTrace(), ([
      level = PktNumSpace::AppData,
      pn,
      packetBytes = packetLen,
      payloadBytes = unsigned(plainLen),
      ecn = d.ecn,
      event = qlog_ ? ZuMv(*qlog_) : PktEvent{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = pktTypeFromPktNumSpace(level);
      event.packetSpace = level;
      event.ecn = ecn;
      event.packetNumber = pn;
      event.packetSize = packetBytes;
      event.payloadSize = payloadBytes;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    noteAck_(
      PktNumSpace::AppData, pn, ack.ackEliciting, ZuMv(ackAddr),
      ack.immediateAck, d.ecn);
    return true;
  }

  template <typename EmitTLS, typename HandleControl>
  bool consumeProtFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, RxAckMeta &ackMeta,
    ZquicLog_::PktEvent *qlog, EmitTLS emitTLS,
    HandleControl handleControl) {
    unsigned offset = 0;
    auto frame_ = ZmAlloc(Frame, 1);
    new (&frame_[0]) Frame{};
    auto &frame = frame_[0];
    ZuGuard frameGuard{[&frame]() { frame.~Frame(); }};
    bool ackEliciting = false;
    bool immediateAck = false;

    while (offset < frames.length()) {
      unsigned used = 0;
      if (FrameCodec::parse(
	  ZuBSpan{frames.data() + offset, frames.length() - offset},
	  frame, used) < 0 || !used)
	return false;
      ++m_rxDiag.framesRx;
      if (!packetFrameLegal_(level, frame)) return false;
      if (qlog) qlogAddRxFrame_(*qlog, level, frame);
      if (FrameCodec::ackEliciting(frame.type))
	ackEliciting = true;
      switch (frame.type) {
	case FrameType::Ack:
	  processAckFrame_(level, frame);
	  break;
	case FrameType::Crypto: {
	  ZuBSpan contiguous;
	  m_rxCryptoAddr[level] = addr;
	  if (m_rxCrypto[level].receiveFrame(frame, contiguous) < 0)
	    return false;
	  m_rxDiag.cryptoBytesRx += frame.payload.length();
	  if (contiguous) {
	    size_t epoch = 0;
	    if (!tlsEpochFromPktNumSpace(level, epoch)) return false;
	    if (!emitTLS(epoch, contiguous, addr)) return false;
	  }
	  break;
	}
	case FrameType::Stream:
	  m_rxDiag.streamBytesRx += frame.payload.length();
	  if (receiveFrame(
      frame, ZmRef<ZiIOBuf>{packetBuf}, nullptr, &immediateAck) < 0)
    return false;
	  ZquicLOG(app()->qlogTrace(), ([
	    streamID = frame.streamID,
	    offset = frame.offset,
	    length = uint64_t(frame.payload.length()),
	    fin = frame.fin,
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    if (!length && !fin) return;
	    StreamDataEvent event{
      .from = StreamDataLoc::Network,
      .to = StreamDataLoc::Transport,
      .additionalInfo = fin ?
		StreamDataInfo::T(StreamDataInfo::FinSet) :
		StreamDataInfo::T(StreamDataInfo::None),
	      .streamID = streamID,
	      .offset = offset,
	      .length = length,
	      .linkInfo = linkInfo
	    };
    o.logStreamDataMoved(event, time);
	  }));
	  impl()->streamFrame(
    frame.streamID, frame.offset, frame.payload, frame.fin);
	  break;
	case FrameType::ResetStream:
	case FrameType::StopSending:
	  if (receiveFrame(frame) < 0) return false;
	  break;
	case FrameType::NewCxnID:
	  if (!receiveNewCxnID_(frame)) return false;
	  break;
	case FrameType::RetireCxnID:
	  if (!receiveRetireCxnID_(frame)) return false;
	  break;
	case FrameType::MaxData:
	  if (!rxApplyMaxData_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreamData:
	  if (!rxApplyMaxStreamData_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::MaxStreams:
	  if (!rxApplyMaxStreams_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::DataBlocked:
	  if (!validateDataBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::StreamDataBlocked:
	  if (!receiveStreamDataBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::StreamsBlocked:
	  if (!validateStreamsBlocked_(frame) ||
	      !handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::PathChallenge:
	case FrameType::PathResponse:
	case FrameType::ConnectionClose:
	case FrameType::ApplicationClose:
	case FrameType::HandshakeDone:
	  if (!handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::Padding:
	case FrameType::Ping:
	  break;
	case FrameType::NewToken:
	  impl()->newToken_(frame.payload);
	  break;
	default:
	  return false;
	}
      offset += used;
    }
    ackMeta.ackEliciting = ackEliciting;
    ackMeta.immediateAck = immediateAck;
    return true;
  }

private:
  bool packetFrameLegal_(PktNumSpace::T level, const Frame &frame) const {
    switch (frame.type) {
      case FrameType::Unknown:
	return false;
      case FrameType::Padding:
      case FrameType::Ping:
      case FrameType::ConnectionClose:
	return true;
      case FrameType::Ack:
	return true;
      case FrameType::Crypto:
	return level != PktNumSpace::AppData || runtimeEstablished_();
      case FrameType::Stream:
      case FrameType::ResetStream:
      case FrameType::StopSending:
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
      case FrameType::DataBlocked:
      case FrameType::StreamDataBlocked:
      case FrameType::StreamsBlocked:
      case FrameType::NewCxnID:
      case FrameType::RetireCxnID:
      case FrameType::PathChallenge:
      case FrameType::PathResponse:
	return level == PktNumSpace::AppData;
      case FrameType::NewToken:
	return !m_isServer && level == PktNumSpace::AppData &&
	  frame.payload.length();
      case FrameType::ApplicationClose:
	return level == PktNumSpace::AppData;
      case FrameType::HandshakeDone:
	return !m_isServer && level == PktNumSpace::AppData;
      default:
	return false;
    }
  }

  StreamRef openLocalStream_(Zi::StreamType::T type) {
    return newStream_(nextStreamID_(type));
  }

  unsigned openQueued_(Zi::StreamType::T type, unsigned limit) {
    uint64_t &queued = m_queued[type];
    unsigned opened = 0;
    while (queued && opened < limit && m_peerLimit[type].open()) {
      StreamRef stream = openLocalStream_(type);
      if (!stream) break;
      --queued;
      ++opened;
      streamQueuedOpen_(stream);
      scheduleStreamWritable_(stream);
    }
    return opened;
  }
  void streamQueuedOpen_(StreamRef stream) {
    if (!stream) return;
    app()->rxRun([link = impl(), stream = ZuMv(stream)]() mutable {
      if (link->disconnecting_()) return;
      link->streamed(ZuMv(stream));
    });
  }
  void scheduleOpenQueued_(Zi::StreamType::T type) {
    unsigned i = type == Zi::StreamType::Simplex ? 1 : 0;
    if (!m_queued[type] || m_peerLimit[type].blocked() ||
	m_openQueuedPending[i])
      return;
    m_openQueuedPending[i] = true;
    app()->txRun([link = impl(), type, i]() mutable {
      if (link->disconnecting_()) return;
      link->m_openQueuedPending[i] = false;
      link->openQueued_(type, OpenQueuedBatch);
      link->scheduleOpenQueued_(type);
    });
  }

  StreamRef findOrAccept_(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (StreamID::server(id) == m_isServer) return nullptr;
    return acceptPeerStream(id);
  }

  static bool localInitiated_(uint64_t id, bool isServer) {
    return StreamID::server(id) == isServer;
  }
  static unsigned streamTypeIndex_(Zi::StreamType::T type) {
    return type == Zi::StreamType::Simplex ? 1 : 0;
  }
  bool canPeerSend_(uint64_t id) const {
    return !StreamID::uni(id) || !localInitiated_(id, m_isServer);
  }
  bool canLocalSend_(uint64_t id) const {
    return !StreamID::uni(id) || localInitiated_(id, m_isServer);
  }
  bool peerOpenedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX) || localInitiated_(id, m_isServer))
      return false;
    Zi::StreamType::T type = StreamID::uni(id);
    return StreamID::ordinal(id) < m_localLimit[type].opened();
  }
  bool localOpenedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX) || !localInitiated_(id, m_isServer))
      return false;
    uint64_t opened = StreamID::uni(id) ? m_nextUniOrdinal : m_nextBidiOrdinal;
    return StreamID::ordinal(id) < opened;
  }
  bool closedStreamID_(uint64_t id) const {
    if (id > uint64_t(INT64_MAX)) return false;
    unsigned i = unsigned(id & 3);
    uint64_t ordinal = StreamID::ordinal(id);
    if (ordinal < m_closedStreamBase[i]) return true;
    return m_closedStreams->find(id);
  }
  void closeStreamID_(uint64_t id) {
    unsigned i = unsigned(id & 3);
    uint64_t ordinal = StreamID::ordinal(id);
    uint64_t &base = m_closedStreamBase[i];
    if (ordinal < base) return;
    if (ordinal > base) {
      m_closedStreams->add(id, true);
      return;
    }
    ++base;
    for (;;) {
      uint64_t next = (base << 2) | i;
      if (!m_closedStreams->del(next)) return;
      ++base;
    }
  }

  bool validateMaxData_(const Frame &frame) const {
    return frame.type == FrameType::MaxData;
  }
  bool validateMaxStreams_(const Frame &frame) const {
    return frame.type == FrameType::MaxStreams &&
      frame.value <= MaxStreamCount;
  }
  bool validateMaxStreamData_(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::MaxStreamData ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canLocalSend_(frame.streamID))
      return false;
    stream = findStream(int64_t(frame.streamID));
    if (stream) return true;
    if (localInitiated_(frame.streamID, m_isServer)) return false;
    if (StreamID::uni(frame.streamID)) return false;
    stream = acceptPeerStream(frame.streamID);
    return stream;
  }
  bool validateDataBlocked_(const Frame &frame) const {
    return frame.type == FrameType::DataBlocked &&
      frame.value <= m_rxDataCredit.limit();
  }
  bool validateStreamDataBlocked_(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::StreamDataBlocked ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canPeerSend_(frame.streamID))
      return false;
    stream = findOrAccept_(frame.streamID);
    return stream && frame.value <= stream->rxCreditLimit();
  }
  bool validateStreamsBlocked_(const Frame &frame) const {
    return frame.type == FrameType::StreamsBlocked &&
      frame.value <= m_localLimit[frame.streamType].limit();
  }

  bool receiveStreamDataBlocked_(const Frame &frame) {
    if (frame.type == FrameType::StreamDataBlocked &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetReceived() || stream->rxComplete()) {
	  ++m_rxDiag.streamBlockedClosedRx;
	  noteInvalidStreamActivity_(TransportError::StreamState, true);
	  return true;
	}
      } else if (closedStreamID_(frame.streamID) &&
	  canPeerSend_(frame.streamID)) {
	++m_rxDiag.streamBlockedClosedRx;
	noteInvalidStreamActivity_(TransportError::StreamState, true);
	return true;
      }
    }
    StreamRef stream;
    if (!validateStreamDataBlocked_(frame, stream)) {
      ++m_rxDiag.streamBlockedInvalidRx;
      noteInvalidStreamActivity_(TransportError::StreamState);
      return false;
    }
    if (!stream->receiveBlocked(frame)) {
      ++m_rxDiag.streamBlockedFinalRx;
      noteInvalidStreamActivity_(TransportError::FinalSize, false, true);
      return false;
    }
    return true;
  }
  bool blockedFrameNeeded_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::DataBlocked:
	return m_lastDataBlocked != frame.value;
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return !stream || stream->lastStreamDataBlocked() != frame.value;
      }
      case FrameType::StreamsBlocked:
	return m_lastStreamsBlocked[frame.streamType] != frame.value;
      default:
	return true;
    }
  }
  void noteBlockedQueued_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::DataBlocked:
	m_lastDataBlocked = frame.value;
	break;
      case FrameType::StreamDataBlocked:
	if (StreamRef stream = findStream(int64_t(frame.streamID)))
	  stream->lastStreamDataBlocked(frame.value);
	break;
      case FrameType::StreamsBlocked:
	m_lastStreamsBlocked[frame.streamType] = frame.value;
	break;
      default:
	break;
    }
  }
  void noteControlDequeued_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::StreamsBlocked:
	m_lastStreamsBlocked[frame.streamType] = U64Null;
	break;
      default:
	break;
    }
  }

  bool queuePendingControl_(PendingControl &slot, const ControlFrame &frame) {
    if (!frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame) {
      switch (frame.type) {
	case FrameType::MaxData:
	case FrameType::MaxStreams:
	case FrameType::DataBlocked:
	case FrameType::StreamsBlocked:
	  if (slot.frame.value >= frame.value) return false;
	  break;
	case FrameType::HandshakeDone:
	  return false;
	default:
	  break;
      }
    }
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  void clearPendingControls_() {
    m_maxDataControl = {};
    m_dataBlockedControl = {};
    m_handshakeDoneControl = {};
    m_pathChallengeControl = {};
    for (unsigned i = 0; i < 2; ++i) {
      m_maxStreamsControl[i] = {};
      m_streamsBlockedControl[i] = {};
    }
    m_pathResponses.clean();
    auto iter = m_streams->iter();
    while (auto node = iter()) node->data().clearControls();
  }

  bool controlStillValid_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return m_maxDataControl.frame == frame &&
	  frame.value == m_rxDataCredit.limit();
      case FrameType::MaxStreamData: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::MaxStreams:
	return m_maxStreamsControl[streamTypeIndex_(frame.streamType)].frame ==
	  frame && frame.value == m_localLimit[frame.streamType].limit();
      case FrameType::DataBlocked:
	return m_dataBlockedControl.frame == frame &&
	  m_txDataCredit.blocked() &&
	  frame.value == m_txDataCredit.limit();
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StreamsBlocked:
	return m_streamsBlockedControl[streamTypeIndex_(frame.streamType)].frame ==
	  frame && m_queued[frame.streamType] &&
	  frame.value == m_peerLimit[frame.streamType].limit();
      case FrameType::ResetStream: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StopSending: {
	StreamRef stream = findStream(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::PathChallenge:
	return m_pathChallengeControl.frame == frame &&
	  m_validatingPath.active &&
	  m_validatingPath.challenge.equals(byteSpan(
	    frame.payload, sizeof(frame.payload)));
      case FrameType::PathResponse:
	return true;
      case FrameType::HandshakeDone:
	return m_handshakeDoneControl.frame == frame;
      default:
	return false;
    }
  }

  void maybeExtendMaxData_() {
    uint64_t window = m_transportParams.initialMaxData;
    if (!window || m_rxDataCredit.available() > window / 2) return;
    uint64_t maximum =
      m_rxDataCredit.used() > U64Null - window ? U64Null :
      m_rxDataCredit.used() + window;
    if (maximum <= m_rxDataCredit.limit()) return;
    m_rxDataCredit.extend(maximum);
    queueFlowUpdate_(
      FlowUpdate{FrameType::MaxData, 0, maximum, Zi::StreamType::Duplex});
  }
  void maybeExtendMaxStreamData_(const StreamRef &stream) {
    if (!stream || !stream->readOpen()) return;
    uint64_t window = initialStreamRxCredit_(uint64_t(stream->id()));
    if (!window || stream->rxCreditAvailable() > window / 2) return;
    uint64_t maximum =
      stream->rxCreditUsed() > U64Null - window ? U64Null :
      stream->rxCreditUsed() + window;
    if (maximum <= stream->rxCreditLimit()) return;
    stream->extendRxCredit(maximum);
    queueFlowUpdate_(FlowUpdate{
      FrameType::MaxStreamData, uint64_t(stream->id()), maximum,
      Zi::StreamType::Duplex});
  }
  bool returnStreamCredit_(const StreamRef &stream) {
    if (!stream || stream->id() < 0 || stream->streamCreditReturned())
      return false;
    uint64_t id = uint64_t(stream->id());
    if (localInitiated_(id, m_isServer)) return false;
    if (!stream->rxComplete() && !stream->resetReceived()) return false;
    if (!StreamID::uni(id) && !stream->finDequeued() && !stream->resetSent())
      return false;
    Zi::StreamType::T type = StreamID::uni(id);
    StreamLimit &limit = m_localLimit[type];
    if (limit.limit() >= MaxStreamCount) return false;
    uint64_t next = limit.limit() + 1;
    limit.extend(next);
    stream->markStreamCreditReturned();
    queueFlowUpdate_(FlowUpdate{FrameType::MaxStreams, 0, limit.limit(), type});
    return true;
  }

  bool txClosed_(const Stream *stream) const {
    if (!canLocalSend_(uint64_t(stream->id()))) return true;
    if (stream->resetSent()) return stream->resetAckd();
    if (!stream->finSent()) return false;
    return stream->finDequeued() && !stream->txUnackdCount();
  }
  bool rxClosed_(const Stream *stream) const {
    if (!canPeerSend_(uint64_t(stream->id()))) return true;
    return stream->rxComplete() || stream->resetReceived();
  }
  bool streamReapable_(const Stream *stream) const {
    if (!stream || stream->id() < 0) return false;
    if (stream->txQueued() || stream->txRangeCount() ||
	stream->txBufferedBytes() || stream->txUnackdCount() ||
	stream->rxPending() || stream->rxQueued())
      return false;
    if (!rxClosed_(stream) || !txClosed_(stream)) return false;
    if (!streamCreditSettled_(stream))
      return false;
    return true;
  }
  bool reapStream_(Stream *stream) {
    if (!streamReapable_(stream)) return false;
    int64_t id = stream->id();
    ZquicLOG(app()->qlogTrace(), ([
      streamID = uint64_t(id),
      streamType = StreamType::T(StreamID::uni(uint64_t(id))),
      streamSide = localInitiated_(uint64_t(id), m_isServer) ?
	StreamSide::T(StreamSide::Sending) :
	StreamSide::T(StreamSide::Receiving),
      reason = StreamReason::T(StreamReason::Reaped),
      offset = stream->rxBytes(),
      length = stream->txBytes(),
      errorCode = stream->appError(),
      fin = stream->finReceived() || stream->finSent(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      StreamEvent event{
		.streamType = streamType,
	.oldState = StreamState::Open,
	.newState = StreamState::Closed,
	.streamSide = streamSide,
	.reason = reason,
	.streamID = streamID,
		.offset = offset,
		.length = length,
		.errorCode = errorCode,
		.fin = fin,
		.linkInfo = linkInfo
      };
      o.logStreamStateUpd(event, time);
    }));
    closeStreamID_(uint64_t(id));
    return m_streams->del(id);
  }
  bool streamCreditSettled_(const Stream *stream) const {
    uint64_t id = uint64_t(stream->id());
    if (localInitiated_(id, m_isServer)) return true;
    if (stream->streamCreditReturned()) return true;
    Zi::StreamType::T type = StreamID::uni(id);
    return m_localLimit[type].limit() >= MaxStreamCount;
  }
  StreamRef newStream_(int64_t id) {
    auto node = new typename Streams::Node{impl(), id};
    StreamRef stream{node};
    stream->txCredit(initialStreamTxCredit_(uint64_t(id)));
    stream->rxCredit(initialStreamRxCredit_(uint64_t(id)));
    m_streams->addNode(node);
    ZquicLOG(app()->qlogTrace(), ([
      streamID = uint64_t(id),
      streamType = StreamType::T(StreamID::uni(uint64_t(id))),
      streamSide = localInitiated_(uint64_t(id), m_isServer) ?
		StreamSide::T(StreamSide::Sending) :
		StreamSide::T(StreamSide::Receiving),
      reason = localInitiated_(uint64_t(id), m_isServer) ?
		StreamReason::T(StreamReason::LocalOpen) :
		StreamReason::T(StreamReason::PeerOpen),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      StreamEvent event{
		.streamType = streamType,
	.oldState = StreamState::Idle,
		.newState = StreamState::Open,
		.streamSide = streamSide,
		.reason = reason,
		.streamID = streamID,
		.linkInfo = linkInfo
      };
      o.logStreamStateUpd(event, time);
    }));
    impl()->streamOpen(
      stream, StreamID::server(uint64_t(id)) == m_isServer);
    return stream;
  }

  uint64_t initialStreamTxCredit_(uint64_t id) const {
    if (!m_crypto.peerTransportParamsReceived()) return 0;
    const auto &params = m_crypto.peerTransportParams();
    if (StreamID::uni(id)) return params.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_isServer;
    return local ? params.initialMaxStreamDataBidiRemote :
      params.initialMaxStreamDataBidiLocal;
  }

  uint64_t initialStreamRxCredit_(uint64_t id) const {
    if (StreamID::uni(id) && StreamID::server(id) == m_isServer) return 0;
    if (StreamID::uni(id)) return m_transportParams.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_isServer;
    return local ? m_transportParams.initialMaxStreamDataBidiLocal :
      m_transportParams.initialMaxStreamDataBidiRemote;
  }

  int64_t nextStreamID_(Zi::StreamType::T type) {
    uint64_t &ordinal =
      type == Zi::StreamType::Simplex ? m_nextUniOrdinal : m_nextBidiOrdinal;
    uint64_t id = (ordinal++ << 2) |
      (m_isServer ? 1U : 0U) |
      (type == Zi::StreamType::Simplex ? 2U : 0U);
    ZiAssert(id <= uint64_t(INT64_MAX), "Zquic", (id),
      "stream ID overflow id=" << id, return INT64_MAX);
    return int64_t(id);
  }

  bool rxInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zquic", (),
      "QUIC link Rx access before app initialization", return false);
    return m_app->rxInvoked();
  }
  bool txInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zquic", (),
      "QUIC link Tx access before app initialization", return false);
    return m_app->txInvoked();
  }

  void scheduleCxnTimer_(
    const char *name, CxnTimer::T action,
    ZuTime out, int mode, ZmScheduler::Timer *timer) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC timer schedule before app initialization", return);
    if (disconnecting_()) return;
    if (debugLog_()) ZiLOG(Debug, "Zquic", ([name](auto &s) {
      s << "QUIC timer armed name=" << name;
    }));
    app()->mx()->add(timer, out, mode, [link = impl(), action](auto &&arm) {
      return arm([link, action]() mutable {
	if (link->disconnecting_()) return;
	link->cxnTimer_(action);
      });
    }, app()->txThread());
  }

  void cancelTimer_(const char *, ZmScheduler::Timer *timer) {
    app()->mx()->cancel(timer);
  }

  void cxnTimer_(CxnTimer::T action) {
    switch (action) {
      case CxnTimer::AckDelay:
	ackDelay_();
	break;
      case CxnTimer::Loss:
	m_lossTimerOut = {};
	lossTime_();
	break;
      case CxnTimer::PTO:
	m_ptoTimerOut = {};
	impl()->pto_();
	break;
      case CxnTimer::Idle:
	m_idleTimerOut = {};
	idleTimeout_();
	break;
      case CxnTimer::Close:
	closeTimeout_();
	break;
      case CxnTimer::KeyDiscard:
	keyDiscard_();
	break;
      case CxnTimer::PMTUD:
	pmtudTimeout_();
	break;
      case CxnTimer::PathValid:
	pathTimeout_();
	break;
    }
  }

  bool ptoLevel_(PktNumSpace::T &level) const {
    bool have = false;
    ZuTime out;
    for (unsigned i = 0; i < 3; ++i) {
      auto l = PktNumSpace::T(i);
      if (m_txSpaceDiscarded[l]) continue;
      const auto &tx = m_txPkts[l];
      if (!tx.bytesInFlight() && !tx.retransmitPending()) continue;
      ZuTime deadline = ptoDeadline_(l);
      if (!have || deadline < out) {
	level = l;
	out = deadline;
	have = true;
      }
    }
    return have;
  }

  ZuTime ptoDeadline_(PktNumSpace::T level) const {
    ZuTime t = m_txPkts[level].latestAckSentTime();
    if (!*t) t = Zm::now();
    ZuTime delay = level == PktNumSpace::AppData ? maxAckDelay_() : ZuTime{0};
    return t + m_ptoBackoff.timeout(m_rtt, delay);
  }

  void resetLinkState_() {
    m_linkState = LinkState::Starting;
    m_drainPTOs = 0;
    m_suspiciousStreamFrames = 0;
    m_suspiciousStreamClosed = false;
  }

  bool startHandshakeState_() {
    if (m_linkState != LinkState::Starting) return false;
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkState,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvent event{
	.oldState = oldState,
	.newState = LinkState::Handshaking,
	.linkInfo = linkInfo
      };
      o.logCxnStateUpd(event, time);
    }));
    m_linkState = LinkState::Handshaking;
    return true;
  }

  bool establishState_() {
    if (m_linkState != LinkState::Handshaking) return false;
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkState,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvent event{
	.oldState = oldState,
	.newState = LinkState::Established,
	.linkInfo = linkInfo
      };
      o.logCxnStateUpd(event, time);
    }));
    m_linkState = LinkState::Established;
    return true;
  }

  bool closeLinkState_() {
    if (m_linkState == LinkState::Closed) return false;
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkState,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvent event{
	.oldState = oldState,
	.newState = LinkState::Closing,
	.linkInfo = linkInfo
      };
      o.logCxnStateUpd(event, time);
    }));
    m_linkState = LinkState::Closing;
    m_drainPTOs = 0;
    return true;
  }
  bool drainLinkState_() {
    if (m_linkState == LinkState::Closed) return false;
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkState,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvent event{
	.oldState = oldState,
	.newState = LinkState::Draining,
	.linkInfo = linkInfo
      };
      o.logCxnStateUpd(event, time);
    }));
    m_linkState = LinkState::Draining;
    m_drainPTOs = 0;
    return true;
  }
  void closedLinkState_() {
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkState,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvent event{
	.oldState = oldState,
	.newState = LinkState::Closed,
	.linkInfo = linkInfo
      };
      o.logCxnStateUpd(event, time);
    }));
    m_linkState = LinkState::Closed;
    m_drainPTOs = 0;
  }

  // immutable
  App			*m_app = nullptr;
  bool			m_isServer = false;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;

  // Rx thread exclusive
  AppClose		m_appClose;
  FlowCredit		m_rxDataCredit;
  StreamLimit		m_peerLimit[2] = {
    StreamLimit(MaxStreamCount),
    StreamLimit(MaxStreamCount)
  };
  StreamsRef		m_streams;
  uint64_t		m_closedStreamBase[4] = {};
  ClosedStreamsRef	m_closedStreams;

  RuntimeRxDiag		m_rxDiag;
  Crypto		m_crypto;
  TransportParams	m_transportParams;
  CryptoStream		m_rxCrypto[3];
  ZiSockAddr		m_rxCryptoAddr[3];
  CxnID			m_initialDCID;
  CxnID			m_origDCID;
  CxnID			m_groupID;
  CxnID			m_localSCID;
  CxnID			m_peerCID;
  ResetToken		m_peerResetToken;
  LocalCIDs		m_localCIDs;
  PeerCIDs		m_peerCIDs;
  uint64_t		m_rxLargestPN[3]{};
  AckManager		m_rxAcks;
  TrafficSecret		m_rxOldTrafficSecret;
  PktProtState		m_rxOldProt;
  TrafficSecret		m_rxNextTrafficSecret;
  PktProtState		m_rxNextProt;
  ZuTime		m_rxOldKeyDiscard;
  RttEstimator		m_rtt;
  PTOBackoff		m_ptoBackoff;
  AckPost		m_ackPost[3];
  // Connection-owned timers; callbacks run on Tx.
  ZmScheduler::Timer	m_ackDelayTimer;
  ZmScheduler::Timer	m_lossTimer;
  ZmScheduler::Timer	m_ptoTimer;
  ZmScheduler::Timer	m_idleTimer;
  ZmScheduler::Timer	m_closeTimer;
  ZmScheduler::Timer	m_keyDiscardTimer;
  // Active-path timers owned by this Link; callbacks run on Tx.
  ZmScheduler::Timer	m_pmtudTimer;
  ZmScheduler::Timer	m_pathTimer;
  ZuTime		m_lossTimerOut;
  ZuTime		m_ptoTimerOut;
  ZuTime		m_idleTimeout;
  ZuTime		m_idleTimerOut;
  ZuTime		m_idleBase;
  ZuTime		m_closeTimerOut;
  ZuTime		m_closeNextResponse;
  PktNumSpace::T	m_ptoTimerLevel = PktNumSpace::Initial;
  bool			m_idleAckElicitingSent = false;
  bool			m_rxSpaceDiscarded[3]{};
  LinkState::T		m_linkState = LinkState::Starting;
  unsigned		m_drainPTOs = 0;
  uint64_t		m_peerRetirePriorTo = 0;
  unsigned		m_suspiciousStreamFrames = 0;
  bool			m_suspiciousStreamClosed = false;
  bool			m_rxKeyPhase = false;
  bool			m_rxOldKeyPhase = false;

  // Tx thread exclusive
  FlowCredit		m_txDataCredit;
  uint64_t		m_nextBidiOrdinal = 0;
  uint64_t		m_nextUniOrdinal = 0;
  StreamLimit		m_localLimit[2] = {
    StreamLimit(MaxStreamCount),
    StreamLimit(MaxStreamCount)
  };
  uint64_t		m_queued[2] = {};
  bool			m_openQueuedPending[2] = {};
  uint64_t		m_lastDataBlocked = U64Null;
  uint64_t		m_lastStreamsBlocked[2] = {U64Null, U64Null};
  StreamQueue		m_streamQueue;
  PendingControl	m_maxDataControl;
  PendingControl	m_maxStreamsControl[2];
  PendingControl	m_dataBlockedControl;
  PendingControl	m_streamsBlockedControl[2];
  PendingControl	m_handshakeDoneControl;
  PendingControl	m_pathChallengeControl;
  PathResponses		m_pathResponses{ZmQueueParams{}.initial(PathResponseMax)};

  CryptoStream		m_txCrypto[3];
  CryptoTxPQueue	m_txCryptoUnackd[3];
  PktProtState		m_txProt[3];
  ZmRef<ZiIOBuf>	m_coalesceInitial;
  ZiSockAddr		m_coalesceAddr;
  RuntimeTxDiag		m_txDiag;
  NewReno		m_congestion;
  uint64_t		m_txRuntimeGen = 0;
  uint64_t		m_txPN[3]{};
  uint64_t		m_txLargestAckd[3]{U64Null, U64Null, U64Null};
  PktTxSpace		m_txPkts[3];
  AckSnapshot		m_txAck[3];
  AckECN		m_peerAckECN[3];
  Path			m_path;
  PathState		m_validatingPath;
  bool			m_txSpaceDiscarded[3]{};
  bool			m_coalesceLong = false;
  bool			m_txKeyPhase = false;
};

} // namespace Zquic

#endif /* Zquic_Link_HH */
