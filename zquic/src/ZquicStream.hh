//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC stream API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic_ {

// Match the ordinary writable-stream turn budget for terminal cleanup work.
enum { StreamCloseBatch = 64 };

struct PendingControl {
  Zquic::ControlFrame frame;
  bool queued = false;
};

struct alignas(Zm::CacheLineSize) StreamAdmission {
  ZmAtomic<unsigned> count = 0;
};

struct alignas(Zm::CacheLineSize) StreamCreditPub {
  ZmAtomic<uint64_t> limit = 0;
  ZmAtomic<unsigned> closed = 0; // Rx terminal publication; Tx only reads
};

struct alignas(Zm::CacheLineSize) StreamFramePub {
  ZmAtomic<uint64_t> maximum = 0; // Tx publishes; off-thread writers read
};

struct alignas(Zm::CacheLineSize) StreamRxStorage {
  FlowCredit		rxCredit;
  uint64_t		rxDelivered = 0;
  uint64_t		rxAdmitted = 0;
  uint64_t		rxRetired = 0;
  uint64_t		creditRuntime = U64Null;
  bool			resetReceived = false;
  bool			closedPublished = false;
  bool			stopReceived = false;
  bool			localStop = false;
  bool			creditReturned = false;
  bool			earlyData = false;
  bool			retired = false;
  StreamRxState		rxState;
  RxStream		rx;
  StreamRxPQueue	rxQueue{0};
  uint64_t		rxAppError = 0;
  StreamError::T	rxError = StreamError::None;
};

struct alignas(Zm::CacheLineSize) StreamTxStorage {
  uint64_t		controlRuntime = 0;
  uint64_t		lastBlocked = U64Null;
  PendingControl	maxStreamDataControl;
  PendingControl	dataBlockedCtl;
  StreamTxRanges	txRanges;
  uint64_t		txBytes = 0;
  uint64_t		txBufferedBytes = 0;
  FlowCredit		txCredit;
  bool			fin = false;
  bool			finDequeued = false;
  bool			finAckd = false;
  bool			txCompleted = false;
  bool			retired = false;
  bool			resetSent = false;
  bool			resetAckd = false;
  bool			stopSent = false;
  bool			stopAckd = false;
  bool                  peerStopped = false;
  bool                  rxDetached = false;
  uint64_t		txAppError = 0;
  StreamError::T	txError = StreamError::None;
  PendingControl	resetStreamControl;
  PendingControl	stopSendingControl;
  bool			txQueued = false;
  StreamTxPQueue	txUnackd{0};
};

struct alignas(Zm::CacheLineSize) StreamRxBoundary {
  char padding[Zm::CacheLineSize];
};

} // Zquic_

