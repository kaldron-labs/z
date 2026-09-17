//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC stream API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic {

template <typename Link_, typename Impl, typename TxBufAlloc_>
class Stream :
  public ZmPolymorph,
  // Rx thread exclusive base. Only the Rx thread may use ZmPQRx APIs/state.
  public ZmPQRx<
    Stream<Link_, Impl, TxBufAlloc_>,
    StreamRxPQueue, ZmPQRxGapIgnore<>>,
  // Tx thread exclusive base. Only the Tx thread may use ZmPQTx APIs/state.
  public ZmPQTx<
    Stream<Link_, Impl, TxBufAlloc_>,
    TxDataPQueue> {
public:
  enum { DefltTxQueueMax = 4096 };

  using Self = Stream<Link_, Impl, TxBufAlloc_>;
  using Link = Link_;
  using Impl_ = Impl;
  using TxBufAlloc = TxBufAlloc_;
  using Rx = ZmPQRx<Self, StreamRxPQueue, ZmPQRxGapIgnore<>>;
  using Tx = ZmPQTx<Self, TxDataPQueue>;
  using RxMsg = StreamRxPQueue::Node;
  using RxQueueSpan = StreamRxPQueue::Span;
  using TxMsg = TxDataPQueue::Node;
  using TxSpan = TxDataPQueue::Span;
  using TxKey = TxDataPQueue::Key;
  using TxUnackdQueue = StreamTxPQueue;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Stream(Link *link, int64_t id) : m_link{link}, m_id{id} { }

  Link *link() const { return m_link; }
  int64_t id() const { return m_id; }
  uint64_t txBytes() const { return m_txBytes; }
  uint64_t txBufferedBytes() const { return m_txBufferedBytes; }
  uint64_t txCreditLimit() const { return m_txCredit.limit(); }
  uint64_t txCreditAvailable() const { return m_txCredit.available(); }
  // Initial peer stream window; upper framed protocols use it to avoid
  // declaring a frame which cannot cross a batched-credit boundary.
  uint64_t txFrameMax() const { return m_txFrameMax; }
  uint64_t rxCreditUsed() const { return m_rxCredit.used(); }
  uint64_t rxCreditLimit() const { return m_rxCredit.limit(); }
  uint64_t rxCreditAvailable() const { return m_rxCredit.available(); }
  uint64_t rxAdmitted() const { return m_rxAdmitted; }
  uint64_t rxRetired() const { return m_rxRetired; }
  uint64_t rxRetirable() const {
    uint64_t limit = m_resetReceived ? finalSize() : m_rxDelivered;
    return m_rxRetired < limit ? limit - m_rxRetired : 0;
  }
  unsigned txRangeCount() const { return m_txQueue.count_(); }
  unsigned txQueueCount() const { return m_txQueueCount.load_(); }
  unsigned txQueueMax() const { return m_txQueueMax; }
  void txQueueMax(unsigned v) { m_txQueueMax = v; }
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }
  unsigned txUnackdCount() const { return m_txUnackd.count_(); }
  uint64_t txUnackdBytes() const { return m_txUnackd.length_(); }
  uint64_t rxBytes() const { return m_rxDelivered; }
  uint64_t finalSize() const { return m_rxState.finalSize(); }
  unsigned rxPending() const { return m_rxQueue.count_(); }
  uint64_t rxAppError() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error access outside Rx thread", return 0);
    return m_rxAppError;
  }
  StreamError::T rxError() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error access outside Rx thread",
      return StreamError::None);
    return m_rxError;
  }
  uint64_t txAppError() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error access outside Tx thread", return 0);
    return m_txAppError;
  }
  StreamError::T txError() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error access outside Tx thread",
      return StreamError::None);
    return m_txError;
  }
  bool finSent() const { return m_fin; }
  bool finDequeued() const { return m_finDequeued; }
  bool txCompleted() const { return m_txCompleted; }
  bool txDrained() const {
    return !m_txQueueCount.load_() && !m_txUnackd.count_();
  }
  bool finReady() const {
    return !m_resetSent && m_fin && !m_finDequeued && !m_txQueue.count_();
  }
  bool finReceived() const { return m_rxState.finSeen(); }
  bool resetSent() const { return m_resetSent; }
  bool resetReceived() const { return m_resetReceived; }
  bool stopSent() const { return m_stopSent; }
  bool resetAckd() const { return m_resetAckd; }
  bool stopAckd() const { return m_stopAckd; }
  bool stopReceived() const { return m_stopReceived; }
  bool readOpen() const { return !m_resetReceived && !rxComplete(); }
  bool closedForStreamCredit() const {
    return (rxComplete() || m_resetReceived) && m_txClosedForCredit;
  }
  bool creditReturned() const { return m_creditReturned; }
  bool earlyData() const { return m_earlyData; }
  void earlyData(bool v) { m_earlyData = v; }
  bool txQueued() const { return m_txQueued; }
  void txQueued(bool v) { m_txQueued = v; }
  bool reapPending() const { return m_reapPending; }
  void reapPending(bool v) { m_reapPending = v; }
  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() const { return m_rx.count_(); }

  RxStream &rxStream() { return m_rx; }

  bool retireRx(uint64_t n) {
    return m_link &&
      m_link->streamRxRetired_(ZmRef<Impl>{impl()}, n);
  }
  bool retireRx_(uint64_t n) {
    if (n > rxRetirable()) return false;
    m_rxRetired += n;
    return true;
  }

  StreamRxPQueue *rxQueue() { return &m_rxQueue; }
  void closeRx_() {
    if (m_link && m_link->app() && m_link->app()->mx())
      ZiAssert(rxInvoked_(), "Zquic", (),
	"QUIC stream Rx close outside Rx thread", return);
    while (m_rxQueue.shift());
    m_rx.clean();
  }
  TxDataPQueue *txQueue() { return &m_txQueue; }
  TxUnackdQueue *txUnackdQueue() { return &m_txUnackd; }

  void txCredit(uint64_t limit) {
    m_txCredit.set(limit);
    if (!m_txFrameMax) m_txFrameMax = limit;
  }
  void extendTxCredit(uint64_t limit) {
    m_txCredit.extend(limit);
    if (!m_txFrameMax) m_txFrameMax = limit;
  }
  bool consumeTxCredit(uint64_t n) { return m_txCredit.consume(n); }
  void rxCredit(uint64_t limit) { m_rxCredit.set(limit); }
  void extendRxCredit(uint64_t limit) { m_rxCredit.extend(limit); }
  bool consumeRxCreditTo(uint64_t n) { return m_rxCredit.consumeTo(n); }
  uint64_t lastBlocked() const { return m_lastBlocked; }
  void lastBlocked(uint64_t n) { m_lastBlocked = n; }
  void markCreditReturned() { m_creditReturned = true; }
  void markTxClosedForCredit() { m_txClosedForCredit = true; }
  unsigned queuedControlFrames() const {
    unsigned n = 0;
    if (m_maxStreamDataControl.queued) ++n;
    if (m_dataBlockedCtl.queued) ++n;
    if (m_resetStreamControl.queued) ++n;
    if (m_stopSendingControl.queued) ++n;
    return n;
  }
  bool controlQueued() const { return queuedControlFrames(); }
  bool queueMaxStreamData(uint64_t value) {
    return queueControl_(
      m_maxStreamDataControl,
      ControlFrame::flowUpdate(FlowUpdate{
	FrameType::MaxStreamData, uint64_t(m_id), value,
	Zquic::StreamType::Duplex}));
  }
  bool queueDataBlocked(uint64_t value) {
    return queueControl_(
      m_dataBlockedCtl,
      ControlFrame::blocked(
	FrameType::StreamDataBlocked, uint64_t(m_id), value));
  }
  bool queueResetStream(uint64_t appError, uint64_t finalSize) {
    return queueControl_(
      m_resetStreamControl,
      ControlFrame::resetStream(uint64_t(m_id), appError, finalSize));
  }
  bool queueStopSending(uint64_t appError) {
    return queueControl_(
      m_stopSendingControl,
      ControlFrame::stopSending(uint64_t(m_id), appError));
  }
  bool nextQueuedControl(ControlFrame &frame) const {
    if (m_maxStreamDataControl.queued) {
      frame = m_maxStreamDataControl.frame;
      return true;
    }
    if (m_dataBlockedCtl.queued) {
      frame = m_dataBlockedCtl.frame;
      return true;
    }
    if (m_resetStreamControl.queued) {
      frame = m_resetStreamControl.frame;
      return true;
    }
    if (m_stopSendingControl.queued) {
      frame = m_stopSendingControl.frame;
      return true;
    }
    return false;
  }
  void controlSent(const ControlFrame &frame) {
    PendingControl *slot = controlSlot_(frame.type);
    if (slot && slot->frame == frame) slot->queued = false;
  }
  void clearControl(const SentFrameRef &ref) {
    PendingControl *slot = controlSlot_(ref.controlType);
    if (!slot) return;
    ControlFrame frame = controlFrame_(ref);
    if (slot->frame == frame) *slot = {};
  }
  void clearControls() {
    m_maxStreamDataControl = {};
    m_dataBlockedCtl = {};
    m_resetStreamControl = {};
    m_stopSendingControl = {};
  }
  bool controlStillValid(const ControlFrame &frame) const {
    const PendingControl *slot = controlSlot_(frame.type);
    if (!slot || !(slot->frame == frame)) return false;
    switch (frame.type) {
      case FrameType::MaxStreamData:
	return readOpen() && frame.value == rxCreditLimit();
      case FrameType::StreamDataBlocked:
	return !txCreditAvailable() && frame.value == txCreditLimit();
      case FrameType::ResetStream:
	return resetSent() && !resetAckd() && frame.value == txBytes();
      case FrameType::StopSending:
	return stopSent() && !stopAckd() && !resetReceived() && !rxComplete();
      default:
	return false;
    }
  }

  void process(RxMsg *msg) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream process outside Rx thread", return);
    if (!msg) return;
    StreamRxData &data = msg->data();
    if (!data.length) return;
    ZiAssert(data.skip + data.length <= data.size, "Zquic", (),
      "stream Rx queued slice exceeds packet-backed range", return);
    m_rxDelivered += data.length;
    m_rxState.delivered(m_rxDelivered);
    ZmRef<ZiRxQueue::Node> buf =
      static_cast<ZiRxQueue::Node *>(msg);
    m_rx.push(ZuMv(buf));
    if (m_link)
      ZquicLOG(m_link->app()->qlogTrace(), ([
	streamID = m_id >= 0 ? uint64_t(m_id) : U64Null,
	streamOffset = data.offset,
	length = uint64_t(data.length),
	linkInfo = m_link->linkInfo()
      ](auto &o, ZuTime time) mutable {
	if (streamID == U64Null) return;
	StreamDataEvt event{
	  .linkInfo = linkInfo
	,
	  .streamID = streamID,
	  .offset = streamOffset,
	  .length = length,
	  .from = StreamDataLoc::Transport,
	  .to = StreamDataLoc::Application};
	o.logStreamDataMoved(event, time);
      }));
  }
  void request(const RxQueueSpan &, const RxQueueSpan &) { }
  void scheduleDequeue() { rxRun_([stream = ZmRef<Self>{impl()}]() {
    stream->dequeueRx_();
  }); }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }
  void dequeueRx_() {
    Rx::dequeue();
    if (m_link && !m_rx.empty())
      m_link->streamRxDequeued_(ZmRef<Impl>{impl()});
  }

  bool send_(TxMsg *, bool) { return true; }
  bool resend_(TxMsg *, bool) { return true; }
  bool sendGap_(const TxSpan &, bool) { return true; }
  bool resendGap_(const TxSpan &, bool) { return true; }
  void archive_(TxMsg *) { }
  ZmRef<TxMsg> retrieve_(TxKey, TxKey) { return nullptr; }
  void scheduleSend() { }
  void rescheduleSend() { }
  void idleSend() { }
  void scheduleResend() { }
  void rescheduleResend() { }
  void idleResend() { }
  void scheduleArchive() { }
  void rescheduleArchive() { }
  void idleArchive() { }

  bool txRange(unsigned i, TxRange &range) const {
    auto iter = m_txQueue.citer();
    while (auto node = iter()) {
      if (!i--) {
	const auto &data = node->data();
	range = TxRange{
	  node, uint32_t(data.skip), uint32_t(data.length),
	  data.streamOffset};
	return true;
      }
    }
    return false;
  }
  bool nextTxRange(PktBudget &, TxRange &range, bool &fin) const {
    fin = false;
    if (txRange(0, range)) return true;
    if (!finReady()) return false;
    range = {};
    range.streamOffset = m_txBytes;
    fin = true;
    return true;
  }
  bool dequeueTxRange(TxRange &range) {
    auto iter = m_txQueue.citer();
    auto node = iter();
    if (!node) return false;
    return consumeTxRange(range, uint32_t(node->data().length));
  }
  bool commitTxRange(TxRange &range, uint32_t length) {
    return consumeTxRange(range, length);
  }
  bool consumeTxRange(TxRange &range, uint32_t length) {
    auto iter = m_txQueue.citer();
    auto first = iter();
    if (!first) return false;
    return consumeTxRange(range, TxRange{
      first, uint32_t(first->data().skip),
      uint32_t(first->data().length), first->data().streamOffset}, length);
  }
  bool commitTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    return consumeTxRange(range, selected, length);
  }
  bool consumeTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    if (!length) return false;
    if (!selected.buf) return false;
    uint64_t key = selected.streamOffset;
    auto current = m_txQueue.find(key);
    if (!current) return false;
    ZiIOBuf *currentBuf = current.ptr();
    if (selected.buf.ptr() != currentBuf) return false;
    const auto &selectedData = current->data();
    if (selected.offset != selectedData.skip ||
	selected.streamOffset != selectedData.streamOffset ||
	selected.length != selectedData.length ||
	length > selectedData.length)
      return false;
    auto node = Tx::abort(key);
    if (!node || node.ptr() != current.ptr()) return false;
    auto &data = node->data();
    if (selected.offset != data.skip ||
	selected.streamOffset != data.streamOffset ||
	selected.length != data.length ||
	length > data.length)
      return false;
    range = TxRange{
      node, uint32_t(data.skip), length, data.streamOffset};
    range.length = length;
    if (length > m_txBufferedBytes) m_txBufferedBytes = 0;
    else m_txBufferedBytes -= length;
    if (length < data.length) {
      data.clipHead(length);
      Tx::send(ZuMv(node));
    } else
      --m_txQueueCount;
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
  bool ackTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n || !m_txUnackd.clear(offset, n)) return false;
    if (fin) m_finAckd = true;
    if (txDrained()) impl()->txDrained_();
    if (m_finAckd && !m_txUnackd.count_() && !m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete_(true);
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
    if (!m_resetSent || m_txBytes != finalSize) return false;
    m_resetAckd = true;
    return true;
  }
  bool ackStop(uint64_t appError) {
    if (!m_stopSent) return false;
    m_stopAckd = true;
    return true;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Stream &stream) :
      Base(unsigned(BufSize), 0, 0), m_stream{&stream} { }

    uint64_t frameMax() const { return m_stream->txFrameMax(); }

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      ZiAssert(skip <= BufSize, "Zquic", (skip),
	"invalid stream headroom " << skip, return nullptr);
      ZmRef<ZiIOBuf> buf = new TxDataPQueue::Node{m_stream};
      buf->skip = skip;
      buf->length = 0;
      return buf;
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      buf->owner = m_stream;
      auto stream = static_cast<Stream *>(buf->owner);
      if constexpr (AppThread)
	return stream->send(ZuMv(buf));
      else
	return stream->send_(ZuMv(buf));
    }

  private:
    // immutable
    Stream	*m_stream;
  };

  auto txStream() {
    ZmAssert(!txInvoked_());
    return TxStream_<true>{*this};
  }
  TxStream_<false> txStream_() { // direct call from within tx thread
    ZmAssert(txInvoked_());
    return TxStream_<false>{*this};
  }

  void fin() {
    if (m_fin || m_resetSent) return;
    m_fin = true;
    notifyTx_();
  }
  void reset(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream reset outside Tx thread", return);
    if (m_resetSent) return;
    m_resetSent = true;
    setTxError_(StreamError::Reset, appError);
    if (m_link && m_id >= 0)
      m_link->localResetStream_(uint64_t(m_id), appError, m_txBytes);
    if (m_link && m_id >= 0)
      m_link->streamResetSent_(
	uint64_t(m_id), appError, m_txBytes);
    if (!m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete_(false);
    }
    notifyTx_();
  }
  void stop(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream stop outside Tx thread", return);
    if (m_stopSent) return;
    m_stopSent = true;
    rxRun_([stream = ZmRef<Impl>{impl()}, appError]() mutable {
      stream->applyStopRx_(appError);
    });
    if (m_link && m_id >= 0)
      m_link->localStopSending_(uint64_t(m_id), appError);
    if (m_link && m_id >= 0)
      m_link->streamStopSendingSent_(uint64_t(m_id), appError);
    notifyTx_();
  }

  bool receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    return receiveFrame_(frame, nullptr, diag, ZuMv(packet));
  }
  bool receiveFrame(
    const Frame &frame, ReceiveFlow &flow, ZmRef<ZiIOBuf> packet,
    BufDiag *diag = nullptr) {
    return receiveFrame_(frame, &flow, diag, ZuMv(packet));
  }

  int processFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveFrame(frame, ZuMv(packet), diag)) return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx_();
  }
  int processFrame(
    const Frame &frame, FlowCredit &connection,
    ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveFrame_(frame, nullptr, &connection, diag, ZuMv(packet)))
      return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx_();
  }
  int processRx_() { return impl()->process(m_rx); }

  bool receiveReset(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC RESET_STREAM receive outside Rx thread", return false);
    if (frame.type != FrameType::ResetStream || m_id < 0 ||
	frame.streamID != uint64_t(m_id) ||
	m_rxDelivered > frame.length ||
	rxPendingBeyond_(frame.length))
      return false;
    StreamRxState next = m_rxState;
    if (!next.receive(frame.length, 0, true)) return false;
    m_rxState = next;
    m_resetReceived = true;
    setRxError_(StreamError::Reset, frame.errorCode);
    return true;
  }

  bool receiveBlocked(const Frame &frame) const {
    if (frame.type != FrameType::StreamDataBlocked || m_id < 0 ||
	frame.streamID != uint64_t(m_id))
      return false;
    if (m_rxState.finalSizeKnown() && frame.value > m_rxState.finalSize())
      return false;
    return frame.value <= m_rxCredit.limit();
  }

  bool receiveStop(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC STOP_SENDING receive outside Rx thread", return false);
    if (frame.type != FrameType::StopSending || m_id < 0 ||
	frame.streamID != uint64_t(m_id))
      return false;
    m_stopReceived = true;
    uint64_t appError = frame.errorCode;
    txInvoke_([stream = ZmRef<Impl>{impl()}, appError]() mutable {
      stream->completeStopTx_(appError);
    });
    return true;
  }

  void completeStopTx_(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC STOP_SENDING completion outside Tx thread", return);
    setTxError_(StreamError::Stop, appError);
    if (!m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete_(false);
    }
  }

  void txComplete_(bool) { }
  void txDrained_() { }

