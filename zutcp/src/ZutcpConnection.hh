//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Connection API implementation

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

namespace Zutcp {

ZtEnumStruct(ZutcpAPI, CxnTimer, int8_t,
  Connect,
  User,
  CloseDrain,
  FinWait,
  AckDelay,
  Loss,
  PTO,
  RTO,
  Persist,
  Idle,
  HeartBeat,
  Close,
  PMTUD);

ZtEnumStruct(ZutcpAPI, TcpCloseState, int8_t,
  Open, CloseWait, FinWait1, FinWait2, Closing, LastAck, TimeWait, Closed);

struct LinkCloseState {
  uint64_t		error = 0;
  bool			closed = false;
  bool			application = false;
};
struct LinkStateSnapshot {
  uint64_t		error = 0;
  LinkState::T		state = LinkState::Closed;
  bool			closed = false;
  bool			application = false;
};

// Fixed sender configuration published by the Rx parameter owner.
struct PeerTxParams {
  uint64_t	initialMaxData = 0, maxAckDelay = 0, ackDelayExponent = 0;
  bool		set = false;
};

namespace TxQueueState {
  inline constexpr uint64_t Closed = uint64_t(1)<<63;
  inline constexpr uint64_t CountMask = Closed - 1;
}

struct TCPOrigins {
  uint32_t local = 0, peer = 0;
  bool localKnown = false, peerKnown = false, server = false;
};

template <typename App, typename Impl, typename TxBufAlloc_>
class Connection :
  public Zutcp_::ServerLinks::Node,
  public Ztc::Link,
  public ZmPQRx<Connection<App, Impl, TxBufAlloc_>,
    DataRxPQueue, ZmPQRxGapIgnore<>> {
template <typename, typename>
friend class Server;

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Connection *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    TelKey telKey() const override {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      if (m_type == Ztc::QueueType::Rx)
	m_link->rxQueueTelemetry_(data);
      else
	m_link->txQueueTelemetry_(data);
    }

  private:
    const Connection			*m_link;
    const Ztc::QueueType::T	m_type;
  };

public:
  using Zutcp_::EndpointLink::mxCxn;

  using Self = Connection<App, Impl, TxBufAlloc_>;
  using TxBufAlloc = TxBufAlloc_;
  static constexpr unsigned TxFlushBatch = 64;
  static constexpr unsigned RecoveryScanBatch = 256;
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }
  unsigned txQueueMax() const { return m_txQueueMax; }
  unsigned dataMPS() const { return unsigned(m_writeMPS); }

  Connection(App *app, bool isServer = false) :
    Connection{app, isServer, ZuID{} << "quic:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  Connection(App *app, bool isServer, ZuID id) :
    m_app{app}, m_id{ZuMv(id)}, m_isServer{isServer} {
    for (auto &pn : m_txLargestAckd) pn = U64Null;
    m_txPkts[PktNumSpace::AppData].byteRecovery(true);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto space = PktNumSpace::T(i);
      AckTracker *tracker = &m_rxAcks.tracker(space);
      tracker->dequeueFn([this, tracker]() {
	this->app()->rxRun([tracker]() {
	  tracker->dequeueRx();
	});
      });
    }
    app->linkAdded_(this);
    initLinkState(LinkState::Starting);
  }
  ~Connection() {
    ZmAssert_(!m_cxnSlot);
    clearCallbacks();
    assert(!timersActive());
    app()->linkDeleted_(this, telState(publicLinkState()));
  }



  void clearCallbacks() {
    for (unsigned i = 0; i < PktNumSpace::N; ++i)
      m_rxAcks.tracker(PktNumSpace::T(i)).dequeueFn({});
  }

  App *app() const { return m_app; }
  TelKey telKey() const override {
    return {app()->telKey().template p<1>(), m_id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = app()->telKey().template p<1>();
    data.id = m_id;
    data.rxCalls = m_rxDiag.datagramsRx.load_();
    data.txCalls = m_txDiag.packetsTx.load_();
    data.rxBytes = m_rxDiag.bytesRx.load_();
    data.txBytes = m_txDiag.bytesTx.load_();
    data.reconnects = 0;
    data.type = Ztc::LinkType::QUIC;
    data.state = telState(publicLinkState());
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    fn(&rxQueue);
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&txQueue);
    return 2;
  }
  void up() override { impl()->up_(); }
  void down() override { (void)impl()->disconnect(); }

  bool isServer() const { return m_isServer; }
  bool closed() const {
    switch (publicLinkState()) {
      case LinkState::Closing:
      case LinkState::Draining:
      case LinkState::Closed:
	return true;
      default:
	return false;
    }
  }
  bool established() const {
    return publicLinkState() == LinkState::Established;
  }
  uint64_t closeError() const { return m_publicCloseError.load_(); }
  const TransportParams &peerParams() const {
    ZmAssert_(rxInvoked_());
    return m_peerParams;
  }
  bool peerParamsSet() const {
    ZmAssert_(rxInvoked_());
    return m_peerParamsSet;
  }
  unsigned queuedControlFrames() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC control queue inspection outside Tx thread", return 0);
    unsigned n = 0;
    if (m_maxDataControl.queued) ++n;
    if (m_dataBlockedControl.queued) ++n;
    n += byteControlCount();
    return n;
  }
  uint64_t rxDataCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_rxDataCredit.available(); }
  uint64_t rxDataRetired() const { return m_rxRetired; }

  bool applyMaxData(const Frame &frame) {
    if (!validateMaxData(frame)) return false;
    txApplyMaxData_(frame.value);
    return true;
  }
  bool receiveDataBlocked(const Frame &frame) {
    if (!validateDataBlocked_(frame)) return false;
    // A probe must receive the current edge even when no application bytes
    // retire. Re-advertise after a lost opening update without granting credit.
    postFlowUpdate(FlowUpdate{FrameType::MaxData, m_rxDataCredit.limit()});
    return true;
  }
  bool frameLegal(
    PktNumSpace::T level, const Frame &frame) const {
    return packetFrameLegal_(level, frame);
  }

public:
  int receiveFrame(const Frame &frame, BufDiag *diag = nullptr) {
    if (frame.type == FrameType::Reset) {
      if (resetReceived()) return finalSize() == frame.length ? 0 : -1;
      if (frame.length < rxAdmitted() || frame.length > rxCreditLimit() ||
          !receiveReset(frame)) return -1;
      if (!consumeRxCreditTo(frame.length)) return -1;
      closeRx();
      if (!retireRx(rxRetirable())) return -1;
      impl()->rxReset(frame.errorCode, frame.length);
      return 0;
    }
    if (frame.type == FrameType::StopSending) {
      if (stopReceived()) return 0;
      return receiveStop(frame) ? 0 : -1;
    }
    return -1;
  }
  int receiveFrame(const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *immediateAck = nullptr, bool *admitted = nullptr) {
    if (frame.type != FrameType::Data) return receiveFrame(frame, diag);
    bool rxClosed = rxComplete() || resetReceived();
    bool refused = false;
    int result = processFrame(frame, ZuMv(packet), diag, &refused);
    if (admitted) *admitted = !refused;
    if (refused) return 0; // resource pressure is not a peer protocol error
    if (result >= 0) {
      if (!result) ++m_rxDiag.dataZeroResultRx;
      if ((rxClosed || !result) && immediateAck) *immediateAck = true;
    } else noteInvalidData(TransportError::FinalSize, false, true);
    return result;
  }
  void rxReset(uint64_t, uint64_t) { }
  void txReset(uint64_t, uint64_t) { }
  void txStopped(uint64_t) { }
  void rxDequeued() { if (rxQueued()) processRx_(); }

protected:
  void dataWritable() {
    m_txReady = true;
    updatePersist_();
    updateUserTimeout_();
  }
  bool takeTxReady() {
    bool ready = m_txReady;
    m_txReady = false;
    return ready;
  }
  void scheduleData() {
    app()->txRun([link = impl(), gen = dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_()) return;
      link->dataWritable();
      link->flushTx_();
    });
  }
  void clearWritableData() { m_txReady = false; }


protected:
public:

  bool localReset(
    uint64_t appError, uint64_t finalSize) {
    return queueReset_(appError, finalSize);
  }
  bool localStopSending(uint64_t appError) {
    return queueStopSending_(appError);
  }
  void transportClose_(FrameType::T type, uint64_t errorCode) {
    impl()->transportClose(type, errorCode);
  }


protected:
  void up_() { }

  void closeState(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC close initiation outside Rx thread", return);
    closingCxnRx_();
    m_rxAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_publicCloseError = errorCode;
    closeLinkStateRx();
    publishLinkSnapshotTx();
    cancelTimers();
  }
  void closeStateTx(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx close initiation outside Tx thread", return);
    m_txAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_txLinkState = LinkState::Closing;
    LinkStateSnapshot snapshot = linkSnapshotTx();
    cancelTimers_();
    app()->rxRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting_()) return;
      link->applyCloseSnapshotRx(snapshot);
    });
  }
  void applyCloseSnapshotRx(LinkStateSnapshot snapshot) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC close snapshot apply outside Rx thread", return);
    if (snapshot.state < m_rxLinkState ||
	(snapshot.state == m_rxLinkState && m_rxAppClose.closed))
      return;
    closeDataRx();
    m_rxAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_publicCloseError = snapshot.error;
    switch (snapshot.state) {
      case LinkState::Draining:
	drainLinkStateRx();
	break;
      case LinkState::Closed:
	closedLinkStateRx();
	break;
      default:
	closeLinkStateRx();
	break;
    }
  }
  bool writeCloseFrame(PktBuild &build, bool appClose) const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC close frame construction outside Tx thread", return false);
    int n = appClose ?
      FrameCodec::writeApplicationClose(
	build.scratch(), build.scratchAvail(), m_txAppClose.error) :
      FrameCodec::writeConnectionClose(
	build.scratch(), build.scratchAvail(), m_txAppClose.error);
    return n > 0 && build.commitScratch(unsigned(n));
  }
  bool appCloseOnDisconnect() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC close kind access outside Tx thread", return false);
    return m_txAppClose.application || app()->stopping();
  }
  bool sendCloseFrame(ZiSockAddr, bool = false) { return false; }

public:
  bool disconnect(bool peer = false) {
    if (m_disconnecting.xch(1)) return false;
    disconnect_(peer, []() { });
    return true;
  }
  template <typename L>
  void disconnect_(bool peer, L &&l) {
    if (app()->nativeTx()) closeTxAdmission();
    m_disconnecting = 1;
    app()->txRun([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->delTimers();
      link->Self::disconnected(peer, ZuMv(l));
    });
  }
  void disconnecting_(bool disconnecting) {
    m_disconnecting = unsigned(disconnecting);
  }
  bool disconnecting_() const { return m_disconnecting.load_(); }
  void closeForInvalidData(TransportError::T error) {
    ++m_rxDiag.suspectDataCloses;
    m_suspiciousDataClosed = true;
    if (!m_rxAppClose.closed) {
      if (app()->nativeTx())
	abortLocalRx_(error, [link = impl()] { link->tcpResetAbort_(false); });
      else closeState(error);
    }
  }
  void noteInvalidData(
    TransportError::T error, bool closedData = false, bool immediate = false) {
    ++m_rxDiag.invalidDataFrames;
    if (closedData) ++m_rxDiag.closedDataFrames;
    if (m_suspiciousDataClosed) return;
    if (immediate || ++m_suspiciousDataFrames >= SuspiciousDataThreshold)
      closeForInvalidData(error);
  }
  Zutcp::LinkInfo linkInfo() const { return linkInfo_(); }

protected:
  struct CIDSel { enum T { Initial, Local, Peer }; };
  enum { PNLength = 2 };
  static constexpr unsigned SuspiciousDataThreshold = 8;

  struct AckSnapshot {
    AckSnapshot() = default;
    // ZtBuiltin's inline buffer is object-local; copy into the already
    // constructed destination so its backing pointer cannot alias the source.
    AckSnapshot(const AckSnapshot &a) { *this = a; }
    AckSnapshot &operator =(const AckSnapshot &a) {
      level = a.level;
      gen = a.gen;
      delay = a.delay;
      byteNext = a.byteNext;
      rxLimit = a.rxLimit;
      windowSerial = a.windowSerial;
      largestRxTime = a.largestRxTime;
      ecn = a.ecn;
      due = a.due;
      ranges = a.ranges;
      return *this;
    }
    AckSnapshot(AckSnapshot &&) = default;
    AckSnapshot &operator =(AckSnapshot &&) = default;

    PktNumSpace::T	level = PktNumSpace::Initial;
    uint64_t		gen = 0;
    uint64_t		delay = 0;
    uint64_t		byteNext = 0;
    uint64_t		rxLimit = 0, windowSerial = 0;
    uint64_t		largestRxTime = 0;
    AckECN		ecn;
    bool		due = false;
    Frame::AckRanges	ranges;
  };
  struct RxAckMeta {
    TCPHeader tcp;
    bool		ackEliciting = false;
    bool		immediateAck = false;
  };
  struct TxAckWork {
    AckSnapshot		ack;
    uint64_t		gen = 0;
    AckGrowth		growth;
    PktAckBatch		ackBatch;
    PktAckBatch		byteBatch;
    ZuTime		ackTime, rtoDeadline;
    uint64_t		previousByteAck = 0;
    bool		bytePhase = false;
    bool		byteProgress = false;
    PktLossBatch	lossBatch;
    bool		ecnValidated = false;
    bool		ecnValid = true;
    bool		lossPhase = false;
    bool		congestionOpened = false;
    bool		retransmit = false;
    bool		initialResponseAckd = false;
    bool		windowUpdate = false;
    uint64_t		ceDelta = 0;
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

  static SentFrameRef qlogRef(const SentFrameRef &ref) {
    return SentFrameRef{.kind = ref.kind, .controlType = ref.controlType,
      .offset = ref.offset, .length = ref.length, .value = ref.value,
      .fin = ref.fin, .range = {}};
  }
  struct TxPktRefs {
    bool add(const SentFrameRef &ref) {
      if (ref.kind == SentFrameKind::None ||
	  refs.length() >= SentPkt::MaxFrames)
	return false;
      refs.push(ref);
      return true;
    }
    unsigned count() const { return refs.length(); }
    const SentFrameRef &operator [](unsigned i) const { return refs[i]; }

    TxPktRefs summary() const {
      TxPktRefs result;
      for (unsigned i = 0, n = count(); i < n; ++i)
	result.refs.push(qlogRef(refs[i]));
      return result;
    }
    ZuArray<SentFrameRef, SentPkt::MaxFrames> refs;
  };
  static bool txControlPacket(ZuBSpan frame, const TxPktRefs *refs) {
    if (refs) {
      for (unsigned i = 0, n = refs->count(); i < n; ++i)
	if ((*refs)[i].kind == SentFrameKind::Data && (*refs)[i].length) return false;
    }
    // Ordinary packetization supplies refs. The single encoded-frame entry
    // also distinguishes FIN/zero-length DATA from application payload.
    if (!frame.length() || (frame[0] & 0xf8) != 0x08) return true;
    Frame parsed;
    unsigned used = 0;
    return !FrameCodec::parse(frame, parsed, used) && !parsed.length;
  }
  bool setTCPOriginsRx_(TCPOrigins origins) {
    ZiAssert(rxInvoked_(), "Zutcp", (), "TCP origin selection outside Rx", return false);
    if ((m_rxOrigins.localKnown || m_rxOrigins.peerKnown) &&
	m_rxOrigins.server != origins.server) return false;
    if ((m_rxOrigins.localKnown && origins.localKnown && m_rxOrigins.local != origins.local) ||
	(m_rxOrigins.peerKnown && origins.peerKnown && m_rxOrigins.peer != origins.peer))
      return false;
    if (m_rxOrigins.localKnown) {
      origins.local = m_rxOrigins.local;
      origins.localKnown = true;
    }
    if (m_rxOrigins.peerKnown) {
      origins.peer = m_rxOrigins.peer;
      origins.peerKnown = true;
    }
    m_rxOrigins = origins;
    app()->txRun([link = impl(), origins, gen = dataGen()] {
      link->installTCPOriginsTx_(origins, gen);
    });
    return true;
  }
  void installTCPOriginsTx_(TCPOrigins origins, uint64_t gen) {
    ZiAssert(txInvoked_(), "Zutcp", (), "TCP origin installation outside Tx", return);
    if (gen != dataGen() || gen != m_txDataGen || disconnecting_()) return;
    if ((m_txOrigins.localKnown || m_txOrigins.peerKnown) &&
	m_txOrigins.server != origins.server) return;
    if ((m_txOrigins.localKnown && (!origins.localKnown || m_txOrigins.local != origins.local)) ||
	(m_txOrigins.peerKnown && (!origins.peerKnown || m_txOrigins.peer != origins.peer)))
      return;
    m_txOrigins = origins;
  }
  bool initTCPOriginsRx_(bool server, const ZiSockAddr &local, const ZiSockAddr &remote) {
    if (!app()->nativeTx()) return true;
    TCPOrigins origins = m_rxOrigins;
    origins.server = server;
    if (!origins.localKnown) {
      auto stack = app()->nativeStack();
      if (!stack || !stack->chooseISN(local, remote, origins.local)) return false;
      origins.localKnown = true;
    }
    return setTCPOriginsRx_(origins);
  }
  bool initialTCPHeader_(TCPHeader &header, bool params) const {
    ZiAssert(txInvoked_(), "Zutcp", (), "TCP Initial header outside Tx", return false);
    header = tcpHeader();
    if (!app()->nativeTx()) return true;
    if (!m_txOrigins.localKnown || (m_txOrigins.server && !m_txOrigins.peerKnown)) return false;
    if (params) {
      header.seq = m_txOrigins.local;
      header.flags = m_txOrigins.server ? 0x12 : 0x02;
      header.ack = m_txOrigins.server ? uint32_t(m_txOrigins.peer + 1) : 0;
    } else {
      if (!m_txOrigins.peerKnown || !m_tcpSynSent) return false;
      header.seq = uint32_t(m_txOrigins.local + 1);
      header.ack = uint32_t(m_txOrigins.peer + 1);
      header.flags = 0x10;
    }
    return true;
  }
  bool qualifyTCPInitialRx_(
    const InitialPkt &initial, const Datagram &d, TCPHeader &header, TCPOrigins &origins) const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "TCP Initial qualification outside Rx", return false);
    header = d.tcp;
    origins = m_rxOrigins;
    if (!app()->nativeTx()) return true;
    const auto &h = initial.header;
    if (h.type != PktType::Initial || !VersionNeg::supported(h.version)) return false;
    unsigned flags = d.tcp.flags & 0x17;
    if (initial.params) {
      if (origins.server) {
	if (!handshakeStartedRx() && (h.dcid.length() < CxnIDGen::InitialLength ||
	    h.scid.length() > CxnIDMax || !d.buf || d.buf->length < MinPktPayload)) return false;
	if (flags != 0x02 ||
	    (handshakeStartedRx() && (h.scid != m_peerCID ||
	      (h.dcid != m_initialDCID && h.dcid != m_localSCID)))) return false;
      } else {
	if (!origins.localKnown || flags != 0x12 || h.dcid != m_localSCID ||
	    d.tcp.ack != uint32_t(origins.local + 1) ||
	    (origins.peerKnown && h.scid != m_peerCID)) return false;
      }
      if (origins.peerKnown && origins.peer != d.tcp.seq) return false;
      origins.peer = d.tcp.seq;
      origins.peerKnown = true;
      if (!origins.localKnown) {
	auto stack = app()->nativeStack();
	if (!origins.server || !stack ||
	    !stack->chooseISN(d.local, d.addr, origins.local)) return false;
	origins.localKnown = true;
      }
      // SYN occupies the position before relative application byte zero.
      header.seq = 0;
      header.ack = 0;
      header.flags &= ~0x02;
    } else {
      if (!origins.localKnown || !origins.peerKnown || flags != 0x10 ||
	  d.tcp.seq != uint32_t(origins.peer + 1) ||
	  d.tcp.ack != uint32_t(origins.local + 1) || h.scid != m_peerCID ||
	  (h.dcid != m_initialDCID && h.dcid != m_localSCID)) return false;
      header.seq = header.ack = 0;
    }
    return true;
  }
  bool normalizeTCPRx_(const TCPHeader &wire, TCPHeader &header) const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "TCP normalization outside Rx", return false);
    header = wire;
    if (!app()->nativeTx()) return true;
    if (!m_rxOrigins.localKnown || !m_rxOrigins.peerKnown || (wire.flags & 0x02)) return false;
    header.seq -= uint32_t(m_rxOrigins.peer + 1);
    if (wire.flags & 0x10) header.ack -= uint32_t(m_rxOrigins.local + 1);
    return true;
  }
  TCPHeader tcpHeader(const TxPktRefs *refs = nullptr) const {
    uint64_t window = m_txRxLimit > m_txRxNext ? m_txRxLimit - m_txRxNext : 0;
    TCPHeader header{.seq = uint32_t(m_txNext), .ack = uint32_t(m_txRxNext),
      .window = uint16_t(window > 65535 ? 65535 : window), .flags = 0x10};
    if (refs)
      for (unsigned i = 0, n = refs->count(); i < n; ++i) {
	const auto &ref = (*refs)[i];
	if (ref.kind != SentFrameKind::Data) continue;
	header.seq = uint32_t(ref.offset);
	if (ref.length) header.flags |= 0x08;
	if (ref.fin) header.flags |= 0x01;
	break;
      }
    if (app()->nativeTx()) {
      if (m_txOrigins.localKnown) header.seq += uint32_t(m_txOrigins.local + 1);
      if (m_txOrigins.peerKnown) header.ack += uint32_t(m_txOrigins.peer + 1);
      else { header.ack = 0; header.flags &= ~0x10; }
    }
    return header;
  }
  void localWindow_(uint64_t serial, uint64_t next, uint64_t limit) {
    if (serial <= m_txRxSerial) return;
    m_txRxSerial = serial;
    m_windowDirty |= next != m_txRxNext || limit != m_txRxLimit;
    m_txRxNext = next;
    m_txRxLimit = limit;
  }
  void receiveTCP(const TCPHeader &header) {
    if (!app()->nativeTx() || !(header.flags & 0x10)) return;
    app()->txRun([link = impl(), header, gen = dataGen()] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting_()) return;
      link->applyTCP_(header);
    });
  }
  void applyTCP_(const TCPHeader &header) {
    uint64_t next = tcpPosition(header.ack, m_txAckd);
    if (next > m_txNext) return;
    bool current = next >= m_txAckd && (!m_txWindowSet ||
      int32_t(header.seq - m_txWindowSeq) > 0 ||
      (header.seq == m_txWindowSeq && int32_t(header.ack - m_txWindowAck) >= 0));
    bool changed = false;
    if (current) {
      uint64_t limit = next + header.window;
      changed = limit != m_txDataCredit.limit() || m_peerWindowZero != !header.window;
      bool reopened = m_peerWindowZero && header.window;
      m_txWindowSet = true;
      m_txWindowSeq = header.seq;
      m_txWindowAck = header.ack;
      m_peerWindowZero = !header.window;
      m_txDataCredit.advertise(limit);
      if (changed) dataWritable();
      if (reopened && m_txAckd < m_txNext)
	m_dataRto.sent(now_(), m_rtt, app()->rtoParams(), true);
      scheduleDataRto_();
    }
    if (establishedTx() && (next > m_txAckd || changed)) {
      TxAckWork work;
      work.ack.level = PktNumSpace::AppData;
      work.ack.byteNext = next;
      work.gen = m_txRuntimeGen;
      work.windowUpdate = changed;
      processAckFrameTx_(ZuMv(work));
    } else if (changed) {
      impl()->flowCredit();
      impl()->flushTx_();
    }
  }

  struct QLogTxFrameRefs {
    QLogTxFrameRefs() = default;
    QLogTxFrameRefs(const SentFrameUpdates &updates) {
      unsigned n = updates.length() < PktTxUpdate::MaxFrames ?
	updates.length() : PktTxUpdate::MaxFrames;
      for (unsigned i = 0; i < n; ++i) refs.push(qlogRef(updates[i].ref));
    }
    unsigned count() const { return refs.length(); }
    const SentFrameRef &operator [](unsigned i) const { return refs[i]; }

    ZuArray<SentFrameRef, PktTxUpdate::MaxFrames> refs;
  };
  using SentControls = ZuArray<ControlFrame, SentPkt::MaxFrames>;
  struct PendingControl {
    ControlFrame	frame;
    bool		queued = false;
  };


  bool txReady() const { return m_txReady; }

  bool queueControl(const ControlFrame &frame) {
    if (!frame || disconnecting_()) return false;
    app()->txInvoke(impl(), [link = impl(), frame]() mutable {
      if (link->disconnecting_()) return link;
      link->txQueueControl_(frame);
      return link;
    });
    return true;
  }
  bool txQueueControl_(const ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC control queue mutation outside Tx thread", return false);
    if (!frame) return false;
    switch (frame.type) {
      case FrameType::MaxData:
	return !app()->nativeTx() && queuePendingControl(m_maxDataControl, frame);
      case FrameType::DataBlocked:
	return !app()->nativeTx() && queuePendingControl(m_dataBlockedControl, frame);
      case FrameType::Reset:
      case FrameType::StopSending: {
	bool queued = false;
	switch (frame.type) {
	  case FrameType::Reset:
	    queued = queueReset(frame.errorCode, frame.value);
	    break;
	  case FrameType::StopSending:
	    queued = queueStopSending(frame.errorCode);
	    break;
	  default:
	    break;
	}
	if (queued) dataWritable();
	return queued;
      }
      default:
	return false;
    }
  }
  bool queueFlowUpdate_(const FlowUpdate &update) {
    return update.needed() && queueControl(ControlFrame::flowUpdate(update));
  }
  void postFlowUpdate(FlowUpdate update) {
    if (app()->nativeTx()) {
      auto next = rxByteNext();
      auto limit = update.maximum;
      auto serial = ++m_rxWindowSerial;
      app()->txRun([link = impl(), next, limit, serial, gen = dataGen()] {
	if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting_()) return;
	link->localWindow_(serial, next, limit);
	link->flushTx_();
      });
      return;
    }
    app()->txRun([
      link = ZmRef<Impl>{impl()}, update = ZuMv(update), gen = dataGen()
    ]() mutable {
      if (link->disconnecting_() || gen != link->dataGen() || gen != link->m_txDataGen)
	return;
      if (link->queueFlowUpdate_(update)) link->queueTxFlush();
    });
  }
  bool queueBlocked(
    uint64_t value) {
    ControlFrame frame = ControlFrame::blocked(value);
    if (!blockedFrameNeeded(frame)) return false;
    if (!queueControl(frame)) return false;
    noteBlockedQueued(frame);
    noteFlowBlocked();
    return true;
  }



  void noteFlowBlocked() {
    ZutcpLOG(app()->qlogTrace(), ([
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      switch (FrameType::DataBlocked) {
	case FrameType::DataBlocked:
	  o.logCxnDataBlockedUpd(
	    BlockedEvt{
	      .linkInfo = linkInfo
	    ,
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::CxnFlowCtrl},
	    time);
	  break;
	default:
	  break;
      }
    }));
  }

  bool queueReset_(
    uint64_t appError, uint64_t finalSize) {
    return queueControl(ControlFrame::resetData( appError, finalSize));
  }
  bool queueStopSending_(uint64_t appError) {
    return queueControl(ControlFrame::stopSending( appError));
  }
  bool rxApplyMaxData(const Frame &frame) {
    if (!validateMaxData(frame)) return false;
    uint64_t value = frame.value;
    app()->txRun([link = impl(), value, gen = dataGen()]() mutable {
      if (link->disconnecting_() || gen != link->dataGen() || gen != link->m_txDataGen)
	return;
      link->txApplyMaxData_(value);
      link->flushTx_();
    });
    return true;
  }
  bool txApplyMaxData_(uint64_t value) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC MAX_DATA processing outside Tx thread", return false);
    if (value <= m_txDataCredit.limit()) return true;
    ZutcpLOG(app()->qlogTrace(), ([
      wasBlocked = m_txDataCredit.blocked(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (wasBlocked) {
	o.logCxnDataBlockedUpd(
	  BlockedEvt{
	    .linkInfo = linkInfo
	  ,
	    .oldState = BlockedState::Blocked,
	    .newState = BlockedState::Unblocked,
	    .reason = BlockedReason::CxnFlowCtrl},
	  time);
      }
    }));
    m_txDataCredit.extend(value);
    m_dataBlockedControl = {};
    dataWritable();
    impl()->flowCredit();
    return true;
  }
  bool establishedRx() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Established;
  }
  bool establishedTx() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Established;
  }
  bool closingRx_() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Closing;
  }
  bool closingTx_() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Closing;
  }
  bool debugLog() const {
#if defined(Zutcp_DEBUG) && defined(ZiMultiplex_DEBUG)
    return app()->mx()->debug();
#else
    return false;
#endif
  }
  bool drainingRx() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Draining;
  }
  bool drainingTx_() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Draining;
  }
  bool handshakeStartedRx() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Handshaking ||
      m_rxLinkState == LinkState::Established;
  }
  bool handshakeStartedTx() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Handshaking ||
      m_txLinkState == LinkState::Established;
  }
  bool linkClosedRx_() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx close access outside Rx thread", return true);
    return m_rxAppClose.closed;
  }
  bool linkClosedTx_() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx close access outside Tx thread", return true);
    return m_txAppClose.closed;
  }
  uint64_t closeErrorTx_() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx close error access outside Tx thread", return 0);
    return m_txAppClose.error;
  }
  uint64_t closeErrorRx_() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx close error access outside Rx thread", return 0);
    return m_rxAppClose.error;
  }
  LinkRxDiag rxDiagSnap() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return {});
    return m_rxDiag;
  }
  LinkTxDiag txDiagSnap() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx diagnostic snapshot outside Tx thread", return {});
    return txDiag_();
  }
  template <typename L>
  void diag(L &&l) const {
    auto link = const_cast<Connection *>(this)->impl();
    if (rxInvoked_()) {
      diagRx_(ZuFwd<L>(l));
      return;
    }
    app()->rxRun([link, l = ZuFwd<L>(l)]() mutable {
      link->diagRx_(ZuMv(l));
    });
  }
  template <typename L>
  void diagRx_(L &&l) const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return);
    auto link = const_cast<Connection *>(this)->impl();
    LinkRxDiag rx = m_rxDiag;
    app()->txRun([link, rx, l = ZuFwd<L>(l)]() mutable {
      LinkDiag diag{rx, link->txDiagSnap()};
      l(diag);
    });
  }
  LinkDiag diag_() const {
    return {m_rxDiag, txDiag_()};
  }
  template <typename L>
  void pathDiag(L &&l) const {
    if (txInvoked_()) {
      PathDiag diag = pathDiag_();
      l(diag);
      return;
    }
    auto link = const_cast<Connection *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      PathDiag diag = link->pathDiag_();
      l(diag);
    });
  }
  PathDiag pathDiag_() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path diagnostic snapshot outside Tx thread", return {});
    return m_path.diag();
  }
  bool pathValidated_() const { return m_path.validated(); }
  unsigned activePathMaxPayload_() const { return m_path.activeMaxPayload(); }
  const ZiSockAddr &activePathRemote_() const { return m_path.remote(); }
  uint64_t pathAntiAmp_() const {
    return m_path.antiAmpRemaining();
  }
  void peerParams(const TransportParams &params) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "peer transport parameter installation outside Rx thread", return);
    if (!m_peerParamsSet) m_writeCredit = params.initialMaxData;
    m_peerParams = params;
    m_peerParamsSet = true;
    ZutcpLOG(app()->qlogTrace(), ([
      origDCID = params.origDCID,
      initialSCID = params.initialSCID,
      maxIdleTimeout = params.maxIdleTimeout,
      maxPayloadSize = params.maxPayloadSize,
      ackDelayExponent = params.ackDelayExponent,
      maxAckDelay = params.maxAckDelay,
      initialMaxData = params.initialMaxData,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      ParamsEvt event{
        .origDCID = origDCID,
        .initialSCID = initialSCID,
        .linkInfo = linkInfo,
        .maxIdleTimeout = maxIdleTimeout,
        .maxPayloadSize = maxPayloadSize,
        .ackDelayExponent = ackDelayExponent,
        .maxAckDelay = maxAckDelay,
        .initialMaxData = initialMaxData,
        .initiator = Initiator::Remote,
      };
      o.logParamsSet(event, time);
    }));
    PeerTxParams tx{
      params.initialMaxData, params.maxAckDelay, params.ackDelayExponent,
      true};
    app()->txRun([link = impl(), tx, gen = dataGen()]() mutable {
      if (link->disconnecting_() || gen != link->dataGen() ||
          gen != link->m_txDataGen) return;
      if (!link->m_peerTxParams.set) link->txCredit(tx.initialMaxData);
      link->m_peerTxParams = tx;
    });
  }
























  bool recordTxUnackd_(
    PktNumSpace::T level, const SentFrameRef &ref) {
    switch (ref.kind) {
      case SentFrameKind::Data:
	return txStillUnackd(ref.offset, ref.length, ref.fin) ||
	  recordTxUnackd(ref.offset, ref.length, ref.fin);
      default:
	return true;
    }
  }
  void recordTxUnackd_(PktNumSpace::T level, const TxPktRefs &refs) {
    for (unsigned i = 0, n = refs.count(); i < n; ++i)
      (void)recordTxUnackd_(level, refs[i]);
  }
  bool requeueDataRef(
    PktNumSpace::T level, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Data) return false;
    if (ref.range.storage && ref.range.storage->native && !ref.range.storage->data)
      return false; // retired source must not recreate ACKed byte work
    if (!recordTxUnackd_(level, ref)) return false;
    return requeueRetransmit_(level, ref);
  }
  void requeueDataRefs(
    PktNumSpace::T level, const TxPktRefs &refs, bool queueTx = true) {
    bool queued = false;
    for (unsigned i = 0, n = refs.count(); i < n; ++i) {
      const SentFrameRef &ref = refs[i];
      queued |=
	requeueDataRef(level, ref);
    }
    if (queued && queueTx) impl()->queueRetransmit();
  }
  bool txAckEliciting(ZuBSpan frame, const TxPktRefs *refs, bool fallback) {
    if (fallback) return true;
    if (refs && refs->count()) return true;
    SentFrameRef ref;
    bool ackEliciting = false;
    return sentFrameRef(frame, ref, ackEliciting) ? ackEliciting : fallback;
  }
  bool ackTxFrame(
    PktNumSpace::T level, const SentFrameRef &ref) {
    switch (ref.kind) {
      case SentFrameKind::Data:
	return ackTxUnackd(ref.offset, ref.length, ref.fin, &ref.range);
      case SentFrameKind::Control: {
	switch (ref.controlType) {
	  case FrameType::Reset: {
	    bool ackd = ackReset(ref.value, ref.length);
	    if (ackd) clearControl(ref);
	    return ackd;
	  }
	  case FrameType::StopSending:
	    if (ackStop(ref.value)) {
	      clearControl(ref);
	      return true;
	    }
	    return false;
	  default:
	    controlAckd(ref);
	    return true;
	}
      }
      default:
	return true;
    }
  }

  bool frameStillUnackd(
    PktNumSpace::T level, const SentFrameRef &ref) const {
    switch (ref.kind) {
      case SentFrameKind::Data:
	return txStillUnackd(ref.offset, ref.length, ref.fin);
      case SentFrameKind::Control:
	return controlStillValid_(controlFrame_(ref));
      default:
	return true;
    }
  }
  static bool clipDataRef(
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
    ref.range.dataOffset = dataFirst;
    ref.range.length = uint32_t(newLength);
    if (dataFirst >= oldOffset && dataFirst <= oldOffset + oldLength) {
      uint64_t advance = dataFirst - oldOffset;
      if (oldRangeOffset + advance <= maxTxRangeField)
	ref.range.offset = uint32_t(oldRangeOffset + advance);
    }
    return newLength || newFin;
  }
  bool clipDataRetx(SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Data) return false;
    if (ref.range.storage && ref.range.storage->pool)
      return txStillUnackd(ref.offset, ref.length, ref.fin);
    uint64_t n = ref.length + (ref.fin ? 1 : 0);
    if (!n || n < ref.length) return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    m_txUnackd.spans(
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
      if (!clipDataRef(tail, end, oldEnd)) tail = {};
    }
    return clipDataRef(ref, first, end);
  }




  ZuBSpan initialTxToken() const {
    return {};
  }
  bool clipRetransmit(
    PktNumSpace::T level, SentFrameRef &ref, SentFrameRef &tail) {
    switch (ref.kind) {
      case SentFrameKind::Data:
	return clipDataRetx(ref, tail);
      case SentFrameKind::Control:
	tail = {};
	return controlStillValid_(controlFrame_(ref));
      default:
	tail = {};
	return true;
    }
  }

  void resetDiag() {
    m_rxDiag = {};
    m_txDiag = {};
  }
  LinkTxDiag txDiag_() const {
    LinkTxDiag diag = m_txDiag;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
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
  void updateCongestionDiag() {
    m_txDiag.congestionWindow = m_congestion.cwnd();
    m_txDiag.congestionSSThresh = m_congestion.ssthresh();
    m_txDiag.bytesInFlight = m_congestion.bytesInFlight();
  }
  void noteRxECN(PktNumSpace::T level, EcnMark::T ecn) {
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
  void setEcnDisabled_(bool b = true) {
#ifdef Zutcp_DEBUG
    auto oldState = m_path.ecnState();
#endif
    m_path.setEcnDisabled(b);
#ifdef Zutcp_DEBUG
    auto newState = m_path.ecnState();
    if (oldState != newState) {
      ZutcpLOG(app()->qlogTrace(), ([
	level = PktNumSpace::T(PktNumSpace::AppData),
	oldState = qlogECNState(oldState),
	newState = qlogECNState(newState),
	reason = b ? ZutcpLog_::ECNReason::MarkFailed :
	  ZutcpLog_::ECNReason::Probe,
	disabled = m_path.ecnDisabled(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	ECNEvt event{
	  .linkInfo = linkInfo,
	  .packetSpace = level,
	  .oldState = ECNState::T(oldState),
	  .state = ECNState::T(newState),
	  .reason = ECNReason::T(reason),
	  .disabled = disabled};

	o.logECNStateUpd(event, time);
      }));
    }
#endif
  }
  void publishWriteMPS() {
    unsigned maxPayload = m_path.activeMaxPayload();
    m_writeMPS = maxPayload > 2 * TxDataPktReserve ?
      maxPayload - 2 * TxDataPktReserve : 0;
  }
  PktBudget sendBudget_() const {
    PktBudget budget;
    unsigned maxPayload = m_path.activeMaxPayload();
    budget.pmtu = maxPayload > TxDataPktReserve ?
      maxPayload - TxDataPktReserve : 0;
    budget.antiAmplification = m_path.sendAllowance();
    unsigned allowance = congestionAllowance_();
    budget.congestion = allowance < maxPayload ? allowance : maxPayload;
    return budget;
  }
  static ZutcpLog_::ECNState::T qlogECNState(PathECNState::T state) {
    switch (state) {
      case PathECNState::Disabled: return ZutcpLog_::ECNState::Disabled;
      case PathECNState::Testing: return ZutcpLog_::ECNState::Testing;
      case PathECNState::Capable: return ZutcpLog_::ECNState::Capable;
      case PathECNState::Failed: return ZutcpLog_::ECNState::Failed;
      default: return ZutcpLog_::ECNState::Unknown;
    }
  }
  void initPathECN(Path &path) {
    path.ecn(app()->ecn());
    ZutcpLOG(app()->qlogTrace(), ([
      level = PktNumSpace::T(PktNumSpace::AppData),
      state = qlogECNState(path.ecnState()),
      reason = path.ecnState() == PathECNState::Testing ?
	ZutcpLog_::ECNReason::Probe : ZutcpLog_::ECNReason::AckECN,
      disabled = path.ecnDisabled(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      ECNEvt event{
	.linkInfo = linkInfo,
	.packetSpace = level,
	.oldState = ECNState::Unknown,
	.state = ECNState::T(state),
	.reason = ECNReason::T(reason),
	.disabled = disabled};

      o.logECNStateUpd(event, time);
    }));
  }
  void resetPath() {
    m_path = m_isServer ?
      Path::server(ZiSockAddr{}, ZiSockAddr{}) :
      Path::client(ZiSockAddr{}, ZiSockAddr{});
    initPathECN(m_path);
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.peerMaxPayload(app()->maxPayload());
    publishWriteMPS();
    if (!m_isServer) m_path.validated();
  }
  bool initClientPath_(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    m_closedKeyRx = {local, remote};
    if (!initTCPOriginsRx_(false, local, remote)) return false;
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote), limit, gen = dataGen()
    ]() mutable {
      if (link->disconnecting_() || gen != link->dataGen() || gen != link->m_txDataGen) return;
      link->initClientPathTx_(ZuMv(local), ZuMv(remote), limit);
    });
    return true;
  }
  void initServerPath_(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    m_closedKeyRx = {local, remote};
    if (app()->nativeTx()) m_rxOrigins.server = true;
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote), limit, gen = dataGen()
    ]() mutable {
      if (link->disconnecting_() || gen != link->dataGen() || gen != link->m_txDataGen) return;
      link->initServerPathTx_(ZuMv(local), ZuMv(remote), limit);
    });
  }
  void initClientPathTx_(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC client path initialization outside Tx thread", return);
    m_path = Path::client(ZuMv(local), ZuMv(remote));
    initPathECN(m_path);
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.routePayload(limit);
    m_path.peerMaxPayload(app()->maxPayload());
    publishWriteMPS();
    m_path.validated();
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Client,
      antiAmplification = m_path.antiAmpRemaining(),
      mtu = m_path.activeMaxPayload(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.antiAmplification = antiAmplification,
	.mtu = mtu,
	.kind = PathKind::Path,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathUpdated(event, time);
    }));
  }
  void initServerPathTx_(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC server path initialization outside Tx thread", return);
    m_path = Path::server(ZuMv(local), ZuMv(remote));
    initPathECN(m_path);
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.routePayload(limit);
    m_path.peerMaxPayload(app()->maxPayload());
    publishWriteMPS();
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Server,
      antiAmplification = m_path.antiAmpRemaining(),
      mtu = m_path.activeMaxPayload(),
      validated = false,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.antiAmplification = antiAmplification,
	.mtu = mtu,
	.kind = PathKind::Path,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathUpdated(event, time);
    }));
  }
  void updatePeerPathMaxPayload() {
    if (!m_peerParamsSet) return;
    uint64_t maxPayload = m_peerParams.maxPayloadSize;
    if (maxPayload > BufSize) maxPayload = BufSize;
    app()->txRun([link = impl(), maxPayload = unsigned(maxPayload)]() mutable {
      if (link->disconnecting_()) return;
      link->m_path.peerMaxPayload(maxPayload);
      link->publishWriteMPS();
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
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Validated,
      reason = PathReason::Initial,
      antiAmplification = m_path.antiAmpRemaining(),
      mtu = m_path.activeMaxPayload(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.antiAmplification = antiAmplification,
	.mtu = mtu,
	.kind = PathKind::Path,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathUpdated(event, time);
    }));
  }
  void recordPathRx(ZiSockAddr remote, unsigned bytes) {
    app()->txRun([
      link = impl(),
      remote = ZuMv(remote),
      bytes
    ]() mutable {
      if (link->disconnecting_()) return;
      link->recordPathRxTx_(ZuMv(remote), bytes);
    });
  }
  void recordPathRxTx_(ZiSockAddr remote, unsigned bytes) {
    if (remote && remote != m_path.remote()) return;
    m_path.received(bytes);
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Received,
      reason = PathReason::Datagram,
      bytes,
      antiAmplification = m_path.antiAmpRemaining(),
      mtu = m_path.activeMaxPayload(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.bytes = bytes,
	.antiAmplification = antiAmplification,
	.mtu = mtu,
	.kind = PathKind::Path,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathUpdated(event, time);
    }));
  }