namespace Zquic {

class alignas(Zm::CacheLineSize) StreamData : public ZmPolymorph {
public:
  explicit StreamData(int64_t id) : m_id{id} { }
  int64_t id() const { return m_id; }

private:
  const int64_t m_id;
  // Keep the shared reference count out of the following Tx node links.
  [[maybe_unused]] char m_padding[Zm::CacheLineSize - sizeof(ZmPolymorph) - sizeof(int64_t)];
};

inline int64_t StreamIDKey(const StreamData &stream) {
  return stream.id();
}

// Rx and Tx retire independently; both registries retain the same object.
ZmHashDerive(TxStreams, StreamData,
  (ZmHashNode<StreamData,
    ZmHashKey<StreamIDKey,
      ZmHashID<"Zquic.Stream.TxHash", ZmHashHeapID<"">>>>));

// Writable notifications can outlive Tx registry retirement. Retain the
// queued object independently until dequeue, as the former reference queue did.
ZmListDerive(TxStreamList, TxStreams::Node,
  ZmListNode<TxStreams::Node, ZmListShadow<>>);
ZmListDerive(StreamQueue, TxStreamList::Node,
  ZmListNode<TxStreamList::Node, ZmListHeapID<"">>);

// Alignment applies to the Rx base subobject, independently of Tx node size.
template <typename Impl>
struct alignas(Zm::CacheLineSize) StreamRxBase :
  ZmPQRx<Impl, StreamRxPQueue, ZmPQRxGapIgnore<>> { };

template <typename Link_, typename Impl>
class Stream :
  public StreamQueue::Node,
  // Rx thread exclusive base. Only the Rx thread may use ZmPQRx APIs/state.
  public StreamRxBase<Stream<Link_, Impl>> {
template <typename, typename, typename>
friend class Link;
template <typename, typename, typename>
friend class CliLink;
template <typename, typename, typename>
friend class SrvLink;

public:
  enum { DefltTxQueueMax = 4096 };

  using Self = Stream<Link_, Impl>;
  using Link = Link_;
  using Impl_ = Impl;
  using Rx = StreamRxBase<Self>;
  using RxMsg = StreamRxPQueue::Node;
  using RxQueueSpan = StreamRxPQueue::Span;
  using TxUnackdQueue = StreamTxPQueue;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  Stream(Link *link, int64_t id) : StreamQueue::Node{id}, m_config{.link = link} { }

  Link *link() const { return m_config.link; }
  uint64_t txBytes() const { return m_streamTx.txBytes; }
  uint64_t txBufferedBytes() const { return m_streamTx.txBufferedBytes; }
  uint64_t txCreditLimit() const { return m_streamTx.txCredit.limit(); }
  uint64_t txCreditAvailable() const { return m_streamTx.txCredit.available(); }
  // Initial peer stream window; upper framed protocols use it to avoid
  // declaring a frame which cannot cross a batched-credit boundary.
  uint64_t txFrameMax() const { return m_framePub.maximum; }
  uint64_t rxCreditUsed() const { return m_streamRx.rxCredit.used(); }
  uint64_t rxCreditLimit() const { return m_streamRx.rxCredit.limit(); }
  uint64_t rxCreditAvailable() const { return m_streamRx.rxCredit.available(); }
  uint64_t rxAdmitted() const { return m_streamRx.rxAdmitted; }
  uint64_t rxRetired() const { return m_streamRx.rxRetired; }
  uint64_t rxRetirable() const {
    uint64_t limit = m_streamRx.resetReceived ? finalSize() : m_streamRx.rxDelivered;
    return m_streamRx.rxRetired < limit ? limit - m_streamRx.rxRetired : 0;
  }
  unsigned txRangeCount() const { return m_streamTx.txRanges.count_(); }
  unsigned txQueueCount() const { return m_admission.count.load_(); }
  unsigned txQueueMax() const { return m_config.txQueueMax; }
  void txQueueMax(unsigned v) { m_config.txQueueMax = v; }
  void txErrorFn(ZiTxErrorFn fn) { m_config.txErrorFn = ZuMv(fn); }
  unsigned txUnackdCount() const { return m_streamTx.txUnackd.count_(); }
  uint64_t txUnackdBytes() const { return m_streamTx.txUnackd.length_(); }
  uint64_t rxBytes() const { return m_streamRx.rxDelivered; }
  uint64_t finalSize() const { return m_streamRx.rxState.finalSize(); }
  unsigned rxPending() const { return m_streamRx.rxQueue.count_(); }
  uint64_t rxAppError() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error access outside Rx thread", return 0);
    return m_streamRx.rxAppError;
  }
  StreamError::T rxError() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error access outside Rx thread",
      return StreamError::None);
    return m_streamRx.rxError;
  }
  uint64_t txAppError() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error access outside Tx thread", return 0);
    return m_streamTx.txAppError;
  }
  StreamError::T txError() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error access outside Tx thread",
      return StreamError::None);
    return m_streamTx.txError;
  }
  bool finSent() const { return m_streamTx.fin; }
  bool finDequeued() const { return m_streamTx.finDequeued; }
  bool txCompleted() const { return m_streamTx.txCompleted; }
  void markRetiredRx() { m_streamRx.retired = true; }
  void markRetiredTx() { m_streamTx.retired = true; }
  bool retiredRx() const { return m_streamRx.retired; }
  bool retiredTx() const { return m_streamTx.retired; }
  bool txDrained() const {
    return !m_admission.count.load_() && !m_streamTx.txUnackd.count_();
  }
  bool finReady() const {
    return !m_streamTx.resetSent && m_streamTx.fin && !m_streamTx.finDequeued && !txRangeCount();
  }
  bool finReceived() const { return m_streamRx.rxState.finSeen(); }
  bool resetSent() const { return m_streamTx.resetSent; }
  bool resetReceived() const { return m_streamRx.resetReceived; }
  bool stopSent() const { return m_streamTx.stopSent; }
  bool resetAckd() const { return m_streamTx.resetAckd; }
  bool stopAckd() const { return m_streamTx.stopAckd; }
  bool stopReceived() const { return m_streamRx.stopReceived; }
  bool readOpen() const { return !m_streamRx.resetReceived && !rxComplete(); }
  bool creditReturned() const { return m_streamRx.creditReturned; }
  bool earlyData() const { return m_streamRx.earlyData; }
  void earlyData(bool v) { m_streamRx.earlyData = v; }
  bool txQueued() const { return m_streamTx.txQueued; }
  void txQueued(bool v) { m_streamTx.txQueued = v; }
  bool rxComplete() const {
    return m_streamRx.rxState.complete() &&
      m_streamRx.rxDelivered == m_streamRx.rxState.finalSize();
  }
  unsigned rxQueued() const { return m_streamRx.rx.count_(); }

  RxStream &rxStream() { return m_streamRx.rx; }

  bool retireRx(uint64_t n) {
    return m_config.link &&
      m_config.link->streamRxRetired(ZmRef<Impl>{impl()}, n);
  }
  bool retireRx_(uint64_t n) {
    if (n > rxRetirable()) return false;
    m_streamRx.rxRetired += n;
    return true;
  }

  StreamRxPQueue *rxQueue() { return &m_streamRx.rxQueue; }
  void closeRx() {
    for (;;) {
      unsigned budget = Zquic_::StreamCloseBatch;
      if (closeRx(budget)) return;
    }
  }
  bool closeRx(unsigned &budget) {
    if (m_config.link && m_config.link->app() && m_config.link->app()->mx() &&
	m_config.link->app()->mx()->running())
      ZiAssert(rxInvoked_(), "Zquic", (),
	"QUIC stream Rx close outside Rx thread", return false);
    m_creditPub.limit = uint64_t{0};
    while (budget && m_streamRx.rxQueue.shift()) --budget;
    while (budget) {
      auto span = m_streamRx.rx.span();
      if (!span) break;
      m_streamRx.rx.advance(span.length());
      --budget;
    }
    return !m_streamRx.rxQueue.count_() && !m_streamRx.rx.count_();
  }
  bool closeTx(unsigned &budget) {
    if (m_config.link && m_config.link->app() && m_config.link->app()->mx() &&
	m_config.link->app()->mx()->running())
      ZiAssert(txInvoked_(), "Zquic", (),
	"QUIC stream Tx close outside Tx thread", return false);
    m_streamTx.retired = true;
    while (budget) {
      auto range = m_streamTx.txRanges.shift();
      if (!range) break;
      m_streamTx.txBufferedBytes -= static_cast<StreamTxData *>(range.ptr())->rangeLength;
      --m_admission.count;
      --budget;
    }
    bool drained = m_streamTx.txUnackd.drain(budget);
    if (txRangeCount() || !drained) return false;
    clearControls();
    if (!m_streamTx.txCompleted) {
      m_streamTx.txCompleted = true;
      impl()->txComplete(false);
    }
    return true;
  }
  TxUnackdQueue *txUnackdQueue() { return &m_streamTx.txUnackd; }

  void txCredit(uint64_t limit) {
    m_streamTx.txCredit.set(limit);
    if (limit && !uint64_t(m_framePub.maximum)) m_framePub.maximum = limit;
  }
  void extendTxCredit(uint64_t limit) {
    m_streamTx.txCredit.extend(limit);
    if (limit && !uint64_t(m_framePub.maximum)) m_framePub.maximum = limit;
  }
  bool consumeTxCredit(uint64_t n) { return m_streamTx.txCredit.consume(n); }
  void rxCredit(uint64_t limit) {
    m_streamRx.rxCredit.set(limit);
    m_creditPub.limit = limit;
  }
  void extendRxCredit(uint64_t limit) {
    m_streamRx.rxCredit.extend(limit);
    m_creditPub.limit = m_streamRx.rxState.finalSizeKnown() || m_streamRx.resetReceived ?
      uint64_t{0} : m_streamRx.rxCredit.limit();
  }
  bool queueBlockedRxCredit(uint64_t blocked) {
    uint64_t limit = m_creditPub.limit;
    return blocked < limit && queueMaxStreamData(limit);
  }
  bool consumeRxCreditTo(uint64_t n) { return m_streamRx.rxCredit.consumeTo(n); }
  uint64_t lastBlocked() const { return m_streamTx.lastBlocked; }
  void lastBlocked(uint64_t n) { m_streamTx.lastBlocked = n; }
  void markCreditReturned() { m_streamRx.creditReturned = true; }
  unsigned queuedControlFrames() const {
    unsigned n = 0;
    if (m_streamTx.maxStreamDataControl.queued) ++n;
    if (m_streamTx.dataBlockedCtl.queued) ++n;
    if (m_streamTx.resetStreamControl.queued) ++n;
    if (m_streamTx.stopSendingControl.queued) ++n;
    return n;
  }
  bool controlQueued() const { return queuedControlFrames(); }
  bool queueMaxStreamData(uint64_t value) {
    return queueControl(
      m_streamTx.maxStreamDataControl,
      ControlFrame::flowUpdate(FlowUpdate{
	FrameType::MaxStreamData, uint64_t(id()), value,
	Zquic::StreamType::Duplex}));
  }
  bool queueDataBlocked(uint64_t value) {
    return queueControl(
      m_streamTx.dataBlockedCtl,
      ControlFrame::blocked(
	FrameType::StreamDataBlocked, uint64_t(id()), value));
  }
  bool queueResetStream(uint64_t appError, uint64_t finalSize) {
    return queueControl(
      m_streamTx.resetStreamControl,
      ControlFrame::resetStream(uint64_t(id()), appError, finalSize));
  }
  bool queueStopSending(uint64_t appError) {
    return queueControl(
      m_streamTx.stopSendingControl,
      ControlFrame::stopSending(uint64_t(id()), appError));
  }
  bool nextQueuedControl(ControlFrame &frame) const {
    if (m_streamTx.maxStreamDataControl.queued) {
      frame = m_streamTx.maxStreamDataControl.frame;
      return true;
    }
    if (m_streamTx.dataBlockedCtl.queued) {
      frame = m_streamTx.dataBlockedCtl.frame;
      return true;
    }
    if (m_streamTx.resetStreamControl.queued) {
      frame = m_streamTx.resetStreamControl.frame;
      return true;
    }
    if (m_streamTx.stopSendingControl.queued) {
      frame = m_streamTx.stopSendingControl.frame;
      return true;
    }
    return false;
  }
  void controlSent(const ControlFrame &frame) {
    PendingControl *slot = controlSlot(frame.type);
    if (slot && slot->frame == frame) slot->queued = false;
  }
  void clearControl(const SentFrameRef &ref) {
    PendingControl *slot = controlSlot(ref.controlType());
    if (!slot) return;
    ControlFrame frame = controlFrame(ref);
    if (slot->frame == frame) *slot = {};
  }
  void clearControls() {
    m_streamTx.maxStreamDataControl = {};
    m_streamTx.dataBlockedCtl = {};
    m_streamTx.resetStreamControl = {};
    m_streamTx.stopSendingControl = {};
  }
  void resetControls(uint64_t runtime) {
    if (m_streamTx.controlRuntime == runtime) return;
    clearControls();
    m_streamTx.lastBlocked = U64Null;
    m_streamTx.controlRuntime = runtime;
  }
  bool controlStillValid(const ControlFrame &frame) const {
    const PendingControl *slot = controlSlot(frame.type);
    if (!slot || !(slot->frame == frame)) return false;
    switch (frame.type) {
      case FrameType::MaxStreamData:
	return frame.value == uint64_t(m_creditPub.limit);
      case FrameType::StreamDataBlocked:
	return !txCreditAvailable() && frame.value == txCreditLimit();
      case FrameType::ResetStream:
	return resetSent() && !resetAckd() && frame.value == m_streamTx.txCredit.used();
      case FrameType::StopSending:
	return stopSent() && !stopAckd() && !unsigned(m_creditPub.closed);
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
    m_streamRx.rxDelivered += data.length;
    m_streamRx.rxState.delivered(m_streamRx.rxDelivered);
    publishRxClosed();
    ZmRef<ZiRxQueue::Node> buf =
      static_cast<ZiRxQueue::Node *>(msg);
    m_streamRx.rx.push(ZuMv(buf));
    if (m_config.link)
      ZquicLOG(m_config.link->app()->qlogTrace(), ([
	streamID = id() >= 0 ? uint64_t(id()) : U64Null,
	streamOffset = data.offset,
	length = uint64_t(data.length),
	linkInfo = m_config.link->linkInfo()
      ](auto &o, ZuTime time) mutable {
	if (streamID == U64Null) return;
	StreamDataEvt event{
	  .linkInfo = linkInfo,
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
    stream->dequeueRx();
  }); }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }
  void dequeueRx() {
    if (m_streamRx.retired || m_streamRx.resetReceived || (m_config.link &&
	(m_config.link->disconnecting_() || m_config.link->linkClosedRx_()))) return;
    Rx::dequeue();
    if (m_config.link && !m_streamRx.rx.empty())
      m_config.link->streamRxDequeued(ZmRef<Impl>{impl()});
  }

  bool txRange(unsigned i, TxRange &range) const {
    auto node = m_streamTx.txRanges.headPtr();
    if (i) {
      auto iter = m_streamTx.txRanges.citer();
      while ((node = iter()) && i) --i;
    }
    if (!node) return false;
    const auto &data = *static_cast<StreamTxData *>(node);
    range = TxRange{data.backing ? data.backing : ZmRef<ZiIOBuf>{
	const_cast<StreamTxData *>(&data)},
      data.rangeOffset, data.rangeLength, data.streamOffset};
    return true;
  }
  bool nextTxRange(PktBudget &, TxRange &range, bool &fin) const {
    fin = false;
    if (txRange(0, range)) return true;
    if (!finReady()) return false;
    range = {};
    range.streamOffset = m_streamTx.txBytes;
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
    if (!selected.buf) return false;
    auto node = static_cast<StreamTxData *>(m_streamTx.txRanges.headPtr());
    if (!node) return false;
    auto backing = node->backing ? node->backing.ptr() : static_cast<ZiIOBuf *>(node);
    if (backing != selected.buf || node->rangeOffset != selected.offset ||
	node->rangeLength != selected.length || node->streamOffset != selected.streamOffset ||
	length > node->rangeLength) return false;
    range = selected;
    range.length = length;
    m_streamTx.txBufferedBytes -= length;
    if (length < node->rangeLength)
      node->advanceRange(length);
    else {
      (void)m_streamTx.txRanges.shift();
      --m_admission.count;
      // Return producer credit after this packetizer turn has committed flow
      // credit, FIN and recovery metadata. Recovery retains the plaintext until
      // ACK independently of the now-empty application queue.
      if constexpr (!ZuIsSame<decltype(&Impl::txDequeued_), void (Self::*)()>{}) {
	if (m_config.link) m_config.link->app()->txRun([
	  link = ZmRef<Link>{m_config.link}, stream = ZmRef<Impl>{impl()}
	]() mutable {
	  if (!link->disconnecting_() && !stream->retiredTx() && !stream->resetSent() &&
	      stream->txError() == StreamError::None)
	    stream->txDequeued_();
	});
      }
    }
    return true;
  }

  bool dequeueFin(uint64_t &offset) {
    if (!finReady()) return false;
    offset = m_streamTx.txBytes;
    m_streamTx.finDequeued = true;
    return true;
  }
  bool recordTxUnackd(uint64_t offset, uint64_t length, bool fin) {
    if (!length && !fin) return false;
    return m_streamTx.txUnackd.add(TxUnackdRange{offset, length, fin}) != ZmPQResult::Invalid;
  }
  bool discardTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    return n && m_streamTx.txUnackd.clear(offset, n);
  }
  bool ackTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n || !m_streamTx.txUnackd.clear(offset, n)) return false;
    if (fin) m_streamTx.finAckd = true;
    if (txDrained()) impl()->txDrained_();
    if (m_streamTx.finAckd && !m_streamTx.txUnackd.count_() && !m_streamTx.txCompleted) {
      m_streamTx.txCompleted = true;
      impl()->txComplete(true);
    }
    return true;
  }
  bool txStillUnackd(uint64_t offset, uint64_t length, bool fin = false) const {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n) return false;
    bool found = false;
    (void)m_streamTx.txUnackd.spans(offset, n, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool ackReset(uint64_t appError, uint64_t finalSize) {
    if (!m_streamTx.resetSent || m_streamTx.txCredit.used() != finalSize) return false;
    m_streamTx.resetAckd = true;
    return true;
  }
  bool ackStop(uint64_t appError) {
    if (!m_streamTx.stopSent) return false;
    m_streamTx.stopAckd = true;
    return true;
  }

  template <bool AppThread>
  class TxStream_ : public Zi::TxStream<TxStream_<AppThread>> {
    using Base = Zi::TxStream<TxStream_<AppThread>>;

  public:
    TxStream_(Stream &stream) :
      Base(stream.link()->app()->streamTxAllocator()->size,
	stream.link()->app()->streamTxAllocator()->headRoom,
	stream.link()->app()->streamTxAllocator()->tailRoom),
      m_stream{&stream} { }

    uint64_t frameMax() const { return m_stream->txFrameMax(); }

    ~TxStream_() { this->flush(); }
    TxStream_(TxStream_ &&) = default;
    TxStream_ &operator =(TxStream_ &&other) {
      if (this != &other) {
	flush();
	Base::operator =(ZuMv(other));
	m_stream = other.m_stream;
	m_pending = ZuMv(other.m_pending);
      }
      return *this;
    }

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      auto allocator = m_stream->link()->app()->streamTxAllocator();
      ZiAssert(skip <= allocator->size - allocator->tailRoom, "Zquic", (skip),
	"invalid stream headroom " << skip, return nullptr);
      auto buf = allocator->allocBuf(m_stream);
      if (!buf) return nullptr;
      ZiAssert(skip <= buf->size && allocator->tailRoom <= buf->size - skip,
	"Zquic", (), "stream buffer cannot hold allocator framing", return nullptr);
      buf->skip = skip;
      buf->length = 0;
      return buf;
    }

    bool appendBuf(ZmRef<ZiIOBuf> buf) {
      if (!flush() || !buf || buf->failed()) { this->fail(); return false; }
      ZiAssert(buf->skip <= buf->size && buf->length <= buf->size - buf->skip,
	"Zquic", (), "stream Tx buffer range violation", this->fail(); return false);
      if (buf->length) m_pending = ZuMv(buf);
      return true;
    }
    using Base::operator <<;
    TxStream_ &operator <<(ZmRef<ZiIOBuf> buf) {
      appendBuf(ZuMv(buf));
      return *this;
    }

    bool flush(bool finish = false) {
      if (!Base::flush(finish)) return false;
      if (m_pending && !sendBuf_({}, Zi::SendFlags::Flush())) { this->fail(); return false; }
      return true;
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, Zi::SendFlags::T flags) {
      StreamTxOp op;
      op.fin = flags & Zi::SendFlags::Finish();
      if (m_pending) op.ranges.pushNode(new StreamTxView{ZuMv(m_pending)});
      if (buf) {
	auto data = static_cast<StreamTxData *>(buf.ptr());
	data->publish();
	op.ranges.pushNode(ZuMv(buf));
      }
      return m_stream->template sendOperation<AppThread>(ZuMv(op));
    }

  private:
    // immutable
    Stream	*m_stream;
    // Last external range stays available for a finishing flush.
    ZmRef<ZiIOBuf> m_pending;
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
    if (m_streamTx.retired || m_streamTx.fin || m_streamTx.resetSent) return;
    m_streamTx.fin = true;
    notifyTx();
  }
  void reset(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream reset outside Tx thread", return);
    if (m_streamTx.retired || m_streamTx.resetSent) return;
    m_streamTx.resetSent = true;
    setTxError(StreamError::Reset, appError);
    // Queued application bytes may exceed peer credit. Final size covers only
    // the extent committed to STREAM frames, including any lost packets.
    auto finalSize = m_streamTx.txCredit.used();
    // Packet recovery retains its own references. A reset forbids further
    // STREAM retransmission and releases all stream-owned queued ranges now.
    unsigned queued = txRangeCount();
    m_streamTx.txRanges.clean();
    m_admission.count -= queued; // Pending off-shard admissions still retire once.
    m_streamTx.txBufferedBytes = 0;
    m_streamTx.txUnackd.clean();
    m_streamTx.dataBlockedCtl = {};
    if (m_config.link && id() >= 0)
      m_config.link->localResetStream(uint64_t(id()), appError, finalSize);
    if (m_config.link && id() >= 0)
      m_config.link->streamResetSent_(
	uint64_t(id()), appError, finalSize);
    if (!m_streamTx.txCompleted) {
      m_streamTx.txCompleted = true;
      impl()->txComplete(false);
    }
    notifyTx();
  }
  void stopRx(uint64_t appError) {
    ZiAssert(rxInvoked_(), "Zquic", (), "QUIC stream stop outside Rx thread", return);
    if (stopRequestedRx(appError))
      txInvoke_([stream = ZmRef<Impl>{impl()}, appError] { stream->sendStopTx(appError); });
  }
  bool stopRequestedRx(uint64_t appError) {
    if (m_streamRx.localStop) return false;
    m_streamRx.localStop = true;
    applyStopRx(appError);
    return true;
  }
  void stop(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (), "QUIC stream stop outside Tx thread", return);
    if (m_streamTx.retired || m_streamTx.stopSent) return;
    rxRun_([stream = ZmRef<Impl>{impl()}, appError] { stream->stopRequestedRx(appError); });
    sendStopTx(appError);
  }
  void sendStopTx(uint64_t appError) {
    if (m_streamTx.retired || m_streamTx.stopSent) return;
    m_streamTx.stopSent = true;
    if (m_config.link && id() >= 0)
      m_config.link->localStopSending(uint64_t(id()), appError);
    if (m_config.link && id() >= 0)
      m_config.link->streamStopSendingSent_(uint64_t(id()), appError);
    notifyTx();
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
    uint64_t delivered = m_streamRx.rxState.delivered();
    bool finSeen = m_streamRx.rxState.finSeen();
    unsigned pending = m_streamRx.rxQueue.count_();
    if (!receiveFrame(frame, ZuMv(packet), diag)) return -1;
    if (m_streamRx.rxState.delivered() == delivered && m_streamRx.rxState.finSeen() == finSeen &&
	m_streamRx.rxQueue.count_() == pending)
      return 0;
    return processRx();
  }
  int processFrame(
    const Frame &frame, FlowCredit &connection,
    ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    uint64_t delivered = m_streamRx.rxState.delivered();
    bool finSeen = m_streamRx.rxState.finSeen();
    unsigned pending = m_streamRx.rxQueue.count_();
    if (!receiveFrame_(frame, nullptr, &connection, diag, ZuMv(packet)))
      return -1;
    if (m_streamRx.rxState.delivered() == delivered && m_streamRx.rxState.finSeen() == finSeen &&
	m_streamRx.rxQueue.count_() == pending)
      return 0;
    return processRx();
  }
  int processRx() { return impl()->process(m_streamRx.rx); }

  bool receiveReset(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC RESET_STREAM receive outside Rx thread", return false);
    if (frame.type != FrameType::ResetStream || id() < 0 ||
	frame.streamID != uint64_t(id()) ||
	m_streamRx.rxDelivered > frame.length ||
	rxPendingBeyond(frame.length))
      return false;
    StreamRxState next = m_streamRx.rxState;
    if (!next.receive(frame.length, 0, true)) return false;
    m_streamRx.rxState = next;
    m_streamRx.resetReceived = true;
    publishRxClosed();
    m_creditPub.limit = uint64_t{0};
    setRxError(StreamError::Reset, frame.errorCode);
    return true;
  }

  bool receiveBlocked(const Frame &frame) const {
    if (frame.type != FrameType::StreamDataBlocked || id() < 0 ||
	frame.streamID != uint64_t(id()))
      return false;
    if (m_streamRx.rxState.finalSizeKnown() && frame.value > m_streamRx.rxState.finalSize())
      return false;
    return frame.value <= m_streamRx.rxCredit.limit();
  }

  void completeStopTx(uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC STOP_SENDING completion outside Tx thread", return);
    setTxError(StreamError::Stop, appError);
    if (!m_streamTx.txCompleted) {
      m_streamTx.txCompleted = true;
      impl()->txComplete(false);
    }
  }

  void txComplete(bool) { }
  void txDrained_() { }
  // AppTx, once per fully packetized source buffer, on a later dispatch turn.
  // This returns producer queue credit; it does not indicate peer ACK receipt.
  void txDequeued_() { }