private:
  static void setError_(uint64_t &appError, StreamError::T &error,
      StreamError::T next, uint64_t nextAppError) {
    if (error == StreamError::Reset ||
	(error == StreamError::Stop && next != StreamError::Reset))
      return;
    error = next;
    appError = nextAppError;
  }
  void setRxError_(StreamError::T error, uint64_t appError) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error update outside Rx thread", return);
    setError_(m_rxAppError, m_rxError, error, appError);
  }
  void setTxError_(StreamError::T error, uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error update outside Tx thread", return);
    setError_(m_txAppError, m_txError, error, appError);
  }
  void applyStopRx_(uint64_t appError) {
    setRxError_(StreamError::Stop, appError);
  }

  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, BufDiag *diag,
    ZmRef<ZiIOBuf> packet) {
    return receiveFrame_(frame, flow, nullptr, diag, ZuMv(packet));
  }

  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, FlowCredit *connection,
    BufDiag *diag, ZmRef<ZiIOBuf> packet) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream receive outside Rx thread", return false);
    if (frame.type != FrameType::Stream || m_id < 0 ||
	frame.streamID != uint64_t(m_id) ||
	frame.length != frame.payload.length() ||
	frame.length > BufSize)
      return false;
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;

    if (!m_rxState.validate(frame.offset, frame.length, frame.fin))
      return false;
    if (frame.fin && rxPendingBeyond_(end)) return false;

    auto spans = ZmScratch(
      RxSpan, m_rxQueue.count_() + 1, RxSpans::VHeap);
    if (frame.length && !newRxSpans_(frame, spans)) return false;
    uint64_t newBytes = rxSpanBytes(spans);
    if (flow && !flow->receive(end, newBytes)) return false;
    if (connection) {
      if (end > m_rxCredit.limit() || newBytes > connection->available())
	return false;
      if (!m_rxCredit.consumeTo(end) || !connection->consume(newBytes))
	return false;
    }

    if (spans)
      if (!queueRxSlices_(frame, spans, ZuMv(packet), diag)) return false;

    if (!m_rxState.receive(frame.offset, frame.length, frame.fin)) return false;
    m_rxAdmitted += newBytes;
    return true;
  }

  template <typename Spans>
  bool newRxSpans_(const Frame &frame, Spans &spans) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx span check outside Rx thread", return false);
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_rxDelivered) return true;

    uint64_t first = frame.offset < m_rxDelivered ? m_rxDelivered : frame.offset;
    return rxNovelSpans(m_rxQueue, first, end, spans);
  }

  template <typename Spans>
  bool queueRxSlices_(
    const Frame &frame, const Spans &spans,
    ZmRef<ZiIOBuf> packet, BufDiag *diag) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx queue outside Rx thread", return false);
    if (!packet) return false;
    for (unsigned i = 0, n = spans.length(); i < n; ++i) {
      uint64_t payloadOffset = spans[i].first - frame.offset;
      uint64_t length64 = spans[i].length();
      if (payloadOffset > frame.payload.length() ||
	  length64 > frame.payload.length() - payloadOffset)
	return false;
      const uint8_t *data = reinterpret_cast<const uint8_t *>(
	frame.payload.data() + payloadOffset);
      Rx::rcvd(new StreamRxPQueue::Node{
	packet, data, unsigned(length64), this, spans[i].first});
    }
    return true;
  }

  bool rxPendingBeyond_(uint64_t finalSize) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx pending check outside Rx thread", return false);
    auto iter = m_rxQueue.citer();
    while (auto node = iter()) {
      const StreamRxData &data = node->data();
      if (data.offset > finalSize || data.length > finalSize - data.offset)
	return true;
    }
    return false;
  }

  bool send(ZmRef<ZiIOBuf> buf) {
    if (ZuUnlikely(!buf || !buf->length)) return false;
    if (ZuUnlikely(!admitTx_())) return sendError_(
      "QUIC stream transmit queue limit exceeded");
    buf->owner = this;
    txInvoke_([buf = ZuMv(buf)]() mutable {
      auto stream = static_cast<Stream *>(buf->owner);
      stream->sendAdmitted_(ZuMv(buf));
    });
    return true;
  }

  bool send_(ZmRef<TxMsg> buf) { // direct call from within tx thread
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream send_ outside Tx thread", return false);
    if (ZuUnlikely(!admitTx_())) return sendError_(
      "QUIC stream transmit queue limit exceeded");
    return sendAdmitted_(ZuMv(buf));
  }