#ifdef Zutcp_DEBUG
  void forceActivePathMTU(unsigned size) {
    m_path.startProbe(size);
    m_path.probeAckd();
    publishWriteMPS();
  }
  bool startPMTUDProbe_(unsigned size) {
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

#endif
  template <typename SendPkt>
  bool sendPathPkt(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) {
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_path.antiAmpRemaining(),
		mtu = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::AntiAmp},
	  time);
      }));
      return false;
    }
    if (!sendPkt(ZuMv(buf), ZuMv(addr), ecn)) return false;
    ZutcpLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return m_path.reserveSend(bytes);
  }
  template <typename SendPkt>
  bool sendPathProbePkt(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) {
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_path.antiAmpRemaining(),
		mtu = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::ProbeAdmit},
	  time);
      }));
      return false;
    }
    if (!sendPkt(ZuMv(buf), ZuMv(addr), ecn)) return false;
    m_path.sent(bytes);
    ZutcpLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  template <typename SendPkt>
  bool sendPathPktApp(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSend(bytes)) {
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_path.antiAmpRemaining(),
		mtu = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::AntiAmp},
	  time);
      }));
      return false;
    }
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), ecn, sent)) return false;
    if (!sent) {
      ZutcpLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::AppSend},
	  time);
      }));
      return true;
    }
    ZutcpLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return m_path.reserveSend(bytes);
  }


  template <typename SendPkt>
  bool sendPathProbePktApp(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_path.canSendProbe(bytes)) {
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_path.antiAmpRemaining(),
		mtu = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .bytes = bytes,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .kind = PathKind::Path,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPathUpdated(event, time);
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::ProbeAdmit},
	  time);
      }));
      return false;
    }
    bool sent = false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr), ecn, sent)) return false;
    if (!sent) {
      ZutcpLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo
	  ,
	    .packetSize = bytes,
	    .reason = Reason::AppSend},
	  time);
      }));
      return true;
    }
    m_path.sent(bytes);
    ZutcpLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  unsigned txPNLength(PktNumSpace::T level) const {
    uint64_t largestAckd = m_txLargestAckd[level];
    if (ZuNull(largestAckd)) return PNLength;
    return PktNumber::encodedLength(m_txPN[level], largestAckd);
  }
  void setTxPN(PktNumSpace::T level, uint64_t pn) {
    m_txPN[level] = pn;
  }
  void resetLink_() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC runtime reset outside Rx thread", return);
    ZmAssert_(!m_cxnSlot);
    disconnecting_(false);
    uint64_t gen = ++m_dataGen;
    ++m_rxDataGen;
    closeDataRx();
    Rx::rxReset(0);
    m_rxState = {};
    m_rxOrigins = {};
    m_rxSynAcked = false;
    m_closedKeyRx = {};
    m_closedSavedRx = false;
    m_mxRxFinish = 0;
    m_mxRxClosing = false;
    m_rxAborting = false;
    m_rxDelivered = m_rxAdmitted = 0;
    m_resetReceived = m_stopReceived = false;
    m_rxAppError = 0;
    m_rxError = DataError::None;
    cancelTimers();
    resetAckPosts();
    resetLinkStateRx();
    m_initialDCID = {};
    m_origDCID = {};
    m_groupID = {};
    m_localSCID = {};
    m_peerCID = {};
    m_localCIDRouted = false;
    m_transportParams = {};
    m_peerParams = {};
    m_peerParamsSet = false;
    m_writeCredit = 0;
    m_rxDataCredit = {};
    m_rxRetired = 0;
    m_rxWindowSerial = 0;
    resetPath();
    app()->txInvoke(impl(), [link = impl(), gen]() mutable {
      if (link->disconnecting_()) return link;
      link->resetTx_(gen);
      return link;
    });
  }
  void resetTx_() { resetTx_(++m_dataGen); }
  void resetTx_(uint64_t gen) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC Tx runtime reset outside Tx thread", return);
    if (gen != dataGen()) return;
    ++m_txRuntimeGen;
    cancelPersist_();
    m_txDataGen = gen;
    m_txOrigins = {};
    m_tcpSynSent = false;
    unsigned queued = txRangeCount();
    m_txRanges.reset(0);
    m_txQueueCount -= queued;
    // New generation; old handoffs still retire their count.
    m_txQueueCount &= TxQueueState::CountMask;
    m_txUnackd.clean();
    m_txBytes = m_txBufferedBytes = m_txFrameMax = 0;
    m_txNext = m_txAckd = m_txRttFence = m_txRtoEnd = 0;
    m_txRxNext = m_txRxLimit = m_txRxSerial = 0;
    m_windowDirty = m_txWindowSet = m_peerWindowZero = false;
    m_txWindowSeq = m_txWindowAck = 0;
    m_dataRto.reset();
    m_fin = m_finDequeued = m_finAckd = m_txCompleted = false;
    m_tcpCloseState = TcpCloseState::Open;
    m_mxPeerFinished = m_mxFinishing = m_mxClosePeer = false;
    m_tcpResetSent = m_tcpResetPeer = false;
    m_resetSent = m_resetAckd = m_stopSent = m_stopAckd = false;
    m_txAppError = 0;
    m_txError = DataError::None;
    m_txReady = false;
    m_lastDataBlocked = U64Null;
    m_txDataCredit = {};
    m_peerTxParams = {};
    m_txAppClose = {};
    m_txLinkState = LinkState::Starting;
    m_txInitialDCID = {};
    m_txLocalParams = {};
    m_txPeerCID = {};
    m_lossTimerOut = ZuTime{};
    m_ptoTimerOut = ZuTime{};
    m_ptoTimerLevel = PktNumSpace::Initial;
    m_idleTimeout = {};
    m_idleTimerOut = ZuTime{};
    m_idleBase = {};
    m_idleAckElicitingSent = false;
    m_heartBeatTimerOut = ZuTime{};
    m_heartBeatBase = {};
    m_closeTimerOut = ZuTime{};
    m_closeNextResponse = {};
    clearPendingControls();
    resetPkts();
    m_congestion = NewReno{app()->maxPayload()};
    updateCongestionDiag();
  }

  void closeLink(uint64_t = 0) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC link close outside Rx thread", return);
    closeDataRx();
    cancelTimers();
    closeLinkStateRx();
    publishLinkSnapshotTx();
  }



  void endpointReady_() { ++m_rxDiag.endpointReady; }
  void endpointFailure_() { ++m_rxDiag.failures; }
  void packetParseFailure() { ++m_rxDiag.failures; }






  void dataBlocked(uint64_t maximum) {
    impl()->flowBlocked(maximum);
  }
  void flowBlocked(uint64_t) {
    ++m_txDiag.unhandledAppEvents;
  }
  void transportClose(FrameType::T, uint64_t) {
    ++m_rxDiag.unhandledAppEvents;
  }




  void dataFrame(uint64_t, ZuBSpan, bool) { }





  bool sendPersistProbe(PktBuild &) { return false; }
  void flowCredit() { }
  void queueTxFlush() { }



  void disconnected(bool peer) {
    disconnected(peer, []() { });
  }
  template <typename L>
  void disconnected(bool peer, L &&l) {
    app()->rxRun([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->Self::disconnected_0(peer, ZuMv(l));
    });
  }
  template <typename L>
  void disconnected_0(bool peer, L &&l) {
    auto mx = app()->mx();
    if (mx && app()->ioActive() && app()->txThread()) {
      ZiAssert(rxInvoked_(), "Zutcp", (),
	"QUIC link disconnect completion outside Rx thread", return);
      closingCxnRx_();
      auto stack = app()->nativeStack();
      ClosedTupleKey key = impl()->closedKeyRx();
      bool remember = stack && !m_closedSavedRx &&
	key.p<0>().ip() && key.p<0>().port() &&
	key.p<1>().ip() && key.p<1>().port();
      ClosedTuple record;
      if (remember) {
	record = closedSnapshotRx(key);
	m_closedSavedRx = true;
      }
      closeDataRx();
      closedLinkStateRx();
      app()->retireLinkRoutes(impl());
      LinkStateSnapshot snapshot = linkSnapshotRx();
      app()->txRun([
	link = impl(), peer, snapshot, stack, record, remember, l = ZuFwd<L>(l)
      ]() mutable {
	if (remember) link->closedSnapshotTx(record);
	link->applyLinkSnapshotTx_(snapshot);
	link->app()->mx()->txRun([
	  link, peer, stack, record, remember, l = ZuMv(l)
	]() mutable {
	  link->app()->rxRun([
	    link, peer, stack, record, remember, l = ZuMv(l)
	  ]() mutable {
	    if (remember) stack->rememberClosed(record);
	    link->Self::disconnected_(peer);
	    l();
	  });
	});
      });
      return;
    }
    // No shard remains after multiplexer finalization.
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(LinkState::Closed);
    if (oldState != newState)
      app()->linkState_(oldState, newState);
    m_rxLinkState = LinkState::Closed;
    m_txLinkState = LinkState::Closed;
    m_publicLinkState = unsigned(LinkState::Closed);
    l();
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
    app()->txRun([link = impl(), initialDCID, localSCID]() mutable {
      link->m_txInitialDCID = initialDCID;
      link->m_txLocalParams.initialSCID = localSCID;
    });
    publishActivePeerCID();
  }

  void setPeerCIDFromHdrSCID(const LongHdr &h) {
    if (h.scid) {
      m_peerCID = h.scid;
      publishActivePeerCID();
    }
  }

  void publishActivePeerCID() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "Zutcp peer CID publication outside Rx thread", return);
    CxnID id = m_peerCID;
    app()->txRun([link = impl(), id]() mutable {
      if (link->disconnecting_()) return;
      link->m_txPeerCID = id;
    });
  }

  template <typename Routes>
  void installLocalCIDRoutes_(Routes &routes) {
    if (m_localCIDRouted || !routes.add(m_localSCID, impl())) return;
    m_localCIDRouted = true;
    ZutcpLOG(app()->qlogTrace(), ([
      cxnID = m_localSCID, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CIDEvt event{
        .cxnID = cxnID, .linkInfo = linkInfo,
        .action = CIDAction::RouteBound, .reason = CIDReason::RouteInstall,
        .length = qlogCount(cxnID.length()), .local = true, .associated = true};
      o.logCIDUpdated(event, time);
    }));
  }
  template <typename Routes>
  void tombstoneLocalCIDRoutes_(Routes &routes) {
    if (!m_localCIDRouted) return;
    routes.tombstone(m_localSCID);
    m_localCIDRouted = false;
    ZutcpLOG(app()->qlogTrace(), ([
      cxnID = m_localSCID, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CIDEvt event{
        .cxnID = cxnID, .linkInfo = linkInfo,
        .action = CIDAction::Tombstone, .reason = CIDReason::RouteTombstone,
        .length = qlogCount(cxnID.length()), .local = true};
      o.logCIDUpdated(event, time);
    }));
  }
  bool acceptLocalShortCID(const CxnID &id) const { return id == m_localSCID; }

  const CxnID &cid_(CIDSel::T cid) const {
    switch (cid) {
      case CIDSel::Initial: return m_txInitialDCID;
      case CIDSel::Local: return m_txLocalParams.initialSCID;
      default: return m_txPeerCID;
    }
  }

  Zutcp::LinkInfo linkInfo_() const {
    return Zutcp::LinkInfo{
      .origDCID = m_origDCID.length() ? m_origDCID : m_initialDCID,
      .groupID = m_groupID.length() ? m_groupID : m_initialDCID,
      .dcid = m_initialDCID,
      .scid = m_localSCID
    };
  }
  const TransportParams &localTransportParams() const {
    return m_transportParams;
  }

  template <typename AppLike>
  static void fillLocalParams(TransportParams &params, AppLike *app) {
    params.maxPayloadSize = app->maxPayload();
    params.initialMaxData = app->maxData();
    if (app->nativeTx() && params.initialMaxData > 65535) params.initialMaxData = 65535;
    params.maxIdleTimeout = app->maxIdleTimeout();
  }

  template <typename AppLike>
  void configLocalParams(AppLike *app) {
    m_transportParams.initialSCID = m_localSCID;
    fillLocalParams(m_transportParams, app);
    m_rxDataCredit.set(m_transportParams.initialMaxData);
    ZutcpLOG(app->qlogTrace(), ([
      origDCID = m_transportParams.origDCID,
      initialSCID = m_transportParams.initialSCID,
      maxIdleTimeout = m_transportParams.maxIdleTimeout,
      maxPayloadSize = m_transportParams.maxPayloadSize,
      ackDelayExponent = m_transportParams.ackDelayExponent,
      maxAckDelay = m_transportParams.maxAckDelay,
      initialMaxData = m_transportParams.initialMaxData,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      ParamsEvt event{
        .origDCID = origDCID,
        .initialSCID = initialSCID,
        .linkInfo = linkInfo,
        .maxIdleTimeout = maxIdleTimeout,
        .maxPayloadSize = maxPayloadSize,
        .ackDelayExponent = ackDelayExponent,
        .maxAckDelay = maxAckDelay,
        .initialMaxData = initialMaxData,
        .initiator = Initiator::Local,
      };
      o.logParamsSet(event, time);
    }));
    // App configuration is immutable while running. Transfer only bootstrap
    // identity; construct the sender's parameter record on its own shard.
    app->txRun([
      link = impl(), origDCID = m_transportParams.origDCID
    ]() mutable {
      link->m_txLocalParams.origDCID = origDCID;
      fillLocalParams(link->m_txLocalParams, link->app());
      link->m_txRxLimit = link->m_txLocalParams.initialMaxData;
    });
  }

  bool loadSrvParams(const ServerBootstrap &bootstrap) {
    return bootstrap.transportParams(m_transportParams);
  }





  bool readyToEstablish() const {
    return m_rxLinkState == LinkState::Handshaking && m_peerParamsSet &&
      (!app()->nativeTx() || m_rxSynAcked);
  }

  bool validateServerParams_(const ClientBootstrap &bootstrap) {
    return m_peerParamsSet && bootstrap.validateServerParams(m_peerParams, m_peerCID);
  }

  bool validateClientParams() const {
    return m_peerParamsSet;
  }

  void establish_() {
    updatePeerPathMaxPayload();
    if (!establishState()) return;
    if (auto stack = app()->nativeStack()) stack->establishedCxn(m_cxnSlot);
    startIdleTimer();
    startHeartBeatTimer();
    ++m_rxDiag.handshakeComplete;
  }



  void resetPkts() {
    cancelTimers();
    for (auto &pn : m_txPN) pn = 0;
    for (auto &pn : m_txLargestAckd) pn = U64Null;
    for (auto &pn : m_rxLargestPN) pn = 0;
    m_rxAcks.clear();
    for (auto &ack : m_txAck) ack = {};
    for (auto &p : m_txPkts) p.clear();
    m_txInitProbeOut = {};
    m_txAppProbeOut = {};
    for (auto &discarded : m_rxSpaceDiscarded) discarded = false;
    for (auto &discarded : m_txSpaceDiscarded) discarded = false;
    m_rtt = {};
    m_ptoBackoff.reset();
  }

  void closeDataRx() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection data Rx close outside Rx thread", return);
    closeRx();
    m_rxAccount = nullptr; // held slots keep their retired generation account
  }

















  bool txPending() const {
    return controlQueued() || (!resetSent() && (txRangeCount() || finReady()));
  }



  SentFrameRef controlRef(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return SentFrameRef::flowUpdate({FrameType::MaxData, frame.value});
      case FrameType::DataBlocked:
	return SentFrameRef::blocked(frame.value);
      case FrameType::Reset:
	return SentFrameRef::resetData( frame.errorCode, frame.value);
      case FrameType::StopSending:
	return SentFrameRef::stopSending( frame.errorCode);
      default:
	return SentFrameRef::control();
    }
  }

  ControlFrame controlFrame_(const SentFrameRef &ref) const {
    ControlFrame frame;
    frame.type = ref.controlType;
    frame.value = ref.value;
    if (ref.controlType == FrameType::Reset) {
      frame.errorCode = ref.value;
      frame.value = ref.length;
    } else if (ref.controlType == FrameType::StopSending) {
      frame.errorCode = ref.value;
      frame.value = 0;
    }
    return frame;
  }

  bool appendControl(
    const ControlFrame &frame, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    SentControls &sentControls) {
    if (!controlStillValid_(frame)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !assembly.addControl(budget, unsigned(n)) ||
	!build.commitScratch(unsigned(n)))
      return false;
    if (!refs.add(controlRef(frame))) return false;
    sentControls.push(frame);
    return true;
  }

  bool appendQueuedCtl(
    PendingControl &slot, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    SentControls &sentControls) {
    if (!slot.queued) return true;
    if (!controlStillValid_(slot.frame)) {
      slot = {};
      return true;
    }
    return appendControl(
      slot.frame, build, budget, assembly, refs,
      sentControls);
  }

  bool appendQueuedCtls(
    PktBuild &build, PktBudget &budget, PktAssembly &assembly,
    TxPktRefs &refs, SentControls &sentControls) {
    if (refs.count() >= SentPkt::MaxFrames) return true;
    if (!appendQueuedCtl(
	  m_maxDataControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedCtl(
	  m_dataBlockedControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    return true;
  }

  bool appendByteCtls(
    PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    SentControls &sentControls) {
    while (refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame;
      if (!this->nextQueuedControl(frame)) return true;
      if (!this->controlStillValid(frame)) {
	this->controlSent(frame);
	continue;
      }
      if (!appendControl(
	    frame, build, budget, assembly, refs,
	    sentControls))
	return false;
      this->controlSent(frame);
      return true;
    }
    return true;
  }

  void controlSent_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::MaxData:
	pendingControlSent(m_maxDataControl, frame);
	break;
      case FrameType::DataBlocked:
	pendingControlSent(m_dataBlockedControl, frame);
	break;
      case FrameType::StopSending:
      case FrameType::Reset:
	controlSent(frame);
	break;
      default:
	break;
    }
    noteControlDequeued(frame);
  }

  void pendingControlSent(PendingControl &slot, const ControlFrame &frame) {
    if (slot.frame == frame) slot.queued = false;
  }
  void clearPendingControl(PendingControl &slot, const ControlFrame &frame) {
    if (slot.frame == frame) slot = {};
  }

  void controlAckd(const SentFrameRef &ref) {
    ControlFrame frame = controlFrame_(ref);
    switch (ref.controlType) {
      case FrameType::MaxData:
	clearPendingControl(m_maxDataControl, frame);
	break;
      case FrameType::DataBlocked:
	clearPendingControl(m_dataBlockedControl, frame);
	break;
      case FrameType::Reset:
      case FrameType::StopSending:
	clearControl(ref);
	break;
      default:
	break;
    }
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedControlPkt(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (),
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
    SentControls sentControls;
    if (!appendQueuedCtls(
	  build, budget, assembly, refs, sentControls))
      return false;
    if (refs.count()) {
      if (!sendPkt(build, ZuMv(addr), refs)) return false;
      for (unsigned i = 0, n = sentControls.length(); i < n; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return false;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushDataBatch_(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    bool sent = false;
    for (unsigned i = 0; i < TxFlushBatch; ++i) {
      bool last = i + 1 == TxFlushBatch;
      uint64_t packetsTx = last ? txPackets() : 0;
      bool sent_ = flushData(addr, appendAck, sendPkt);
      sent |= sent_;
      if (!sent_ || !txReady()) break;
      // Yield after a productive full batch. A flow-blocked result can be
      // true without a packet; it must await credit rather than repost itself.
      if (last && txPackets() != packetsTx)
	impl()->queueTxFlush(addr);
    }
    return sent;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushData(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC control/data flush outside Tx thread", return false);
    return flushDataTx(
      ZuMv(addr),
      [&appendAck](PktBuild &build) { return appendAck(build); },
      [&sendPkt](
	  PktBuild &build, ZiSockAddr addr_,
	  const TxPktRefs &refs) {
	return sendPkt(build, ZuMv(addr_), refs);
      });
  }

  template <typename AppendAck, typename SendPkt>
  bool flushDataTx(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC control/data flush outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) {
      if (app()->nativeTx() && m_windowDirty) return sendPkt(build, ZuMv(addr), TxPktRefs{});
      return false;
    }
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    TxPktRefs refs;
    TxRange first;
    bool direct = txRange(0, first) && first.storage->native;
    PktBudget controlBudget = budget;
    if (direct && controlBudget.pmtu > TxDataPktReserve - DataPktizer::PrefixMax)
      controlBudget.pmtu = TxDataPktReserve - DataPktizer::PrefixMax;
    bool controlSent = false;
    unsigned before = build.bytes();
    bool ackBuilt = appendAck(build);
    unsigned ackBytes = build.bytes() - before;
    if (!ackBuilt || (ackBytes && !controlBudget.add(ackBytes))) {
      if (!direct) return false;
      // A large ACK uses reserved control backing before data. Refusal keeps
      // it pending; data still owns its original slot and may make progress.
      if (ackBuilt && ackBytes) controlSent = sendPkt(build, addr, TxPktRefs{});
      build.reset();
      ackBytes = 0;
    }
    SentControls sentControls;
    if (!appendQueuedCtls(
	  build, controlBudget, assembly, refs, sentControls) && !direct)
      return false;
    budget.used = controlBudget.used;
    auto fail = [&refs, controlSent, this]() {
      requeueDataRefs(PktNumSpace::AppData, refs);
      return controlSent;
    };
    bool blocked = false;
    while (txReady() && refs.count() < SentPkt::MaxFrames) {
      if (!takeTxReady() || !txPending()) continue;
      if (this->controlQueued()) {
	if (!appendByteCtls(
	      build, direct ? controlBudget : budget, assembly, refs,
	      sentControls) && !direct)
	  return fail();
	if (txPending())
	  dataWritable();
	if (!direct) continue;
	budget.used = controlBudget.used;
      }
      if (this->txRangeCount() && m_txDataCredit.blocked()) {
	dataWritable();
	updatePersist_();
	blocked = true;
	break;
      }
      TxRange next;
      if (assembly.dataAdded() && txRange(0, next) && next.storage->native) {
	dataWritable();
	break;
      }
      DataFrameInfo info;
      int n = DataPktizer::writeNext(
	build.scratch(), build.scratchAvail(),
	budget, assembly, *impl(), &info);
      if (n <= 0) {
	dataWritable();
	break;
      }
      SentFrameRef ref =
	SentFrameRef::data( info.range, info.fin);
      if (!info.length) {
	ref.offset = info.offset;
	ref.length = 0;
      }
      if (!build.commitScratch(unsigned(n)) || !build.add(info.range)) {
	requeueDataRefs(
	  PktNumSpace::AppData, refs, false);
	bool queued = requeueDataRef(
	  PktNumSpace::AppData, ref);
	if (queued) impl()->queueRetransmit();
	return controlSent;
      }
      if (!refs.add(ref)) {
	requeueDataRefs(
	  PktNumSpace::AppData, refs, false);
	bool queued = requeueDataRef(
	  PktNumSpace::AppData, ref);
	if (queued) impl()->queueRetransmit();
	return controlSent;
      }
      m_txDiag.dataBytesTx += info.length;
      
      if (txPending())
	dataWritable();
      if (info.range.storage && info.range.storage->native) break;
    }
    if (refs.count() || ackBytes || (app()->nativeTx() && m_windowDirty)) {
      if (!sendPkt(build, ZuMv(addr), refs)) {
	requeueDataRefs(PktNumSpace::AppData, refs);
	return controlSent;
      }
      for (unsigned i = 0, n = sentControls.length(); i < n; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return controlSent || blocked;
  }



  template <typename AppendAck, typename SendPkt>
  bool sendDataPkt(
    ZiSockAddr addr, AppendAck appendAck,
    SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection packetization outside Tx thread", return false);
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_txDataCredit.available();
    PktAssembly assembly;
    if (this->txRangeCount() && m_txDataCredit.blocked()) {
      dataWritable();
      updatePersist_();
      return false;
    }
    TxRange first;
    bool direct = txRange(0, first) && first.storage->native;
    unsigned before = build.bytes();
    bool ackBuilt = appendAck(build);
    unsigned controlBytes = build.bytes() - before;
    if (direct && (!ackBuilt ||
        controlBytes > TxDataPktReserve - DataPktizer::PrefixMax)) {
      build.reset();
      controlBytes = 0;
    } else if (!ackBuilt) return false;
    if (controlBytes && !assembly.addControl(budget, controlBytes)) return false;
    DataFrameInfo info;
    int n = DataPktizer::writeNext(
      build.scratch(), build.scratchAvail(),
      budget, assembly, *impl(), &info);
    if (n <= 0) return false;
    SentFrameRef ref = SentFrameRef::data( info.range, info.fin);
    if (!info.length) {
      ref.offset = info.offset;
      ref.length = 0;
    }
    if (!build.commitScratch(unsigned(n)) || !build.add(info.range)) {
      bool queued = requeueDataRef(
	PktNumSpace::AppData, ref);
      if (queued) impl()->queueRetransmit();
      return false;
    }
    TxPktRefs refs;
    if (!refs.add(ref)) {
      bool queued = requeueDataRef(
	PktNumSpace::AppData, ref);
      if (queued) impl()->queueRetransmit();
      return false;
    }
    if (!sendPkt(build, ZuMv(addr), refs)) {
      requeueDataRefs(PktNumSpace::AppData, refs);
      return false;
    }
    m_txDiag.dataBytesTx += info.length;
    
    return true;
  }

  uint64_t localMaxAckDelayUS() const {
    return uint64_t(m_transportParams.maxAckDelay) * TimeUSPerMS;
  }

  uint64_t nowUS() const {
    return uint64_t(now_().microsecs());
  }

  bool immediateAck_(PktNumSpace::T) const { return true; }

  uint64_t rxByteNext() const {
    uint64_t next = m_rxState.delivered();
    return next + unsigned(m_rxState.finSeen() && m_rxState.finalSize() == next);
  }

  bool clipTCPData(Frame &frame, uint32_t seq) const {
    uint64_t next = rxByteNext();
    int64_t first = int32_t(seq - uint32_t(next));
    int64_t end = first + frame.length;
    uint64_t window = m_rxDataCredit.limit() > next ? m_rxDataCredit.limit() - next : 0;
    if (window > 65535) window = 65535;
    if (!window || first >= int64_t(window) || end + unsigned(frame.fin) <= 0)
      return false;
    unsigned head = first < 0 ? unsigned(-first) : 0;
    unsigned tail = end > int64_t(window) ? unsigned(end - window) : 0;
    frame.fin = frame.fin && end >= 0 && end < int64_t(window);
    frame.offset = first < 0 ? next : next + first;
    frame.length -= head + tail;
    frame.payload = {frame.payload.data() + head, unsigned(frame.length)};
    return frame.length || frame.fin;
  }

  int admitTCPSegment(const TCPHeader &tcp, ZuBSpan frames) const {
    uint64_t length = 0;
    auto storage = ZmScratch(Frame, 1);
    auto &frame = *new (storage.push()) Frame{};
    for (unsigned offset = 0; offset < frames.length();) {
      unsigned remaining = frames.length() - offset, used = 0;
      if (FrameCodec::parse({frames.data() + offset, remaining}, frame, used) < 0 ||
	  !used || used > remaining || !packetFrameLegal_(PktNumSpace::AppData, frame))
	return -1;
      // Native production emits one original DATA buffer per packet. The
      // current carrier maps every DATA frame to the same TCP header SEQ.
      if (frame.type == FrameType::Data && frame.length > length) length = frame.length;
      offset += used;
    }
    length += unsigned(bool(tcp.flags & 0x01));
    return tcpSequenceAllowed(tcp.seq, length);
  }

  bool tcpSequenceAllowed(uint32_t seq, uint64_t length) const {
    uint64_t next = rxByteNext();
    uint64_t window = m_rxDataCredit.limit() > next ? m_rxDataCredit.limit() - next : 0;
    if (window > 65535) window = 65535;
    int64_t first = int32_t(seq - uint32_t(next));
    if (!length) return window ? first >= 0 && uint64_t(first) < window : !first;
    return window && first < int64_t(window) && first + int64_t(length) > 0;
  }

  void receiveTCPReset_() {
    if (m_rxAborting || disconnecting_()) return;
    closeTxAdmission();
    closingCxnRx_();
    m_rxAborting = true;
    closeDataRx();
    cancelTimers();
    app()->txRun([link = impl(), gen = m_rxDataGen] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting_()) return;
      link->m_tcpResetPeer = true;
      link->closeStateTx();
      link->tcpResetAbort_(true);
    });
  }

  void replyTCPWindow() {
    auto next = rxByteNext(), limit = m_rxDataCredit.limit();
    auto serial = ++m_rxWindowSerial;
    app()->txRun([link = impl(), next, limit, serial, gen = m_rxDataGen] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting_()) return;
      link->localWindow_(serial, next, limit);
      // A rejected persist probe needs a reply even when the edge is unchanged.
      link->m_windowDirty = true;
      link->flushTx_();
    });
  }

  void postAckSnapshot(PktNumSpace::T level, ZiSockAddr addr) {
    PktNumSpace::T space = level;
    if (!m_rxAcks.pending(space)) return;
    AckSnapshot ack;
    ack.level = level;
    ack.gen = m_rxAcks.gen(space);
    if (level == PktNumSpace::AppData) {
      ack.byteNext = rxByteNext();
      ack.rxLimit = m_rxDataCredit.limit();
      ack.windowSerial = ++m_rxWindowSerial;
    }
    ack.largestRxTime = m_rxAcks.largestRxTime(space);
    ack.due = m_rxAcks.immediate(space);
    ack.ecn = m_rxAcks.ackECN(space);
    const AckTracker &tracker = m_rxAcks.tracker(space);
    ack.ranges = tracker.snapshot(Frame::MaxAckRanges);
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
      link->consumeAckSnapshotTx(level);
    });
  }

  void noteAck(
    PktNumSpace::T level, uint64_t pn, bool ackEliciting, ZiSockAddr addr,
    bool forceImmediate = false, EcnMark::T ecn = EcnMark::NotECT) {
    if (m_rxSpaceDiscarded[level]) return;
    PktNumSpace::T space = level;
    uint64_t now = nowUS();
    bool immediate = ackEliciting && (forceImmediate || immediateAck_(level));
    if (!m_rxAcks.received(
	space, pn, now, localMaxAckDelayUS(), ackEliciting, immediate, ecn))
      return;
    ++m_rxDiag.ackCommitsRx;
    if (ackEliciting) ++m_rxDiag.ackElicitingRx;
    if (immediate) ++m_rxDiag.ackImmediateRx;
    noteRxECN(level, ecn);
    if (pn > m_rxLargestPN[level])
      m_rxLargestPN[level] = pn;
    postAckSnapshot(level, ZuMv(addr));
    if (level == PktNumSpace::AppData && establishedRx())
      notePeerPkt();
  }

  void noteDupAck(PktNumSpace::T level, uint64_t pn, ZiSockAddr addr) {
    if (m_rxSpaceDiscarded[level] || drainingRx() || closingRx_()) return;
    if (!m_rxAcks.ackEliciting(
	level, pn, nowUS(), localMaxAckDelayUS(), true))
      return;
    ++m_rxDiag.ackCommitsRx;
    ++m_rxDiag.ackElicitingRx;
    ++m_rxDiag.ackImmediateRx;
    postAckSnapshot(level, ZuMv(addr));
  }

  bool noteAckTx(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK snapshot install outside Tx thread", return false);
    if (app()->nativeTx() && ack.level == PktNumSpace::AppData)
      localWindow_(ack.windowSerial, ack.byteNext, ack.rxLimit);
    bool due = m_txAck[ack.level].due || ack.due;
    m_txAck[ack.level] = ack;
    m_txAck[ack.level].due = due;
    ++m_txDiag.ackSnapshotInstallsTx;
    if (due) ++m_txDiag.ackDueInstallsTx;
    return due;
  }

  void resetAckPosts() {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
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

  void consumeAckSnapshotTx(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zutcp", (),
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
    if (m_txSpaceDiscarded[level]) return;
    if (noteAckTx(ack)) {
      cancelAckDelayTimer();
      impl()->flushTx_(ZuMv(addr));
    } else if (deadline) {
      scheduleAckDelayTimer(intervalUS(deadlineUS));
    }
  }

  bool appendPendingAck(
    PktNumSpace::T level, PktBuild &build, bool ackOnly = false) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK append outside Tx thread", return false);
    AckSnapshot &ack = m_txAck[level];
    ++m_txDiag.ackAppendTx;
    if (!ack.ranges) {
      ++m_txDiag.ackAppendEmptyTx;
      return true;
    }
    if (ackOnly && !ack.due) {
      ++m_txDiag.ackAppendNotDueTx;
      return true;
    }
    AckECN ecn = m_path.ecnDisabled() ? AckECN{} : ack.ecn;
    int n = level == PktNumSpace::AppData && !app()->nativeTx() ?
      FrameCodec::writeDataAck(build.scratch(), build.scratchAvail(), ack.byteNext,
	ack.ranges.data(), ack.ranges.length(), ecn) :
      FrameCodec::writeAckECN(build.scratch(), build.scratchAvail(),
	ack.ranges.data(), ack.ranges.length(), 0, ecn);
    if (n < 0) return false;
    if (!build.commitScratch(unsigned(n))) return false;
    build.markAck(unsigned(level));
    return true;
  }

  bool pendingAck(PktNumSpace::T level) const {
    const AckSnapshot &ack = m_txAck[level];
    return ack.ranges && ack.due;
  }

  void ackSentTx_(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK commit outside Tx thread", return);
    AckSnapshot ack = m_txAck[level];
    if (!ack.ranges) return;
    ++m_txDiag.ackSentTx;
    m_txAck[level].ranges.clear();
    m_txAck[level].due = false;
    app()->rxRun([link = impl(), level, gen = ack.gen,
      byteNext = ack.byteNext, dataGen = m_txDataGen]() mutable {
      if (link->disconnecting_() || dataGen != link->m_rxDataGen) return;
      link->ackSentRx_(level, gen, byteNext);
    });
  }

  void ackSentRx_(PktNumSpace::T level, uint64_t gen, uint64_t byteNext = U64Null) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC ACK receive-state commit outside Rx thread", return);
    m_rxAcks.sent(level, gen);
    if ((mxCxn || app()->nativeTx()) && level == PktNumSpace::AppData &&
	m_rxState.finSeen() &&
	m_rxState.complete() && byteNext == rxByteNext()) {
      m_mxRxFinish |= MxFINAck;
      finishMxRx_();
    }
  }

  void scheduleAckDelayTimer(ZuTime out) {
    scheduleCxnTimer(
      "ACK delay", CxnTimer::AckDelay,
      out, ZmScheduler::Advance, &m_ackDelayTimer);
  }
  void cancelAckDelayTimer() { cancelTimer_(CxnTimer::AckDelay, &m_ackDelayTimer); }

  void scheduleLossTimer(ZuTime out) {
    scheduleCxnTimer(
      "loss time", CxnTimer::Loss,
      out, ZmScheduler::Update, &m_lossTimer);
  }
  ZuTime lossThreshold() const { return m_rtt.timeThreshold(); }
  ZuTime persistentCongestionThresh() const {
    return ptoTimeout() * ZuDecimal{3};
  }
  ZuTime nextLossTime_() const {
    ZuTime threshold = lossThreshold();
    ZuTime out;
    bool have = false;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
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
  bool lossPending() const {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto level = PktNumSpace::T(i);
      if (m_txSpaceDiscarded[level] ||
	  ZuNull(m_txLargestAckd[level]) ||
	  !m_txPkts[level].ackElicitingInFlight())
	continue;
      return true;
    }
    return false;
  }
  bool detectLoss(
    PktNumSpace::T level, ZuTime now, PktLossBatch &batch, bool &complete) {
    complete = true;
    if (m_txSpaceDiscarded[level]) return false;
    ZuTime threshold = lossThreshold();
    if (!*threshold) return false;
    PktTxUpdate update;
    complete = m_txPkts[level].markTimeThreshLossBatch(
      m_txLargestAckd[level], now, threshold, batch,
      RecoveryScanBatch, &update);
    if (!update.lostBytes)
      return false;
    applyLossUpdateTx(update);
    ZutcpLOG(app()->qlogTrace(), ([
      level,
      lostBytes = update.lostBytes,
      bytesInFlight = m_congestion.bytesInFlight(),
      lostFrames = QLogTxFrameRefs{update.lostFrames},
      reason = RecReason::TimeThreshold,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (!lostBytes) return;
      RecLogEvt event{
		.linkInfo = linkInfo
      ,
		.bytes = lostBytes,
		.bytesInFlight = bytesInFlight,
	.kind = RecKind::Aggregate,
	.packetSpace = level,
	.reason = RecReason::T(reason),
		.frameCount = qlogCount(lostFrames.count())};
      for (unsigned i = 0, n = lostFrames.count(); i < n; ++i)
	qlogAddTxFrame(event, lostFrames[i]);

      o.logPktLost(event, time);
      o.logRecPktLost(event, time);
      if (event.frames)
	o.logMarkRetrans(event, time);
    }));
    if (complete && m_txPkts[level].persistentCongestion(
	persistentCongestionThresh(), RecoveryScanBatch))
      persistentCongestion_();
    updateCongestionDiag();
    return true;
  }
  void detectLossTx(
    unsigned level, ZuTime now, PktLossBatch batch, bool lost) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC loss scan outside Tx thread", return);
    while (level < PktNumSpace::N) {
      PktNumSpace::T space = PktNumSpace::T(level);
      bool complete = true;
      lost |= detectLoss(space, now, batch, complete);
      if (!complete) {
	app()->txRun([
	  link = impl(), level, now, batch, lost
	]() mutable {
	  if (link->disconnecting_()) return;
	  link->detectLossTx(level, now, batch, lost);
	});
	return;
      }
      ++level;
      batch = {};
    }
    if (lost) {
      impl()->queueRetransmit();
    }
    schedulePTO_();
    scheduleLossTimer();
  }
  void scheduleLossTimer() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC loss timer schedule outside Tx thread", return);
    if (linkClosedTx_()) return;
    if (!lossPending()) {
      ++m_txDiag.lossCanceled;
      cancelLossTimer();
      return;
    }
    ZuTime out = nextLossTime_();
    if (!*out) {
      ++m_txDiag.lossCanceled;
      ZutcpLOG(app()->qlogTrace(), ([
	kind = RecKind::Loss,
	space = PktNumSpace::N,
	reason = RecReason::Canceled,
	deadlineUS = uint64_t{0},
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
      cancelLossTimer();
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_lossTimer && m_lossTimerOut == out) return;
    ++m_txDiag.lossArmed;
    m_lossTimerOut = out;
    ZutcpLOG(app()->qlogTrace(), ([
      kind = RecKind::Loss,
      space = PktNumSpace::N,
      reason = RecReason::Armed,
      deadlineUS = qlogUS(out),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo
      ,
		.deadlineUS = deadlineUS,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason)};

      o.logLossTimerUpd(event, time);
    }));
    scheduleLossTimer(out);
  }
  void cancelLossTimer() {
    m_lossTimerOut = ZuTime{};
    cancelTimer_(CxnTimer::Loss, &m_lossTimer);
  }

  void schedulePTO() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC PTO schedule before app initialization", return);
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->schedulePTO_();
    });
  }

  void schedulePMTUD() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC PMTUD schedule before app initialization", return);
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->schedulePMTUD_();
    });
  }

  void schedulePTO_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PTO schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC PTO schedule before app initialization", return);
    if (linkClosedTx_()) return;
    ++m_txDiag.ptoSched;
    PktNumSpace::T level = PktNumSpace::Initial;
    if (!ptoLevel(level)) {
      ++m_txDiag.ptoNoLevel;
      ZutcpLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	space = PktNumSpace::N,
	reason = RecReason::NoLevel,
      deadlineUS = uint64_t{0},
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
      cancelPTO_();
      return;
    }
    ZuTime out = ptoDeadline(level);
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_ptoTimer && m_ptoTimerOut == out && m_ptoTimerLevel == level)
      return;
    if (debugLog()) ZiLOG(Debug, "Zutcp",
      ([level, bytesInFlight = m_txPkts[level].bytesInFlight()](auto &s) {
	s << "PTO armed level=" << int(level) <<
	  " bytesInFlight=" << bytesInFlight;
      }));
    ++m_txDiag.ptoArmed;
    m_ptoTimerOut = out;
    m_ptoTimerLevel = level;
    ZutcpLOG(app()->qlogTrace(), ([
      kind = RecKind::PTO,
      level,
      reason = RecReason::Armed,
      deadlineUS = qlogUS(out),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo
      ,
		.deadlineUS = deadlineUS,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = level,
	  .reason = RecReason::T(reason)};

      o.logLossTimerUpd(event, time);
    }));
    schedulePTOTimer(out);
    if (m_idleTimeout) scheduleIdleTimer();
  }

  void schedulePTOTimer(ZuTime out) {
    scheduleCxnTimer(
      "PTO", CxnTimer::PTO,
      out, ZmScheduler::Update, &m_ptoTimer);
  }

  void cancelPTO() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC PTO cancel before app initialization", return);
    cancelPTO_();
  }

  void cancelPTO_() {
    if (*m_ptoTimerOut)
      ZutcpLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	level = m_ptoTimerLevel,
	reason = RecReason::Canceled,
		deadlineUS = uint64_t{0},
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = level,
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
    m_ptoTimerOut = ZuTime{};
    app()->mx()->cancel(&m_ptoTimer);
  }

  void startIdleTimer() {
    ZuTime timeout = negotiatedIdleTimeout();
    app()->txRun([link = impl(), timeout]() mutable {
      if (link->disconnecting_()) return;
      link->startIdleTimerTx(timeout);
    });
  }
  void startIdleTimerTx(ZuTime timeout) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC idle timer start outside Tx thread", return);
    m_idleTimeout = timeout;
    m_idleBase = now_();
    m_idleAckElicitingSent = false;
    scheduleIdleTimer();
  }
  ZuTime effectiveIdleTimeout() const {
    if (!m_idleTimeout) return {};
    ZuTime minTimeout = closeDrainDelay();
    return m_idleTimeout < minTimeout ? minTimeout : m_idleTimeout;
  }
  ZuTime idleDeadline_() const {
    ZuTime timeout = effectiveIdleTimeout();
    if (!timeout) return {};
    return m_idleBase + timeout;
  }
  void scheduleIdleTimer() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC idle timer schedule outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx() || !m_idleTimeout) {
      cancelIdleTimer();
      return;
    }
    ZuTime out = idleDeadline_();
    if (!out) {
      cancelIdleTimer();
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_idleTimer && m_idleTimerOut == out) return;
    m_idleTimerOut = out;
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void scheduleIdleTimer(ZuTime out) {
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void cancelIdleTimer() {
    m_idleTimerOut = ZuTime{};
    cancelTimer_(CxnTimer::Idle, &m_idleTimer);
  }
  void startHeartBeatTimer() {
    ZuTime interval = app()->heartBeat();
    app()->txRun([link = impl(), interval]() mutable {
      if (link->disconnecting_()) return;
      link->startHeartBeatTimerTx(interval);
    });
  }
  void startHeartBeatTimerTx(ZuTime interval) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC heartbeat timer start outside Tx thread", return);
    if (!*interval) {
      cancelHBTimer();
      return;
    }
    m_heartBeatBase = now_();
    scheduleHBTimer();
  }
  ZuTime heartBeatDeadline() const {
    ZuTime interval = app()->heartBeat();
    if (!*interval) return {};
    return m_heartBeatBase + interval;
  }
  void scheduleHBTimer() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC heartbeat timer schedule outside Tx thread", return);
    ZuTime interval = app()->heartBeat();
    if (linkClosedTx_() || !establishedTx() || !*interval) {
      cancelHBTimer();
      return;
    }
    if (!*m_heartBeatBase) m_heartBeatBase = now_();
    ZuTime out = heartBeatDeadline();
    if (!*out) {
      cancelHBTimer();
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_heartBeatTimer && m_heartBeatTimerOut == out) return;
    m_heartBeatTimerOut = out;
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_heartBeatTimer);
  }
  void scheduleHBTimer(ZuTime out) {
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_heartBeatTimer);
  }
  void cancelHBTimer() {
    m_heartBeatTimerOut = ZuTime{};
    cancelTimer_(CxnTimer::HeartBeat, &m_heartBeatTimer);
  }
  void noteHBTx() {
    if (!*app()->heartBeat()) return;
    m_heartBeatBase = now_();
    scheduleHBTimer();
  }
  void notePeerPkt() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->notePeerPktTx_();
    });
  }
  void notePeerPktTx_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC peer activity outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx()) return;
    if (m_idleTimeout) {
      m_idleBase = now_();
      m_idleAckElicitingSent = false;
      scheduleIdleTimer();
    }
    noteHBTx();
  }
  void noteAckElicitTx() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ack-eliciting send activity outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx()) return;
    if (m_idleTimeout) {
      if (!m_idleAckElicitingSent) {
	m_idleBase = now_();
	m_idleAckElicitingSent = true;
      }
      scheduleIdleTimer();
    }
    noteHBTx();
  }

  void scheduleCloseTimer(ZuTime out) {
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    m_closeTimerOut = out;
    scheduleCxnTimer(
      "close", CxnTimer::Close,
      out, ZmScheduler::Update, &m_closeTimer);
  }
  void cancelCloseTimer() {
    m_closeTimerOut = ZuTime{};
    cancelTimer_(CxnTimer::Close, &m_closeTimer);
  }

  ZuTime peerCloseDeadline() const {
    return now_() + closeDrainDelay();
  }
  void enterLocalClosingTx() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC local closing outside Tx thread", return);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelDataRto_();
    cancelPersist_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelPMTUDTimer();
    m_closeNextResponse = {};
    scheduleCloseTimer(now_());
  }
  void enterPeerDraining_0(uint64_t errorCode) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC peer draining outside Rx thread", return);
    closeDataRx();
    m_rxAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = false};
    m_publicCloseError = errorCode;
    drainLinkStateRx();
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting_()) return;
      link->applyLinkSnapshotTx_(snapshot);
      link->peerDrainingTx();
      link->enterPeerDraining_1();
    });
  }
  void enterPeerDraining_1() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC peer draining outside Tx thread", return);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelDataRto_();
    cancelPersist_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelPMTUDTimer();
    clearWritableData();
    for (auto &p : m_txPkts) p.clear();
    if (!*m_closeTimerOut)
      scheduleCloseTimer(peerCloseDeadline());
  }
  void noteClosingPacket(ZiSockAddr addr) {
    app()->txRun([
      link = impl(),
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->sendCloseResponseTx(ZuMv(addr));
    });
  }
  void sendCloseResponseTx(ZiSockAddr addr) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC close response outside Tx thread", return);
    if (!closingTx_() || !addr) return;
    ZuTime now = now_();
    if (*m_closeNextResponse && now < m_closeNextResponse) return;
    m_closeNextResponse = now + RttEstimator::Granularity;
    (void)impl()->sendCloseFrame(ZuMv(addr), true);
  }




  void schedulePMTUDTimer(ZuTime out) {
    scheduleCxnTimer(
      "PMTUD", CxnTimer::PMTUD,
      out, ZmScheduler::Advance, &m_pmtudTimer);
  }
  void cancelPMTUDTimer() { cancelTimer_(CxnTimer::PMTUD, &m_pmtudTimer); }

  void schedulePMTUD_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD schedule outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx() ||
	m_path.probePending() || m_pmtudTimer)
      return;
    if (!m_path.nextProbeSize()) return;
    impl()->queueTxFlush();
  }




  void cancelTimers() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC timer cancel before app initialization", return);
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->cancelTimers_();
      return link;
    });
  }
  void cancelTimers_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC timer cancel outside Tx thread", return);
    // Live-state cleanup only; disconnect/final teardown uses delTimers_().
    cancelTimer_(CxnTimer::Connect, &m_connectTimer);
    cancelTimer_(CxnTimer::User, &m_userTimer);
    cancelTimer_(CxnTimer::CloseDrain, &m_closeDrainTimer);
    cancelTimer_(CxnTimer::FinWait, &m_finWaitTimer);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelDataRto_();
    cancelPersist_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelCloseTimer();
    cancelPMTUDTimer();
  }
  void delTimers() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC timer teardown outside Tx thread", return);
    m_connectTimerOut = ZuTime{};
    m_userTimerOut = ZuTime{};
    m_closeDrainTimerOut = m_finWaitTimerOut = ZuTime{};
    m_ackDelayTimerOut = ZuTime{};
    m_pmtudTimerOut = ZuTime{};
    m_lossTimerOut = ZuTime{};
    m_ptoTimerOut = ZuTime{};
    m_rtoTimerOut = ZuTime{};
    m_persistTimerOut = ZuTime{};
    m_persistInterval = ZuTime{};
    m_idleTimerOut = ZuTime{};
    m_heartBeatTimerOut = ZuTime{};
    m_heartBeatBase = {};
    m_closeTimerOut = ZuTime{};
    m_closeNextResponse = {};
    app()->mx()->del(&m_connectTimer);
    app()->mx()->del(&m_userTimer);
    app()->mx()->del(&m_closeDrainTimer);
    app()->mx()->del(&m_finWaitTimer);
    app()->mx()->del(&m_ackDelayTimer);
    app()->mx()->del(&m_lossTimer);
    app()->mx()->del(&m_ptoTimer);
    app()->mx()->del(&m_rtoTimer);
    app()->mx()->del(&m_persistTimer);
    app()->mx()->del(&m_idleTimer);
    app()->mx()->del(&m_heartBeatTimer);
    app()->mx()->del(&m_closeTimer);
    app()->mx()->del(&m_pmtudTimer);
  }
  bool timersActive() const {
    return m_connectTimer || m_userTimer || m_closeDrainTimer || m_finWaitTimer ||
      m_ackDelayTimer || m_lossTimer || m_ptoTimer ||
      m_rtoTimer || m_persistTimer ||
      m_idleTimer || m_heartBeatTimer || m_closeTimer || m_pmtudTimer;
  }

  void ackDelay_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK delay timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->ackDelayExpired();
  }
  void lossTime_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC loss timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->lossTimeExpired();
  }
  void closeTimeout_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC close timer outside Tx thread", return);
    if (disconnecting_()) return;
    m_closeTimerOut = ZuTime{};
    impl()->closeExpired();
  }
  void idleTimeout_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC idle timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (linkClosedTx_() || !establishedTx() || !m_idleTimeout) return;
    ZuTime out = idleDeadline_();
    ZuTime now = now_();
    if (*out && out > now) {
      scheduleIdleTimer();
      return;
    }
    impl()->idleExpired();
  }
  void heartBeat_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC heartbeat timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (linkClosedTx_() || !establishedTx() || !*app()->heartBeat()) return;
    ZuTime out = heartBeatDeadline();
    ZuTime now = now_();
    if (*out && out > now) {
      scheduleHBTimer();
      return;
    }
    dispatchHBExpired(0);
    if (!m_heartBeatTimer) scheduleHBTimer();
  }

  void pmtudTimeout() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->pmtudExpired_();
  }


  void ackDelayExpired() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK delay expiry outside Tx thread", return);
    m_txAck[PktNumSpace::AppData].due = true;
    impl()->flushTx_();
  }
  void lossTimeExpired() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC loss timer expired outside Tx thread", return);
    if (linkClosedTx_()) return;
    if (!lossPending()) {
      ++m_txDiag.lossCanceled;
      cancelLossTimer();
      schedulePTO_();
      return;
    }
    ++m_txDiag.lossExpired;
    ZuTime now = now_();
    detectLossTx(0, now, {}, false);
  }
  void connectExpired() { }
  void policyExpired(CxnTimer::T) { }
  void closeExpired() { }
  void peerDrainingTx() { }
  void idleExpired() { impl()->closeExpired(); }
  void heartBeatExpired() { }
  template <typename U = Impl,
    decltype(ZuDeclVal<U *>()->heartBeatExpired(), int()) = 0>
  void dispatchHBExpired(int) {
    impl()->heartBeatExpired();
  }
  void dispatchHBExpired(...) { heartBeatExpired(); }


  void pmtudExpired_() {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD expiry outside Tx thread", return);
    if (!m_path.probeExpired()) return;
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Expired,
	      size = m_path.probeSize(),
	      reason = PathReason::Timeout,
	      activeMaxPayload = m_path.activeMaxPayload(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo
	      ,
		.mtu = size ? size : activeMaxPayload,
	.kind = PathKind::PMTUD,
	.action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    schedulePMTUD_();
  }
  void applyPathHint_(PathHint hint) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC path hint outside Tx thread", return);
    if (!hint) return;
    m_path.applyHint(hint);
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Hint,
	      size = m_path.activeMaxPayload(),
	      reason = PathReason::PathHint,
	      activeMaxPayload = m_path.activeMaxPayload(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo
	      ,
		.mtu = size ? size : activeMaxPayload,
	.kind = PathKind::PMTUD,
	.action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    if (!m_path.probePending()) cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeAckd_(unsigned size) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD ACK outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeAckd();
    publishWriteMPS();
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Acked,
	      size,
	      reason = PathReason::Probe,
	      activeMaxPayload = m_path.activeMaxPayload(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo
	      ,
		.mtu = size ? size : activeMaxPayload,
	.kind = PathKind::PMTUD,
	.action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeLost_(unsigned size) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD loss outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeLost();
    publishWriteMPS();
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Lost,
	      size,
	      reason = PathReason::Probe,
	      activeMaxPayload = m_path.activeMaxPayload(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo
	      ,
		.mtu = size ? size : activeMaxPayload,
	.kind = PathKind::PMTUD,
	.action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void persistentCongestion_() {
    m_congestion.persistentCongestion();
    ++m_txDiag.persistentCongestion;
    updateCongestionDiag();
  }

  void discardPktNumSpace_0(PktNumSpace::T level) {
    if (level == PktNumSpace::AppData || m_rxSpaceDiscarded[level]) return;
    m_rxSpaceDiscarded[level] = true;
    m_rxAcks.sent(PktNumSpace::T(level));
    app()->txRun([link = impl(), level]() mutable {
      if (link->disconnecting_()) return;
      link->discardPktNumSpace_1(level);
    });
  }

	  void discardPktNumSpace_1(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC sent-packet discard outside Tx thread", return);
    if (level == PktNumSpace::AppData || m_txSpaceDiscarded[level]) return;
    m_txSpaceDiscarded[level] = true;
    m_congestion.release(m_txPkts[level].bytesInFlight());
    m_txPkts[level].clear();
    m_txAck[level].ranges.clear();
    updateCongestionDiag();
    scheduleLossTimer();
    schedulePTO_();
  }

















  template <typename FlushTx, typename SendProbe>
  void ptoRecovery(
    FlushTx flushTx, SendProbe sendProbe) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PTO recovery outside Tx thread", return);
    notePTOExpired();
    PktNumSpace::T probeLevel = PktNumSpace::Initial;
    if (!ptoLevel(probeLevel)) return;
    // RFC 9002 6.2.4: probe another space with data in flight, within
    // the two-datagram PTO budget. A successful Handshake retransmission
    // must not indefinitely suppress the client's outstanding 1-RTT flight.
    PktNumSpace::T companion = PktNumSpace::N;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto level = PktNumSpace::T(i);
      if (level == probeLevel || m_txSpaceDiscarded[level] ||
	  !m_txPkts[level].ackElicitingInFlight())
	continue;
      companion = level;
      break;
    }
    uint64_t primaryPN = m_txPN[probeLevel];
    uint64_t companionPN = companion < PktNumSpace::N ? m_txPN[companion] : 0;
    uint64_t packetsBefore = txPackets();
    unsigned budget = companion < PktNumSpace::N ? 1 : 2;
    auto finish = [this, &sendProbe, probeLevel, companion,
	primaryPN, companionPN, packetsBefore](bool primarySent) {
      if (primarySent && companion < PktNumSpace::N &&
	  m_txPN[probeLevel] != primaryPN &&
	  m_txPN[companion] == companionPN && txPackets() - packetsBefore < 2) {
	if (sendProbe(companion)) notePTOProbe(companion, 1);
      }
      schedulePTO_();
    };
    unsigned probeCount = 0;
    bool probe = reclaimPTO(probeLevel, probeCount, budget);
    if (impl()->retransmit_(true, budget, probeLevel)) {
      notePTORetx();
      finish(true);
      return;
    }
    if (!probe) {
      uint64_t packetsTx = txPackets();
      if (flushTx()) {
	notePTOFlush();
	if (txPackets() != packetsTx) finish(false);
      }
      return;
    }
    notePTOProbe(probeLevel, probeCount);
    bool sent = sendProbe(probeLevel);
    finish(sent);
  }

  bool persistNeeded_() const {
    return establishedTx() && !linkClosedTx_() && !m_resetSent &&
      (app()->nativeTx() ? m_peerWindowZero &&
	(txRangeCount() || m_txAckd < m_txNext || (m_fin && !m_finDequeued)) :
	txRangeCount() && m_txDataCredit.blocked());
  }
  void cancelPersist_() {
    m_persistInterval = ZuTime{};
    cancelTimer_(CxnTimer::Persist, &m_persistTimer);
  }
  void updatePersist_() {
    if (!persistNeeded_()) {
      if (*m_persistTimerOut || *m_persistInterval) cancelPersist_();
      return;
    }
    if (*m_persistTimerOut) return;
    noteFlowBlocked();
    m_persistInterval = m_dataRto.timeout(m_rtt, app()->rtoParams());
    if (m_persistInterval < ZuTime{1}) m_persistInterval = ZuTime{1};
    if (m_persistInterval > ZuTime{60}) m_persistInterval = ZuTime{60};
    m_persistTimerOut = now_() + m_persistInterval;
    scheduleCxnTimer("persist", CxnTimer::Persist,
      m_persistTimerOut, ZmScheduler::Update, &m_persistTimer);
  }
  void persistExpired_() {
    if (!persistNeeded_()) { cancelPersist_(); return; }
    PktBuild build;
    int n = app()->nativeTx() ? FrameCodec::writePing(build.scratch(), build.scratchAvail()) :
      FrameCodec::writeDataBlocked(build.scratch(), build.scratchAvail(), m_txDataCredit.limit());
    if (n > 0 && build.commitScratch(unsigned(n)))
      impl()->sendPersistProbe(build);
    if (!persistNeeded_()) { cancelPersist_(); return; }
    // A responsive zero window is not loss evidence. This schedule has no
    // packet-history entry, congestion response or relationship to DATA RTO.
    m_persistInterval += m_persistInterval;
    if (m_persistInterval > ZuTime{60}) m_persistInterval = ZuTime{60};
    m_persistTimerOut = now_() + m_persistInterval;
    scheduleCxnTimer("persist", CxnTimer::Persist,
      m_persistTimerOut, ZmScheduler::Update, &m_persistTimer);
  }

  bool userPending_() const {
    return establishedTx() && !linkClosedTx_() && !m_resetSent &&
      (m_txAckd < m_txNext || m_txAckd < m_txBytes || (m_fin && m_txAckd == m_txBytes));
  }
  void updateUserTimeout_(ZuTime progress = {}) {
    auto timeout = app()->userTimeout();
    if (timeout == ZuTime{0} || !userPending_()) {
      if (*m_userTimerOut) cancelTimer_(CxnTimer::User, &m_userTimer);
      return;
    }
    if (*m_userTimerOut && !*progress) return;
    auto out = (*progress ? progress : now_()) + timeout;
    scheduleCxnTimer("user", CxnTimer::User, out, ZmScheduler::Update, &m_userTimer);
  }
  void userTimeout_() {
    if (app()->userTimeout() == ZuTime{0} || !userPending_()) return;
    expirePolicy_(CxnTimer::User);
  }

  void expirePolicy_(CxnTimer::T action) {
    if (app()->nativeTx()) {
      closeTxAdmission();
      impl()->sendTCPReset_();
    }
    closeStateTx();
    impl()->policyExpired(action);
  }
  void updateCloseTimers_() {
    if (!establishedTx() || linkClosedTx_()) return;
    if (m_fin && m_tcpCloseState == TcpCloseState::Open)
      m_tcpCloseState = TcpCloseState::FinWait1;
    else if (m_fin && m_tcpCloseState == TcpCloseState::CloseWait)
      m_tcpCloseState = TcpCloseState::LastAck;
    if (m_finAckd) {
      if (m_tcpCloseState == TcpCloseState::FinWait1)
	m_tcpCloseState = TcpCloseState::FinWait2;
      else if (m_tcpCloseState == TcpCloseState::Closing)
	m_tcpCloseState = TcpCloseState::TimeWait;
      else if (m_tcpCloseState == TcpCloseState::LastAck)
	m_tcpCloseState = TcpCloseState::Closed;
    }
    bool drain = m_tcpCloseState == TcpCloseState::FinWait1 ||
      m_tcpCloseState == TcpCloseState::Closing || m_tcpCloseState == TcpCloseState::LastAck;
    if (drain) {
      if (!*m_closeDrainTimerOut)
	scheduleCxnTimer("close drain", CxnTimer::CloseDrain,
	  now_() + app()->closeDrainTimeout(), ZmScheduler::Update, &m_closeDrainTimer);
    } else if (*m_closeDrainTimerOut)
      cancelTimer_(CxnTimer::CloseDrain, &m_closeDrainTimer);
    if (m_tcpCloseState == TcpCloseState::FinWait2) {
      if (!*m_finWaitTimerOut)
	scheduleCxnTimer("FIN wait", CxnTimer::FinWait,
	  now_() + app()->finWaitTimeout(), ZmScheduler::Update, &m_finWaitTimer);
    } else if (*m_finWaitTimerOut)
      cancelTimer_(CxnTimer::FinWait, &m_finWaitTimer);
  }
  void peerFinTx_(uint64_t gen) {
    if (gen != m_txDataGen || disconnecting_() || linkClosedTx_()) return;
    switch (m_tcpCloseState) {
      case TcpCloseState::Open: m_tcpCloseState = TcpCloseState::CloseWait; break;
      case TcpCloseState::FinWait1: m_tcpCloseState = TcpCloseState::Closing; break;
      case TcpCloseState::FinWait2: m_tcpCloseState = TcpCloseState::TimeWait; break;
      default: break;
    }
    updateCloseTimers_();
  }

  void cancelDataRto_() {
    m_rtoTimerOut = ZuTime{};
    cancelTimer_(CxnTimer::RTO, &m_rtoTimer);
  }

  void scheduleDataRto_() {
    if (linkClosedTx_()) return;
    ZuTime out = m_dataRto.deadline();
    if (!*out || m_txAckd == m_txNext || (app()->nativeTx() && m_peerWindowZero)) {
      cancelDataRto_();
      return;
    }
    if (!establishedTx()) return;
    if (m_rtoTimer && m_rtoTimerOut == out) return;
    m_rtoTimerOut = out;
    scheduleCxnTimer("DATA RTO", CxnTimer::RTO,
      out, ZmScheduler::Update, &m_rtoTimer);
  }

  void dataRtoExpired_() {
    if (linkClosedTx_() || m_txAckd == m_txNext ||
	(app()->nativeTx() && m_peerWindowZero) ||
	!m_dataRto.expire(now_(), m_rtt, app()->rtoParams())) return;
    m_congestion.timeout(uint64_t(now_().microsecs()), m_txAckd < m_txRtoEnd);
    dataRtoRecovery(m_txAckd, m_txRuntimeGen, {});
  }

  void dataRtoRecovery(uint64_t next, uint64_t gen, PktAckBatch batch) {
    if (gen != m_txRuntimeGen || next != m_txAckd || disconnecting_()) return;
    bool done = reclaimDataRtoBatch(next, batch);
    if (gen != m_txRuntimeGen || disconnecting_()) return;
    if (!done) {
      app()->txRun([link = impl(), next, gen, batch]() mutable {
	link->dataRtoRecovery(next, gen, batch);
      });
      return;
    }
    updateCongestionDiag();
    (void)impl()->retransmit_(true, 1, PktNumSpace::AppData);
    scheduleDataRto_();
  }

  bool reclaimDataRtoBatch(uint64_t next, PktAckBatch &batch) {
    PktTxUpdate update;
    bool done = m_txPkts[PktNumSpace::AppData].reclaimBytesBatch(
      next, batch, RecoveryScanBatch, update);
    for (unsigned i = 0, n = update.lostFrames.length(); i < n; ++i) {
      const auto &ref = update.lostFrames[i].ref;
      if (ref.kind != SentFrameKind::Data) continue;
      uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
      if (end > m_txRtoEnd) m_txRtoEnd = end;
    }
    applyLossUpdateTx(update);
    return done;
  }

  bool reclaimPTO(PktNumSpace::T &level, unsigned &probes, unsigned limit = 2) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PTO reclaim outside Tx thread", return false);
    probes = 0;
    if (!ptoLevel(level)) return false;
    unsigned n = m_txPkts[level].reclaimOnPTO(limit);
    bool fallbackProbe = !n && fallbackProbeLevel(level);
    if (fallbackProbe) {
      ZuTime now = now_();
      ZuTime &probeOut = fallbackProbeOut(level);
      if (*probeOut && now < probeOut) {
	schedulePTO_();
	return false;
      }
      probeOut = now + fallbackProbeDelay();
      n = 1;
    }
    if (n) {
      probes = n;
      ++m_txDiag.ptoCount;
      m_ptoBackoff.expired();
      ZutcpLOG(app()->qlogTrace(), ([
	level,
	probes = n,
		backoff = m_ptoBackoff.count(),
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .value = backoff,
	  .bytes = probes,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::PTO,
	  .packetSpace = level,
	  .reason = RecReason::Backoff};
	o.logLossTimerUpd(event, time);
      }));
      if (debugLog()) ZiLOG(Debug, "Zutcp", ([level, n](auto &s) {
	  s << "PTO fired level=" << int(level) << " probes=" << n;
	}));
    }
    return n;
  }
  bool reclaimPTO() {
    PktNumSpace::T level = PktNumSpace::Initial;
    unsigned probes = 0;
    return reclaimPTO(level, probes);
  }
  void notePTOExpired() {
    ++m_txDiag.ptoExpired;
    ZutcpLOG(app()->qlogTrace(), ([
      level = [this]() -> PktNumSpace::T {
	PktNumSpace::T level_ = PktNumSpace::Initial;
	return ptoLevel(level_) ? level_ : PktNumSpace::N;
      }(),
      backoff = m_ptoBackoff.count(),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo
      ,
		.value = backoff,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
	.kind = RecKind::PTO,
	.packetSpace = level,
	.reason = RecReason::Expired};
      o.logLossTimerUpd(event, time);
    }));
  }
  void notePTOFlush() { ++m_txDiag.ptoFlush; }
  void notePTORetx() { ++m_txDiag.ptoRetx; }
  void notePTOProbe(PktNumSpace::T level, unsigned probes) {
    ++m_txDiag.ptoProbe;
    ZutcpLOG(app()->qlogTrace(), ([
      level,
      probes,
      backoff = m_ptoBackoff.count(),
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo
      ,
	.value = backoff,
		.bytes = probes,
		.cwnd = cwnd,
		.ssthresh = ssthresh,
		.bytesInFlight = bytesInFlight,
	.kind = RecKind::PTO,
	.packetSpace = level,
	.reason = RecReason::Probe};
      o.logLossTimerUpd(event, time);
    }));
  }
  uint64_t txPackets() const { return m_txDiag.packetsTx.load_(); }

  bool nextRetransmit_(
    PktNumSpace::T &level, SentFrameRef &ref,
    PktNumSpace::T onlySpace = PktNumSpace::N) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC retransmit selection outside Tx thread", return false);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      PktNumSpace::T l = PktNumSpace::T(i);
      if (onlySpace < PktNumSpace::N && l != onlySpace) continue;
      if (m_txSpaceDiscarded[l]) continue;
      do {
	if (!m_txPkts[l].nextRetransmit(ref)) goto nextSpace;
	SentFrameRef tail;
	if (!clipRetransmit(l, ref, tail)) continue;
	if (tail) (void)requeueRetransmit_(l, tail);
	break;
      } while (true);
      level = l;
      ++m_txDiag.retransmittedFrames;
      return true;