private:
  static void setError_(uint64_t &appError, StreamError::T &error,
      StreamError::T next, uint64_t nextAppError) {
    if (error == StreamError::Reset ||
	(error == StreamError::Stop && next != StreamError::Reset))
      return;
    error = next;
    appError = nextAppError;
  }
  void setRxError(StreamError::T error, uint64_t appError) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx error update outside Rx thread", return);
    setError_(m_streamRx.rxAppError, m_streamRx.rxError, error, appError);
  }
  void setTxError(StreamError::T error, uint64_t appError) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx error update outside Tx thread", return);
    setError_(m_streamTx.txAppError, m_streamTx.txError, error, appError);
  }
  void applyStopRx(uint64_t appError) {
    setRxError(StreamError::Stop, appError);
  }

  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, BufDiag *diag,
    ZmRef<ZiIOBuf> packet) {
    return receiveFrame_(frame, flow, nullptr, diag, ZuMv(packet));
  }

protected:
  template <typename L> void ownerLayout(L l) const {
    l(static_cast<const Rx *>(this), sizeof(Rx));
    l(&m_config, sizeof(m_config));
    l(&m_admission, sizeof(m_admission));
    l(&m_creditPub, sizeof(m_creditPub));
    l(&m_framePub, sizeof(m_framePub));
    l(&m_streamRx, sizeof(m_streamRx));
    l(&m_streamTx, sizeof(m_streamTx));
  }
  // Explicit compile-time selection also permits differential receive fixtures.
  template <bool Fast = true>
  bool receiveFrame_(
    const Frame &frame, ReceiveFlow *flow, FlowCredit *connection,
    BufDiag *diag, ZmRef<ZiIOBuf> packet) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream receive outside Rx thread", return false);
    if (frame.type != FrameType::Stream || id() < 0 ||
	frame.streamID != uint64_t(id()) ||
	frame.length != frame.payload.length() ||
	frame.length > BufSize)
      return false;
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;

    if (!m_streamRx.rxState.validate(frame.offset, frame.length, frame.fin))
      return false;
    if (frame.fin && rxPendingBeyond(end)) return false;

    if constexpr (Fast) {
      if (ZuLikely(frame.length && !frame.fin &&
	  frame.offset == m_streamRx.rxDelivered && !m_streamRx.rxQueue.count_() && packet)) {
	if (!receiveCredit(end, frame.length, flow, connection)) return false;
	queueRxSlice(frame, frame.offset, frame.length, ZuMv(packet));
	if (!m_streamRx.rxState.receive(frame.offset, frame.length, frame.fin)) return false;
	publishRxClosed();
	m_streamRx.rxAdmitted += frame.length;
	return true;
      }
    }

    auto spans = ZmScratch(
      RxSpan, m_streamRx.rxQueue.count_() + 1, RxSpans::VHeap);
    if (frame.length && !newRxSpans(frame, spans)) return false;
    uint64_t newBytes = rxSpanBytes(spans);
    if (!receiveCredit(end, newBytes, flow, connection)) return false;

    if (spans)
      if (!queueRxSlices(frame, spans, ZuMv(packet), diag)) return false;

    if (!m_streamRx.rxState.receive(frame.offset, frame.length, frame.fin)) return false;
    if (frame.fin) m_creditPub.limit = uint64_t{0};
    publishRxClosed();
    m_streamRx.rxAdmitted += newBytes;
    return true;
  }