private:
  bool admitTx_() {
    if (++m_txQueueCount <= m_txQueueMax) return true;
    --m_txQueueCount;
    return false;
  }
  bool sendAdmitted_(ZmRef<TxMsg> buf) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream send_ outside Tx thread",
      --m_txQueueCount; return false);
    if (ZuUnlikely(!buf)) return false;
    if (ZuUnlikely(m_resetSent || !buf->length)) {
      --m_txQueueCount;
      return sendError_("QUIC stream is closed for transmission");
    }
    ZiAssert(buf->skip + buf->length <= buf->size, "Zquic", (),
      "stream Tx buffer range violation",
      --m_txQueueCount; return false);
    uint32_t offset = buf->skip;
    uint32_t length = buf->length;
    buf->data().publish(offset, length, m_txBytes);
    Tx::send(ZuMv(buf));
    if (m_link)
      ZquicLOG(m_link->app()->qlogTrace(), ([
	streamID = m_id >= 0 ? uint64_t(m_id) : U64Null,
	streamOffset = m_txBytes,
	length = uint64_t(length),
	linkInfo = m_link->linkInfo()
      ](auto &o, ZuTime time) {
	if (streamID == U64Null) return;
	StreamDataEvt event{
	  .linkInfo = linkInfo
	,
	  .streamID = streamID,
	  .offset = streamOffset,
	  .length = length,
	  .from = StreamDataLoc::Application,
	  .to = StreamDataLoc::Transport};
	o.logStreamDataMoved(event, time);
      }));
    m_txBytes += length;
    m_txBufferedBytes += length;
    notifyTx_();
    return true;
  }

  bool sendError_(ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Zquic", message);
    if (m_txErrorFn && !m_txErrorFn(e) && m_link)
      m_link->disconnect();
    return false;
  }