nextSpace:
      continue;
    }
    return false;
  }



  bool requeueRetransmit_(PktNumSpace::T level, const SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zutcp",
      (), "QUIC retransmit requeue outside Tx thread", return false);
    if (m_txSpaceDiscarded[level]) return false;
    if (!frameStillUnackd(level, ref)) return false;
    return m_txPkts[level].requeueRetransmit(ref);
  }

  bool buildRetxData(PktBuild &build, SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Data) return false;
    build.reset();
    bool direct = ref.range.storage && ref.range.storage->pool;
    bool ackBuilt = appendPendingAck(PktNumSpace::AppData, build);
    if (direct && (!ackBuilt || build.bytes() > TxDataPktReserve - DataPktizer::PrefixMax))
      build.reset();
    else if (!ackBuilt) return false;
    // ACK ranges and path MTU can change while this plaintext is retained.
    // Split the recovery reference without consuming flow credit again.
    int header = Pkt::shortHdrLen(
      cid_(CIDSel::Peer), txPNLength(PktNumSpace::AppData));
    if (header < 0) return false;
    unsigned overhead = unsigned(header);
    unsigned maxPayload = m_path.activeMaxPayload();
    if (overhead >= maxPayload || build.bytes() >= maxPayload - overhead) return false;
    unsigned available = maxPayload - overhead - build.bytes();
    int n = FrameCodec::writeDataPrefix(
      build.scratch(), build.scratchAvail(), ref.offset,
      ref.length, ref.fin);
    if (n <= 0 || unsigned(n) > available) return false;
    SentFrameRef head, tail;
    auto selected = &ref;
    if (ref.length > available - unsigned(n)) {
      if (direct) return false;
      unsigned length = available - unsigned(n);
      if (!length) return false;
      uint64_t split = ref.offset + length;
      head = ref;
      tail = ref;
      if (!clipDataRef(head, ref.offset, split) ||
	  !clipDataRef(tail, split, ref.offset + ref.length + (ref.fin ? 1 : 0)))
	return false;
      selected = &head;
      n = FrameCodec::writeDataPrefix(
	build.scratch(), build.scratchAvail(), head.offset,
	head.length, head.fin);
    }
    if (n <= 0 || !build.commitScratch(unsigned(n)) || !build.add(selected->range))
      return false;
    if (tail) {
      if (!requeueRetransmit_(PktNumSpace::AppData, tail)) return false;
      ref = ZuMv(head);
    }
    return true;
  }

  bool buildRetxCtl(PktBuild &build, const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Control ||
	ref.controlType == FrameType::Unknown)
      return false;
    ControlFrame frame = controlFrame_(ref);
    if (!controlStillValid_(frame)) return false;
    build.reset();
    if (!appendPendingAck(PktNumSpace::AppData, build)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    return true;
  }

  bool buildPingProbe(PktNumSpace::T level, PktBuild &build) {
    build.reset();
    if (!appendPendingAck(level, build)) return false;
    int n = FrameCodec::writePing(build.scratch(), build.scratchAvail());
    return n > 0 && build.commitScratch(unsigned(n));
  }

  bool buildPingProbe(PktBuild &build) {
    return buildPingProbe(PktNumSpace::AppData, build);
  }

  template <typename SendProbe>
  bool sendPMTUDProbe(ZiSockAddr addr, SendProbe sendProbe) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC PMTUD probe send outside Tx thread", return false);
    if (!establishedTx() || m_path.probePending()) return false;
    unsigned size = m_path.nextProbeSize();
    if (!size || !m_path.canSendProbe(size) || !m_congestion.canSend(size)) {
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
		size,
		reason = PathReason::Admission,
		activeMaxPayload = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .mtu = size ? size : activeMaxPayload,
	  .kind = PathKind::PMTUD,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPMTUDUpdated(event, time);
      }));
      return false;
    }
    m_path.startProbe(size);
    ZutcpLOG(app()->qlogTrace(), ([
      action = PathAction::Sent,
      size,
      reason = PathReason::Probe,
      activeMaxPayload = m_path.activeMaxPayload(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo
      ,
		.mtu = size ? size : activeMaxPayload,
	.kind = PathKind::PMTUD,
	.action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    PktBuild build;
    if (!buildPingProbe(build) || !sendProbe(build, ZuMv(addr), size)) {
      m_path.probeLost();
    publishWriteMPS();
      ZutcpLOG(app()->qlogTrace(), ([
	action = PathAction::Lost,
		size,
		reason = PathReason::SendFail,
		activeMaxPayload = m_path.activeMaxPayload(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo
		,
	  .mtu = size ? size : activeMaxPayload,
	  .kind = PathKind::PMTUD,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPMTUDUpdated(event, time);
      }));
      return false;
    }
    schedulePMTUDTimer(now_() + ptoTimeout());
    return true;
  }



  bool rxPktSeen(PktNumSpace::T level, uint64_t pn) const {
    return m_rxAcks.tracker(level).contains(pn);
  }

  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan frame,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT, PacketLease **backing = nullptr) {
    SentFrameRef ref;
    bool ackEliciting = false;
    if (!sentFrameRef(frame, ref, ackEliciting)) return true;
    return recordTxPkt(
      level, pn, bytes, ref, ackEliciting, false, 0, ackLevel, ackLargest,
      packetType, ecn, backing);
  }

  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const SentFrameRef &ref, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT, PacketLease **backing = nullptr) {
    TxPktRefs refs;
    if (ref.kind != SentFrameKind::None) refs.add(ref);
    return recordTxPkt(
      level, pn, bytes, refs, ackEliciting, pmtudProbe, pmtudSize,
      ackLevel, ackLargest, packetType, ecn, backing);
  }

  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const TxPktRefs &refs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT, PacketLease **backing = nullptr) {
    if (m_txSpaceDiscarded[level]) return true;
    if (!ackEliciting && !refs.count()) return true;
    SentPkt packet;
    packet.pn = pn;
    packet.space = level;
    packet.packetType = packetType == PktType::N ?
      pktTypeFromSpace(level) : packetType;
    packet.bytes = bytes;
    packet.ecn = ecn;
    packet.sentTime = now_();
    packet.ackEliciting = ackEliciting;
    packet.inFlight = ackEliciting;
    packet.pmtudProbe = pmtudProbe;
    packet.pmtudSize = pmtudSize;
    packet.ackLevel = ackLevel;
    packet.ackLargest = ackLargest;
    for (unsigned i = 0, n = refs.count(); i < n; ++i)
      packet.addFrame(refs[i]);
    if (!m_txPkts[level].add(packet, backing)) return false;
    if (ackEliciting) m_path.sentECN(level, ecn);
    recordTxUnackd_(level, refs);
    if (ackEliciting) {
      m_congestion.sent(bytes);
      updateCongestionDiag();
      schedulePTO_();
      scheduleLossTimer();
    }
    return true;
  }

  uint64_t txNext() const { return m_txNext; }
  uint64_t txAckd() const { return m_txAckd; }
  void acceptTxData(const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Data || (!ref.length && !ref.fin)) return;
    uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
    bool retransmission = ref.offset < m_txNext;
    if (retransmission && end > m_txRttFence) m_txRttFence = end;
    if (end > m_txNext) m_txNext = end;
    if (m_txAckd < m_txNext) {
      m_dataRto.sent(now_(), m_rtt, app()->rtoParams(), retransmission);
      scheduleDataRto_();
    }
    updateUserTimeout_();
  }
  void acceptTxData(const TxPktRefs &refs) {
    for (unsigned i = 0, n = refs.count(); i < n; ++i) acceptTxData(refs[i]);
  }

  void processAckFrame(PktNumSpace::T level, const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC ACK receive processing outside Rx thread", return);
    if (!frame.ackRanges) return;
    AckSnapshot ack;
    ack.level = level;
    ack.delay = frame.value;
    ack.byteNext = app()->nativeTx() ? 0 : frame.byteNext;
    ack.ecn = frame.ackECN;
    ack.ranges = frame.ackRanges;
    app()->txRun([link = impl(), ack]() mutable {
      if (link->disconnecting_()) return;
      link->processAckFrameTx_(ack);
    });
  }

  void applyECNAckTx(
    const AckSnapshot *ack, const PktTxUpdate &update, bool *ecnValid,
    uint64_t *ceDelta) {
    if (!ack || !update.ecnAckdPackets || m_path.ecnDisabled()) return;
    if (!ack->ecn.any()) {
      if (m_path.ecnProbeExpired() && ecnValid && *ecnValid) {
#ifdef Zutcp_DEBUG
	auto oldState = m_path.ecnState();
#endif
	m_path.ecnValidationFailed();
	++m_txDiag.ecnValidationFailures;
	*ecnValid = false;
	ZutcpLOG(app()->qlogTrace(), ([
	  level = ack->level,
	  oldState = qlogECNState(oldState),
	  state = ECNState::Failed,
	  linkInfo = linkInfo_()
	](auto &o, ZuTime time) {
	  ECNEvt event{
	    .linkInfo = linkInfo,
	    .packetSpace = level,
	    .oldState = ECNState::T(oldState),
	    .state = ECNState::T(state),
	    .reason = ECNReason::NoAckECN,
	    .disabled = true};

	  o.logECNStateUpd(event, time);
	}));
      }
      return;
    }
#ifdef Zutcp_DEBUG
    auto oldState = m_path.ecnState();
#endif
    m_path.ackdECN(update.ecnAckdPackets);
#ifdef Zutcp_DEBUG
    auto newState = m_path.ecnState();
    if (oldState != newState) {
      ZutcpLOG(app()->qlogTrace(), ([
	level = ack->level,
	oldState = qlogECNState(oldState),
	newState = qlogECNState(newState),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	ECNEvt event{
	  .linkInfo = linkInfo,
	  .packetSpace = level,
	  .oldState = ECNState::T(oldState),
	  .state = ECNState::T(newState),
	  .reason = ECNReason::AckECN,
	  .disabled = false};

	o.logECNStateUpd(event, time);
      }));
    }
#endif
    if (!ceDelta || !*ceDelta) return;
    m_path.ceECN(update.ecnAckdBytes);
    if (m_congestion.congestionEventAt(
	uint64_t(update.ecnAckdSentTime.microsecs()),
	uint64_t(now_().microsecs()))) {
      *ceDelta = 0;
      ZutcpLOG(app()->qlogTrace(), ([
	reason = RecReason::ECNCE,
	cwnd = m_congestion.cwnd(),
	ssthresh = m_congestion.ssthresh(),
	bytesInFlight = m_congestion.bytesInFlight(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason)};

	o.logCongStateUpd(event, time);
      }));
    }
  }
  void initialResponseAckdTx(uint64_t) { }

  void applyAckUpdateTx(
    const PktTxUpdate &update, TxAckWork &work,
    const AckSnapshot *ack = nullptr, bool *ecnValid = nullptr,
    uint64_t *ceDelta = nullptr) {
    uint64_t workEpoch = m_congestion.epoch();
    work.initialResponseAckd |= update.initialResponseAckd;
    applyAckOfAckTx(update);
    for (unsigned i = 0, n = update.ackdFrames.length(); i < n; ++i) {
      if (work.gen != m_txRuntimeGen) return;
      (void)ackTxFrame(update.level, update.ackdFrames[i].ref);
    }
    if (work.gen != m_txRuntimeGen) return;

    applyECNAckTx(ack, update, ecnValid, ceDelta);
    work.growth.stage(m_congestion, workEpoch, update.eligibleAckdBytes);
    if (update.normalAckdBytes) {
      m_congestion.release(update.normalAckdBytes);
      work.congestionOpened = true;
      ZutcpLOG(app()->qlogTrace(), ([
      reason = RecReason::Ack,
      cwnd = m_congestion.cwnd(),
      ssthresh = m_congestion.ssthresh(),
      bytesInFlight = m_congestion.bytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason)};

	o.logCongStateUpd(event, time);
      }));
    }
    if (update.pmtudAckdBytes) {
      m_congestion.release(update.pmtudAckdBytes);
      work.congestionOpened = true;
      onPMTUDProbeAckd_(update.pmtudAckdSize);
      ZutcpLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDAck,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason)};

	o.logCongStateUpd(event, time);
      }));
    }
  }
  void applyAckOfAckTx(const PktTxUpdate &update) {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
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
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC ACK-of-ACK trim outside Rx thread", return);
    m_rxAcks.tracker(level).ackdByPeer(largest);
  }
  void applyLossUpdateTx(const PktTxUpdate &update) {
    if (update.normalLostBytes) {
      m_congestion.lostAt(
	update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()),
	uint64_t(now_().microsecs()));
      ZutcpLOG(app()->qlogTrace(), ([
		reason = RecReason::Loss,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason)};

	o.logCongStateUpd(event, time);
      }));
    }
    if (update.pmtudLostBytes) {
      m_congestion.lostAt(
	update.pmtudLostBytes,
	uint64_t(update.pmtudLostSentTime.microsecs()),
	uint64_t(now_().microsecs()), true);
      onPMTUDProbeLost_(update.pmtudLostSize);
      ZutcpLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDLoss,
		cwnd = m_congestion.cwnd(),
		ssthresh = m_congestion.ssthresh(),
		bytesInFlight = m_congestion.bytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::NewReno,
	  .packetSpace = PktNumSpace::AppData,
	  .reason = RecReason::T(reason)};

	o.logCongStateUpd(event, time);
      }));
    }
  }

  void processAckFrameTx_(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    TxAckWork work;
    work.ack = ack;
    work.gen = m_txRuntimeGen;
    processAckFrameTx_(ZuMv(work));
  }
  void processAckFrameTx_(TxAckWork work) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    AckSnapshot &ack = work.ack;
    if (work.gen != m_txRuntimeGen) return;
    if ((!ack.ranges && ack.level != PktNumSpace::AppData) ||
	m_txSpaceDiscarded[ack.level]) return;
    if (!ackFrameValidTx(ack)) {
      ++m_txDiag.failures;
      return;
    }
    if (!work.ecnValidated) {
      work.ackTime = now_();
      work.previousByteAck = m_txAckd;
      work.byteProgress = ack.level == PktNumSpace::AppData && ack.byteNext > m_txAckd;
      if (work.byteProgress) {
	m_txAckd = ack.byteNext;
	updateUserTimeout_(work.ackTime);
	m_dataRto.ack(work.ackTime, m_txAckd < m_txNext, m_rtt, app()->rtoParams());
	work.rtoDeadline = m_dataRto.deadline();
	scheduleDataRto_();
      }
      work.ecnValid = validateAckECN(ack, &work.ceDelta);
      for (unsigned i = 0, n = ack.ranges.length(); i < n; ++i) {
	uint64_t largest = ack.ranges[i].largest;
	if (ZuNull(m_txLargestAckd[ack.level]) ||
	    largest > m_txLargestAckd[ack.level])
	  m_txLargestAckd[ack.level] = largest;
      }
      work.ecnValidated = true;
    }
    if (ack.level == PktNumSpace::AppData && !work.bytePhase) {
      PktTxUpdate bytes;
      bool done = m_txPkts[ack.level].ackBytesBatch(ack.byteNext, work.byteBatch,
	RecoveryScanBatch, &bytes, m_congestion.recoveryStartTime());
      applyAckUpdateTx(bytes, work, &ack, &work.ecnValid, &work.ceDelta);
      if (work.gen != m_txRuntimeGen || disconnecting_()) return;
      if (!done) {
	app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
	  if (!link->disconnecting_()) link->processAckFrameTx_(ZuMv(work));
	});
	return;
      }
      work.bytePhase = true;
    }
	    PktTxUpdate update;
	    if (!work.lossPhase) {
	      if (!m_txPkts[ack.level].ackBatch(
		  ack.ranges.data(), ack.ranges.length(),
		  work.ackBatch, RecoveryScanBatch,
		  ack.level, &update, m_congestion.recoveryStartTime(),
		  ack.level != PktNumSpace::AppData)) {
		applyAckUpdateTx(
		  update, work,
		  &ack, &work.ecnValid, &work.ceDelta);
		ZutcpLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  largestAcked = ack.ranges ?
	    ack.ranges[ack.ranges.length() - 1].largest : 0,
	  ackDelayUS = ack.level == PktNumSpace::AppData ?
    qlogUS(ackDelay_(ack.delay)) : 0,
	  ackedBytes = update.ackdBytes,
	  lostBytes = update.lostBytes,
	  nRanges = ack.ranges.length(),
	  nAckdFrames = update.ackdFrames.length(),
	  nLostFrames = update.lostFrames.length(),
	  packetNumbers = qlogAckedPNs(update),
	  packetNumbersTruncated = update.ackedPNsTruncated,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) mutable {
	  if (!packetNumbers) return;
	  AckEvt event{
	    .packetNumbers = ZuMv(packetNumbers),
	    .linkInfo = linkInfo
	  ,
    .largestAcked = largestAcked,
    .ackDelayUS = ackDelayUS,
    .ackedBytes = ackedBytes,
    .lostBytes = lostBytes,
    .rangeCount = qlogCount(nRanges),
    .ackedFrames = qlogCount(nAckdFrames),
	    .lostFrames = qlogCount(nLostFrames),
	    .packetNumbersTruncated = packetNumbersTruncated};
		  event.packetSpace = level;
		  o.logPktsAcked(event, time);
		}));
		app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
		  if (link->disconnecting_()) return;
		  link->processAckFrameTx_(ZuMv(work));
		});
		return;
	      }
	      applyAckUpdateTx(
		update, work,
		&ack, &work.ecnValid, &work.ceDelta);
	      ZutcpLOG(app()->qlogTrace(), ([
	level = ack.level,
	largestAcked = ack.ranges ?
	  ack.ranges[ack.ranges.length() - 1].largest : 0,
	ackDelayUS = ack.level == PktNumSpace::AppData ?
	  qlogUS(ackDelay_(ack.delay)) : 0,
	ackedBytes = update.ackdBytes,
	lostBytes = update.lostBytes,
	nRanges = ack.ranges.length(),
	nAckdFrames = update.ackdFrames.length(),
		nLostFrames = update.lostFrames.length(),
		packetNumbers = qlogAckedPNs(update),
		packetNumbersTruncated = update.ackedPNsTruncated,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) mutable {
	if (!packetNumbers) return;
	AckEvt event{
	  .packetNumbers = ZuMv(packetNumbers),
	  .linkInfo = linkInfo
		,
	  .largestAcked = largestAcked,
	  .ackDelayUS = ackDelayUS,
	  .ackedBytes = ackedBytes,
	  .lostBytes = lostBytes,
	  .rangeCount = qlogCount(nRanges),
	  .ackedFrames = qlogCount(nAckdFrames),
	  .lostFrames = qlogCount(nLostFrames),
	  .packetNumbersTruncated = packetNumbersTruncated};
		event.packetSpace = level;
		o.logPktsAcked(event, time);
	      }));
	      work.lossPhase = true;
	    } else {
	      applyAckUpdateTx(
		update, work,
		&ack, &work.ecnValid, &work.ceDelta);
	    }
    if (work.gen != m_txRuntimeGen || disconnecting_()) return;
    if (work.ackBatch.haveAckForLoss) {
      PktTxUpdate lossUpdate;
      if (!m_txPkts[ack.level].markPktThreshLossBatch(
	  work.ackBatch.largestAckdForLoss, 3, work.lossBatch,
	  RecoveryScanBatch, &lossUpdate)) {
	applyLossUpdateTx(lossUpdate);
	ZutcpLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  lostBytes = lossUpdate.lostBytes,
	  bytesInFlight = m_congestion.bytesInFlight(),
	  lostFrames =
	    QLogTxFrameRefs{lossUpdate.lostFrames},
	  reason = RecReason::PacketThreshold,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  if (!lostBytes) return;
	  RecLogEvt event{
	    .linkInfo = linkInfo
	  ,
	    .bytes = lostBytes,
	    .bytesInFlight = bytesInFlight,
    .kind = RecKind::Aggregate,
    .packetSpace = level,
	    .reason = RecReason::T(reason),
	    .frameCount = qlogCount(lostFrames.count())};
	  for (unsigned i = 0, n = lostFrames.count(); i < n; ++i)
    qlogAddTxFrame(event, lostFrames[i]);

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
      applyLossUpdateTx(lossUpdate);
      ZutcpLOG(app()->qlogTrace(), ([
	level = ack.level,
	lostBytes = lossUpdate.lostBytes,
		bytesInFlight = m_congestion.bytesInFlight(),
		lostFrames =
		  QLogTxFrameRefs{lossUpdate.lostFrames},
		reason = RecReason::PacketThreshold,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	if (!lostBytes) return;
	RecLogEvt event{
	  .linkInfo = linkInfo
		,
	  .bytes = lostBytes,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::Aggregate,
	  .packetSpace = level,
	  .reason = RecReason::T(reason),
	  .frameCount = qlogCount(lostFrames.count())};
	for (unsigned i = 0, n = lostFrames.count(); i < n; ++i)
	  qlogAddTxFrame(event, lostFrames[i]);

	o.logPktLost(event, time);
	o.logRecPktLost(event, time);
	if (event.frames)
	  o.logMarkRetrans(event, time);
      }));
      if (lossUpdate.lostBytes) work.retransmit = true;
    }
    if (work.lossBatch.lost) {
      if (m_txPkts[ack.level].persistentCongestion(
	  persistentCongestionThresh(), RecoveryScanBatch))
	persistentCongestion_();
    }
    bool dataFlight = ack.level == PktNumSpace::AppData &&
      (work.byteProgress || m_txAckd < m_txNext);
    const auto &sample = dataFlight ? work.byteBatch : work.ackBatch;
    if (sample.ackd || work.byteProgress) {
      ZuTime now = work.ackTime;
      bool unambiguous = ack.level != PktNumSpace::AppData ||
	work.previousByteAck >= m_txRttFence;
      if (unambiguous && *sample.latestSentTime && sample.latestSentTime < now) {
	m_rtt.sample(now - sample.latestSentTime, ZuTime{0}, false);
	ZutcpLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  latestRTTUS = qlogUS(m_rtt.latest()),
	  smoothedRTTUS = qlogUS(m_rtt.smoothed()),
	  rttVarianceUS = qlogUS(m_rtt.variance()),
	  minRTTUS = qlogUS(m_rtt.min()),
	  cwnd = m_congestion.cwnd(),
	  ssthresh = m_congestion.ssthresh(),
	  bytesInFlight = m_congestion.bytesInFlight(),
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  RecLogEvt event{
	    .linkInfo = linkInfo
	  ,
    .latestRTTUS = latestRTTUS,
    .smoothedRTTUS = smoothedRTTUS,
    .rttVarianceUS = rttVarianceUS,
	    .minRTTUS = minRTTUS,
	    .cwnd = cwnd,
	    .ssthresh = ssthresh,
	    .bytesInFlight = bytesInFlight,
    .kind = RecKind::RTT,
    .packetSpace = level};

	  o.logMetricsUpd(event, time);
	}));
      }
      m_ptoBackoff.reset();
    }
    // Commit only after this ACK's ECN and loss responses, including all
    // bounded continuations. Reset/discard return above without growth.
    work.growth.commit(m_congestion);
    if (work.byteProgress && ack.byteNext == m_txAckd &&
	m_dataRto.deadline() == work.rtoDeadline) {
      // Refresh the estimate at this ACK's receipt time only while its restart
      // is still current. New ACKs, retransmissions and expiry take precedence.
      m_dataRto.ack(work.ackTime, m_txAckd < m_txNext, m_rtt, app()->rtoParams());
      scheduleDataRto_();
    }
    updateCongestionDiag();
    schedulePTO_();
    scheduleLossTimer();
    if (m_isServer && work.initialResponseAckd)
      impl()->initialResponseAckdTx(m_txDataGen);
    if (work.windowUpdate && m_txDataCredit.available()) impl()->flowCredit();
    if (app()->nativeTx()) { updatePersist_(); scheduleDataRto_(); }
    updateUserTimeout_();
    bool retransmitted = false;
    unsigned allowance = congestionAllowance_();
    if (work.congestionOpened || allowance || work.windowUpdate) {
      // Spend ACK-reopened credit on retained loss before ordinary producers.
      // Posting recovery after a synchronous bulk flush can starve it while
      // new data keeps resetting PTO. Include losses queued by earlier ACKs.
      if (allowance)
	for (unsigned i = 0; i < PktNumSpace::N; ++i)
	  if (m_txPkts[i].retransmitPending()) {
	    retransmitted = impl()->retransmit_(); // existing bounded batch
	    break;
	  }
      impl()->flushTx_();
    }
    if (work.retransmit && !retransmitted) impl()->queueRetransmit();
    finishMxTx_();
  }

  bool ackFrameValidTx(const AckSnapshot &ack) const {
    if (ack.level == PktNumSpace::AppData && ack.byteNext > m_txNext) return false;
    for (unsigned i = 0, n = ack.ranges.length(); i < n; ++i)
      if (ack.ranges[i].largest >= m_txPN[ack.level]) return false;
    return true;
  }

  bool validateAckECN(const AckSnapshot &ack, uint64_t *ceDelta = nullptr) {
    if (!ack.ecn.any()) return true;
    const AckECN &last = m_path.peerAckECN(ack.level);
    const AckECN &sent = m_path.sentECN(ack.level);
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
    uint64_t sentECT = sent.ect0 + sent.ect1;
    if (sentECT < sent.ect0) {
      fail = true;
    }
    sentECT += sent.ce;
    if (sentECT < sent.ce) {
      fail = true;
    }
    if (withCE > sentECT || ack.ecn.ect0 > sent.ect0 ||
	ack.ecn.ect1 > sent.ect1)
      fail = true;
    if (fail) {
#ifdef Zutcp_DEBUG
      auto oldState = m_path.ecnState();
      uint64_t largest = 0;
      if (ack.ranges)
	largest = ack.ranges[ack.ranges.length() - 1].largest;
#endif
      m_path.ecnValidationFailed();
      ++m_txDiag.ecnValidationFailures;
      ZutcpLOG(app()->qlogTrace(), ([
	level = ack.level,
	oldState = qlogECNState(oldState),
	state = ECNState::Failed,
	ect0 = ack.ecn.ect0,
	ect1 = ack.ecn.ect1,
	ce = ack.ecn.ce,
	previousECT0 = last.ect0,
		previousECT1 = last.ect1,
		previousCE = last.ce,
		sentECT0 = sent.ect0,
		sentECT1 = sent.ect1,
		sentTotal = sentECT,
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
	if (withCE_ > sentTotal || ect0 > sentECT0 || ect1 > sentECT1)
	  reason = ECNReason::CounterExceedsAck;
	ECNEvt event{
	  .linkInfo = linkInfo
		,
	  .ect0 = ect0,
	  .ect1 = ect1,
	  .ce = ce,
	  .previousECT0 = previousECT0,
	  .previousECT1 = previousECT1,
	  .previousCE = previousCE,
	  .largestAcked = largestAcked,
	  .packetSpace = level,
	  .oldState = ECNState::T(oldState),
	  .state = ECNState::T(state),
	  .reason = reason,
	  .disabled = disabled};

	o.logECNStateUpd(event, time);
      }));
      return false;
    }
    if (ceDelta) *ceDelta = ack.ecn.ce - last.ce;
    m_path.peerAckECN(ack.level) = ack.ecn;
    m_txDiag.peerAckECN[ack.level] = ack.ecn;
    return true;
  }

  ZuTime now_() const { return app()->transportNow(); }
  static constexpr ZuTime intervalUS(uint64_t usec) {
    return ZuTime{
      int64_t(usec / TimeUSPerSec),
      int32_t((usec % TimeUSPerSec) * TimeNSPerUS)};
  }
  static constexpr ZuTime intervalMS(uint64_t ms) {
    return ZuTime{
      int64_t(ms / TimeMSPerSec),
      int32_t((ms % TimeMSPerSec) * TimeNSPerMS)};
  }
  ZuTime maxAckDelay_() const {
    ZiAssert(txInvoked_(), "Zutcp", (), "ACK timer access outside Tx thread", return {});
    return intervalMS(m_peerTxParams.maxAckDelay);
  }
  static ZuTime transportParamMS(uint64_t ms) {
    if (!ms) return ZuTime{0};
    return intervalMS(ms);
  }
  ZuTime negotiatedIdleTimeout() const {
    uint64_t local = m_transportParams.maxIdleTimeout;
    uint64_t peer = m_peerParamsSet ?
      m_peerParams.maxIdleTimeout : 0;
    if (!local) return transportParamMS(peer);
    if (!peer) return transportParamMS(local);
    return transportParamMS(local < peer ? local : peer);
  }
  static ZuTime decodeAckDelay(uint64_t delay, uint64_t maxDelay, uint64_t exponent) {
    ZuTime maxAckDelay = intervalMS(maxDelay);
    uint64_t maxAckUsec = uint64_t(maxAckDelay.microsecs());
    if (delay > (maxAckUsec >> exponent))
      return maxAckDelay;
    delay <<= exponent;
    ZuTime ackDelay = intervalUS(delay);
    return ackDelay < maxAckDelay ? ackDelay : maxAckDelay;
  }
  ZuTime ackDelay_(uint64_t delay) const {
    ZiAssert(txInvoked_(), "Zutcp", (), "ACK delay access outside Tx thread", return {});
    return m_peerTxParams.set ? decodeAckDelay(
      delay, m_peerTxParams.maxAckDelay, m_peerTxParams.ackDelayExponent) : ZuTime{0};
  }
  ZuTime ptoTimeout() const {
    return m_ptoBackoff.timeout(m_rtt, maxAckDelay_());
  }
  ZuTime closeDrainDelay() const {
    return ptoTimeout() * ZuDecimal{3};
  }

  bool buildPayload_(
    PktNumSpace::T level, PktBuild &build, ZuBSpan frame) {
    build.reset();
    return appendPendingAck(level, build) && build.add(frame);
  }

  bool buildPayload_(
    PktNumSpace::T level, PktBuild &build,
    ZuBSpan prefix, ZuBSpan payload) {
    build.reset();
    return appendPendingAck(level, build) &&
      build.add(prefix) && build.add(payload);
  }

  void noteTxControlFrame(FrameType::T type) {
    switch (type) {
      case FrameType::MaxData: ++m_txDiag.maxDataTx; break;
      case FrameType::DataBlocked: ++m_txDiag.dataBlockedTx; break;
      case FrameType::Reset: ++m_txDiag.resetDataTx; break;
      case FrameType::StopSending: ++m_txDiag.stopSendingTx; break;
      default: break;
    }
  }

  void noteTxPktDiag(const TxPktRefs *refs, uint8_t ackLevel) {
    bool hasAck = ackLevel < PktNumSpace::N;
    bool hasData = false;
    bool hasControl = false;
    if (refs) {
      for (unsigned i = 0, n = refs->count(); i < n; ++i) {
	const SentFrameRef &ref = (*refs)[i];
	switch (ref.kind) {
	  case SentFrameKind::Data:
    ++m_txDiag.dataFramesTx;
    hasData = true;
    break;
	  case SentFrameKind::Control:
    ++m_txDiag.controlFramesTx;
    hasControl = true;
    noteTxControlFrame(ref.controlType);
    break;
	  default:
    break;
	}
      }
    }
    if (hasData && hasControl && hasAck)
      ++m_txDiag.ackDataCtlPktsTx;
    else if (hasData && hasControl)
      ++m_txDiag.dataCtlPktsTx;
    else if (hasData && hasAck)
      ++m_txDiag.ackDataPacketsTx;
    else if (hasControl && hasAck)
      ++m_txDiag.ackCtlPktsTx;
    else if (hasData)
      ++m_txDiag.dataOnlyPacketsTx;
    else if (hasControl)
      ++m_txDiag.ctlOnlyPktsTx;
    else if (hasAck)
      ++m_txDiag.ackOnlyPacketsTx;
    else
      ++m_txDiag.otherPacketsTx;
  }





  static uint64_t qlogUS(ZuTime t) {
    return uint64_t(t.microsecs());
  }

  static uint8_t qlogCount(unsigned n) {
    return n > 255 ? 255 : uint8_t(n);
  }

  static void qlogAddAckRange(
    ZutcpLog_::FrameEvt &frame, uint64_t first, uint64_t largest) {
    if (frame.ackRanges.length() >= ZutcpLog_::AckRangeMax) return;
    new (frame.ackRanges.push()) ZutcpLog_::QAckRange{
      .first = first,
      .largest = largest
    };
  }

  static void qlogAddFrame(
    ZutcpLog_::PktEvt &event, const ZutcpLog_::FrameEvt &frame) {
    if (event.frames.length() >= ZutcpLog_::FrameMax) {
      event.framesTruncated = true;
      return;
    }
    event.frames.push(frame);
  }

  static void qlogAddFrame(
    ZutcpLog_::RecLogEvt &event, const ZutcpLog_::FrameEvt &frame) {
    if (event.frames.length() < ZutcpLog_::FrameMax)
      event.frames.push(frame);
  }

  static ZuArray<uint64_t, ZutcpLog_::AckPacketMax> qlogAckedPNs(
    const PktTxUpdate &update) {
    ZuArray<uint64_t, ZutcpLog_::AckPacketMax> packetNumbers;
    for (unsigned i = 0; i < update.nAckedPNs; ++i)
      new (packetNumbers.push()) uint64_t(update.ackedPNs[i]);
    return packetNumbers;
  }

  ZuArray<ZutcpLog_::QAckRange, ZutcpLog_::AckRangeMax> qlogAckRanges(
    uint8_t ackLevel) const {
    ZuArray<ZutcpLog_::QAckRange, ZutcpLog_::AckRangeMax> ranges;
    if (ackLevel >= PktNumSpace::N) return ranges;
    const AckSnapshot &ack = m_txAck[ackLevel];
    for (unsigned i = 0, n = ack.ranges.length(); i < n; ++i) {
      if (ranges.length() >= ZutcpLog_::AckRangeMax) break;
      new (ranges.push()) ZutcpLog_::QAckRange{
	.first = ack.ranges[i].first,
	.largest = ack.ranges[i].largest
      };
    }
    return ranges;
  }

  static void qlogAddAckFrame(
    ZutcpLog_::PktEvt &event, uint8_t ackLevel, uint64_t ackLargest,
    const ZuArray<ZutcpLog_::QAckRange, ZutcpLog_::AckRangeMax>
      &ackRanges) {
    if (ackLevel >= PktNumSpace::N) return;
    ZutcpLog_::FrameEvt frame{
      .largestAcked = ackLargest,
      .type = FrameType::Ack,
      .rangeCount = qlogCount(ackRanges.length())
    };
    for (unsigned i = 0, n = ackRanges.length(); i < n; ++i)
      qlogAddAckRange(frame, ackRanges[i].first, ackRanges[i].largest);
    qlogAddFrame(event, frame);
  }

  void qlogAddRxFrame(
    ZutcpLog_::PktEvt &event, PktNumSpace::T level,
    const Frame &frame_) {
    event.frameCount = qlogCount(event.frameCount + 1);

    ZutcpLog_::FrameEvt frame{.type = frame_.type};
    switch (frame_.type) {
      case FrameType::Ack:
	frame.largestAcked = frame_.offset;
	frame.ackDelayUS = level == PktNumSpace::AppData && m_peerParamsSet ?
	  qlogUS(decodeAckDelay(
	    frame_.value, m_peerParams.maxAckDelay, m_peerParams.ackDelayExponent)) : 0;
	frame.rangeCount = qlogCount(frame_.ackRanges.length());
	for (unsigned i = 0, n = frame_.ackRanges.length(); i < n; ++i)
	  qlogAddAckRange(
    frame, frame_.ackRanges[i].first, frame_.ackRanges[i].largest);
	frame.ect0 = frame_.ackECN.ect0;
	frame.ect1 = frame_.ackECN.ect1;
	frame.ce = frame_.ackECN.ce;
	break;
      case FrameType::Data:
	frame.offset = frame_.offset;
	frame.length = frame_.length;
	frame.fin = frame_.fin;
	break;
      case FrameType::Reset:
	frame.errorCode = frame_.errorCode;
	frame.length = frame_.length;
	break;
      case FrameType::StopSending:
	frame.errorCode = frame_.errorCode;
	break;
      
      case FrameType::MaxData:
      case FrameType::DataBlocked:

	frame.value = frame_.value;
	break;
      
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	frame.errorCode = frame_.errorCode;
	frame.length = frame_.payload.length();
	break;
      default:
	break;
    }
    qlogAddFrame(event, frame);
  }

  template <typename Event>
  static void qlogAddTxFrame(Event &event, const SentFrameRef &ref) {
    switch (ref.kind) {
      case SentFrameKind::Data:
	qlogAddFrame(event, ZutcpLog_::FrameEvt{
	  .offset = ref.offset,
	  .length = ref.length,
	  .type = FrameType::Data,
	  .fin = ref.fin
	});
	return;
      case SentFrameKind::Control:
	qlogAddFrame(event, ZutcpLog_::FrameEvt{
	  .offset = ref.offset,
	  .length = ref.length,
	  .value = ref.value,
	  .errorCode =
    ref.controlType == FrameType::Reset ||
    ref.controlType == FrameType::StopSending ? ref.value : 0,
	  .type = ref.controlType
	});
	return;
      default:
	return;
    }
  }

  bool txAckMeta(
    PktNumSpace::T level, PktBuild &payload,
    uint8_t &ackLevel, uint64_t &ackLargest) const {
    ackLevel = PktNumSpace::N;
    ackLargest = 0;
    if (!payload.ack(level)) return false;
    const AckSnapshot &ack = m_txAck[level];
    if (!ack.ranges) return false;
    ackLevel = uint8_t(level);
    ackLargest = ack.ranges[ack.ranges.length() - 1].largest;
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendInitialPkt(
    CIDSel::T dcid, CIDSel::T scid,
    unsigned pnLength, bool padInitial, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "Initial packet construction outside Tx thread", return false);
    auto buf = allocTxPkt();
    if (ZuUnlikely(!buf)) return false;
    unsigned capacity = txPktCapacity(buf);
    bool pktAckEliciting = txAckEliciting(recordFrame, recordRefs, ackEliciting);
    const TransportParams *params = pktAckEliciting ? &m_txLocalParams : nullptr;
    // The first native server response carries pending envelope ACK evidence
    // with SYNACK; an ACK-only Initial must not precede the accepted local SYN.
    if (app()->nativeTx() && m_txOrigins.server && params &&
	!payload.ack(PktNumSpace::Initial) && !appendPendingAck(PktNumSpace::Initial, payload))
      return false;
    uint64_t pn = m_txPN[PktNumSpace::Initial];
    unsigned frameLength = payload.bytes();
    unsigned paramLength = params ? params->encodedLength() : 0;
    unsigned bodyPrefix = VarInt::length(paramLength) + paramLength;
    // Padding can enlarge the header's length VarInt. Calculate that length
    // before writing, so minimum-sized output still fits a 1200-byte buffer.
    for (unsigned i = 0; i < 4; ++i) {
      int header = Pkt::initialHdrLen(cid_(dcid), cid_(scid),
	uint64_t(bodyPrefix) + frameLength, pnLength);
      if (header < 0) return false;
      unsigned offset = unsigned(header) + pnLength + bodyPrefix;
      unsigned minimum = padInitial && offset < MinPktPayload ? MinPktPayload - offset : 0;
      unsigned target = minimum > payload.bytes() ? minimum : payload.bytes();
      if (target == frameLength) break;
      frameLength = target;
    }
    int offset = InitialPkt::write(buf->data(), capacity, cid_(dcid), cid_(scid),
      pn, pnLength, params, frameLength);
    if (offset < 0 || !payload.padTo(frameLength)) return false;
    unsigned n = unsigned(offset);
    for (unsigned i = 0; i < payload.count(); ++i) {
      const auto &part = payload.data()[i];
      memcpy(buf->data() + n, part.data(), part.length());
      n += part.length();
    }
    buf->length = unsigned(n);
    if constexpr (ZuIsSame<decltype(buf), ZmRef<PacketBuf>>{}) {
      if (app()->nativeTx()) {
	if (!initialTCPHeader_(buf->tcp, params)) return false;
      } else buf->tcp = tcpHeader(recordRefs);
    }
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(PktNumSpace::Initial, payload, ackLevel, ackLargest);
    PacketLease *backing = nullptr;
    if (!recordPktTx(
	  PktNumSpace::Initial, pn, unsigned(n), recordFrame, recordRefs,
	  pktAckEliciting, false, 0, ackLevel, ackLargest,
	  PktType::Initial, EcnMark::NotECT, &backing))
      return false;
    bindPacketLease(buf.ptr(), backing);
    bool sent = sendPkt(buf, ZuMv(addr));
    bindPacketLease(buf.ptr(), nullptr);
    if (!sent) {
      discardPktTx(
	PktNumSpace::Initial, pn, unsigned(n), pktAckEliciting);
      return false;
    }
    if (app()->nativeTx() && params) m_tcpSynSent = true;
    if (payload.ack(PktNumSpace::Initial)) ackSentTx_(PktNumSpace::Initial);
    return true;
  }









  template <typename ReceiveLong, typename ReceiveShort>
  void receiveDatagram_(
    Datagram d, ReceiveLong receiveLong, ReceiveShort receiveShort) {
    if (app()->nativeTx() && m_rxAborting) return;
    m_rxDiag.datagramsRx.store_(m_rxDiag.datagramsRx.load_() + 1);
    if (!d.buf) {
      ++m_rxDiag.failures;
      return;
    }
    m_rxDiag.bytesRx.store_(m_rxDiag.bytesRx.load_() + d.buf->length);
    recordPathRx(d.addr, d.buf->length);
    bool ok = true;
    unsigned offset = 0;
    while (offset < d.buf->length) {
      ZuBSpan packet{
	d.buf->data() + offset, d.buf->length - offset};
      if (Pkt::isLong(packet)) {
	LongHdr h;
	if (Pkt::parseLong(packet, h) < 0) {
	  ZutcpLOG(app()->qlogTrace(), ([
	    level = PktNumSpace::Initial,
	    packetBytes = packet.length(),
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    PktEvt event{.packetSize = packetBytes};
	    event.packetType = PktType::N;
	    event.packetSpace = level;
	    event.ecn = EcnMark::N;
	    event.reason = Reason::ParseLong;
	    event.linkInfo = linkInfo;
	    o.logPktDrop(event, time);
	  }));
	  ok = false;
	  break;
	}
	uint64_t packetLen = uint64_t(h.pnOffset) + h.length;
	if (packetLen > packet.length() || packetLen < h.payloadOffset ||
    packetLen > UINT_MAX) {
	  ZutcpLOG(app()->qlogTrace(), ([
	    packetType = h.type,
	    level = PktNumSpace::Initial,
	    packetBytes = packet.length(),
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    PktEvt event{.packetSize = packetBytes};
	    event.packetType = packetType;
	    event.packetSpace = level;
	    event.ecn = EcnMark::N;
	    event.reason = Reason::PacketLength;
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
    ZutcpLOG(app()->qlogTrace(), ([
      bytes = d.buf->length,
      ecn = d.ecn,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramRecv(event, time);
    }));
  }

  bool duplicatePkt(PktNumSpace::T level, uint64_t pn, ZiSockAddr addr, ZuBSpan frames) {
    unsigned offset = 0;
    Frame frame;
    while (offset < frames.length()) {
      unsigned n;
      if (FrameCodec::parse({frames.data() + offset, frames.length() - offset}, frame, n) < 0 ||
	  !n || !packetFrameLegal_(level, frame)) return false;
      if (FrameCodec::ackEliciting(frame.type)) {
	noteDupAck(level, pn, ZuMv(addr));
	return true;
      }
      offset += n;
    }
    return true;
  }

  template <typename PrepareLong, typename ConsumeFrames>
  bool receiveLongPkt(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    PrepareLong prepareLong, ConsumeFrames consumeFrames) {
    auto base = d.buf->data() + packetOffset;
    InitialPkt initial;
    if (initial.parse({base, packetLen}, m_rxLargestPN[PktNumSpace::Initial]) !=
	int(packetLen)) return false;
    TransportParams params;
    if (initial.params && (params.decode(initial.params) < 0 ||
	params.initialSCID != initial.header.scid)) return false;
    if (app()->nativeTx() && initial.params && !m_rxOrigins.server &&
	params.origDCID != m_initialDCID) return false;
    TCPHeader tcp;
    TCPOrigins origins;
    if (!qualifyTCPInitialRx_(initial, d, tcp, origins) || !prepareLong(initial.header, d))
      return false;
    if (app()->nativeTx()) {
      if (!setTCPOriginsRx_(origins)) return false;
      if (!initial.params || !origins.server) m_rxSynAcked = true;
    }
    constexpr auto level = PktNumSpace::Initial;
    auto pn = initial.number;
    if (rxPktSeen(level, pn)) {
      if (initial.params) { noteDupAck(level, pn, d.addr); return true; }
      return duplicatePkt(level, pn, d.addr, initial.frames);
    }
    if (drainingRx()) return true;
    if (closingRx_()) { noteClosingPacket(d.addr); return true; }
    if (initial.params && !m_peerParamsSet) {
      if (app()->nativeTx()) params.initialMaxData = d.tcp.window;
      peerParams(params);
    }
    receiveTCP(tcp);
    ++m_rxDiag.packetsRx;
    RxAckMeta ack;
    ack.tcp = tcp;
    ZuUnion<void, ZutcpLog_::PktEvt> event;
    ZutcpLog_::PktEvt *qlog = nullptr;
    if (ZutcpLogger::enabled()) qlog = new (event.new_<1>()) ZutcpLog_::PktEvt{};
    if (!consumeFrames(level, pn, initial.frames, d.addr, d.buf, ack, qlog)) {
      ZutcpLOG(app()->qlogTrace(), ([pn, packetLen, linkInfo = linkInfo_()](
          auto &o, ZuTime time) {
        PktEvt event{
          .linkInfo = linkInfo,
          .packetNumber = pn,
          .packetSize = packetLen,
          .packetType = PktType::Initial,
          .packetSpace = level,
          .reason = Reason::FramePolicy
        };
        o.logPktDrop(event, time);
      }));
      return false;
    }
    if (initial.params) { ack.ackEliciting = true; ack.immediateAck = true; }
    noteAck(level, pn, ack.ackEliciting, d.addr, ack.immediateAck, d.ecn);
    if (qlog) ZutcpLOG(app()->qlogTrace(), ([
      event = *qlog, pn, packetLen,
      payloadSize = initial.frames.length(), ecn = d.ecn,
      ackEliciting = ack.ackEliciting, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = PktType::Initial;
      event.packetSpace = level;
      event.packetNumber = pn;
      event.packetSize = packetLen;
      event.payloadSize = payloadSize;
      event.ecn = ecn;
      event.ackEliciting = ackEliciting;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    return true;
  }





  template <typename HandleControl>
  bool consumeFrames(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, RxAckMeta &ackMeta,
    ZutcpLog_::PktEvt *qlog, HandleControl handleControl) {
    unsigned offset = 0;
    auto frame_ = ZmScratch(Frame, 1);
    auto &frame = *new (frame_.push()) Frame{};
    bool ackEliciting = false;
    bool immediateAck = false;
    bool admitted = true;

    unsigned n = frames.length();
    while (offset < n) {
      unsigned used = 0;
      if (FrameCodec::parse(
	  ZuBSpan{frames.data() + offset, n - offset},
	  frame, used) < 0 || !used) {
	return false;
      }
      ++m_rxDiag.framesRx;
      if (!packetFrameLegal_(level, frame)) {
	return false;
      }
      if (qlog) qlogAddRxFrame(*qlog, level, frame);
      if (FrameCodec::ackEliciting(frame.type))
	ackEliciting = true;
      switch (frame.type) {
	case FrameType::Ack:
	  processAckFrame(level, frame);
	  break;
	case FrameType::Data:
	{
	  if (app()->nativeTx()) {
	    frame.fin = ackMeta.tcp.flags & 0x01;
	    if (!clipTCPData(frame, ackMeta.tcp.seq)) {
	      immediateAck = true;
	      break;
	    }
	  }
	  bool retained = true;
	  m_rxDiag.dataBytesRx += frame.payload.length();
	  if (receiveFrame(
		frame, ZmRef<ZiIOBuf>{packetBuf}, nullptr, &immediateAck, &retained) < 0) {
	    return false;
	  }
	  if (retained)
	    impl()->dataFrame(frame.offset, frame.payload, frame.fin);
	  else
	    admitted = false; // continue later control, but withhold this packet's ACK
	} break;
	case FrameType::Reset:
	case FrameType::StopSending:
	  if (receiveFrame(frame) < 0) {
	    return false;
	  }
	  break;
	case FrameType::MaxData:
	  if (app()->nativeTx() || !rxApplyMaxData(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::DataBlocked:
	  if (app()->nativeTx() || !receiveDataBlocked(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::ConnectionClose:
	case FrameType::ApplicationClose:
	  if (!handleControl(level, frame, addr)) return false;
	  break;
	case FrameType::Padding:
	case FrameType::Ping:
	  break;
	default:
	  return false;
	}
      offset += used;
    }
    ackMeta.ackEliciting = ackEliciting;
    ackMeta.immediateAck = immediateAck;
    return admitted;
  }

  template <typename ConsumeFrames>
  bool receiveShortPkt(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    ConsumeFrames consumeFrames, const CxnID *routedDCID = nullptr) {
    auto base = d.buf->data() + packetOffset;
    ShortHdr header;
    unsigned cidLength = routedDCID ? routedDCID->length() : m_localSCID.length();
    if (Pkt::parseShort({base, packetLen}, cidLength, header) < 0) return false;
    unsigned offset = header.pnOffset + header.pnLength;
    if (offset > packetLen) return false;
    uint64_t truncated = 0;
    for (unsigned i = 0; i < header.pnLength; ++i)
      truncated = (truncated<<8) | base[header.pnOffset + i];
    constexpr auto level = PktNumSpace::AppData;
    auto pn = PktNumber::decode(m_rxLargestPN[level], truncated, header.pnLength<<3);
    if (app()->nativeTx() && m_rxAborting) return true;
    if (app()->nativeTx() && (d.tcp.flags & 0x04) && !m_rxOrigins.peerKnown) {
      // SYN-SENT can authenticate this reset's ACK without a peer sequence
      // origin. No packet-number, peer-window or control evidence is applied.
      if (!m_rxOrigins.server && m_rxOrigins.localKnown && header.dcid == m_localSCID &&
	  m_rxLinkState == LinkState::Handshaking && (d.tcp.flags & 0x17) == 0x14 &&
	  d.tcp.ack == uint32_t(m_rxOrigins.local + 1)) receiveTCPReset_();
      return true;
    }
    TCPHeader tcp;
    if (!normalizeTCPRx_(d.tcp, tcp)) return false;
    if (app()->nativeTx() && (tcp.flags & 0x04)) {
      if (tcpSequenceAllowed(tcp.seq, 0)) receiveTCPReset_();
      return true; // Never apply ACK/control evidence or respond to an incoming RST.
    }
    if (app()->nativeTx()) {
      int admitted = admitTCPSegment(tcp, {base + offset, packetLen - offset});
      if (admitted < 0) return false;
      if (!admitted) {
	replyTCPWindow();
	return true; // No rejected PN, peer ACK/window, control or retention commitment.
      }
    }
    if (rxPktSeen(level, pn))
      return duplicatePkt(level, pn, d.addr, {base + offset, packetLen - offset});
    if (drainingRx()) return true;
    if (closingRx_()) { noteClosingPacket(d.addr); return true; }
    receiveTCP(tcp);
    RxAckMeta ack;
    ack.tcp = tcp;
    ZuUnion<void, ZutcpLog_::PktEvt> event;
    ZutcpLog_::PktEvt *qlog = nullptr;
    if (ZutcpLogger::enabled()) qlog = new (event.new_<1>()) ZutcpLog_::PktEvt{};
    if (!consumeFrames(level, pn, {base + offset, packetLen - offset},
        d.addr, d.buf, ack, qlog)) {
      ZutcpLOG(app()->qlogTrace(), ([pn, packetLen, linkInfo = linkInfo_()](
          auto &o, ZuTime time) {
        PktEvt event{
          .linkInfo = linkInfo,
          .packetNumber = pn,
          .packetSize = packetLen,
          .packetType = PktType::Short,
          .packetSpace = level,
          .reason = Reason::FramePolicy
        };
        o.logPktDrop(event, time);
      }));
      return false;
    }
    ++m_rxDiag.packetsRx;
    noteAck(level, pn, ack.ackEliciting, d.addr, ack.immediateAck, d.ecn);
    if (qlog) ZutcpLOG(app()->qlogTrace(), ([
      event = *qlog, pn, packetLen,
      payloadSize = packetLen - offset, ecn = d.ecn,
      ackEliciting = ack.ackEliciting, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = PktType::Short;
      event.packetSpace = level;
      event.packetNumber = pn;
      event.packetSize = packetLen;
      event.payloadSize = payloadSize;
      event.ecn = ecn;
      event.ackEliciting = ackEliciting;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    return true;
  }

  bool recordPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT, PacketLease **backing = nullptr) {
    bool recorded = recordRefs ?
      recordTxPkt(
	level, pn, bytes, *recordRefs, ackEliciting, pmtudProbe, pmtudSize,
	ackLevel, ackLargest, packetType, ecn, backing) :
      recordTxPkt(
	level, pn, bytes, recordFrame, ackLevel, ackLargest, packetType, ecn, backing);
    if (!recorded) return false;
    ++m_txPN[level];
    m_txDiag.packetsTx.store_(m_txDiag.packetsTx.load_() + 1);
    m_txDiag.bytesTx.store_(m_txDiag.bytesTx.load_() + bytes);
    noteTxPktDiag(recordRefs, ackLevel);
    ZutcpLOG(app()->qlogTrace(), ([
      level, pn,
      packetBytes = bytes,
      bytesInFlight = m_congestion.bytesInFlight(),
      ackEliciting,
      ackLevel, ackLargest,
      ackRanges = qlogAckRanges(ackLevel),
      packetType,
      ecn,
      hasRecordRefs = bool(recordRefs),
      recordRefs = recordRefs ? recordRefs->summary() : TxPktRefs{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      unsigned frameCount = hasRecordRefs ? recordRefs.count() : 0;
      if (ackLevel < PktNumSpace::N) ++frameCount;
      PktEvt event{
		.linkInfo = linkInfo
      ,
	.packetNumber = pn,
		.packetSize = packetBytes,
		.bytesInFlight = bytesInFlight,
		.frameCount = qlogCount(frameCount),
		.ackEliciting = ackEliciting};
      event.packetType = packetType == PktType::N ?
	pktTypeFromSpace(level) : packetType;
      event.packetSpace = level;
      event.ecn = ecn;
      qlogAddAckFrame(event, ackLevel, ackLargest, ackRanges);
      if (hasRecordRefs)
	for (unsigned i = 0, n = recordRefs.count(); i < n; ++i)
	  qlogAddTxFrame(event, recordRefs[i]);
      o.logPktSent(event, time);
    }));
    if (ackEliciting) {
      if (level == PktNumSpace::AppData && establishedTx())
	noteAckElicitTx();
      schedulePTO();
    }
    return true;
  }
  void discardPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, bool ackEliciting) {
    m_txPkts[level].discard(pn);
    // A failed send retires only this packet; its data still needs an ACK.
    if (ackEliciting) {
      m_congestion.release(bytes);
      updateCongestionDiag();
    }
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendTCPResetPkt_(AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zutcp", (), "TCP reset send outside Tx", return false);
    if (!app()->nativeTx() || !m_txOrigins.localKnown || m_mxFinishing ||
	m_tcpCloseState == TcpCloseState::TimeWait || m_tcpCloseState == TcpCloseState::Closed ||
	m_tcpResetPeer || m_tcpResetSent ||
	!m_txPeerCID || m_txPN[PktNumSpace::AppData] == UINT64_MAX) return false;
    auto buf = allocTxPkt();
    if (!buf) return false;
    auto pn = m_txPN[PktNumSpace::AppData];
    int n = Pkt::writeShort(buf->data(), buf->avail(), m_txPeerCID, pn, 4);
    if (n < 0 || unsigned(n) == buf->avail()) return false;
    buf->data()[n] = 0; // Retain the intermediate short carrier, without eliciting an ACK.
    buf->length = unsigned(n) + 1;
    buf->tcp = tcpHeader();
    buf->tcp.flags = m_txOrigins.peerKnown ? 0x14 : 0x04;
    if (!sendPkt(ZuMv(buf))) return false;
    ++m_txPN[PktNumSpace::AppData];
    m_tcpResetSent = true;
    return true;
  }
  bool sendTCPReset_() { return false; }
  void tcpResetAbort_(bool peer) { disconnect(peer); }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendShortPkt_(
    CIDSel::T dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    return sendShortPkt_(cid_(dcid), pnLength, payload, ZuMv(addr), recordFrame,
      recordRefs, ackEliciting, ZuMv(allocTxPkt), ZuMv(sendPkt), pmtudSize);
  }
  template <typename AllocTxPkt, typename SendPkt>
  bool sendShortPkt_(
    const CxnID &dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    ZiAssert(txInvoked_(), "Zutcp", (), "Short packet build outside Tx", return false);
    if (!establishedTx() && !closingTx_()) return false;
    if (app()->nativeTx() && (!m_txOrigins.localKnown || !m_txOrigins.peerKnown)) return false;
    int shortHead = Pkt::shortHdrLen(dcid, pnLength);
    if (shortHead < 0) return false;
    decltype(allocTxPkt()) buf;
    TxBacking *source = nullptr;
    unsigned shift = 0, head = 0;
    if constexpr (ZuIsSame<decltype(buf), ZmRef<PacketBuf>>{}) {
      if (!pmtudSize && recordRefs && recordRefs->count()) {
	const auto &ref = (*recordRefs)[recordRefs->count() - 1];
	auto storage = ref.range.storage.ptr();
	if (ref.kind == SentFrameKind::Data && storage && storage->native &&
	    (storage->pending || *storage->backing)) {
	  if (*storage->backing && !app()->txWritable(*storage)) return false;
	  head = tcpHead(m_path.local().type());
	  if (!payload.count() || !head || ref.range.offset != storage->payloadOffset ||
	      ref.range.length != storage->payloadLength) return false;
	  const auto &last = payload.data()[payload.count() - 1];
	  if (last.data() != storage->data + ref.range.offset ||
	      last.length() != ref.range.length) return false;
	  unsigned prefix = unsigned(shortHead) + payload.bytes() - ref.range.length;
	  if (storage->payloadOffset < head + prefix) return false;
	  shift = storage->payloadOffset - head - prefix;
	  buf = static_cast<PacketBuf *>(storage);
	  buf->allocator_ = storage->native;
	  buf->data__ = reinterpret_cast<uintptr_t>(storage->data) |
	    ZiIOBuf::Builtin | ZiIOBuf::Registered;
	  buf->size = storage->size;
	  buf->skip = storage->payloadOffset;
	  buf->length = storage->payloadLength;
	  source = storage;
	  source->fin |= ref.fin;
	}
      }
    }
    if (!buf) buf = allocTxPkt();
    if (!buf) return false;
    TxPacketView view{source ? static_cast<PacketBuf *>(buf.ptr()) : nullptr,
      shift, head};
    unsigned capacity = txPktCapacity(buf);
    constexpr auto level = PktNumSpace::AppData;
    auto pn = m_txPN[level];
    int header = Pkt::writeShort(buf->data(), capacity, dcid, pn, pnLength);
    if (header < 0 || unsigned(header) > capacity) return false;
    unsigned padded = pmtudSize;
    if (padded && (padded <= unsigned(header) ||
        !payload.padTo(padded - unsigned(header)))) return false;
    if (payload.bytes() > capacity - unsigned(header)) return false;
    unsigned n = unsigned(header);
    for (unsigned i = 0; i < payload.count(); ++i) {
      auto part = payload.data()[i];
      if (!source || i + 1 != payload.count())
	memcpy(buf->data() + n, part.data(), part.length());
      n += part.length();
    }
    buf->length = n;
    auto windowSerial = m_txRxSerial;
    if constexpr (ZuIsSame<decltype(buf), ZmRef<PacketBuf>>{}) {
      buf->tcp = tcpHeader(recordRefs);
      // The sole untracked PING is the intermediate persist probe.
      if (!ackEliciting && recordRefs && !recordRefs->count() && payload.bytes() == 1 &&
	  payload.count() == 1 && payload.data()[0][0] == 0x01) {
	buf->tcp.seq = uint32_t(m_txNext - 1);
	if (app()->nativeTx()) buf->tcp.seq += uint32_t(m_txOrigins.local + 1);
      }
    }
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(level, payload, ackLevel, ackLargest);
    bool eliciting = txAckEliciting(recordFrame, recordRefs, ackEliciting);
    auto ecn = m_path.txECN(level, eliciting, pmtudSize != 0);
    PacketLease *backing = nullptr;
    if (!recordPktTx(level, pn, n, recordFrame, recordRefs, eliciting,
        pmtudSize != 0, pmtudSize, ackLevel, ackLargest, PktType::Short, ecn, &backing))
      return false;
    bindPacketLease(buf.ptr(), source ? &source->backing : backing);
    bool sent = sendPkt(buf, ZuMv(addr), ecn);
    bindPacketLease(buf.ptr(), nullptr);
    if (sent) {
      if (windowSerial == m_txRxSerial) m_windowDirty = false;
      if (recordRefs) acceptTxData(*recordRefs);
      else {
	SentFrameRef ref;
	bool eliciting;
	if (sentFrameRef(recordFrame, ref, eliciting)) acceptTxData(ref);
      }
    }
    if (source && source->pending && !buf->data_()) {
      auto pending = source->pending.ptr();
      pending->data__ = 0;
      pending->size = pending->skip = pending->length = 0;
      source->pending = nullptr;
    }
    if (!sent) {
      discardPktTx(level, pn, n, eliciting);
      return false;
    }
    if (payload.ack(level)) ackSentTx_(level);
    return true;
  }

private:
  bool packetFrameLegal_(
    PktNumSpace::T level, const Frame &frame) const {

    switch (frame.type) {
      case FrameType::Unknown:
	return false;
      case FrameType::Padding:
      case FrameType::Ping:
      case FrameType::ConnectionClose:
	return true;
      case FrameType::Ack:
	return level == PktNumSpace::AppData ?
	  (app()->nativeTx() ? ZuNull(frame.byteNext) : !ZuNull(frame.byteNext)) :
	  ZuNull(frame.byteNext);
      case FrameType::Data:
      case FrameType::Reset:
      case FrameType::StopSending:
      case FrameType::MaxData:
      
      
      case FrameType::DataBlocked:
      
      




	return level == PktNumSpace::AppData;
      case FrameType::ApplicationClose:
	return level == PktNumSpace::AppData;
      default:
	return false;
    }
  }

  bool validateMaxData(const Frame &frame) const {
    return frame.type == FrameType::MaxData;
  }
  bool validateDataBlocked_(const Frame &frame) const {
    return frame.type == FrameType::DataBlocked &&
      frame.value <= m_rxDataCredit.limit();
  }
  bool blockedFrameNeeded(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::DataBlocked:
	return m_lastDataBlocked != frame.value;
      default:
	return true;
    }
  }
  void noteBlockedQueued(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::DataBlocked:
	m_lastDataBlocked = frame.value;
	break;
      default:
	break;
    }
  }
  void noteControlDequeued(const ControlFrame &frame) {
    switch (frame.type) {
      default:
	break;
    }
  }

  bool queuePendingControl(PendingControl &slot, const ControlFrame &frame) {
    if (!frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame) {
      switch (frame.type) {
	case FrameType::MaxData:
	case FrameType::DataBlocked:
	  if (slot.frame.value >= frame.value) return false;
	  break;
	default:
	  break;
      }
    }
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  void clearPendingControls() {
    m_maxDataControl = {};
    m_dataBlockedControl = {};
    clearControls();
  }

  bool controlStillValid_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return m_maxDataControl.frame == frame;
      case FrameType::DataBlocked:
	return !m_resetSent && m_dataBlockedControl.frame == frame &&
	  m_txDataCredit.blocked() &&
	  frame.value == m_txDataCredit.limit();
      case FrameType::Reset:
      case FrameType::StopSending:
	return controlStillValid(frame);
      default:
	return false;
    }
  }

  void maybeExtendMaxData() {
    uint64_t window = m_transportParams.initialMaxData;
    if (!window || m_rxDataCredit.available() > window / 2) return;
    uint64_t maximum =
      m_rxRetired > U64Null - window ? U64Null :
      m_rxRetired + window;
    if (maximum <= m_rxDataCredit.limit()) return;
    m_rxDataCredit.extend(maximum);
    postFlowUpdate(
      FlowUpdate{FrameType::MaxData, maximum});
  }
  bool rxInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zutcp", (),
      "QUIC link Rx access before app initialization", return false);
    return m_app->rxInvoked();
  }
  bool txInvoked_() const {
    ZiAssert(m_app && m_app->mx(), "Zutcp", (),
      "QUIC link Tx access before app initialization", return false);
    return m_app->txInvoked();
  }

  void scheduleCxnTimer(
    const char *name, CxnTimer::T action,
    ZuTime out, int mode, ZmScheduler::Timer *timer) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC timer schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC timer schedule before app initialization", return);
    if (disconnecting_()) return;
    if (debugLog()) ZiLOG(Debug, "Zutcp", ([name](auto &s) {
      s << "QUIC timer armed name=" << name;
    }));
    auto target = timerState_(action).deadline;
    if (*timer && **target &&
	((mode == ZmScheduler::Advance && *target <= out) ||
	 (mode == ZmScheduler::Defer && *target >= out))) return;
    *target = out;
    app()->mx()->add(timer, out, ZmScheduler::Update,
      [link = impl(), action](auto &&arm) {
	return arm([link, action]() mutable {
	  if (link->disconnecting_()) return;
	  link->cxnTimer(action);
	});
      }, app()->txThread());
  }

  struct TimerState {
    ZmScheduler::Timer *timer;
    ZuTime *deadline;
  };
  TimerState timerState_(CxnTimer::T action) {
    switch (action) {
      case CxnTimer::Connect: return {&m_connectTimer, &m_connectTimerOut};
      case CxnTimer::User: return {&m_userTimer, &m_userTimerOut};
      case CxnTimer::CloseDrain: return {&m_closeDrainTimer, &m_closeDrainTimerOut};
      case CxnTimer::FinWait: return {&m_finWaitTimer, &m_finWaitTimerOut};
      case CxnTimer::AckDelay: return {&m_ackDelayTimer, &m_ackDelayTimerOut};
      case CxnTimer::Loss: return {&m_lossTimer, &m_lossTimerOut};
      case CxnTimer::PTO: return {&m_ptoTimer, &m_ptoTimerOut};
      case CxnTimer::RTO: return {&m_rtoTimer, &m_rtoTimerOut};
      case CxnTimer::Persist: return {&m_persistTimer, &m_persistTimerOut};
      case CxnTimer::Idle: return {&m_idleTimer, &m_idleTimerOut};
      case CxnTimer::HeartBeat: return {&m_heartBeatTimer, &m_heartBeatTimerOut};
      case CxnTimer::Close: return {&m_closeTimer, &m_closeTimerOut};
      default: return {&m_pmtudTimer, &m_pmtudTimerOut};
    }
  }
  void cancelTimer_(CxnTimer::T action, ZmScheduler::Timer *timer) {
    *timerState_(action).deadline = ZuTime{};
    app()->mx()->cancel(timer);
  }

protected:
  void cxnTimer(CxnTimer::T action) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "connection timer dispatch outside Tx thread", return);
    auto state = timerState_(action);
    ZuTime out = *state.deadline;
    if (!*out) return;
    if (out > now_()) {
      scheduleCxnTimer("deadline recheck", action, out, ZmScheduler::Update, state.timer);
      return;
    }
    *state.deadline = ZuTime{};
    switch (action) {
      case CxnTimer::Connect:
	if (m_txLinkState == LinkState::Handshaking) impl()->connectExpired();
	break;
      case CxnTimer::User:
	userTimeout_();
	break;
      case CxnTimer::CloseDrain:
      case CxnTimer::FinWait:
	if (!linkClosedTx_()) expirePolicy_(action);
	break;
      case CxnTimer::AckDelay:
	ackDelay_();
	break;
      case CxnTimer::Loss:
	m_lossTimerOut = ZuTime{};
	lossTime_();
	break;
      case CxnTimer::PTO:
	m_ptoTimerOut = ZuTime{};
	impl()->pto_();
	break;
      case CxnTimer::RTO:
	m_rtoTimerOut = ZuTime{};
	dataRtoExpired_();
	break;
      case CxnTimer::Persist:
	persistExpired_();
	break;
      case CxnTimer::Idle:
	m_idleTimerOut = ZuTime{};
	idleTimeout_();
	break;
      case CxnTimer::HeartBeat:
	m_heartBeatTimerOut = ZuTime{};
	heartBeat_();
	break;
      case CxnTimer::Close:
	closeTimeout_();
	break;
      case CxnTimer::PMTUD:
	pmtudTimeout();
	break;
    }
  }

private:
  bool ptoLevel(PktNumSpace::T &level) const {
    bool have = false;
    ZuTime out;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto l = PktNumSpace::T(i);
      if (m_txSpaceDiscarded[l]) continue;
      if (l == PktNumSpace::AppData && !establishedTx()) continue;
      const auto &tx = m_txPkts[l];
      if (!tx.ackElicitingInFlight() &&
	  (l == PktNumSpace::AppData || !tx.retransmitPending())) continue;
      ZuTime deadline = ptoDeadline(l);
      if (!have || deadline < out) {
	level = l;
	out = deadline;
	have = true;
      }
    }
    if (have) return true;
    return fallbackProbeLevel(level);
  }

  bool fallbackProbeLevel(PktNumSpace::T &level) const {
    if (!handshakeStartedTx() || establishedTx()) return false;
    bool have = false;
    ZuTime out;
    auto candidate = [this, &level, &have, &out](
	PktNumSpace::T l, bool pending) {
      if (!pending) return;
      ZuTime deadline = ptoDeadline(l);
      if (!have || deadline < out) {
	level = l;
	out = deadline;
	have = true;
      }
    };
    candidate(PktNumSpace::Initial, initProbePending());
    candidate(PktNumSpace::AppData, appProbePending());
    return have;
  }

  ZuTime ptoDeadline(PktNumSpace::T level) const {
    ZuTime t = m_txPkts[level].latestAckSentTime();
    if (!*t) t = now_();
    ZuTime delay = level == PktNumSpace::AppData ? maxAckDelay_() : ZuTime{0};
    ZuTime out = t + m_ptoBackoff.timeout(m_rtt, delay);
    if (level == PktNumSpace::Initial && initProbePending() &&
	*m_txInitProbeOut && m_txInitProbeOut > out)
      out = m_txInitProbeOut;
    if (level == PktNumSpace::AppData && appProbePending() &&
	*m_txAppProbeOut && m_txAppProbeOut > out)
      out = m_txAppProbeOut;
    return out;
  }

  bool initProbePending() const {
    if (m_txSpaceDiscarded[PktNumSpace::Initial]) return false;
    const auto &tx = m_txPkts[PktNumSpace::Initial];
    return !tx.ackElicitingInFlight() && !tx.retransmitPending();
  }



  bool appProbePending() const {
    if (m_txSpaceDiscarded[PktNumSpace::AppData]) return false;
    const auto &tx = m_txPkts[PktNumSpace::AppData];
    return !tx.ackElicitingInFlight() && !tx.retransmitPending();
  }

  ZuTime &fallbackProbeOut(PktNumSpace::T level) {
    switch (level) {
      case PktNumSpace::AppData: return m_txAppProbeOut;
      default: return m_txInitProbeOut;
    }
  }

  // Fallback probes restart pre-establishment recovery when no ack-eliciting
  // packet remains in flight to clock Initial PTO progress.
  static constexpr unsigned PTOFallbackProbeDelayNS = 100000000;
  static ZuTime fallbackProbeDelay() {
    return ZuTime(0, PTOFallbackProbeDelayNS);
  }

  static Ztc::LinkState::T telState(LinkState::T state) {
    switch (state) {
      case LinkState::Starting:
      case LinkState::Handshaking:
	return Ztc::LinkState::Connecting;
      case LinkState::Established:
	return Ztc::LinkState::Up;
      case LinkState::Closing:
      case LinkState::Draining:
	return Ztc::LinkState::Disconnecting;
      default:
	return Ztc::LinkState::Down;
    }
  }

  LinkState::T publicLinkState() const {
    return LinkState::T(m_publicLinkState.load_());
  }
  void initLinkState(LinkState::T state) {
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState_(oldState, newState);
    m_rxLinkState = state;
    m_txLinkState = state;
    m_publicLinkState = unsigned(state);
  }
  void linkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC lifecycle transition outside Rx thread", return);
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState_(oldState, newState);
    m_rxLinkState = state;
    m_publicLinkState = unsigned(state);
  }
  LinkStateSnapshot linkSnapshotRx() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC lifecycle snapshot outside Rx thread", return {});
    return LinkStateSnapshot{
      .error = m_rxAppClose.error,
      .state = m_rxLinkState,
      .closed = m_rxAppClose.closed,
      .application = m_rxAppClose.application};
  }
  LinkStateSnapshot linkSnapshotTx() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC lifecycle snapshot outside Tx thread", return {});
    return LinkStateSnapshot{
      .error = m_txAppClose.error,
      .state = m_txLinkState,
      .closed = m_txAppClose.closed,
      .application = m_txAppClose.application};
  }
  void applyLinkSnapshotTx_(LinkStateSnapshot snapshot) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC lifecycle snapshot apply outside Tx thread", return);
    if (snapshot.state < m_txLinkState) return;
    bool opening = m_txLinkState == LinkState::Starting &&
      snapshot.state == LinkState::Handshaking;
    m_txAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_txLinkState = snapshot.state;
    if (opening)
      scheduleCxnTimer("connect", CxnTimer::Connect, now_() + app()->connectTimeout(),
	ZmScheduler::Update, &m_connectTimer);
    else if (snapshot.state >= LinkState::Established)
      cancelTimer_(CxnTimer::Connect, &m_connectTimer);
    updateUserTimeout_();
    updateCloseTimers_();
  }
  void publishLinkSnapshotTx() {
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting_()) return;
      link->applyLinkSnapshotTx_(snapshot);
    });
  }
  void logLinkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC lifecycle log outside Rx thread", return);
    ZutcpLOG(app()->qlogTrace(), ([
      oldState = m_rxLinkState,
      newState = state,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvt event{
	.linkInfo = linkInfo
      ,
	.oldState = oldState,
	.newState = newState};
      o.logCxnStateUpd(event, time);
    }));
  }
  void resetLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC lifecycle reset outside Rx thread", return);
    m_rxAppClose = {};
    m_publicCloseError = 0;
    linkStateRx(LinkState::Starting);
    m_suspiciousDataFrames = 0;
    m_suspiciousDataClosed = false;
  }