private:
  void publishRxClosed() {
    if (m_streamRx.closedPublished ||
	(!m_streamRx.resetReceived && !rxComplete())) return;
    m_streamRx.closedPublished = true;
    m_creditPub.closed = 1;
  }

  bool receiveCredit(
    uint64_t end, uint64_t newBytes, ReceiveFlow *flow, FlowCredit *connection) {
    if (flow && !flow->receive(end, newBytes)) return false;
    if (connection) {
      if (end > m_streamRx.rxCredit.limit() || newBytes > connection->available())
	return false;
      if (!m_streamRx.rxCredit.consumeTo(end) || !connection->consume(newBytes))
	return false;
    }
    return true;
  }

  void queueRxSlice(
    const Frame &frame, uint64_t first, unsigned length, ZmRef<ZiIOBuf> packet) {
    Rx::rcvd(new StreamRxPQueue::Node{
      ZuMv(packet), frame.payload.data() + (first - frame.offset), length,
      link()->app()->streamRxAllocator(), this, first});
  }

  template <typename Spans>
  bool newRxSpans(const Frame &frame, Spans &spans) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx span check outside Rx thread", return false);
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_streamRx.rxDelivered) return true;

    uint64_t first = frame.offset < m_streamRx.rxDelivered ?
      m_streamRx.rxDelivered : frame.offset;
    return rxNovelSpans(m_streamRx.rxQueue, first, end, spans);
  }

  template <typename Spans>
  bool queueRxSlices(
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
      queueRxSlice(frame, spans[i].first, unsigned(length64), packet);
    }
    return true;
  }

  bool rxPendingBeyond(uint64_t finalSize) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx pending check outside Rx thread", return false);
    auto iter = m_streamRx.rxQueue.citer();
    while (auto node = iter()) {
      const StreamRxData &data = node->data();
      if (data.offset > finalSize || data.length > finalSize - data.offset)
	return true;
    }
    return false;
  }

  bool send(ZmRef<ZiIOBuf> buf, bool fin = false) {
    return sendBuffer<true>(ZuMv(buf), fin);
  }
  bool send_(ZmRef<ZiIOBuf> buf, bool fin = false) {
    return sendBuffer<false>(ZuMv(buf), fin);
  }
  bool send(ZuBSpan data, bool fin = false) {
    return sendBytes<true>(data, fin);
  }
  bool send_(ZuBSpan data, bool fin = false) {
    return sendBytes<false>(data, fin);
  }