public:

  bool txInvoked_() const {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Tx access before app initialization",
      return false);
    return m_link->app()->txInvoked();
  }
  bool rxInvoked_() const {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Rx access before app initialization",
      return false);
    return m_link->app()->rxInvoked();
  }

  template <typename L>
  void txInvoke_(L &&l) {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Tx invoke before app initialization",
      return);
    m_link->app()->txInvoke(ZuFwd<L>(l));
  }
  template <typename L>
  void rxRun_(L &&l) {
    ZiAssert(m_link && m_link->app() && m_link->app()->mx(),
      "Zquic", (), "QUIC stream Rx run before app initialization",
      return);
    m_link->app()->rxRun(ZuFwd<L>(l));
  }

  void notifyTx_() {
    if (m_link && m_id >= 0) {
      m_link->streamWritable_(ZmRef<Impl>{impl()});
      m_link->flushTx_();
    }
  }
  struct PendingControl {
    ControlFrame	frame;
    bool		queued = false;
  };
  bool queueControl_(PendingControl &slot, const ControlFrame &frame) {
    if (m_id < 0 || !frame) return false;
    if (slot.frame == frame) {
      if (slot.queued) return false;
      slot.queued = true;
      return true;
    }
    if (slot.frame &&
	frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending &&
	slot.frame.value >= frame.value)
      return false;
    slot.frame = frame;
    slot.queued = true;
    return true;
  }
  PendingControl *controlSlot_(FrameType::T type) {
    switch (type) {
      case FrameType::MaxStreamData: return &m_maxStreamDataControl;
      case FrameType::StreamDataBlocked: return &m_dataBlockedCtl;
      case FrameType::ResetStream: return &m_resetStreamControl;
      case FrameType::StopSending: return &m_stopSendingControl;
      default: return nullptr;
    }
  }
  const PendingControl *controlSlot_(FrameType::T type) const {
    return const_cast<Stream *>(this)->controlSlot_(type);
  }
  static ControlFrame controlFrame_(const SentFrameRef &ref) {
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
    return frame;
  }