protected:
  bool startHandshake() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC handshake transition outside Rx thread", return false);
    if (m_rxLinkState != LinkState::Starting) return false;
    logLinkStateRx(LinkState::Handshaking);
    linkStateRx(LinkState::Handshaking);
    publishLinkSnapshotTx();
    return true;
  }

private:
  bool establishState() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC establish transition outside Rx thread", return false);
    if (m_rxLinkState != LinkState::Handshaking) return false;
    logLinkStateRx(LinkState::Established);
    linkStateRx(LinkState::Established);
    publishLinkSnapshotTx();
    return true;
  }

  bool closeLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC close transition outside Rx thread", return false);
    if (m_rxLinkState >= LinkState::Closing) return false;
    logLinkStateRx(LinkState::Closing);
    linkStateRx(LinkState::Closing);
    return true;
  }
  bool drainLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC drain transition outside Rx thread", return false);
    if (m_rxLinkState >= LinkState::Draining) return false;
    logLinkStateRx(LinkState::Draining);
    linkStateRx(LinkState::Draining);
    return true;
  }
  void closedLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC closed transition outside Rx thread", return);
    if (m_rxLinkState == LinkState::Closed) return;
    logLinkStateRx(LinkState::Closed);
    linkStateRx(LinkState::Closed);
  }