private:
  bool txOverflow() {
    auto fn = m_config.txErrorFn;
    if (m_config.link) m_config.link->disconnect();
    if (fn) {
      auto e = ZeEXCEPT(Error, "Zquic", "QUIC stream transmit queue limit exceeded");
      (void)fn(e);
    }
    return false;
  }
  bool admitTx() {
    if (++m_admission.count <= m_config.txQueueMax) return true;
    --m_admission.count;
    return false;
  }
  template <bool AppThread>
  bool sendOperation(StreamTxOp op) {
    unsigned count = op.ranges.count_();
    unsigned admitted = 0;
    while (admitted < count) {
      if (!admitTx()) {
	m_admission.count -= admitted;
	return txOverflow();
      }
      ++admitted;
    }
    if constexpr (AppThread) {
      // A single dispatch transfers all owned ranges and their final FIN intent.
      txInvoke_([stream = ZmRef<Impl>{impl()}, op = ZuMv(op)]() mutable {
	stream->sendAdmitted(ZuMv(op));
      });
      return true;
    } else
      return sendAdmitted(ZuMv(op));
  }
  template <bool AppThread>
  bool sendBuffer(ZmRef<ZiIOBuf> buf, bool fin) {
    if (buf && buf->failed()) return false;
    if ((!buf || !buf->length) && !fin) return false;
    ZiAssert(!buf || (buf->skip <= buf->size && buf->length <= buf->size - buf->skip),
      "Zquic", (), "stream Tx buffer range violation", return false);
    StreamTxOp op;
    op.fin = fin;
    if (buf && buf->length) op.ranges.pushNode(new StreamTxView{ZuMv(buf)});
    return sendOperation<AppThread>(ZuMv(op));
  }
  template <bool AppThread>
  bool sendBytes(ZuBSpan data, bool fin) {
    if (!data.length() && !fin) return false;
    StreamTxOp op;
    op.fin = fin;
    auto allocator = m_config.link->app()->streamTxAllocator();
    unsigned capacity = allocator->capacity();
    if (!capacity) return sendError("QUIC stream allocator has no payload capacity");
    uint64_t count = data.length() ? 1 + (data.length() - 1)/capacity : 0;
    unsigned admitted = m_admission.count.load_();
    if (count && (count > m_config.txQueueMax || admitted > m_config.txQueueMax - count))
      return txOverflow();
    // Concurrent producers can spend credit after this preflight. The actual
    // reservation below remains authoritative before the owned operation posts.
    while (data.length()) {
      auto buf = allocator->allocBuf(this);
      if (!buf || buf->failed()) return false;
      unsigned length = buf->avail() - allocator->tailRoom;
      if (data.length() < length) length = unsigned(data.length());
      if (!length || !buf->append(ZuBSpan{data.data(), length})) return false;
      auto node = static_cast<StreamTxData *>(buf.ptr());
      node->publish();
      op.ranges.pushNode(node);
      data = ZuBSpan{data.data() + length, data.length() - length};
    }
    return sendOperation<AppThread>(ZuMv(op));
  }
  bool sendAdmitted(StreamTxOp op) {
    unsigned count = op.ranges.count_();
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream send outside Tx thread",
      m_admission.count -= count; return false);
    if (ZuUnlikely(m_streamTx.retired || m_streamTx.resetSent || m_streamTx.fin)) {
      m_admission.count -= count;
      return sendError("QUIC stream is closed for transmission");
    }
    while (auto queued = op.ranges.shift()) {
      auto node = static_cast<StreamTxData *>(queued.ptr());
      uint32_t length = node->rangeLength;
      node->streamOffset = m_streamTx.txBytes;
      m_streamTx.txRanges.pushNode(ZuMv(queued));
      if (m_config.link)
	ZquicLOG(m_config.link->app()->qlogTrace(), ([
	  streamID = id() >= 0 ? uint64_t(id()) : U64Null,
	  streamOffset = m_streamTx.txBytes,
	  length = uint64_t(length),
	  linkInfo = m_config.link->linkInfo()
	](auto &o, ZuTime time) {
	  if (streamID == U64Null) return;
	  StreamDataEvt event{
	    .linkInfo = linkInfo,
	    .streamID = streamID,
	    .offset = streamOffset,
	    .length = length,
	    .from = StreamDataLoc::Application,
	    .to = StreamDataLoc::Transport};
	  o.logStreamDataMoved(event, time);
	}));
      m_streamTx.txBytes += length;
      m_streamTx.txBufferedBytes += length;
    }
    if (op.fin) m_streamTx.fin = true;
    notifyTx();
    return true;
  }

  bool sendError(ZuCSpan message) {
    auto e = ZeEXCEPT(Error, "Zquic", message);
    if (m_config.txErrorFn && !m_config.txErrorFn(e) && m_config.link)
      m_config.link->disconnect();
    return false;
  }