private:
  // immutable
  Link			*m_link = nullptr;
  int64_t		m_id;

  // shared admission state and published configuration
  ZmAtomic<unsigned>	m_txQueueCount = 0;
  unsigned		m_txQueueMax = DefltTxQueueMax;
  ZiTxErrorFn		m_txErrorFn;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  FlowCredit		m_rxCredit;
  uint64_t		m_rxDelivered = 0;
  uint64_t		m_rxAdmitted = 0;
  uint64_t		m_rxRetired = 0;
  bool			m_resetReceived = false;
  bool			m_stopReceived = false;
  bool			m_creditReturned = false;
  bool			m_txClosedForCredit = false;
  bool			m_earlyData = false;
  StreamRxState		m_rxState;
  RxStream		m_rx;
  StreamRxPQueue	m_rxQueue{0};
  uint64_t		m_lastBlocked = U64Null;
  PendingControl	m_maxStreamDataControl;
  PendingControl	m_dataBlockedCtl;
  uint64_t		m_rxAppError = 0;
  StreamError::T	m_rxError = StreamError::None;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  uint64_t		m_txBytes = 0;
  uint64_t		m_txBufferedBytes = 0;
  FlowCredit		m_txCredit;
  uint64_t		m_txFrameMax = 0;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_finAckd = false;
  bool			m_txCompleted = false;
  bool			m_resetSent = false;
  bool			m_resetAckd = false;
  bool			m_stopSent = false;
  bool			m_stopAckd = false;
  bool			m_reapPending = false;
  uint64_t		m_txAppError = 0;
  StreamError::T	m_txError = StreamError::None;
  PendingControl	m_resetStreamControl;
  PendingControl	m_stopSendingControl;
  bool			m_txQueued = false;
  TxDataPQueue		m_txQueue{0};
  StreamTxPQueue	m_txUnackd{0};
};

template <typename Link, typename Impl, typename TxBufAlloc_ = StreamTxBufAlloc<>>
class CliStream : public Stream<Link, Impl, TxBufAlloc_> {
public:
  using Base = Stream<Link, Impl, TxBufAlloc_>;
  using Base::Base;
};

template <typename Link, typename Impl, typename TxBufAlloc_ = StreamTxBufAlloc<>>
class SrvStream : public Stream<Link, Impl, TxBufAlloc_> {
public:
  using Base = Stream<Link, Impl, TxBufAlloc_>;
  using Base::Base;
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }

ZmHashDeriveT((Stream_), Streams_, Stream_,
  (ZmHashNode<Stream_,
    ZmHashKey<Stream_IDAxor<Stream_>,
	ZmHashHeapID<"Zquic.Stream.ObjectHash">>>));

} // namespace Zquic