protected:
  uint64_t txQueueCount_() const {
    uint64_t count = 0;
    ZuUnroll::all<PktNumSpace::N>([this, &count](auto I) {
      count += m_txPkts[I()].count();
    });
    return count;
  }

private:
  void rxQueueTelemetry_(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inBytes = m_rxDiag.bytesRx.load_();
    data.outBytes = 0;
    data.inCount = m_rxDiag.datagramsRx.load_();
    data.outCount = 0;
    data.count = 0;
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Rx;
  }

  void txQueueTelemetry_(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inBytes = 0;
    data.outBytes = m_txDiag.bytesTx.load_();
    data.inCount = 0;
    data.outCount = m_txDiag.packetsTx.load_();
    data.count = txQueueCount_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Tx;
  }

public:
  enum { DefltTxQueueMax = 4096 };

  using Rx = ZmPQRx<Self, DataRxPQueue, ZmPQRxGapIgnore<>>;
  using RxMsg = DataRxPQueue::Node;
  using RxQueueSpan = DataRxPQueue::Span;
  using TxUnackdQueue = DataTxPQueue;


  uint64_t txBytes() const { return m_txBytes; }
  uint64_t txBufferedBytes() const { return m_txBufferedBytes; }
  uint64_t txCreditLimit() const { return m_txDataCredit.limit(); }
  uint64_t txCreditAvailable() const { return m_txDataCredit.available(); }
  // Initial peer connection window; upper framed protocols use it to avoid
  // declaring a frame which cannot cross a batched-credit boundary.
  uint64_t txFrameMax() const { return m_txFrameMax; }
  uint64_t rxCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxCreditAvailable() const { return m_rxDataCredit.available(); }
  uint64_t rxAdmitted() const { return m_rxAdmitted; }
  uint64_t rxRetired() const { return m_rxRetired; }
  uint64_t rxRetirable() const {
    uint64_t limit = m_resetReceived ? finalSize() : m_rxDelivered;
    return m_rxRetired < limit ? limit - m_rxRetired : 0;
  }
  unsigned txRangeCount() const { return m_txRanges.count_(); }
  unsigned txQueueCount() const {
    return unsigned(m_txQueueCount.load_() & TxQueueState::CountMask);
  }
  bool txAdmissionClosed() const { return m_txQueueCount.load_() & TxQueueState::Closed; }
  void closeTxAdmission() { m_txQueueCount |= TxQueueState::Closed; }
  uint64_t dataGen() const { return m_dataGen.load_(); }
  void txQueueMax(unsigned v) { m_txQueueMax = v; }
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }
  unsigned txUnackdCount() const { return m_txUnackd.count_(); }
  uint64_t txUnackdBytes() const { return m_txUnackd.length_(); }
  uint64_t rxBytes() const { return m_rxDelivered; }
  uint64_t finalSize() const { return m_rxState.finalSize(); }
  unsigned rxPending() const { return m_rxQueue.count_(); }
  uint64_t rxAppError() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx error access outside Rx thread", return 0);
    return m_rxAppError;
  }
  DataError::T rxError() const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx error access outside Rx thread",
      return DataError::None);
    return m_rxError;
  }
  uint64_t txAppError() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection Tx error access outside Tx thread", return 0);
    return m_txAppError;
  }
  DataError::T txError() const {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection Tx error access outside Tx thread",
      return DataError::None);
    return m_txError;
  }
  TcpCloseState::T tcpCloseState() const { return m_tcpCloseState; }
  void adoptCxnSlotRx_(CxnSlot slot) {
    ZiAssert(rxInvoked_(), "Zutcp", (), "connection admission outside Rx", return);
    ZmAssert_(!m_cxnSlot);
    m_cxnSlot = ZuMv(slot);
  }
  void releaseCxnSlotRx_() {
    ZiAssert(rxInvoked_(), "Zutcp", (), "connection admission release outside Rx", return);
    if (auto stack = app()->nativeStack()) stack->releaseCxn(m_cxnSlot);
  }
  bool hasCxnSlotRx_() const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "connection slot inspection outside Rx", return false);
    return bool(m_cxnSlot);
  }
  bool deferCxnRelease() const { return false; }
  void closingCxnRx_() {
    ZiAssert(rxInvoked_(), "Zutcp", (), "connection close admission outside Rx", return);
    if (auto stack = app()->nativeStack()) stack->closingCxn(m_cxnSlot);
  }
  ClosedTupleKey closedKeyRx() const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "closed tuple outside Rx", return {});
    return m_closedKeyRx;
  }
  ClosedTuple closedSnapshotRx(const ClosedTupleKey &key) const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "closed snapshot outside Rx", return {});
    uint32_t next = uint32_t(rxByteNext());
    if (app()->nativeTx() && m_rxOrigins.peerKnown) next += uint32_t(m_rxOrigins.peer + 1);
    return {.key = key, .rxNext = next,
      .fin = m_rxState.finSeen() && m_rxState.complete(), .localCID = m_localSCID};
  }
  void closedSnapshotTx(ClosedTuple &record) const {
    ZiAssert(txInvoked_(), "Zutcp", (), "closed snapshot outside Tx", return);
    record.txNext = uint32_t(m_txNext);
    if (app()->nativeTx() && m_txOrigins.localKnown)
      record.txNext += uint32_t(m_txOrigins.local + 1);
    record.peerCID = m_txPeerCID;
    record.txPN = m_txPN[PktNumSpace::AppData];
  }
  bool finSent() const { return m_fin; }
  bool finDequeued() const { return m_finDequeued; }
  bool txCompleted() const { return m_txCompleted; }
  bool txDrained() const {
    return !txQueueCount() && !m_txUnackd.count_();
  }
  bool finCredit() const {
    return !app()->nativeTx() || m_txBytes < m_txDataCredit.limit();
  }
  bool finReady() const {
    return !m_resetSent && m_fin && !m_finDequeued && !txQueueCount() && finCredit();
  }
  bool finReceived() const { return m_rxState.finSeen(); }
  bool resetSent() const { return m_resetSent; }
  bool resetReceived() const { return m_resetReceived; }
  bool stopSent() const { return m_stopSent; }
  bool resetAckd() const { return m_resetAckd; }
  bool stopAckd() const { return m_stopAckd; }
  bool stopReceived() const { return m_stopReceived; }
  bool readOpen() const { return !m_resetReceived && !rxComplete(); }


  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() const { return m_rx.count_(); }

  Reader &reader() { return m_rx; }

  bool retireRx(uint64_t n) {
    if (!impl() || !retireRxData_(n)) return false;
    if (!mxCxn && !m_mxRxActive) finishNativeRx_();
    return true;
  }
  bool retireRxData_(uint64_t n) {
    if (n > rxRetirable()) return false;
    m_rxRetired += n;
    maybeExtendMaxData();
    return true;
  }

  DataRxPQueue *rxQueue() { return &m_rxQueue; }
  void closeRx() {
    if (impl() && this->app() && this->app()->mx())
      ZiAssert(rxInvoked_(), "Zutcp", (),
	"QUIC connection Rx close outside Rx thread", return);
    while (m_rxQueue.shift());
    m_rx.clean();
  }
  TxUnackdQueue *txUnackdQueue() { return &m_txUnackd; }

  void txCredit(uint64_t limit) {
    m_txDataCredit.set(limit);
    if (!m_txFrameMax) {
      m_txFrameMax = limit;
      if (!uint64_t(m_writeCredit)) m_writeCredit = limit;
    }
  }
  void extendTxCredit(uint64_t limit) {
    m_txDataCredit.extend(limit);
    if (!m_txFrameMax) {
      m_txFrameMax = limit;
      if (!uint64_t(m_writeCredit)) m_writeCredit = limit;
    }
  }
  bool consumeTxCredit(uint64_t n, bool wholePacket = false) {
    bool consumed = wholePacket && app()->nativeTx() ?
      m_txDataCredit.consumePacket(n) : m_txDataCredit.consume(n);
    if (!consumed) return false;
    updatePersist_();
    return true;
  }
  void rxCredit(uint64_t limit) { m_rxDataCredit.set(limit); }
  void extendRxCredit(uint64_t limit) { m_rxDataCredit.extend(limit); }
  bool consumeRxCreditTo(uint64_t n) { return m_rxDataCredit.consumeTo(n); }
  unsigned byteControlCount() const {
    return unsigned(m_resetDataControl.queued) + unsigned(m_stopSendingControl.queued);
  }
  bool controlQueued() const { return byteControlCount(); }
  bool queueReset(uint64_t appError, uint64_t finalSize) {
    return queueControl(
      m_resetDataControl,
      ControlFrame::resetData( appError, finalSize));
  }
  bool queueStopSending(uint64_t appError) {
    return queueControl(
      m_stopSendingControl,
      ControlFrame::stopSending( appError));
  }
  bool nextQueuedControl(ControlFrame &frame) const {
    if (m_resetDataControl.queued) {
      frame = m_resetDataControl.frame;
      return true;
    }
    if (m_stopSendingControl.queued) {
      frame = m_stopSendingControl.frame;
      return true;
    }
    return false;
  }
  void controlSent(const ControlFrame &frame) {
    PendingControl *slot = controlSlot(frame.type);
    if (slot && slot->frame == frame) slot->queued = false;
  }
  void clearControl(const SentFrameRef &ref) {
    PendingControl *slot = controlSlot(ref.controlType);
    if (!slot) return;
    ControlFrame frame = controlFrame_(ref);
    if (slot->frame == frame) *slot = {};
  }
  void clearControls() {
    m_resetDataControl = {};
    m_stopSendingControl = {};
  }
  bool controlStillValid(const ControlFrame &frame) const {
    const PendingControl *slot = controlSlot(frame.type);
    if (!slot || !(slot->frame == frame)) return false;
    switch (frame.type) {
      case FrameType::Reset:
	return resetSent() && !resetAckd() && frame.value == m_txDataCredit.used();
      case FrameType::StopSending:
	return stopSent() && !stopAckd();
      default:
	return false;
    }
  }

  void process(RxMsg *msg) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection process outside Rx thread", return);
    if (!msg) return;
    DataRx &data = msg->data();
    if (!data.length) return;
    ZiAssert(data.skip + data.length <= data.size, "Zutcp", (),
      "connection Rx queued slice exceeds packet-backed range", return);
    m_rxDelivered += data.length;
    m_rxState.delivered(m_rxDelivered);
    ZmRef<ZiRxQueue::Node> buf =
      static_cast<ZiRxQueue::Node *>(msg);
    m_rx.push(ZuMv(buf));
  }
  void request(const RxQueueSpan &, const RxQueueSpan &) { }
  void scheduleDequeue() { rxRun_([cxn = ZmRef<Self>{impl()}, gen = m_rxDataGen]() {
    if (gen == cxn->m_rxDataGen) cxn->dequeueRx_();
  }); }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }
  void dequeueRx_() {
    Rx::dequeue();
    if (impl() && !m_rx.empty())
      this->rxDequeued();
  }

  bool txRange(unsigned i, TxRange &range) const {
    auto iter = m_txRanges.citer();
    while (auto node = iter()) {
      if (i--) continue;
      const auto &data = node->data();
      range = TxRange{node, data.offset, data.bytes, data.dataOffset};
      return true;
    }
    return false;
  }
  bool nextTxRange(PktBudget &, TxRange &range, bool &fin) const {
    fin = false;
    if (txRange(0, range)) return true;
    if (!finReady()) return false;
    range = {};
    range.dataOffset = m_txBytes;
    fin = true;
    return true;
  }
  bool dequeueTxRange(TxRange &range) {
    TxRange selected;
    if (!txRange(0, selected)) return false;
    return consumeTxRange(range, selected, selected.length);
  }
  bool commitTxRange(TxRange &range, uint32_t length) {
    return consumeTxRange(range, length);
  }
  bool consumeTxRange(TxRange &range, uint32_t length) {
    TxRange selected;
    return txRange(0, selected) && consumeTxRange(range, selected, length);
  }
  bool commitTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    return consumeTxRange(range, selected, length);
  }
  bool consumeTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    if (!length) return false;
    if (!selected.storage) return false;
    uint64_t key = selected.dataOffset;
    auto part = m_txRanges.find(key);
    if (!part) return false;
    const auto &stored = part->data();
    if (part.ptr() != selected.storage.ptr() || stored.offset != selected.offset ||
	stored.bytes != selected.length || length > stored.length()) return false;
    auto node = m_txRanges.del(key);
    range = selected;
    range.length = length;
    if (range.storage->native) {
      unsigned prepared = range.offset + length - range.storage->payloadOffset;
      if (range.storage->preparedLength < prepared) range.storage->preparedLength = prepared;
    }
    m_txBufferedBytes -= length;
    if (length < node->data().length()) {
      node->data().clipHead(length);
      m_txRanges.add(ZuMv(node));
    } else {
      --m_txQueueCount;
      // Return producer credit after this packetizer turn has committed flow
      // credit, FIN and recovery metadata. Recovery retains the plaintext until
      // ACK independently of the now-empty application queue.
      if constexpr (!ZuIsSame<decltype(&Impl::txDequeued_), void (Self::*)()>{}) {
	if (impl()) this->app()->txRun([
	  cxn = impl(), gen = dataGen()
	]() mutable {
	  if (gen == cxn->dataGen() && !cxn->disconnecting_() && !cxn->resetSent() &&
	      cxn->txError() == DataError::None)
	    cxn->txDequeued_();
	});
      }
    }
    updatePersist_();
    return true;
  }

  bool dequeueFin(uint64_t &offset) {
    if (!finReady()) return false;
    offset = m_txBytes;
    m_finDequeued = true;
    return true;
  }
  bool recordTxUnackd(uint64_t offset, uint64_t length, bool fin) {
    if (!length && !fin) return false;
    return m_txUnackd.add(new TxUnackdQueue::Node{
      TxUnackdRange{offset, length, fin}}) != ZmPQResult::Invalid;
  }
  bool discardTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    return n && m_txUnackd.clear(offset, n);
  }
  bool ackTxUnackd(
    uint64_t offset, uint64_t length, bool fin = false, const TxRange *range = nullptr) {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n || !m_txUnackd.clear(offset, n)) return false;
    if (range && range->storage && range->storage->native) {
      auto source = range->storage.ptr();
      uint64_t base = range->dataOffset - (range->offset - source->payloadOffset);
      if (*source->backing && source->preparedLength == source->payloadLength &&
	  !txStillUnackd(base, source->payloadLength, source->fin)) {
	// Historical packet records keep identity for late ACKs, not live data.
	// Publish capacity before producer callbacks; kernel use still gates reuse.
	source->data = nullptr;
	source->backing.reset();
      }
    }
    if (fin) {
      m_finAckd = true;
      updateCloseTimers_();
    }
    if (txDrained()) impl()->txDrained_();
    if (m_finAckd && !m_txUnackd.count_() && !m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete(true);
    }
    return true;
  }
  bool txStillUnackd(uint64_t offset, uint64_t length, bool fin = false) const {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n) return false;
    bool found = false;
    (void)m_txUnackd.spans(offset, n, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool ackReset(uint64_t appError, uint64_t finalSize) {
    if (!m_resetSent || m_txDataCredit.used() != finalSize) return false;
    m_resetAckd = true;
    return true;
  }
  bool ackStop(uint64_t appError) {
    if (!m_stopSent) return false;
    m_stopAckd = true;
    return true;
  }

  template <bool AppThread>
  class Writer : public Zi::TxStream<Writer<AppThread>> {
    using Base = Zi::TxStream<Writer<AppThread>>;

  public:
    Writer(Connection &cxn) : Writer{cxn, cxn.app()->dataTxAllocator()} { }

    uint64_t frameMax() const { return m_cxn->txFrameMax(); }
    uint64_t gen() const { return m_gen; }

    ~Writer() { this->flush(); }
    Writer(Writer &&) = default;
    Writer &operator =(Writer &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      if (!m_allocator || skip < m_allocator->headRoom || skip > this->maxSize() ||
	  m_gen != m_cxn->dataGen() || m_cxn->txAdmissionClosed()) return nullptr;
      unsigned mps = this->maxSize() - this->headRoom() - this->tailRoom();
      auto buf = m_allocator->allocBuf(m_cxn, 0, mps);
      if (buf) { buf->skip = skip; buf->length = 0; }
      return buf;
    }

    bool appendBuf(ZmRef<ZiIOBuf> buf) {
      if (!Base::flush()) return false;
      if (buf && !buf->failed() && !buf->length) return true;
      bool ok = sendBuf_(ZuMv(buf), false);
      if (!ok) this->fail();
      return ok;
    }
    using Base::operator <<;
    Writer &operator <<(ZmRef<ZiIOBuf> buf) {
      appendBuf(ZuMv(buf));
      return *this;
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      if (m_gen != m_cxn->dataGen()) return false;
      if constexpr (AppThread) return m_cxn->enqueue(ZuMv(buf), m_gen);
      else return m_cxn->enqueue_(ZuMv(buf));
    }

  private:
    static unsigned capacity(Connection &cxn, Zi::IOBufAllocator *a) {
      if (!a) return 0;
      if (a->type != Zi::IOBufType::TxTCP) return a->size;
      unsigned mps = unsigned(cxn.m_writeMPS);
      uint64_t credit = cxn.m_writeCredit;
      if (uint64_t(mps) > credit) mps = unsigned(credit);
      if (mps > a->capacity()) mps = a->capacity();
      return mps ? a->headRoom + mps + a->tailRoom : 0;
    }
    Writer(Connection &cxn, Zi::IOBufAllocator *a) :
      Base(capacity(cxn, a), a ? a->headRoom : 0, a ? a->tailRoom : 0),
      m_cxn{&cxn}, m_gen{cxn.dataGen()}, m_allocator{a}
    {
      if (!a || this->maxSize() <= this->headRoom() + this->tailRoom()) this->fail();
    }
    // immutable
    Connection	*m_cxn;
    uint64_t	m_gen;
    Zi::IOBufAllocator *m_allocator;
  };

  auto writer() {
    ZmAssert(!txInvoked_());
    return Writer<true>{*this};
  }
  Writer<false> writer_() { // direct call from within tx thread
    ZmAssert(txInvoked_());
    return Writer<false>{*this};
  }

  void fin() {
    if (m_fin || m_resetSent) return;
    if (app()->nativeTx()) closeTxAdmission();
    m_fin = true;
    if (app()->nativeStack())
      app()->rxRun([link = impl(), gen = m_txDataGen] {
	if (gen == link->m_rxDataGen && !link->disconnecting_()) link->closingCxnRx_();
      });
    updatePersist_();
    updateCloseTimers_();
    notifyTx_();
  }
  void reset(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection reset outside Tx thread", return);
    if (m_resetSent) return;
    if (app()->nativeTx()) {
      impl()->sendTCPReset_();
      closeStateTx();
      impl()->tcpResetAbort_(false);
      return;
    }
    m_resetSent = true;
    setTxError(DataError::Reset, appError);
    // Queued application bytes may exceed peer credit. Final size covers only
    // the extent committed to DATA frames, including any lost packets.
    auto finalSize = m_txDataCredit.used();
    // Packet recovery retains its own references. A reset forbids further
    // DATA retransmission and releases all connection-owned queued ranges now.
    unsigned queued = txRangeCount();
    m_txRanges.clean();
    m_txQueueCount -= queued; // Pending off-shard admissions still retire once.
    m_txBufferedBytes = 0;
    m_txUnackd.clean();
    m_dataBlockedControl = {};
    if (impl())
      queueReset(appError, finalSize);
    if (impl())
      impl()->txReset(appError, finalSize);
    if (!m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete(false);
    }
    notifyTx_();
  }
  void stop(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection stop outside Tx thread", return);
    if (m_stopSent) return;
    m_stopSent = true;
    rxRun_([cxn = ZmRef<Impl>{impl()}, appError, gen = dataGen()]() mutable {
      if (gen == cxn->dataGen()) cxn->applyStopRx(appError);
    });
    if (impl())
      queueStopSending(appError);
    if (impl())
      impl()->txStopped(appError);
    notifyTx_();
  }

  bool receiveDataFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    return receiveDataFrame_(frame, diag, ZuMv(packet));
  }
  int processFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *refused = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveDataFrame_(frame, diag, ZuMv(packet), refused)) return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx_();
  }
  int processRx_() {
    if (mxCxn) return recvMx_();
    uint64_t gen = m_rxDataGen;
    m_mxRxActive = true;
    int result = impl()->process(m_rx);
    m_mxRxActive = false;
    if (result >= 0 && gen == m_rxDataGen) finishNativeRx_();
    return result;
  }

  void abortTCPMx_() {
    ZiAssert(txInvoked_(), "Zutcp", (), "TCP abrupt close outside Tx", return);
    if (!app()->nativeTx() || m_mxFinishing) return;
    impl()->sendTCPReset_();
    closeStateTx();
  }

  bool abortingRx_() const {
    ZiAssert(rxInvoked_(), "Zutcp", (), "native abort state outside Rx", return false);
    return m_rxAborting;
  }
  template <typename L> void abortLocalRx_(
    uint64_t error, L &&drain, bool application = false)
  {
    ZiAssert(rxInvoked_(), "Zutcp", (), "local native abort outside Rx", return);
    if (!m_rxAborting) {
      closingCxnRx_();
      m_rxAborting = true;
      closeDataRx();
      cancelTimers();
    }
    // Preserve completion callbacks from repeated explicit abort requests. All
    // output decisions remain ordered on Tx before the existing Rx owner drain.
    app()->txRun([link = ZmRef<Impl>{impl()}, error, application, gen = m_rxDataGen,
      drain = ZuFwd<L>(drain)]() mutable {
      if (gen != link->m_txDataGen) return;
      link->closeTxAdmission();
      if (!link->disconnecting_()) {
	link->sendTCPReset_();
	if (!link->linkClosedTx_()) link->closeStateTx(error, application);
      }
      link->app()->rxRun([link = ZuMv(link), gen, drain = ZuMv(drain)]() mutable {
	if (gen == link->m_rxDataGen) drain();
      });
    });
  }

  void closeMxRx_() {
    ZiAssert(rxInvoked_(), "Zutcp", (), "mx receive close intent outside Rx", return);
    if (!mxCxn || disconnecting_()) return;
    m_mxRxClosing = true;
    recvMx_();
  }

  void shutdownMx_(bool peer = false) {
    ZiAssert(txInvoked_(), "Zutcp", (), "mx FIN initiation outside Tx", return);
    if (!mxCxn || disconnecting_()) return;
    mxCxn->txUp() = 0;
    mxCxn->txContext().complete();
    if (!m_fin) {
      m_mxClosePeer = peer;
      fin(); // Existing packetizer drains admitted ranges before FIN.
    }
    finishMxTx_();
  }