public:

  bool txInvoked_() const {
    ZiAssert(m_config.link && m_config.link->app() && m_config.link->app()->mx(),
      "Zquic", (), "QUIC stream Tx access before app initialization",
      return false);
    return m_config.link->app()->txInvoked();
  }
  bool rxInvoked_() const {
    ZiAssert(m_config.link && m_config.link->app() && m_config.link->app()->mx(),
      "Zquic", (), "QUIC stream Rx access before app initialization",
      return false);
    return m_config.link->app()->rxInvoked();
  }

  template <typename L>
  void txInvoke_(L &&l) {
    ZiAssert(m_config.link && m_config.link->app() && m_config.link->app()->mx(),
      "Zquic", (), "QUIC stream Tx invoke before app initialization",
      return);
    m_config.link->app()->txInvoke(ZuFwd<L>(l));
  }
  template <typename L>
  void rxRun_(L &&l) {
    ZiAssert(m_config.link && m_config.link->app() && m_config.link->app()->mx(),
      "Zquic", (), "QUIC stream Rx run before app initialization",
      return);
    m_config.link->app()->rxRun(ZuFwd<L>(l));
  }

  void notifyTx() {
    if (m_config.link && id() >= 0) {
      m_config.link->streamWritable(ZmRef<Impl>{impl()});
      m_config.link->queueTxFlush();
    }
  }
  using PendingControl = Zquic_::PendingControl;
  bool queueControl(PendingControl &slot, const ControlFrame &frame) {
    if (m_streamTx.retired || id() < 0 || !frame) return false;
    if (m_config.link) resetControls(m_config.link->txRuntime());
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
  PendingControl *controlSlot(FrameType::T type) {
    switch (type) {
      case FrameType::MaxStreamData: return &m_streamTx.maxStreamDataControl;
      case FrameType::StreamDataBlocked: return &m_streamTx.dataBlockedCtl;
      case FrameType::ResetStream: return &m_streamTx.resetStreamControl;
      case FrameType::StopSending: return &m_streamTx.stopSendingControl;
      default: return nullptr;
    }
  }
  const PendingControl *controlSlot(FrameType::T type) const {
    return const_cast<Stream *>(this)->controlSlot(type);
  }

private:
  // Immutable/published configuration, separate from the Rx base tail and
  // producer admission writes.
  struct alignas(Zm::CacheLineSize) Config {
    Link		*link = nullptr;
    unsigned		txQueueMax = DefltTxQueueMax;
    ZiTxErrorFn		txErrorFn;
  } m_config;

  // shared admission state and published configuration
  Zquic_::StreamAdmission m_admission;
  Zquic_::StreamCreditPub m_creditPub;
  Zquic_::StreamFramePub m_framePub;

  // Rx thread exclusive
  Zquic_::StreamRxStorage m_streamRx;

  // Tx thread exclusive
  Zquic_::StreamTxStorage m_streamTx;


};

template <typename Link, typename Impl>
class CliStream : public Stream<Link, Impl> {
public:
  using Base = Stream<Link, Impl>;
  using Base::Base;
};

template <typename Link, typename Impl>
class SrvStream : public Stream<Link, Impl> {
public:
  using Base = Stream<Link, Impl>;
  using Base::Base;
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }

// The application-derived tail has unknown ownership. Do not append the Rx
// registry link into its final cache line. This is storage only; the concrete
// ZmHash node remains the allocator and reference-counted object.
template <typename Stream_>
class StreamRxNode : public Stream_ {
public:
  using Stream_::Stream_;

private:
  Zquic_::StreamRxBoundary m_boundary;
};

ZmListDeriveT((Stream_), StreamRxList, StreamRxNode<Stream_>,
  (ZmListNode<StreamRxNode<Stream_>, ZmListShadow<>>));
ZmHashDeriveT((Stream_), Streams_, (typename StreamRxList<Stream_>::Node),
  (ZmHashNode<typename StreamRxList<Stream_>::Node,
    ZmHashKey<Stream_IDAxor<Stream_>,
	ZmHashHeapID<"Zquic.Stream.ObjectHash">>>));

} // namespace Zquic
