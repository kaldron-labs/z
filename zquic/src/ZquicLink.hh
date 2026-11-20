//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC link API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic {

ZtEnumStruct(ZquicAPI, CxnTimer, int8_t,
  AckDelay,
  Loss,
  PTO,
  Idle,
  HeartBeat,
  Close,
  KeyDiscard,
  PMTUD,
  PathValid);

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

} // Zquic

namespace Zquic_ {

struct alignas(Zm::CacheLineSize) LinkRxStats : LinkRxDiag { };
struct alignas(Zm::CacheLineSize) LinkTxStats : LinkTxDiag { };

struct ClosedStreamData {
  uint64_t id;
  explicit ClosedStreamData(uint64_t id_) : id{id_} { }
};
inline uint64_t closedStreamKey(const ClosedStreamData &stream) {
  return stream.id;
}
ZmListDerive(ClosedStreamList, ClosedStreamData,
  ZmListNode<ClosedStreamData, ZmListShadow<>>);
ZmHashDerive(ClosedStreams, ClosedStreamList::Node,
  (ZmHashNode<ClosedStreamList::Node,
    ZmHashKey<closedStreamKey, ZmHashHeapID<"Zquic.Stream.ClosedHash">>>));

using LinkDiscFn = ZmFn<void(), ZmFnHeapID<"Zquic.Link.DiscFn">>;
using LinkDiscFns = Queue<ZmQueue<LinkDiscFn,
  ZmQueueHeapID<"Zquic.Link.DiscFns">>>;

// One pooled batch per final Rx drain; no owner reference or packet-path work.
template <typename Stream, unsigned N>
struct StreamCloseWork : public ZmNHeap<StreamCloseWork<Stream, N>>, public ZmObject {
friend inline ZuStringT<"Zquic.Stream.Close"> ZmNHeapID(StreamCloseWork *);
  ZuArray<ZmRef<Stream>, N> streams;
  LinkDiscFns completed;
  unsigned next = 0;
};

struct AckSnapshot {
  PktNumSpace::T	level = PktNumSpace::Initial;
  uint64_t		gen = 0;
  uint64_t		sequence = 0;
  uint64_t		epoch = 0;
  uint64_t		keyGeneration = 0;
  uint64_t		delay = 0;
  uint64_t		largestRxTime = 0;
  uint64_t		receivedUS = 0;
  uint64_t		deadlineUS = 0;
  AckECN		ecn;
  bool		due = false;
  Frame::AckRanges	ranges;
  AckSnapshot() = default;
  // ZtBuiltin's inline buffer is object-local; copy into the already
  // constructed destination so its backing pointer cannot alias the source.
  AckSnapshot(const AckSnapshot &a) { *this = a; }
  AckSnapshot &operator =(const AckSnapshot &a) {
    level = a.level;
    gen = a.gen;
    sequence = a.sequence;
    epoch = a.epoch;
    keyGeneration = a.keyGeneration;
    delay = a.delay;
    largestRxTime = a.largestRxTime;
    receivedUS = a.receivedUS;
    deadlineUS = a.deadlineUS;
    ecn = a.ecn;
    due = a.due;
    ranges = a.ranges;
    return *this;
  }
  AckSnapshot(AckSnapshot &&) = default;
  AckSnapshot &operator =(AckSnapshot &&) = default;
};
struct RxAckMeta {
  bool		ackEliciting = false;
  bool		immediateAck = false;
};
struct TxAckWork {
  AckSnapshot		ack;
  uint64_t		gen = 0;
  uint64_t		peerSequence = 0;
  AckGrowth		growth;
  PktAckBatch		ackBatch;
  PktLossBatch	lossBatch;
  bool		ecnValidated = false;
  bool		ecnValid = true;
  bool		lossPhase = false;
  bool		congestionOpened = false;
  bool		retiredData = false;
  bool		retransmit = false;
  uint64_t		ceDelta = 0;
};
struct PeerAckPending {
  AckSnapshot ack;
  uint64_t sequence = 0;
};
using PeerAckQueue = Zquic_::Queue<
  ZmList<PeerAckPending, ZmListHeapID<"Zquic.PeerAck">>>;
// Exceptional snapshots are owned by their queued Tx handoff. These scalar
// counters describe that work without letting telemetry traverse it.
struct alignas(Zm::CacheLineSize) AckPost final : public Zquic_::QueueInfo {
  struct alignas(Zm::CacheLineSize) Count { uint64_t value = 0; };
  Count published, consumed;
  void telemetry(Ztc::QueueTelemetry &data) const override {
    queueTelemetry(data);
    uint64_t in = published.value, out = consumed.value;
    data.count = in > out ? in - out : 0;
  }
};
struct AckHandoff : public ZmNHeap<AckHandoff>, public ZmObject {
friend inline ZuStringT<"Zquic.Ack.Handoff"> ZmNHeapID(AckHandoff *);
  AckSnapshot ack;
  ZiSockAddr addr;
  uint64_t runtime;
  AckHandoff(AckSnapshot ack_, ZiSockAddr addr_, uint64_t runtime_) :
    ack{ZuMv(ack_)}, addr{ZuMv(addr_)}, runtime{runtime_} { }
};
struct alignas(Zm::CacheLineSize) AckCompletion {
  ZmAtomic<uint64_t>	gen = 0;
  ZmAtomic<uint64_t>	retireFrontier = 0;
};
enum { AckPubSlots = 64 };
struct alignas(Zm::CacheLineSize) AckPubSlot {
  uint64_t		sequence = 0;
  uint64_t		epoch = 0;
  uint64_t		gen = 0;
  AckRange		range;
  uint64_t		largestRxTime = 0;
  uint64_t		deadlineUS = 0;
  AckECN		ecn;
  bool		due = false;
};
struct alignas(Zm::CacheLineSize) AckRxPub {
  ZmAtomic<uint64_t>	sequence = 0;
  ZmAtomic<uint64_t>	epoch = 0;
  uint32_t		full = 0;
};
struct alignas(Zm::CacheLineSize) AckTxPub {
  ZmAtomic<uint64_t>	readSequence = 0;
};
struct alignas(Zm::CacheLineSize) RxPathPub {
  ZmAtomic<uint64_t>	epoch = 0;
  ZmAtomic<uint64_t>	bytes = 0;
  ZmAtomic<uint64_t>	activityUS = 0;
  uint64_t		base = 0; // immutable for the published path epoch
};
struct alignas(Zm::CacheLineSize) PeerAckSlot {
  uint64_t		epoch = 0;
  uint64_t		delay = 0;
  uint64_t		receivedUS = 0;
  AckRange		range;
  AckECN		ecn;
};
struct alignas(Zm::CacheLineSize) PeerAckRxPub {
  ZmAtomic<uint64_t>	sequence = 0;
  uint32_t		full = 0;
};
struct alignas(Zm::CacheLineSize) PeerAckTxPub {
  ZmAtomic<uint64_t>	readSequence = 0;
  ZmAtomic<unsigned>	waiting = 0;
};
struct TxCryptoSnapshot {
  TransportParams localParams;
  TransportParams peerParams;
  bool peerParamsSet = false;
  bool rxAppSecret = false;
  ZuArray<TrafficSecret, PktNumSpace::N>
			secrets =
			  ZuArray<TrafficSecret, PktNumSpace::N>(
			    PktNumSpace::N);
  TrafficSecret	earlySecret;
  ZuArray<bool, PktNumSpace::N>
			installed =
			  fixedArray<bool, PktNumSpace::N>();
  bool		earlyInstalled = false;
};


struct InitialKeyDir { using T = int8_t; enum { Client, Server }; };

struct CIDSel { using T = int8_t; enum { Initial, Local, Peer }; };

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
  uint64_t		attemptID = 0;
  ZuTime		deadline;
  PathChallenge	challenge;
  MigrationState::T	state = MigrationState::Idle;
  MigrationReason::T	reason = MigrationReason::None;
  bool		active = false;
  bool		localRebind = false;
  bool		closeOnFailure = false;
  bool		armTimer = true;
  bool		sendChallenge = true;
};

struct QLogTxFrameRefs {
  ZuArray<SentFrameRef, PktTxUpdate::MaxFrames> refs;

  QLogTxFrameRefs() = default;
  QLogTxFrameRefs(const SentFrameUpdates &updates) {
    unsigned n = updates.length() < PktTxUpdate::MaxFrames ?
	updates.length() : PktTxUpdate::MaxFrames;
    for (unsigned i = 0; i < n; ++i) refs.push(updates[i].ref);
  }
  unsigned count() const { return refs.length(); }
  const SentFrameRef &operator [](unsigned i) const { return refs[i]; }
};

struct RxEarlyData {
  LinkEarlyState::T	state = LinkEarlyState::None;
  ZeroRTTReason::T	reason = ZeroRTTReason::None;
  uint64_t		bytes = 0;
  bool		oneRTTSeen = false;
};

struct TxEarlyData {
  LinkEarlyState::T	state = LinkEarlyState::None;
  ZeroRTTReason::T	reason = ZeroRTTReason::None;
  uint64_t		bytes = 0;
};

struct TimerRes {
  ZuTime logical;
  ZuTime reserved;
  uint64_t serial = 0;
};

struct alignas(Zm::CacheLineSize) LinkShared {
  ZmAtomic<unsigned>	disconnecting = 0;
  // Advisory public/telemetry snapshot; protocol code uses owner-local state.
  ZmAtomic<unsigned>	publicLinkState = unsigned(LinkState::Closed);
  ZmAtomic<uint64_t>	publicCloseError = 0;
};

struct alignas(Zm::CacheLineSize) CreditRxPub {
  ZmAtomic<uint64_t> limit = 0;
  ZmAtomic<uint64_t> sequence = 0;
};

struct alignas(Zm::CacheLineSize) CreditTxPub {
  ZmAtomic<uint64_t> done = 0;
};

struct alignas(Zm::CacheLineSize) RxAckTimer {
  ZmScheduler::Timer timer;
};

struct alignas(Zm::CacheLineSize) TimerSlot {
  ZmScheduler::Timer node;
};

struct alignas(Zm::CacheLineSize) TimerStorage {
  TimerSlot ackDelay;
  TimerSlot loss;
  TimerSlot pto;
  TimerSlot idle;
  TimerSlot heartBeat;
  TimerSlot close;
  TimerSlot keyDiscard;
  TimerSlot pmtud;
  TimerSlot path;
};

struct alignas(Zm::CacheLineSize) LinkCapacity {
  Zquic_::EndpointTxWait wait;
  Zquic_::EndpointTxToken token;
  Zquic_::PktAllocTarget target;
  Zquic_::EndpointDiscFn done;
  uint64_t runtime = 0, sequence = 0;
  bool needed = false, cancelling = false;
};

struct alignas(Zm::CacheLineSize) LinkPressure {
  Zquic_::EndpointDiscFn done;
  unsigned pending = 0;
  bool restart = false;
};

using PathResponses = Queue<ZmQueue<ControlFrame,
  ZmQueueHeapID<"Zquic.Link.PathResponses">>>;
using NewCxnIDControls = Queue<ZmQueue<ControlFrame,
  ZmQueueHeapID<"Zquic.Link.NewCxnID">>>;
// Bound concrete PATH_RESPONSE replies by the per-packet frame metadata limit.
enum { PathResponseMax = SentPkt::MaxFrames };

struct alignas(Zm::CacheLineSize) LinkTxStorage {
  using TxStreamsRef = ZmRef<Zquic::TxStreams>;

  Zquic::LinkInfo info;
  InitialSecret initialSecret;
  TransportParams localParams;
  TransportParams peerParams;
  bool peerParamsSet = false;
  bool rxAppSecret = false;
  CreditStreams::Node *creditCursor = nullptr;
  uint64_t creditScanSeq = 0;
  uint64_t rxMaxDataSeen = 0;
  bool creditScanning = false;
  bool resetting = false;
  FlowCredit		txDataCredit;
  LinkCloseState	txAppClose;
  LinkState::T		txLinkState = LinkState::Closed;
  TxStreamsRef		txStreams{new TxStreams};
  CreditStreams	creditStreams;
  RttEstimator		rtt;
  PTOBackoff		ptoBackoff;
  // Connection-owned timers; callbacks run on Tx.
  // Active-path timers owned by this Link; callbacks run on Tx.
  ZuTime		lossTimerOut;
  TimerRes		lossRes;
  TimerRes		ptoRes;
  TimerRes		idleRes;
  ZuTime		ptoTimerOut;
  ZuTime		idleTimeout;
  ZuTime		idleTimerOut;
  ZuTime		idleBase;
  ZuTime		heartBeatTimerOut;
  ZuTime		heartBeatBase;
  ZuTime		closeTimerOut;
  ZuTime		closeNextResponse;
  uint64_t		closeDrainSerial = 0;
  PktNumSpace::T	ptoTimerLevel = PktNumSpace::Initial;
  bool			idleAckElicitingSent = false;
  ZuArray<StreamLimit, Zquic::StreamType::N>
		      peerLimit = {
    StreamLimit(MaxStreamCount),
    StreamLimit(MaxStreamCount)
  };
  uint64_t		nextDuplexOrdinal = 0;
  uint64_t		nextUniOrdinal = 0;
  ZuArray<uint64_t, Zquic::StreamType::N>
		      queued =
			fixedArray<uint64_t, Zquic::StreamType::N>();
  ZuArray<bool, Zquic::StreamType::N>
		      openQueuedPending =
			fixedArray<bool, Zquic::StreamType::N>();
  uint64_t		lastDataBlocked = U64Null;
  ZuArray<uint64_t, Zquic::StreamType::N>
		      lastStreamsBlocked = {U64Null, U64Null};
  StreamQueue		streamQueue;
  PendingControl	maxDataControl;
  ZuArray<PendingControl, Zquic::StreamType::N>
		      maxStreamsControl =
			ZuArray<PendingControl, Zquic::StreamType::N>(
			  Zquic::StreamType::N);
  PendingControl	dataBlockedControl;
  ZuArray<PendingControl, Zquic::StreamType::N>
		      streamsBlockedControl =
			ZuArray<PendingControl, Zquic::StreamType::N>(
			  Zquic::StreamType::N);
  PendingControl	handshakeDoneControl;
  PendingControl	pathChallengeControl;
  PathResponses		pathResponses{ZmQueueParams{}.initial(PathResponseMax)};
  NewCxnIDControls	newCxnIDControls{
    ZmQueueParams{}.initial(LocalCIDLimit)};

  ZuArray<CryptoStream, PktNumSpace::N>
		      txCrypto =
			ZuArray<CryptoStream, PktNumSpace::N>(
			  PktNumSpace::N);
  ZuArray<CryptoTxPQueue, PktNumSpace::N>
		      txCryptoUnackd =
			ZuArray<CryptoTxPQueue, PktNumSpace::N>(
			  PktNumSpace::N);
  ZuArray<PktProtState, PktNumSpace::N>
		      txProt =
			ZuArray<PktProtState, PktNumSpace::N>(
			  PktNumSpace::N);
  PktProtState		txEarlyProt;
  TxEarlyData		txEarlyData;
  ZmRef<ZiIOBuf>	coalesceInitial;
  ZiSockAddr		coalesceAddr;
  Zquic_::LinkTxStats	txDiag;
  NewReno		congestion;
  uint64_t		txRuntimeGen = 0;
  unsigned		txSendDepth = 0;
  bool			txSendDeferred = false;
  uint64_t		txPeerAckSeq = 0;
  uint64_t		txPeerAckActive = 0;
  PeerAckQueue		peerAckPending;
  bool			txPeerAckDraining = false;
  bool			txPeerAckFlush = false;
  uint64_t		txPathEpoch = 0;
  uint64_t		txPathRxBytes = 0;
  uint64_t		txPeerActivityUS = 0;
  bool			txPathRxReady = false;
  uint64_t		txAckSeq = 0;
  uint64_t		txAckEpoch = 0;
  uint64_t		txAckKeyGen = 0;
  ZuArray<uint64_t, PktNumSpace::N>
		      txAckSentGen = fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N>
		      txAckRetired = fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N>
		      txPN =
			fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<uint64_t, PktNumSpace::N>
		      txLargestAckd =
			fixedArray<uint64_t, PktNumSpace::N>();
  ZuArray<PktTxSpace, PktNumSpace::N>
		      txPkts =
			ZuArray<PktTxSpace, PktNumSpace::N>(
			  PktNumSpace::N);
  ZuArray<AckSnapshot, PktNumSpace::N>
		      txAck =
			ZuArray<AckSnapshot, PktNumSpace::N>(
			  PktNumSpace::N);
  ZuTime		txInitProbeOut;
  ZuTime		txHandshakeProbeOut;
  ZuTime		txAppProbeOut;
  Path			path;
  PathState		migration;
  uint64_t		nextMigrationAttemptID = 1;
  ZuArray<bool, PktNumSpace::N>
		      txSpaceDiscarded =
			fixedArray<bool, PktNumSpace::N>();
  bool			coalesceLong = false;
  bool			txKeyPhase = false;
  uint64_t		txKeyGeneration = 0;
  CxnID			txPeerCID;
  uint64_t		txPeerSeq = U64Null;
  CxnID			txCandidatePeerCID;
  uint64_t		txCandidatePeerSeq = U64Null;
};

} // Zquic_

namespace Zquic {

template <typename App, typename Impl, typename TxBufAlloc_, typename Stream_>
class Link :
  public Zquic_::ServerLinks::Node,
  public Ztc::Link {
template <typename, typename>
friend class Server;
template <typename, typename, typename>
friend class Zquic::Stream;


  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Link *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    TelKey telKey() const override {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      if (m_type == Ztc::QueueType::Rx)
	m_link->rxQueueTelemetry(data);
      else
	m_link->txQueueTelemetry(data);
    }

  private:
    const Link			*m_link;
    const Ztc::QueueType::T	m_type;
  };

public:
  using StreamType = Zquic::StreamType;

  using Self = Link<App, Impl, TxBufAlloc_, Stream_>;
  using TxBufAlloc = TxBufAlloc_;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  // 64 covers the former common batch; valid larger reap sets grow on heap.
  using ReapStreams = ZtBuiltin<
    ZtArray<Stream *,
      ZtArrayHeapID<"Zquic.Recovery.ReapStreams">>,
    64>;
  using Streams = Streams_<Stream>;
  using StreamsRef = ZmRef<Streams>;
  using RxStreams = StreamRxList<Stream>;
  using TxStreams = Zquic::TxStreams;
  using TxStreamsRef = ZmRef<TxStreams>;
  using ClosedStreams = Zquic_::ClosedStreams;
  using ClosedStreamsRef = ZmRef<ClosedStreams>;
  using PathResponses = Zquic_::PathResponses;
  using NewCxnIDControls = Zquic_::NewCxnIDControls;
  using StreamQueue = Zquic::StreamQueue;
  enum { OpenQueuedBatch = 32 };
  enum { StreamFlushBatch = 64 };
  // Match the writable-stream turn budget during final connection teardown.
  enum { StreamCloseBatch = Zquic_::StreamCloseBatch };
  using StreamCloseWork = Zquic_::StreamCloseWork<Stream, StreamCloseBatch>;
  // PATH_RESPONSE frames are concrete replies; cap queued payloads at the
  // per-packet sent-frame metadata limit.
  enum { PathResponseMax = Zquic_::PathResponseMax };
  enum { RecoveryScanBatch = 256 };
  enum { StreamClassCount = 4 };
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }
  unsigned txQueueMax() const { return Stream::DefltTxQueueMax; }

  Link(App *app, bool isServer = false) :
    Link{app, isServer, ZuID{} << "quic:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  Link(App *app, bool isServer, ZuID id) :
    m_config{app, id, Zquic_::QueueInfo::prefix(id, app->queueOrdinal()), isServer} {
    for (auto &pn : m_linkTx.txLargestAckd) pn = U64Null;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      CryptoStream *crypto = &m_linkRx.rxCrypto[i];
      crypto->allocators(app->cryptoRxAllocator(), nullptr);
      m_linkTx.txCrypto[i].allocators(nullptr, app->cryptoTxAllocator());
      initCryptoDequeue(PktNumSpace::T(i));
    }
    initQueues_();
    app->linkAdded_(this);
    initLinkState(LinkState::Starting);
  }
  ~Link() {
    clearCallbacks();
    assert(!timersActive());
    m_streamQueues.clear();
    app()->linkDeleted_(this, telState(publicLinkState()));
  }

  void initCryptoDequeue(PktNumSpace::T space) {
    m_linkRx.rxCrypto[space].dequeueFn([this, space] {
      app()->rxRun([link = impl(), space, runtime = m_linkRx.rxRuntimeGen] {
	if (runtime != link->m_linkRx.rxRuntimeGen || link->disconnecting_() ||
	    link->linkClosedRx_() || link->m_linkRx.rxSpaceDiscarded[space]) return;
	link->m_linkRx.rxCrypto[space].dequeue();
      });
    });
  }

  void initCryptoDelivery() {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto space = PktNumSpace::T(i);
      CryptoStream *crypto = &m_linkRx.rxCrypto[i];
      initCryptoDequeue(space);
      crypto->deliveryFn([this, space](ZuBSpan span) {
	if (!m_linkRx.crypto.tlsReady() || linkClosedRx_()) return;
	size_t epoch = space == PktNumSpace::Initial ? 0 :
	  space == PktNumSpace::Handshake ? 2 : 3;
	if (!this->impl()->emitTLS_(epoch, span, m_linkRx.rxCryptoAddr[space]))
	  this->tlsFailure();
      });
    }
  }

  void clearCallbacks() {
    for (auto &crypto : m_linkRx.rxCrypto) {
      crypto.dequeueFn({});
      crypto.deliveryFn({});
    }
  }

  App *app() const { return m_config.app; }
  TelKey telKey() const override {
    return {app()->telKey().template p<1>(), m_config.id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = app()->telKey().template p<1>();
    data.id = m_config.id;
    data.rxCalls = m_linkRx.rxDiag.datagramsRx;
    data.txCalls = m_linkTx.txDiag.packetsTx;
    data.rxBytes = m_linkRx.rxDiag.bytesRx;
    data.txBytes = m_linkTx.txDiag.bytesTx;
    data.reconnects = 0;
    data.type = Ztc::LinkType::QUIC;
    data.state = telState(publicLinkState());
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    // Preserve the link-keyed Rx/Tx summaries consumed by Ztc. Detailed
    // queues below are enumerated directly from their actual owners.
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&rxQueue);
    fn(&txQueue);
    unsigned n = 2;
    auto visit = [&fn, &n](const Ztc::Queue *queue) {
      ++n;
      fn(const_cast<Ztc::Queue *>(queue));
    };
    visit(&m_ackRing);
    visit(&m_peerAckRing);
    visit(&m_linkRx.discFns);
    visit(&m_linkTx.peerAckPending);
    visit(&m_linkTx.streamQueue);
    visit(&m_linkTx.pathResponses);
    visit(&m_linkTx.newCxnIDControls);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      visit(&m_ackPost[i]);
      m_linkTx.txPkts[i].allQueues(visit);
      visit(m_linkRx.rxCrypto[i].rxQueue());
      visit(m_linkTx.txCrypto[i].txQueue());
      visit(&m_linkTx.txCryptoUnackd[i]);
    }
    n += m_streamQueues.allQueues(ZuMv(fn));
    return n;
  }
  void up() override { impl()->up_(); }
  void down() override { (void)impl()->disconnect(); }

  bool isServer() const { return m_config.isServer; }
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
  uint64_t closeError() const { return m_linkShared.publicCloseError.load_(); }
  uint64_t streamCount() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx stream count outside Rx thread", return 0);
    return m_linkRx.streams->count_();
  }
  uint64_t peerStreamLimit(Zquic::StreamType::T type) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer stream limit access outside Tx thread", return 0);
    return m_linkTx.peerLimit[type].limit();
  }
  uint64_t localStreamsOpened(Zquic::StreamType::T type) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local stream count access outside Tx thread", return 0);
    return m_linkTx.peerLimit[type].opened();
  }
  uint64_t queuedLocalStreams(Zquic::StreamType::T type) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC queued local stream access outside Tx thread", return 0);
    return m_linkTx.queued[type];
  }
  uint64_t localStreamLimit(Zquic::StreamType::T type) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC local stream limit access outside Rx thread", return 0);
    return m_linkRx.localLimit[type].limit();
  }
  uint64_t peerStreamsOpened(Zquic::StreamType::T type) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer stream count access outside Rx thread", return 0);
    return m_linkRx.localLimit[type].opened();
  }
  bool localStreamsBlocked(Zquic::StreamType::T type) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local stream blocked access outside Tx thread", return false);
    return m_linkTx.queued[type] != 0;
  }
  unsigned queuedControlFrames() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue inspection outside Tx thread", return 0);
    unsigned n = 0;
    if (m_linkTx.maxDataControl.queued) ++n;
    if (m_linkTx.dataBlockedControl.queued) ++n;
    if (m_linkTx.handshakeDoneControl.queued) ++n;
    if (m_linkTx.pathChallengeControl.queued) ++n;
    for (unsigned i = 0; i < 2; ++i) {
      if (m_linkTx.maxStreamsControl[i].queued) ++n;
      if (m_linkTx.streamsBlockedControl[i].queued) ++n;
    }
    n += m_linkTx.pathResponses.count_();
    n += m_linkTx.newCxnIDControls.count_();
    auto iter = m_linkTx.txStreams->citer();
    while (auto node = iter())
      n += static_cast<const Stream *>(node)->queuedControlFrames();
    return n;
  }
  uint64_t rxDataCreditUsed() const { return m_linkRx.rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_linkRx.rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_linkRx.rxDataCredit.available(); }
  uint64_t rxDataRetired() const { return m_linkRx.rxDataRetired; }

  void setPeerStreamLimit(Zquic::StreamType::T type, uint64_t limit) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer stream limit update outside Tx thread", return);
    m_linkTx.peerLimit[type].set(limit);
  }
  void setLocalStreamLimit(Zquic::StreamType::T type, uint64_t limit) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC local stream limit update outside Rx thread", return);
    m_linkRx.localLimit[type].set(limit);
  }

  bool rxApplyMaxStreams(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC MAX_STREAMS receipt outside Rx thread", return false);
    if (!validateMaxStreams(frame)) return false;
    uint64_t value = frame.value;
    Zquic::StreamType::T type = frame.streamType;
    app()->txRun([link = impl(), type, value]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxStreams(type, value);
      link->flushTx_();
    });
    return true;
  }
  bool applyMaxData(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC MAX_DATA receipt outside Rx thread", return false);
    if (!validateMaxData(frame)) return false;
    app()->txInvoke([link = impl(), value = frame.value] {
      if (!link->disconnecting_()) link->txApplyMaxData(value);
    });
    return true;
  }
  bool applyMaxStreamData(const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC MAX_STREAM_DATA receipt outside Rx thread", return false);
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->txClosedRx()) {
	  ++m_linkRx.rxDiag.streamMaxClosedRx;
	  return true;
	}
      } else if (closedStreamID(frame.streamID) &&
	  canLocalSend(frame.streamID)) {
	++m_linkRx.rxDiag.streamMaxClosedRx;
	return true;
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData(frame, stream)) {
      ++m_linkRx.rxDiag.streamMaxInvalidRx;
      noteInvalidStreamActivity(TransportError::StreamState);
      return true;
    }
    app()->txInvoke([link = impl(), stream = ZuMv(stream), value = frame.value]() mutable {
      if (!link->disconnecting_()) link->txApplyMaxStreamData(ZuMv(stream), value);
    });
    return true;
  }
  bool receiveDataBlocked(const Frame &frame) {
    return validateDataBlocked(frame);
  }
  bool rxStreamBlocked(const Frame &frame) {
    return rxStreamBlocked_(frame);
  }
  bool receiveStreamsBlocked(const Frame &frame) {
    return validateStreamsBlocked(frame);
  }
  bool frameLegal(
    PktNumSpace::T level, const Frame &frame, bool earlyData = false) const {
    return packetFrameLegal(level, frame, earlyData);
  }

  StreamRef stream(Zquic::StreamType::T type = Zquic::StreamType::Duplex) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local stream allocation outside Tx thread", return {});
    if (disconnecting_() || linkClosedTx_()) return nullptr;
    if (!m_linkTx.peerLimit[type].open()) {
      ++m_linkTx.queued[type];
      queueBlocked(
	FrameType::StreamsBlocked, 0, m_linkTx.peerLimit[type].limit(), type);
      impl()->streamsBlocked(type, m_linkTx.peerLimit[type].limit());
      return nullptr;
    }
    return openLocalStream(type);
  }

  StreamRef acceptPeerStream(uint64_t id) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer stream acceptance outside Rx thread", return {});
    if (disconnecting_() || linkClosedRx_()) return nullptr;
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (StreamID::server(id) == m_config.isServer) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (closedStreamID(id)) return nullptr;
    Zquic::StreamType::T type = StreamID::uni(id);
    uint64_t opened = StreamID::ordinal(id) + 1;
    if (!m_linkRx.localLimit[type].allowsTo(opened)) return nullptr;

    StreamRef stream = newStream(int64_t(id));
    if (!stream) return nullptr;
    if (!peerOpenedStreamID(id))
      ZiAssert(m_linkRx.localLimit[type].openTo(opened), "Zquic",
	(), "peer stream count advanced past local limit", return nullptr);
    impl()->streamed(stream);
    scheduleStreamWritable(stream);
    return stream;
  }

  StreamRef findStream(int64_t id) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx stream lookup outside Rx thread", return {});
    return m_linkRx.streams->find(id);
  }

protected:
  StreamRef findTxStream_(int64_t id) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx stream lookup outside Tx thread", return {});
    auto node = m_linkTx.txStreams->findPtr(id);
    return node ? StreamRef{static_cast<Stream *>(node)} : StreamRef{};
  }

  void publishStreamRx(StreamRef stream) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx publication outside Rx thread", return);
    if (!stream || stream->id() < 0 || m_linkRx.streams->find(stream->id())) return;
    if (disconnecting_() || linkClosedRx_()) {
      stream->finalRxQueues();
      stream->closeRx();
      return;
    }
    stream->initRxQueues(app(), streamQueueID(stream->id()), m_streamQueues);
    m_linkRx.allStreams.pushNode(static_cast<RxStreams::Node *>(stream.ptr()));
    m_linkRx.streams->addNode(static_cast<Streams::Node *>(stream.ptr()));
  }
  void publishStreamTx(StreamRef stream) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx publication outside Tx thread", return);
    if (!stream || stream->id() < 0 || m_linkTx.txStreams->findPtr(stream->id()))
      return;
    if (disconnecting_() || linkClosedTx_()) {
      stream->finalTxQueues();
      // Publication precedes this fresh stream's first Tx admission.
      unsigned budget = StreamCloseBatch;
      stream->closeTx(budget);
      return;
    }
    stream->initTxQueues(app(), streamQueueID(stream->id()), m_streamQueues);
    m_linkTx.creditStreams.pushNode(static_cast<CreditStreams::Node *>(stream.ptr()));
    if (stream->refreshCreditTx()) streamWritable(stream);
    m_linkTx.txStreams->addNode(ZmRef<TxStreams::Node>{ZuMv(stream)});
  }

public:

  int receiveFrame(const Frame &frame, BufDiag *diag = nullptr) {
    if (frame.type != FrameType::ResetStream &&
	frame.type != FrameType::StopSending)
      return -1;
    StreamRef stream = findOrAccept(frame.streamID);
    if (!stream) {
      if (frame.streamID <= uint64_t(INT64_MAX) &&
	  closedStreamID(frame.streamID)) {
	++m_linkRx.rxDiag.streamCtlClosedRx;
	return 0;
      }
      ++m_linkRx.rxDiag.streamCtlInvalidRx;
      noteInvalidStreamActivity(TransportError::StreamState);
      return -1;
    }
    if (frame.type == FrameType::ResetStream) {
      if (stream->resetReceived()) {
	if (stream->finalSize() == frame.length) return 0;
	++m_linkRx.rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity(TransportError::FinalSize, true, true);
	return -1;
      }
      uint64_t admitted = stream->rxAdmitted();
      if (frame.length < admitted) {
	++m_linkRx.rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity(TransportError::FinalSize, true, true);
	return -1;
      }
      uint64_t novel = frame.length - admitted;
      if (frame.length > stream->rxCreditLimit() ||
	  novel > m_linkRx.rxDataCredit.available()) {
	++m_linkRx.rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity(TransportError::FlowControl, false, true);
	return -1;
      }
      if (!stream->receiveReset(frame)) {
	++m_linkRx.rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity(TransportError::FinalSize, false, true);
	return -1;
      }
      if (!stream->consumeRxCreditTo(frame.length) ||
	  !m_linkRx.rxDataCredit.consume(novel)) {
	++m_linkRx.rxDiag.streamDataFinalRx;
	noteInvalidStreamActivity(TransportError::FlowControl, false, true);
	return -1;
      }
      returnStreamCreditRx(stream);
      impl()->streamResetReceived(stream, frame.errorCode, frame.length);
      bool retired = streamRxRetired(stream, stream->rxRetirable());
      drainResetRx(ZuMv(stream), m_linkRx.rxRuntimeGen, retired);
      return retired ? 0 : -1;
    }
    if (stream->txClosedRx()) return 0;
    if (stream->stopReceived()) return 0;
    if (stream->receiveStop(frame)) {
      impl()->streamStopSendingReceived(stream, frame.errorCode);
      return 0;
    }
    ++m_linkRx.rxDiag.streamCtlInvalidRx;
    noteInvalidStreamActivity(TransportError::StreamState);
    return -1;
  }

  int receiveFrame(
    const Frame &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *immediateAck = nullptr, bool earlyData = false) {
    if (frame.type != FrameType::Stream)
      return receiveFrame(frame, diag);
    StreamRef stream = findOrAccept(frame.streamID);
    if (!stream) {
      if (frame.streamID > uint64_t(INT64_MAX) ||
	  !closedStreamID(frame.streamID) ||
	  !canPeerSend(frame.streamID)) {
	++m_linkRx.rxDiag.streamDataInvalidRx;
	noteInvalidStreamActivity(TransportError::StreamState);
	return -1;
      }
      if (immediateAck) *immediateAck = true;
      return 0;
    }
    if (earlyData) stream->earlyData(true);
    bool rxClosed = stream->rxComplete() || stream->resetReceived();
    int rc = stream->processFrame(frame, m_linkRx.rxDataCredit, ZuMv(packet), diag);
    if (rc >= 0) {
      if (!rc) ++m_linkRx.rxDiag.streamNoDataRx;
      if ((rxClosed || !rc) && immediateAck) *immediateAck = true;
      returnStreamCreditRx(stream);
      impl()->streamData(stream, frame.offset, frame.payload, frame.fin);
      if (frame.fin) reapStreamFromRx(stream);
    } else {
      TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
	TransportError::StreamState : TransportError::FinalSize;
      if (error == TransportError::StreamState)
	++m_linkRx.rxDiag.streamDataStateRx;
      else
	++m_linkRx.rxDiag.streamDataFinalRx;
      noteInvalidStreamActivity(error, false, true);
    }
    return rc;
  }
  void streamRxDequeued(StreamRef stream) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx dequeue processing outside Rx thread", return);
    if (!stream || stream->retiredRx()) return;
    bool wasRxQueued = stream->rxPending() || stream->rxQueued();
    int rc = stream->processRx();
    if (rc >= 0) {
      returnStreamCreditRx(stream);
      if (wasRxQueued && !stream->rxPending() && !stream->rxQueued())
	reapStreamFromRx(stream);
      return;
    }
    TransportError::T error = stream->rxComplete() || stream->resetReceived() ?
      TransportError::StreamState : TransportError::FinalSize;
    if (error == TransportError::StreamState)
      ++m_linkRx.rxDiag.streamRxDeqStateRx;
    else
      ++m_linkRx.rxDiag.streamRxDeqFinalRx;
    noteInvalidStreamActivity(error, false, true);
  }

  bool streamRxRetired(StreamRef stream, uint64_t length) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx retirement outside Rx thread", return false);
    if (!stream || stream->retiredRx()) return false;
    uint64_t used = m_linkRx.rxDataCredit.used();
    if (ZuUnlikely(m_linkRx.rxDataRetired > used)) return false;
    uint64_t retirable = used - m_linkRx.rxDataRetired;
    if (!stream || length > stream->rxRetirable() || length > retirable ||
	m_linkRx.rxDataRetired > U64Null - length)
      return false;
    if (!stream->retireRx_(length)) return false;
    m_linkRx.rxDataRetired += length;
    maybeExtendMaxData();
    maybeExtendMaxStreamData(stream);
    return true;
  }

  void drainResetRx(StreamRef stream, uint64_t runtime, bool reap) {
    if (runtime != m_linkRx.rxRuntimeGen || disconnecting_() || stream->retiredRx()) return;
    do {
      unsigned budget = StreamCloseBatch;
      if (stream->closeRx(budget)) {
	if (reap) reapStreamFromRx(stream);
	return;
      }
      if (app()->mx() && app()->mx()->running()) {
	app()->rxRun([link = impl(), stream = ZuMv(stream), runtime, reap]() mutable {
	  link->drainResetRx(ZuMv(stream), runtime, reap);
	});
	return;
      }
    } while (true);
  }

  void streamWritable(const StreamRef &stream) {
    if (!stream || stream->retiredTx() || disconnecting_() ||
	stream->id() < 0 || stream->txQueued()) return;
    stream->txQueued(true);
    m_linkTx.streamQueue.pushNode(
      static_cast<StreamQueue::Node *>(stream.ptr()));
  }
  void scheduleStreamWritable(StreamRef stream) {
    if (!stream || stream->id() < 0) return;
    app()->txInvoke(impl(), [
      link = impl(),
      stream = ZuMv(stream)
    ]() mutable {
      if (link->disconnecting_()) return link;
      if (link->streamTxPending(stream)) {
	link->streamWritable(stream);
	link->flushTx_();
      }
      return link;
    });
  }
  bool localResetStream(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueResetStream_(streamID, appError, finalSize);
  }
  bool localStopSending(uint64_t streamID, uint64_t appError) {
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
  void newToken(ZuBSpan) { }

protected:
  void up_() { }

  void closeState(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC close initiation outside Rx thread", return);
    cancelAckRx();
    m_linkRx.rxAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_linkShared.publicCloseError = errorCode;
    closeLinkStateRx();
    publishLinkSnapshotTx();
    cancelTimers();
  }
  void closeStateTx(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx close initiation outside Tx thread", return);
    m_linkTx.txAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_linkTx.txLinkState = LinkState::Closing;
    LinkStateSnapshot snapshot = linkSnapshotTx();
    cancelTimers_();
    app()->rxRun([
      link = impl(), runtime = m_linkTx.txRuntimeGen, snapshot
    ]() mutable {
      if (link->disconnecting_() || runtime != link->m_linkRx.rxRuntimeGen) return;
      link->applyCloseSnapshotRx(snapshot);
    });
  }
  void applyCloseSnapshotRx(LinkStateSnapshot snapshot) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC close snapshot apply outside Rx thread", return);
    if (snapshot.state < m_linkRx.rxLinkState ||
	(snapshot.state == m_linkRx.rxLinkState && m_linkRx.rxAppClose.closed))
      return;
    cancelAckRx();
    closeStreamsRx();
    m_linkRx.rxAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_linkShared.publicCloseError = snapshot.error;
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
    m_linkRx.crypto.resetTLS();
  }
  bool writeCloseFrame(PktBuild &build, bool appClose) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close frame construction outside Tx thread", return false);
    int n = appClose ?
      FrameCodec::writeApplicationClose(
	build.scratch(), build.scratchAvail(), m_linkTx.txAppClose.error) :
      FrameCodec::writeConnectionClose(
	build.scratch(), build.scratchAvail(), m_linkTx.txAppClose.error);
    return n > 0 && build.commitScratch(unsigned(n));
  }
  bool appCloseOnDisconnect() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close kind access outside Tx thread", return false);
    return m_linkTx.txAppClose.application || app()->stopping();
  }
  bool sendCloseFrame(ZiSockAddr, bool = false) { return false; }

public:
  bool disconnect(bool peer = false) {
    if (m_linkShared.disconnecting.xch(1)) return false;
    disconnect_(peer, []() { });
    return true;
  }
  template <typename L>
  void disconnect_(bool peer, L &&l) {
    m_linkShared.disconnecting = 1;
    app()->rxInvoke([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->beginDisconnectRx(peer, Zquic_::LinkDiscFn{ZuMv(l)});
    });
  }
  void disconnecting_(bool disconnecting) {
    m_linkShared.disconnecting = unsigned(disconnecting);
  }
  bool disconnecting_() const { return m_linkShared.disconnecting.load_(); }
  void closeForStreamActivity(TransportError::T error) {
    ++m_linkRx.rxDiag.suspectStreamCloses;
    m_linkRx.suspiciousStreamClosed = true;
    if (!m_linkRx.rxAppClose.closed) closeState(error);
  }
  void noteInvalidStreamActivity(
    TransportError::T error, bool closedStream = false, bool immediate = false) {
    ++m_linkRx.rxDiag.invalidStreamFrames;
    if (closedStream) ++m_linkRx.rxDiag.closedStreamFrames;
    if (m_linkRx.suspiciousStreamClosed) return;
    if (immediate || ++m_linkRx.suspiciousStreamFrames >= SuspiciousStreamThreshold)
      closeForStreamActivity(error);
  }
  Zquic::LinkInfo linkInfo() const { return linkInfo_(); }

protected:
  using InitialKeyDir = Zquic_::InitialKeyDir;
  using CIDSel = Zquic_::CIDSel;
  enum { PNLength = 2 };
  // RFC9000 specifies only a minimum/default of 2 for active_connection_id_limit.
  // This is the local advertised policy cap for active peer-issued CIDs.
  enum { LocalCIDLimit = Zquic::LocalCIDLimit };
  enum { SuspiciousStreamThreshold = 8 };
  enum { PathValidPTOFactor = 3 };
  static constexpr ZuTime PathValidMinTimeout{1};

  using LinkCID = Zquic_::LinkCID;
  using PathState = Zquic_::PathState;
  using AckSnapshot = Zquic_::AckSnapshot;
  using RxAckMeta = Zquic_::RxAckMeta;
  using TxAckWork = Zquic_::TxAckWork;
  using PeerAckPending = Zquic_::PeerAckPending;
  using PeerAckQueue = Zquic_::PeerAckQueue;
  using AckPost = Zquic_::AckPost;
  using AckHandoff = Zquic_::AckHandoff;
  using AckCompletion = Zquic_::AckCompletion;
  using AckPubSlot = Zquic_::AckPubSlot;
  using AckRxPub = Zquic_::AckRxPub;
  using AckTxPub = Zquic_::AckTxPub;
  using RxPathPub = Zquic_::RxPathPub;
  using PeerAckSlot = Zquic_::PeerAckSlot;
  using PeerAckRxPub = Zquic_::PeerAckRxPub;
  using PeerAckTxPub = Zquic_::PeerAckTxPub;
  using TxCryptoSnapshot = Zquic_::TxCryptoSnapshot;
  enum { AckPubSlots = Zquic_::AckPubSlots };
  struct TxPktRefs {
    ZuArray<SentFrameRef, SentPkt::MaxFrames> refs;
    ZuArray<Stream *, SentPkt::MaxFrames> streams;

    bool add(const SentFrameRef &ref, Stream *stream = nullptr) {
      if (ref.kind == SentFrameKind::None ||
	  refs.length() >= SentPkt::MaxFrames)
	return false;
      refs.push(ref);
      streams.push(stream);
      return true;
    }
    unsigned count() const { return refs.length(); }
    const SentFrameRef &operator [](unsigned i) const { return refs[i]; }
    Stream *stream(unsigned i) const { return streams[i]; }
  };
  using QLogTxFrameRefs = Zquic_::QLogTxFrameRefs;
  using SentControls = ZuArray<ControlFrame, SentPkt::MaxFrames>;
  using PendingControl = Zquic_::PendingControl;
  using RxEarlyData = Zquic_::RxEarlyData;
  using TxEarlyData = Zquic_::TxEarlyData;
  using LocalCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.LocalCID">>;
  using PeerCIDs =
    ZtArray<LinkCID, ZtArrayHeapID<"Zquic.Link.PeerCID">>;

  unsigned scheduledStreamCount() const {
    return m_linkTx.streamQueue.count_();
  }

  bool queueControl(const ControlFrame &frame) {
    if (!frame || disconnecting_()) return false;
    app()->txInvoke(impl(), [link = impl(), frame]() mutable {
      if (link->disconnecting_()) return link;
      link->txQueueControl(frame);
      return link;
    });
    return true;
  }
  bool txQueueControl(const ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control queue mutation outside Tx thread", return false);
    if (!frame) return false;
    switch (frame.type) {
      case FrameType::MaxData:
	return queuePendingControl(m_linkTx.maxDataControl, frame);
      case FrameType::MaxStreams:
	return queuePendingControl(
	  m_linkTx.maxStreamsControl[streamTypeIndex(frame.streamType)], frame);
      case FrameType::DataBlocked:
	return queuePendingControl(m_linkTx.dataBlockedControl, frame);
      case FrameType::StreamsBlocked:
	return queuePendingControl(
	  m_linkTx.streamsBlockedControl[streamTypeIndex(frame.streamType)], frame);
      case FrameType::PathChallenge:
	return queuePendingControl(m_linkTx.pathChallengeControl, frame);
      case FrameType::PathResponse:
	m_linkTx.pathResponses.push(frame);
	while (m_linkTx.pathResponses.count_() > PathResponseMax) {
	  m_linkTx.pathResponses.queueFull();
	  m_linkTx.pathResponses.shift();
	}
	return true;
      case FrameType::NewCxnID:
	m_linkTx.newCxnIDControls.push(frame);
	while (m_linkTx.newCxnIDControls.count_() > LocalCIDLimit) {
	  m_linkTx.newCxnIDControls.queueFull();
	  m_linkTx.newCxnIDControls.shift();
	}
	return true;
      case FrameType::HandshakeDone:
	return queuePendingControl(m_linkTx.handshakeDoneControl, frame);
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending: {
	if (frame.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	if (!stream) return false;
	bool queued = false;
	switch (frame.type) {
	  case FrameType::MaxStreamData:
	    queued = stream->queueMaxStreamData(frame.value);
	    break;
	  case FrameType::StreamDataBlocked:
	    queued = stream->queueDataBlocked(frame.value);
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
	if (queued) streamWritable(stream);
	return queued;
      }
      default:
	return false;
    }
  }
  bool queueFlowUpdate(const FlowUpdate &update) {
    return update.needed() && queueControl(ControlFrame::flowUpdate(update));
  }
  void postFlowUpdate(FlowUpdate update) {
    app()->txRun([
      link = impl(), update = ZuMv(update)
    ]() mutable {
      if (link->disconnecting_()) return;
      if (link->queueFlowUpdate(update)) link->queueTxFlush();
    });
  }

  void requestCreditTx() {
    m_creditRxPub.sequence = ++m_linkRx.creditSequence;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (m_linkRx.creditPosted &&
	uint64_t(m_creditTxPub.done) < m_linkRx.creditPosted) return;
    m_linkRx.creditPosted = m_linkRx.creditSequence;
    app()->txRun([link = impl(), runtime = m_linkRx.rxRuntimeGen]() mutable {
      if (runtime != link->m_linkTx.txRuntimeGen) return;
      link->startCreditTx();
    });
  }
  void startCreditTx() {
    if (disconnecting_() || m_linkTx.resetting || m_linkTx.creditScanning) return;
    uint64_t sequence = m_creditRxPub.sequence;
    if (sequence <= uint64_t(m_creditTxPub.done)) return;
    m_linkTx.creditScanning = true;
    m_linkTx.creditScanSeq = sequence;
    m_linkTx.creditCursor = m_linkTx.creditStreams.headPtr();
    serviceCreditTx();
  }

  bool refreshCreditTx(uint64_t blocked = U64Null) {
    uint64_t maximum = m_creditRxPub.limit;
    if (maximum <= m_linkTx.rxMaxDataSeen && maximum <= blocked) return false;
    m_linkTx.rxMaxDataSeen = maximum;
    return queueFlowUpdate(FlowUpdate{
      FrameType::MaxData, 0, maximum, Zquic::StreamType::Duplex});
  }

  void blockedCreditRx(const Frame &frame) {
    app()->txRun([link = impl(), runtime = m_linkRx.rxRuntimeGen,
	streamID = frame.streamID, value = frame.value,
	streamBlocked = frame.type == FrameType::StreamDataBlocked]() mutable {
      if (runtime != link->m_linkTx.txRuntimeGen || link->disconnecting_()) return;
      bool queued = false;
      if (streamBlocked) {
	if (StreamRef stream = link->findTxStream_(int64_t(streamID)))
	  if ((queued = stream->refreshCreditTx(value))) link->streamWritable(stream);
      } else queued = link->refreshCreditTx(value);
      if (queued) link->flushTx_();
    });
  }

  void serviceCreditTx() {
    uint64_t runtime = m_linkTx.txRuntimeGen;
    bool queued = refreshCreditTx();
    for (unsigned i = 0; i < StreamFlushBatch && m_linkTx.creditCursor; ++i) {
      auto node = m_linkTx.creditCursor;
      m_linkTx.creditCursor = static_cast<CreditStreams::Node::Ext *>(node)->next;
      auto stream = static_cast<Stream *>(node);
      if (stream->refreshCreditTx()) {
	streamWritable(StreamRef{stream});
	queued = true;
      }
    }
    if (queued) impl()->flushTx_();
    if (runtime != m_linkTx.txRuntimeGen || disconnecting_()) return;
    if (!m_linkTx.creditCursor) {
      m_linkTx.creditScanning = false;
      m_creditTxPub.done = m_linkTx.creditScanSeq;
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      uint64_t sequence = m_creditRxPub.sequence;
      if (sequence <= m_linkTx.creditScanSeq) return;
      m_linkTx.creditScanning = true;
      m_linkTx.creditScanSeq = sequence;
      m_linkTx.creditCursor = m_linkTx.creditStreams.headPtr();
    }
    app()->txRun([link = impl(), runtime]() mutable {
      if (runtime != link->m_linkTx.txRuntimeGen || link->disconnecting_()) return;
      link->serviceCreditTx();
    });
  }
  bool queueBlocked(
    FrameType::T type, uint64_t streamID, uint64_t value,
    Zquic::StreamType::T streamType = Zquic::StreamType::Duplex) {
    ControlFrame frame = ControlFrame::blocked(
      type, streamID, value, streamType);
    if (!blockedFrameNeeded(frame)) return false;
    if (!queueControl(frame)) return false;
    noteBlockedQueued(frame);
    ZquicLOG(app()->qlogTrace(), ([
      type,
      streamID,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      switch (type) {
	case FrameType::DataBlocked:
	  o.logCxnDataBlockedUpd(
	    BlockedEvt{
	      .linkInfo = linkInfo,
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::CxnFlowCtrl},
	    time);
	  break;
	case FrameType::StreamDataBlocked:
	  o.logStreamDataBlockedUpd(
	    BlockedEvt{
	      .linkInfo = linkInfo,
	      .streamID = streamID,
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::StreamFlowCtrl},
	    time);
	  break;
	case FrameType::StreamsBlocked:
	  o.logStreamDataBlockedUpd(
	    BlockedEvt{
	      .linkInfo = linkInfo,
	      .streamID = streamID,
	      .oldState = BlockedState::Unblocked,
	      .newState = BlockedState::Blocked,
	      .reason = BlockedReason::StreamID},
	    time);
	  break;
	default:
	  break;
      }
    }));
    return true;
  }
  bool queuePathResponse_(ZuBSpan data) {
    return queueControl(ControlFrame::pathResponse(data));
  }
  bool queuePathChallenge(ZuBSpan data) {
    return queueControl(ControlFrame::pathChallenge(data));
  }
  bool queueHandshakeDone() {
    return queueControl(ControlFrame::handshakeDone());
  }
  bool queueResetStream_(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    return queueControl(ControlFrame::resetStream(
      streamID, appError, finalSize));
  }
  bool queueStopSending_(uint64_t streamID, uint64_t appError) {
    return queueControl(ControlFrame::stopSending(streamID, appError));
  }
  bool rxApplyMaxData(const Frame &frame) {
    if (!validateMaxData(frame)) return false;
    uint64_t value = frame.value;
    app()->txRun([link = impl(), value]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxData(value);
      link->flushTx_();
    });
    return true;
  }
  bool rxApplyMaxStreamData(const Frame &frame) {
    if (frame.type == FrameType::MaxStreamData &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->txClosedRx()) {
	  ++m_linkRx.rxDiag.streamMaxClosedRx;
	  return true;
	}
      } else if (closedStreamID(frame.streamID) &&
	  canLocalSend(frame.streamID)) {
	++m_linkRx.rxDiag.streamMaxClosedRx;
	return true;
      }
    }
    StreamRef stream;
    if (!validateMaxStreamData(frame, stream)) {
      ++m_linkRx.rxDiag.streamMaxInvalidRx;
      noteInvalidStreamActivity(TransportError::StreamState);
      return false;
    }
    uint64_t value = frame.value;
    app()->txRun([
      link = impl(),
      stream = ZuMv(stream),
      value
    ]() mutable {
      if (link->disconnecting_()) return;
      link->txApplyMaxStreamData(ZuMv(stream), value);
      link->flushTx_();
    });
    return true;
  }
  bool txApplyMaxData(uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_DATA processing outside Tx thread", return false);
    if (value <= m_linkTx.txDataCredit.limit()) return true;
    ZquicLOG(app()->qlogTrace(), ([
      wasBlocked = m_linkTx.txDataCredit.blocked(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (wasBlocked) {
	o.logCxnDataBlockedUpd(
	  BlockedEvt{
	    .linkInfo = linkInfo,
	    .oldState = BlockedState::Blocked,
	    .newState = BlockedState::Unblocked,
	    .reason = BlockedReason::CxnFlowCtrl},
	  time);
      }
    }));
    m_linkTx.txDataCredit.extend(value);
    m_linkTx.dataBlockedControl = {};
    impl()->flowCredit(
      FrameType::MaxData, 0, Zquic::StreamType::Duplex);
    return true;
  }
  bool txApplyMaxStreamData(StreamRef stream, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAM_DATA processing outside Tx thread", return false);
    if (!stream) return false;
    if (stream->resetSent() || stream->finDequeued()) return true;
    uint64_t streamID = uint64_t(stream->id());
    ZquicLOG(app()->qlogTrace(), ([
      streamID,
      wasBlocked = stream->txRangeCount() && !stream->txCreditAvailable(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (wasBlocked) {
	o.logStreamDataBlockedUpd(
	  BlockedEvt{
	    .linkInfo = linkInfo,
	    .streamID = streamID,
	    .oldState = BlockedState::Blocked,
	    .newState = BlockedState::Unblocked,
	    .reason = BlockedReason::StreamFlowCtrl},
	  time);
      }
    }));
    stream->extendTxCredit(value);
    stream->clearControl(SentFrameRef::blocked(
      FrameType::StreamDataBlocked, streamID,
      stream->lastBlocked(), Zquic::StreamType::Duplex));
    if (streamTxPending(stream)) streamWritable(stream);
    impl()->flowCredit(
      FrameType::MaxStreamData, streamID, Zquic::StreamType::Duplex);
    return true;
  }
  bool txApplyMaxStreams(Zquic::StreamType::T type, uint64_t value) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC MAX_STREAMS processing outside Tx thread", return false);
    m_linkTx.peerLimit[type].extend(value);
    openQueued(type, OpenQueuedBatch);
    scheduleOpenQueued(type);
    impl()->streamCredit(type);
    return true;
  }
  void postPeerStreamLimitsTx(uint64_t duplex, uint64_t simplex, uint64_t data = U64Null) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer stream limit publication outside Rx thread", return);
    app()->txRun([link = impl(), duplex, simplex, data]() mutable {
      if (link->disconnecting_()) return;
      if (!ZuNull(data)) link->m_linkTx.txDataCredit.set(data);
      link->applyPeerStreamLimitsTx(duplex, simplex);
    });
  }
  void applyPeerStreamLimitsTx(uint64_t duplex, uint64_t simplex) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer stream limit apply outside Tx thread", return);
    m_linkTx.peerLimit[Zquic::StreamType::Duplex].set(duplex);
    m_linkTx.peerLimit[Zquic::StreamType::Simplex].set(simplex);
  }

  bool establishedRx() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_linkRx.rxLinkState == LinkState::Established;
  }
  bool establishedTx() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_linkTx.txLinkState == LinkState::Established;
  }
  bool closingRx_() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_linkRx.rxLinkState == LinkState::Closing;
  }
  bool closingTx_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_linkTx.txLinkState == LinkState::Closing;
  }
  bool debugLog() const {
#if defined(Zquic_DEBUG) && defined(ZiMultiplex_DEBUG)
    return app()->mx()->debug();
#else
    return false;
#endif
  }
  bool drainingRx() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_linkRx.rxLinkState == LinkState::Draining;
  }
  bool drainingTx() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_linkTx.txLinkState == LinkState::Draining;
  }
  bool handshakeStartedRx() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx lifecycle access outside Rx thread", return false);
    return m_linkRx.rxLinkState == LinkState::Handshaking ||
      m_linkRx.rxLinkState == LinkState::Established;
  }
  bool handshakeStartedTx() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx lifecycle access outside Tx thread", return false);
    return m_linkTx.txLinkState == LinkState::Handshaking ||
      m_linkTx.txLinkState == LinkState::Established;
  }
  bool linkClosedRx_() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx close access outside Rx thread", return true);
    return m_linkRx.rxAppClose.closed;
  }
  bool linkClosedTx_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx close access outside Tx thread", return true);
    return m_linkTx.txAppClose.closed;
  }
  uint64_t closeErrorTx_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx close error access outside Tx thread", return 0);
    return m_linkTx.txAppClose.error;
  }
  uint64_t closeErrorRx_() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx close error access outside Rx thread", return 0);
    return m_linkRx.rxAppClose.error;
  }
  LinkRxDiag rxDiagSnap() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return {});
    return m_linkRx.rxDiag;
  }
  LinkTxDiag txDiagSnap() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx diagnostic snapshot outside Tx thread", return {});
    return txDiag();
  }
  template <typename L>
  void diag(L &&l) const {
    auto link = const_cast<Link *>(this)->impl();
    if (rxInvoked_()) {
      diagRx(ZuFwd<L>(l));
      return;
    }
    app()->rxRun([link, l = ZuFwd<L>(l)]() mutable {
      link->diagRx(ZuMv(l));
    });
  }
  template <typename L>
  void diagRx(L &&l) const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic snapshot outside Rx thread", return);
    auto link = const_cast<Link *>(this)->impl();
    LinkRxDiag rx = m_linkRx.rxDiag;
    app()->txRun([link, rx, l = ZuFwd<L>(l)]() mutable {
      LinkDiag diag{rx, link->txDiagSnap()};
      l(diag);
    });
  }
  LinkDiag diag_() const {
    return {m_linkRx.rxDiag, txDiag()};
  }
  template <typename L>
  void pathDiag(L &&l) const {
    if (txInvoked_()) {
      PathDiag diag = pathDiag_();
      l(diag);
      return;
    }
    auto link = const_cast<Link *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      PathDiag diag = link->pathDiag_();
      l(diag);
    });
  }
  PathDiag pathDiag_() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path diagnostic snapshot outside Tx thread", return {});
    return m_linkTx.path.diag();
  }
  bool pathValidated_() const { return m_linkTx.path.validated(); }
  unsigned activePathMaxUDP_() const { return m_linkTx.path.activeMaxUDP(); }
  const ZiSockAddr &activePathRemote_() const { return m_linkTx.path.remote(); }
  uint64_t pathAntiAmp_() const {
    return m_linkTx.path.antiAmpRemaining();
  }
  Crypto &crypto_() { return m_linkRx.crypto; }
  const Crypto &crypto_() const { return m_linkRx.crypto; }
  void snapshotTxCrypto(TxCryptoSnapshot &snapshot) const {
    snapshot.localParams = m_linkRx.transportParams;
    snapshot.peerParamsSet = m_linkRx.crypto.peerParamsSet();
    if (snapshot.peerParamsSet) snapshot.peerParams = m_linkRx.crypto.peerParams();
    snapshot.rxAppSecret = m_linkRx.crypto.rxSecretInstalled(PktNumSpace::AppData);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto level = PktNumSpace::T(i);
      snapshot.installed[i] = m_linkRx.crypto.txSecretInstalled(level);
      if (snapshot.installed[i])
	snapshot.secrets[i] = m_linkRx.crypto.txSecret(level);
    }
    snapshot.earlyInstalled =
      m_linkRx.crypto.txKeySecretInstalled(PktKeyLevel::ZeroRTT);
    if (snapshot.earlyInstalled)
      snapshot.earlySecret =
	m_linkRx.crypto.txKeySecret(PktKeyLevel::ZeroRTT);
  }
	  bool txInstallSecret(
	    PktNumSpace::T level, const TrafficSecret &secret) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx traffic secret install outside Tx thread", return false);
    if (!secret.valid()) return false;
    if (!m_linkTx.txProt[level].init(secret, level, true)) {
      m_linkTx.txProt[level].clear();
      return false;
    }
    return true;
  }
  bool rxInstallKeySecret(
    PktKeyLevel::T level, const TrafficSecret &secret) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx key traffic secret install outside Rx thread", return false);
    return m_linkRx.crypto.rxKeySecret(level, secret);
  }
  bool txInstallKeySecret(
    PktKeyLevel::T level, const TrafficSecret &secret) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx key traffic secret install outside Tx thread", return false);
    if (level != PktKeyLevel::ZeroRTT)
      return txInstallSecret(spaceFromKeyLevel(level), secret);
    if (!secret.valid()) return false;
    if (!m_linkTx.txEarlyProt.init(secret, PktNumSpace::AppData, true)) {
      m_linkTx.txEarlyProt.clear();
      return false;
    }
    return true;
  }
  bool installTxCrypto(const TxCryptoSnapshot &snapshot) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx crypto snapshot install outside Tx thread", return false);
    m_linkTx.localParams = snapshot.localParams;
    m_linkTx.peerParams = snapshot.peerParams;
    m_linkTx.peerParamsSet = snapshot.peerParamsSet;
    m_linkTx.rxAppSecret = snapshot.rxAppSecret;
    for (unsigned i = 0; i < PktNumSpace::N; ++i)
      if (!m_linkTx.txSpaceDiscarded[i] && snapshot.installed[i] &&
	  !txInstallSecret(PktNumSpace::T(i), snapshot.secrets[i]))
	return false;
    if (snapshot.earlyInstalled &&
	!txInstallKeySecret(
	  PktKeyLevel::ZeroRTT, snapshot.earlySecret))
      return false;
    return true;
  }
  bool txSecretInstalled_(PktNumSpace::T level) const {
    return m_linkTx.txProt[level].valid();
  }
  bool txKeySecretInstalled_(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_linkTx.txEarlyProt.valid() :
      txSecretInstalled_(spaceFromKeyLevel(level));
  }
  bool canTxZeroRTT_() const {
    return !establishedTx() &&
      txKeySecretInstalled_(PktKeyLevel::ZeroRTT);
  }
  bool rxEarlyDataAccepted() const {
    return m_linkRx.rxEarlyData.state == LinkEarlyState::Accepted ||
      m_linkRx.rxEarlyData.state == LinkEarlyState::Done;
  }
  bool rxEarlyDataRejected() const {
    return m_linkRx.rxEarlyData.state == LinkEarlyState::Rejected;
  }
  bool txEarlyDataRejected() const {
    return m_linkTx.txEarlyData.state == LinkEarlyState::Rejected;
  }
  LinkEarlyState::T rxEarlyDataState() const {
    return m_linkRx.rxEarlyData.state;
  }
  LinkEarlyState::T txEarlyDataState() const {
    return m_linkTx.txEarlyData.state;
  }
  bool rxOneRTTSeen_() const { return m_linkRx.rxEarlyData.oneRTTSeen; }
  void rememberZeroRTTParams(const TransportParams &params) {
    m_linkRx.zeroRTTParams = params;
    m_linkRx.zeroRTTParamsSet = true;
  }
  void clearZeroRTTParams() {
    m_linkRx.zeroRTTParamsSet = false;
    m_linkRx.zeroRTTParams = {};
  }
  ZeroRTTReason::T validatePeerZeroRTT() const {
    if (!m_linkRx.zeroRTTParamsSet) return ZeroRTTReason::None;
    if (!m_linkRx.crypto.peerParamsSet())
      return ZeroRTTReason::TransportParams;
    return validateZeroRTTParams(
      m_linkRx.zeroRTTParams, m_linkRx.crypto.peerParams());
  }
  const TrafficSecret &txSecret_(PktNumSpace::T level) const {
    return m_linkTx.txProt[level].secret;
  }
  const TrafficSecret &txKeySecret_(PktKeyLevel::T level) const {
    return level == PktKeyLevel::ZeroRTT ?
      m_linkTx.txEarlyProt.secret :
      txSecret_(spaceFromKeyLevel(level));
  }
  PktProtState &txProtState_(PktNumSpace::T level) {
    return m_linkTx.txProt[level];
  }
  PktProtState &txKeyProtState_(PktKeyLevel::T level) {
    return level == PktKeyLevel::ZeroRTT ?
      m_linkTx.txEarlyProt : txProtState_(spaceFromKeyLevel(level));
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
	StreamRef stream_ = findTxStream_(int64_t(ref.streamID));
	return stream_ &&
	  (stream_->txStillUnackd(ref.offset, ref.length, ref.fin) ||
	    stream_->recordTxUnackd(ref.offset, ref.length, ref.fin));
      }
      case SentFrameKind::Crypto:
	if (txRangeStillUnackd(
	      m_linkTx.txCryptoUnackd[level], ref.offset, ref.length))
	  return true;
	return m_linkTx.txCryptoUnackd[level].add(
	    new CryptoTxPQueue::Node{
	      TxUnackdRange{ref.offset, ref.length}}) != ZmPQResult::Invalid;
      default:
	return true;
    }
  }
  void recordTxUnackd_(PktNumSpace::T level, const TxPktRefs &refs) {
    for (unsigned i = 0, n = refs.count(); i < n; ++i)
      (void)recordTxUnackd_(level, refs[i], refs.stream(i));
  }
  bool requeueStreamRef(
    PktNumSpace::T level, const SentFrameRef &ref,
    Stream *stream = nullptr, bool consumeCredit = false) {
    if (ref.kind != SentFrameKind::Stream) return false;
    if (consumeCredit && ref.length)
      if (!m_linkTx.txDataCredit.consume(ref.length)) return false;
    if (!recordTxUnackd_(level, ref, stream)) return false;
    return requeueRetransmit_(level, ref);
  }
  void requeueStreamRefs(
    PktNumSpace::T level, const TxPktRefs &refs, bool consumeCredit,
    bool queueTx = true) {
    bool queued = false;
    for (unsigned i = 0, n = refs.count(); i < n; ++i) {
      const SentFrameRef &ref = refs[i];
      queued |=
	requeueStreamRef(level, ref, refs.stream(i), consumeCredit);
    }
    if (queued && queueTx && !pktAllocationPending()) impl()->queueRetransmit();
  }
  bool txAckEliciting(ZuBSpan frame, const TxPktRefs *refs, bool fallback) {
    if (fallback) return true;
    if (refs && refs->count()) return true;
    SentFrameRef ref;
    bool ackEliciting = false;
    return sentFrameRef(frame, ref, ackEliciting) ? ackEliciting : fallback;
  }
  bool ackTxFrame(
    PktNumSpace::T level, const SentFrameRef &ref, Stream *stream = nullptr) {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	if (stream) return stream->ackTxUnackd(ref.offset, ref.length, ref.fin);
	StreamRef stream_ = findTxStream_(int64_t(ref.streamID));
	return stream_ && stream_->ackTxUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return m_linkTx.txCryptoUnackd[level].clear(ref.offset, ref.length);
      case SentFrameKind::Control: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream_;
	if (!stream) {
	  stream_ = findTxStream_(int64_t(ref.streamID));
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
	    controlAckd(ref);
	    return true;
	}
      }
      default:
	return true;
    }
  }
  bool txRangeStillUnackd(
    const CryptoTxPQueue &queue, uint64_t offset, uint64_t length) const {
    if (!length) return false;
    bool found = false;
    queue.spans(offset, length, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }
  bool frameStillUnackd(
    PktNumSpace::T level, const SentFrameRef &ref) const {
    switch (ref.kind) {
      case SentFrameKind::Stream: {
	if (ref.streamID > uint64_t(INT64_MAX)) return false;
	StreamRef stream = findTxStream_(int64_t(ref.streamID));
	return stream &&
	  stream->txStillUnackd(ref.offset, ref.length, ref.fin);
      }
      case SentFrameKind::Crypto:
	return txRangeStillUnackd(
	  m_linkTx.txCryptoUnackd[level], ref.offset, ref.length);
      case SentFrameKind::Control:
	return controlStillValid_(controlFrame(ref));
      default:
	return true;
    }
  }
  static bool clipStreamRef(
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
  bool clipStreamRetx(SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Stream ||
	ref.streamID > uint64_t(INT64_MAX))
      return false;
    StreamRef stream = findTxStream_(int64_t(ref.streamID));
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
      if (!clipStreamRef(tail, end, oldEnd)) tail = {};
    }
    return clipStreamRef(ref, first, end);
  }
  bool clipCryptoRetx(
    PktNumSpace::T level, SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Crypto ||
	m_linkTx.txSpaceDiscarded[level] || !ref.length)
      return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    m_linkTx.txCryptoUnackd[level].spans(
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
    if (first < ref.offset) first = ref.offset;
    if (end > oldEnd) end = oldEnd;
    ref.offset = first;
    ref.length = end - first;
    return true;
  }
  unsigned cryptoRetxMax(PktNumSpace::T level) const {
    unsigned maxUDP = m_linkTx.path.activeMaxUDP();
    if (maxUDP > BufSize) maxUDP = BufSize;
    switch (level) {
      case PktNumSpace::Initial:
	return longCryptoRetxMax(
	  maxUDP, level,
	  m_config.isServer ? CIDSel::Peer : CIDSel::Initial,
	  InitialSecret::TagLen, impl()->initialTxToken());
      case PktNumSpace::Handshake:
	if (!txSecretInstalled_(level)) return 0;
	return longCryptoRetxMax(
	  maxUDP, level, CIDSel::Peer,
	  txSecret_(level).tagLen, {});
      case PktNumSpace::AppData:
	if (!txSecretInstalled_(level)) return 0;
	return shortCryptoRetxMax(
	  maxUDP, txSecret_(level).tagLen);
      default:
	return 0;
    }
  }
  unsigned longCryptoRetxMax(
    unsigned maxUDP, PktNumSpace::T level, CIDSel::T dcid,
    unsigned tagLen, ZuBSpan token) const {
    enum { LenCalcMax = 4 };
    unsigned pnLength = txPNLength(level);
    unsigned plainLen = maxUDP;
    for (unsigned i = 0; i < LenCalcMax; ++i) {
      int headerLen = level == PktNumSpace::Initial ?
	Pkt::initialHdrLen(
	  headerCID(dcid), headerCID(CIDSel::Local), token,
	  uint64_t(plainLen) + tagLen, pnLength) :
	Pkt::handshakeHdrLen(
	  headerCID(dcid), headerCID(CIDSel::Local),
	  uint64_t(plainLen) + tagLen, pnLength);
      if (headerLen < 0) return 0;
      unsigned overhead = unsigned(headerLen) + pnLength + tagLen;
      unsigned nextPlainLen = maxUDP > overhead ? maxUDP - overhead : 0;
      if (nextPlainLen == plainLen) return nextPlainLen;
      plainLen = nextPlainLen;
    }
    return plainLen;
  }
  unsigned shortCryptoRetxMax(unsigned maxUDP, unsigned tagLen) const {
    int headerLen =
      Pkt::shortHdrLen(headerCID(CIDSel::Peer),
	txPNLength(PktNumSpace::AppData));
    if (headerLen < 0) return 0;
    unsigned overhead = unsigned(headerLen) + tagLen;
    return maxUDP > overhead ? maxUDP - overhead : 0;
  }
  ZuBSpan initialTxToken() const {
    return {};
  }
  bool clipRetransmit(
    PktNumSpace::T level, SentFrameRef &ref, SentFrameRef &tail) {
    switch (ref.kind) {
      case SentFrameKind::Stream:
	return clipStreamRetx(ref, tail);
      case SentFrameKind::Crypto:
	return clipCryptoRetx(level, ref, tail);
      case SentFrameKind::Control:
	tail = {};
	return controlStillValid_(controlFrame(ref));
      default:
	tail = {};
	return true;
    }
  }

  void resetDiagRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC Rx diagnostic reset outside Rx thread", return);
    m_linkRx.rxDiag = {};
  }
  void resetDiagTx() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx diagnostic reset outside Tx thread", return);
    m_linkTx.txDiag = {};
  }
  LinkTxDiag txDiag() const {
    LinkTxDiag diag = m_linkTx.txDiag;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      const PktTxSpace &space = m_linkTx.txPkts[i];
      diag.pktBytesInFlight[i] = space.bytesInFlight();
      diag.sentPackets[i] = space.count();
      diag.retransmitPending[i] = space.retransmitPending();
      diag.retransmittable[i] = space.retransmittable();
    }
    diag.ptoTimerActive = !!m_timers.pto.node;
    diag.lossTimerActive = !!m_timers.loss.node;
    diag.ptoBackoff = m_linkTx.ptoBackoff.count();
    diag.ptoTimeoutUS =
      uint64_t(m_linkTx.ptoBackoff.timeout(m_linkTx.rtt, peerMaxAckDelay()).microsecs());
    return diag;
  }
  void updateCongestionDiag() {
    m_linkTx.txDiag.congestionWindow = uint64_t{0};
    m_linkTx.txDiag.congestionSSThresh = uint64_t{0};
    if (ZuUnlikely(app()->congestionControl())) {
      m_linkTx.txDiag.congestionWindow = m_linkTx.congestion.cwnd();
      m_linkTx.txDiag.congestionSSThresh = m_linkTx.congestion.ssthresh();
    }
    m_linkTx.txDiag.bytesInFlight = txBytesInFlight();
  }
  uint64_t txBytesInFlight() const {
    uint64_t bytes = 0;
    for (unsigned i = 0; i < PktNumSpace::N; ++i)
      if (!m_linkTx.txSpaceDiscarded[i]) bytes += m_linkTx.txPkts[i].bytesInFlight();
    return bytes;
  }
  const PktTxSpace &txPackets_(PktNumSpace::T space) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC recovery state outside Tx thread", return m_linkTx.txPkts[space]);
    return m_linkTx.txPkts[space];
  }
  uint64_t congestionWindow() const {
    if (ZuUnlikely(app()->congestionControl())) return m_linkTx.congestion.cwnd();
    return 0;
  }
  uint64_t congestionSSThresh() const {
    if (ZuUnlikely(app()->congestionControl())) return m_linkTx.congestion.ssthresh();
    return 0;
  }
  void noteRxECN(PktNumSpace::T level, EcnMark::T ecn) {
    AckECN &diag = m_linkRx.rxDiag.ecnRx[level];
    switch (ecn) {
      case EcnMark::ECT0: ++diag.ect0; break;
      case EcnMark::ECT1: ++diag.ect1; break;
      case EcnMark::CE: ++diag.ce; break;
    }
  }
  unsigned congestionAllowance_() const {
    if (ZuUnlikely(app()->congestionControl())) {
      uint64_t cwnd = m_linkTx.congestion.cwnd();
      uint64_t inFlight = m_linkTx.congestion.bytesInFlight();
      if (inFlight >= cwnd) return 0;
      uint64_t allowance = cwnd - inFlight;
      return allowance > uint64_t(unsigned(-1)) ?
	unsigned(-1) : unsigned(allowance);
    }
    return unsigned(-1);
  }
  bool ecnDisabled_() const { return m_linkTx.path.ecnDisabled(); }
  void setEcnDisabled_(bool b = true) {
    bool disabled = true;
    if (ZuUnlikely(app()->congestionControl())) disabled = b;
#ifdef Zquic_DEBUG
    auto oldState = m_linkTx.path.ecnState();
#endif
    m_linkTx.path.setEcnDisabled(disabled);
#ifdef Zquic_DEBUG
    auto newState = m_linkTx.path.ecnState();
    if (oldState != newState) {
      ZquicLOG(app()->qlogTrace(), ([
	level = PktNumSpace::T(PktNumSpace::AppData),
	oldState = qlogECNState(oldState),
	newState = qlogECNState(newState),
	reason = b ? ZquicLog_::ECNReason::MarkFailed :
	  ZquicLog_::ECNReason::Probe,
	disabled = m_linkTx.path.ecnDisabled(),
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
  PktBudget sendBudget_() {
    drainPeerAcksTx(false);
    refreshCreditTx();
    refreshPathRxTx();
    PktBudget budget;
    unsigned maxUDP = m_linkTx.path.activeMaxUDP();
    budget.pmtu = maxUDP > TxStreamPktReserve ?
      maxUDP - TxStreamPktReserve : 0;
    budget.antiAmplification = m_linkTx.path.sendAllowance();
    budget.congestion = maxUDP;
    if (ZuUnlikely(app()->congestionControl())) {
      unsigned allowance = congestionAllowance_();
      if (allowance < maxUDP && awaitPeerAckTx())
	allowance = congestionAllowance_();
      if (allowance < maxUDP) budget.congestion = allowance;
    }
    return budget;
  }
  static ZquicLog_::ECNState::T qlogECNState(PathECNState::T state) {
    switch (state) {
      case PathECNState::Disabled: return ZquicLog_::ECNState::Disabled;
      case PathECNState::Testing: return ZquicLog_::ECNState::Testing;
      case PathECNState::Capable: return ZquicLog_::ECNState::Capable;
      case PathECNState::Failed: return ZquicLog_::ECNState::Failed;
      default: return ZquicLog_::ECNState::Unknown;
    }
  }
  void initPathECN(Path &path) {
    path.ecn(false);
    if (ZuUnlikely(app()->congestionControl())) path.ecn(app()->ecn());
    ZquicLOG(app()->qlogTrace(), ([
      level = PktNumSpace::T(PktNumSpace::AppData),
      state = qlogECNState(path.ecnState()),
      reason = path.ecnState() == PathECNState::Testing ?
	ZquicLog_::ECNReason::Probe : ZquicLog_::ECNReason::AckECN,
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
    m_linkTx.path = m_config.isServer ?
      Path::server(ZiSockAddr{}, ZiSockAddr{}) :
      Path::client(ZiSockAddr{}, ZiSockAddr{});
    initPathECN(m_linkTx.path);
    m_linkTx.path.configuredMaxUDP(app()->maxUDP());
    m_linkTx.path.peerMaxUDP(app()->maxUDP());
    if (!m_config.isServer) m_linkTx.path.validated();
    m_linkTx.migration = {};
    m_linkTx.pathChallengeControl = {};
  }
  void initClientPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->initClientPathTx(ZuMv(local), ZuMv(remote));
    });
  }
  void initServerPath_(ZiSockAddr local, ZiSockAddr remote) {
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->initServerPathTx(ZuMv(local), ZuMv(remote));
    });
  }
  void initClientPathTx(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC client path initialization outside Tx thread", return);
    m_linkTx.path = Path::client(ZuMv(local), ZuMv(remote));
    publishActivePathRx();
    initPathECN(m_linkTx.path);
    m_linkTx.path.configuredMaxUDP(app()->maxUDP());
    m_linkTx.path.peerMaxUDP(app()->maxUDP());
    m_linkTx.path.validated();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Client,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
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
  void initServerPathTx(ZiSockAddr local, ZiSockAddr remote) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC server path initialization outside Tx thread", return);
    m_linkTx.path = Path::server(ZuMv(local), ZuMv(remote));
    publishActivePathRx();
    initPathECN(m_linkTx.path);
    m_linkTx.path.configuredMaxUDP(app()->maxUDP());
    m_linkTx.path.peerMaxUDP(app()->maxUDP());
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Created,
      reason = PathReason::Server,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
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
  void updatePeerPathMaxUDP() {
    if (!m_linkRx.crypto.peerParamsSet()) return;
    uint64_t maxUDP = m_linkRx.crypto.peerParams().maxUDPPayloadSize;
    if (maxUDP > BufSize) maxUDP = BufSize;
    app()->txRun([link = impl(), maxUDP = unsigned(maxUDP)]() mutable {
      if (link->disconnecting_()) return;
      link->m_linkTx.path.peerMaxUDP(maxUDP);
    });
  }
  void validatePath_() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->validatePathTx();
    });
  }
  void validatePathTx() {
    m_linkTx.path.validated();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Validated,
      reason = PathReason::Initial,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
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
  void publishActivePathRx() {
    m_linkTx.txPathRxReady = false;
    uint64_t epoch = ++m_linkTx.txPathEpoch;
    app()->rxRun([
      link = impl(), epoch, runtime = m_linkTx.txRuntimeGen,
      local = m_linkTx.path.local(), remote = m_linkTx.path.remote()
    ]() mutable {
      if (link->disconnecting_() || runtime != link->m_linkRx.rxRuntimeGen) return;
      link->m_linkRx.rxPathRemote = remote;
      link->m_rxPathPub.base = link->m_linkRx.rxPathBytes;
      link->m_rxPathPub.epoch = epoch;
      link->pathPublishedRx(ZuMv(local), ZuMv(remote));
    });
  }
  void refreshPathRxTx() {
    if (uint64_t(m_rxPathPub.epoch) != m_linkTx.txPathEpoch) return;
    if (!m_linkTx.txPathRxReady) {
      m_linkTx.txPathRxBytes = m_rxPathPub.base;
      m_linkTx.txPathRxReady = true;
    }
    uint64_t total = m_rxPathPub.bytes;
    uint64_t bytes = total - m_linkTx.txPathRxBytes;
    m_linkTx.txPathRxBytes = total;
    while (bytes) {
      unsigned n = bytes > UINT_MAX ? UINT_MAX : unsigned(bytes);
      m_linkTx.path.received(n);
      bytes -= n;
    }
  }
  void recordPathRx(ZiSockAddr remote, unsigned bytes) {
    if (establishedRx() && remote && remote == m_linkRx.rxPathRemote) {
      m_linkRx.rxPathBytes += bytes;
      m_rxPathPub.bytes = m_linkRx.rxPathBytes;
      return;
    }
    app()->txRun([
      link = impl(),
      remote = ZuMv(remote),
      bytes
    ]() mutable {
      if (link->disconnecting_()) return;
      link->recordPathRxTx(ZuMv(remote), bytes);
    });
  }
  void recordPathRxTx(ZiSockAddr remote, unsigned bytes) {
    if (remote && m_linkTx.migration.active && remote == m_linkTx.migration.path.remote()) {
      m_linkTx.migration.path.received(bytes);
      return;
    }
    if (remote && remote != m_linkTx.path.remote()) return;
    m_linkTx.path.received(bytes);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Received,
      reason = PathReason::Datagram,
      bytes,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
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
  void observePathRx_(ZiSockAddr local, ZiSockAddr remote, unsigned bytes) {
    if (remote && remote == m_linkRx.rxPathRemote) return;
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote),
      bytes
    ]() mutable {
      if (link->disconnecting_()) return;
      link->observePathRxTx(ZuMv(local), ZuMv(remote), bytes);
    });
  }
  void observePathRxTx(
    ZiSockAddr local, ZiSockAddr remote, unsigned bytes = 0,
    bool armTimer = true, bool sendChallenge = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path receive observation outside Tx thread", return);
    ++m_linkTx.txDiag.pathRxObserved;
    if (!remote) {
      ++m_linkTx.txDiag.pathRxNull;
      return;
    }
    if (m_linkTx.migration.active &&
	remote == m_linkTx.migration.path.remote()) {
      if (bytes) m_linkTx.migration.path.received(bytes);
      ++m_linkTx.txDiag.pathValidationActive;
      return;
    }
    if (remote == m_linkTx.path.remote()) {
      ++m_linkTx.txDiag.pathRxSame;
      return;
    }
    if (app()->migrationMode() == MigrationMode::Disabled) {
      ++m_linkTx.txDiag.pathValidationDisabled;
      ++m_linkTx.txDiag.migration.policyReject;
      return;
    }
    ++m_linkTx.txDiag.migration.peerObserved;
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Observed,
      reason = PathReason::PeerAddrChange,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
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
    startPathValid(
      ZuMv(local), ZuMv(remote), armTimer, bytes, sendChallenge);
  }
  bool startPathValid(
    ZiSockAddr local, ZiSockAddr remote, bool armTimer = true,
    unsigned rxBytes = 0, bool sendChallenge = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path validation start outside Tx thread", return false);
    if (!remote || remote == m_linkTx.path.remote()) return false;
    PathState state;
    state.prev = m_linkTx.path;
    state.path = m_config.isServer ?
      Path::server(ZuMv(local), remote) :
      Path::client(ZuMv(local), remote);
    initPathECN(state.path);
    state.path.configuredMaxUDP(m_linkTx.path.configuredMaxUDP());
    state.path.peerMaxUDP(m_linkTx.path.peerMaxUDP());
    if (rxBytes) state.path.received(rxBytes);
    ++m_linkTx.txDiag.migration.requested;
    if (!state.challenge.generate())
      return false;
    state.deadline = pathValidDeadline();
    state.state = MigrationState::Requested;
    state.reason = MigrationReason::Passive;
    state.attemptID = m_linkTx.nextMigrationAttemptID++;
    state.active = true;
    state.armTimer = armTimer;
    state.sendChallenge = sendChallenge;
    m_linkTx.migration = state;
    selectPeerCID_0(state.attemptID, false);
    return true;
  }
  void selectPeerCID_2(
      uint64_t attemptID, CxnID id, uint64_t sequence, bool selected) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer CID selection completion outside Tx thread", return);
    if (!m_linkTx.migration.active || m_linkTx.migration.attemptID != attemptID ||
	m_linkTx.migration.state != MigrationState::Requested) {
      if (selected) releasePeerCID(id, sequence);
      return;
    }
    if (!selected) {
      ++m_linkTx.txDiag.migration.noPeerCID;
      m_linkTx.migration.reason = MigrationReason::NoPeerCID;
      logMigration(
	ZquicLog_::MigrationAction::CIDUnavailable,
	MigrationReason::NoPeerCID);
      failMig(MigrationReason::NoPeerCID);
      return;
    }
    m_linkTx.migration.peerCID = id;
    m_linkTx.migration.peerSeq = sequence;
    m_linkTx.txCandidatePeerCID = id;
    m_linkTx.txCandidatePeerSeq = sequence;
    logMigration(ZquicLog_::MigrationAction::CIDSelected);
    if (m_linkTx.migration.reason != MigrationReason::Passive) {
      impl()->migrationCIDReady();
      return;
    }
    selectPeerCID_3();
  }
  void selectPeerCID_3() {
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Requested ||
	m_linkTx.migration.reason != MigrationReason::Passive)
      return;
    m_linkTx.migration.state = MigrationState::Validating;
    if (m_linkTx.migration.peerCID == m_linkTx.txPeerCID)
      ++m_linkTx.txDiag.migration.natRebind;
    ++m_linkTx.txDiag.pathValidating;
    ++m_linkTx.txDiag.migration.started;
    logMigration(ZquicLog_::MigrationAction::Started);
    impl()->migrationStarted(migrationResult());
    if (m_linkTx.migration.armTimer)
      schedulePathTimer(m_linkTx.migration.deadline);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::ChallengeTx,
      reason = PathReason::PeerAddrChange,
      attemptID = m_linkTx.migration.attemptID,
      deadlineUS = qlogUS(m_linkTx.migration.deadline),
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.attemptID = attemptID,
	.antiAmplification = antiAmplification,
	.deadlineUS = deadlineUS,
	.mtu = mtu,
	.kind = PathKind::PathValid,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathValid(event, time);
    }));
    if (m_linkTx.migration.sendChallenge && !impl()->sendPassiveMigChal()) {
      txQueueControl(ControlFrame::pathChallenge(
	m_linkTx.migration.challenge.bspan()));
      logMigration(ZquicLog_::MigrationAction::ChallengeQueued);
      impl()->queueTxFlush(m_linkTx.migration.path.remote());
    }
  }
  bool onPathResponse(ZuBSpan data) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PATH_RESPONSE processing outside Tx thread", return false);
    if (!m_linkTx.migration.active ||
	!m_linkTx.migration.challenge.equals(data)) {
      ++m_linkTx.txDiag.pathResponseUnknown;
      if (m_linkTx.migration.active)
	logMigration(
	  ZquicLog_::MigrationAction::ResponseMismatch, MigrationReason::Validation);
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::ResponseUnk,
		reason = PathReason::Mismatch,
		attemptID = m_linkTx.migration.active ?
		  m_linkTx.migration.attemptID : U64Null,
		antiAmplification = m_linkTx.path.antiAmpRemaining(),
		mtu = m_linkTx.path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PathEvt event{
	  .linkInfo = linkInfo,
	  .attemptID = attemptID,
	  .antiAmplification = antiAmplification,
	  .mtu = mtu,
	  .kind = PathKind::PathValid,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPathValid(event, time);
      }));
      return false;
    }
    m_linkTx.pathChallengeControl = {};
    logMigration(ZquicLog_::MigrationAction::ResponseMatched);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::ResponseRx,
      reason = PathReason::Matched,
      attemptID = m_linkTx.migration.attemptID,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.attemptID = attemptID,
		.antiAmplification = antiAmplification,
		.mtu = mtu,
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.validated = validated};

      o.logPathValid(event, time);
    }));
    promotePath_0();
    return true;
  }
  void rxPathResponse(ZuBSpan data) {
    if (data.length() != PathChallenge::Length) return;
    PathData payload{data};
    app()->txRun([link = impl(), payload]() mutable {
      if (link->disconnecting_()) return;
      // RFC 9000 section 8.2.3: a response received on any path validates
      // the path on which the matching challenge was sent.
      link->onPathResponse(payload);
    });
  }
  void promotePath_0() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path promotion outside Tx thread", return);
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Validating)
      return;
    m_linkTx.migration.state = MigrationState::Promoted;
    cancelPathTimer();
    promotePath_1();
  }
  void promotePath_3(
      uint64_t attemptID, CxnID id, uint64_t sequence, bool committed) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC promoted peer CID acknowledgement outside Tx thread", return);
    if (!m_linkTx.migration.active || m_linkTx.migration.attemptID != attemptID ||
	m_linkTx.migration.state != MigrationState::Promoted)
      return;
    if (!committed || id != m_linkTx.txCandidatePeerCID ||
	sequence != m_linkTx.txCandidatePeerSeq) {
      failMig(MigrationReason::NoPeerCID);
      return;
    }
    m_linkTx.txPeerCID = id;
    m_linkTx.txPeerSeq = sequence;
    m_linkTx.txCandidatePeerCID = {};
    m_linkTx.txCandidatePeerSeq = U64Null;
    promotePath_4();
  }
  void promotePath_4() {
    refreshPathRxTx();
    m_linkTx.path = m_linkTx.migration.path;
    m_linkTx.path.validated();
    publishActivePathRx();
    ++m_linkTx.txDiag.pathValidationPromoted;
    ++m_linkTx.txDiag.migration.promoted;
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Validated,
      reason = PathReason::Response,
      attemptID = m_linkTx.migration.attemptID,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.attemptID = attemptID,
		.antiAmplification = antiAmplification,
		.mtu = mtu,
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.validated = validated};

      o.logPathValid(event, time);
    }));
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Updated,
      reason = PathReason::Promoted,
      attemptID = m_linkTx.migration.attemptID,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = true,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.attemptID = attemptID,
		.antiAmplification = antiAmplification,
		.mtu = mtu,
        .kind = PathKind::Path,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.validated = validated};

      o.logPathUpdated(event, time);
    }));
    impl()->pathPromoted();
    MigrationResult result = migrationResult(true, m_linkTx.migration.reason);
    logMigration(ZquicLog_::MigrationAction::Promoted);
    m_linkTx.migration = {};
    impl()->pathUpdate(activePathInfo());
    impl()->migrationPromoted(result);
    impl()->queueTxFlush();
  }
  void failPathValid() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation failure outside Tx thread", return);
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Validating) return;
    m_linkTx.migration.state = MigrationState::Failed;
    m_linkTx.migration.reason = MigrationReason::Timeout;
    MigrationResult result = migrationResult(false, m_linkTx.migration.reason);
    ++m_linkTx.txDiag.migration.abandoned;
    ++m_linkTx.txDiag.migration.failed;
    ++m_linkTx.txDiag.migration.timeouts;
    logMigration(ZquicLog_::MigrationAction::Abandoned);
    logMigration(ZquicLog_::MigrationAction::Failed);
    m_linkTx.pathChallengeControl = {};
    cancelPathTimer();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Failed,
      reason = PathReason::Timeout,
      attemptID = m_linkTx.migration.attemptID,
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.attemptID = attemptID,
		.antiAmplification = antiAmplification,
		.mtu = mtu,
        .kind = PathKind::PathValid,
        .action = PathAction::T(action),
        .reason = PathReason::T(reason),
		.validated = validated};

      o.logPathValid(event, time);
    }));
    releaseCandidatePeerCID();
    impl()->discardMigEndpoint();
    m_linkTx.migration = {};
    impl()->migrationFailed(result);
    impl()->queueTxFlush();
  }
  void pathExpired() { failPathValid(); }
  bool prepareMig(
    const MigrationParams &params,
    MigrationReason::T &reason, bool preferred = false) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC active migration preparation outside Tx thread", return false);
    reason = MigrationReason::None;
    ++m_linkTx.txDiag.migration.requested;
    if (!establishedTx()) {
      reason = MigrationReason::Validation;
      return false;
    }
    if (m_linkTx.migration.active) {
      reason = MigrationReason::Validation;
      return false;
    }
    if (!preferred && m_linkTx.peerParamsSet &&
	m_linkTx.peerParams.disableActiveMigration) {
      reason = MigrationReason::PeerDisabled;
      ++m_linkTx.txDiag.migration.policyReject;
      logMigRejected(params, reason);
      return false;
    }
    ZiSockAddr local = params.local ? params.local : m_linkTx.path.local();
    ZiSockAddr remote = params.remote ? params.remote : m_linkTx.path.remote();
    if (!local || !remote) {
      reason = MigrationReason::Validation;
      return false;
    }
    if (!params.rebindLocal && remote == m_linkTx.path.remote()) {
      reason = MigrationReason::Validation;
      return false;
    }

    PathState state;
    state.prev = m_linkTx.path;
    state.path = m_config.isServer ?
      Path::server(ZuMv(local), remote) :
      Path::client(ZuMv(local), remote);
    initPathECN(state.path);
    state.path.configuredMaxUDP(m_linkTx.path.configuredMaxUDP());
    state.path.peerMaxUDP(m_linkTx.path.peerMaxUDP());
    state.reason = params.reason != MigrationReason::None ?
      params.reason : MigrationReason::Active;
    state.localRebind = params.rebindLocal;
    state.closeOnFailure = params.closeOnFailure;
    state.state = MigrationState::Requested;
    state.attemptID = m_linkTx.nextMigrationAttemptID++;
    state.active = true;
    logMigration(state, ZquicLog_::MigrationAction::Requested);
    logMigration(state, ZquicLog_::MigrationAction::Started);
    ++m_linkTx.txDiag.migration.started;
    if (!state.challenge.generate()) {
      reason = MigrationReason::Validation;
      logMigration(state, ZquicLog_::MigrationAction::Rejected, reason);
      return false;
    }
    state.deadline = pathValidDeadline();
    m_linkTx.migration = state;
    selectPeerCID_0(state.attemptID, params.requireNewPeerCID,
      preferred ? 1 : U64Null);
    if (!m_linkTx.migration.active) {
      reason = MigrationReason::NoPeerCID;
      return false;
    }
    return m_linkTx.migration.attemptID == state.attemptID;
  }
  ZiSockAddr migLocal() const { return m_linkTx.migration.path.local(); }
  bool migLocalRebind() const { return m_linkTx.migration.localRebind; }
  void updateMigLocal(ZiSockAddr local) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC active migration local update outside Tx thread", return);
    if (!m_linkTx.migration.active || !local) return;
    Path path = m_config.isServer ?
      Path::server(ZuMv(local), m_linkTx.migration.path.remote()) :
      Path::client(ZuMv(local), m_linkTx.migration.path.remote());
    initPathECN(path);
    path.configuredMaxUDP(m_linkTx.path.configuredMaxUDP());
    path.peerMaxUDP(m_linkTx.path.peerMaxUDP());
    m_linkTx.migration.path = ZuMv(path);
  }
  void migRebindStart() {
    logMigration(ZquicLog_::MigrationAction::RebindStart);
  }
  void migRebindOK() {
    ++m_linkTx.txDiag.migration.localRebindOK;
    logMigration(ZquicLog_::MigrationAction::RebindOK);
  }
  void migRebindFail(MigrationReason::T reason) {
    ++m_linkTx.txDiag.migration.localRebindFail;
    logMigration(ZquicLog_::MigrationAction::RebindFail, reason);
  }
  bool activateMig(bool armTimer = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC active migration activation outside Tx thread", return false);
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Requested)
      return false;
    m_linkTx.migration.state = MigrationState::Validating;
    ++m_linkTx.txDiag.pathValidating;
    impl()->migrationStarted(migrationResult());
    if (armTimer)
      schedulePathTimer(m_linkTx.migration.deadline);
    logMigration(ZquicLog_::MigrationAction::ChallengeQueued);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::ChallengeTx,
      reason = PathReason::Client,
      attemptID = m_linkTx.migration.attemptID,
      deadlineUS = qlogUS(m_linkTx.migration.deadline),
      antiAmplification = m_linkTx.path.antiAmpRemaining(),
      mtu = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
	.linkInfo = linkInfo,
	.attemptID = attemptID,
	.antiAmplification = antiAmplification,
	.deadlineUS = deadlineUS,
	.mtu = mtu,
	.kind = PathKind::PathValid,
	.action = PathAction::T(action),
	.reason = PathReason::T(reason),
	.validated = validated};

      o.logPathValid(event, time);
    }));
    return true;
  }
  bool buildMigChal(
    PktBuild &build, TxPktRefs &refs, ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC active migration challenge build outside Tx thread", return false);
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Validating)
      return false;
    frame = ControlFrame::pathChallenge(m_linkTx.migration.challenge.bspan());
    if (!txQueueControl(frame)) return false;
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    PktAssembly assembly;
    SentControls sentControls;
    return appendControl(
      frame, build, budget, assembly, refs,
      sentControls);
  }
  void migChalSent(const ControlFrame &frame) {
    controlSent_(frame);
  }
  const ZiSockAddr &migRemote() const {
    return m_linkTx.migration.path.remote();
  }
  const CxnID &migPeerCID() const {
    return m_linkTx.migration.peerCID;
  }
  const CxnID &validatingPeerCID() const {
    return m_linkTx.migration.peerCID;
  }
  bool buildPassiveMigChal(
    PktBuild &build, TxPktRefs &refs, ControlFrame &frame) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC passive migration challenge build outside Tx thread",
      return false);
    if (!m_linkTx.migration.active ||
	m_linkTx.migration.state != MigrationState::Validating)
      return false;
    frame = ControlFrame::pathChallenge(m_linkTx.migration.challenge.bspan());
    build.reset();
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    if (!refs.add(controlRef(frame))) return false;
    // Keep the challenge recoverable until its matching response arrives.
    (void)txQueueControl(frame);
    build.markPathValidation();
    return true;
  }
  void passiveMigChalSent(const ControlFrame &frame) {
    controlSent_(frame);
  }
  bool buildPathResponse(
    ZuBSpan data, PktBuild &build, TxPktRefs &refs,
    ZiSockAddr remote = {}, bool includeChallenge = true,
    bool includeAck = true) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PATH_RESPONSE build outside Tx thread", return false);
    if (data.length() != PathChallenge::Length) return false;
    build.reset();
    if (includeAck && !appendPendingAck(PktNumSpace::AppData, build))
      return false;
    if (includeChallenge && remote && m_linkTx.migration.active &&
	m_linkTx.migration.state == MigrationState::Validating &&
	m_linkTx.migration.challenge.valid() &&
	remote == m_linkTx.migration.path.remote()) {
      ControlFrame frame =
	ControlFrame::pathChallenge(m_linkTx.migration.challenge.bspan());
      int n = frame.write(build.scratch(), build.scratchAvail());
      if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
      if (!refs.add(controlRef(frame))) return false;
    }
    ControlFrame frame = ControlFrame::pathResponse(data);
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    build.markPathValidation();
    return refs.add(controlRef(frame));
  }
  template <typename Send>
  bool sendPathResponsePkt(ZuBSpan data, ZiSockAddr addr, Send send) {
    uint64_t runtime = m_linkTx.txRuntimeGen;
    ZiSockAddr retryAddr = addr;
    ZiSockAddr buildAddr = addr;
    ZiSockAddr validationAddr = addr;
    PktBuild build;
    TxPktRefs refs;
    if (!buildPathResponse(data, build, refs, ZuMv(buildAddr))) {
      refs = {};
      if (!buildPathResponse(data, build, refs)) return false;
    }
    bool sent = send(build, ZuMv(addr), &refs);
    if (runtime != m_linkTx.txRuntimeGen || sent) return sent;

    // PATH_RESPONSE is not retransmitted.  If recovery metadata or frame
    // admission is saturated by streams, send the direct response untracked.
    PktBuild retry;
    TxPktRefs retryRefs;
    if (!buildPathResponse(data, retry, retryRefs, {}, false, false))
      return false;
    sent = send(retry, ZuMv(retryAddr), nullptr);
    if (runtime != m_linkTx.txRuntimeGen) return sent;
    if (sent && validationAddr && m_linkTx.migration.active &&
	m_linkTx.migration.state == MigrationState::Validating &&
	validationAddr == m_linkTx.migration.path.remote())
      (void)impl()->sendPassiveMigChal();
    return sent;
  }
  void failMig(MigrationReason::T reason) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC active migration failure outside Tx thread", return);
    if (!m_linkTx.migration.active) return;
    m_linkTx.migration.state = MigrationState::Failed;
    m_linkTx.migration.reason = reason;
    MigrationResult result = migrationResult(false, reason);
    bool closeOnFailure = m_linkTx.migration.closeOnFailure;
    ++m_linkTx.txDiag.migration.failed;
    if (reason == MigrationReason::Endpoint)
      ++m_linkTx.txDiag.migration.endpointFailure;
    else if (reason == MigrationReason::Timeout)
      ++m_linkTx.txDiag.migration.timeouts;
    logMigration(ZquicLog_::MigrationAction::Failed, reason);
    if (closeOnFailure)
      logMigration(ZquicLog_::MigrationAction::Closed, reason);
    releaseCandidatePeerCID();
    impl()->discardMigEndpoint();
    m_linkTx.migration = {};
    m_linkTx.pathChallengeControl = {};
    cancelPathTimer();
    impl()->migrationFailed(result);
    if (closeOnFailure) {
      closeStateTx(TransportError::NoError);
      if (ZiSockAddr addr = m_linkTx.path.remote())
	(void)impl()->sendCloseFrame(ZuMv(addr), true);
      enterLocalClosingTx();
    }
  }
  bool migrationActive_() const { return m_linkTx.migration.active; }
  uint64_t activePeerSeq() const { return m_linkTx.txPeerSeq; }
  static PathInfo pathInfo_(
    const Path &path, PathRole::T role,
    MigrationState::T state, MigrationReason::T reason, uint64_t peerSeq) {
    const PathDiag &diag = path.diag();
    return PathInfo{
      .local = path.local(),
      .remote = path.remote(),
      .peerCIDSequence = peerSeq,
      .bytesRx = diag.bytesRx,
      .bytesTx = diag.bytesTx,
      .activeMaxUDP = path.activeMaxUDP(),
      .role = role,
      .migrationState = state,
      .reason = reason,
      .validated = path.validated()
    };
  }
  PathInfo activePathInfo() const {
    return pathInfo_(
      m_linkTx.path, PathRole::Active, MigrationState::Idle,
      MigrationReason::None, activePeerSeq());
  }
  const ZiSockAddr &activeLocalTx() const { return m_linkTx.path.local(); }
  const ZiSockAddr &activeRemoteTx() const { return m_linkTx.path.remote(); }
  PathInfo candidatePathInfo_() const {
    if (!m_linkTx.migration.active) return PathInfo{};
    return pathInfo_(
      m_linkTx.migration.path, PathRole::Candidate, m_linkTx.migration.state,
      m_linkTx.migration.reason, m_linkTx.migration.peerSeq);
  }
  MigrationResult migrationResult(
    bool success = false,
    MigrationReason::T reason = MigrationReason::None) const {
    MigrationState::T state = m_linkTx.migration.active ?
      m_linkTx.migration.state : MigrationState::T(MigrationState::Idle);
    MigrationReason::T reason_ = reason != MigrationReason::None ? reason :
      (m_linkTx.migration.active ?
	m_linkTx.migration.reason : MigrationReason::T(MigrationReason::None));
    return MigrationResult{
      .active = activePathInfo(),
      .candidate = candidatePathInfo_(),
      .state = state,
      .reason = reason_,
      .attemptID = m_linkTx.migration.active ? m_linkTx.migration.attemptID : U64Null,
      .success = success
    };
  }
  void logMigration(
    ZquicLog_::MigrationAction::T action,
    MigrationReason::T reason = MigrationReason::None) const {
    if (!m_linkTx.migration.active) return;
    logMigration(m_linkTx.migration, action, reason);
  }
  void logMigration(
    const PathState &migration,
    ZquicLog_::MigrationAction::T action,
    MigrationReason::T reason = MigrationReason::None) const {
    if (!migration.active) return;
    ZquicLOG(app()->qlogTrace(), ([
      action,
      reason = reason != MigrationReason::None ? reason : migration.reason,
      state = migration.state,
      activeLocal = m_linkTx.path.local(),
      activeRemote = m_linkTx.path.remote(),
      candidateLocal = migration.path.local(),
      candidateRemote = migration.path.remote(),
      attemptID = migration.attemptID,
      peerCIDSeq = migration.peerSeq,
      deadlineUS = qlogUS(migration.deadline),
      mtu = migration.path.activeMaxUDP(),
      pathRole = action == ZquicLog_::MigrationAction::Promoted ?
	PathRole::Active : PathRole::Candidate,
      localRebind = migration.localRebind,
      validated = action == ZquicLog_::MigrationAction::Promoted ||
	migration.path.validated(),
      closeOnFailure = migration.closeOnFailure,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      MigrationEvt event{
	.linkInfo = linkInfo,
	.activeLocal = activeLocal,
	.activeRemote = activeRemote,
	.candidateLocal = candidateLocal,
	.candidateRemote = candidateRemote,
	.attemptID = attemptID,
	.peerCIDSeq = peerCIDSeq,
	.deadlineUS = deadlineUS,
	.mtu = mtu,
	.action = ZquicLog_::MigrationAction::T(action),
	.reason = MigrationReason::T(reason),
	.state = MigrationState::T(state),
	.pathRole = PathRole::T(pathRole),
	.localRebind = localRebind,
	.validated = validated,
	.closeOnFailure = closeOnFailure};

      o.logMigrationUpdated(event, time);
    }));
  }
  void logMigRejected(
    const MigrationParams &params, MigrationReason::T reason) {
    ZiSockAddr local = params.local ? params.local : m_linkTx.path.local();
    ZiSockAddr remote = params.remote ? params.remote : m_linkTx.path.remote();
    if (!local || !remote) return;
    PathState state;
    state.prev = m_linkTx.path;
    state.path = m_config.isServer ?
      Path::server(ZuMv(local), remote) :
      Path::client(ZuMv(local), remote);
    state.path.configuredMaxUDP(m_linkTx.path.configuredMaxUDP());
    state.path.peerMaxUDP(m_linkTx.path.peerMaxUDP());
    state.state = MigrationState::Idle;
    state.reason = reason;
    state.localRebind = params.rebindLocal;
    state.closeOnFailure = params.closeOnFailure;
    state.attemptID = m_linkTx.nextMigrationAttemptID++;
    state.active = true;
    logMigration(state, ZquicLog_::MigrationAction::Requested, reason);
    logMigration(state, ZquicLog_::MigrationAction::Rejected, reason);
  }
  bool validatingPath_() const { return migrationActive_(); }
  bool startPathValidation(ZiSockAddr local, ZiSockAddr remote) {
    if (!remote || remote == m_linkTx.path.remote())
      return false;
    if (m_linkTx.migration.active &&
	remote == m_linkTx.migration.path.remote())
      return false;
    return startPathValid(ZuMv(local), ZuMv(remote), false);
  }
  const ZiSockAddr &validatingRemote() const {
    return m_linkTx.migration.path.remote();
  }
  ZuBSpan validatingChallenge_() const {
    return m_linkTx.migration.active ?
      m_linkTx.migration.challenge.bspan() : ZuBSpan{};
  }
  bool installAppDataKeys_(
    const TrafficSecret &rx, const TrafficSecret &tx, const CxnID &localCID) {
    m_linkRx.localSCID = localCID;
    linkStateRx(LinkState::Established);
    publishLinkSnapshotTx();
    clearPeerKeyState();
    return m_linkRx.crypto.rxSecret(PktNumSpace::AppData, rx) &&
      txInstallSecret(PktNumSpace::AppData, tx);
  }
#ifdef Zquic_DEBUG
  void forceActivePathMTU(unsigned size) {
    m_linkTx.path.startProbe(size);
    m_linkTx.path.probeAckd();
  }
  bool startPMTUDProbe_(unsigned size) {
    return m_linkTx.path.startProbeChecked(size);
  }
  void ackPMTUDProbe_(unsigned size) {
    onPMTUDProbeAckd(size);
  }
  void losePMTUDProbe_(unsigned size) {
    onPMTUDProbeLost(size);
  }
  void expirePMTUDProbe_() { pmtudExpired(); }
  unsigned pathProbeSize_() const { return m_linkTx.path.probeSize(); }
  bool pathProbeRetryPending_() const { return m_linkTx.path.probeRetryPending(); }
  void discardPeerKeys_() { discardOldPeerKeys(); }
#endif
  // Reserve anti-amplification accounting before output can become visible.
  // Refusal rolls back only the original runtime/path; reset owns its cleanup.
  struct PathSend {
    Self *link;
    Path *path;
    unsigned bytes;
    uint64_t runtime;
    uint64_t epoch;
    uint64_t attempt;
    bool accepted = false;

    PathSend(Self *link_, Path &path_, unsigned bytes_) :
      link{link_}, path{&path_}, bytes{bytes_},
      runtime{link->m_linkTx.txRuntimeGen}, epoch{link->m_linkTx.txPathEpoch},
      attempt{link->m_linkTx.migration.attemptID} { path->sent(bytes); }
    ~PathSend() {
      if (accepted || runtime != link->m_linkTx.txRuntimeGen ||
	  epoch != link->m_linkTx.txPathEpoch) return;
      if (path == &link->m_linkTx.path ||
	  (link->m_linkTx.migration.active &&
	    attempt == link->m_linkTx.migration.attemptID))
	path->unreserveSend(bytes);
    }
  };

  template <typename SendPkt>
  bool sendPathPkt(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    refreshPathRxTx();
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_linkTx.path.canSend(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_linkTx.path.antiAmpRemaining(),
		mtu = m_linkTx.path.activeMaxUDP(),
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
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AntiAmp},
	  time);
      }));
      return false;
    }
    PathSend reservation{this, m_linkTx.path, bytes};
    reservation.accepted = sendPkt(ZuMv(buf), ZuMv(addr), ecn);
    if (!reservation.accepted) return false;
    if (reservation.runtime != m_linkTx.txRuntimeGen) return true;
    ZquicLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  template <typename SendPkt>
  bool sendPathProbePkt(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_linkTx.path.canSendProbe(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_linkTx.path.antiAmpRemaining(),
		mtu = m_linkTx.path.activeMaxUDP(),
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
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::ProbeAdmit},
	  time);
      }));
      return false;
    }
    PathSend reservation{this, m_linkTx.path, bytes};
    reservation.accepted = sendPkt(ZuMv(buf), ZuMv(addr), ecn);
    if (!reservation.accepted) return false;
    if (reservation.runtime != m_linkTx.txRuntimeGen) return true;
    ZquicLOG(app()->qlogTrace(), ([
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
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_linkTx.path.canSend(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
		bytes,
		antiAmplification = m_linkTx.path.antiAmpRemaining(),
		mtu = m_linkTx.path.activeMaxUDP(),
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
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AntiAmp},
	  time);
      }));
      return false;
    }
    PathSend reservation{this, m_linkTx.path, bytes};
    bool sent = false;
    bool accepted = sendPkt(ZuMv(buf), ZuMv(addr), ecn, sent);
    reservation.accepted = accepted && sent;
    if (!accepted) return false;
    if (reservation.runtime != m_linkTx.txRuntimeGen) return true;
    if (!sent) {
      ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AppSend},
	  time);
      }));
      return true;
    }
    ZquicLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  Path &txPath(const ZiSockAddr &addr) {
    if (addr && m_linkTx.migration.active && addr == m_linkTx.migration.path.remote())
      return m_linkTx.migration.path;
    return m_linkTx.path;
  }
  template <typename SendPkt>
  bool sendValidPathPktApp(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC validation path packet send outside Tx thread", return false);
    if (!buf) return false;
    Path &path = txPath(addr);
    const Path &path_ = path;
    (void)path_;
    unsigned bytes = buf->length;
    if (!path.canSend(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::AntiAmp,
	bytes,
	antiAmplification = path_.antiAmpRemaining(),
	mtu = path_.activeMaxUDP(),
	validated = path_.validated(),
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
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AntiAmp},
	  time);
      }));
      return false;
    }
    PathSend reservation{this, path, bytes};
    bool sent = false;
    bool accepted = sendPkt(ZuMv(buf), ZuMv(addr), ecn, sent);
    reservation.accepted = accepted && sent;
    if (!accepted) return false;
    if (reservation.runtime != m_linkTx.txRuntimeGen) return sent;
    if (!sent) {
      ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AppSend},
	  time);
      }));
      return false;
    }
    ZquicLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  template <typename SendPkt>
  bool sendPathProbePktApp(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt,
      EcnMark::T ecn = EcnMark::NotECT) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path probe packet send outside Tx thread", return false);
    if (!buf) return false;
    unsigned bytes = buf->length;
    if (!m_linkTx.path.canSendProbe(bytes)) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
	reason = PathReason::ProbeAdmit,
		bytes,
		antiAmplification = m_linkTx.path.antiAmpRemaining(),
		mtu = m_linkTx.path.activeMaxUDP(),
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
	o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::ProbeAdmit},
	  time);
      }));
      return false;
    }
    PathSend reservation{this, m_linkTx.path, bytes};
    bool sent = false;
    bool accepted = sendPkt(ZuMv(buf), ZuMv(addr), ecn, sent);
    reservation.accepted = accepted && sent;
    if (!accepted) return false;
    if (reservation.runtime != m_linkTx.txRuntimeGen) return true;
    if (!sent) {
      ZquicLOG(app()->qlogTrace(), ([bytes, linkInfo = linkInfo_()](auto &o, ZuTime time) {
		o.logPktDrop(
	  PktEvt{
	    .linkInfo = linkInfo,
	    .packetSize = bytes,
	    .reason = Reason::AppSend},
	  time);
      }));
      return true;
    }
    ZquicLOG(app()->qlogTrace(), ([
      bytes, ecn, linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramSent(event, time);
    }));
    return true;
  }
  unsigned txPNLength(PktNumSpace::T level) const {
    uint64_t largestAckd = m_linkTx.txLargestAckd[level];
    if (ZuNull(largestAckd)) return PNLength;
    return PktNumber::encodedLength(m_linkTx.txPN[level], largestAckd);
  }
  uint64_t txRuntime() const { return m_linkTx.txRuntimeGen; }
  void setTxPN(PktNumSpace::T level, uint64_t pn) {
    m_linkTx.txPN[level] = pn;
  }
  template <typename L>
  void resetLink(L l) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC runtime reset outside Rx thread", return);
    cancelAckRx();
    ++m_linkRx.rxRuntimeGen;
    m_creditRxPub.limit = uint64_t{0};
    m_linkRx.creditPosted = 0;
    uint64_t creditSequence = m_linkRx.creditSequence;
    m_linkRx.rxPeerAckSeen = false;
    for (unsigned i = 0; i < StreamClassCount; ++i) {
      if (!m_linkRx.closedStreamPending[i]) continue;
      m_linkRx.closedStreamPending[i] = false;
      advanceClosedStreams(i);
    }
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      m_linkRx.rxAckMinGen[i] = m_linkRx.rxAcks.gen(PktNumSpace::T(i)) + 1;
      m_linkRx.rxAckRetired[i] = 0;
    }
    m_linkRx.rxPathRemote = {};
    disconnecting_(false);
    m_linkRx.statelessReset = false;
    m_linkRx.closeCursor = nullptr;
    m_linkRx.closePending = false;
    resetLinkStateRx();
    m_linkRx.initialDCID = {};
    m_linkRx.origDCID = {};
    m_linkRx.groupID = {};
    m_linkRx.localSCID = {};
    m_linkRx.peerCID = {};
    m_linkRx.peerResetToken = {};
    clearCIDState();
    m_linkRx.transportParams = {};
    m_linkRx.rxDataCredit = {};
    m_linkRx.rxDataRetired = 0;
    m_linkRx.rxEarlyData = {};
    clearZeroRTTParams();
    m_linkRx.crypto.resetTLS();
    for (auto &pn : m_linkRx.rxLargestPN) pn = 0;
    m_linkRx.rxAcks.clear();
    clearPeerKeyState();
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      m_linkRx.rxSpaceDiscarded[i] = true;
      m_linkRx.crypto.discardSecret(PktNumSpace::T(i));
    }
    app()->txInvoke(impl(), [
      link = impl(), creditSequence, runtime = m_linkRx.rxRuntimeGen, l = ZuMv(l)
    ]() mutable {
      if (link->disconnecting_()) return link;
      link->resetTx(creditSequence, [link, runtime, l = ZuMv(l)]() mutable {
	link->app()->rxRun([link, runtime, l = ZuMv(l)]() mutable {
	  link->drainResetRx(runtime, ZuMv(l));
	});
      });
      return link;
    });
  }
  template <typename L>
  void drainResetRx(uint64_t runtime, L l) {
    if (disconnecting_() || runtime != m_linkRx.rxRuntimeGen) return;
    do {
      unsigned budget = StreamCloseBatch;
      bool drained = true;
      for (auto &crypto : m_linkRx.rxCrypto)
	if (!crypto.drainRx(budget)) { drained = false; break; }
      if (drained) break;
      if (app()->mx() && app()->mx()->running()) {
	app()->rxRun([link = impl(), runtime, l = ZuMv(l)]() mutable {
	  link->drainResetRx(runtime, ZuMv(l));
	});
	return;
      }
    } while (true);
    for (auto &discarded : m_linkRx.rxSpaceDiscarded) discarded = false;
    l();
  }
  void resetTx(uint64_t creditSequence = U64Null) {
    resetTx(creditSequence, [] {});
  }
  template <typename L>
  void resetTx(uint64_t creditSequence, L l) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Tx runtime reset outside Tx thread", return);
    ++m_linkTx.txRuntimeGen;
    m_linkTx.resetting = true;
    m_linkTx.txSendDeferred = false;
    cancelTimers_();
    m_linkTx.txDataCredit = {};
    m_linkTx.lastDataBlocked = U64Null;
    for (auto &value : m_linkTx.lastStreamsBlocked) value = U64Null;
    resetPath();
    m_linkTx.rxMaxDataSeen = 0;
    m_linkTx.creditScanning = false;
    m_linkTx.creditCursor = m_linkTx.creditStreams.headPtr();
    // Only retire publications preceding the Rx reset. Rx may already have
    // published new-runtime credit while this owner handoff was queued.
    if (!ZuNull(creditSequence)) m_creditTxPub.done = creditSequence;
    m_peerAckRing.consumer.waiting = 0;
    ++m_linkTx.txPathEpoch;
    m_linkTx.txPathRxReady = false;
    m_linkTx.txPeerActivityUS = m_rxPathPub.activityUS;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      m_linkTx.txAckRetired[i] = 0;
      m_ackCompletion[i].retireFrontier = uint64_t{0};
    }
    for (auto &pending : m_linkTx.openQueuedPending) pending = false;
    m_linkTx.txAppClose = {};
    m_linkTx.txLinkState = LinkState::Starting;
    m_linkTx.txPeerCID = {};
    m_linkTx.txPeerSeq = U64Null;
    m_linkTx.txCandidatePeerCID = {};
    m_linkTx.txCandidatePeerSeq = U64Null;
    m_linkTx.lossTimerOut = {};
    m_linkTx.ptoTimerOut = {};
    m_linkTx.ptoTimerLevel = PktNumSpace::Initial;
    m_linkTx.idleTimeout = {};
    m_linkTx.idleTimerOut = {};
    m_linkTx.idleBase = {};
    m_linkTx.idleAckElicitingSent = false;
    m_linkTx.heartBeatTimerOut = {};
    m_linkTx.heartBeatBase = {};
    m_linkTx.closeTimerOut = {};
    m_linkTx.closeNextResponse = {};
    clearLinkControls();
    resetPktState();
    for (auto &discarded : m_linkTx.txSpaceDiscarded) discarded = true;
    if (ZuUnlikely(app()->congestionControl()))
      m_linkTx.congestion = NewReno{app()->maxUDP()};
    updateCongestionDiag();
    if (m_pressure.pending || m_capacity.wait.busy()) {
      cancelPktWait([link = impl(), runtime = m_linkTx.txRuntimeGen,
	l = ZuMv(l)]() mutable {
	link->drainResetTx(runtime, ZuMv(l));
      });
    } else {
      cancelPktWait();
      drainResetTx(m_linkTx.txRuntimeGen, ZuMv(l));
    }
  }
  template <typename L>
  void drainResetTx(uint64_t runtime, L l) {
    if (disconnecting_() || runtime != m_linkTx.txRuntimeGen) return;
    do {
      unsigned budget = StreamCloseBatch;
      while (budget && m_linkTx.creditCursor) {
	auto node = m_linkTx.creditCursor;
	m_linkTx.creditCursor = static_cast<CreditStreams::Node::Ext *>(node)->next;
	static_cast<Stream *>(node)->resetControls(runtime);
	--budget;
      }
      bool drained = !m_linkTx.creditCursor && clearPeerAcksTx(budget);
      if (drained) {
	for (unsigned i = 0; i < PktNumSpace::N; ++i)
	  if (!drainPktSpaceTx(PktNumSpace::T(i), budget)) { drained = false; break; }
      }
      if (drained) break;
      if (app()->mx() && app()->mx()->running()) {
	app()->txRun([link = impl(), runtime, l = ZuMv(l)]() mutable {
	  link->drainResetTx(runtime, ZuMv(l));
	});
	return;
      }
    } while (true);
    for (auto &discarded : m_linkTx.txSpaceDiscarded) discarded = false;
    m_linkTx.resetting = false;
    startCreditTx();
    if (disconnecting_() || runtime != m_linkTx.txRuntimeGen) return;
    l();
  }

  void closeLink(uint64_t = 0) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC link close outside Rx thread", return);
    cancelAckRx();
    closeStreamsRx();
    cancelTimers();
    closeLinkStateRx();
    publishLinkSnapshotTx();
    m_linkRx.crypto.resetTLS();
  }

  void resetTLSRx() {
    m_linkRx.crypto.resetTLS();
  }

  void endpointReady() { ++m_linkRx.rxDiag.endpointReady; }
  void endpointFailure() { ++m_linkRx.rxDiag.failures; }
  void packetParseFailure() { ++m_linkRx.rxDiag.failures; }
  void tlsFailure() {
    ++m_linkRx.rxDiag.failures;
    ZquicLOG(app()->qlogTrace(), ([linkInfo = linkInfo_()](auto &o, ZuTime time) {
      SecEvt event{
	.linkInfo = linkInfo,
	.kind = SecKind::TLS,
	.reason = SecReason::Handshake,
	.success = false};
      o.logTLSAlert(event, time);
    }));
  }
  void handshakeDoneAckd() { }
  void newTokenRx() { ++m_linkRx.rxDiag.newTokenRx; }
  void newTokenTx() { ++m_linkTx.txDiag.newTokenTx; }
  void pathPromoted() { }
  void pathPublishedRx(ZiSockAddr, ZiSockAddr) { }
  template <typename L> void commitMigEndpoint(L l) { l(true); }
  void discardMigEndpoint() { }
  void dataBlocked(uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zquic::StreamType::Duplex, maximum);
  }
  void streamDataBlocked(uint64_t streamID, uint64_t maximum) {
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zquic::StreamType::Duplex, maximum);
  }
  void streamsBlocked(Zquic::StreamType::T type, uint64_t maximum) {
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
  }
  void streamOpen(StreamRef, bool) { ++m_linkRx.rxDiag.unhandledAppEvents; }
  void streamData(
    StreamRef, uint64_t, ZuBSpan, bool) {
    ++m_linkRx.rxDiag.unhandledAppEvents;
  }
  void streamResetReceived(
    StreamRef, uint64_t, uint64_t) {
    ++m_linkRx.rxDiag.unhandledAppEvents;
  }
  void streamResetSent(uint64_t, uint64_t, uint64_t) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void streamStopSendingReceived(StreamRef, uint64_t) {
    ++m_linkRx.rxDiag.unhandledAppEvents;
  }
  void streamStopSendingSent(uint64_t, uint64_t) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void flowBlocked(
    FrameType::T, uint64_t, Zquic::StreamType::T, uint64_t) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void transportClose(FrameType::T, uint64_t) {
    ++m_linkRx.rxDiag.unhandledAppEvents;
  }
  void migrationStarted(const MigrationResult &) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void migrationPromoted(const MigrationResult &) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void migrationFailed(const MigrationResult &) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void pathUpdate(const PathInfo &) {
    ++m_linkTx.txDiag.unhandledAppEvents;
  }
  void streamFrame(
    uint64_t, uint64_t, ZuBSpan, bool) { }
  void retiredLocalCID(uint64_t, const CxnID &) { }
  void refreshPromotedRoutes() { }
  void localCIDsIssued() { }
  void migrationCIDsReady() { impl()->queueTxFlush(); }
  void migrationCIDReady() { }
  void flowCredit(
      FrameType::T, uint64_t, Zquic::StreamType::T) { }
  void streamCredit(Zquic::StreamType::T) { }
  void queueTxFlush() { }
  bool sendPassiveMigChal() { return false; }
  bool sendMigChal(bool = false) { return false; }
  void statelessReset() { ++m_linkRx.rxDiag.unhandledAppEvents; }
  void disconnected(bool peer) {
    disconnected(peer, []() { });
  }
  template <typename L>
  void disconnected(bool peer, L &&l) {
    app()->rxRun([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->beginDisconnectRx(peer, Zquic_::LinkDiscFn{ZuMv(l)});
    });
  }
  void beginDisconnectRx(bool peer, Zquic_::LinkDiscFn l) {
    disconnecting_(true);
    auto mx = app()->mx();
    bool live = mx && mx->running() && app()->txThread();
    if (live)
      ZiAssert(rxInvoked_(), "Zquic", (),
	"QUIC link disconnect completion outside Rx thread", return);
    m_linkRx.discFns.push(Zquic_::LinkDiscFn{
      [link = impl(), peer, live, l = ZuMv(l)]() mutable {
	if (live) link->Self::disconnected_(peer);
	l();
      }});
    if (m_linkRx.discWork) return;
    ZmRef<StreamCloseWork> work = new StreamCloseWork;
    m_linkRx.discWork = work;
    if (mx) {
      cancelAckRx();
      mx->del(&m_rxAckTimerStorage.timer);
    }
    if (live) {
      app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
	link->delTimers();
	link->cancelPktWait([link, work = ZuMv(work)]() mutable {
	  link->app()->rxRun([link, work = ZuMv(work)]() mutable {
	    link->Self::disconnected_0(ZuMv(work));
	  });
	});
      });
      return;
    }
    disconnected_0(ZuMv(work));
  }
  void disconnected_0(ZmRef<StreamCloseWork> work) {
    auto mx = app()->mx();
    bool live = mx && mx->running() && app()->txThread();
    if (live) {
      closedLinkStateRx();
      m_linkRx.crypto.resetTLS();
      app()->retireLinkRoutes(impl());
      LinkStateSnapshot snapshot = linkSnapshotRx();
      retireStreamsRx([
	link = impl(), snapshot, work
      ]() mutable {
	link->app()->txRun([link, snapshot, work = ZuMv(work)]() mutable {
	  link->applyLinkSnapshotTx(snapshot);
	  link->retireStreamsTx([link, work = ZuMv(work)]() mutable {
	    link->app()->txRun([link, work = ZuMv(work)]() mutable {
	      link->app()->mx()->txRun([link, work = ZuMv(work)]() mutable {
		link->app()->rxRun([link, work = ZuMv(work)]() mutable {
		  link->completeDisconnectRx(ZuMv(work));
		});
	      });
	    });
	  });
	});
      }, work);
      return;
    }
    // No shard remains after multiplexer finalization.
    auto oldState = telState(m_linkRx.rxLinkState);
    auto newState = telState(LinkState::Closed);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_linkRx.rxLinkState = LinkState::Closed;
    m_linkTx.txLinkState = LinkState::Closed;
    m_linkShared.publicLinkState = unsigned(LinkState::Closed);
    m_linkRx.crypto.resetTLS();
    retireStreamsRx([link = impl(), work]() mutable {
      link->retireStreamsTx([link, work = ZuMv(work)]() mutable {
	link->completeDisconnectRx(ZuMv(work));
      });
    }, work);
  }
  void completeDisconnectRx(ZmRef<StreamCloseWork> work) {
    work->completed = ZuMv(m_linkRx.discFns);
    m_linkRx.discWork = nullptr;
    runDisconnectFns(app(), ZuMv(work));
  }
  static void runDisconnectFns(App *app, ZmRef<StreamCloseWork> work) {
    do {
      unsigned budget = StreamCloseBatch;
      while (budget) {
	auto fn = work->completed.shift();
	if (!fn) break;
	--budget;
	fn();
      }
      if (!work->completed.count_()) return;
      if (app->mx() && app->mx()->running()) {
	app->rxRun([app, work = ZuMv(work)]() mutable {
	  runDisconnectFns(app, ZuMv(work));
	});
	return;
      }
    } while (true);
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
    m_linkRx.origDCID = origDCID;
    m_linkRx.groupID = groupID;
    m_linkRx.initialDCID = initialDCID;
    m_linkRx.localSCID = localSCID;
    m_linkRx.peerCID = peerCID;
    addLocalCID_(localSCID, 0);
    addPeerCID_(peerCID, 0);
    if (auto cid = findCID(m_linkRx.peerCIDs, peerCID))
      cid->associated = true;
    publishActivePeerCID();
  }

  void setPeerCIDFromHdrSCID(const LongHdr &h) {
    if (h.scid) {
      m_linkRx.peerCID = h.scid;
      publishActivePeerCID();
    }
  }

  void publishActivePeerCID() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC active peer CID publication outside Rx thread", return);
    uint64_t sequence = U64Null;
    if (auto cid = findCID(m_linkRx.peerCIDs, m_linkRx.peerCID))
      sequence = cid->sequence;
    CxnID id = m_linkRx.peerCID;
    app()->txRun([link = impl(), id, sequence, info = linkInfo_()]() mutable {
      if (link->disconnecting_()) return;
      link->m_linkTx.info = info;
      link->m_linkTx.txPeerCID = id;
      link->m_linkTx.txPeerSeq = sequence;
    });
  }

  void setPeerResetToken_(const ResetToken &token) {
    m_linkRx.peerResetToken = token;
    if (m_linkRx.peerCID)
      addPeerCID_(m_linkRx.peerCID, 0, token);
  }

  bool addLocalCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID(m_linkRx.localCIDs, id, sequence, resetToken, true);
  }
  bool addPeerCID_(
    const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken = {}) {
    return addCID(m_linkRx.peerCIDs, id, sequence, resetToken, false);
  }
  LinkCID *findLocalCID(uint64_t sequence) {
    return findCID(m_linkRx.localCIDs, sequence);
  }
  const LinkCID *findLocalCID(uint64_t sequence) const {
    return findCID(m_linkRx.localCIDs, sequence);
  }
  const LinkCID *peerCID_(uint64_t sequence) const {
    return findCID(m_linkRx.peerCIDs, sequence);
  }
  bool retireLocalCID(uint64_t sequence, CxnID &id) {
    LinkCID *cid = findLocalCID(sequence);
    if (!cid || cid->state == CxnState::Tombstone) return false;
    id = cid->id;
    cid->state = CxnState::Retired;
    cid->associated = false;
    ZquicLOG(app()->qlogTrace(), ([
      action = CIDAction::Retired,
      reason = CIDReason::PeerRequest,
      cxnID = cid->id,
      sequence = cid->sequence,
      length = qlogCount(cid->id.length()),
      local = true,
      associated = cid->associated,
      resetToken = cid->resetToken.valid(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CIDEvt event{
	.cxnID = cxnID,
	.linkInfo = linkInfo,
	.sequence = sequence,
	.kind = CIDKind::CxnID,
	.action = CIDAction::T(action),
	.reason = CIDReason::T(reason),
	.length = length,
	.local = local,
	.associated = associated,
	.resetToken = resetToken};

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
    if (frame.offset > m_linkRx.peerRetirePriorTo)
      retirePeerCIDsPriorTo(frame.offset);
    if (frame.value < m_linkRx.peerRetirePriorTo)
      return true;
    return addPeerCID_(id, frame.value, frame.resetToken);
  }
  bool receiveRetireCxnID_(const Frame &frame) {
    if (frame.type != FrameType::RetireCxnID) return false;
    CxnID id;
    if (!retireLocalCID(frame.value, id)) return false;
    impl()->retiredLocalCID(frame.value, id);
    return true;
  }
  bool issueMigrationCIDs() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC migration CID issuance outside Rx thread", return false);
    if (!establishedRx() ||
	app()->migrationMode() != MigrationMode::Active ||
	!app()->migCIDRes())
      return true;
    unsigned peerLimit = m_linkRx.crypto.peerParamsSet() ?
      unsigned(m_linkRx.crypto.peerParams().activeCxnIDLimit) : 2;
    if (peerLimit < 2) return true;
    unsigned target = app()->migCIDRes() + 1;
    if (target > peerLimit) target = peerLimit;
    if (target > LocalCIDLimit) target = LocalCIDLimit;
    unsigned active = 0;
    uint64_t sequence = 0;
    for (auto &cid : m_linkRx.localCIDs) {
      if (cid.state == CxnState::Active) ++active;
      if (cid.state != CxnState::Tombstone && cid.sequence > sequence)
	sequence = cid.sequence;
    }
    bool issued = false;
    unsigned attempts = 0;
    while (active < target && attempts++ < target * 2) {
      CxnID id;
      ResetToken token;
      if (!CxnIDGen::random(id) || !token.generate()) return false;
      uint64_t seq = ++sequence;
      if (!addLocalCID_(id, seq, token)) continue;
      queueControl(ControlFrame::newCxnID(seq, 0, id, token));
      issued = true;
      ++active;
    }
    if (issued)
      impl()->localCIDsIssued();
    return active >= target;
  }
  void scheduleMigrationCIDs() {
    if (rxInvoked_()) {
      if (issueMigrationCIDs()) impl()->migrationCIDsReady();
      return;
    }
    app()->rxRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      if (link->issueMigrationCIDs()) link->migrationCIDsReady();
    });
  }
  void selectPeerCID_1(uint64_t attemptID, bool requireNewPeerCID,
      uint64_t requiredSequence = U64Null) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer CID selection outside Rx thread", return);
    CxnID id;
    uint64_t sequence = U64Null;
    bool selected = selectPeerCIDRx(
      requireNewPeerCID, id, sequence, true, requiredSequence);
    app()->txRun([
      link = impl(), attemptID, id, sequence, selected
    ]() mutable {
      if (link->disconnecting_()) return;
      link->selectPeerCID_2(attemptID, id, sequence, selected);
    });
  }
  bool selectPeerCIDRx(
      bool requireNewPeerCID, CxnID &id, uint64_t &sequence,
      bool reserve = false, uint64_t requiredSequence = U64Null) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer CID selection outside Rx thread", return false);
    id = m_linkRx.peerCID;
    sequence = U64Null;
    if (!m_linkRx.peerCID) {
      sequence = 0;
      return requiredSequence == U64Null;
    }
    for (auto &cid : m_linkRx.peerCIDs) {
      if (cid.state != CxnState::Active || cid.associated) continue;
      if (requiredSequence != U64Null && cid.sequence != requiredSequence) continue;
      if (reserve) cid.associated = true;
      id = cid.id;
      sequence = cid.sequence;
      return true;
    }
    if (requiredSequence != U64Null) return false;
    if (auto cid = findCID(m_linkRx.peerCIDs, m_linkRx.peerCID)) {
      sequence = cid->sequence;
      return !requireNewPeerCID || !m_linkRx.peerCID.length();
    }
    return false;
  }
  void selectPeerCID_0(uint64_t attemptID, bool requireNewPeerCID,
      uint64_t requiredSequence = U64Null) {
    app()->rxRun([
      link = impl(), attemptID, requireNewPeerCID, requiredSequence
    ]() mutable {
      if (link->disconnecting_()) return;
      link->selectPeerCID_1(attemptID, requireNewPeerCID, requiredSequence);
    });
  }
  void promotePath_2(
      uint64_t attemptID, CxnID id, uint64_t sequence) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC promoted peer CID commit outside Rx thread", return);
    bool committed = !id && sequence == 0;
    LinkCID *selected = id ? findCID(m_linkRx.peerCIDs, sequence) : nullptr;
    if (selected && selected->state == CxnState::Active &&
	selected->associated && selected->id == id) {
      for (auto &cid : m_linkRx.peerCIDs) cid.associated = false;
      selected->associated = true;
      m_linkRx.peerCID = selected->id;
      committed = true;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::RouteBound,
	reason = CIDReason::PathPromoted,
	cxnID = selected->id,
	sequence = selected->sequence,
	attemptID,
	length = qlogCount(selected->id.length()),
	local = false,
	associated = selected->associated,
	resetToken = selected->resetToken.valid(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	CIDEvt event{
	  .cxnID = cxnID,
	  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .attemptID = attemptID,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .length = length,
	  .local = local,
	  .associated = associated,
	  .resetToken = resetToken};
	  o.logCIDUpdated(event, time);
      }));
    }
    if (committed) impl()->refreshPromotedRoutes();
    app()->txRun([
      link = impl(), attemptID, id, sequence, committed
    ]() mutable {
      if (link->disconnecting_()) return;
      link->promotePath_3(attemptID, id, sequence, committed);
    });
  }
  void promotePath_1() {
    uint64_t attemptID = m_linkTx.migration.attemptID;
    CxnID id = m_linkTx.txCandidatePeerCID;
    uint64_t sequence = m_linkTx.txCandidatePeerSeq;
    impl()->commitMigEndpoint([
      link = impl(), attemptID, id, sequence
    ](bool ok) mutable {
      if (link->disconnecting_() || !link->m_linkTx.migration.active ||
	  link->m_linkTx.migration.attemptID != attemptID) return;
      if (!ok) {
	link->failMig(MigrationReason::Endpoint);
	return;
      }
      link->app()->rxRun([link, attemptID, id, sequence]() mutable {
	if (link->disconnecting_()) return;
	link->promotePath_2(attemptID, id, sequence);
      });
    });
  }

  void releaseCandidatePeerCID() {
    CxnID id = m_linkTx.txCandidatePeerCID;
    uint64_t sequence = m_linkTx.txCandidatePeerSeq;
    m_linkTx.txCandidatePeerCID.clear();
    m_linkTx.txCandidatePeerSeq = U64Null;
    releasePeerCID(id, sequence);
  }
  void releasePeerCID(CxnID id, uint64_t sequence) {
    if (!id || id == m_linkTx.txPeerCID) return;
    app()->rxRun([link = impl(), id, sequence]() mutable {
      if (link->disconnecting_()) return;
      if (auto cid = findCID(link->m_linkRx.peerCIDs, sequence))
	if (cid->id == id && cid->id != link->m_linkRx.peerCID)
	  cid->associated = false;
    });
  }
  ZuTime pathValidDeadline() const {
    ZuTime timeout = ptoTimeout();
    if (timeout < PathValidMinTimeout) timeout = PathValidMinTimeout;
    return now_() +
      ZuTime{ZuTime::Nano{timeout.nanosecs() * PathValidPTOFactor}};
  }
  template <typename Routes>
  void installLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_linkRx.localCIDs) {
      if (cid.state != CxnState::Active || cid.associated) continue;
      if (!routes.add(cid.id, cid.sequence, impl(), cid.resetToken)) continue;
      cid.associated = true;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::RouteBound,
	reason = CIDReason::RouteInstall,
	cxnID = cid.id,
	sequence = cid.sequence,
	length = qlogCount(cid.id.length()),
	local = true,
	associated = cid.associated,
	resetToken = cid.resetToken.valid(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	CIDEvt event{
	  .cxnID = cxnID,
	  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .length = length,
	  .local = local,
	  .associated = associated,
	  .resetToken = resetToken};

	o.logCIDUpdated(event, time);
      }));
    }
  }
  template <typename Routes>
  void retireLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_linkRx.localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.retire(cid.id);
      cid.associated = false;
      cid.state = CxnState::Retired;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Retired,
	reason = CIDReason::RouteRetire,
	cxnID = cid.id,
	sequence = cid.sequence,
	length = qlogCount(cid.id.length()),
	local = true,
	associated = cid.associated,
	resetToken = cid.resetToken.valid(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	CIDEvt event{
	  .cxnID = cxnID,
	  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .length = length,
	  .local = local,
	  .associated = associated,
	  .resetToken = resetToken};

	o.logCIDUpdated(event, time);
      }));
    }
  }
  template <typename Routes>
  void tombstoneLocalCIDRoutes_(Routes &routes) {
    for (auto &cid : m_linkRx.localCIDs) {
      if (cid.state == CxnState::Tombstone || !cid.associated) continue;
      routes.tombstone(cid.id);
      cid.associated = false;
      cid.state = CxnState::Tombstone;
      ZquicLOG(app()->qlogTrace(), ([
	action = CIDAction::Tombstone,
	reason = CIDReason::RouteTombstone,
	cxnID = cid.id,
	sequence = cid.sequence,
	length = qlogCount(cid.id.length()),
	local = true,
	associated = cid.associated,
	resetToken = cid.resetToken.valid(),
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	CIDEvt event{
	  .cxnID = cxnID,
	  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
	  .length = length,
	  .local = local,
	  .associated = associated,
	  .resetToken = resetToken};

	o.logCIDUpdated(event, time);
      }));
    }
  }

  void clearCIDState() {
    m_linkRx.localCIDs.clear();
    m_linkRx.peerCIDs.clear();
    m_linkRx.peerRetirePriorTo = 0;
  }
  template <typename CIDs>
  static LinkCID *findCID(CIDs &cids, uint64_t seq) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static const LinkCID *findCID(const CIDs &cids, uint64_t seq) {
    for (const auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.sequence == seq)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static LinkCID *findCID(CIDs &cids, const CxnID &id) {
    for (auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.id == id)
	return &cid;
    return nullptr;
  }
  template <typename CIDs>
  static const LinkCID *findCID(const CIDs &cids, const CxnID &id) {
    for (const auto &cid : cids)
      if (cid.state != CxnState::Tombstone && cid.id == id)
	return &cid;
    return nullptr;
  }
  bool acceptLocalShortCID(const CxnID &id) const {
    if (id == m_linkRx.localSCID) return true;
    const LinkCID *cid = findCID(m_linkRx.localCIDs, id);
    return cid && cid->state == CxnState::Active;
  }
  template <typename CIDs>
  bool addCID(
    CIDs &cids, const CxnID &id, uint64_t sequence,
    const ResetToken &resetToken, bool local) {
    if (!id) return false;
    if (!local && !peerCIDTokenUnique(cids, id, resetToken)) return false;
    if (auto cid = findCID(cids, sequence)) {
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
		length = qlogCount(cid->id.length()),
		local,
		associated = cid->associated,
		resetToken = cid->resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvt event{
	  .cxnID = cxnID,
		  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken};

	o.logCIDUpdated(event, time);
      }));
      return true;
    }
    if (auto cid = findCID(cids, id)) {
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
		length = qlogCount(cid->id.length()),
		local,
		associated = cid->associated,
		resetToken = cid->resetToken.valid(),
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
		CIDEvt event{
	  .cxnID = cxnID,
		  .linkInfo = linkInfo,
	  .sequence = sequence,
	  .kind = CIDKind::CxnID,
	  .action = CIDAction::T(action),
	  .reason = CIDReason::T(reason),
		  .length = length,
		  .local = local,
		  .associated = associated,
		  .resetToken = resetToken};

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
      LocalCIDLimit :
      m_linkRx.transportParams.activeCxnIDLimit;
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
	      length = qlogCount(slot->id.length()),
	      local,
	      associated = slot->associated,
	      resetToken = slot->resetToken.valid(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      CIDEvt event{
	.cxnID = cxnID,
		.linkInfo = linkInfo,
	.sequence = sequence,
        .kind = CIDKind::CxnID,
        .action = CIDAction::T(action),
        .reason = CIDReason::T(reason),
		.length = length,
		.local = local,
		.associated = associated,
		.resetToken = resetToken};

      o.logCIDUpdated(event, time);
    }));
    return true;
  }
  template <typename CIDs>
  bool peerCIDTokenUnique(
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
  void retirePeerCIDsPriorTo(uint64_t sequence) {
    m_linkRx.peerRetirePriorTo = sequence;
    for (auto &cid : m_linkRx.peerCIDs)
      if (cid.state == CxnState::Active && cid.sequence < sequence) {
	cid.state = CxnState::Retired;
	ZquicLOG(app()->qlogTrace(), ([
	  action = CIDAction::Retired,
	  reason = CIDReason::RetirePrior,
	  cxnID = cid.id,
	  sequence = cid.sequence,
		  length = qlogCount(cid.id.length()),
		  local = false,
		  associated = cid.associated,
		  resetToken = cid.resetToken.valid(),
		  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
		  CIDEvt event{
	    .cxnID = cxnID,
		    .linkInfo = linkInfo,
	    .sequence = sequence,
	    .kind = CIDKind::CxnID,
	    .action = CIDAction::T(action),
	    .reason = CIDReason::T(reason),
		    .length = length,
		    .local = local,
		    .associated = associated,
		    .resetToken = resetToken};

	  o.logCIDUpdated(event, time);
	}));
      }
  }

  const CxnID &headerCID(CIDSel::T cid) const {
    switch (cid) {
      case CIDSel::Initial: return m_linkTx.info.dcid;
      case CIDSel::Local: return m_linkTx.info.scid;
      default: return m_linkTx.txPeerCID;
    }
  }

  Zquic::LinkInfo linkInfo_() const {
    if (app()->txInvoked()) return m_linkTx.info;
    return Zquic::LinkInfo{
      .origDCID = m_linkRx.origDCID.length() ? m_linkRx.origDCID : m_linkRx.initialDCID,
      .groupID = m_linkRx.groupID.length() ? m_linkRx.groupID : m_linkRx.initialDCID,
      .dcid = m_linkRx.initialDCID,
      .scid = m_linkRx.localSCID
    };
  }
  const TransportParams &localTransportParams() const {
    return m_linkRx.transportParams;
  }

  template <typename AppLike>
  void configLocalParams(AppLike *app) {
    m_linkRx.transportParams.initialSCID = m_linkRx.localSCID;
    m_linkRx.transportParams.maxUDPPayloadSize = app->maxUDP();
    m_linkRx.transportParams.initialMaxData = app->maxData();
    m_linkRx.transportParams.initMaxStreamDataDuplexLcl =
      app->maxStreamData();
    m_linkRx.transportParams.initMaxStreamDataDuplexRmt =
      app->maxStreamData();
    m_linkRx.transportParams.initialMaxStreamDataUni = app->maxStreamData();
    m_linkRx.transportParams.initMaxStreamsDuplex = app->maxStreamsDuplex();
    m_linkRx.transportParams.initialMaxStreamsUni = app->maxStreamsSimplex();
    m_linkRx.transportParams.maxIdleTimeout = app->maxIdleTimeout();
    m_linkRx.transportParams.activeCxnIDLimit = LocalCIDLimit;
    m_linkRx.transportParams.disableActiveMigration =
      app->migrationMode() != MigrationMode::Active;
    m_linkRx.rxDataCredit.set(m_linkRx.transportParams.initialMaxData);
    m_linkRx.localLimit[Zquic::StreamType::Duplex].set(
      m_linkRx.transportParams.initMaxStreamsDuplex);
    m_linkRx.localLimit[Zquic::StreamType::Simplex].set(
      m_linkRx.transportParams.initialMaxStreamsUni);
  }

  bool loadSrvParams(const ServerBootstrap &bootstrap) {
    if (bootstrap.transportParams(m_linkRx.transportParams)) return true;
    tlsFailure();
    return false;
  }

  bool deriveInitial_() {
    if (m_linkRx.crypto.deriveInitial(m_linkRx.initialDCID)) {
      const auto &keys = m_linkRx.crypto.initialKeys();
      app()->txRun([link = impl(),
	  secret = m_config.isServer ? keys.server : keys.client,
	  params = m_linkRx.transportParams]() mutable {
	if (link->disconnecting_()) return;
	link->m_linkTx.initialSecret = secret;
	link->m_linkTx.localParams = params;
      });
      return true;
    }
    tlsFailure();
    return false;
  }

  bool initTLS_(CryptoConfig config) {
    config.localParams = &m_linkRx.transportParams;
    if (m_linkRx.crypto.initTLS(config)) {
      ZquicLOG(app()->qlogTrace(), ([
	origDCID = m_linkRx.transportParams.origDCID,
	initialSCID = m_linkRx.transportParams.initialSCID,
	retrySCID = m_linkRx.transportParams.retrySCID,
	statelessResetTokenPresent =
	  m_linkRx.transportParams.statelessResetTokenPresent,
	maxIdleTimeout = m_linkRx.transportParams.maxIdleTimeout,
	maxUDPPayloadSize = m_linkRx.transportParams.maxUDPPayloadSize,
	ackDelayExponent = m_linkRx.transportParams.ackDelayExponent,
	maxAckDelay = m_linkRx.transportParams.maxAckDelay,
	activeCxnIDLimit = m_linkRx.transportParams.activeCxnIDLimit,
	initialMaxData = m_linkRx.transportParams.initialMaxData,
	initMaxStreamDataDuplexLcl =
	  m_linkRx.transportParams.initMaxStreamDataDuplexLcl,
	initMaxStreamDataDuplexRmt =
	  m_linkRx.transportParams.initMaxStreamDataDuplexRmt,
		initialMaxStreamDataUni = m_linkRx.transportParams.initialMaxStreamDataUni,
		initMaxStreamsDuplex = m_linkRx.transportParams.initMaxStreamsDuplex,
		initialMaxStreamsUni = m_linkRx.transportParams.initialMaxStreamsUni,
		disableActiveMigration = m_linkRx.transportParams.disableActiveMigration,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	ParamsEvt event{
	  .origDCID = origDCID,
	  .initialSCID = initialSCID,
	  .retrySCID = retrySCID,
		  .linkInfo = linkInfo,
	  .maxIdleTimeout = maxIdleTimeout,
	  .maxUDPPayloadSize = maxUDPPayloadSize,
	  .ackDelayExponent = ackDelayExponent,
	  .maxAckDelay = maxAckDelay,
	  .activeCxnIDLimit = activeCxnIDLimit,
	  .initialMaxData = initialMaxData,
	  .initMaxStreamDataDuplexLcl = initMaxStreamDataDuplexLcl,
	  .initMaxStreamDataDuplexRmt = initMaxStreamDataDuplexRmt,
	  .initialMaxStreamDataUni = initialMaxStreamDataUni,
	  .initMaxStreamsDuplex = initMaxStreamsDuplex,
		  .initialMaxStreamsUni = initialMaxStreamsUni,
	  .initiator = Initiator::Local,
		  .statelessResetTokenPresent = statelessResetTokenPresent,
		  .disableActiveMigration = disableActiveMigration};
	o.logParamsSet(event, time);
      }));
      return true;
    }
    tlsFailure();
    return false;
  }

  bool readyToEstablish() const {
    return m_linkRx.rxLinkState == LinkState::Handshaking &&
      m_linkRx.crypto.oneRTTReady() &&
      m_linkRx.crypto.txSecretInstalled(PktNumSpace::AppData) &&
      m_linkRx.crypto.rxSecretInstalled(PktNumSpace::AppData);
  }

  bool validateServerParams_(const ClientBootstrap &bootstrap) {
    if (!m_linkRx.crypto.peerParamsSet() ||
	!bootstrap.validateServerParams(
	  m_linkRx.crypto.peerParams(), m_linkRx.peerCID))
      return false;
    const auto &params = m_linkRx.crypto.peerParams();
    ZeroRTTReason::T reason = validatePeerZeroRTT();
    if (reason != ZeroRTTReason::None)
      rejectEarlyData(reason);
    if (params.statelessResetTokenPresent)
      m_linkRx.peerResetToken = params.statelessResetToken;
    if (const auto &preferred = params.preferredAddress; preferred) {
      if (!m_linkRx.peerCID ||
	  !addPeerCID_(preferred.cid, 1, preferred.resetToken)) return false;
    }
    return true;
  }

  bool validateClientParams() const {
    return m_linkRx.crypto.peerParamsSet() &&
      !m_linkRx.crypto.peerParams().statelessResetTokenPresent &&
      !m_linkRx.crypto.peerParams().preferredAddress;
  }

  void establish_() {
    if (m_linkRx.crypto.peerParamsSet()) {
      const auto &params = m_linkRx.crypto.peerParams();
      postPeerStreamLimitsTx(
	params.initMaxStreamsDuplex, params.initialMaxStreamsUni, params.initialMaxData);
      ZquicLOG(app()->qlogTrace(), ([
	origDCID = params.origDCID,
	initialSCID = params.initialSCID,
	retrySCID = params.retrySCID,
	statelessResetTokenPresent = params.statelessResetTokenPresent,
	maxIdleTimeout = params.maxIdleTimeout,
	maxUDPPayloadSize = params.maxUDPPayloadSize,
	ackDelayExponent = params.ackDelayExponent,
	maxAckDelay = params.maxAckDelay,
	activeCxnIDLimit = params.activeCxnIDLimit,
	initialMaxData = params.initialMaxData,
	initMaxStreamDataDuplexLcl =
	  params.initMaxStreamDataDuplexLcl,
	initMaxStreamDataDuplexRmt =
	  params.initMaxStreamDataDuplexRmt,
		initialMaxStreamDataUni = params.initialMaxStreamDataUni,
		initMaxStreamsDuplex = params.initMaxStreamsDuplex,
		initialMaxStreamsUni = params.initialMaxStreamsUni,
		disableActiveMigration = params.disableActiveMigration,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	ParamsEvt event{
	  .origDCID = origDCID,
	  .initialSCID = initialSCID,
	  .retrySCID = retrySCID,
		  .linkInfo = linkInfo,
	  .maxIdleTimeout = maxIdleTimeout,
	  .maxUDPPayloadSize = maxUDPPayloadSize,
	  .ackDelayExponent = ackDelayExponent,
	  .maxAckDelay = maxAckDelay,
	  .activeCxnIDLimit = activeCxnIDLimit,
	  .initialMaxData = initialMaxData,
	  .initMaxStreamDataDuplexLcl = initMaxStreamDataDuplexLcl,
	  .initMaxStreamDataDuplexRmt = initMaxStreamDataDuplexRmt,
	  .initialMaxStreamDataUni = initialMaxStreamDataUni,
	  .initMaxStreamsDuplex = initMaxStreamsDuplex,
		  .initialMaxStreamsUni = initialMaxStreamsUni,
	  .initiator = Initiator::Remote,
		  .statelessResetTokenPresent = statelessResetTokenPresent,
		  .disableActiveMigration = disableActiveMigration};
	o.logParamsSet(event, time);
      }));
    }
	    ZquicLOG(app()->qlogTrace(), ([
	      alpn = ZeString{m_linkRx.crypto.negotiatedProtocol()},
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      if (!alpn) return;
      SecEvt event{
		.alpn = ZuMv(alpn),
		.linkInfo = linkInfo,
	.kind = SecKind::ALPN,
		.trigger = SecTrigger::Selected,
		.success = true};
      o.logALPNInfo(event, time);
    }));
	    updatePeerPathMaxUDP();
	    establishState();
	    if (m_linkRx.rxEarlyData.state == LinkEarlyState::Recv ||
		m_linkRx.rxEarlyData.state == LinkEarlyState::Offered) {
	      acceptEarlyDataRx();
	      app()->txRun([link = impl()]() mutable {
		if (link->disconnecting_()) return;
		link->acceptEarlyDataTx();
	      });
	    }
	    startIdleTimer();
	    startHeartBeatTimer();
    ++m_linkRx.rxDiag.handshakeComplete;
  }

  auto negotiatedProtocol_() const {
    return m_linkRx.crypto.negotiatedProtocol();
  }

  void resetPkts() {
    for (auto &s : m_linkTx.txCrypto) s.reset();
    for (auto &queue : m_linkTx.txCryptoUnackd) queue.clean();
    for (auto &p : m_linkTx.txPkts) p.clear();
    resetPktState();
  }
  void resetPktState() {
    m_linkTx.initialSecret.clear();
    m_linkTx.info = {};
    m_linkTx.localParams = {};
    m_linkTx.peerParams = {};
    m_linkTx.peerParamsSet = false;
    m_linkTx.rxAppSecret = false;
    for (auto &p : m_linkTx.txProt) p.clear();
    m_linkTx.txEarlyProt.clear();
    m_linkTx.txEarlyData = {};
    for (auto &pn : m_linkTx.txPN) pn = 0;
    for (auto &pn : m_linkTx.txLargestAckd) pn = U64Null;
    for (auto &ack : m_linkTx.txAck) ack = {};
    m_linkTx.coalesceInitial = nullptr;
    m_linkTx.coalesceAddr = {};
    m_linkTx.coalesceLong = false;
    m_linkTx.txInitProbeOut = {};
    m_linkTx.txHandshakeProbeOut = {};
    m_linkTx.txAppProbeOut = {};
    for (auto &discarded : m_linkTx.txSpaceDiscarded) discarded = false;
    m_linkTx.rtt = {};
    m_linkTx.ptoBackoff.reset();
    m_linkTx.txKeyPhase = false;
    m_linkTx.txKeyGeneration = 0;
  }

  void closeStreamsRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream table Rx close outside Rx thread", return);
    if (m_linkRx.closePending) return;
    if (!m_linkRx.closeCursor) m_linkRx.closeCursor = m_linkRx.allStreams.headPtr();
    closeStreamsRx_();
  }
  void closeStreamsRx_() {
    do {
      unsigned budget = StreamCloseBatch;
      while (budget && m_linkRx.closeCursor) {
	--budget;
	auto node = m_linkRx.closeCursor;
	auto stream = static_cast<Stream *>(node);
	if (!stream->closeRx(budget)) break;
	m_linkRx.closeCursor = static_cast<RxStreams::Node::Ext *>(node)->next;
      }
      if (!m_linkRx.closeCursor) return;
      if (app()->mx() && app()->mx()->running()) {
	m_linkRx.closePending = true;
	app()->rxRun([link = impl(), runtime = m_linkRx.rxRuntimeGen]() mutable {
	  if (runtime != link->m_linkRx.rxRuntimeGen || link->disconnecting_()) return;
	  link->m_linkRx.closePending = false;
	  link->closeStreamsRx_();
	});
	return;
      }
    } while (true);
  }

  template <typename L>
  void retireStreamsRx(L l, ZmRef<StreamCloseWork> work) {
    m_linkRx.closeCursor = nullptr;
    m_linkRx.closePending = false;
    do {
      unsigned budget = StreamCloseBatch;
      if (work->next == work->streams.length()) {
	work->streams.clear();
	work->next = 0;
	while (budget) {
	  auto node = m_linkRx.allStreams.headPtr();
	  if (!node) break;
	  StreamRef stream{static_cast<Stream *>(node)};
	  m_linkRx.allStreams.delNode(node);
	  (void)m_linkRx.streams->del(stream->id());
	  work->streams.push(ZuMv(stream));
	  --budget;
	}
	// Prevent queued callbacks from delivering detached streams while
	// their buffer cleanup is yielding.
	for (auto &stream : work->streams) stream->finalRxQueues();
      }
      while (budget && work->next < work->streams.length()) {
	--budget;
	auto &stream = work->streams[work->next];
	if (!stream->closeRx(budget)) break;
	stream = nullptr;
	++work->next;
      }
      while (budget) {
	auto node = m_linkRx.closedStreamList.headPtr();
	if (!node) break;
	auto id = node->id;
	m_linkRx.closedStreamList.delNode(node);
	(void)m_linkRx.closedStreams->del(id);
	--budget;
      }
      bool drained = work->next == work->streams.length() &&
	!m_linkRx.streams->count_() && !m_linkRx.closedStreams->count_();
      if (drained) {
	for (auto &crypto : m_linkRx.rxCrypto)
	  if (!crypto.drainRx(budget)) { drained = false; break; }
      }
      if (drained) break;
      if (app()->mx() && app()->mx()->running()) {
	app()->rxRun([link = impl(), l = ZuMv(l), work = ZuMv(work)]() mutable {
	  link->retireStreamsRx(ZuMv(l), ZuMv(work));
	});
	return;
      }
    } while (true);
    for (auto &base : m_linkRx.closedStreamBase) base = 0;
    for (auto &pending : m_linkRx.closedStreamPending) pending = false;
    for (auto &limit : m_linkRx.localLimit) limit = StreamLimit{MaxStreamCount};
    l();
  }

  template <typename L>
  void retireStreamsTx(L l) {
    m_linkTx.creditCursor = nullptr;
    m_linkTx.creditScanning = false;
    do {
      unsigned budget = StreamCloseBatch;
      clearWritableStreams(budget);
      while (budget) {
	auto node = m_linkTx.creditStreams.headPtr();
	if (!node) break;
	--budget;
	StreamRef stream{static_cast<Stream *>(node)};
	if (!stream->retiredTx()) stream->finalTxQueues();
	if (!stream->closeTx(budget)) break;
	m_linkTx.creditStreams.delNode(node);
	(void)m_linkTx.txStreams->del(stream->id());
      }
      bool drained = !m_linkTx.streamQueue.headPtr() && !m_linkTx.creditStreams.headPtr();
      if (drained) drained = clearPeerAcksTx(budget);
      if (drained) {
	for (unsigned i = 0; i < PktNumSpace::N; ++i) {
	  if (!drainPktSpaceTx(PktNumSpace::T(i), budget)) {
	    drained = false;
	    break;
	  }
	}
      }
      if (drained) break;
      if (app()->mx() && app()->mx()->running()) {
	app()->txRun([link = impl(), l = ZuMv(l)]() mutable {
	  link->retireStreamsTx(ZuMv(l));
	});
	return;
      }
    } while (true);
    m_linkTx.nextDuplexOrdinal = m_linkTx.nextUniOrdinal = 0;
    for (auto &limit : m_linkTx.peerLimit) limit = StreamLimit{MaxStreamCount};
    for (auto &queued : m_linkTx.queued) queued = 0;
    for (auto &pending : m_linkTx.openQueuedPending) pending = false;
    clearLinkControls();
    resetPkts();
    m_linkTx.resetting = false;
    l();
  }

  void clearPeerKeyState() {
    m_linkRx.rxOldSecret.clear();
    m_linkRx.rxOldProt.clear();
    m_linkRx.rxNextSecret.clear();
    m_linkRx.rxNextProt.clear();
    m_linkRx.rxKeyPhase = false;
    m_linkRx.rxOldKeyPhase = false;
    m_linkRx.rxKeyGeneration = 0;
  }
  bool ensureNextPeerKey() {
    if (m_linkRx.rxNextProt.valid()) return true;
    if (!PktProt::deriveNextSecret(
	  m_linkRx.rxNextSecret,
	  m_linkRx.crypto.rxSecret(PktNumSpace::AppData)))
      return false;
    if (!m_linkRx.rxNextProt.init(
	  m_linkRx.rxNextSecret, PktNumSpace::AppData, false)) {
      m_linkRx.rxNextSecret.clear();
      return false;
    }
    return true;
  }
  bool commitPeerKeyUpdate(const TrafficSecret &nextRxSecret) {
    m_linkRx.rxOldSecret = m_linkRx.crypto.rxSecret(PktNumSpace::AppData);
    if (!m_linkRx.rxOldProt.init(
	  m_linkRx.rxOldSecret, PktNumSpace::AppData, false)) {
      m_linkRx.rxOldSecret.clear();
      return false;
    }
    m_linkRx.rxOldKeyPhase = m_linkRx.rxKeyPhase;
    if (!m_linkRx.crypto.rxSecret(PktNumSpace::AppData, nextRxSecret))
      return false;
    m_linkRx.rxKeyPhase = !m_linkRx.rxKeyPhase;
    uint64_t keyGeneration = ++m_linkRx.rxKeyGeneration;
    m_linkRx.rxNextSecret.clear();
    m_linkRx.rxNextProt.clear();
    ++m_linkRx.rxDiag.peerKeyUpdates;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      keyPhase = uint64_t(m_linkRx.rxKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvt event{
		.linkInfo = linkInfo,
		.value = keyPhase,
	.kind = SecKind::KeyUpdated,
	.packetSpace = level,
	.keyType = SecKeyType::RX,
	.trigger = SecTrigger::Remote,
		.reason = SecReason::KeyPhase,
		.success = true};
      o.logKeyUpdated(event, time);
    }));
    app()->txInvoke(impl(), [link = impl(), keyGeneration]() mutable {
      if (link->disconnecting_()) return link;
      link->txInstallPeerKeyUpdate(keyGeneration);
      link->scheduleKeyDiscardTimer(link->keyDiscardDeadline());
      return link;
    });
    return true;
  }
  void discardOldPeerKeys() {
    if (!m_linkRx.rxOldProt.valid()) return;
    m_linkRx.rxOldSecret.clear();
    m_linkRx.rxOldProt.clear();
    ++m_linkRx.rxDiag.keyDiscards;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      oldKeyPhase = uint64_t(m_linkRx.rxOldKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvt event{
		.linkInfo = linkInfo,
		.value = oldKeyPhase,
	.kind = SecKind::KeyRetired,
	.packetSpace = level,
	.keyType = SecKeyType::RXOld,
	.trigger = SecTrigger::Remote,
		.reason = SecReason::KeyUpdate,
		.success = true};
      o.logKeyRetired(event, time);
    }));
  }
  bool txInstallPeerKeyUpdate(uint64_t keyGeneration) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer key update outside Tx thread", return false);
    if (keyGeneration <= m_linkTx.txKeyGeneration) return true;
    if (keyGeneration != m_linkTx.txKeyGeneration + 1) return false;
    TrafficSecret nextTxSecret;
    if (!PktProt::deriveNextSecret(
	  nextTxSecret, txSecret_(PktNumSpace::AppData)) ||
	!txInstallSecret(PktNumSpace::AppData, nextTxSecret))
      return false;
    m_linkTx.txKeyPhase = !m_linkTx.txKeyPhase;
    m_linkTx.txKeyGeneration = keyGeneration;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::T(PktNumSpace::AppData),
	      keyPhase = uint64_t(m_linkTx.txKeyPhase),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      SecEvt event{
		.linkInfo = linkInfo,
		.value = keyPhase,
	.kind = SecKind::KeyUpdated,
	.packetSpace = level,
	.keyType = SecKeyType::TX,
	.trigger = SecTrigger::Local,
		.reason = SecReason::PeerUpdate,
		.success = true};
      o.logKeyUpdated(event, time);
    }));
    return true;
  }

  bool checkStatelessReset_(ZuBSpan datagram, bool notify = false) {
    if (!StatelessReset::verify(datagram, m_linkRx.peerResetToken)) return false;
    if (m_linkRx.statelessReset) return true;
    m_linkRx.statelessReset = true;
    cancelAckRx();
	    ZquicLOG(app()->qlogTrace(), ([
	      bytes = datagram.length(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvt event{
		.linkInfo = linkInfo,
		.value = bytes,
		.kind = SecKind::StatelessReset,
		.success = true};
      event.trigger = SecTrigger::Received;
      event.reason = SecReason::TokenMatch;
      o.logSecEvt(EvtName::StatelessReset, event, time);
    }));
    m_linkRx.rxAppClose.closed = true;
    drainLinkStateRx();
    m_linkRx.rxAcks.clear();
    ++m_linkRx.rxDiag.failures;
    bool notifyDisconnect = notify && !m_linkShared.disconnecting.xch(1);
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([
      link = ZmRef<Impl>{impl()}, notifyDisconnect, snapshot
    ]() mutable {
      link->applyLinkSnapshotTx(snapshot);
      link->statelessReset_0(notifyDisconnect);
    });
    return true;
  }

  void statelessReset_0(bool notifyDisconnect) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stateless-reset cleanup outside Tx thread", return);
    delTimers();
    drainClosingTx([
      link = ZmRef<Impl>{impl()}, notifyDisconnect, runtime = m_linkTx.txRuntimeGen
    ]() mutable {
      auto app = link->app();
      app->rxRun([link = ZuMv(link), notifyDisconnect, runtime]() mutable {
	if (runtime != link->m_linkRx.rxRuntimeGen ||
	    link->m_linkRx.rxLinkState == LinkState::Closed) return;
	link->statelessReset_1(notifyDisconnect);
      });
    });
  }

  void statelessReset_1(bool notifyDisconnect) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stateless-reset completion outside Rx thread", return);
    impl()->statelessReset();
    if (notifyDisconnect) impl()->resetDisconnected();
  }
  void resetDisconnected() { impl()->disconnected(true); }

  template <
    typename SendFlights,
    typename MarkEstablished, typename AfterEstablished>
  bool advanceTLS(
    size_t inEpoch, ZuBSpan input, ZiSockAddr addr,
    SendFlights sendFlights, MarkEstablished markEstablished,
    AfterEstablished afterEstablished) {
    auto &crypto = m_linkRx.crypto;
    if (!crypto.tlsReady() || linkClosedRx_()) return false;
    auto out = app()->tlsOutputAllocator()->allocBuf(this);
    CryptoOffsets offsets;
    int n = crypto.handleTLSMessage(out.ptr(), offsets, inEpoch, input);
    if (n < 0) {
      tlsFailure();
      if (crypto.tlsKeyUpdateRejected()) {
	// RFC 9001 section 6.
	failTLS_(0x010a, ZuMv(addr));
      } else if (crypto.tlsResult() == Ztls_ErrorRawOutput) {
	failTLS_(TransportError::InternalError, {});
      }
      return false;
    }
    syncEarlyDataTLS();
    if (!sendFlights(ZuMv(out), offsets, addr))
      return false;
    markEstablished();
    return afterEstablished(ZuMv(addr));
  }

  void failTLS_(uint64_t error, ZiSockAddr addr) {
    m_linkRx.crypto.failTLS();
    if (!addr) {
      // Record the failure before disconnecting suppresses close publication.
      closeState(error);
      disconnect();
      return;
    }
    // The closing snapshot retires TLS on Rx after CRYPTO delivery returns.
    app()->txRun([
      link = impl(), runtime = m_linkRx.rxRuntimeGen, error, addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_() || runtime != link->m_linkTx.txRuntimeGen)
	return;
      link->closeStateTx(error);
      (void)link->sendCloseFrame(ZuMv(addr), true);
      link->enterLocalClosingTx();
    });
  }

  StreamRef nextWritableStream() {
    StreamRef stream{m_linkTx.streamQueue.shift()};
    if (!stream) return nullptr;
    stream->refreshCreditTx();
    stream->txQueued(false);
    return stream;
  }

  void clearWritableStreams() {
    for (;;) {
      unsigned budget = StreamCloseBatch;
      if (clearWritableStreams(budget)) return;
    }
  }
  bool clearWritableStreams(unsigned &budget) {
    while (budget) {
      StreamRef stream{m_linkTx.streamQueue.shift()};
      if (!stream) break;
      stream->txQueued(false);
      --budget;
    }
    return !m_linkTx.streamQueue.headPtr();
  }
  template <typename L>
  void drainClosingTx(L l) {
    drainClosingTx(ZuMv(l), m_linkTx.txRuntimeGen, ++m_linkTx.closeDrainSerial);
  }
  template <typename L>
  void drainClosingTx(L l, uint64_t runtime, uint64_t serial) {
    if (runtime != m_linkTx.txRuntimeGen || serial != m_linkTx.closeDrainSerial ||
	m_linkTx.txLinkState == LinkState::Closed) return;
    do {
      unsigned budget = StreamCloseBatch;
      bool drained = clearWritableStreams(budget) && clearPeerAcksTx(budget);
      if (drained) {
	for (auto &packets : m_linkTx.txPkts)
	  if (!packets.drain(budget)) { drained = false; break; }
      }
      if (drained) { l(); return; }
      if (app()->mx() && app()->mx()->running()) {
	app()->txRun([link = impl(), l = ZuMv(l), runtime, serial]() mutable {
	  link->drainClosingTx(ZuMv(l), runtime, serial);
	});
	return;
      }
    } while (true);
  }

  bool streamTxPending(const StreamRef &stream) const {
    return stream &&
      (stream->controlQueued() ||
	(!stream->resetSent() && (stream->txRangeCount() || stream->finReady())));
  }
  bool streamEarlyTxPending(const StreamRef &stream) const {
    return stream && stream->id() >= 0 && !stream->resetSent() &&
      (stream->txRangeCount() || stream->finReady());
  }
  bool streamEarlyTxAllowed(const StreamRef &stream) {
    if (!streamEarlyTxPending(stream)) return false;
    return app()->allowEarlyStream(
      impl(), uint64_t(stream->id()), stream->finReady());
  }

  SentFrameRef controlRef(const ControlFrame &frame) const {
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
	return SentFrameRef::pathChallenge(frame.payload);
      case FrameType::PathResponse:
	return SentFrameRef::pathResponse(frame.payload);
      case FrameType::HandshakeDone:
	return SentFrameRef::handshakeDone();
      case FrameType::NewCxnID:
	return SentFrameRef::newCxnID(
	  frame.streamID, frame.value, frame.cxnID, frame.resetToken);
      case FrameType::ResetStream:
	return SentFrameRef::resetStream(
	  frame.streamID, frame.errorCode, frame.value);
      case FrameType::StopSending:
	return SentFrameRef::stopSending(frame.streamID, frame.errorCode);
      default:
	return SentFrameRef::control();
    }
  }

  ControlFrame controlFrame(const SentFrameRef &ref) const {
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
      frame.payload = ref.payload;
    else if (ref.controlType == FrameType::NewCxnID) {
      frame.cxnID = ref.cxnID;
      frame.resetToken = ref.resetToken;
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
    if (frame.type == FrameType::PathChallenge || frame.type == FrameType::PathResponse)
      build.markPathValidation();
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
	  m_linkTx.maxDataControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    for (unsigned i = 0;
	i < Zquic::StreamType::N && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedCtl(
	    m_linkTx.maxStreamsControl[i], build, budget, assembly, refs,
	    sentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedCtl(
	  m_linkTx.dataBlockedControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    for (unsigned i = 0;
	i < Zquic::StreamType::N && refs.count() < SentPkt::MaxFrames; ++i)
      if (!appendQueuedCtl(
	    m_linkTx.streamsBlockedControl[i], build, budget, assembly, refs,
	    sentControls))
	return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedCtl(
	  m_linkTx.pathChallengeControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    if (refs.count() < SentPkt::MaxFrames &&
	!appendQueuedCtl(
	  m_linkTx.handshakeDoneControl, build, budget, assembly, refs,
	  sentControls))
      return false;
    auto iter = m_linkTx.pathResponses.iter();
    while (refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame = iter();
      if (!frame) break;
      if (!appendControl(
	    frame, build, budget, assembly, refs,
	    sentControls))
	return false;
    }
    auto cidIter = m_linkTx.newCxnIDControls.iter();
    while (refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame = cidIter();
      if (!frame) break;
      if (!appendControl(
	    frame, build, budget, assembly, refs,
	    sentControls))
	return false;
    }
    return true;
  }

  bool appendStreamCtls(
    const StreamRef &stream, PktBuild &build, PktBudget &budget,
    PktAssembly &assembly, TxPktRefs &refs,
    SentControls &sentControls) {
    while (stream && refs.count() < SentPkt::MaxFrames) {
      ControlFrame frame;
      if (!stream->nextQueuedControl(frame)) return true;
      if (!stream->controlStillValid(frame)) {
	stream->controlSent(frame);
	continue;
      }
      if (!appendControl(
	    frame, build, budget, assembly, refs,
	    sentControls))
	return false;
      stream->controlSent(frame);
      return true;
    }
    return true;
  }

  void controlSent_(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::MaxData:
	pendingControlSent(m_linkTx.maxDataControl, frame);
	break;
      case FrameType::MaxStreams:
	pendingControlSent(
	  m_linkTx.maxStreamsControl[streamTypeIndex(frame.streamType)], frame);
	break;
      case FrameType::DataBlocked:
	pendingControlSent(m_linkTx.dataBlockedControl, frame);
	break;
      case FrameType::StreamsBlocked:
	pendingControlSent(
	  m_linkTx.streamsBlockedControl[streamTypeIndex(frame.streamType)], frame);
	break;
      case FrameType::PathChallenge:
	pendingControlSent(m_linkTx.pathChallengeControl, frame);
	break;
      case FrameType::PathResponse:
	m_linkTx.pathResponses.shift();
	break;
      case FrameType::NewCxnID:
	m_linkTx.newCxnIDControls.shift();
	break;
      case FrameType::HandshakeDone:
	pendingControlSent(m_linkTx.handshakeDoneControl, frame);
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::StopSending:
	if (frame.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findTxStream_(int64_t(frame.streamID)))
	    stream->controlSent(frame);
	break;
      case FrameType::ResetStream:
	if (frame.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findTxStream_(int64_t(frame.streamID))) {
	    stream->controlSent(frame);
	    notifyStreamTxClosed(stream);
	  }
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
  void clearNewCxnIDCtl(const ControlFrame &frame) {
    (void)frame;
  }
  void controlAckd(const SentFrameRef &ref) {
    ControlFrame frame = controlFrame(ref);
    switch (ref.controlType) {
      case FrameType::MaxData:
	clearPendingControl(m_linkTx.maxDataControl, frame);
	break;
      case FrameType::MaxStreams:
	clearPendingControl(
	  m_linkTx.maxStreamsControl[streamTypeIndex(ref.streamType)], frame);
	break;
      case FrameType::DataBlocked:
	clearPendingControl(m_linkTx.dataBlockedControl, frame);
	break;
      case FrameType::StreamsBlocked:
	clearPendingControl(
	  m_linkTx.streamsBlockedControl[streamTypeIndex(ref.streamType)], frame);
	break;
      case FrameType::PathChallenge:
	clearPendingControl(m_linkTx.pathChallengeControl, frame);
	break;
      case FrameType::HandshakeDone:
	clearPendingControl(m_linkTx.handshakeDoneControl, frame);
	impl()->handshakeDoneAckd();
	break;
      case FrameType::NewCxnID:
	clearNewCxnIDCtl(frame);
	break;
      case FrameType::MaxStreamData:
      case FrameType::StreamDataBlocked:
      case FrameType::ResetStream:
      case FrameType::StopSending:
	if (ref.streamID <= uint64_t(INT64_MAX))
	  if (StreamRef stream = findTxStream_(int64_t(ref.streamID)))
	    stream->clearControl(ref);
	break;
      default:
	break;
    }
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedControlPkt(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control packetization outside Tx thread", return false);
    uint64_t runtime = m_linkTx.txRuntimeGen;
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
      bool sent = sendPkt(build, ZuMv(addr), refs);
      if (runtime != m_linkTx.txRuntimeGen) return sent;
      if (!sent) return false;
      for (unsigned i = 0, n = sentControls.length(); i < n; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return false;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushCtlStreamsBatch(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    uint64_t runtime = m_linkTx.txRuntimeGen;
    bool sent = false;
    for (unsigned i = 0; i < StreamFlushBatch; ++i) {
      bool last = i + 1 == StreamFlushBatch;
      uint64_t packetsTx = last ? txPackets() : 0;
      bool sent_ = flushCtlStreams(addr, appendAck, sendPkt);
      sent |= sent_;
      if (runtime != m_linkTx.txRuntimeGen) return sent;
      if (!sent_ || !scheduledStreamCount()) break;
      // Yield after a productive full batch. A flow-blocked result can be
      // true without a packet; it must await credit rather than repost itself.
      if (last && txPackets() != packetsTx)
	impl()->queueTxFlush(addr);
    }
    return sent;
  }

  template <typename AppendAck, typename SendPkt>
  bool flushCtlStreams(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    return flushCtlStreamsTx(
      ZuMv(addr),
      [&appendAck](PktBuild &build) { return appendAck(build); },
      [&sendPkt](
	  PktBuild &build, ZiSockAddr addr_,
	  const TxPktRefs &refs) {
	return sendPkt(build, ZuMv(addr_), refs);
      });
  }

  template <typename AppendAck, typename SendPkt>
  bool flushCtlStreamsTx(
    ZiSockAddr addr, AppendAck appendAck, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC control/stream flush outside Tx thread", return false);
    uint64_t runtime = m_linkTx.txRuntimeGen;
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_linkTx.txDataCredit.available();
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
    auto fail = [&refs, this]() {
      requeueStreamRefs(PktNumSpace::AppData, refs, false);
      return false;
    };
    bool blocked = false;
    while (scheduledStreamCount() && refs.count() < SentPkt::MaxFrames) {
      StreamRef stream = nextWritableStream();
      if (!stream || !streamTxPending(stream)) continue;
      if (stream->controlQueued()) {
	if (!appendStreamCtls(
	      stream, build, budget, assembly, refs,
	      sentControls))
	  return fail();
	if (streamTxPending(stream) && stream->id() >= 0)
	  streamWritable(stream);
	continue;
      }
      if (stream->txRangeCount() && m_linkTx.txDataCredit.blocked()) {
	streamWritable(stream);
	queueBlocked(FrameType::DataBlocked, 0, m_linkTx.txDataCredit.limit());
	blocked = true;
	break;
      }
      if (stream->txRangeCount() && !stream->txCreditAvailable()) {
	streamWritable(stream);
	queueBlocked(
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
	if (stream->id() >= 0) streamWritable(stream);
	break;
      }
      SentFrameRef ref =
	SentFrameRef::stream(info.streamID, info.range, info.fin);
      if (!info.length) {
	ref.offset = info.offset;
	ref.length = 0;
      }
      if (!build.commitScratch(unsigned(n)) || !build.add(info.range)) {
	requeueStreamRefs(
	  PktNumSpace::AppData, refs, false, false);
	bool queued = requeueStreamRef(
	  PktNumSpace::AppData, ref, stream.ptr(), true);
	if (queued) impl()->queueRetransmit();
	return false;
      }
      if (!refs.add(ref, stream.ptr())) {
	requeueStreamRefs(
	  PktNumSpace::AppData, refs, false, false);
	bool queued = requeueStreamRef(
	  PktNumSpace::AppData, ref, stream.ptr(), true);
	if (queued) impl()->queueRetransmit();
	return false;
      }
      if (info.length && !m_linkTx.txDataCredit.consume(info.length)) return fail();
      m_linkTx.txDiag.streamBytesTx += info.length;
      ZquicLOG(app()->qlogTrace(), ([
		streamID = info.streamID,
		offset = info.offset,
		length = info.length,
		fin = info.fin,
		linkInfo = linkInfo_()
	      ](auto &o, ZuTime time) {
	if (!length && !fin) return;
	StreamDataEvt event{
		  .linkInfo = linkInfo,
		  .streamID = streamID,
		  .offset = offset,
		  .length = length,
	  .from = StreamDataLoc::Transport,
	  .to = StreamDataLoc::Network,
	  .additionalInfo = fin ?
	    StreamDataInfo::T(StreamDataInfo::FinSet) :
	    StreamDataInfo::T(StreamDataInfo::None)};
	o.logStreamDataMoved(event, time);
      }));
      notifyStreamTxClosed(stream);
      if (streamTxPending(stream) && stream->id() >= 0)
	streamWritable(stream);
    }
    if (refs.count()) {
      bool sent = sendPkt(build, ZuMv(addr), refs);
      if (runtime != m_linkTx.txRuntimeGen) return sent;
      if (!sent) {
	requeueStreamRefs(PktNumSpace::AppData, refs, false);
	return false;
      }
      for (unsigned i = 0, n = sentControls.length(); i < n; ++i)
	controlSent_(sentControls[i]);
      return true;
    }
    return blocked;
  }

  template <typename SendPkt>
  bool flushEarlyStreams(ZiSockAddr addr, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC 0-RTT stream flush outside Tx thread", return false);
    if (!canTxZeroRTT_()) return false;
    uint64_t runtime = m_linkTx.txRuntimeGen;
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_linkTx.txDataCredit.available();
    PktAssembly assembly;
    TxPktRefs refs;
    auto fail = [&refs, this]() {
      requeueStreamRefs(PktNumSpace::AppData, refs, false);
      return false;
    };
    bool blocked = false;
    unsigned checked = scheduledStreamCount();
    while (checked-- && scheduledStreamCount() &&
	refs.count() < SentPkt::MaxFrames) {
      StreamRef stream = nextWritableStream();
      if (!stream) continue;
      if (!streamEarlyTxPending(stream)) {
	if (streamTxPending(stream)) streamWritable(stream);
	continue;
      }
      if (!streamEarlyTxAllowed(stream)) {
	streamWritable(stream);
	continue;
      }
      if (stream->txRangeCount() && m_linkTx.txDataCredit.blocked()) {
	streamWritable(stream);
	blocked = true;
	break;
      }
      if (stream->txRangeCount() && !stream->txCreditAvailable()) {
	streamWritable(stream);
	blocked = true;
	break;
      }
      StreamFrameInfo info;
      int n = StreamPktizer::writeNext(
	build.scratch(), build.scratchAvail(),
	budget, assembly, *stream, &info);
      if (n <= 0) {
	if (stream->id() >= 0) streamWritable(stream);
	break;
      }
      SentFrameRef ref =
	SentFrameRef::stream(info.streamID, info.range, info.fin);
      if (!info.length) {
	ref.offset = info.offset;
	ref.length = 0;
      }
      if (!build.commitScratch(unsigned(n)) || !build.add(info.range)) {
	requeueStreamRefs(
	  PktNumSpace::AppData, refs, false, false);
	bool queued = requeueStreamRef(
	  PktNumSpace::AppData, ref, stream.ptr(), true);
	if (queued) impl()->queueRetransmit();
	return false;
      }
      if (!refs.add(ref, stream.ptr())) {
	requeueStreamRefs(
	  PktNumSpace::AppData, refs, false, false);
	bool queued = requeueStreamRef(
	  PktNumSpace::AppData, ref, stream.ptr(), true);
	if (queued) impl()->queueRetransmit();
	return false;
      }
      if (info.length && !m_linkTx.txDataCredit.consume(info.length)) return fail();
      m_linkTx.txDiag.streamBytesTx += info.length;
      ZquicLOG(app()->qlogTrace(), ([
	streamID = info.streamID,
	offset = info.offset,
	length = info.length,
	fin = info.fin,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	if (!length && !fin) return;
	StreamDataEvt event{
	  .linkInfo = linkInfo,
	  .streamID = streamID,
	  .offset = offset,
	  .length = length,
	  .from = StreamDataLoc::Transport,
	  .to = StreamDataLoc::Network,
	  .additionalInfo = fin ?
	    StreamDataInfo::T(StreamDataInfo::FinSet) :
	    StreamDataInfo::T(StreamDataInfo::None)};
	o.logStreamDataMoved(event, time);
      }));
      notifyStreamTxClosed(stream);
      if (streamEarlyTxPending(stream))
	streamWritable(stream);
    }
    if (refs.count()) {
      bool sent = sendPkt(build, ZuMv(addr), refs);
      if (runtime != m_linkTx.txRuntimeGen) return sent;
      if (!sent) {
	requeueStreamRefs(PktNumSpace::AppData, refs, false);
	return false;
      }
      return true;
    }
    return blocked;
  }

  template <typename AppendAck, typename SendPkt>
  bool sendQueuedStreamPkt(
    StreamRef stream, ZiSockAddr addr, AppendAck appendAck,
    SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream packetization outside Tx thread", return false);
    uint64_t runtime = m_linkTx.txRuntimeGen;
    PktBuild build;
    build.reset();
    PktBudget budget = sendBudget_();
    if (!budget.congestion) return false;
    budget.flow = m_linkTx.txDataCredit.available();
    PktAssembly assembly;
    if (stream->txRangeCount() && m_linkTx.txDataCredit.blocked()) {
      streamWritable(stream);
      queueBlocked(FrameType::DataBlocked, 0, m_linkTx.txDataCredit.limit());
      return false;
    }
    if (stream->txRangeCount() && !stream->txCreditAvailable()) {
      streamWritable(stream);
      uint64_t limit = stream->txCreditLimit();
      queueBlocked(FrameType::StreamDataBlocked, uint64_t(stream->id()), limit);
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
    if (n <= 0) return false;
    SentFrameRef ref = SentFrameRef::stream(info.streamID, info.range, info.fin);
    if (!info.length) {
      ref.offset = info.offset;
      ref.length = 0;
    }
    if (!build.commitScratch(unsigned(n)) || !build.add(info.range)) {
      bool queued = requeueStreamRef(
	PktNumSpace::AppData, ref, stream.ptr(), true);
      if (queued) impl()->queueRetransmit();
      return false;
    }
    TxPktRefs refs;
    if (!refs.add(ref, stream.ptr())) {
      bool queued = requeueStreamRef(
	PktNumSpace::AppData, ref, stream.ptr(), true);
      if (queued) impl()->queueRetransmit();
      return false;
    }
    if (info.length && !m_linkTx.txDataCredit.consume(info.length)) return false;
    bool sent = sendPkt(build, ZuMv(addr), refs);
    if (runtime != m_linkTx.txRuntimeGen) return sent;
    if (!sent) {
      requeueStreamRefs(PktNumSpace::AppData, refs, false);
      return false;
    }
    m_linkTx.txDiag.streamBytesTx += info.length;
    ZquicLOG(app()->qlogTrace(), ([
	      streamID = info.streamID,
	      offset = info.offset,
	      length = info.length,
	      fin = info.fin,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      if (!length && !fin) return;
      StreamDataEvt event{
		.linkInfo = linkInfo,
		.streamID = streamID,
		.offset = offset,
		.length = length,
	.from = StreamDataLoc::Transport,
	.to = StreamDataLoc::Network,
	.additionalInfo = fin ?
	  StreamDataInfo::T(StreamDataInfo::FinSet) :
	  StreamDataInfo::T(StreamDataInfo::None)};
      o.logStreamDataMoved(event, time);
    }));
    notifyStreamTxClosed(stream);
    return true;
  }

  uint64_t localMaxAckDelayUS() const {
    return uint64_t(m_linkRx.transportParams.maxAckDelay) * TimeUSPerMS;
  }

  uint64_t nowUS() const {
    return uint64_t(now_().microsecs());
  }

  bool ackIsImmediate(PktNumSpace::T level) const {
    if (level != PktNumSpace::AppData) return true;
    const AckTracker &tracker = m_linkRx.rxAcks.tracker(level);
    return tracker.multipleRanges();
  }

  bool publishAckRx(ZiSockAddr addr, bool contiguous) {
    constexpr auto space = PktNumSpace::AppData;
    const AckTracker &tracker = m_linkRx.rxAcks.tracker(space);
    bool eligible = establishedRx() && contiguous && tracker.count() == 1;
    if (!eligible || addr != m_linkRx.rxAckAddr ||
	m_linkRx.rxKeyGeneration != m_linkRx.rxAckKeyGen) {
      m_ackRing.producer.epoch = 0;
      m_linkRx.rxAckFast = false;
      m_linkRx.rxAckResume = false;
      return false;
    }
    if (!m_linkRx.rxAckFast) {
      if (!m_linkRx.rxAckResume ||
	  uint64_t(m_ackRing.consumer.readSequence) < m_linkRx.rxAckFallbackSeq)
	return false;
      m_ackRing.producer.epoch = m_linkRx.rxAckEpoch;
      m_linkRx.rxAckFast = true;
    }
    uint64_t sequence = m_linkRx.rxAckSeq + 1;
    if (sequence - uint64_t(m_ackRing.consumer.readSequence) > AckPubSlots) {
      ++m_ackRing.producer.full;
      m_ackRing.producer.epoch = 0;
      m_linkRx.rxAckFast = false;
      m_linkRx.rxAckResume = false;
      return false;
    }
    AckPubSlot &slot = m_ackRing.slots[sequence % AckPubSlots];
    slot.sequence = sequence;
    slot.epoch = m_linkRx.rxAckEpoch;
    slot.gen = m_linkRx.rxAcks.gen(space);
    tracker.range(0, slot.range);
    slot.largestRxTime = m_linkRx.rxAcks.largestRxTime(space);
    slot.deadlineUS = m_linkRx.rxAcks.deadlineSet(space) ?
      m_linkRx.rxAcks.deadline(space) : 0;
    slot.ecn = m_linkRx.rxAcks.ackECN(space);
    slot.due = m_linkRx.rxAcks.immediate(space);
    m_linkRx.rxAckSeq = sequence;
    // This is the only publication on the ordinary Rx ACK path. In
    // particular, no independently published PN can outrun its timestamp.
    m_ackRing.producer.sequence = sequence;
    serviceAckRx();
    return true;
  }

  void refreshAckTx() {
    uint64_t epoch = m_ackRing.producer.epoch;
    if (!epoch || epoch != m_linkTx.txAckEpoch) return;
    uint64_t sequence = m_ackRing.producer.sequence;
    if (sequence <= m_linkTx.txAckSeq) return;
    const AckPubSlot &slot = m_ackRing.slots[sequence % AckPubSlots];
    if (slot.epoch != epoch) return;
    AckSnapshot ack;
    ack.level = PktNumSpace::AppData;
    ack.sequence = sequence;
    ack.epoch = epoch;
    ack.gen = slot.gen;
    ack.keyGeneration = m_linkTx.txAckKeyGen;
    ack.largestRxTime = slot.largestRxTime;
    ack.deadlineUS = slot.deadlineUS;
    ack.ecn = slot.ecn;
    ack.due = slot.due;
    ack.ranges.push(slot.range);
    noteAckTx(ack);
    // Reclamation is separate from successful-send completion. The slot
    // can be reused only after every field has been copied into Tx storage.
    m_ackRing.consumer.readSequence = sequence;
  }

  void requestAckTx() {
    uint64_t gen = m_linkRx.rxAcks.gen(PktNumSpace::AppData);
    if (m_linkRx.rxAckNotifyGen &&
	uint64_t(m_ackCompletion[PktNumSpace::AppData].gen) < m_linkRx.rxAckNotifyGen)
      return;
    m_linkRx.rxAckNotifyGen = gen;
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_() || link->linkClosedTx_()) return;
      link->refreshAckTx();
      if (link->pendingAck(PktNumSpace::AppData)) link->flushTx_();
    });
  }

  void serviceAckRx(bool expired = false) {
    constexpr auto space = PktNumSpace::AppData;
    if (disconnecting_() || closingRx_() || drainingRx()) return;
    consumeAckSentRx();
    if (!m_linkRx.rxAcks.pending(space)) return;
    uint64_t now = nowUS();
    uint64_t deadline;
    if (expired && m_linkRx.rxAcks.due(space, now)) {
      requestAckTx();
      // A direct refusal must leave a live obligation. Retry on a bounded
      // deadline; never re-arm an already-expired absolute time in a loop.
      uint64_t retry = localMaxAckDelayUS();
      deadline = now + (retry > TimeUSPerMS ? retry : TimeUSPerMS);
    } else if (m_linkRx.rxAcks.deadlineSet(space)) {
      deadline = m_linkRx.rxAcks.deadline(space);
    } else {
      uint64_t delay = localMaxAckDelayUS();
      deadline = now + (delay ? delay : TimeUSPerMS);
    }
    if (m_linkRx.rxAckTimerOut && m_linkRx.rxAckTimerOut <= deadline) return;
    m_linkRx.rxAckTimerOut = deadline;
    uint64_t serial = ++m_linkRx.rxAckTimerSerial;
    app()->mx()->del(&m_rxAckTimerStorage.timer);
    app()->mx()->add(&m_rxAckTimerStorage.timer, intervalUS(deadline), ZmScheduler::Update,
      [link = impl(), serial](auto &&arm) {
	return arm([link, serial]() mutable {
	  if (serial != link->m_linkRx.rxAckTimerSerial || link->disconnecting_())
	    return;
	  link->m_linkRx.rxAckTimerOut = 0;
	  link->m_linkRx.rxAckNotifyGen = 0;
	  link->serviceAckRx(true);
	});
      }, app()->rxThread());
  }

  void cancelAckRx() {
    ++m_linkRx.rxAckTimerSerial;
    m_linkRx.rxAckTimerOut = 0;
    m_linkRx.rxAckNotifyGen = 0;
    m_ackRing.producer.epoch = 0;
    m_linkRx.rxAckFast = m_linkRx.rxAckResume = false;
    app()->mx()->del(&m_rxAckTimerStorage.timer);
  }

  void postAckSnapshot(
    PktNumSpace::T level, ZiSockAddr addr, bool contiguous = false) {
    PktNumSpace::T space = level;
    if (!m_linkRx.rxAcks.pending(space)) return;
    if (level == PktNumSpace::AppData && publishAckRx(addr, contiguous)) return;
    AckSnapshot ack;
    ack.level = level;
    ack.gen = m_linkRx.rxAcks.gen(space);
    if (level == PktNumSpace::AppData)
      ack.keyGeneration = m_linkRx.rxKeyGeneration;
    ack.largestRxTime = m_linkRx.rxAcks.largestRxTime(space);
    ack.due = m_linkRx.rxAcks.immediate(space);
    ack.ecn = m_linkRx.rxAcks.ackECN(space);
    const AckTracker &tracker = m_linkRx.rxAcks.tracker(space);
    ack.ranges = tracker.snapshot(Frame::MaxAckRanges);
    bool deadline = m_linkRx.rxAcks.deadlineSet(space);
    uint64_t deadlineUS = deadline ? m_linkRx.rxAcks.deadline(space) : 0;
    ack.deadlineUS = deadlineUS;
    if (level == PktNumSpace::AppData) {
      m_ackRing.producer.epoch = 0;
      m_linkRx.rxAckFast = false;
      ack.sequence = ++m_linkRx.rxAckSeq;
      ack.epoch = ++m_linkRx.rxAckEpoch;
      m_linkRx.rxAckFallbackSeq = ack.sequence;
      m_linkRx.rxAckResume = establishedRx() && contiguous && tracker.count() == 1;
      m_linkRx.rxAckAddr = addr;
      m_linkRx.rxAckKeyGen = m_linkRx.rxKeyGeneration;
      serviceAckRx();
    }
    ZmRef<AckHandoff> handoff = new AckHandoff{
      ZuMv(ack), ZuMv(addr), m_linkRx.rxRuntimeGen};
    ++m_ackPost[level].published.value;
    ++m_linkRx.rxDiag.ackSnapshotPostsRx;
    app()->txRun([link = impl(), handoff = ZuMv(handoff)]() mutable {
      auto level = handoff->ack.level;
      ++link->m_ackPost[level].consumed.value;
      if (link->disconnecting_() || handoff->runtime != link->m_linkTx.txRuntimeGen) return;
      link->consumeAckSnapshotTx(handoff->ack, ZuMv(handoff->addr));
    });
  }

  void noteAck(
    PktNumSpace::T level, uint64_t pn, bool ackEliciting, ZiSockAddr addr,
    bool forceImmediate = false, EcnMark::T ecn = EcnMark::NotECT) {
    consumeAckSentRx();
    if (m_linkRx.rxSpaceDiscarded[level]) return;
    PktNumSpace::T space = level;
    uint64_t now = nowUS();
    bool immediate = ackEliciting && (forceImmediate || ackIsImmediate(level));
    if (!m_linkRx.rxAcks.received(
	space, pn, now, localMaxAckDelayUS(), ackEliciting, immediate, ecn))
      return;
    ++m_linkRx.rxDiag.ackCommitsRx;
    if (ackEliciting) ++m_linkRx.rxDiag.ackElicitingRx;
    if (immediate) ++m_linkRx.rxDiag.ackImmediateRx;
    noteRxECN(level, ecn);
    bool contiguous = pn == m_linkRx.rxLargestPN[level] + 1;
    if (pn > m_linkRx.rxLargestPN[level])
      m_linkRx.rxLargestPN[level] = pn;
    postAckSnapshot(level, ZuMv(addr), contiguous && !forceImmediate);
    if (level == PktNumSpace::AppData && establishedRx())
      notePeerPkt();
  }

  void noteDupAck(PktNumSpace::T level, uint64_t pn, ZiSockAddr addr) {
    consumeAckSentRx();
    if (m_linkRx.rxSpaceDiscarded[level] || drainingRx() || closingRx_()) return;
    if (!m_linkRx.rxAcks.ackEliciting(
	level, pn, nowUS(), localMaxAckDelayUS(), true))
      return;
    ++m_linkRx.rxDiag.ackCommitsRx;
    ++m_linkRx.rxDiag.ackElicitingRx;
    ++m_linkRx.rxDiag.ackImmediateRx;
    postAckSnapshot(level, ZuMv(addr));
  }

  bool noteAckTx(const AckSnapshot &ack) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK snapshot install outside Tx thread", return false);
    if (ack.sequence) {
      if (ack.sequence <= m_linkTx.txAckSeq) return false;
      m_linkTx.txAckSeq = ack.sequence;
    }
    if (ack.gen <= m_linkTx.txAckSentGen[ack.level]) return false;
    bool due = m_linkTx.txAck[ack.level].due || ack.due;
    m_linkTx.txAck[ack.level] = ack;
    m_linkTx.txAck[ack.level].due = due;
    ++m_linkTx.txDiag.ackSnapshotInstallsTx;
    if (due) ++m_linkTx.txDiag.ackDueInstallsTx;
    return due;
  }

  void consumeAckSnapshotTx(const AckSnapshot &ack, ZiSockAddr addr) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK snapshot consume outside Tx thread", return);
    auto level = ack.level;
    if (m_linkTx.txSpaceDiscarded[level]) return;
    // A coalesced ACK for a new peer key phase must use the corresponding
    // updated sending keys, even if its Tx post predates the key update.
    if (level == PktNumSpace::AppData &&
	!txInstallPeerKeyUpdate(ack.keyGeneration))
      return;
    if (level == PktNumSpace::AppData && ack.sequence >= m_linkTx.txAckSeq) {
      m_linkTx.txAckEpoch = ack.epoch;
      m_linkTx.txAckKeyGen = ack.keyGeneration;
    }
    bool due = noteAckTx(ack);
    if (ack.sequence) m_ackRing.consumer.readSequence = m_linkTx.txAckSeq;
    if (due || pendingAck(level)) {
      if (level == PktNumSpace::AppData) cancelAckDelayTimer();
      impl()->flushTx_(ZuMv(addr));
    }
  }

  bool appendPendingAck(
    PktNumSpace::T level, PktBuild &build, bool ackOnly = false) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK append outside Tx thread", return false);
    if (level == PktNumSpace::AppData) refreshAckTx();
    AckSnapshot &ack = m_linkTx.txAck[level];
    ++m_linkTx.txDiag.ackAppendTx;
    if (!ack.ranges) {
      ++m_linkTx.txDiag.ackAppendEmptyTx;
      return true;
    }
    if (ackOnly && !pendingAck(level)) {
      ++m_linkTx.txDiag.ackAppendNotDueTx;
      return true;
    }
    uint64_t delay = 0;
    if (level == PktNumSpace::AppData) {
      uint64_t now = nowUS();
      if (now > ack.largestRxTime)
	delay = (now - ack.largestRxTime) >>
	  m_linkTx.localParams.ackDelayExponent;
    }
    int n = ack.ecn.any() ?
      FrameCodec::writeAckECN(
	build.scratch(), build.scratchAvail(),
	ack.ranges.data(), ack.ranges.length(),
	delay, ack.ecn) :
      FrameCodec::writeAckRanges(
	build.scratch(), build.scratchAvail(),
	ack.ranges.data(), ack.ranges.length(),
	delay);
    if (n < 0) return false;
    if (!build.commitScratch(unsigned(n))) return false;
    build.markAck(unsigned(level), ack.gen,
      ack.ranges[ack.ranges.length() - 1].largest);
    return true;
  }

  bool pendingAck(PktNumSpace::T level) {
    if (level == PktNumSpace::AppData) refreshAckTx();
    const AckSnapshot &ack = m_linkTx.txAck[level];
    return ack.ranges && (ack.due ||
      (ack.deadlineUS && nowUS() >= ack.deadlineUS));
  }

  void ackSentTx(PktNumSpace::T level, uint64_t gen) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK commit outside Tx thread", return);
    ++m_linkTx.txDiag.ackSentTx;
    if (m_linkTx.txAck[level].gen <= gen) {
      m_linkTx.txAck[level].ranges.clear();
      m_linkTx.txAck[level].due = false;
      if (level == PktNumSpace::AppData) cancelAckDelayTimer();
    }
    if (gen > m_linkTx.txAckSentGen[level]) {
      m_linkTx.txAckSentGen[level] = gen;
      m_ackCompletion[level].gen = gen;
    }
  }

  void consumeAckSentRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive-state commit outside Rx thread", return);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto space = PktNumSpace::T(i);
      uint64_t gen = m_ackCompletion[i].gen;
      m_linkRx.rxAcks.sent(space, gen);
      // A fresh completion also confirms that Tx drained the old runtime
      // and reset its PN retirement frontier before receiving new ACK work.
      if (gen < m_linkRx.rxAckMinGen[i]) continue;
      uint64_t frontier = m_ackCompletion[i].retireFrontier;
      if (frontier > m_linkRx.rxAckRetired[i]) {
	m_linkRx.rxAckRetired[i] = frontier;
	m_linkRx.rxAcks.tracker(space).ackdByPeer(frontier - 1);
      }
    }
  }

  void scheduleAckDelayTimer(ZuTime out) {
    scheduleCxnTimer(
      "ACK delay", CxnTimer::AckDelay,
      out, ZmScheduler::Advance, &m_timers.ackDelay.node);
  }
  void cancelAckDelayTimer() { cancelTimer("ACK delay", &m_timers.ackDelay.node); }

  void scheduleLossTimer(ZuTime out) {
    scheduleCxnTimer(
      "loss time", CxnTimer::Loss,
      out, ZmScheduler::Update, &m_timers.loss.node);
  }
  ZuTime lossThreshold() const { return m_linkTx.rtt.timeThreshold(); }
  ZuTime persistentCongestionThresh() const {
    return ptoTimeout() * ZuDecimal{3};
  }
  ZuTime nextLossTime_() const {
    ZuTime threshold = lossThreshold();
    ZuTime out;
    bool have = false;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto level = PktNumSpace::T(i);
      if (m_linkTx.txSpaceDiscarded[level]) continue;
      ZuTime deadline =
	m_linkTx.txPkts[level].nextLossTime(
	  m_linkTx.txLargestAckd[level], threshold, RecoveryScanBatch);
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
      if (m_linkTx.txSpaceDiscarded[level] ||
	  ZuNull(m_linkTx.txLargestAckd[level]) ||
	  !m_linkTx.txPkts[level].ackElicitingInFlight())
	continue;
      return true;
    }
    return false;
  }
  bool detectLoss(
    PktNumSpace::T level, ZuTime now, PktLossBatch &batch, bool &complete) {
    complete = true;
    if (m_linkTx.txSpaceDiscarded[level]) return false;
    ZuTime threshold = lossThreshold();
    if (!*threshold) return false;
    PktTxUpdate update;
    complete = m_linkTx.txPkts[level].markTimeThreshLossBatch(
      m_linkTx.txLargestAckd[level], now, threshold, batch,
      RecoveryScanBatch, &update);
    if (!update.lostBytes)
      return false;
    applyLossUpdateTx(update);
    ZquicLOG(app()->qlogTrace(), ([
      level,
      lostBytes = update.lostBytes,
      bytesInFlight = txBytesInFlight(),
      lostFrames = QLogTxFrameRefs{update.lostFrames},
      reason = RecReason::TimeThreshold,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (!lostBytes) return;
      RecLogEvt event{
		.linkInfo = linkInfo,
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
    if (ZuUnlikely(app()->congestionControl()))
      if (complete && m_linkTx.txPkts[level].persistentCongestion(
	  persistentCongestionThresh(), RecoveryScanBatch))
	persistentCongestion_();
    updateCongestionDiag();
    return true;
  }
  void detectLossTx(
    unsigned level, ZuTime now, PktLossBatch batch, bool lost) {
    ZiAssert(txInvoked_(), "Zquic", (),
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
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer schedule outside Tx thread", return);
    if (linkClosedTx_()) return;
    if (!lossPending()) {
      ++m_linkTx.txDiag.lossCanceled;
      m_linkTx.lossTimerOut = {};
      m_linkTx.lossRes.logical = ZuTime{};
      return;
    }
    ZuTime out = nextLossTime_();
    if (!*out) {
      ++m_linkTx.txDiag.lossCanceled;
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::Loss,
	space = PktNumSpace::N,
	reason = RecReason::Canceled,
	deadlineUS = uint64_t{0},
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
      m_linkTx.lossTimerOut = {};
      m_linkTx.lossRes.logical = ZuTime{};
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    ++m_linkTx.txDiag.lossArmed;
    m_linkTx.lossTimerOut = out;
    ZquicLOG(app()->qlogTrace(), ([
      kind = RecKind::Loss,
      space = PktNumSpace::N,
      reason = RecReason::Armed,
      deadlineUS = qlogUS(out),
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo,
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
    m_linkTx.lossTimerOut = {};
    cancelTimer("loss time", &m_timers.loss.node);
  }

  void schedulePTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO schedule before app initialization", return);
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->schedulePTO_();
    });
  }

  void schedulePMTUD() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PMTUD schedule before app initialization", return);
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
    if (linkClosedTx_()) return;
    ++m_linkTx.txDiag.ptoSched;
    PktNumSpace::T level = PktNumSpace::Initial;
    if (!ptoLevel(level)) {
      ++m_linkTx.txDiag.ptoNoLevel;
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	space = PktNumSpace::N,
	reason = RecReason::NoLevel,
      deadlineUS = uint64_t{0},
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = PktNumSpace::T(space),
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
      m_linkTx.ptoTimerOut = {};
      m_linkTx.ptoRes.logical = ZuTime{};
      return;
    }
    ZuTime out = ptoDeadline(level);
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_linkTx.ptoTimerLevel != level) cancelPTO_();
    if (debugLog()) ZiLOG(Debug, "Zquic",
      ([level, bytesInFlight = m_linkTx.txPkts[level].bytesInFlight()](auto &s) {
	s << "PTO armed level=" << int(level) <<
	  " bytesInFlight=" << bytesInFlight;
      }));
    ++m_linkTx.txDiag.ptoArmed;
    m_linkTx.ptoTimerOut = out;
    m_linkTx.ptoTimerLevel = level;
    ZquicLOG(app()->qlogTrace(), ([
      kind = RecKind::PTO,
      level,
      reason = RecReason::Armed,
      deadlineUS = qlogUS(out),
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo,
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
    if (m_linkTx.idleTimeout) scheduleIdleTimer();
  }

  void schedulePTOTimer(ZuTime out) {
    scheduleCxnTimer(
      "PTO", CxnTimer::PTO,
      out, ZmScheduler::Update, &m_timers.pto.node);
  }

  void cancelPTO() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC PTO cancel before app initialization", return);
    cancelPTO_();
  }

  void cancelPTO_() {
    if (*m_linkTx.ptoTimerOut)
      ZquicLOG(app()->qlogTrace(), ([
	kind = RecKind::PTO,
	level = m_linkTx.ptoTimerLevel,
	reason = RecReason::Canceled,
		deadlineUS = uint64_t{0},
		cwnd = congestionWindow(),
		ssthresh = congestionSSThresh(),
		bytesInFlight = txBytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo,
	  .deadlineUS = deadlineUS,
	  .cwnd = cwnd,
	  .ssthresh = ssthresh,
	  .bytesInFlight = bytesInFlight,
	  .kind = RecKind::T(kind),
	  .packetSpace = level,
	  .reason = RecReason::T(reason)};

	o.logLossTimerUpd(event, time);
      }));
    m_linkTx.ptoTimerOut = {};
    cancelTimer("PTO", &m_timers.pto.node);
  }

  void startIdleTimer() {
    ZuTime timeout = negotiatedIdleTimeout();
    app()->txRun([link = impl(), timeout]() mutable {
      if (link->disconnecting_()) return;
      link->startIdleTimerTx(timeout);
    });
  }
  void startIdleTimerTx(ZuTime timeout) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer start outside Tx thread", return);
    m_linkTx.idleTimeout = timeout;
    m_linkTx.idleBase = now_();
    m_linkTx.idleAckElicitingSent = false;
    scheduleIdleTimer();
  }
  ZuTime effectiveIdleTimeout() const {
    if (!m_linkTx.idleTimeout) return {};
    ZuTime minTimeout = closeDrainDelay();
    return m_linkTx.idleTimeout < minTimeout ? minTimeout : m_linkTx.idleTimeout;
  }
  ZuTime idleDeadline() const {
    ZuTime timeout = effectiveIdleTimeout();
    if (!timeout) return {};
    return m_linkTx.idleBase + timeout;
  }
  void scheduleIdleTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer schedule outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx() || !m_linkTx.idleTimeout) {
      cancelIdleTimer();
      return;
    }
    ZuTime out = idleDeadline();
    if (!out) {
      cancelIdleTimer();
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    m_linkTx.idleTimerOut = out;
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_timers.idle.node);
  }
  void scheduleIdleTimer(ZuTime out) {
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_timers.idle.node);
  }
  void cancelIdleTimer() {
    m_linkTx.idleTimerOut = {};
    cancelTimer("idle", &m_timers.idle.node);
  }
  void startHeartBeatTimer() {
    ZuTime interval = app()->heartBeat();
    app()->txRun([link = impl(), interval]() mutable {
      if (link->disconnecting_()) return;
      link->startHeartBeatTimerTx(interval);
    });
  }
  void startHeartBeatTimerTx(ZuTime interval) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC heartbeat timer start outside Tx thread", return);
    if (!*interval) {
      cancelHBTimer();
      return;
    }
    m_linkTx.heartBeatBase = now_();
    scheduleHBTimer();
  }
  ZuTime heartBeatDeadline() const {
    ZuTime interval = app()->heartBeat();
    if (!*interval) return {};
    return m_linkTx.heartBeatBase + interval;
  }
  void scheduleHBTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC heartbeat timer schedule outside Tx thread", return);
    ZuTime interval = app()->heartBeat();
    if (linkClosedTx_() || !establishedTx() || !*interval) {
      cancelHBTimer();
      return;
    }
    if (!*m_linkTx.heartBeatBase) m_linkTx.heartBeatBase = now_();
    ZuTime out = heartBeatDeadline();
    if (!*out) {
      cancelHBTimer();
      return;
    }
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    if (m_timers.heartBeat.node && m_linkTx.heartBeatTimerOut == out) return;
    m_linkTx.heartBeatTimerOut = out;
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_timers.heartBeat.node);
  }
  void scheduleHBTimer(ZuTime out) {
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_timers.heartBeat.node);
  }
  void cancelHBTimer() {
    m_linkTx.heartBeatTimerOut = {};
    cancelTimer("heartbeat", &m_timers.heartBeat.node);
  }
  void noteHBTx() {
    if (!*app()->heartBeat()) return;
    m_linkTx.heartBeatBase = now_();
    scheduleHBTimer();
  }
  void notePeerPkt() {
    m_rxPathPub.activityUS = nowUS();
  }
  void refreshPeerPktTx() {
    uint64_t activity = m_rxPathPub.activityUS;
    if (activity <= m_linkTx.txPeerActivityUS) return;
    m_linkTx.txPeerActivityUS = activity;
    if (linkClosedTx_() || !establishedTx()) return;
    ZuTime time = intervalUS(activity);
    if (m_linkTx.idleTimeout) {
      m_linkTx.idleBase = time;
      m_linkTx.idleAckElicitingSent = false;
    }
    if (*app()->heartBeat()) m_linkTx.heartBeatBase = time;
  }
  void noteAckElicitTx() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ack-eliciting send activity outside Tx thread", return);
    refreshPeerPktTx();
    if (linkClosedTx_() || !establishedTx()) return;
    if (m_linkTx.idleTimeout) {
      if (!m_linkTx.idleAckElicitingSent) {
	m_linkTx.idleBase = now_();
	m_linkTx.idleAckElicitingSent = true;
      }
      scheduleIdleTimer();
    }
    noteHBTx();
  }

  void scheduleCloseTimer(ZuTime out) {
    ZuTime now = now_();
    if (out <= now) out = now + RttEstimator::Granularity;
    m_linkTx.closeTimerOut = out;
    scheduleCxnTimer(
      "close", CxnTimer::Close,
      out, ZmScheduler::Update, &m_timers.close.node);
  }
  void cancelCloseTimer() {
    m_linkTx.closeTimerOut = {};
    cancelTimer("close", &m_timers.close.node);
  }

  ZuTime peerCloseDeadline() const {
    return now_() + closeDrainDelay();
  }
  void enterLocalClosingTx() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local closing outside Tx thread", return);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelKeyDiscardTimer();
    cancelPMTUDTimer();
    cancelPathTimer();
    m_linkTx.closeNextResponse = {};
    scheduleCloseTimer(now_());
  }
  void enterPeerDraining_0(uint64_t errorCode) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC peer draining outside Rx thread", return);
    closeStreamsRx();
    m_linkRx.rxAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = false};
    m_linkShared.publicCloseError = errorCode;
    drainLinkStateRx();
    m_linkRx.crypto.resetTLS();
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting_()) return;
      link->applyLinkSnapshotTx(snapshot);
      link->peerDrainingTx();
      link->enterPeerDraining_1();
    });
  }
  void enterPeerDraining_1() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC peer draining outside Tx thread", return);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelKeyDiscardTimer();
    cancelPMTUDTimer();
    cancelPathTimer();
    drainClosingTx([link = impl(), deadline = peerCloseDeadline()] {
      if (!link->disconnecting_() && !link->m_linkTx.closeTimerOut)
	link->scheduleCloseTimer(deadline);
    });
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
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close response outside Tx thread", return);
    if (!closingTx_() || !addr) return;
    ZuTime now = now_();
    if (*m_linkTx.closeNextResponse && now < m_linkTx.closeNextResponse) return;
    m_linkTx.closeNextResponse = now + RttEstimator::Granularity;
    (void)impl()->sendCloseFrame(ZuMv(addr), true);
  }

  void scheduleKeyDiscardTimer(ZuTime out) {
    scheduleCxnTimer(
      "key discard", CxnTimer::KeyDiscard,
      out, ZmScheduler::Advance, &m_timers.keyDiscard.node);
  }
  void cancelKeyDiscardTimer() {
    cancelTimer("key discard", &m_timers.keyDiscard.node);
  }

  void schedulePMTUDTimer(ZuTime out) {
    scheduleCxnTimer(
      "PMTUD", CxnTimer::PMTUD,
      out, ZmScheduler::Advance, &m_timers.pmtud.node);
  }
  void cancelPMTUDTimer() { cancelTimer("PMTUD", &m_timers.pmtud.node); }

  void schedulePMTUD_() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD schedule outside Tx thread", return);
    if (linkClosedTx_() || !establishedTx() ||
	m_linkTx.path.probePending() || m_timers.pmtud.node)
      return;
    if (!m_linkTx.path.nextProbeSize()) return;
    impl()->queueTxFlush();
  }

  void schedulePathTimer(ZuTime out) {
    scheduleCxnTimer(
      "path validation", CxnTimer::PathValid,
      out, ZmScheduler::Update, &m_timers.path.node);
  }
  void cancelPathTimer() {
    cancelTimer("path validation", &m_timers.path.node);
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
    // Live-state cleanup only; disconnect/final teardown uses delTimers().
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelIdleTimer();
    cancelHBTimer();
    cancelCloseTimer();
    cancelKeyDiscardTimer();
    cancelPMTUDTimer();
    cancelPathTimer();
  }
  void delTimers() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer teardown outside Tx thread", return);
    invalidateRes(m_linkTx.lossRes);
    invalidateRes(m_linkTx.ptoRes);
    invalidateRes(m_linkTx.idleRes);
    m_linkTx.lossTimerOut = {};
    m_linkTx.ptoTimerOut = {};
    m_linkTx.idleTimerOut = {};
    m_linkTx.heartBeatTimerOut = {};
    m_linkTx.heartBeatBase = {};
    m_linkTx.closeTimerOut = {};
    m_linkTx.closeNextResponse = {};
    app()->mx()->del(&m_timers.ackDelay.node);
    app()->mx()->del(&m_timers.loss.node);
    app()->mx()->del(&m_timers.pto.node);
    app()->mx()->del(&m_timers.idle.node);
    app()->mx()->del(&m_timers.heartBeat.node);
    app()->mx()->del(&m_timers.close.node);
    app()->mx()->del(&m_timers.keyDiscard.node);
    app()->mx()->del(&m_timers.pmtud.node);
    app()->mx()->del(&m_timers.path.node);
  }
  bool timersActive() const {
    return m_rxAckTimerStorage.timer || m_timers.ackDelay.node ||
      m_timers.loss.node || m_timers.pto.node || m_timers.idle.node ||
      m_timers.heartBeat.node || m_timers.close.node || m_timers.keyDiscard.node ||
      m_timers.pmtud.node || m_timers.path.node;
  }

  void ackDelayTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->ackDelayExpired();
  }
  void lossTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->lossTimeExpired();
  }
  void closeTimeout() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC close timer outside Tx thread", return);
    if (disconnecting_()) return;
    m_linkTx.closeTimerOut = {};
    impl()->closeExpired();
  }
  void idleTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC idle timer outside Tx thread", return);
    refreshPeerPktTx();
    if (disconnecting_()) return;
    if (linkClosedTx_() || !establishedTx() || !m_linkTx.idleTimeout) return;
    ZuTime out = idleDeadline();
    ZuTime now = now_();
    if (*out && out > now) {
      scheduleIdleTimer();
      return;
    }
    impl()->idleExpired();
  }
  void heartBeatTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC heartbeat timer outside Tx thread", return);
    refreshPeerPktTx();
    if (disconnecting_()) return;
    if (linkClosedTx_() || !establishedTx() || !*app()->heartBeat()) return;
    ZuTime out = heartBeatDeadline();
    ZuTime now = now_();
    if (*out && out > now) {
      scheduleHBTimer();
      return;
    }
    dispatchHBExpired(0);
    if (!m_timers.heartBeat.node) scheduleHBTimer();
  }
  void keyDiscardTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->keyDiscardExpired();
  }
  void pmtudTimeout() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (!linkClosedTx_()) impl()->pmtudExpired();
  }
  void pathTimer() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path-validation timer outside Tx thread", return);
    if (disconnecting_()) return;
    if (m_linkTx.migration.active && *m_linkTx.migration.deadline &&
	now_() < m_linkTx.migration.deadline) {
      schedulePathTimer(m_linkTx.migration.deadline);
      return;
    }
    if (!linkClosedTx_()) impl()->pathExpired();
  }

  void ackDelayExpired() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK delay expiry outside Tx thread", return);
    if (!m_linkTx.txAck[PktNumSpace::AppData].ranges) return;
    m_linkTx.txAck[PktNumSpace::AppData].due = true;
    impl()->flushTx_();
  }
  void lossTimeExpired() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC loss timer expired outside Tx thread", return);
    if (linkClosedTx_()) return;
    if (!lossPending()) {
      ++m_linkTx.txDiag.lossCanceled;
      cancelLossTimer();
      schedulePTO_();
      return;
    }
    ++m_linkTx.txDiag.lossExpired;
    ZuTime now = now_();
    detectLossTx(0, now, {}, false);
  }
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
  void keyDiscardExpired() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC key discard expiry outside Tx thread", return);
    app()->rxRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->clearExpiredKeysRx();
    });
  }
  void clearExpiredKeysRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC key discard outside Rx thread", return);
    discardOldPeerKeys();
  }
  void pmtudExpired() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD expiry outside Tx thread", return);
    if (!m_linkTx.path.probeExpired()) return;
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Expired,
	      size = m_linkTx.path.probeSize(),
	      reason = PathReason::Timeout,
	      activeMaxUDP = m_linkTx.path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.mtu = size ? size : activeMaxUDP,
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    schedulePMTUD_();
  }
  void applyPathHint_(PathHint hint) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC path hint outside Tx thread", return);
    if (!hint) return;
    m_linkTx.path.applyHint(hint);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Hint,
	      size = m_linkTx.path.activeMaxUDP(),
	      reason = PathReason::PathHint,
	      activeMaxUDP = m_linkTx.path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.mtu = size ? size : activeMaxUDP,
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    if (!m_linkTx.path.probePending()) cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeAckd(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD ACK outside Tx thread", return);
    if (!size || !m_linkTx.path.probePending()) return;
    m_linkTx.path.probeAckd();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Acked,
	      size,
	      reason = PathReason::Probe,
	      activeMaxUDP = m_linkTx.path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.mtu = size ? size : activeMaxUDP,
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeLost(unsigned size) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD loss outside Tx thread", return);
    if (!size || !m_linkTx.path.probePending()) return;
    m_linkTx.path.probeLost();
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Lost,
	      size,
	      reason = PathReason::Probe,
	      activeMaxUDP = m_linkTx.path.activeMaxUDP(),
	      validated = pathValidated_(),
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.mtu = size ? size : activeMaxUDP,
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
    if (ZuUnlikely(app()->congestionControl())) {
      m_linkTx.congestion.persistentCongestion();
      ++m_linkTx.txDiag.persistentCongestion;
    }
    updateCongestionDiag();
  }

  void discardPktNumSpace_0(PktNumSpace::T level) {
    if (level == PktNumSpace::AppData || m_linkRx.rxSpaceDiscarded[level]) return;
    m_linkRx.rxSpaceDiscarded[level] = true;
    if (level == PktNumSpace::Handshake) m_linkRx.crypto.discardSecret(level);
    drainDiscardedRx(level, m_linkRx.rxRuntimeGen);
    m_linkRx.rxAcks.sent(PktNumSpace::T(level));
	    ZquicLOG(app()->qlogTrace(), ([level, linkInfo = linkInfo_()](auto &o, ZuTime time) {
	      SecEvt event{
		.linkInfo = linkInfo,
		.kind = SecKind::KeyRetired,
		.keyType = SecKeyType::RX,
		.trigger = SecTrigger::HSComplete,
		.reason = SecReason::PacketSpace,
		.success = true};
      event.packetSpace = level;
      o.logKeyRetired(event, time);
    }));
    app()->txRun([link = impl(), level]() mutable {
      if (link->disconnecting_()) return;
      link->discardPktNumSpace_1(level);
    });
  }

  void discardPktNumSpace_1(PktNumSpace::T level) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC sent-packet discard outside Tx thread", return);
    if (level == PktNumSpace::AppData || m_linkTx.txSpaceDiscarded[level]) return;
    cancelPTO_();
    cancelLossTimer();
    m_linkTx.txSpaceDiscarded[level] = true;
    if (level == PktNumSpace::Initial) m_linkTx.initialSecret.clear();
    m_linkTx.txProt[level].clear();
    if (ZuUnlikely(app()->congestionControl()))
      m_linkTx.congestion.release(m_linkTx.txPkts[level].bytesInFlight());
    drainDiscardedTx(level, m_linkTx.txRuntimeGen);
    m_linkTx.txAck[level].ranges.clear();
    ZquicLOG(app()->qlogTrace(), ([
      level,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      SecEvt event{
	.linkInfo = linkInfo,
	.kind = SecKind::KeyRetired,
	.keyType = SecKeyType::TX,
	.trigger = SecTrigger::HSComplete,
	.reason = SecReason::PacketSpace,
	.success = true};
      event.packetSpace = level;
      o.logKeyRetired(event, time);
    }));
    updateCongestionDiag();
    scheduleLossTimer();
    schedulePTO_();
  }

  void drainDiscardedRx(PktNumSpace::T level, uint64_t runtime) {
    if (disconnecting_() || runtime != m_linkRx.rxRuntimeGen) return;
    do {
      unsigned budget = StreamCloseBatch;
      if (m_linkRx.rxCrypto[level].drainRx(budget)) return;
      if (app()->mx() && app()->mx()->running()) {
	app()->rxRun([link = impl(), level, runtime] {
	  link->drainDiscardedRx(level, runtime);
	});
	return;
      }
    } while (true);
  }

  bool drainPktSpaceTx(PktNumSpace::T level, unsigned &budget) {
    return m_linkTx.txCryptoUnackd[level].drain(budget) &&
      m_linkTx.txCrypto[level].drainTx(budget) &&
      m_linkTx.txPkts[level].drain(budget);
  }
  void drainDiscardedTx(PktNumSpace::T level, uint64_t runtime) {
    if (disconnecting_() || runtime != m_linkTx.txRuntimeGen) return;
    do {
      unsigned budget = StreamCloseBatch;
      if (drainPktSpaceTx(level, budget)) return;
      if (app()->mx() && app()->mx()->running()) {
	app()->txRun([link = impl(), level, runtime] {
	  link->drainDiscardedTx(level, runtime);
	});
	return;
      }
    } while (true);
  }

  void rejectEarlyData(ZeroRTTReason::T reason) {
    if (rxInvoked_()) {
      rejectEarlyDataRx(reason);
      app()->txRun([link = impl(), reason]() mutable {
	if (link->disconnecting_()) return;
	link->rejectEarlyDataTx(reason);
      });
      return;
    }
    if (txInvoked_()) rejectEarlyDataTx(reason);
  }

  bool rejectEarlyDataRx(ZeroRTTReason::T reason) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC 0-RTT Rx rejection outside Rx thread", return false);
    if (m_linkRx.rxEarlyData.state == LinkEarlyState::Rejected ||
	m_linkRx.rxEarlyData.state == LinkEarlyState::Done)
      return false;
    m_linkRx.rxEarlyData.state = LinkEarlyState::Rejected;
    m_linkRx.rxEarlyData.reason = reason;
    m_linkRx.crypto.rejectZeroRTT();
    app()->earlyDataRejected(impl(), reason);
    ZquicLOG(app()->qlogTrace(), ([
      reason = zeroRTTSecReason(reason),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      SecEvt event{
	.linkInfo = linkInfo,
	.kind = SecKind::PktProtect,
	.trigger = SecTrigger::RX,
	.reason = reason,
	.success = false};
      event.packetSpace = PktNumSpace::AppData;
      o.logSecEvt(EvtName::ZeroRTTReject, event, time);
    }));
    return true;
  }

  bool rejectEarlyDataTx(ZeroRTTReason::T reason) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC 0-RTT Tx rejection outside Tx thread", return false);
    if (m_linkTx.txEarlyData.state != LinkEarlyState::Rejected) {
      m_linkTx.txEarlyData.state = LinkEarlyState::Rejected;
      m_linkTx.txEarlyData.reason = reason;
    }
    app()->earlyDataRejected(impl(), reason);
    rejectZeroRTTTx(reason);
    return true;
  }

  bool acceptEarlyDataRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC 0-RTT Rx acceptance outside Rx thread", return false);
    if (m_linkRx.rxEarlyData.state == LinkEarlyState::Rejected ||
	m_linkRx.rxEarlyData.state == LinkEarlyState::Done)
      return false;
    if (m_linkRx.rxEarlyData.state == LinkEarlyState::None)
      return false;
    m_linkRx.rxEarlyData.state = LinkEarlyState::Accepted;
    auto iter = m_linkRx.streams->iter();
    while (auto node = iter()) node->data().earlyData(false);
    app()->earlyDataAccepted(impl());
    ZquicLOG(app()->qlogTrace(), ([
      isServer = m_config.isServer,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (!isServer) return;
      SecEvt event{
	.linkInfo = linkInfo,
	.kind = SecKind::TLS,
	.packetSpace = PktNumSpace::AppData,
	.trigger = SecTrigger::Received,
	.reason = SecReason::ZeroRTT,
	.success = true};
      o.logSecEvt(EvtName::ZeroRTTAccept, event, time);
    }));
    return true;
  }

  bool acceptEarlyDataTx() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC 0-RTT Tx acceptance outside Tx thread", return false);
    if (m_linkTx.txEarlyData.state == LinkEarlyState::Rejected ||
	m_linkTx.txEarlyData.state == LinkEarlyState::Done)
      return false;
    if (m_linkTx.txEarlyData.state == LinkEarlyState::None)
      return false;
    m_linkTx.txEarlyData.state = LinkEarlyState::Accepted;
    m_linkTx.txEarlyProt.clear();
    app()->earlyDataAccepted(impl());
    ZquicLOG(app()->qlogTrace(), ([
      isServer = m_config.isServer,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      if (isServer) return;
      SecEvt event{
	.linkInfo = linkInfo,
	.kind = SecKind::TLS,
	.packetSpace = PktNumSpace::AppData,
	.trigger = SecTrigger::Received,
	.reason = SecReason::ZeroRTT,
	.success = true};
      o.logSecEvt(EvtName::ZeroRTTAccept, event, time);
    }));
    return true;
  }

  void syncEarlyDataTLS() {
    if (m_linkRx.crypto.earlyDataRejected()) {
      rejectEarlyData(ZeroRTTReason::TLSRejected);
      return;
    }
    if (!m_linkRx.crypto.earlyDataAccepted()) return;
    if (rxInvoked_()) {
      if (m_linkRx.rxEarlyData.state == LinkEarlyState::Recv ||
	  m_linkRx.rxEarlyData.state == LinkEarlyState::Offered)
	acceptEarlyDataRx();
      app()->txRun([link = impl()]() mutable {
	if (link->disconnecting_()) return;
	link->acceptEarlyDataTx();
      });
      return;
    }
    if (txInvoked_()) acceptEarlyDataTx();
  }

  unsigned rejectZeroRTTTx(
    ZeroRTTReason::T reason = ZeroRTTReason::TLSRejected) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC 0-RTT rejection outside Tx thread", return 0);
    (void)reason;
    m_linkTx.txEarlyProt.clear();
    PktTxUpdate update;
    update.level = PktNumSpace::AppData;
    uint64_t releasedBytes = 0;
    unsigned n = m_linkTx.txPkts[PktNumSpace::AppData].reject(
      PktType::ZeroRTT, &releasedBytes, &update);
    if (!n) return 0;
    if (releasedBytes) {
      if (ZuUnlikely(app()->congestionControl())) m_linkTx.congestion.release(releasedBytes);
      updateCongestionDiag();
    }
    schedulePTO_();
    scheduleLossTimer();
    if (update.lostFrames) impl()->queueRetransmit();
    return n;
  }

  template <typename SendCrypto>
  bool sendCryptoPTO(
    SendCrypto sendCrypto, PktNumSpace::T onlySpace = PktNumSpace::N) {
    SentFrameRef cryptoRef;
    PktNumSpace::T cryptoLevel;
    if (!nextCryptoPTO_(cryptoLevel, cryptoRef, onlySpace)) return false;
    PktBuild build;
    if (!buildRetxCrypto(cryptoLevel, build, cryptoRef)) return false;
    if (!sendCrypto(cryptoLevel, build, cryptoRef)) return false;
    schedulePTO_();
    return true;
  }

  template <typename SendCrypto, typename FlushTx, typename SendProbe>
  void ptoRecovery(
    SendCrypto sendCrypto, FlushTx flushTx, SendProbe sendProbe) {
    ZiAssert(txInvoked_(), "Zquic", (),
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
      if (level == probeLevel || m_linkTx.txSpaceDiscarded[level] ||
	  !txSecretInstalled_(level) || !m_linkTx.txPkts[level].ackElicitingInFlight())
	continue;
      companion = level;
      break;
    }
    uint64_t primaryPN = m_linkTx.txPN[probeLevel];
    uint64_t companionPN = companion < PktNumSpace::N ? m_linkTx.txPN[companion] : 0;
    uint64_t packetsBefore = txPackets();
    unsigned budget = companion < PktNumSpace::N ? 1 : 2;
    auto finish = [this, &sendProbe, probeLevel, companion,
	primaryPN, companionPN, packetsBefore](bool primarySent) {
      if (primarySent && companion < PktNumSpace::N &&
	  m_linkTx.txPN[probeLevel] != primaryPN &&
	  m_linkTx.txPN[companion] == companionPN && txPackets() - packetsBefore < 2) {
	if (sendProbe(companion)) notePTOProbe(companion, 1);
      }
      schedulePTO_();
    };
    unsigned probeCount = 0;
    bool probe = reclaimPTO(probeLevel, probeCount, budget);
    // Candidate proof must precede recovery of unrelated old-path data.
    if (m_linkTx.migration.active &&
	m_linkTx.migration.state == MigrationState::Validating &&
	(m_config.isServer ? impl()->sendPassiveMigChal() : impl()->sendMigChal(true))) {
      schedulePTO_();
      return;
    }
    if (!probe && probeLevel <= PktNumSpace::Handshake)
      if (sendCryptoPTO(sendCrypto, probeLevel)) { finish(true); return; }
    if (impl()->retransmit_(true, budget, probeLevel)) {
      notePTORetx();
      finish(true);
      return;
    }
    if (probe && probeLevel <= PktNumSpace::Handshake)
      if (sendCryptoPTO(sendCrypto, probeLevel)) { finish(true); return; }
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

  bool reclaimPTO(PktNumSpace::T &level, unsigned &probes, unsigned limit = 2) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO reclaim outside Tx thread", return false);
    probes = 0;
    if (!ptoLevel(level)) return false;
    unsigned n = m_linkTx.txPkts[level].reclaimOnPTO(limit);
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
      ++m_linkTx.txDiag.ptoCount;
      m_linkTx.ptoBackoff.expired();
      ZquicLOG(app()->qlogTrace(), ([
	level,
	probes = n,
		backoff = m_linkTx.ptoBackoff.count(),
		cwnd = congestionWindow(),
		ssthresh = congestionSSThresh(),
		bytesInFlight = txBytesInFlight(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	RecLogEvt event{
	  .linkInfo = linkInfo,
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
      if (debugLog()) ZiLOG(Debug, "Zquic", ([level, n](auto &s) {
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
    ++m_linkTx.txDiag.ptoExpired;
    ZquicLOG(app()->qlogTrace(), ([
      level = [this]() -> PktNumSpace::T {
	PktNumSpace::T level_ = PktNumSpace::Initial;
	return ptoLevel(level_) ? level_ : PktNumSpace::N;
      }(),
      backoff = m_linkTx.ptoBackoff.count(),
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo,
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
  void notePTOFlush() { ++m_linkTx.txDiag.ptoFlush; }
  void notePTORetx() { ++m_linkTx.txDiag.ptoRetx; }
  void notePTOProbe(PktNumSpace::T level, unsigned probes) {
    ++m_linkTx.txDiag.ptoProbe;
    ZquicLOG(app()->qlogTrace(), ([
      level,
      probes,
      backoff = m_linkTx.ptoBackoff.count(),
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      RecLogEvt event{
		.linkInfo = linkInfo,
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
  uint64_t txPackets() const { return m_linkTx.txDiag.packetsTx; }

  bool nextRetransmit_(
    PktNumSpace::T &level, SentFrameRef &ref,
    PktNumSpace::T onlySpace = PktNumSpace::N) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC retransmit selection outside Tx thread", return false);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      PktNumSpace::T l = PktNumSpace::T(i);
      if (onlySpace < PktNumSpace::N && l != onlySpace) continue;
      if (m_linkTx.txSpaceDiscarded[l]) continue;
      do {
	if (!m_linkTx.txPkts[l].nextRetransmit(ref)) goto nextSpace;
	SentFrameRef tail;
	if (!clipRetransmit(l, ref, tail)) continue;
	if (tail) (void)requeueRetransmit_(l, tail);
	break;
      } while (true);
      level = l;
      ++m_linkTx.txDiag.retransmittedFrames;
      return true;
nextSpace:
      continue;
    }
    return false;
  }

  bool nextCryptoPTO_(
    PktNumSpace::T &level, SentFrameRef &ref,
    PktNumSpace::T onlySpace = PktNumSpace::N) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PTO crypto selection outside Tx thread", return false);
    for (unsigned i = 0; i <= unsigned(PktNumSpace::Handshake); ++i) {
      PktNumSpace::T l = PktNumSpace::T(i);
      if (onlySpace < PktNumSpace::N && l != onlySpace) continue;
      if (m_linkTx.txSpaceDiscarded[l] || !m_linkTx.txCryptoUnackd[l].count_()) continue;
      if (l == PktNumSpace::Handshake &&
	  !txSecretInstalled_(PktNumSpace::Handshake))
	continue;
      bool found = false;
      SentFrameRef ref_;
      (void)m_linkTx.txCryptoUnackd[l].spans(
	0, uint64_t(-1) >> 1,
	[&found, &ref_](const auto &span) {
	  ref_.kind = SentFrameKind::Crypto;
	  ref_.offset = span.key();
	  ref_.length = span.length();
	  found = ref_.length;
	  return false;
	});
      if (!found) continue;
      level = l;
      ref = ref_;
      ++m_linkTx.txDiag.retransmittedFrames;
      return true;
    }
    return false;
  }

  bool requeueRetransmit_(PktNumSpace::T level, const SentFrameRef &ref) {
    ZiAssert(txInvoked_(), "Zquic",
      (), "QUIC retransmit requeue outside Tx thread", return false);
    if (m_linkTx.txSpaceDiscarded[level]) return false;
    if (!frameStillUnackd(level, ref)) return false;
    return m_linkTx.txPkts[level].requeueRetransmit(ref);
  }

  bool buildRetxStream(PktBuild &build, SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Stream) return false;
    build.reset();
    if (!appendPendingAck(PktNumSpace::AppData, build)) return false;
    // ACK ranges and path MTU can change while this plaintext is retained.
    // Split the recovery reference without consuming flow credit again.
    int header = Pkt::shortHdrLen(
      headerCID(CIDSel::Peer), txPNLength(PktNumSpace::AppData));
    if (header < 0) return false;
    unsigned overhead = unsigned(header) + txSecret_(PktNumSpace::AppData).tagLen;
    unsigned maxUDP = m_linkTx.path.activeMaxUDP();
    if (overhead >= maxUDP || build.bytes() >= maxUDP - overhead) return false;
    unsigned available = maxUDP - overhead - build.bytes();
    int n = FrameCodec::writeStreamPrefix(
      build.scratch(), build.scratchAvail(), ref.streamID, ref.offset,
      ref.length, ref.fin);
    if (n <= 0 || unsigned(n) > available) return false;
    SentFrameRef head, tail;
    auto selected = &ref;
    if (ref.length > available - unsigned(n)) {
      unsigned length = available - unsigned(n);
      if (!length) return false;
      uint64_t split = ref.offset + length;
      head = ref;
      tail = ref;
      if (!clipStreamRef(head, ref.offset, split) ||
	  !clipStreamRef(tail, split, ref.offset + ref.length + (ref.fin ? 1 : 0)))
	return false;
      selected = &head;
      n = FrameCodec::writeStreamPrefix(
	build.scratch(), build.scratchAvail(), head.streamID, head.offset,
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
	ref.controlType == FrameType::Unknown ||
	ref.controlType == FrameType::PathResponse)
      return false;
    ControlFrame frame = controlFrame(ref);
    if (!controlStillValid_(frame)) return false;
    build.reset();
    if (!appendPendingAck(PktNumSpace::AppData, build)) return false;
    int n = frame.write(build.scratch(), build.scratchAvail());
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    if (frame.type == FrameType::PathChallenge) build.markPathValidation();
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
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC PMTUD probe send outside Tx thread", return false);
    if (!establishedTx() || m_linkTx.path.probePending()) return false;
    unsigned size = m_linkTx.path.nextProbeSize();
    bool blocked = !size || !m_linkTx.path.canSendProbe(size);
    if (ZuUnlikely(app()->congestionControl()))
      if (!blocked) blocked = !m_linkTx.congestion.canSend(size);
    if (blocked) {
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Blocked,
		size,
		reason = PathReason::Admission,
		activeMaxUDP = m_linkTx.path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo,
	  .mtu = size ? size : activeMaxUDP,
	  .kind = PathKind::PMTUD,
	  .action = PathAction::T(action),
	  .reason = PathReason::T(reason),
	  .validated = validated};

	o.logPMTUDUpdated(event, time);
      }));
      return false;
    }
    m_linkTx.path.startProbe(size);
    ZquicLOG(app()->qlogTrace(), ([
      action = PathAction::Sent,
      size,
      reason = PathReason::Probe,
      activeMaxUDP = m_linkTx.path.activeMaxUDP(),
      validated = pathValidated_(),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PathEvt event{
		.linkInfo = linkInfo,
		.mtu = size ? size : activeMaxUDP,
        .kind = PathKind::PMTUD,
        .action = PathAction::T(action),
		.reason = PathReason::T(reason),
		.validated = validated};

      o.logPMTUDUpdated(event, time);
    }));
    uint64_t runtime = m_linkTx.txRuntimeGen;
    PktBuild build;
    bool sent = buildPingProbe(build) && sendProbe(build, ZuMv(addr), size);
    if (runtime != m_linkTx.txRuntimeGen) return sent;
    if (!sent) {
      m_linkTx.path.probeLost();
      ZquicLOG(app()->qlogTrace(), ([
	action = PathAction::Lost,
		size,
		reason = PathReason::SendFail,
		activeMaxUDP = m_linkTx.path.activeMaxUDP(),
		validated = pathValidated_(),
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PathEvt event{
	  .linkInfo = linkInfo,
	  .mtu = size ? size : activeMaxUDP,
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

  bool buildRetxCrypto(
    PktNumSpace::T level, PktBuild &build, SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Crypto || m_linkTx.txSpaceDiscarded[level])
      return false;
    build.reset();
    if (!appendPendingAck(level, build)) return false;
    unsigned maxLen = cryptoRetxMax(level);
    if (build.bytes() >= maxLen) return false;
    maxLen -= build.bytes();
    unsigned length = ref.length > maxLen ? maxLen : unsigned(ref.length);
    for (;;) {
      int n = FrameCodec::writeCryptoPrefix(
	build.scratch(), build.scratchAvail(), ref.offset, length);
      if (n <= 0 || unsigned(n) >= maxLen) return false;
      unsigned avail = maxLen - unsigned(n);
      if (length > avail) {
	length = avail;
	continue;
      }
      ZuBSpan payload;
      if (!m_linkTx.txCrypto[level].txPayload(ref.offset, length, payload)) return false;
      if (payload.length() < length) { length = payload.length(); continue; }
      if (!build.commitScratch(unsigned(n)) || !build.add(payload)) return false;
      if (ref.length > length) {
	SentFrameRef tail = ref;
	tail.offset += length;
	tail.length -= length;
	(void)requeueRetransmit_(level, tail);
	ref.length = length;
      }
      return true;
    }
  }

  bool rxPktSeen(PktNumSpace::T level, uint64_t pn) const {
    return m_linkRx.rxAcks.tracker(level).contains(pn);
  }

  template <bool Finish = true>
  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan frame,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT) {
    SentFrameRef ref;
    bool ackEliciting = false;
    if (!sentFrameRef(frame, ref, ackEliciting)) return true;
    return recordTxPkt<Finish>(
      level, pn, bytes, ref, ackEliciting, false, 0, ackLevel, ackLargest,
      packetType, ecn);
  }

  template <bool Finish = true>
  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const SentFrameRef &ref, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT) {
    TxPktRefs refs;
    if (ref.kind != SentFrameKind::None) refs.add(ref);
    return recordTxPkt<Finish>(
      level, pn, bytes, refs, ackEliciting, pmtudProbe, pmtudSize,
      ackLevel, ackLargest, packetType, ecn);
  }

  template <bool Finish = true>
  bool recordTxPkt(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const TxPktRefs &refs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT) {
    if (m_linkTx.txSpaceDiscarded[level]) return true;
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
      packet.addFrame(refs[i], refs.stream(i));
    if (!m_linkTx.txPkts[level].add(packet)) return false;
    if (ZuUnlikely(app()->congestionControl()))
      if (ackEliciting) m_linkTx.path.sentECN(level, ecn);
    recordTxUnackd_(level, refs);
    if (ackEliciting) {
      if (ZuUnlikely(app()->congestionControl())) m_linkTx.congestion.sent(bytes);
      if constexpr (Finish) {
	updateCongestionDiag();
	schedulePTO_();
	scheduleLossTimer();
      }
    }
    return true;
  }

  void processAckFrame(PktNumSpace::T level, const Frame &frame) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC ACK receive processing outside Rx thread", return);
    if (!frame.ackRanges) return;
    AckSnapshot ack;
    ack.level = level;
    ack.delay = frame.value;
    ack.ecn = frame.ackECN;
    ack.ranges = frame.ackRanges;
    ack.receivedUS = nowUS();
    ack.epoch = m_linkRx.rxRuntimeGen;
    if (level != PktNumSpace::AppData || !establishedRx()) {
      app()->txRun([link = impl(), ack]() mutable {
	if (link->disconnecting_() || ack.epoch != link->m_linkTx.txRuntimeGen) return;
	link->processAckFrameTx(ack);
      });
      return;
    }
    uint64_t sequence = ++m_linkRx.rxPeerAckSeq;
    uint64_t read = m_peerAckRing.consumer.readSequence;
    const AckRange &range = ack.ranges[0];
    bool ordinary = ack.ranges.length() == 1 &&
      (!m_linkRx.rxPeerAckSeen || range.largest > m_linkRx.rxPeerAckLargest) &&
      ack.ecn.ce == m_linkRx.rxPeerAckCE;
    m_linkRx.rxPeerAckSeen = true;
    m_linkRx.rxPeerAckFirst = range.first;
    m_linkRx.rxPeerAckLargest = ack.ranges[ack.ranges.length() - 1].largest;
    m_linkRx.rxPeerAckCE = ack.ecn.ce;
    if (ordinary && read >= m_linkRx.rxPeerAckFallback && sequence - read <= AckPubSlots) {
      auto &slot = m_peerAckRing.slots[sequence % AckPubSlots];
      slot.epoch = ack.epoch;
      slot.delay = ack.delay;
      slot.receivedUS = ack.receivedUS;
      slot.range = range;
      slot.ecn = ack.ecn;
      m_peerAckRing.producer.sequence = sequence;
      // Pair with the blocked sender's store/load barrier. Release/acquire
      // alone permits both owners to miss the other's publication.
      __atomic_thread_fence(__ATOMIC_SEQ_CST);
      if (unsigned(m_peerAckRing.consumer.waiting)) requestPeerAckTx();
      return;
    }
    if (ordinary && read >= m_linkRx.rxPeerAckFallback && sequence - read > AckPubSlots)
      ++m_peerAckRing.producer.full;
    // Preserve every exceptional ACK, including ranges omitted by a later
    // ACK. Subsequent ordinary publications wait for this handoff to drain.
    m_linkRx.rxPeerAckFallback = sequence;
    app()->txRun([link = impl(), ack, sequence]() mutable {
      if (link->disconnecting_()) return;
      link->m_linkTx.peerAckPending.push(PeerAckPending{ack, sequence});
      link->drainPeerAcksTx();
    });
  }

  void requestPeerAckTx() {
    if (m_linkRx.rxPeerAckPosted &&
	uint64_t(m_peerAckRing.consumer.readSequence) < m_linkRx.rxPeerAckPosted) return;
    m_linkRx.rxPeerAckPosted = m_linkRx.rxPeerAckSeq;
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->drainPeerAcksTx();
    });
  }

  bool deferTxFlush() {
    if (m_linkTx.txSendDepth) {
      m_linkTx.txSendDeferred = true;
      return true;
    }
    if (!m_linkTx.txPeerAckDraining && !m_linkTx.txPeerAckActive) return false;
    m_linkTx.txPeerAckFlush = true;
    return true;
  }

  void finishShortSend(uint64_t runtime) {
    if (--m_linkTx.txSendDepth || !m_linkTx.txSendDeferred) return;
    m_linkTx.txSendDeferred = false;
    if (runtime != m_linkTx.txRuntimeGen || disconnecting_()) return;
    app()->txRun([link = impl(), runtime]() mutable {
      if (runtime != link->m_linkTx.txRuntimeGen || link->disconnecting_()) return;
      link->flushTx_();
    });
  }

  void drainPeerAcksTx(bool flush = true) {
    if (disconnecting_() || m_linkTx.txPeerAckDraining || m_linkTx.txPeerAckActive) return;
    m_linkTx.txPeerAckDraining = true;
    // A duplicate or invalid ACK may open no credit. The service turn must
    // still let a previously blocked sender recheck and restore its wait.
    m_linkTx.txPeerAckFlush |= bool(m_peerAckRing.consumer.waiting.load_());
    m_peerAckRing.consumer.waiting = 0;
    uint64_t available = m_peerAckRing.producer.sequence;
    uint64_t runtime = m_linkTx.txRuntimeGen;
    unsigned serviced = 0;
    while (serviced < AckPubSlots && !m_linkTx.txPeerAckActive) {
      uint64_t sequence = m_linkTx.txPeerAckSeq + 1;
      AckSnapshot ack;
      if (sequence <= available) {
	const auto &slot = m_peerAckRing.slots[sequence % AckPubSlots];
	if (slot.epoch > runtime) break;
	ack.level = PktNumSpace::AppData;
	ack.epoch = slot.epoch;
	ack.delay = slot.delay;
	ack.receivedUS = slot.receivedUS;
	ack.ecn = slot.ecn;
	ack.ranges.push(slot.range);
      } else {
	auto node = m_linkTx.peerAckPending.headPtr();
	if (!node || node->data().sequence != sequence || node->data().ack.epoch > runtime) break;
	auto pending = m_linkTx.peerAckPending.shiftVal();
	ack = ZuMv(pending.ack);
      }
      ++serviced;
      if (ack.epoch == runtime) {
	m_linkTx.txPeerAckActive = sequence;
	processAckFrameTx(ack, sequence);
      } else {
	m_linkTx.txPeerAckSeq = sequence;
      }
      if (m_linkTx.txRuntimeGen != runtime) break;
    }
    m_peerAckRing.consumer.readSequence = m_linkTx.txPeerAckSeq;
    m_linkTx.txPeerAckDraining = false;
    bool deferred = m_linkTx.txPeerAckFlush;
    m_linkTx.txPeerAckFlush = false;
    if (flush && deferred && runtime == m_linkTx.txRuntimeGen && !disconnecting_())
      impl()->flushTx_();
    if (serviced == AckPubSlots && !m_linkTx.txPeerAckActive &&
	runtime == m_linkTx.txRuntimeGen && !disconnecting_() &&
	(m_linkTx.txPeerAckSeq < available || !m_linkTx.peerAckPending.empty_()))
      app()->txRun([link = impl(), runtime] {
	if (runtime == link->m_linkTx.txRuntimeGen && !link->disconnecting_())
	  link->drainPeerAcksTx();
      });
  }

  void finishPeerAckTx(const TxAckWork &work) {
    if (!work.peerSequence || work.gen != m_linkTx.txRuntimeGen ||
	work.peerSequence != m_linkTx.txPeerAckActive) return;
    m_linkTx.txPeerAckActive = 0;
    m_linkTx.txPeerAckSeq = work.peerSequence;
    drainPeerAcksTx();
  }

  bool clearPeerAcksTx(unsigned &budget) {
    if (m_linkTx.txPeerAckActive > m_linkTx.txPeerAckSeq)
      m_linkTx.txPeerAckSeq = m_linkTx.txPeerAckActive;
    m_linkTx.txPeerAckActive = 0;
    while (budget) {
      auto node = m_linkTx.peerAckPending.shift();
      if (!node) break;
      --budget;
      if (node->data().sequence > m_linkTx.txPeerAckSeq)
	m_linkTx.txPeerAckSeq = node->data().sequence;
    }
    m_peerAckRing.consumer.readSequence = m_linkTx.txPeerAckSeq;
    return m_linkTx.peerAckPending.empty_();
  }

  bool awaitPeerAckTx() {
    m_peerAckRing.consumer.waiting = 1;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (m_linkTx.txPeerAckActive) return false;
    if (uint64_t(m_peerAckRing.producer.sequence) <= m_linkTx.txPeerAckSeq) return false;
    drainPeerAcksTx(false);
    return true;
  }

  void queueAckReap(
    ReapStreams &streams, Stream *stream) {
    if (!stream) return;
    for (unsigned i = 0, n = streams.length(); i < n; ++i)
      if (streams[i] == stream) return;
    streams.push(stream);
  }
  void reapAckedStreams(PktTxUpdate &update, ReapStreams &streams) {
    update.clearAckdFrames();
    for (unsigned i = 0, n = streams.length(); i < n; ++i)
      reapStream_0(streams[i]);
  }
  void reapStreamFromRx(const StreamRef &stream) {
    if (!stream || stream->id() < 0) return;
    int64_t id = stream->id();
    app()->txRun([link = impl(), id]() mutable {
      if (link->disconnecting_()) return;
      link->reapStreamByIDTx(id);
    });
  }
  bool reapStreamByIDTx(int64_t id) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream reap outside Tx thread", return false);
    Stream *stream = nullptr;
    {
      StreamRef streamRef = findTxStream_(id);
      stream = streamRef.ptr();
    }
    return reapStream_0(stream);
  }
  void applyECNAckTx(
    const AckSnapshot *ack, const PktTxUpdate &update, bool *ecnValid,
    uint64_t *ceDelta) {
    if (!ack || !update.ecnAckdPackets || m_linkTx.path.ecnDisabled()) return;
    if (!ack->ecn.any()) {
      if (m_linkTx.path.ecnProbeExpired() && ecnValid && *ecnValid) {
#ifdef Zquic_DEBUG
	auto oldState = m_linkTx.path.ecnState();
#endif
	m_linkTx.path.ecnValidationFailed();
	++m_linkTx.txDiag.ecnValidationFailures;
	*ecnValid = false;
	ZquicLOG(app()->qlogTrace(), ([
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
#ifdef Zquic_DEBUG
    auto oldState = m_linkTx.path.ecnState();
#endif
    m_linkTx.path.ackdECN(update.ecnAckdPackets);
#ifdef Zquic_DEBUG
    auto newState = m_linkTx.path.ecnState();
    if (oldState != newState) {
      ZquicLOG(app()->qlogTrace(), ([
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
    m_linkTx.path.ceECN(update.ecnAckdBytes);
    if (m_linkTx.congestion.congestionEventAt(
	uint64_t(update.ecnAckdSentTime.microsecs()),
	uint64_t(now_().microsecs()))) {
      *ceDelta = 0;
      ZquicLOG(app()->qlogTrace(), ([
	reason = RecReason::ECNCE,
	cwnd = congestionWindow(),
	ssthresh = congestionSSThresh(),
	bytesInFlight = txBytesInFlight(),
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
  void applyAckUpdateTx(
    const PktTxUpdate &update, TxAckWork &work,
    ReapStreams &reapStreams,
    const AckSnapshot *ack = nullptr, bool *ecnValid = nullptr,
    uint64_t *ceDelta = nullptr) {
    uint64_t workEpoch = 0;
    if (ZuUnlikely(app()->congestionControl())) workEpoch = m_linkTx.congestion.epoch();
    applyAckOfAckTx(update);
    for (unsigned i = 0, n = update.ackdFrames.length(); i < n; ++i) {
      const SentFrameUpdate &frame = update.ackdFrames[i];
      const SentFrameRef &ref = frame.ref;
      StreamRef streamRef;
      Stream *stream = static_cast<Stream *>(frame.owner);
      if ((ref.kind == SentFrameKind::Stream ||
	  ref.kind == SentFrameKind::Control) &&
	  ref.streamID <= uint64_t(INT64_MAX)) {
	streamRef = findTxStream_(int64_t(ref.streamID));
	stream = streamRef.ptr();
      }
      if (ackTxFrame(update.level, ref, stream))
	switch (ref.kind) {
	  case SentFrameKind::Stream:
	    if (ref.fin) queueAckReap(reapStreams, stream);
	    break;
	  case SentFrameKind::Control:
	    if (ref.controlType == FrameType::ResetStream)
	      queueAckReap(reapStreams, stream);
	    break;
	  default:
	    break;
	}
    }
    work.retiredData |= bool(update.ackdFrames);
    if (ZuUnlikely(app()->congestionControl())) {
      applyECNAckTx(ack, update, ecnValid, ceDelta);
      work.growth.stage(m_linkTx.congestion, workEpoch, update.eligibleAckdBytes);
    }
    if (ZuUnlikely(app()->congestionControl()) && update.normalAckdBytes) {
      m_linkTx.congestion.release(update.normalAckdBytes);
      work.congestionOpened = true;
      ZquicLOG(app()->qlogTrace(), ([
      reason = RecReason::Ack,
      cwnd = congestionWindow(),
      ssthresh = congestionSSThresh(),
      bytesInFlight = txBytesInFlight(),
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
    if (update.pmtudAckdBytes) {
      if (ZuUnlikely(app()->congestionControl())) {
	m_linkTx.congestion.release(update.pmtudAckdBytes);
	work.congestionOpened = true;
      }
      onPMTUDProbeAckd(update.pmtudAckdSize);
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDAck,
		cwnd = congestionWindow(),
		ssthresh = congestionSSThresh(),
		bytesInFlight = txBytesInFlight(),
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
  void applyAckOfAckTx(const PktTxUpdate &update) {
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      if (!update.ackdAck[i]) continue;
      uint64_t frontier = update.ackLargest[i] + 1;
      if (frontier <= m_linkTx.txAckRetired[i]) continue;
      m_linkTx.txAckRetired[i] = frontier;
      m_ackCompletion[i].retireFrontier = frontier;
    }
  }
  void applyLossUpdateTx(const PktTxUpdate &update) {
    if (ZuUnlikely(app()->congestionControl()) && update.normalLostBytes) {
      m_linkTx.congestion.lostAt(
	update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()),
	uint64_t(now_().microsecs()));
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::Loss,
		cwnd = congestionWindow(),
		ssthresh = congestionSSThresh(),
		bytesInFlight = txBytesInFlight(),
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
    if (update.pmtudLostBytes) {
      if (ZuUnlikely(app()->congestionControl())) m_linkTx.congestion.lostAt(
	update.pmtudLostBytes,
	uint64_t(update.pmtudLostSentTime.microsecs()),
	uint64_t(now_().microsecs()), true);
      onPMTUDProbeLost(update.pmtudLostSize);
      ZquicLOG(app()->qlogTrace(), ([
		reason = RecReason::PMTUDLoss,
		cwnd = congestionWindow(),
		ssthresh = congestionSSThresh(),
		bytesInFlight = txBytesInFlight(),
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

  void processAckFrameTx(const AckSnapshot &ack, uint64_t peerSequence = 0) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    TxAckWork work;
    work.ack = ack;
    work.gen = m_linkTx.txRuntimeGen;
    work.peerSequence = peerSequence;
    processAckFrameTx(ZuMv(work));
  }
  void processAckFrameTx(TxAckWork work) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC ACK sent-packet processing outside Tx thread", return);
    AckSnapshot &ack = work.ack;
    if (disconnecting_() || work.gen != m_linkTx.txRuntimeGen) return;
    if (work.peerSequence && work.peerSequence != m_linkTx.txPeerAckActive) return;
    if (!ack.ranges || m_linkTx.txSpaceDiscarded[ack.level]) {
      finishPeerAckTx(work);
      return;
    }
    if (!ackFrameValidTx(ack)) {
      ++m_linkTx.txDiag.failures;
      finishPeerAckTx(work);
      return;
    }
    if (!work.ecnValidated) {
      if (ZuUnlikely(app()->congestionControl()))
	work.ecnValid = validateAckECN(ack, &work.ceDelta);
      for (unsigned i = 0, n = ack.ranges.length(); i < n; ++i) {
	uint64_t largest = ack.ranges[i].largest;
	if (ZuNull(m_linkTx.txLargestAckd[ack.level]) ||
	    largest > m_linkTx.txLargestAckd[ack.level])
	  m_linkTx.txLargestAckd[ack.level] = largest;
      }
      work.ecnValidated = true;
    }
	    uint64_t recoveryStart = 0;
	    if (ZuUnlikely(app()->congestionControl()))
	      recoveryStart = m_linkTx.congestion.recoveryStartTime();
	    PktTxUpdate update;
	    ReapStreams reapStreams;
	    if (!work.lossPhase) {
	      if (!m_linkTx.txPkts[ack.level].ackBatch(
		  ack.ranges.data(), ack.ranges.length(),
		  work.ackBatch, RecoveryScanBatch,
		  ack.level, &update, recoveryStart)) {
		applyAckUpdateTx(
		  update, work, reapStreams,
		  &ack, &work.ecnValid, &work.ceDelta);
		ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  largestAcked = ack.ranges ?
	    ack.ranges[ack.ranges.length() - 1].largest : 0,
	  ackDelayUS = ack.level == PktNumSpace::AppData ?
    qlogUS(decodeAckDelay(ack.delay)) : 0,
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
	    .linkInfo = linkInfo,
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
		reapAckedStreams(update, reapStreams);
		app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
		  if (link->disconnecting_()) return;
		  link->processAckFrameTx(ZuMv(work));
		});
		return;
	      }
	      applyAckUpdateTx(
		update, work, reapStreams,
		&ack, &work.ecnValid, &work.ceDelta);
	      ZquicLOG(app()->qlogTrace(), ([
	level = ack.level,
	largestAcked = ack.ranges ?
	  ack.ranges[ack.ranges.length() - 1].largest : 0,
	ackDelayUS = ack.level == PktNumSpace::AppData ?
	  qlogUS(decodeAckDelay(ack.delay)) : 0,
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
	  .linkInfo = linkInfo,
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
	      reapAckedStreams(update, reapStreams);
	      work.lossPhase = true;
	    } else {
	      applyAckUpdateTx(
		update, work, reapStreams,
		&ack, &work.ecnValid, &work.ceDelta);
	      reapAckedStreams(update, reapStreams);
	    }
    if (work.ackBatch.haveAckForLoss) {
      PktTxUpdate lossUpdate;
      if (!m_linkTx.txPkts[ack.level].markPktThreshLossBatch(
	  work.ackBatch.largestAckdForLoss, 3, work.lossBatch,
	  RecoveryScanBatch, &lossUpdate)) {
	applyLossUpdateTx(lossUpdate);
	ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  lostBytes = lossUpdate.lostBytes,
	  bytesInFlight = txBytesInFlight(),
	  lostFrames =
	    QLogTxFrameRefs{lossUpdate.lostFrames},
	  reason = RecReason::PacketThreshold,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  if (!lostBytes) return;
	  RecLogEvt event{
	    .linkInfo = linkInfo,
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
	  link->processAckFrameTx(ZuMv(work));
	});
	return;
      }
      applyLossUpdateTx(lossUpdate);
      ZquicLOG(app()->qlogTrace(), ([
	level = ack.level,
	lostBytes = lossUpdate.lostBytes,
		bytesInFlight = txBytesInFlight(),
		lostFrames =
		  QLogTxFrameRefs{lossUpdate.lostFrames},
		reason = RecReason::PacketThreshold,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	if (!lostBytes) return;
	RecLogEvt event{
	  .linkInfo = linkInfo,
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
    if (ZuUnlikely(app()->congestionControl()) && work.lossBatch.lost) {
      if (m_linkTx.txPkts[ack.level].persistentCongestion(
	  persistentCongestionThresh(), RecoveryScanBatch))
	persistentCongestion_();
    }
    if (work.ackBatch.ackd) {
      ZuTime now = ack.receivedUS ? intervalUS(ack.receivedUS) : now_();
      if (*work.ackBatch.latestSentTime && work.ackBatch.latestSentTime < now) {
	m_linkTx.rtt.sample(
	  now - work.ackBatch.latestSentTime,
	  ack.level == PktNumSpace::AppData ? decodeAckDelay(ack.delay) : ZuTime{0},
	  ack.level == PktNumSpace::AppData);
	ZquicLOG(app()->qlogTrace(), ([
	  level = ack.level,
	  latestRTTUS = qlogUS(m_linkTx.rtt.latest()),
	  smoothedRTTUS = qlogUS(m_linkTx.rtt.smoothed()),
	  rttVarianceUS = qlogUS(m_linkTx.rtt.variance()),
	  minRTTUS = qlogUS(m_linkTx.rtt.min()),
	  cwnd = congestionWindow(),
	  ssthresh = congestionSSThresh(),
	  bytesInFlight = txBytesInFlight(),
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  RecLogEvt event{
	    .linkInfo = linkInfo,
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
      m_linkTx.ptoBackoff.reset();
    }
    // Commit only after this ACK's ECN and loss responses, including all
    // bounded continuations. Reset/discard return above without growth.
    if (ZuUnlikely(app()->congestionControl())) work.growth.commit(m_linkTx.congestion);
    updateCongestionDiag();
    schedulePTO_();
    scheduleLossTimer();
    bool retransmitted = false;
    unsigned allowance = congestionAllowance_();
    if (work.retiredData || work.congestionOpened || allowance) {
      // Spend ACK-reopened credit on retained loss before ordinary producers.
      // Posting recovery after a synchronous bulk flush can starve it while
      // new data keeps resetting PTO. Include losses queued by earlier ACKs.
      if (allowance)
	for (unsigned i = 0; i < PktNumSpace::N; ++i)
	  if (m_linkTx.txPkts[i].retransmitPending()) {
	    retransmitted = impl()->retransmit_(); // existing bounded batch
	    break;
	  }
      impl()->flushTx_();
    }
    if (work.retransmit && !retransmitted) impl()->queueRetransmit();
    finishPeerAckTx(work);
  }

  bool ackFrameValidTx(const AckSnapshot &ack) const {
    for (unsigned i = 0, n = ack.ranges.length(); i < n; ++i)
      if (ack.ranges[i].largest >= m_linkTx.txPN[ack.level]) return false;
    return true;
  }

  bool validateAckECN(const AckSnapshot &ack, uint64_t *ceDelta = nullptr) {
    if (!ack.ecn.any()) return true;
    const AckECN &sent = m_linkTx.path.sentECN(ack.level);
    if (!sent.any()) return true;
    const AckECN &last = m_linkTx.path.peerAckECN(ack.level);
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
#ifdef Zquic_DEBUG
      auto oldState = m_linkTx.path.ecnState();
      uint64_t largest = 0;
      if (ack.ranges)
	largest = ack.ranges[ack.ranges.length() - 1].largest;
#endif
      m_linkTx.path.ecnValidationFailed();
      ++m_linkTx.txDiag.ecnValidationFailures;
      ZquicLOG(app()->qlogTrace(), ([
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
	  .linkInfo = linkInfo,
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
    m_linkTx.path.peerAckECN(ack.level) = ack.ecn;
    m_linkTx.txDiag.peerAckECN[ack.level] = ack.ecn;
    return true;
  }

  static ZuTime now_() { return Zm::now(); }
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
  const TransportParams *peerParams_() const {
    if (app()->txInvoked())
      return m_linkTx.peerParamsSet ? &m_linkTx.peerParams : nullptr;
    return m_linkRx.crypto.peerParamsSet() ? &m_linkRx.crypto.peerParams() : nullptr;
  }
  ZuTime peerMaxAckDelay() const {
    auto params = peerParams_();
    return params ? intervalMS(params->maxAckDelay) : ZuTime{0};
  }
  static ZuTime transportParamMS(uint64_t ms) {
    if (!ms) return ZuTime{0};
    return intervalMS(ms);
  }
  ZuTime negotiatedIdleTimeout() const {
    uint64_t local = m_linkRx.transportParams.maxIdleTimeout;
    uint64_t peer = m_linkRx.crypto.peerParamsSet() ?
      m_linkRx.crypto.peerParams().maxIdleTimeout : 0;
    if (!local) return transportParamMS(peer);
    if (!peer) return transportParamMS(local);
    return transportParamMS(local < peer ? local : peer);
  }
  ZuTime decodeAckDelay(uint64_t delay) const {
    auto peer = peerParams_();
    if (!peer) return ZuTime{0};
    const auto &params = *peer;
    ZuTime maxAckDelay = peerMaxAckDelay();
    uint64_t maxAckUsec = uint64_t(maxAckDelay.microsecs());
    if (delay > (maxAckUsec >> params.ackDelayExponent))
      return maxAckDelay;
    delay <<= params.ackDelayExponent;
    ZuTime ackDelay = intervalUS(delay);
    return ackDelay < maxAckDelay ? ackDelay : maxAckDelay;
  }
  ZuTime ptoTimeout() const {
    return m_linkTx.ptoBackoff.timeout(m_linkTx.rtt, peerMaxAckDelay());
  }
  ZuTime closeDrainDelay() const {
    return ptoTimeout() * ZuDecimal{3};
  }
  ZuTime keyDiscardDeadline() const {
    return now_() + closeDrainDelay();
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

  template <typename SendPkt>
  bool holdInitialForCoalesce(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_linkTx.coalesceLong || m_linkTx.coalesceInitial)
      return sendPkt(ZuMv(buf), ZuMv(addr));
    ZquicLOG(app()->qlogTrace(), ([
      packetBytes = buf->length,
      reason = Reason::Coalescing,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      PktEvt event{
	.packetSize = packetBytes
      };
      event.packetType = PktType::Initial;
      event.packetSpace = PktNumSpace::Initial;
      event.reason = reason;
      event.linkInfo = linkInfo;
      o.logPktBuf(event, time);
    }));
    m_linkTx.coalesceInitial = ZuMv(buf);
    m_linkTx.coalesceAddr = ZuMv(addr);
    return true;
  }

  template <typename SendPkt>
  bool sendHandshakeCoalesced(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, SendPkt sendPkt) {
    if (!m_linkTx.coalesceInitial) return sendPkt(ZuMv(buf), ZuMv(addr));
    ZmRef<ZiIOBuf> initial = ZuMv(m_linkTx.coalesceInitial);
    ZiSockAddr initialAddr = ZuMv(m_linkTx.coalesceAddr);
    if (initial->avail() >= buf->length) {
      if (!initial->append(buf->cspan())) return false;
      return sendPkt(ZuMv(initial), ZuMv(initialAddr));
    }
    if (!sendPkt(ZuMv(initial), ZuMv(initialAddr))) return false;
    return sendPkt(ZuMv(buf), ZuMv(addr));
  }

  template <typename SendPkt>
  bool flushCoalescedInitial(SendPkt sendPkt) {
    if (!m_linkTx.coalesceInitial) return true;
    ZmRef<ZiIOBuf> initial = ZuMv(m_linkTx.coalesceInitial);
    ZiSockAddr addr = ZuMv(m_linkTx.coalesceAddr);
    return sendPkt(ZuMv(initial), ZuMv(addr));
  }
  void beginLongCoalesce() { m_linkTx.coalesceLong = true; }
  void endLongCoalesce() { m_linkTx.coalesceLong = false; }

  template <typename SendCryptoPkt>
  bool sendCryptoFlights_(
    ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
    unsigned chunkMax, ZiSockAddr addr, SendCryptoPkt sendCryptoPkt) {
    uint64_t runtime = m_linkTx.txRuntimeGen;
    return Zquic::sendCryptoFlights_(
      m_linkTx.txCrypto, m_linkTx.txDiag,
      ZuMv(data), offsets, chunkMax, ZuMv(addr),
      [this, runtime, sendCryptoPkt](
	  PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) mutable {
	bool sent = sendCryptoPkt(
	  level, prefix, payload, ref, ZuMv(addr_));
	return runtime == m_linkTx.txRuntimeGen && sent;
      },
      [this, runtime](PktNumSpace::T level, const SentFrameRef &ref) {
	// Retain unsent TLS bytes for every blocked output path, including
	// amplification/congestion credit and finite packet providers.
	if (runtime != m_linkTx.txRuntimeGen || disconnecting_() ||
	    m_linkTx.txSpaceDiscarded[level] || !recordTxUnackd_(level, ref)) return false;
	(void)requeueRetransmit_(level, ref); // an existing dedup entry already retains it
	return true;
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

  void noteTxControlFrame(FrameType::T type) {
    switch (type) {
      case FrameType::MaxData: ++m_linkTx.txDiag.maxDataTx; break;
      case FrameType::MaxStreamData: ++m_linkTx.txDiag.maxStreamDataTx; break;
      case FrameType::MaxStreams: ++m_linkTx.txDiag.maxStreamsTx; break;
      case FrameType::DataBlocked: ++m_linkTx.txDiag.dataBlockedTx; break;
      case FrameType::StreamDataBlocked: ++m_linkTx.txDiag.streamDataBlockedTx; break;
      case FrameType::StreamsBlocked: ++m_linkTx.txDiag.streamsBlockedTx; break;
      case FrameType::ResetStream: ++m_linkTx.txDiag.resetStreamTx; break;
      case FrameType::StopSending: ++m_linkTx.txDiag.stopSendingTx; break;
      case FrameType::PathChallenge: ++m_linkTx.txDiag.pathChallengeTx; break;
      case FrameType::PathResponse: ++m_linkTx.txDiag.pathResponseTx; break;
      case FrameType::HandshakeDone: ++m_linkTx.txDiag.handshakeDoneTx; break;
      case FrameType::NewCxnID: ++m_linkTx.txDiag.newCxnIDTx; break;
      default: break;
    }
  }

  void noteTxPktDiag(const TxPktRefs *refs, uint8_t ackLevel) {
    bool hasAck = ackLevel < PktNumSpace::N;
    bool hasStream = false;
    bool hasControl = false;
    bool hasCrypto = false;
    if (refs) {
      for (unsigned i = 0, n = refs->count(); i < n; ++i) {
	const SentFrameRef &ref = (*refs)[i];
	switch (ref.kind) {
	  case SentFrameKind::Stream:
    ++m_linkTx.txDiag.streamFramesTx;
    hasStream = true;
    break;
	  case SentFrameKind::Control:
    ++m_linkTx.txDiag.controlFramesTx;
    hasControl = true;
    noteTxControlFrame(ref.controlType);
    break;
	  case SentFrameKind::Crypto:
    ++m_linkTx.txDiag.cryptoFramesTx;
    hasCrypto = true;
    break;
	  default:
    break;
	}
      }
    }
    if (hasStream && hasControl && hasAck)
      ++m_linkTx.txDiag.ackStreamCtlPktsTx;
    else if (hasStream && hasControl)
      ++m_linkTx.txDiag.streamCtlPktsTx;
    else if (hasStream && hasAck)
      ++m_linkTx.txDiag.ackStreamPacketsTx;
    else if (hasControl && hasAck)
      ++m_linkTx.txDiag.ackCtlPktsTx;
    else if (hasStream)
      ++m_linkTx.txDiag.streamOnlyPacketsTx;
    else if (hasControl)
      ++m_linkTx.txDiag.ctlOnlyPktsTx;
    else if (hasAck)
      ++m_linkTx.txDiag.ackOnlyPacketsTx;
    else if (hasCrypto)
      ++m_linkTx.txDiag.cryptoPacketsTx;
    else
      ++m_linkTx.txDiag.otherPacketsTx;
  }

  bool prepareProtPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT) {
    bool recorded = recordRefs ?
      recordTxPkt<false>(
	level, pn, bytes, *recordRefs, ackEliciting, pmtudProbe, pmtudSize,
	ackLevel, ackLargest, packetType, ecn) :
      recordTxPkt<false>(
	level, pn, bytes, recordFrame, ackLevel, ackLargest, packetType, ecn);
    if (!recorded) return false;
    ++m_linkTx.txPN[level];
    return true;
  }

  void finishProtPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes,
    const TxPktRefs *recordRefs, bool ackEliciting,
    uint8_t ackLevel, uint64_t ackLargest, PktType::T packetType,
    EcnMark::T ecn, const PktBuild *payload = nullptr) {
    m_linkTx.txDiag.packetsTx += 1;
    m_linkTx.txDiag.bytesTx += bytes;
    noteTxPktDiag(recordRefs, ackLevel);
    ZquicLOG(app()->qlogTrace(), ([
      level, pn,
      packetBytes = bytes,
      bytesInFlight = txBytesInFlight(),
      ackEliciting,
      ackLevel, ackLargest,
      ackRanges = qlogAckRanges(ackLevel, payload),
      packetType,
      ecn,
      hasRecordRefs = bool(recordRefs),
      recordRefs = recordRefs ? *recordRefs : TxPktRefs{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      unsigned frameCount = hasRecordRefs ? recordRefs.count() : 0;
      if (ackLevel < PktNumSpace::N) ++frameCount;
      PktEvt event{
		.linkInfo = linkInfo,
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
      updateCongestionDiag();
      schedulePTO_();
      scheduleLossTimer();
    }
  }

  template <bool Finish = true>
  bool recordProtPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    bool pmtudProbe = false, unsigned pmtudSize = 0,
    uint8_t ackLevel = PktNumSpace::N, uint64_t ackLargest = 0,
    PktType::T packetType = PktType::N,
    EcnMark::T ecn = EcnMark::NotECT) {
    if (!prepareProtPktTx(
	level, pn, bytes, recordFrame, recordRefs, ackEliciting,
	pmtudProbe, pmtudSize, ackLevel, ackLargest, packetType, ecn))
      return false;
    if constexpr (Finish)
      finishProtPktTx(level, pn, bytes, recordRefs, ackEliciting,
	ackLevel, ackLargest, packetType, ecn);
    return true;
  }

  void discardProtPktTx(
    PktNumSpace::T level, uint64_t pn, unsigned bytes, bool ackEliciting) {
    m_linkTx.txPkts[level].discard(pn);
    // A failed send retires only this packet; its data still needs an ACK.
    if (ackEliciting) {
      if (ZuUnlikely(app()->congestionControl())) m_linkTx.congestion.release(bytes);
      updateCongestionDiag();
    }
  }

  static uint64_t qlogUS(ZuTime t) {
    return uint64_t(t.microsecs());
  }

  static uint8_t qlogCount(unsigned n) {
    return n > 255 ? 255 : uint8_t(n);
  }

  static void qlogAddAckRange(
    ZquicLog_::FrameEvt &frame, uint64_t first, uint64_t largest) {
    if (frame.ackRanges.length() >= ZquicLog_::AckRangeMax) return;
    new (frame.ackRanges.push()) ZquicLog_::QAckRange{
      .first = first,
      .largest = largest
    };
  }

  static void qlogAddFrame(
    ZquicLog_::PktEvt &event, const ZquicLog_::FrameEvt &frame) {
    if (event.frames.length() >= ZquicLog_::FrameMax) {
      event.framesTruncated = true;
      return;
    }
    event.frames.push(frame);
  }

  static void qlogAddFrame(
    ZquicLog_::RecLogEvt &event, const ZquicLog_::FrameEvt &frame) {
    if (event.frames.length() < ZquicLog_::FrameMax)
      event.frames.push(frame);
  }

  static ZuArray<uint64_t, ZquicLog_::AckPacketMax> qlogAckedPNs(
    const PktTxUpdate &update) {
    ZuArray<uint64_t, ZquicLog_::AckPacketMax> packetNumbers;
    for (unsigned i = 0; i < update.nAckedPNs; ++i)
      new (packetNumbers.push()) uint64_t(update.ackedPNs[i]);
    return packetNumbers;
  }

  ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax> qlogAckRanges(
    uint8_t ackLevel, const PktBuild *payload = nullptr) const {
    ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax> ranges;
    if (ackLevel >= PktNumSpace::N) return ranges;
    Frame encoded;
    unsigned used = 0;
    if (payload) {
      if (!payload->count()) return ranges;
      const auto &part = payload->data()[0];
      if (FrameCodec::parse(part, encoded, used) ||
	  !encoded.ackRanges) return ranges;
    }
    const auto &ackRanges = payload ? encoded.ackRanges : m_linkTx.txAck[ackLevel].ranges;
    for (unsigned i = 0, n = ackRanges.length(); i < n; ++i) {
      if (ranges.length() >= ZquicLog_::AckRangeMax) break;
      new (ranges.push()) ZquicLog_::QAckRange{
	.first = ackRanges[i].first,
	.largest = ackRanges[i].largest
      };
    }
    return ranges;
  }

  static void qlogAddAckFrame(
    ZquicLog_::PktEvt &event, uint8_t ackLevel, uint64_t ackLargest,
    const ZuArray<ZquicLog_::QAckRange, ZquicLog_::AckRangeMax>
      &ackRanges) {
    if (ackLevel >= PktNumSpace::N) return;
    ZquicLog_::FrameEvt frame{
      .largestAcked = ackLargest,
      .type = FrameType::Ack,
      .rangeCount = qlogCount(ackRanges.length())
    };
    for (unsigned i = 0, n = ackRanges.length(); i < n; ++i)
      qlogAddAckRange(frame, ackRanges[i].first, ackRanges[i].largest);
    qlogAddFrame(event, frame);
  }

  void qlogAddRxFrame(
    ZquicLog_::PktEvt &event, PktNumSpace::T level,
    const Frame &frame_) {
    event.frameCount = qlogCount(event.frameCount + 1);

    ZquicLog_::FrameEvt frame{.type = frame_.type};
    switch (frame_.type) {
      case FrameType::Ack:
	frame.largestAcked = frame_.offset;
	frame.ackDelayUS =
	  level == PktNumSpace::AppData ? qlogUS(decodeAckDelay(frame_.value)) : 0;
	frame.rangeCount = qlogCount(frame_.ackRanges.length());
	for (unsigned i = 0, n = frame_.ackRanges.length(); i < n; ++i)
	  qlogAddAckRange(
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
    qlogAddFrame(event, frame);
  }

  template <typename Event>
  static void qlogAddTxFrame(Event &event, const SentFrameRef &ref) {
    switch (ref.kind) {
      case SentFrameKind::Stream:
	qlogAddFrame(event, ZquicLog_::FrameEvt{
	  .streamID = ref.streamID,
	  .offset = ref.offset,
	  .length = ref.length,
	  .type = FrameType::Stream,
	  .fin = ref.fin
	});
	return;
      case SentFrameKind::Crypto:
	qlogAddFrame(event, ZquicLog_::FrameEvt{
	  .offset = ref.offset,
	  .length = ref.length,
	  .type = FrameType::Crypto
	});
	return;
      case SentFrameKind::Control:
	qlogAddFrame(event, ZquicLog_::FrameEvt{
	  .cxnID = ref.cxnID,
	  .streamID = ref.streamID,
	  .offset = ref.offset,
	  .length = ref.length,
	  .value = ref.value,
	  .errorCode =
    ref.controlType == FrameType::ResetStream ||
    ref.controlType == FrameType::StopSending ? ref.value : 0,
	  .type = ref.controlType,
	  .streamType = ZquicLog_::StreamType::T(ref.streamType)
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
    ackLevel = uint8_t(level);
    ackLargest = payload.ackLargest();
    return true;
  }

  const auto &initialKeys_(InitialKeyDir::T dir) const {
    return dir == InitialKeyDir::Client ?
      m_linkRx.crypto.initialKeys().client :
      m_linkRx.crypto.initialKeys().server;
  }

public:
  // Optional adapter callbacks; only concrete endpoints allocate wire packets.
  Zquic_::PktAllocTarget pktAllocTarget() const { return {}; }
  void pktPressureResume() { }
  void waitPktTx(Zquic_::EndpointTxWait &wait, uint64_t epoch, Zquic_::EndpointTxFn fn) {
    wait.start(app()->mx(), app()->txThread(), epoch, ZuMv(fn));
    app()->mx()->txRun([wait = &wait] { wait->reject(); });
  }
  void pktAllocFailed() {
    ++m_linkTx.txDiag.failures;
    closeStateTx(TransportError::InternalError);
    impl()->closeExpired();
  }

protected:
  bool pktAllocationPending() const {
    return m_pressure.pending ||
      (m_capacity.needed && m_capacity.wait.busy());
  }
  ZmRef<ZiIOBuf> allocPktTx() {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC packet allocation outside Tx thread", return nullptr);
    if (m_pressure.pending) { m_pressure.restart = true; return nullptr; }
    if (m_capacity.needed && m_capacity.wait.busy() && !m_capacity.token) return nullptr;
    auto target = impl()->pktAllocTarget();
    if (!target.allocator) return nullptr;
    auto buf = target.allocator->allocBuf(target.owner, 0, target.mps);
    if (ZuUnlikely(!buf)) impl()->pktAllocFailed();
    return buf;
  }

  template <typename Send> bool submitPktTx(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool priority, Send send) {
    if (m_pressure.pending) { m_pressure.restart = true; return false; }
    uint64_t runtime = m_linkTx.txRuntimeGen;
    auto token = m_capacity.token;
    m_capacity.token = {};
    bool blocked = false;
    bool sent = send(ZuMv(buf), ZuMv(addr), ecn, priority, token, &blocked);
    if (runtime != m_linkTx.txRuntimeGen) return sent;
    if (!sent && token) cancelCapacityTx();
    if (blocked) {
      m_capacity.needed = true;
      requestCapacityTx();
    }
    return sent;
  }

  void requestCapacityTx() {
    if (!m_capacity.needed || m_capacity.wait.busy() ||
	m_pressure.pending || disconnecting_() || linkClosedTx_()) return;
    m_capacity.runtime = m_linkTx.txRuntimeGen;
    m_capacity.target = impl()->pktAllocTarget();
    ++m_capacity.sequence;
    impl()->waitPktTx(m_capacity.wait, m_capacity.runtime,
      Zquic_::EndpointTxFn{impl(), [](Impl *link, int result, Zquic_::EndpointTxToken token) {
	link->capacityEvent(result, token);
      }});
  }

  void capacityEvent(int result, Zquic_::EndpointTxToken token) {
    if (result != Zquic_::EndpointTxResult::Ready) {
      m_capacity.token = {};
      if (result == Zquic_::EndpointTxResult::Cancelled) {
	++m_capacity.sequence;
	if (!m_capacity.cancelling) m_capacity.needed = false;
      } else if (!m_capacity.cancelling) requestCapacityTx();
      return;
    }
    if (m_capacity.cancelling || m_pressure.pending || disconnecting_() || linkClosedTx_() ||
	m_capacity.runtime != m_linkTx.txRuntimeGen ||
	!(m_capacity.target == impl()->pktAllocTarget())) {
      cancelCapacityTx();
      return;
    }
    m_capacity.token = token;
    m_capacity.needed = false;
    app()->txRun([link = impl(), sequence = m_capacity.sequence, runtime = m_capacity.runtime] {
      if (sequence != link->m_capacity.sequence || runtime != link->txRuntime() ||
	  link->disconnecting_() || link->linkClosedTx_() || link->m_pressure.pending) return;
      link->pktPressureResume();
      if (sequence == link->m_capacity.sequence && link->m_capacity.token)
	link->cancelCapacityTx(); // no eligible output used the reservation
    });
  }

  void cancelCapacityTx(Zquic_::EndpointDiscFn done = {}) {
    ++m_capacity.sequence;
    m_capacity.token = {};
    m_capacity.needed = false;
    if (m_capacity.cancelling) {
      if (done) {
	if (m_capacity.done)
	  m_capacity.done = [first = ZuMv(m_capacity.done), done = ZuMv(done)]() mutable {
	    first(); done();
	  };
	else m_capacity.done = ZuMv(done);
      }
      return;
    }
    if (!m_capacity.wait.busy()) {
      if (done) app()->txRun([done = ZuMv(done)]() mutable { done(); });
      return;
    }
    m_capacity.cancelling = true;
    m_capacity.done = ZuMv(done);
    m_capacity.wait.cancel([link = impl()] {
      link->m_capacity.cancelling = false;
      auto done = ZuMv(link->m_capacity.done);
      link->requestCapacityTx();
      if (done) done();
    });
  }

  void cancelPktWait(Zquic_::EndpointDiscFn done = {}) {
    ++m_capacity.sequence;
    m_capacity.token = {};
    m_capacity.needed = false;
    m_pressure.restart = false;
    if (done) {
      if (m_pressure.done)
	m_pressure.done = [first = ZuMv(m_pressure.done), done = ZuMv(done)]() mutable {
	  first(); done();
	};
      else m_pressure.done = ZuMv(done);
    }
    if (m_pressure.pending) return;
    if (!m_capacity.wait.busy() && !m_pressure.done) return;
    m_pressure.pending = 1;
    cancelCapacityTx([link = impl()] { link->pressureCancelled(); });
  }

  void pressureCancelled() {
    if (--m_pressure.pending) return;
    auto done = ZuMv(m_pressure.done);
    bool restart = m_pressure.restart;
    m_pressure.restart = false;
    if (!done && restart && !disconnecting_() && !linkClosedTx_()) impl()->pktPressureResume();
    if (done) done();
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtInitialPkt(
    InitialKeyDir::T, CIDSel::T dcid, CIDSel::T scid,
    unsigned pnLength, ZuBSpan token, bool padInitial, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Initial packet protection outside Tx thread", return false);
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    if (ZuUnlikely(!buf)) return false;
    unsigned capacity = txPktCapacity(buf);
    const auto &initialKeys = m_linkTx.initialSecret;
    int headerLen = -1;
    unsigned payloadPlainLen = payload.bytes();
    unsigned targetPlainLen = payloadPlainLen;
    for (unsigned i = 0; i < 4; ++i) {
      headerLen = Pkt::writeInitial(
	buf->data(), capacity, headerCID(dcid), headerCID(scid),
	token, targetPlainLen + InitialSecret::TagLen, pnLength);
      if (headerLen < 0) return false;
      if (!padInitial) break;
      unsigned overhead =
	unsigned(headerLen) + pnLength + InitialSecret::TagLen;
      unsigned minPlainLen =
	overhead < MinUDPPayload ? MinUDPPayload - overhead : 0;
      unsigned nextPlainLen = minPlainLen > payloadPlainLen ?
	minPlainLen : payloadPlainLen;
      if (nextPlainLen == targetPlainLen) break;
      targetPlainLen = nextPlainLen;
    }
    if (padInitial && !payload.padTo(targetPlainLen)) return false;
    if (PktNumber::encode(
	  buf->data() + headerLen, capacity - unsigned(headerLen),
	  m_linkTx.txPN[PktNumSpace::Initial], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_linkTx.txPN[PktNumSpace::Initial];
    int n = InitialPktProt::protectLongV(
      buf->data(), capacity, initialKeys, pn,
      byteSpan(buf->data(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Initial,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(PktNumSpace::Initial, payload, ackLevel, ackLargest);
    bool pktAckEliciting =
      txAckEliciting(recordFrame, recordRefs, ackEliciting);
    if (!recordProtPktTx(
	  PktNumSpace::Initial, pn, unsigned(n), recordFrame, recordRefs,
	  pktAckEliciting, false, 0, ackLevel, ackLargest))
      return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
      discardProtPktTx(
	PktNumSpace::Initial, pn, unsigned(n), pktAckEliciting);
      return false;
    }
    if (payload.ack(PktNumSpace::Initial))
      ackSentTx(PktNumSpace::Initial, payload.ackGen());
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtHandshakePkt(
    CIDSel::T dcid, CIDSel::T scid, unsigned pnLength,
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Handshake packet protection outside Tx thread", return false);
    if (!txSecretInstalled_(PktNumSpace::Handshake)) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Handshake,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    if (ZuUnlikely(!buf)) return false;
    unsigned capacity = txPktCapacity(buf);
    if (!payload.padForProtSample(
	  pnLength, txSecret_(PktNumSpace::Handshake).tagLen))
      return false;
    int headerLen = Pkt::writeHandshake(
      buf->data(), capacity, headerCID(dcid), headerCID(scid),
      payload.bytes() +
	txSecret_(PktNumSpace::Handshake).tagLen,
      pnLength);
    if (headerLen < 0)
      return false;
    if (PktNumber::encode(
	  buf->data() + headerLen, capacity - unsigned(headerLen),
	  m_linkTx.txPN[PktNumSpace::Handshake], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_linkTx.txPN[PktNumSpace::Handshake];
    int n = PktProt::protectLongV(
      buf->data(), capacity,
      txProtState_(PktNumSpace::Handshake), pn,
      byteSpan(buf->data(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Handshake,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(PktNumSpace::Handshake, payload, ackLevel, ackLargest);
    bool pktAckEliciting =
      txAckEliciting(recordFrame, recordRefs, ackEliciting);
    if (!recordProtPktTx(
	  PktNumSpace::Handshake, pn, unsigned(n), recordFrame, recordRefs,
	  pktAckEliciting, false, 0, ackLevel, ackLargest))
      return false;
    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
      discardProtPktTx(
	PktNumSpace::Handshake, pn, unsigned(n), pktAckEliciting);
      return false;
    }
    if (payload.ack(PktNumSpace::Handshake))
      ackSentTx(PktNumSpace::Handshake, payload.ackGen());
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtZeroRTTPkt(
    CIDSel::T dcid, CIDSel::T scid, unsigned pnLength,
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const TxPktRefs *recordRefs, bool ackEliciting,
    AllocTxPkt allocTxPkt, SendPkt sendPkt) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC 0-RTT packet protection outside Tx thread", return false);
    if (!txKeySecretInstalled_(PktKeyLevel::ZeroRTT)) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
	level = PktNumSpace::AppData,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    if (ZuUnlikely(!buf)) return false;
    unsigned capacity = txPktCapacity(buf);
    const TrafficSecret &secret =
      txKeySecret_(PktKeyLevel::ZeroRTT);
    if (!payload.padForProtSample(
	  pnLength, secret.tagLen))
      return false;
    int headerLen = Pkt::writeLong(
      buf->data(), capacity, PktType::ZeroRTT,
      headerCID(dcid), headerCID(scid),
      payload.bytes() + secret.tagLen, pnLength);
    if (headerLen < 0 ||
	PktNumber::encode(
	  buf->data() + headerLen, capacity - unsigned(headerLen),
	  m_linkTx.txPN[PktNumSpace::AppData], pnLength) != int(pnLength))
      return false;
    uint64_t pn = m_linkTx.txPN[PktNumSpace::AppData];
    int n = PktProt::protectLongV(
      buf->data(), capacity,
      txKeyProtState_(PktKeyLevel::ZeroRTT), pn,
      byteSpan(buf->data(), unsigned(headerLen) + pnLength),
      payload.data(), payload.count(), unsigned(headerLen), pnLength);
    if (n < 0) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
	level = PktNumSpace::AppData,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(PktNumSpace::AppData, payload, ackLevel, ackLargest);
    bool pktAckEliciting =
      txAckEliciting(recordFrame, recordRefs, ackEliciting);
    if (!recordProtPktTx(
	  PktNumSpace::AppData, pn, unsigned(n), recordFrame, recordRefs,
	  pktAckEliciting, false, 0, ackLevel, ackLargest, PktType::ZeroRTT))
      return false;
	    if (!sendPkt(ZuMv(buf), ZuMv(addr))) {
	      discardProtPktTx(
		PktNumSpace::AppData, pn, unsigned(n), pktAckEliciting);
	      return false;
	    }
    if (m_linkTx.txEarlyData.state == LinkEarlyState::None)
      m_linkTx.txEarlyData.state = LinkEarlyState::Offered;
    m_linkTx.txEarlyData.bytes += unsigned(n);
    return true;
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtShortPkt(
    CIDSel::T dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    return sendProtShortPkt(
      headerCID(dcid), pnLength, payload, ZuMv(addr), recordFrame,
      recordRefs, ackEliciting, ZuMv(allocTxPkt), ZuMv(sendPkt),
      pmtudSize);
  }

  template <typename AllocTxPkt, typename SendPkt>
  bool sendProtShortPkt(
    const CxnID &dcid, unsigned pnLength, PktBuild &payload,
    ZiSockAddr addr, ZuBSpan recordFrame, const TxPktRefs *recordRefs,
    bool ackEliciting, AllocTxPkt allocTxPkt, SendPkt sendPkt,
    unsigned pmtudSize = 0) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC Short packet protection outside Tx thread", return false);
    uint64_t runtime = m_linkTx.txRuntimeGen;
    ++m_linkTx.txSendDepth;
    struct SendScope {
      Self *link;
      uint64_t runtime;
      ~SendScope() { link->finishShortSend(runtime); }
    } scope{this, runtime};
    if (!establishedTx() &&
			!txSecretInstalled_(PktNumSpace::AppData)) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    ZmRef<ZiIOBuf> buf = allocTxPkt();
    if (ZuUnlikely(!buf || runtime != m_linkTx.txRuntimeGen)) return false;
    unsigned capacity = txPktCapacity(buf);
    int headerLen = Pkt::writeShort(
      buf->data(), capacity, dcid,
      m_linkTx.txPN[PktNumSpace::AppData], pnLength, m_linkTx.txKeyPhase);
    if (headerLen < 0) return false;
    if (!payload.padForProtSample(
	  pnLength, txSecret_(PktNumSpace::AppData).tagLen))
      return false;
    unsigned minSize = payload.pathValidation() &&
      (!m_config.isServer || txPath(addr).canSend(MinUDPPayload)) ? MinUDPPayload : 0;
    unsigned paddedSize = pmtudSize > minSize ? pmtudSize : minSize;
    if (paddedSize) {
      unsigned tagLen = txSecret_(PktNumSpace::AppData).tagLen;
      unsigned headerBytes = unsigned(headerLen);
      if (paddedSize <= headerBytes + tagLen) return false;
      if (!payload.padTo(paddedSize - headerBytes - tagLen)) return false;
    }
    uint64_t pn = m_linkTx.txPN[PktNumSpace::AppData];
    int n = PktProt::protectShortV(
      buf->data(), capacity,
      txProtState_(PktNumSpace::AppData), pn,
      byteSpan(buf->data(), unsigned(headerLen)),
      payload.data(), payload.count(),
      unsigned(headerLen) - pnLength, pnLength);
    if (n < 0) {
      ++m_linkTx.txDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		SecEvt event{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protect,
	  .success = false};
	event.packetSpace = level;
	event.trigger = SecTrigger::TX;
	o.logSecEvt(EvtName::PktProtectFail, event, time);
      }));
      return false;
    }
    buf->length = unsigned(n);
    uint8_t ackLevel;
    uint64_t ackLargest;
    txAckMeta(PktNumSpace::AppData, payload, ackLevel, ackLargest);
    bool pktAckEliciting =
      txAckEliciting(recordFrame, recordRefs, ackEliciting);
    EcnMark::T ecn = EcnMark::NotECT;
    if (ZuUnlikely(app()->congestionControl()))
      ecn = m_linkTx.path.txECN(PktNumSpace::AppData, pktAckEliciting, pmtudSize != 0);
    if (!recordProtPktTx<false>(
	  PktNumSpace::AppData, pn, unsigned(n), recordFrame, recordRefs,
	  pktAckEliciting, pmtudSize != 0, pmtudSize, ackLevel, ackLargest,
	  PktType::N, ecn))
      return false;
    bool sent = sendPkt(ZuMv(buf), ZuMv(addr), ecn);
    if (runtime != m_linkTx.txRuntimeGen) return sent;
    if (!sent) {
      discardProtPktTx(
	PktNumSpace::AppData, pn, unsigned(n), pktAckEliciting);
      schedulePTO_();
      scheduleLossTimer();
      return false;
    }
    finishProtPktTx(PktNumSpace::AppData, pn, unsigned(n), recordRefs,
      pktAckEliciting, ackLevel, ackLargest, PktType::N, ecn, &payload);
    if (payload.ack(PktNumSpace::AppData))
      ackSentTx(PktNumSpace::AppData, payload.ackGen());
    return true;
  }

  template <typename ReceiveLong, typename ReceiveShort>
  void receiveDatagram_(
    Datagram d, ReceiveLong receiveLong, ReceiveShort receiveShort) {
    consumeAckSentRx();
    m_linkRx.rxDiag.datagramsRx += 1;
    if (!d.buf) {
      ++m_linkRx.rxDiag.failures;
      return;
    }
    m_linkRx.rxDiag.bytesRx += d.buf->length;
    recordPathRx(d.addr, d.buf->length);
    bool ok = true;
    unsigned offset = 0;
    while (offset < d.buf->length) {
      ZuBSpan packet{
	d.buf->data() + offset, d.buf->length - offset};
      if (Pkt::isLong(packet)) {
	LongHdr h;
	if (Pkt::parseLong(packet, h) < 0) {
	  ZquicLOG(app()->qlogTrace(), ([
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
	  ZquicLOG(app()->qlogTrace(), ([
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
    if (!ok) ++m_linkRx.rxDiag.failures;
    ZquicLOG(app()->qlogTrace(), ([
      bytes = d.buf->length,
      ecn = d.ecn,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      DgramEvt event{.linkInfo = linkInfo, .size = bytes};
      event.ecn = ecn;
      o.logDgramRecv(event, time);
    }));
  }

  template <typename PrepareLong, typename ConsumeFrames>
  bool receiveProtLongPkt(
    InitialKeyDir::T keyDir, Datagram &d, unsigned packetOffset, unsigned packetLen,
    PrepareLong prepareLong, ConsumeFrames consumeFrames) {
    uint8_t *base = d.buf->data() + packetOffset;
    ZuBSpan packet{base, packetLen};
    auto datagram = d.buf->cspan();
    LongHdr h;
    if (Pkt::parseLong(packet, h) < 0) {
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::Initial,
	packetBytes = packetLen,
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
      return false;
    }
    if (!prepareLong(h, d)) {
      ZquicLOG(app()->qlogTrace(), ([
		packetType = h.type,
	level = PktNumSpace::Initial,
	packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvt event{.packetSize = packetBytes};
		event.packetType = packetType;
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = Reason::PrepareLong;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    PktKeyLevel::T keyLevel =
      h.type == PktType::Initial ? PktKeyLevel::Initial :
      h.type == PktType::Handshake ? PktKeyLevel::Handshake :
      h.type == PktType::ZeroRTT ? PktKeyLevel::ZeroRTT :
      PktKeyLevel::N;
    PktNumSpace::T level = spaceFromKeyLevel(keyLevel);
    bool earlyData = keyLevel == PktKeyLevel::ZeroRTT;
    if (keyLevel == PktKeyLevel::N || level == PktNumSpace::N) {
      ZquicLOG(app()->qlogTrace(), ([
	packetType = h.type,
	packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PktEvt event{.packetSize = packetBytes};
	event.packetType = packetType;
	event.packetSpace = PktNumSpace::N;
	event.ecn = EcnMark::N;
	event.reason = Reason::UnsupportedLongType;
	event.linkInfo = linkInfo;
	o.logPktDrop(event, time);
      }));
      return false;
    }
    if (earlyData &&
	(m_linkRx.rxEarlyData.oneRTTSeen || rxEarlyDataRejected())) {
      if (!rxEarlyDataRejected())
	rejectEarlyData(ZeroRTTReason::AfterOneRTT);
      ZquicLOG(app()->qlogTrace(), ([
	packetType = h.type, level, packetBytes = packetLen,
	reason = m_linkRx.rxEarlyData.oneRTTSeen ?
	  Reason::AfterOneRTT :
	  Reason::FramePolicy,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PktEvt event{.packetSize = packetBytes};
	event.packetType = packetType;
	event.packetSpace = level;
	event.ecn = EcnMark::N;
	event.reason = reason;
	event.linkInfo = linkInfo;
	o.logPktDrop(event, time);
      }));
      return true;
    }
    if (m_linkRx.rxSpaceDiscarded[level]) {
      ZquicLOG(app()->qlogTrace(), ([
	packetType = h.type, level, packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PktEvt event{.packetSize = packetBytes};
	event.packetType = packetType;
	event.packetSpace = level;
	event.ecn = EcnMark::N;
	event.reason = Reason::DiscardedSpace;
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
	m_linkRx.rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    else {
      if (!m_linkRx.crypto.rxKeySecretInstalled(keyLevel)) {
	ZquicLOG(app()->qlogTrace(), ([
	  packetType = h.type, level, packetBytes = packetLen,
	  linkInfo = linkInfo_()
	](auto &o, ZuTime time) {
	  PktEvt event{.packetSize = packetBytes};
	  event.packetType = packetType;
	  event.packetSpace = level;
	  event.ecn = EcnMark::N;
	  event.reason = Reason::MissingKeys;
	  event.linkInfo = linkInfo;
	  o.logPktDrop(event, time);
	  SecEvt security{
	    .linkInfo = linkInfo,
	    .kind = SecKind::PktProtect,
	    .reason = SecReason::MissingKeys,
	    .success = false};
	  security.packetSpace = level;
	  security.trigger = SecTrigger::RX;
	  o.logSecEvt(EvtName::PktProtectFail, security, time);
	}));
	return true;
      }
      plainLen = PktProt::unprotectLong(
	base, packetLen,
	m_linkRx.crypto.rxKeyProtState(keyLevel),
	m_linkRx.rxLargestPN[level], h.pnOffset, pn, payloadOffset);
    }
    if (plainLen < 0) {
      if (checkStatelessReset_(datagram, true)) return true;
      ++m_linkRx.rxDiag.failures;
      ZquicLOG(app()->qlogTrace(), ([
	packetType = h.type, level, packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PktEvt event{.packetSize = packetBytes};
	event.packetType = packetType;
	event.packetSpace = level;
	event.ecn = EcnMark::N;
	event.reason = Reason::Protection;
	event.linkInfo = linkInfo;
	o.logPktDrop(event, time);
	SecEvt security{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::Protection,
	  .success = false};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvt(EvtName::PktProtectFail, security, time);
      }));
      return true;
    }
    if (rxPktSeen(level, pn)) {
      ++m_linkRx.rxDiag.duplicatePacketsRx;
      noteDupAck(level, pn, d.addr);
      ZquicLOG(app()->qlogTrace(), ([
	packetType = h.type, level, packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
	PktEvt event{.packetSize = packetBytes};
	event.packetType = packetType;
	event.packetSpace = level;
	event.ecn = EcnMark::N;
	event.reason = Reason::Duplicate;
	event.linkInfo = linkInfo;
	o.logPktDrop(event, time);
      }));
      return true;
    }
    ++m_linkRx.rxDiag.packetsRx;
    if (drainingRx()) return true;
    if (closingRx_()) {
      noteClosingPacket(d.addr);
      return true;
    }
    RxAckMeta ack;
    ZiSockAddr ackAddr = d.addr;
    ZuUnion<void, ZquicLog_::PktEvt> qlog;
    ZquicLog_::PktEvt *qlog_ = nullptr;
    if (ZquicLogger::enabled())
      qlog_ = new (qlog.new_<1>()) ZquicLog_::PktEvt{};
    if (!consumeFrames(
	  level, pn, byteSpan(base + payloadOffset, unsigned(plainLen)),
	  d.addr, d.buf, ack, qlog_, earlyData))
      return false;
    if (level == PktNumSpace::Handshake &&
	!m_linkRx.rxSpaceDiscarded[PktNumSpace::Initial])
      discardPktNumSpace_0(PktNumSpace::Initial);
    if (earlyData) {
      if (m_linkRx.rxEarlyData.state == LinkEarlyState::None)
	m_linkRx.rxEarlyData.state = LinkEarlyState::Recv;
      m_linkRx.rxEarlyData.bytes += unsigned(plainLen);
    }
    ZquicLOG(app()->qlogTrace(), ([
      packetType = h.type,
      level, pn,
      packetBytes = packetLen,
      payloadBytes = unsigned(plainLen),
      ecn = d.ecn,
      event = qlog_ ? ZuMv(*qlog_) : PktEvt{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = packetType;
      event.packetSpace = level;
      event.ecn = ecn;
      event.packetNumber = pn;
      event.packetSize = packetBytes;
      event.payloadSize = payloadBytes;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    noteAck(
      level, pn, ack.ackEliciting, ZuMv(ackAddr), ack.immediateAck, d.ecn);
    return true;
  }

  template <typename ConsumeFrames>
  bool rxProtShortPkt(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    ConsumeFrames consumeFrames, const CxnID *routedDCID = nullptr) {
    uint8_t *base = d.buf->data() + packetOffset;
    ZuBSpan packet{base, packetLen};
    auto datagram = d.buf->cspan();
    if (ZuUnlikely(!m_linkRx.crypto.rxSecretInstalled(PktNumSpace::AppData))) {
      if (checkStatelessReset_(datagram, true)) return true;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
	packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvt event{.packetSize = packetBytes};
		event.packetType = pktTypeFromSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = Reason::MissingKeys;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
		SecEvt security{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::MissingKeys,
	  .success = false};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvt(
	  EvtName::PktProtectFail, security, time);
      }));
      return true;
    }
    ShortHdr h;
    unsigned cidLen = routedDCID && *routedDCID ?
      routedDCID->length() : m_linkRx.localSCID.length();
    if (Pkt::parseShort(packet, cidLen, h) < 0 ||
	(routedDCID && *routedDCID && !(h.dcid == *routedDCID)) ||
	!acceptLocalShortCID(h.dcid)) {
      if (checkStatelessReset_(datagram, true)) return true;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
	packetBytes = packetLen,
	linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvt event{.packetSize = packetBytes};
		event.packetType = pktTypeFromSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = Reason::ParseShort;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return false;
    }
    uint64_t pn = 0;
    unsigned payloadOffset = 0;
    int plainLen = PktProt::unprotectShort(
      base, packetLen,
      m_linkRx.crypto.rxProtState(PktNumSpace::AppData),
      m_linkRx.rxLargestPN[PktNumSpace::AppData],
      h.pnOffset, pn, payloadOffset);
    if (ZuUnlikely(plainLen < 0)) {
      if (m_linkRx.rxOldProt.valid()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_linkRx.rxOldProt,
	  m_linkRx.rxLargestPN[PktNumSpace::AppData],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  if (!((base[0] & 0x04) == (m_linkRx.rxOldKeyPhase ? 0x04 : 0))) {
    ++m_linkRx.rxDiag.invalidKeyPhases;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::AppData,
	      packetBytes = packetLen,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      PktEvt event{.packetSize = packetBytes};
	      event.packetType = pktTypeFromSpace(level);
	      event.packetSpace = level;
	      event.ecn = EcnMark::N;
	      event.reason = Reason::BadKeyPhase;
	      event.linkInfo = linkInfo;
	      o.logPktDrop(event, time);
	      SecEvt security{
			.linkInfo = linkInfo,
			.kind = SecKind::PktProtect,
			.reason = SecReason::BadKeyPhase,
			.success = false};
      security.packetSpace = level;
      security.trigger = SecTrigger::RX;
      o.logSecEvt(
		EvtName::PktProtectFail, security, time);
    }));
    return false;
	  }
	  ++m_linkRx.rxDiag.oldKeysAccepted;
	}
      }
      if (plainLen < 0 && ensureNextPeerKey()) {
	plainLen = PktProt::unprotectShort(
	  base, packetLen, m_linkRx.rxNextProt,
	  m_linkRx.rxLargestPN[PktNumSpace::AppData],
	  h.pnOffset, pn, payloadOffset);
	if (plainLen >= 0) {
	  bool phase = base[0] & 0x04;
	  // Retaining reordered-packet keys must not block the next update.
	  if (phase == m_linkRx.rxKeyPhase ||
	      pn <= m_linkRx.rxLargestPN[PktNumSpace::AppData]) {
	    ++m_linkRx.rxDiag.invalidKeyPhases;
	    ZquicLOG(app()->qlogTrace(), ([
	      level = PktNumSpace::AppData,
	      packetBytes = packetLen,
	      linkInfo = linkInfo_()
	    ](auto &o, ZuTime time) {
	      PktEvt event{.packetSize = packetBytes};
	      event.packetType = pktTypeFromSpace(level);
	      event.packetSpace = level;
	      event.ecn = EcnMark::N;
	      event.reason = Reason::BadKeyPhase;
	      event.linkInfo = linkInfo;
	      o.logPktDrop(event, time);
	      SecEvt security{
			.linkInfo = linkInfo,
			.kind = SecKind::PktProtect,
			.reason = SecReason::BadKeyPhase,
			.success = false};
      security.packetSpace = level;
      security.trigger = SecTrigger::RX;
      o.logSecEvt(
		EvtName::PktProtectFail, security, time);
    }));
    return false;
	  }
	  if (!commitPeerKeyUpdate(m_linkRx.rxNextSecret))
    return false;
	}
      }
      if (ZuUnlikely(plainLen < 0)) {
	if (checkStatelessReset_(datagram, true)) return true;
	++m_linkRx.rxDiag.failures;
		ZquicLOG(app()->qlogTrace(), ([
	  level = PktNumSpace::AppData,
	  packetBytes = packetLen,
	  linkInfo = linkInfo_()
		](auto &o, ZuTime time) {
	  PktEvt event{.packetSize = packetBytes};
	  event.packetType = pktTypeFromSpace(level);
	  event.packetSpace = level;
	  event.ecn = EcnMark::N;
	  event.reason = Reason::Protection;
	  event.linkInfo = linkInfo;
	  o.logPktDrop(event, time);
	  SecEvt security{
	    .linkInfo = linkInfo,
	    .kind = SecKind::PktProtect,
	    .reason = SecReason::Protection,
	    .success = false};
	  security.packetSpace = level;
	  security.trigger = SecTrigger::RX;
	  o.logSecEvt(
    EvtName::PktProtectFail, security, time);
	}));
	return false;
      }
    } else if (ZuUnlikely((base[0] & 0x04) != (m_linkRx.rxKeyPhase ? 0x04 : 0))) {
      ++m_linkRx.rxDiag.invalidKeyPhases;
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvt event{.packetSize = packetBytes};
		event.packetType = pktTypeFromSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = Reason::BadKeyPhase;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
		SecEvt security{
	  .linkInfo = linkInfo,
	  .kind = SecKind::PktProtect,
	  .reason = SecReason::BadKeyPhase,
	  .success = false};
	security.packetSpace = level;
	security.trigger = SecTrigger::RX;
	o.logSecEvt(
	  EvtName::PktProtectFail, security, time);
      }));
      return true;
    }
    if (rxPktSeen(PktNumSpace::AppData, pn)) {
      ++m_linkRx.rxDiag.duplicatePacketsRx;
      noteDupAck(PktNumSpace::AppData, pn, d.addr);
      ZquicLOG(app()->qlogTrace(), ([
		level = PktNumSpace::AppData,
		packetBytes = packetLen,
		linkInfo = linkInfo_()
      ](auto &o, ZuTime time) {
		PktEvt event{.packetSize = packetBytes};
		event.packetType = pktTypeFromSpace(level);
		event.packetSpace = level;
		event.ecn = EcnMark::N;
		event.reason = Reason::Duplicate;
		event.linkInfo = linkInfo;
		o.logPktDrop(event, time);
      }));
      return true;
    }
    ++m_linkRx.rxDiag.packetsRx;
    if (drainingRx()) return true;
    if (closingRx_()) {
      noteClosingPacket(d.addr);
      return true;
    }
    RxAckMeta ack;
    ZiSockAddr ackAddr = d.addr;
    ZuUnion<void, ZquicLog_::PktEvt> qlog;
    ZquicLog_::PktEvt *qlog_ = nullptr;
    if (ZquicLogger::enabled())
      qlog_ = new (qlog.new_<1>()) ZquicLog_::PktEvt{};
    if (!consumeFrames(
	  PktNumSpace::AppData, pn,
	  byteSpan(base + payloadOffset, unsigned(plainLen)),
	  d.addr, d.buf, ack, qlog_))
      return false;
    m_linkRx.rxEarlyData.oneRTTSeen = true;
    ZquicLOG(app()->qlogTrace(), ([
      level = PktNumSpace::AppData,
      pn,
      packetBytes = packetLen,
      payloadBytes = unsigned(plainLen),
      ecn = d.ecn,
      event = qlog_ ? ZuMv(*qlog_) : PktEvt{},
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) mutable {
      event.packetType = pktTypeFromSpace(level);
      event.packetSpace = level;
      event.ecn = ecn;
      event.packetNumber = pn;
      event.packetSize = packetBytes;
      event.payloadSize = payloadBytes;
      event.linkInfo = linkInfo;
      o.logPktRecv(event, time);
    }));
    noteAck(
      PktNumSpace::AppData, pn, ack.ackEliciting, ZuMv(ackAddr),
      ack.immediateAck, d.ecn);
    return true;
  }

  template <typename EmitTLS, typename HandleControl>
  bool consumeProtFrames(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, RxAckMeta &ackMeta,
    ZquicLog_::PktEvt *qlog, EmitTLS emitTLS,
    HandleControl handleControl, bool earlyData = false) {
    unsigned offset = 0;
    auto frame_ = ZmScratch(Frame, 1);
    auto &frame = *new (frame_.push()) Frame{};
    bool ackEliciting = false;
    bool immediateAck = false;

    unsigned n = frames.length();
    while (offset < n) {
      unsigned used = 0;
      if (FrameCodec::parse(
	  ZuBSpan{frames.data() + offset, n - offset},
	  frame, used) < 0 || !used) {
	return false;
      }
      ++m_linkRx.rxDiag.framesRx;
      if (!packetFrameLegal(level, frame, earlyData)) {
	if (earlyData) rejectEarlyData(ZeroRTTReason::FramePolicy);
	return false;
      }
      if (qlog) qlogAddRxFrame(*qlog, level, frame);
      if (FrameCodec::ackEliciting(frame.type))
	ackEliciting = true;
      switch (frame.type) {
	case FrameType::Ack:
	  processAckFrame(level, frame);
	  break;
	case FrameType::Crypto: {
	  ZuBSpan contiguous;
	  m_linkRx.rxCryptoAddr[level] = addr;
	  if (!m_linkRx.crypto.tlsReady() ||
	      m_linkRx.rxCrypto[level].receiveFrame(frame, contiguous) < 0) {
	    return false;
	  }
	  m_linkRx.rxDiag.cryptoBytesRx += frame.payload.length();
	  if (contiguous) {
	    size_t epoch = 0;
	    if (!tlsEpochFromSpace(level, epoch))
	      return false;
	    if (!emitTLS(epoch, contiguous, addr))
	      return false;
	  }
	  break;
	}
	case FrameType::Stream:
	  m_linkRx.rxDiag.streamBytesRx += frame.payload.length();
	  if (receiveFrame(
		frame, ZmRef<ZiIOBuf>{packetBuf}, nullptr, &immediateAck,
		earlyData) < 0) {
	    return false;
	  }
	  ZquicLOG(app()->qlogTrace(), ([
	    streamID = frame.streamID,
	    offset = frame.offset,
	    length = uint64_t(frame.payload.length()),
	    fin = frame.fin,
	    linkInfo = linkInfo_()
	  ](auto &o, ZuTime time) {
	    if (!length && !fin) return;
	    StreamDataEvt event{
	      .linkInfo = linkInfo,
	      .streamID = streamID,
	      .offset = offset,
	      .length = length,
      .from = StreamDataLoc::Network,
      .to = StreamDataLoc::Transport,
      .additionalInfo = fin ?
		StreamDataInfo::T(StreamDataInfo::FinSet) :
		StreamDataInfo::T(StreamDataInfo::None)};
    o.logStreamDataMoved(event, time);
	  }));
	  impl()->streamFrame(
    frame.streamID, frame.offset, frame.payload, frame.fin);
	  break;
	case FrameType::ResetStream:
	case FrameType::StopSending:
	  if (receiveFrame(frame) < 0) {
	    return false;
	  }
	  blockedCreditRx(frame);
	  break;
	case FrameType::NewCxnID:
	  if (!receiveNewCxnID_(frame)) {
	    return false;
	  }
	  break;
	case FrameType::RetireCxnID:
	  if (!receiveRetireCxnID_(frame)) {
	    return false;
	  }
	  break;
	case FrameType::MaxData:
	  if (!rxApplyMaxData(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::MaxStreamData:
	  if (!rxApplyMaxStreamData(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::MaxStreams:
	  if (!rxApplyMaxStreams(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::DataBlocked:
	  if (!validateDataBlocked(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::StreamDataBlocked:
	  if (!rxStreamBlocked_(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  blockedCreditRx(frame);
	  break;
	case FrameType::StreamsBlocked:
	  if (!validateStreamsBlocked(frame) ||
	      !handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::PathChallenge:
	case FrameType::PathResponse:
	case FrameType::ConnectionClose:
	case FrameType::ApplicationClose:
	case FrameType::HandshakeDone:
	  if (!handleControl(level, frame, addr)) {
	    return false;
	  }
	  break;
	case FrameType::Padding:
	case FrameType::Ping:
	  break;
	case FrameType::NewToken:
	  impl()->newToken(frame.payload);
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
  bool packetFrameLegal(
    PktNumSpace::T level, const Frame &frame, bool earlyData = false) const {
    if (earlyData) {
      if (level != PktNumSpace::AppData) return false;
      switch (frame.type) {
	case FrameType::Padding:
	case FrameType::Ping:
	case FrameType::Stream:
	  return true;
	default:
	  return false;
      }
    }
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
	return level != PktNumSpace::AppData || establishedRx();
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
	return !m_config.isServer && level == PktNumSpace::AppData &&
	  frame.payload.length();
      case FrameType::ApplicationClose:
	return level == PktNumSpace::AppData;
      case FrameType::HandshakeDone:
	return !m_config.isServer && level == PktNumSpace::AppData;
      default:
	return false;
    }
  }

  StreamRef openLocalStream(Zquic::StreamType::T type) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local stream open outside Tx thread", return {});
    return newStream(nextStreamID(type));
  }

  unsigned openQueued(Zquic::StreamType::T type, unsigned limit) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC queued stream open outside Tx thread", return 0);
    uint64_t &queued = m_linkTx.queued[type];
    unsigned opened = 0;
    while (queued && opened < limit && m_linkTx.peerLimit[type].open()) {
      StreamRef stream = openLocalStream(type);
      if (!stream) break;
      --queued;
      ++opened;
      streamQueuedOpen(stream);
      scheduleStreamWritable(stream);
    }
    return opened;
  }
  void streamQueuedOpen(StreamRef stream) {
    if (!stream) return;
    app()->rxRun([link = impl(), stream = ZuMv(stream)]() mutable {
      if (link->disconnecting_()) return;
      link->streamed(ZuMv(stream));
    });
  }
  void scheduleOpenQueued(Zquic::StreamType::T type) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC queued stream scheduling outside Tx thread", return);
    unsigned i = type == Zquic::StreamType::Simplex ? 1 : 0;
    if (!m_linkTx.queued[type] || m_linkTx.peerLimit[type].blocked() ||
	m_linkTx.openQueuedPending[i])
      return;
    m_linkTx.openQueuedPending[i] = true;
    app()->txRun([link = impl(), type, i, gen = m_linkTx.txRuntimeGen]() mutable {
      if (gen != link->m_linkTx.txRuntimeGen) return;
      link->m_linkTx.openQueuedPending[i] = false;
      if (link->disconnecting_()) return;
      link->openQueued(type, OpenQueuedBatch);
      link->scheduleOpenQueued(type);
    });
  }

  StreamRef findOrAccept(uint64_t id) {
    if (id > uint64_t(INT64_MAX)) return nullptr;
    if (auto stream = findStream(int64_t(id))) return stream;
    if (StreamID::server(id) == m_config.isServer) return nullptr;
    return acceptPeerStream(id);
  }

  static bool localInitiated(uint64_t id, bool isServer) {
    return StreamID::server(id) == isServer;
  }
  static unsigned streamTypeIndex(Zquic::StreamType::T type) {
    return type == Zquic::StreamType::Simplex ? 1 : 0;
  }
  bool canPeerSend(uint64_t id) const {
    return !StreamID::uni(id) || !localInitiated(id, m_config.isServer);
  }
  bool canLocalSend(uint64_t id) const {
    return !StreamID::uni(id) || localInitiated(id, m_config.isServer);
  }
  bool peerOpenedStreamID(uint64_t id) const {
    if (id > uint64_t(INT64_MAX) || localInitiated(id, m_config.isServer))
      return false;
    Zquic::StreamType::T type = StreamID::uni(id);
    return StreamID::ordinal(id) < m_linkRx.localLimit[type].opened();
  }
  bool localOpenedStreamID(uint64_t id) const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC local stream ID access outside Tx thread", return false);
    if (id > uint64_t(INT64_MAX) || !localInitiated(id, m_config.isServer))
      return false;
    uint64_t opened = StreamID::uni(id) ? m_linkTx.nextUniOrdinal : m_linkTx.nextDuplexOrdinal;
    return StreamID::ordinal(id) < opened;
  }
  bool closedStreamID(uint64_t id) const {
    if (id > uint64_t(INT64_MAX)) return false;
    unsigned i = unsigned(id & 3);
    uint64_t ordinal = StreamID::ordinal(id);
    if (ordinal < m_linkRx.closedStreamBase[i]) return true;
    return bool(m_linkRx.closedStreams->findPtr(id));
  }
  void closeStreamID(uint64_t id) {
    unsigned i = unsigned(id & 3);
    uint64_t ordinal = StreamID::ordinal(id);
    uint64_t &base = m_linkRx.closedStreamBase[i];
    if (ordinal < base) return;
    if (ordinal > base) {
      auto node = new ClosedStreams::Node{id};
      m_linkRx.closedStreams->addNode(node);
      m_linkRx.closedStreamList.pushNode(
	static_cast<Zquic_::ClosedStreamList::Node *>(node));
      return;
    }
    ++base;
    advanceClosedStreams(i);
  }
  void advanceClosedStreams(unsigned i) {
    if (m_linkRx.closedStreamPending[i]) return;
    do {
      uint64_t &base = m_linkRx.closedStreamBase[i];
      for (unsigned n = 0; n < StreamCloseBatch; ++n) {
	uint64_t next = (base << 2) | i;
	auto node = m_linkRx.closedStreams->del(next);
	if (!node) return;
	m_linkRx.closedStreamList.delNode(
	  static_cast<Zquic_::ClosedStreamList::Node *>(node.ptr()));
	++base;
      }
      if (app()->mx() && app()->mx()->running()) {
	m_linkRx.closedStreamPending[i] = true;
	app()->rxRun([link = impl(), i, runtime = m_linkRx.rxRuntimeGen]() mutable {
	  if (runtime != link->m_linkRx.rxRuntimeGen || link->disconnecting_()) return;
	  link->m_linkRx.closedStreamPending[i] = false;
	  link->advanceClosedStreams(i);
	});
	return;
      }
    } while (true);
  }

  bool validateMaxData(const Frame &frame) const {
    return frame.type == FrameType::MaxData;
  }
  bool validateMaxStreams(const Frame &frame) const {
    return frame.type == FrameType::MaxStreams &&
      frame.value <= MaxStreamCount;
  }
  bool validateMaxStreamData(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::MaxStreamData ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canLocalSend(frame.streamID))
      return false;
    stream = findStream(int64_t(frame.streamID));
    if (stream) return true;
    if (localInitiated(frame.streamID, m_config.isServer)) return false;
    if (StreamID::uni(frame.streamID)) return false;
    stream = acceptPeerStream(frame.streamID);
    return stream;
  }
  bool validateDataBlocked(const Frame &frame) const {
    return frame.type == FrameType::DataBlocked &&
      frame.value <= m_linkRx.rxDataCredit.limit();
  }
  bool validateStreamBlocked(const Frame &frame, StreamRef &stream) {
    stream = nullptr;
    if (frame.type != FrameType::StreamDataBlocked ||
	frame.streamID > uint64_t(INT64_MAX) ||
	!canPeerSend(frame.streamID))
      return false;
    stream = findOrAccept(frame.streamID);
    return stream && frame.value <= stream->rxCreditLimit();
  }
  bool validateStreamsBlocked(const Frame &frame) const {
    return frame.type == FrameType::StreamsBlocked &&
      frame.value <= m_linkRx.localLimit[frame.streamType].limit();
  }

  bool rxStreamBlocked_(const Frame &frame) {
    if (frame.type == FrameType::StreamDataBlocked &&
	frame.streamID <= uint64_t(INT64_MAX)) {
      if (StreamRef stream = findStream(int64_t(frame.streamID))) {
	if (stream->resetReceived() || stream->rxComplete()) {
	  ++m_linkRx.rxDiag.streamBlockedClosedRx;
	  noteInvalidStreamActivity(TransportError::StreamState, true);
	  return true;
	}
      } else if (closedStreamID(frame.streamID) &&
	  canPeerSend(frame.streamID)) {
	++m_linkRx.rxDiag.streamBlockedClosedRx;
	noteInvalidStreamActivity(TransportError::StreamState, true);
	return true;
      }
    }
    StreamRef stream;
    if (!validateStreamBlocked(frame, stream)) {
      ++m_linkRx.rxDiag.streamBlockedInvalidRx;
      noteInvalidStreamActivity(TransportError::StreamState);
      return false;
    }
    if (!stream->receiveBlocked(frame)) {
      ++m_linkRx.rxDiag.streamBlockedFinalRx;
      noteInvalidStreamActivity(TransportError::FinalSize, false, true);
      return false;
    }
    return true;
  }
  bool blockedFrameNeeded(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::DataBlocked:
	return m_linkTx.lastDataBlocked != frame.value;
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	return !stream || stream->lastBlocked() != frame.value;
      }
      case FrameType::StreamsBlocked:
	return m_linkTx.lastStreamsBlocked[frame.streamType] != frame.value;
      default:
	return true;
    }
  }
  void noteBlockedQueued(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::DataBlocked:
	m_linkTx.lastDataBlocked = frame.value;
	break;
      case FrameType::StreamDataBlocked:
	if (StreamRef stream = findTxStream_(int64_t(frame.streamID)))
	  stream->lastBlocked(frame.value);
	break;
      case FrameType::StreamsBlocked:
	m_linkTx.lastStreamsBlocked[frame.streamType] = frame.value;
	break;
      default:
	break;
    }
  }
  void noteControlDequeued(const ControlFrame &frame) {
    switch (frame.type) {
      case FrameType::StreamsBlocked:
	m_linkTx.lastStreamsBlocked[frame.streamType] = U64Null;
	break;
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
  void clearLinkControls() {
    m_linkTx.maxDataControl = {};
    m_linkTx.dataBlockedControl = {};
    m_linkTx.handshakeDoneControl = {};
    m_linkTx.pathChallengeControl = {};
    for (unsigned i = 0; i < 2; ++i) {
      m_linkTx.maxStreamsControl[i] = {};
      m_linkTx.streamsBlockedControl[i] = {};
    }
    m_linkTx.pathResponses.clean();
    m_linkTx.newCxnIDControls.clean();
  }

  bool controlStillValid_(const ControlFrame &frame) const {
    switch (frame.type) {
      case FrameType::MaxData:
	return m_linkTx.maxDataControl.frame == frame &&
	  frame.value == uint64_t(m_creditRxPub.limit);
      case FrameType::MaxStreamData: {
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::MaxStreams:
	return m_linkTx.maxStreamsControl[streamTypeIndex(frame.streamType)].frame ==
	  frame;
      case FrameType::DataBlocked:
	return m_linkTx.dataBlockedControl.frame == frame &&
	  m_linkTx.txDataCredit.blocked() &&
	  frame.value == m_linkTx.txDataCredit.limit();
      case FrameType::StreamDataBlocked: {
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StreamsBlocked:
	return m_linkTx.streamsBlockedControl[streamTypeIndex(frame.streamType)].frame ==
	  frame && m_linkTx.queued[frame.streamType] &&
	  frame.value == m_linkTx.peerLimit[frame.streamType].limit();
      case FrameType::ResetStream: {
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::StopSending: {
	StreamRef stream = findTxStream_(int64_t(frame.streamID));
	return stream && stream->controlStillValid(frame);
      }
      case FrameType::PathChallenge:
	return m_linkTx.pathChallengeControl.frame == frame &&
	  m_linkTx.migration.active &&
	  m_linkTx.migration.challenge.equals(frame.payload);
      case FrameType::PathResponse:
	return true;
      case FrameType::HandshakeDone:
	return m_linkTx.handshakeDoneControl.frame == frame;
      case FrameType::NewCxnID: {
	const LinkCID *cid = findLocalCID(frame.streamID);
	return cid &&
	  cid->state == CxnState::Active &&
	  cid->id == frame.cxnID &&
	  cid->resetToken == frame.resetToken;
      }
      default:
	return false;
    }
  }

  void maybeExtendMaxData() {
    uint64_t window = m_linkRx.transportParams.initialMaxData;
    if (!window || m_linkRx.rxDataCredit.available() > window / 2) return;
    uint64_t maximum =
      m_linkRx.rxDataRetired > U64Null - window ? U64Null :
      m_linkRx.rxDataRetired + window;
    if (maximum <= m_linkRx.rxDataCredit.limit()) return;
    m_linkRx.rxDataCredit.extend(maximum);
    m_creditRxPub.limit = maximum;
    requestCreditTx();
  }
  void maybeExtendMaxStreamData(const StreamRef &stream) {
    if (!stream || !stream->readOpen()) return;
    uint64_t window = initialStreamRxCredit(uint64_t(stream->id()));
    if (!window || stream->rxCreditAvailable() > window / 2) return;
    uint64_t maximum =
      stream->rxRetired() > U64Null - window ? U64Null :
      stream->rxRetired() + window;
    if (maximum <= stream->rxCreditLimit()) return;
    stream->extendRxCredit(maximum);
    requestCreditTx();
  }
  void notifyStreamTxClosed(const StreamRef &stream) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream credit notification outside Tx thread", return);
    if (!stream || stream->id() < 0 ||
	(!stream->finDequeued() && !stream->resetSent()))
      return;
    app()->rxRun([link = impl(), stream]() mutable {
      if (link->disconnecting_()) return;
      stream->markTxClosedForCredit();
      link->returnStreamCreditRx(stream);
    });
  }
  bool returnStreamCreditRx(const StreamRef &stream) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream credit return outside Rx thread", return false);
    if (!stream || stream->retiredRx() || stream->id() < 0 || stream->creditReturned())
      return false;
    uint64_t id = uint64_t(stream->id());
    if (localInitiated(id, m_config.isServer)) return false;
    if (!stream->rxComplete() && !stream->resetReceived()) return false;
    if (!StreamID::uni(id) && !stream->closedForStreamCredit())
      return false;
    Zquic::StreamType::T type = StreamID::uni(id);
    StreamLimit &limit = m_linkRx.localLimit[type];
    if (limit.limit() >= MaxStreamCount) return false;
    uint64_t next = limit.limit() + 1;
    limit.extend(next);
    stream->markCreditReturned();
    postFlowUpdate(
      FlowUpdate{FrameType::MaxStreams, 0, limit.limit(), type});
    return true;
  }

  bool txClosed(const Stream *stream) const {
    if (!canLocalSend(uint64_t(stream->id()))) return true;
    if (stream->resetSent()) return stream->resetAckd();
    if (!stream->finSent()) return false;
    return stream->finDequeued() && !stream->txUnackdCount();
  }
  bool isRxClosed(const Stream *stream) const {
    if (!canPeerSend(uint64_t(stream->id()))) return true;
    return stream->rxComplete() || stream->resetReceived();
  }
  bool streamReapableTx(const Stream *stream) const {
    if (!stream || stream->id() < 0) return false;
    if (stream->txQueued() || stream->txRangeCount() ||
	stream->txBufferedBytes() || stream->txUnackdCount())
      return false;
    return txClosed(stream);
  }
  bool reapStream_0(Stream *stream) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream retirement outside Tx thread", return false);
    if (!streamReapableTx(stream) || stream->reapPending()) return false;
    int64_t id = stream->id();
    StreamRef ref = findTxStream_(id);
    if (!ref || ref.ptr() != stream) return false;
    stream->reapPending(true);
    uint64_t txBytes = stream->txBytes();
    bool finSent = stream->finSent();
    StreamError::T txError = stream->txError();
    uint64_t txAppError = stream->txAppError();
    app()->rxRun([
      link = impl(), stream = ZuMv(ref), txBytes, finSent,
      txError, txAppError
    ]() mutable {
      link->reapStream_1(
	ZuMv(stream), txBytes, finSent, txError, txAppError);
    });
    return true;
  }
  void reapStream_1(StreamRef stream, uint64_t txBytes, bool finSent,
      StreamError::T txError, uint64_t txAppError) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC stream Rx retirement outside Rx thread", return);
    int64_t id = stream ? stream->id() : -1;
    StreamRef found;
    if (id >= 0) found = m_linkRx.streams->find(id);
    if (!found || found.ptr() != stream.ptr() ||
	stream->rxPending() || stream->rxQueued() ||
	!isRxClosed(stream) || !streamCreditSettled(stream)) {
      app()->txRun([link = impl(), stream = ZuMv(stream)]() mutable {
	if (stream) stream->reapPending(false);
      });
      return;
    }
    ZquicLOG(app()->qlogTrace(), ([
      streamID = uint64_t(id),
      streamType = StreamType::T(StreamID::uni(uint64_t(id))),
      streamSide = localInitiated(uint64_t(id), m_config.isServer) ?
	StreamSide::T(StreamSide::Sending) :
	StreamSide::T(StreamSide::Receiving),
      reason = StreamReason::T(StreamReason::Reaped),
      offset = stream->rxBytes(),
      length = txBytes,
      errorCode = stream->rxError() != StreamError::None ?
	stream->rxAppError() :
	txError != StreamError::None ? txAppError : 0,
      fin = stream->finReceived() || finSent,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      StreamEvt event{
		.linkInfo = linkInfo,
	.streamID = streamID,
		.offset = offset,
		.length = length,
		.errorCode = errorCode,
		.streamType = streamType,
	.oldState = StreamState::Open,
	.newState = StreamState::Closed,
	.streamSide = streamSide,
	.reason = reason,
		.fin = fin};
      o.logStreamStateUpd(event, time);
    }));
    closeStreamID(uint64_t(id));
    stream->finalRxQueues();
    auto rxNode = static_cast<RxStreams::Node *>(stream.ptr());
    if (m_linkRx.closeCursor == rxNode)
      m_linkRx.closeCursor = static_cast<RxStreams::Node::Ext *>(rxNode)->next;
    m_linkRx.allStreams.delNode(rxNode);
    (void)m_linkRx.streams->del(id);
    app()->txRun([link = impl(), stream = ZuMv(stream), id]() mutable {
      link->reapStream_2(ZuMv(stream), id);
    });
  }
  void reapStream_2(StreamRef stream, int64_t id) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC stream Tx retirement completion outside Tx thread", return);
    auto node = m_linkTx.txStreams->findPtr(id);
    if (node && static_cast<Stream *>(node) == stream.ptr()) {
      auto credit = static_cast<CreditStreams::Node *>(stream.ptr());
      if (m_linkTx.creditCursor == credit)
	m_linkTx.creditCursor = static_cast<CreditStreams::Node::Ext *>(credit)->next;
      m_linkTx.creditStreams.delNode(credit);
      stream->finalTxQueues();
      (void)m_linkTx.txStreams->del(id);
    }
  }
  bool streamCreditSettled(const Stream *stream) const {
    uint64_t id = uint64_t(stream->id());
    if (localInitiated(id, m_config.isServer)) return true;
    if (stream->creditReturned()) return true;
    Zquic::StreamType::T type = StreamID::uni(id);
    return m_linkRx.localLimit[type].limit() >= MaxStreamCount;
  }
  StreamRef newStream(int64_t id) {
    auto node = new Streams::Node{impl(), id};
    StreamRef stream{node};
    stream->txQueueMax(impl()->txQueueMax());
    stream->txCredit(initialStreamTxCredit(uint64_t(id)));
    stream->rxCredit(initialStreamRxCredit(uint64_t(id)));
    if (rxInvoked_()) {
      publishStreamRx(stream);
      app()->txRun([link = impl(), stream]() mutable {
	link->publishStreamTx(ZuMv(stream));
      });
    } else {
      ZiAssert(txInvoked_(), "Zquic", (),
	"QUIC stream creation outside Rx/Tx thread", return {});
      publishStreamTx(stream);
      app()->rxRun([link = impl(), stream]() mutable {
	link->publishStreamRx(ZuMv(stream));
      });
    }
    ZquicLOG(app()->qlogTrace(), ([
      streamID = uint64_t(id),
      streamType = StreamType::T(StreamID::uni(uint64_t(id))),
      streamSide = localInitiated(uint64_t(id), m_config.isServer) ?
		StreamSide::T(StreamSide::Sending) :
		StreamSide::T(StreamSide::Receiving),
      reason = localInitiated(uint64_t(id), m_config.isServer) ?
		StreamReason::T(StreamReason::LocalOpen) :
		StreamReason::T(StreamReason::PeerOpen),
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      StreamEvt event{
		.linkInfo = linkInfo,
		.streamID = streamID,
		.streamType = streamType,
	.oldState = StreamState::Idle,
		.newState = StreamState::Open,
		.streamSide = streamSide,
		.reason = reason};
      o.logStreamStateUpd(event, time);
    }));
    impl()->streamOpen(
      stream, StreamID::server(uint64_t(id)) == m_config.isServer);
    return stream;
  }

  uint64_t initialStreamTxCredit(uint64_t id) const {
    auto peer = peerParams_();
    if (!peer) return 0;
    const auto &params = *peer;
    if (StreamID::uni(id)) return params.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_config.isServer;
    return local ? params.initMaxStreamDataDuplexRmt :
      params.initMaxStreamDataDuplexLcl;
  }

  uint64_t initialStreamRxCredit(uint64_t id) const {
    const auto &params = app()->txInvoked() ? m_linkTx.localParams : m_linkRx.transportParams;
    if (StreamID::uni(id) && StreamID::server(id) == m_config.isServer) return 0;
    if (StreamID::uni(id)) return params.initialMaxStreamDataUni;
    bool local = StreamID::server(id) == m_config.isServer;
    return local ? params.initMaxStreamDataDuplexLcl :
      params.initMaxStreamDataDuplexRmt;
  }

  int64_t nextStreamID(Zquic::StreamType::T type) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC next stream ID allocation outside Tx thread", return INT64_MAX);
    uint64_t &ordinal =
      type == Zquic::StreamType::Simplex ? m_linkTx.nextUniOrdinal : m_linkTx.nextDuplexOrdinal;
    uint64_t id = (ordinal++ << 2) |
      (m_config.isServer ? 1U : 0U) |
      (type == Zquic::StreamType::Simplex ? 2U : 0U);
    ZiAssert(id <= uint64_t(INT64_MAX), "Zquic", (id),
      "stream ID overflow id=" << id, return INT64_MAX);
    return int64_t(id);
  }

  bool rxInvoked_() const {
    ZiAssert(m_config.app && m_config.app->mx(), "Zquic", (),
      "QUIC link Rx access before app initialization", return false);
    return m_config.app->rxInvoked();
  }
  bool txInvoked_() const {
    ZiAssert(m_config.app && m_config.app->mx(), "Zquic", (),
      "QUIC link Tx access before app initialization", return false);
    return m_config.app->txInvoked();
  }

  using TimerRes = Zquic_::TimerRes;
  TimerRes *timerRes(CxnTimer::T action) {
    switch (action) {
      case CxnTimer::Loss: return &m_linkTx.lossRes;
      case CxnTimer::PTO: return &m_linkTx.ptoRes;
      case CxnTimer::Idle: return &m_linkTx.idleRes;
      default: return nullptr;
    }
  }
  static void invalidateRes(TimerRes &res) {
    ++res.serial;
    res.logical = res.reserved = ZuTime{};
  }
  void reservedTimer(
    const char *name, CxnTimer::T action, ZmScheduler::Timer *timer,
    uint64_t serial, uint64_t runtime) {
    TimerRes &res = *timerRes(action);
    if (runtime != m_linkTx.txRuntimeGen || serial != res.serial || disconnecting_()) return;
    res.reserved = ZuTime{};
    drainPeerAcksTx();
    // ACK processing can replace this reservation before expiry dispatch.
    if (runtime != m_linkTx.txRuntimeGen || serial != res.serial || !*res.logical) return;
    // The ACK is already known, even when retiring its ranges needs more
    // owner turns. Completion recomputes both recovery deadlines; do not run
    // loss/PTO against packets still covered by that active ACK.
    if (m_linkTx.txPeerAckActive &&
	(action == CxnTimer::Loss || action == CxnTimer::PTO)) return;
    if (res.logical > now_()) {
      scheduleCxnTimer(name, action, res.logical, ZmScheduler::Update, timer);
      return;
    }
    res.logical = ZuTime{};
    cxnTimer(action);
  }

  void scheduleCxnTimer(
    const char *name, CxnTimer::T action,
    ZuTime out, int mode, ZmScheduler::Timer *timer) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC timer schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC timer schedule before app initialization", return);
    if (disconnecting_()) return;
    if (TimerRes *res = timerRes(action)) {
      res->logical = out;
      if (*res->reserved && res->reserved <= out) return;
      res->reserved = out;
      uint64_t serial = ++res->serial;
      uint64_t runtime = m_linkTx.txRuntimeGen;
      // add() reuses an armed callback. Disarm first when its identity changes.
      app()->mx()->del(timer);
      app()->mx()->add(timer, out, mode,
	[link = impl(), name, action, timer, serial, runtime](auto &&arm) {
	  return arm([link, name, action, timer, serial, runtime]() mutable {
	    link->reservedTimer(name, action, timer, serial, runtime);
	  });
	}, app()->txThread());
      return;
    }
    if (debugLog()) ZiLOG(Debug, "Zquic", ([name = ZeString{name}](auto &s) {
      s << "QUIC timer armed name=" << name;
    }));
    app()->mx()->add(timer, out, mode, [link = impl(), action](auto &&arm) {
      return arm([link, action]() mutable {
	if (link->disconnecting_()) return;
	link->cxnTimer(action);
      });
    }, app()->txThread());
  }

  void cancelTimer(const char *, ZmScheduler::Timer *timer) {
    if (timer == &m_timers.loss.node) invalidateRes(m_linkTx.lossRes);
    else if (timer == &m_timers.pto.node) invalidateRes(m_linkTx.ptoRes);
    else if (timer == &m_timers.idle.node) invalidateRes(m_linkTx.idleRes);
    app()->mx()->del(timer);
  }

  void cxnTimer(CxnTimer::T action) {
    switch (action) {
      case CxnTimer::AckDelay:
	ackDelayTimer();
	break;
      case CxnTimer::Loss:
	m_linkTx.lossTimerOut = {};
	lossTimer();
	break;
      case CxnTimer::PTO:
	m_linkTx.ptoTimerOut = {};
	impl()->ptoTimer();
	break;
      case CxnTimer::Idle:
	m_linkTx.idleTimerOut = {};
	idleTimer();
	break;
      case CxnTimer::HeartBeat:
	m_linkTx.heartBeatTimerOut = {};
	heartBeatTimer();
	break;
      case CxnTimer::Close:
	closeTimeout();
	break;
      case CxnTimer::KeyDiscard:
	keyDiscardTimer();
	break;
      case CxnTimer::PMTUD:
	pmtudTimeout();
	break;
      case CxnTimer::PathValid:
	pathTimer();
	break;
    }
  }

  bool ptoLevel(PktNumSpace::T &level) const {
    bool have = false;
    ZuTime out;
    for (unsigned i = 0; i <= unsigned(PktNumSpace::Handshake); ++i) {
      auto l = PktNumSpace::T(i);
      if (m_linkTx.txSpaceDiscarded[l] || !m_linkTx.txCryptoUnackd[l].count_()) continue;
      if (l == PktNumSpace::Handshake &&
	  !txSecretInstalled_(PktNumSpace::Handshake))
	continue;
      ZuTime deadline = ptoDeadline(l);
      if (!have || deadline < out) {
	level = l;
	out = deadline;
	have = true;
      }
    }
    if (have) return true;
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      auto l = PktNumSpace::T(i);
      if (m_linkTx.txSpaceDiscarded[l]) continue;
      if (l == PktNumSpace::AppData &&
	  !(m_linkTx.txSpaceDiscarded[PktNumSpace::Handshake] ||
	    (m_config.isServer && establishedTx())))
	continue;
      const auto &tx = m_linkTx.txPkts[l];
      if (!tx.ackElicitingInFlight() && !tx.retransmitPending()) continue;
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
    candidate(PktNumSpace::Handshake, handshakeProbePending());
    candidate(PktNumSpace::AppData,
      m_linkTx.txSpaceDiscarded[PktNumSpace::Handshake] && appProbePending());
    return have;
  }

  ZuTime ptoDeadline(PktNumSpace::T level) const {
    ZuTime t = m_linkTx.txPkts[level].latestAckSentTime();
    if (!*t) t = Zm::now();
    ZuTime delay = level == PktNumSpace::AppData ? peerMaxAckDelay() : ZuTime{0};
    ZuTime out = t + m_linkTx.ptoBackoff.timeout(m_linkTx.rtt, delay);
    if (level == PktNumSpace::Initial && initProbePending() &&
	*m_linkTx.txInitProbeOut && m_linkTx.txInitProbeOut > out)
      out = m_linkTx.txInitProbeOut;
    if (level == PktNumSpace::Handshake && handshakeProbePending() &&
	*m_linkTx.txHandshakeProbeOut && m_linkTx.txHandshakeProbeOut > out)
      out = m_linkTx.txHandshakeProbeOut;
    if (level == PktNumSpace::AppData && appProbePending() &&
	*m_linkTx.txAppProbeOut && m_linkTx.txAppProbeOut > out)
      out = m_linkTx.txAppProbeOut;
    return out;
  }

  bool initProbePending() const {
    if (m_linkTx.txSpaceDiscarded[PktNumSpace::Initial]) return false;
    if (m_linkTx.txCryptoUnackd[PktNumSpace::Initial].count_()) return false;
    const auto &tx = m_linkTx.txPkts[PktNumSpace::Initial];
    return !tx.ackElicitingInFlight() && !tx.retransmitPending();
  }

  bool handshakeProbePending() const {
    if (m_linkTx.txSpaceDiscarded[PktNumSpace::Handshake]) return false;
    if (!txSecretInstalled_(PktNumSpace::Handshake)) return false;
    const auto &ack = m_linkTx.txAck[PktNumSpace::Handshake];
    if (ack.ranges && ack.due) return true;
    if (m_linkTx.rxAppSecret) return false;
    const auto &tx = m_linkTx.txPkts[PktNumSpace::Handshake];
    return !tx.ackElicitingInFlight() && !tx.retransmitPending();
  }

  bool appProbePending() const {
    if (m_linkTx.txSpaceDiscarded[PktNumSpace::AppData]) return false;
    if (!txSecretInstalled_(PktNumSpace::AppData)) return false;
    const auto &tx = m_linkTx.txPkts[PktNumSpace::AppData];
    return !tx.ackElicitingInFlight() && !tx.retransmitPending();
  }

  ZuTime &fallbackProbeOut(PktNumSpace::T level) {
    switch (level) {
      case PktNumSpace::Handshake: return m_linkTx.txHandshakeProbeOut;
      case PktNumSpace::AppData: return m_linkTx.txAppProbeOut;
      default: return m_linkTx.txInitProbeOut;
    }
  }

  // Fallback probes restart pre-establishment recovery when no ack-eliciting
  // packet remains in flight to clock Initial/Handshake PTO progress.
  enum { PTOFallbackProbeDelayNS = 100000000 };
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
    return LinkState::T(m_linkShared.publicLinkState.load_());
  }
  void initLinkState(LinkState::T state) {
    auto oldState = telState(m_linkRx.rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_linkRx.rxLinkState = state;
    m_linkTx.txLinkState = state;
    m_linkShared.publicLinkState = unsigned(state);
  }
  void linkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC lifecycle transition outside Rx thread", return);
    auto oldState = telState(m_linkRx.rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_linkRx.rxLinkState = state;
    m_linkShared.publicLinkState = unsigned(state);
  }
  LinkStateSnapshot linkSnapshotRx() const {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC lifecycle snapshot outside Rx thread", return {});
    return LinkStateSnapshot{
      .error = m_linkRx.rxAppClose.error,
      .state = m_linkRx.rxLinkState,
      .closed = m_linkRx.rxAppClose.closed,
      .application = m_linkRx.rxAppClose.application};
  }
  LinkStateSnapshot linkSnapshotTx() const {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC lifecycle snapshot outside Tx thread", return {});
    return LinkStateSnapshot{
      .error = m_linkTx.txAppClose.error,
      .state = m_linkTx.txLinkState,
      .closed = m_linkTx.txAppClose.closed,
      .application = m_linkTx.txAppClose.application};
  }
  void applyLinkSnapshotTx(LinkStateSnapshot snapshot) {
    ZiAssert(txInvoked_(), "Zquic", (),
      "QUIC lifecycle snapshot apply outside Tx thread", return);
    if (snapshot.state < m_linkTx.txLinkState) return;
    m_linkTx.txAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_linkTx.txLinkState = snapshot.state;
  }
  void publishLinkSnapshotTx() {
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting_()) return;
      link->applyLinkSnapshotTx(snapshot);
    });
  }
  void logLinkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC lifecycle log outside Rx thread", return);
    ZquicLOG(app()->qlogTrace(), ([
      oldState = m_linkRx.rxLinkState,
      newState = state,
      linkInfo = linkInfo_()
    ](auto &o, ZuTime time) {
      CxnStateEvt event{
	.linkInfo = linkInfo,
	.oldState = oldState,
	.newState = newState};
      o.logCxnStateUpd(event, time);
    }));
  }
  void resetLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC lifecycle reset outside Rx thread", return);
    m_linkRx.rxAppClose = {};
    m_linkShared.publicCloseError = 0;
    linkStateRx(LinkState::Starting);
    m_linkRx.suspiciousStreamFrames = 0;
    m_linkRx.suspiciousStreamClosed = false;
  }

protected:
  bool startHandshake() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC handshake transition outside Rx thread", return false);
    if (m_linkRx.rxLinkState != LinkState::Starting) return false;
    logLinkStateRx(LinkState::Handshaking);
    linkStateRx(LinkState::Handshaking);
    publishLinkSnapshotTx();
    return true;
  }

private:
  bool establishState() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC establish transition outside Rx thread", return false);
    if (m_linkRx.rxLinkState != LinkState::Handshaking) return false;
    logLinkStateRx(LinkState::Established);
    linkStateRx(LinkState::Established);
    publishLinkSnapshotTx();
    return true;
  }

  bool closeLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC close transition outside Rx thread", return false);
    if (m_linkRx.rxLinkState >= LinkState::Closing) return false;
    logLinkStateRx(LinkState::Closing);
    linkStateRx(LinkState::Closing);
    return true;
  }
  bool drainLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC drain transition outside Rx thread", return false);
    if (m_linkRx.rxLinkState >= LinkState::Draining) return false;
    logLinkStateRx(LinkState::Draining);
    linkStateRx(LinkState::Draining);
    return true;
  }
  void closedLinkStateRx() {
    ZiAssert(rxInvoked_(), "Zquic", (),
      "QUIC closed transition outside Rx thread", return);
    if (m_linkRx.rxLinkState == LinkState::Closed) return;
    logLinkStateRx(LinkState::Closed);
    linkStateRx(LinkState::Closed);
  }

protected:
  uint64_t txQueueCount_() const {
    uint64_t count = 0;
    ZuUnroll::all<PktNumSpace::N>([this, &count](auto I) {
      count += m_linkTx.txPkts[I()].count();
    });
    return count;
  }

  template <typename L> void ownerLayout(L l) const {
    l(&m_config, sizeof(m_config));
    l(&m_linkShared, sizeof(m_linkShared));
    l(&m_linkRx, sizeof(m_linkRx));
    l(&m_linkTx, sizeof(m_linkTx));
    l(&m_linkRx.rxDiag, sizeof(m_linkRx.rxDiag));
    l(&m_linkTx.txDiag, sizeof(m_linkTx.txDiag));
    l(&m_timers, sizeof(m_timers));
    l(&m_rxAckTimerStorage, sizeof(m_rxAckTimerStorage));
    l(&m_ackRing.producer, sizeof(m_ackRing.producer));
    l(&m_ackRing.consumer, sizeof(m_ackRing.consumer));
    l(&m_peerAckRing.producer, sizeof(m_peerAckRing.producer));
    l(&m_peerAckRing.consumer, sizeof(m_peerAckRing.consumer));
    l(&m_creditRxPub, sizeof(m_creditRxPub));
    l(&m_creditTxPub, sizeof(m_creditTxPub));
    l(&m_rxPathPub, sizeof(m_rxPathPub));
    l(&m_capacity, sizeof(m_capacity));
    l(&m_capacity.wait, sizeof(m_capacity.wait));
    l(&m_pressure, sizeof(m_pressure));
    for (const auto &slot : m_ackRing.slots) l(&slot, sizeof(slot));
    for (const auto &slot : m_peerAckRing.slots) l(&slot, sizeof(slot));
    for (const auto &slot : m_ackCompletion) l(&slot, sizeof(slot));
    for (const auto &slot : m_ackPost) l(&slot, sizeof(slot));
  }

private:
  ZuID streamQueueID(int64_t id) const {
    return ZuID{} << m_config.queueID << ":stream:" << ZuBox<int64_t>{id};
  }
  void initQueues_() {
    const auto &id = m_config.queueID;
    m_linkRx.discFns.initQueue(app(), id, "disconnect", Ztc::QueueType::Rx, 0);
    m_ackRing.initQueue(app(), id, "ack:publication", Ztc::QueueType::Rx,
      AckPubSlots);
    m_peerAckRing.initQueue(app(), id, "peer-ack:publication", Ztc::QueueType::Rx,
      AckPubSlots);
    m_linkTx.peerAckPending.initQueue(app(), id, "peer-ack:deferred", Ztc::QueueType::Tx,
      0);
    m_linkTx.streamQueue.initQueue(app(), id, "writable", Ztc::QueueType::Tx, 0);
    m_linkTx.pathResponses.initQueue(app(), id, "path-response", Ztc::QueueType::Tx,
      PathResponseMax);
    m_linkTx.newCxnIDControls.initQueue(app(), id, "new-cid", Ztc::QueueType::Tx,
      LocalCIDLimit);
    for (unsigned i = 0; i < PktNumSpace::N; ++i) {
      Zquic_::QueueString prefix;
      prefix << id << ":ack:" << PktNumSpace::Map::v2s(i);
      m_ackPost[i].initQueue(app(), prefix, "mailbox", Ztc::QueueType::Rx, 0);
      prefix.length(0);
      prefix << id << ":pkt:" << PktNumSpace::Map::v2s(i);
      m_linkTx.txPkts[i].initQueues(app(), prefix);
      prefix.length(0);
      prefix << id << ":crypto:" << PktNumSpace::Map::v2s(i);
      m_linkRx.rxCrypto[i].initRxQueue(app(), prefix);
      m_linkTx.txCrypto[i].initTxQueue(app(), prefix);
      m_linkTx.txCryptoUnackd[i].initQueue(
	app(), prefix, "unackd", Ztc::QueueType::Tx, 0);
    }
  }

  void rxQueueTelemetry(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_config.id;
    data.inBytes = m_linkRx.rxDiag.bytesRx;
    data.outBytes = 0;
    data.inCount = m_linkRx.rxDiag.datagramsRx;
    data.outCount = 0;
    data.count = 0;
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Rx;
  }

  void txQueueTelemetry(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_config.id;
    data.inBytes = 0;
    data.outBytes = m_linkTx.txDiag.bytesTx;
    data.inCount = 0;
    data.outCount = m_linkTx.txDiag.packetsTx;
    data.count = txQueueCount_();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Tx;
  }

  // immutable
  struct alignas(Zm::CacheLineSize) Config {
    App			*app = nullptr;
    const ZuID		id;
    const ZuID		queueID;
    bool			isServer = false;
  } m_config;

  // shared
  using SharedState = Zquic_::LinkShared;
  SharedState m_linkShared;
  ZuArray<AckCompletion, PktNumSpace::N>
			m_ackCompletion =
			  ZuArray<AckCompletion, PktNumSpace::N>(PktNumSpace::N);
  Zquic_::PublicationQueue<AckPubSlot, AckRxPub, AckTxPub, AckPubSlots>
			m_ackRing;
  RxPathPub		m_rxPathPub;
  Zquic_::PublicationQueue<PeerAckSlot, PeerAckRxPub, PeerAckTxPub, AckPubSlots>
			m_peerAckRing;
  using CreditRxPub = Zquic_::CreditRxPub;
  CreditRxPub m_creditRxPub;
  using CreditTxPub = Zquic_::CreditTxPub;
  CreditTxPub m_creditTxPub;
  ZuArray<AckPost, PktNumSpace::N>
			m_ackPost = ZuArray<AckPost, PktNumSpace::N>(PktNumSpace::N);
  using RxAckTimer = Zquic_::RxAckTimer;
  RxAckTimer m_rxAckTimerStorage;

  using TimerSlot = Zquic_::TimerSlot;
  using TimerStorage = Zquic_::TimerStorage;
  TimerStorage m_timers;

  Zquic_::QueueCatalog m_streamQueues;

  // Rx thread exclusive
  struct alignas(Zm::CacheLineSize) RxStorage {
    Zquic_::LinkDiscFns discFns;
    ZmRef<StreamCloseWork> discWork;
    uint64_t creditSequence = 0;
    uint64_t creditPosted = 0;
    LinkCloseState	rxAppClose;
    bool			statelessReset = false;
    FlowCredit		rxDataCredit;
    uint64_t		rxDataRetired = 0;
    ZuArray<StreamLimit, Zquic::StreamType::N>
			localLimit = {
      StreamLimit(MaxStreamCount),
      StreamLimit(MaxStreamCount)
    };
    StreamsRef		streams{new Streams};
    RxStreams		allStreams;
    RxStreams::Node	*closeCursor = nullptr;
    bool			closePending = false;
    ZuArray<uint64_t, StreamClassCount>
			closedStreamBase =
			  fixedArray<uint64_t, StreamClassCount>();
    ZuArray<bool, StreamClassCount> closedStreamPending =
      fixedArray<bool, StreamClassCount>();
    ClosedStreamsRef	closedStreams{new ClosedStreams};
    Zquic_::ClosedStreamList closedStreamList;

    Zquic_::LinkRxStats	rxDiag;
    Crypto		crypto;
    TransportParams	transportParams;
    ZuArray<CryptoStream, PktNumSpace::N>
			rxCrypto =
			  ZuArray<CryptoStream, PktNumSpace::N>(
			    PktNumSpace::N);
    ZuArray<ZiSockAddr, PktNumSpace::N>
			rxCryptoAddr =
			  ZuArray<ZiSockAddr, PktNumSpace::N>(
			    PktNumSpace::N);
    CxnID			initialDCID;
    CxnID			origDCID;
    CxnID			groupID;
    CxnID			localSCID;
    CxnID			peerCID;
    ResetToken		peerResetToken;
    LocalCIDs		localCIDs;
    PeerCIDs		peerCIDs;
    ZuArray<uint64_t, PktNumSpace::N>
			rxLargestPN =
			  fixedArray<uint64_t, PktNumSpace::N>();
    AckManager		rxAcks;
    ZuArray<uint64_t, PktNumSpace::N>
			rxAckMinGen = {1, 1, 1};
    ZuArray<uint64_t, PktNumSpace::N>
			rxAckRetired = fixedArray<uint64_t, PktNumSpace::N>();
    uint64_t		rxAckSeq = 0;
    uint64_t		rxAckEpoch = 0;
    uint64_t		rxAckFallbackSeq = 0;
    uint64_t		rxAckKeyGen = 0;
    ZiSockAddr		rxAckAddr;
    uint64_t		rxAckNotifyGen = 0;
    uint64_t		rxAckTimerOut = 0;
    uint64_t		rxAckTimerSerial = 0;
    bool			rxAckFast = false;
    bool			rxAckResume = false;
    ZiSockAddr		rxPathRemote;
    uint64_t		rxPathBytes = 0;
    uint64_t		rxRuntimeGen = 0;
    uint64_t		rxPeerAckSeq = 0;
    uint64_t		rxPeerAckFallback = 0;
    uint64_t		rxPeerAckFirst = 0;
    uint64_t		rxPeerAckLargest = 0;
    uint64_t		rxPeerAckCE = 0;
    uint64_t		rxPeerAckPosted = 0;
    bool			rxPeerAckSeen = false;
    RxEarlyData		rxEarlyData;
    TransportParams	zeroRTTParams;
    bool			zeroRTTParamsSet = false;
    TrafficSecret		rxOldSecret;
    PktProtState		rxOldProt;
    TrafficSecret		rxNextSecret;
    PktProtState		rxNextProt;
    ZuArray<bool, PktNumSpace::N>
			rxSpaceDiscarded =
			  fixedArray<bool, PktNumSpace::N>();
    LinkState::T		rxLinkState = LinkState::Closed;
    uint64_t		peerRetirePriorTo = 0;
    unsigned		suspiciousStreamFrames = 0;
    bool			suspiciousStreamClosed = false;
    bool			rxKeyPhase = false;
    bool			rxOldKeyPhase = false;
    uint64_t		rxKeyGeneration = 0;
  } m_linkRx;

  // Tx thread exclusive
  Zquic_::LinkTxStorage m_linkTx;

  using CapacityStorage = Zquic_::LinkCapacity;
  CapacityStorage m_capacity;
  using PressureStorage = Zquic_::LinkPressure;
  PressureStorage m_pressure;


};

} // namespace Zquic