private:
  // Shared native/plugin graceful retirement checkpoints.
  enum { MxEOF = 1, MxFINAck = 2, MxFinishPosted = 4 };
  void finishNativeRx_() {
    if (!app()->nativeTx() || disconnecting_() || m_rxAborting ||
	!finReceived() || !rxComplete() || !m_rx.empty()) return;
    m_mxRxFinish |= MxEOF;
    finishMxRx_();
  }
  void finishMxRx_() {
    if ((!mxCxn && !app()->nativeTx()) ||
	m_mxRxFinish != (MxEOF | MxFINAck)) return;
    m_mxRxFinish |= MxFinishPosted;
    app()->txRun([cxn = impl(), gen = m_rxDataGen] {
      if (gen != cxn->m_txDataGen || cxn->disconnecting_()) return;
      cxn->m_mxPeerFinished = true;
      cxn->finishMxTx_();
    });
  }
  void finishMxTx_() {
    if ((!mxCxn && !app()->nativeTx()) || disconnecting_() || m_mxFinishing ||
	!m_finAckd || m_txUnackd.count_() || !m_mxPeerFinished) return;
    m_mxFinishing = true;
    // Both byte directions are complete; the last peer-FIN ACK was accepted.
    // Role-specific close uses the existing owner drain and common closed history.
    if (mxCxn) app()->mx()->plugin()->close(mxCxn, m_mxClosePeer);
    else impl()->nativeFinished_();
  }

public:
  void nativeFinished_() { }

  int recvMx_() {
    ZiAssert(rxInvoked_(), "Zutcp", (), "mx receive outside Rx thread", return -1);
    if (m_mxRxActive) { m_mxRxPending = true; return 0; }
    m_mxRxActive = true;
    int result = 0;
    while (auto cxn = mxCxn) {
      auto &context = cxn->rxContext();
      if (m_mxRxClosing && context.completed()) {
	// Explicit local disconnect abandons a completed application's reader.
	// Preserve pending reassembly until contiguous DATA/FIN reaches it.
	m_rx.clean();
	if (!retireRx(rxRetirable())) result = -1;
	break;
      }
      if (context.completed() || !context.buf || m_rx.empty()) break;
      auto span = m_rx.span();
      unsigned length = span.length();
      if (!context.buf->data_()) {
	ZmRef<ZiIOBuf> node;
	auto n = m_rx.extract(
	  [](auto part) -> int64_t { return part.length(); },
	  []() -> ZmRef<ZiRxQueue::Node> { return {}; }, node);
	ZmAssert_(n == length && node);
	if (!static_cast<DataRx *>(node.ptr())->attach(context.buf)) {
	  result = -1;
	  break;
	}
      } else {
	auto buf = context.buf.ptr();
	if (length > UINT32_MAX - buf->length) { result = -1; break; }
	auto ptr = buf->ensure(buf->length + length);
	if (!ptr) { result = -1; break; }
	memcpy(ptr + buf->length, span.data(), length);
	buf->length += length;
	m_rx.advance(length);
      }
      if (!retireRx(length)) { result = -1; break; }
      cxn->rxCall(length);
      result = 1;
      bool stop = context();
      if (mxCxn != cxn) break;
      if (context.disconnected()) { cxn->disconnect(); break; }
      if (stop) break;
    }
    m_mxRxActive = false;
    if (mxCxn && rxComplete() && m_rx.empty() && !(m_mxRxFinish & MxEOF)) {
      m_mxRxFinish |= MxEOF;
      mxCxn->rxUp() = 0;
      mxCxn->rxContext().complete();
      app()->txRun([cxn = impl(), gen = m_rxDataGen] {
	if (gen == cxn->m_txDataGen && !cxn->disconnecting_()) cxn->shutdownMx_(true);
      });
      finishMxRx_();
    }
    if (m_mxRxPending) {
      m_mxRxPending = false;
      app()->mx()->push([cxn = ZmRef<Impl>{impl()}, gen = dataGen()] {
	if (gen == cxn->dataGen() && cxn->mxCxn) cxn->recvMx_();
      }, app()->mx()->rxThread());
    }
    return result;
  }

  void sendMx_() {
    ZiAssert(txInvoked_(), "Zutcp", (), "mx send outside Tx thread", return);
    if (!mxCxn || disconnecting_()) return;
    if (m_fin) { mxCxn->txContext().complete(); return; }
    if (m_mxTxActive) { m_mxTxPending = true; return; }
    m_mxTxActive = true;
    auto mx = mxCxn;
    auto &context = mx->txContext();
    while (!context.completed() && context.buf && !disconnecting_()) {
      auto buf = context.buf;
      unsigned length = buf->length;
      if (buf->failed() || context.offset > length) {
	context.length = -1;
	context();
	mx->close();
	break;
      }
      unsigned n = length - context.offset;
      if (n) {
	if (txQueueCount() >= txQueueMax()) break;
	if (!enqueue_(buf, context.offset)) {
	  context.length = -1;
	  context();
	  mx->close();
	  break;
	}
	mx->txCall(n);
      }
      context.length = int(n);
      do {
	if (context.completed()) break;
      } while (!context());
      if (context.disconnected()) { mx->disconnect(); break; }
      if (context.completed() || !context.buf) break;
      // Completion can install another buffer or reinitialize the same one.
      if (context.buf == buf && (!buf->data_() || context.offset >= buf->length)) {
	context.complete();
	break;
      }
    }
    m_mxTxActive = false;
    if (m_mxTxPending) {
      m_mxTxPending = false;
      app()->txRun([cxn = ZmRef<Impl>{impl()}, gen = dataGen()] {
	if (gen == cxn->dataGen() && cxn->mxCxn && !cxn->disconnecting_()) cxn->sendMx_();
      });
    }
  }

  bool receiveReset(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC RESET_DATA receive outside Rx thread", return false);
    if (frame.type != FrameType::Reset ||
	m_rxDelivered > frame.length ||
	rxPendingBeyond(frame.length))
      return false;
    RxState next = m_rxState;
    if (!next.receive(frame.length, 0, true)) return false;
    m_rxState = next;
    m_resetReceived = true;
    setRxError(DataError::Reset, frame.errorCode);
    return true;
  }

  bool receiveStop(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC STOP_SENDING receive outside Rx thread", return false);
    if (frame.type != FrameType::StopSending)
      return false;
    m_stopReceived = true;
    uint64_t appError = frame.errorCode;
    txInvoke_([cxn = ZmRef<Impl>{impl()}, appError, gen = dataGen()]() mutable {
      if (gen == cxn->dataGen()) cxn->completeStopTx(appError);
    });
    return true;
  }

  void completeStopTx(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC STOP_SENDING completion outside Tx thread", return);
    setTxError(DataError::Stop, appError);
    if (!m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete(false);
    }
  }

  void txComplete(bool) { }
  void txDrained_() { }
  // AppTx, once per fully packetized source buffer, on a later dispatch turn.
  // This returns producer queue credit; it does not indicate peer ACK receipt.
  void txDequeued_() { }

private:
  static void setError_(uint64_t &appError, DataError::T &error,
      DataError::T next, uint64_t nextAppError) {
    if (error == DataError::Reset ||
	(error == DataError::Stop && next != DataError::Reset))
      return;
    error = next;
    appError = nextAppError;
  }
  void setRxError(DataError::T error, uint64_t appError) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx error update outside Rx thread", return);
    setError_(m_rxAppError, m_rxError, error, appError);
  }
  void setTxError(DataError::T error, uint64_t appError) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection Tx error update outside Tx thread", return);
    setError_(m_txAppError, m_txError, error, appError);
  }
  void applyStopRx(uint64_t appError) {
    setRxError(DataError::Stop, appError);
  }

  bool receiveDataFrame_(
    const Frame &frame, BufDiag *diag, ZmRef<ZiIOBuf> packet,
    bool *refused = nullptr) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection receive outside Rx thread", return false);
    if (frame.type != FrameType::Data ||
	frame.length != frame.payload.length() ||
	frame.length > BufSize)
      return false;
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;

    if (!m_rxState.validate(frame.offset, frame.length, frame.fin))
      return false;
    if (frame.fin && rxPendingBeyond(end)) return false;

    auto spans = ZmScratch(
      RxSpan, m_rxQueue.count_() + 1, RxSpans::VHeap);
    if (frame.length && !newRxSpans(frame, spans)) return false;
    uint64_t newBytes = rxSpanBytes(spans);
    if (end > m_rxDataCredit.limit()) return false;
    bool heap = false;
    if (spans && !packet) return false;
    if (spans && !app()->retainRx(packet, m_rxAccount, m_rxDataGen)) {
      // Contiguous service must remain possible even when application-held
      // frames fill both admission caps. Copy only this bounded packet's novel
      // slices to the existing Rx heap; byte credit still bounds queued data.
      // Out-of-order input consumes no service reserve and commits no ACK.
      if (spans[0].first != m_rxDelivered) {
	if (refused) *refused = true;
	return false;
      }
      heap = true;
    }
    if (!m_rxDataCredit.consumeTo(end)) return false;

    bool complete = m_rxState.complete();
    if (spans)
      if (!queueRxSlices(frame, spans, ZuMv(packet), diag, heap)) return false;

    if (!m_rxState.receive(frame.offset, frame.length, frame.fin)) return false;
    uint64_t next = m_rxState.delivered();
    auto contiguous = m_rxQueue.citer(next);
    while (auto node = contiguous()) {
      const auto &data = node->data();
      if (data.offset > next) break;
      uint64_t end = data.offset + data.length;
      if (end > next) next = end;
    }
    m_rxState.delivered(next);
    if (!complete && m_rxState.complete()) {
      closingCxnRx_();
      app()->txRun([cxn = impl(), gen = m_rxDataGen] { cxn->peerFinTx_(gen); });
    }
    m_rxAdmitted += newBytes;
    return true;
  }

  template <typename Spans>
  bool newRxSpans(const Frame &frame, Spans &spans) const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx span check outside Rx thread", return false);
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_rxDelivered) return true;

    uint64_t first = frame.offset < m_rxDelivered ? m_rxDelivered : frame.offset;
    return rxNovelSpans(m_rxQueue, first, end, spans);
  }

  template <typename Spans>
  bool queueRxSlices(
    const Frame &frame, const Spans &spans,
    ZmRef<ZiIOBuf> packet, BufDiag *diag, bool heap = false) {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx queue outside Rx thread", return false);
    if (!packet) return false;
    for (unsigned i = 0, n = spans.length(); i < n; ++i) {
      uint64_t payloadOffset = spans[i].first - frame.offset;
      uint64_t length64 = spans[i].length();
      if (payloadOffset > frame.payload.length() ||
	  length64 > frame.payload.length() - payloadOffset)
	return false;
      auto data = frame.payload.data() + payloadOffset;
      ZmRef<RxMsg> node = new RxMsg{
	packet, data, unsigned(length64), app()->dataRxAllocator(),
	this, spans[i].first};
      // Force the existing ensure replacement path before handing off a
      // service slice. Application retention can then pin only heap storage.
      if (heap && !node->ensure(unsigned(length64) + 1)) return false;
      Rx::rcvd(ZuMv(node));
    }
    return true;
  }

  bool rxPendingBeyond(uint64_t finalSize) const {
    ZiAssert(rxInvoked_(), "Zutcp", (),
      "QUIC connection Rx pending check outside Rx thread", return false);
    auto iter = m_rxQueue.citer();
    while (auto node = iter()) {
      const DataRx &data = node->data();
      if (data.offset > finalSize || data.length > finalSize - data.offset)
	return true;
    }
    return false;
  }

public:
  bool enqueue(ZmRef<ZiIOBuf> buf) {
    return enqueue(ZuMv(buf), dataGen());
  }

protected:
  bool enqueue(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    if (!buf || buf->failed() || !buf->length) return false;
    if (gen != dataGen()) return false;
    if (!admitTx()) return false;
    // Pending admissions keep their reserved credit through reset and retire
    // it once on Tx, even when their generation no longer matches.
    txInvoke_([cxn = ZmRef<Impl>{impl()}, buf = ZuMv(buf), gen]() mutable {
      cxn->sendAdmitted_(ZuMv(buf), gen);
    });
    return true;
  }
public:
  bool enqueue_(ZmRef<ZiIOBuf> buf, unsigned offset = 0) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection send outside Tx thread", return false);
    if (!admitTx()) return false;
    return sendAdmitted_(ZuMv(buf), dataGen(), offset);
  }

private:
  bool admitTx() {
    uint64_t state = m_txQueueCount.load_();
    for (;;) {
      if (state & TxQueueState::Closed) return false;
      if (state >= m_txQueueMax)
	return sendError("QUIC connection transmit queue limit exceeded");
      uint64_t previous = m_txQueueCount.cmpXch(state + 1, state);
      if (previous == state) return true;
      state = previous;
    }
  }
  void retireTxAdmission_() {
    ZiAssert(txInvoked_(), "Zutcp", (), "Tx admission retirement outside Tx", return);
    ZmAssert_(txQueueCount());
    uint64_t remaining = --m_txQueueCount;
    // A late handoff can be the last work blocking the current generation's FIN.
    if (!(remaining & TxQueueState::CountMask) && m_fin && !disconnecting_()) {
      dataWritable();
      impl()->queueTxFlush();
    }
  }
  bool sendAdmitted_(ZmRef<ZiIOBuf> buf, uint64_t gen, unsigned offset = 0) {
    ZiAssert(txInvoked_(), "Zutcp", (),
      "QUIC connection send_ outside Tx thread",
      --m_txQueueCount; return false);
    if (disconnecting_() || gen != m_txDataGen || gen != dataGen()) {
      retireTxAdmission_();
      return false;
    }
    if (ZuUnlikely(!buf || buf->failed() || m_resetSent || offset >= buf->length)) {
      retireTxAdmission_();
      return sendError("QUIC connection is closed for transmission");
    }
    ZiAssert(buf->skip + buf->length <= buf->size, "Zutcp", (),
      "connection Tx buffer range violation",
      retireTxAdmission_(); return false);
    uint32_t length = buf->length - offset;
    auto allocator = buf->allocator();
    auto native = buf->registered() && allocator->mx == app()->mx() &&
      allocator->type == Zi::IOBufType::TxTCP ? allocator : nullptr;
    auto pool = app()->txPool(native);
    if (app()->nativeTx() && !pool) {
      retireTxAdmission_();
      return sendError("native transport requires registered Tx backing from this mx");
    }
    ZmRef<TxRanges::Node> node = new TxRanges::Node{ZuMv(buf), m_txBytes, offset};
    node->native = native;
    node->pool = pool;
    m_txRanges.add(ZuMv(node));
    m_txBytes += length;
    m_txBufferedBytes += length;
    notifyTx_();
    return true;
  }

  bool sendError(ZuCSpan message) {
    auto gen = dataGen(); // The error callback may replace this incarnation.
    auto e = ZeEXCEPT(Error, "Zutcp", message);
    if (m_txErrorFn && !m_txErrorFn(e) && impl()) {
      if (app()->nativeTx())
	rxRun_([link = ZmRef<Impl>{impl()}, gen] {
	  if (gen != link->dataGen() || link->disconnecting_()) return;
	  link->abortLocalRx_(0, [link = link.ptr()] { link->tcpResetAbort_(false); });
	});
      else this->disconnect();
    }
    return false;
  }

public:

  template <typename L>
  void txInvoke_(L &&l) {
    ZiAssert(impl() && this->app() && this->app()->mx(),
      "Zutcp", (), "QUIC connection Tx invoke before app initialization",
      return);
    this->app()->txInvoke(ZuFwd<L>(l));
  }
  template <typename L>
  void rxRun_(L &&l) {
    ZiAssert(impl() && this->app() && this->app()->mx(),
      "Zutcp", (), "QUIC connection Rx run before app initialization",
      return);
    this->app()->rxRun(ZuFwd<L>(l));
  }

  void notifyTx_() { dataWritable(); impl()->flushTx_(); }

  bool queueControl(PendingControl &slot, const ControlFrame &frame) {
    if (!frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame &&
	frame.type != FrameType::Reset &&
	frame.type != FrameType::StopSending &&
	slot.frame.value >= frame.value)
      return false;
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  PendingControl *controlSlot(FrameType::T type) {
    switch (type) {
      case FrameType::Reset: return &m_resetDataControl;
      case FrameType::StopSending: return &m_stopSendingControl;
      default: return nullptr;
    }
  }
  const PendingControl *controlSlot(FrameType::T type) const {
    return const_cast<Self *>(this)->controlSlot(type);
  }
private:
  // immutable

  // shared admission state and published configuration
  ZmAtomic<uint64_t>	m_txQueueCount = 0;
  // Reset invalidates posted byte work before either owner clears its state.
  ZmAtomic<uint64_t>	m_dataGen = 0;
  unsigned		m_txQueueMax = DefltTxQueueMax;
  ZiTxErrorFn		m_txErrorFn;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  uint64_t		m_rxDelivered = 0;
  uint64_t		m_rxDataGen = 0;
  TCPOrigins		m_rxOrigins;
  bool			m_rxSynAcked = false;
  uint64_t		m_rxAdmitted = 0;
  uint64_t		m_rxRetired = 0;
  uint64_t		m_rxWindowSerial = 0;
  bool			m_mxRxActive = false, m_mxRxPending = false;
  uint8_t		m_mxRxFinish = 0;
  bool			m_mxRxClosing = false;
  CxnSlot		m_cxnSlot;
  ClosedTupleKey	m_closedKeyRx;
  bool			m_closedSavedRx = false;
  bool			m_rxAborting = false;
  bool			m_resetReceived = false;
  bool			m_stopReceived = false;
  RxState		m_rxState;
  Reader		m_rx;
  DataRxPQueue	m_rxQueue{0};
  ZmRef<RxAccount>	m_rxAccount;
  uint64_t		m_rxAppError = 0;
  DataError::T	m_rxError = DataError::None;

  // Tx thread exclusive
  bool			m_mxTxActive = false, m_mxTxPending = false;
  bool			m_tcpResetSent = false, m_tcpResetPeer = false;
  bool			m_mxPeerFinished = false, m_mxFinishing = false, m_mxClosePeer = false;
  alignas(Zm::CacheLineSize)
  TxRanges	m_txRanges{0};
  uint64_t		m_txDataGen = 0;
  TCPOrigins		m_txOrigins;
  bool			m_tcpSynSent = false;
  uint64_t		m_txBytes = 0;
  uint64_t		m_txNext = 0, m_txAckd = 0, m_txRttFence = 0;
  uint64_t		m_txRtoEnd = 0;
  uint64_t		m_txBufferedBytes = 0;
  uint64_t		m_txFrameMax = 0;
  ZmAtomic<unsigned>	m_writeMPS{0};
  ZmAtomic<uint64_t>	m_writeCredit{0};
  TcpCloseState::T	m_tcpCloseState = TcpCloseState::Open;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_finAckd = false;
  bool			m_txCompleted = false;
  bool			m_resetSent = false;
  bool			m_resetAckd = false;
  bool			m_stopSent = false;
  bool			m_stopAckd = false;
  uint64_t		m_txAppError = 0;
  DataError::T	m_txError = DataError::None;
  PendingControl	m_resetDataControl;
  PendingControl	m_stopSendingControl;
  DataTxPQueue	m_txUnackd{0};
  bool			m_txReady = false;

  // immutable
  App			*m_app = nullptr;
  const ZuID		m_id;
  bool			m_isServer = false;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;
  // Advisory public/telemetry snapshot; protocol code uses owner-local state.
  ZmAtomic<unsigned>	m_publicLinkState = unsigned(LinkState::Closed);
  ZmAtomic<uint64_t>	m_publicCloseError = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  LinkCloseState	m_rxAppClose;
  FlowCredit		m_rxDataCredit;
  LinkRxDiag		m_rxDiag;
  TransportParams	m_transportParams;
  TransportParams	m_peerParams;
  bool			m_peerParamsSet = false;
  CxnID			m_initialDCID;
  CxnID			m_origDCID;
  CxnID			m_groupID;
  CxnID			m_localSCID;
  CxnID			m_peerCID;
  bool			m_localCIDRouted = false;
  ZuArray<uint64_t, PktNumSpace::N>
			m_rxLargestPN =
			  fixedArray<uint64_t, PktNumSpace::N>();
  AckManager		m_rxAcks;
  ZuArray<AckPost, PktNumSpace::N>
			m_ackPost =
			  ZuArray<AckPost, PktNumSpace::N>(
			    PktNumSpace::N);
  ZuArray<bool, PktNumSpace::N>
			m_rxSpaceDiscarded =
			  fixedArray<bool, PktNumSpace::N>();
  LinkState::T		m_rxLinkState = LinkState::Closed;
  unsigned		m_suspiciousDataFrames = 0;
  bool			m_suspiciousDataClosed = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  FlowCredit		m_txDataCredit;
  uint64_t		m_txRxNext = 0, m_txRxLimit = 0, m_txRxSerial = 0;
  uint32_t		m_txWindowSeq = 0, m_txWindowAck = 0;
  bool			m_windowDirty = false, m_txWindowSet = false, m_peerWindowZero = false;
  PeerTxParams		m_peerTxParams;
  LinkCloseState	m_txAppClose;
  LinkState::T		m_txLinkState = LinkState::Closed;
  RttEstimator		m_rtt;
  PTOBackoff		m_ptoBackoff;
  DataRto		m_dataRto;
  // Connection-owned timers; callbacks run on Tx.
  ZmScheduler::Timer	m_connectTimer;
  ZmScheduler::Timer	m_userTimer;
  ZmScheduler::Timer	m_closeDrainTimer, m_finWaitTimer;
  ZmScheduler::Timer	m_ackDelayTimer;
  ZmScheduler::Timer	m_lossTimer;
  ZmScheduler::Timer	m_ptoTimer;
  ZmScheduler::Timer	m_rtoTimer;
  ZmScheduler::Timer	m_persistTimer;
  ZmScheduler::Timer	m_idleTimer;
  ZmScheduler::Timer	m_heartBeatTimer;
  ZmScheduler::Timer	m_closeTimer;
  // Active-path timers owned by this Connection; callbacks run on Tx.
  ZmScheduler::Timer	m_pmtudTimer;
  ZuTime		m_connectTimerOut;
  ZuTime		m_userTimerOut;
  ZuTime		m_closeDrainTimerOut, m_finWaitTimerOut;
  ZuTime		m_ackDelayTimerOut;
  ZuTime		m_lossTimerOut;
  ZuTime		m_ptoTimerOut;
  ZuTime		m_rtoTimerOut;
  ZuTime		m_persistTimerOut;
  ZuTime		m_persistInterval;
  ZuTime		m_idleTimeout;
  ZuTime		m_idleTimerOut;
  ZuTime		m_idleBase;
  ZuTime		m_heartBeatTimerOut;
  ZuTime		m_heartBeatBase;
  ZuTime		m_closeTimerOut;
  ZuTime		m_closeNextResponse;
  ZuTime		m_pmtudTimerOut;
  PktNumSpace::T	m_ptoTimerLevel = PktNumSpace::Initial;
  bool			m_idleAckElicitingSent = false;
  uint64_t		m_lastDataBlocked = U64Null;
  PendingControl	m_maxDataControl;
  PendingControl	m_dataBlockedControl;
  LinkTxDiag		m_txDiag;
  NewReno		m_congestion;
  uint64_t		m_txRuntimeGen = 0;
  ZuArray<uint64_t, PktNumSpace::N>
			m_txPN =
			  fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N>
			m_txLargestAckd =
			  fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<PktTxSpace, PktNumSpace::N>
			m_txPkts =
			  ZuArray<PktTxSpace, PktNumSpace::N>(
			    PktNumSpace::N);
  ZuArray<AckSnapshot, PktNumSpace::N>
			m_txAck =
			  ZuArray<AckSnapshot, PktNumSpace::N>(
			    PktNumSpace::N);
  ZuTime		m_txInitProbeOut;
  ZuTime		m_txAppProbeOut;
  Path			m_path;
  ZuArray<bool, PktNumSpace::N>
			m_txSpaceDiscarded =
			  fixedArray<bool, PktNumSpace::N>();
  TransportParams	m_txLocalParams;
  CxnID			m_txInitialDCID;
  CxnID			m_txPeerCID;
};

} // namespace Zutcp
