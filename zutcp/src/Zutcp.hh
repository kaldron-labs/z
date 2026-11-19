//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// User-mode TCP plugin for ZiMultiplex.

#ifndef Zutcp_HH
#define Zutcp_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcMx.hh>

#include <zlib/Zxdp.hh>

#include <zlib/ZutcpAlloc.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/ZutcpISN.hh>
#include <zlib/ZutcpOptions.hh>
#include <zlib/ZutcpQueue.hh>
#include <zlib/ZutcpTFO.hh>

namespace Zutcp_ {
  class CliCxn;
  class SrvCxn;
  class Listener;
}
namespace ZutcpTest { class Instance; }

namespace Zutcp {

struct Datagram;

using PacketFn = ZmFn<void(ZmRef<Zi::IOBuf>), ZmFnHeapID<"Zutcp.Packet">>;

enum { DefaultMaxPayload = 1200 }; // configured cap, not a wire floor
enum { BufSize = 1472 };

inline constexpr uint64_t DefaultMaxData = 16<<20; // 16M
enum { DefaultMaxCxns = 1024, DefaultMaxPendingCxns = 128 };
enum { DefaultConnectTimeout = 30 };
enum { DefaultUserTimeout = 120 };
enum { DefaultCloseDrainTimeout = 30, DefaultFinWaitTimeout = 60 };
enum { DefaultClosedTupleRingSize = 4096 };

struct RtoParams {
  ZuTime	initial{1};
  ZuTime	min{0, 200000000};
  ZuTime	max{60};
  ZuTime	granularity{0, 1000000};

  bool operator ==(const RtoParams &) const = default;

  bool valid() const {
    return initial > ZuTime{0} && min > ZuTime{0} && max >= min &&
      initial >= min && initial <= max && granularity > ZuTime{0};
  }
};

struct InstanceParams {
  PacketFn		packet;
  unsigned		packetCount = ZiIOBuf_DefltCount;

  unsigned		controlBufCount = DefaultControlBufCount;
  unsigned		txControlCredits = DefaultTxControlCredits;
  unsigned		rxRetainFramesPerCxn = DefaultRxRetainPerCxn;
  uint64_t		maxData = DefaultMaxData;
  unsigned		maxCxns = DefaultMaxCxns, maxPendingCxns = DefaultMaxPendingCxns;
  unsigned		closedTupleRingSize = DefaultClosedTupleRingSize;
  unsigned		tfoCookieCacheSize = DefaultTFOCookieCacheSize;

  ZuTime		connectTimeout{DefaultConnectTimeout};
  ZuTime		userTimeout{DefaultUserTimeout};
  ZuTime		closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime		finWaitTimeout{DefaultFinWaitTimeout};
  ZuTime		ackHoldTimeout{0, 25000000};
  bool			congestionControl = true;
  uint64_t		maxIdleTimeout = 0;
  ZuTime		heartBeat;
  unsigned		maxPayload = DefaultMaxPayload;
  RtoParams		rtoParams;
  ZuTime		tlpPeerAckDelay{0, 200000000};

  bool			sackPermitted = true, windowScale = true, timestamps = true, ecn = false;

  bool operator ==(const InstanceParams &) const = default;
};

// Shared endpoint data lives in the client/listener's intrusive binding node.
// Instance indexes those nodes without owning or retaining the endpoints.
struct Endpoint;
using EndpointFn = void (*)(
  Endpoint *, ZmRef<Zi::IOBuf>, ZiSockAddr, ZiSockAddr, unsigned, const TCPHeader &);
struct Endpoint {
  ZiSockAddr	bindAddr;
  EndpointFn	receive = nullptr;
};

inline const ZiSockAddr &endpointKey(const Endpoint &endpoint) { return endpoint.bindAddr; }

ZmHashDerive(EndpointIndex, Endpoint,
  (ZmHashNode<Endpoint,
    ZmHashKey<endpointKey,
      ZmHashShadow<ZmHashID<"Zutcp.Endpoints", ZmHashHeapID<"">>>>>));

// Tx-owned runtime snapshot, or cold control access while stopped.
struct TxAdmission {
  uint64_t	capacity = 0, dataStalls = 0, controlStalls = 0;
  unsigned	reserved = 0, outstanding = 0, controlCount = 0;
};

inline bool positiveTimeout(ZuTime v)
{
  return *v && v > ZuTime{0} && unsigned(v.nsec()) < 1000000000;
}

// Rx-owned admission snapshot, or cold control access while stopped.
struct CxnAdmission {
  uint64_t	refused = 0;
  unsigned	total = 0, pending = 0, closing = 0;
  unsigned	maxCxns = DefaultMaxCxns, maxPendingCxns = DefaultMaxPendingCxns;
};

using ClosedTupleKey = ZuTuple<ZiSockAddr, ZiSockAddr>; // local, remote

// One identity and ownership membership for every native TCP connection.
// Instance mutates membership/publication on Rx under m_cxnsLock; telemetry
// takes lifetime references under that lock before invoking callbacks.
struct ZutcpAPI Cxn : public ZmPolymorph {
  enum { Admitted = 1, Pending = 2, Closing = 4 };

  Cxn			*ownerNext = nullptr;
  Cxn			**ownerPrev = nullptr;
  ZiConnection		*mxCxn = nullptr;
  bool			retiring = false;
  uint8_t			admission = 0; // Rx-owned, independent of list membership

  ~Cxn() override { ZmAssert_(!admission); }

  virtual void abortRx() = 0;
  virtual ClosedTupleKey closedKeyRx() const = 0;
  virtual bool established() const = 0;
  virtual const Zutcp_::Listener *listener() const { return nullptr; }
};

// Interface/routing scope belongs to the containing Instance. No packet or
// connection reference survives in a closed record. Copies cross owners.
struct ClosedTuple {
  ClosedTupleKey	key;
  uint64_t		id = 0;
  ZuTime		tsRecentTime;
  uint32_t		txNext = 0, rxNext = 0, tsRecent = 0, lastSentTS = 0;
  bool			timestamps = false, fin = false;
};

inline const ClosedTupleKey &closedTupleKey(const ClosedTuple &record) { return record.key; }

inline bool reuseSequence(uint32_t candidate, uint32_t previous)
{
  uint32_t distance = candidate - previous;
  return distance && distance < 0x80000000U;
}

inline bool reuseCxnTuple(const ClosedTuple &record, uint32_t localISN, uint32_t peerISN)
{
  // Records contain NEXT wire positions. Incomplete peer history cannot bound
  // an old out-of-order tail; timestamp-only eligibility belongs to Phase 5.
  return record.fin && reuseSequence(localISN, uint32_t(record.txNext - 1)) &&
    reuseSequence(peerISN, uint32_t(record.rxNext - 1));
}

// Timestamp evidence augments the remote direction only. Complete peer FIN
// and the separately selected local sequence proof remain caller requirements.
inline bool reusePeerTuple(
  const ClosedTuple &record, const TCPHeader &syn, bool timestamps, ZuTime now)
{
  if (!record.fin) return false;
  bool newerSequence = reuseSequence(syn.seq, uint32_t(record.rxNext - 1));
  bool proposed = timestamps && (syn.options.flags & TCPOptions::TS);
  if (!record.timestamps) return proposed || newerSequence;
  if (!proposed) return newerSequence;
  bool recent = *record.tsRecentTime && (now < record.tsRecentTime ||
    now - record.tsRecentTime <= ZuTime{24*24*60*60});
  if (!recent) return newerSequence;
  uint32_t distance = syn.options.tsval - record.tsRecent;
  return (distance && distance < uint32_t(0x80000000)) || (!distance && newerSequence);
}

struct ClosedTupleStats {
  unsigned	capacity = 0, count = 0;
  uint64_t	inserted = 0, evicted = 0;
};

// Rx-owned fixed slot ring. The intrusive FIFO preserves closure order while
// the hash indexes those same slots. Evidence updates never refresh FIFO order.
class ZutcpAPI ClosedTuples : public QueueTelemetry<"closed-tuples", Ztc::QueueType::Rx> {
  ZmHashDerive(IndexBase, ClosedTuple,
    (ZmHashNode<ClosedTuple,
      ZmHashKey<closedTupleKey,
	ZmHashShadow<ZmHashLock<ZmNoLock, ZmHashHeapID<"">>>>>));
  struct Index : public ZmNHeap<Index>, public IndexBase {
  friend inline ZuStringT<"Zutcp.Closed.Index"> ZmNHeapID(Index *);
    using ZmNHeap<Index>::operator new;
    using ZmNHeap<Index>::operator delete;
    using IndexBase::IndexBase;
  };
  ZmListDerive(Order, Index::Node,
    ZmListNode<Index::Node, ZmListKey<closedTupleKey, ZmListShadow<>>>);
  using Slots = ZtArray<Order::Node, ZtArrayHeapID<"Zutcp.Closed.Slots">>;

public:
  explicit ClosedTuples(unsigned capacity = DefaultClosedTupleRingSize);
  ClosedTuples(const ClosedTuples &) = delete;
  ClosedTuples &operator =(const ClosedTuples &) = delete;
  ClosedTuple remember(ClosedTuple);
  bool find(const ClosedTupleKey &, ClosedTuple &) const;
  bool update(const ClosedTuple &); // matching record identity only
  void resize(unsigned); // stopped, preserve newest records in order
  void final();
  ClosedTupleStats stats() const;
  void telemetry(Ztc::QueueTelemetry &data) const override
  {
    queueTelemetry(data);
    data.count = m_order.count_();
    data.size = m_slots.length();
    data.inCount = m_inserted;
    data.outCount = m_evicted;
    data.full = m_full;
  }

private:
  void append(const ClosedTuple &);

  Slots		m_slots;
  ZmRef<Index>	m_index;
  Order		m_order;
  uint64_t	m_nextID = 0, m_inserted = 0, m_evicted = 0;
  unsigned	m_full = 0;
};

struct ClientPorts {
  enum { First = 49152, End = 65536, Count = End - First };
};
ZtEnumStruct(ZutcpAPI, CxnBindError, int8_t, None, Invalid, InUse, Retained, Exhausted);

// Cold binding policy shared by Instance and deterministic storage tests. Mutation
// in production is confined to Instance's Rx boundary; this owns no extra registry.
ZutcpAPI CxnBindError::T bindCxnTuple(
  EndpointIndex::Node &, const ZiSockAddr &remote, unsigned &nextPort,
  EndpointIndex &, const ClosedTuples &history);

// Scalar timestamp clock; Instance samples it only on Tx using the existing clock.
// A backward wall step adjusts the logical offset without freezing later ticks.
class TCPClock {
public:
  bool sample(ZuTime now, uint32_t &out)
  {
    if (!*now || now < ZuTime{0}) return false;
    uint64_t wall = uint64_t(now.microsecs()) / 1000;
    if (!m_known) { m_ticks = wall; m_known = true; }
    else if (wall >= m_wall) m_ticks += wall - m_wall;
    m_wall = wall;
    out = uint32_t(m_ticks);
    return true;
  }

private:
  uint64_t	m_wall = 0, m_ticks = 0;
  bool		m_known = false;
};

class ZutcpAPI Instance : public ZmNHeap<Instance>, public ZiMxPlugin,
  private ZmEngine<Instance> {
friend inline ZuStringT<"Zutcp.Instance"> ZmNHeapID(Instance *);
friend class ZutcpTest::Instance;
friend class ZmEngine<Instance>;
friend class Zutcp_::CliCxn;
friend class Zutcp_::SrvCxn;
friend class Zutcp_::Listener;
  using Engine = ZmEngine<Instance>;

public:
  using Engine::state;
  using Engine::running;
  using Engine::stopping;
  using Params = InstanceParams;

  Instance();
  ~Instance() override;

  // Takes ownership of the initialized XDP instance on the first successful call.
  ZuUnion<void, ZeException> init(ZiMultiplex *, ZmRef<Zxdp::Instance> &&, Params params);
  void rx() override;
  void wake() override;

  // Native phase-1 entry: complete contiguous Ethernet packet on mx Tx.
  // Acceptance detaches backing; refusal leaves the buffer unchanged.
  bool sendPacket(const ZmRef<Zi::IOBuf> &, PacketLease * = nullptr);
  void reconcileTx(); // Tx owner: retire kernel leases before retained header changes
  ZmRef<PacketBuf> allocPacket(void *owner, ZiIPType::T, bool control = false);

  ZuTime transportNow() const { return Zm::now(); }
  bool ioActive() const { return state() != ZmEngineState::Stopped; }
  unsigned rxThread() const { return mx()->rxThread(); }
  unsigned txThread() const { return mx()->txThread(); }
  bool rxInvoked() const { return mx()->invoked(rxThread()); }
  bool txInvoked() const { return mx()->invoked(txThread()); }

  template <typename ...Args> void rxRun(Args &&...args)
  {
    mx()->run(ZuFwd<Args>(args)..., rxThread());
  }

  template <typename ...Args> void rxInvoke(Args &&...args)
  {
    mx()->invoke(ZuFwd<Args>(args)..., rxThread());
  }

  template <typename ...Args> void txRun(Args &&...args)
  {
    mx()->run(ZuFwd<Args>(args)..., txThread());
  }

  template <typename ...Args> void txInvoke(Args &&...args)
  {
    mx()->invoke(ZuFwd<Args>(args)..., txThread());
  }

  template <typename L> void txPost(L &&fn)
  {
    ZmAssert_(txInvoked());
    mx()->push(ZuFwd<L>(fn), txThread());
  }

  bool sackPermitted() const { return m_params.sackPermitted; }
  bool windowScale() const { return m_params.windowScale; }
  bool timestamps() const { return m_params.timestamps; }
  bool ecn() const { return m_params.ecn && m_params.congestionControl; }
  bool congestionControl() const { return m_params.congestionControl; }
  uint64_t maxData() const { return m_params.maxData; }
  unsigned maxCxns() const { return m_params.maxCxns; }
  unsigned maxPendingCxns() const { return m_params.maxPendingCxns; }
  unsigned closedTupleRingSize() const { return m_params.closedTupleRingSize; }
  unsigned tfoCookieCacheSize() const { return m_params.tfoCookieCacheSize; }
  ZuTime connectTimeout() const { return m_params.connectTimeout; }
  ZuTime userTimeout() const { return m_params.userTimeout; }
  ZuTime closeDrainTimeout() const { return m_params.closeDrainTimeout; }
  ZuTime finWaitTimeout() const { return m_params.finWaitTimeout; }
  uint64_t maxIdleTimeout() const { return m_params.maxIdleTimeout; }
  ZuTime heartBeat() const { return m_params.heartBeat; }
  unsigned maxPayload() const { return m_params.maxPayload; }
  unsigned controlBufCount() const { return m_params.controlBufCount; }
  unsigned txControlCredits() const { return m_params.txControlCredits; }
  unsigned rxRetainFramesPerCxn() const { return m_params.rxRetainFramesPerCxn; }
  const RtoParams &rtoParams() const { return m_params.rtoParams; }
  ZuTime tlpPeerAckDelay() const { return m_params.tlpPeerAckDelay; }
  ZuTime ackHoldTimeout() const { return m_params.ackHoldTimeout; }

  bool retainRx(ZiIOBuf *packet, Zi::IOQueue &retained)
  {
    if (retained.headPtr() == packet) return true;
    retained.unshiftNode(packet);
    while (retained.count_() > m_params.rxRetainFramesPerCxn) retained.pop();
    return true;
  }

  Zi::IOBufPool *txPool(Zi::IOBufAllocator *allocator) const
  {
    if (!allocator || allocator->mx != mx() || allocator->type != Zi::IOBufType::TxTCP)
      return nullptr;
    return allocator->pool.ptr();
  }

  void reportError(ZeException);
  CxnAdmission cxnAdmission() const;
  bool reserveCxn(); // before constructing an owner
  void cancelCxn(); // release a reservation that acquired no owner
  void admitCxn(Cxn &); // attach an existing reservation
  void establishedCxn(Cxn &);
  void closingCxn(Cxn &);
  void releaseCxn(Cxn &);

  bool chooseISN(const ZiSockAddr &, const ZiSockAddr &, uint32_t &) const;
  bool tfoCookie(const ZiIP &, const ZiSockAddr &, TFOCookie &) const;
  void saveTFOCookie(const ZiIP &, const ZiSockAddr &, const TFOCookie &);
  bool issueTFO(const ZiSockAddr &, const ZiIP &, TFOCookie &) const;
  bool validTFO(const ZiSockAddr &, const ZiIP &, const TFOCookie &) const;

  // Live owners qualify their current window/PAWS first, then offer stale input.
  bool recoverClosed(const Datagram &);
  void resetUnmatched(const Datagram &);
  bool tcpTimestamp(ZuTime, uint32_t &);
  ClosedTuple rememberClosed(ClosedTuple);
  bool closedTuple(const ClosedTupleKey &, ClosedTuple &) const;
  bool updateClosed(const ClosedTuple &);
  ClosedTupleStats closedTupleStats() const;

  TxAdmission txAdmission() const;
  bool ready() const;
  unsigned mtu() const { return m_mtu; }
  Zi::IOBufPool *control() const { return m_control; }
  Zi::IOBufAllocator *dataRxAllocator() const;
  Zi::IOBufAllocator *dataTxAllocator() { return &m_allocators.p<1>().data; }

  // Rx-owned native endpoint registry. Port zero selects an unused local port.
  // Exact local addresses take precedence over a same-family wildcard.
  ZuUnion<void, ZeException> bind(EndpointIndex::Node &, const ZiSockAddr &remote = {});
  bool unbind(EndpointIndex::Node &);
  // Rx-owned, direct route/static-neighbor lookup before protocol setup.
  ZuUnion<IPPath, ZeException> resolve(ZiIP remote, ZiIP local = {});

  // Zi connections share the existing native transport owners.
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions) override;
  void stopListening(ZiIP, uint16_t) override;
  void connect(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override;
  void udp(ZiConnectFn, ZiFailFn, ZiIPType::T, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions) override;
  void recv(ZiConnection *) override;
  void send(ZiConnection *) override;
  void disconnect(ZiConnection *, bool) override;
  void close(ZiConnection *, bool) override;

  TelKey telKey() const override { return {mx()->id()}; }
  void telemetry(Ztc::MxTelemetry &) const override;
  unsigned allCxns(AllCxnsFn) const override;
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override;
  void watch(AddCxnFn, DelCxnFn) override;
  void unwatch() override;

private:
  Zi::IOBufPool *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override;
  bool deferredBacking() const override { return true; }
  AllocResult alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override;
  void start() override;
  void stop() override;
  void final() override;

  enum { Down, Up, Closing }; // physical I/O publication, not the control FSM
  enum { DrainIdle, DrainQueued, DrainActive };

  Zi::IOBufPool *ingress(unsigned count, unsigned align);
  template <typename L> bool spawn(L &&fn) {
    mx()->rxRun(ZuFwd<L>(fn));
    return true;
  }
  void start_();
  void stop_();
  void stopTx();
  void stopEndpoints();
  void received(uint8_t *, unsigned);
  void rxProgress();
  void receivedUDP(uint8_t *, unsigned, const IPView &);
  void sendUDP(ZiConnection *);
  void closeUDP(ZiConnection *, bool);
  bool closedPacket(const uint8_t *, const IPView &, const TCPView &);
  void replyClosed(ClosedTuple, IPPath);
  void resetReply(const TCPHeader &, unsigned, IPPath);
  void replyControl(TCPHeader, IPPath);
  void pollTx();
  void txService(bool);
  void addCxn(Cxn *);
  void delCxn(Cxn *);
  void delListener(Zutcp_::Listener *);
  void stopCxns();
  void stopLinks(const Zutcp_::Listener *, bool);
  bool tupleClaimed(const Zutcp_::Listener *, const ClosedTupleKey &) const;
  void cxnsDrained();
  void cxnAdded() { ++m_cxnObjects; }
  void cxnRetired(ZiFailFn = {}, bool = false);
  template <typename C> bool publish(C *, ZiConnectFn &, ZiCxnInfo);
  template <typename C, typename L> void retire(C *, bool, L &&);
  template <typename L> static void visit(ZiConnection *, L &&);

  struct UdpBinding { unsigned portKey = 0; };
  static unsigned udpPortKey(const UdpBinding &binding) { return binding.portKey; }
  ZmHashDerive(UdpBindings, UdpBinding,
    (ZmHashNode<UdpBinding, ZmHashKey<udpPortKey,
      ZmHashShadow<ZmHashID<"Zutcp.UDP.Bindings", ZmHashHeapID<"">>>>>));
  class UdpCxn;
  bool publishUDP(UdpCxn *, ZiConnectFn &, ZiCxnInfo);
  void retireUDP(UdpCxn *, bool);

  using PacketAllocator = Zi::IOBufPoolAllocator<Zi::IOBufType::Rx>;
  using NativeTxAllocator = Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Pkt.Tx">>;
  using NativeDataAllocator = Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Data.Tx">>;
  using NativeControlAllocator = Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Control.Tx">>;

  struct Allocators {
    NativeTxAllocator		packet;
    NativeDataAllocator		data;
    NativeControlAllocator	control;

    Allocators(ZiMultiplex *mx, Zi::IOBufPool *packets, Zi::IOBufPool *controls) :
      packet{mx, packets}, data{mx, packets}, control{mx, controls} { }
  };

  // Configuration is fixed while running; init creates interface-bound resources.
  // Live secret operations are const after initialization.
  ISN				m_isn;
  TFOKey			m_tfoKey;
  InstanceParams		m_params;
  unsigned			m_mtu = 0, m_ingressCount = 0;
  uint64_t			m_txCapacity = 0;
  unsigned			m_controlCredits = DefaultTxControlCredits;

  ZmRef<Zxdp::Instance>		m_xdp;
  ZmRef<Zi::IOBufPool>		m_ingress;
  ZmRef<Zi::IOBufPool>		m_packets, m_control;
  ZmRef<Zi::IOBufPool>		m_requestedTx;
  ZuUnion<void, PacketAllocator>	m_packetRx;
  ZuUnion<void, Allocators>	m_allocators;

  // One native connection list; pending and retiring entries remain owned.
  mutable ZmRWLock		m_cxnsLock;
  Cxn				*m_cxns = nullptr;
  unsigned			m_cxnCount = 0;
  Zutcp_::Listener		*m_listeners = nullptr;

  mutable ZmRWLock		m_watchLock;
  AddCxnFn			m_addCxn;
  DelCxnFn			m_delCxn;

  // Physical I/O stays published until the endpoint owner drain completes.
  alignas(Zm::CacheLineSize)
  ZmAtomic<unsigned>		m_ioState{Down};
  bool				m_startFailed = false;
  ZeException			m_error;

  // Wake callers write these atomics; keep them off the publication/read line.
  alignas(Zm::CacheLineSize)
  ZmAtomic<unsigned>		m_wakeUsers{0};

  // Rx-owned protocol, endpoint and connection state.
  alignas(Zm::CacheLineSize)
  TFOCache			m_tfoCookies;
  EndpointIndex			m_endpoints;
  UdpBindings			m_udpBindings;
  unsigned			m_nextPort = ClientPorts::First;
  unsigned			m_nextUdpPort = ClientPorts::First;
  uint64_t			m_cxnRefused = 0;
  unsigned			m_cxnTotal = 0, m_cxnPending = 0, m_cxnClosing = 0;
  ClosedTuples			m_closed;
  unsigned			m_cxnObjects = 0, m_cxnRetiring = 0;
  unsigned			m_rxPackets = 0;
  ZmAtomic<unsigned>		m_endpointDrain{DrainIdle};

  // Tx-owned timestamp and completion service.
  alignas(Zm::CacheLineSize)
  TCPClock			m_tcpClock;
  unsigned			m_txPending = 0;
  bool				m_senderStopping = false;
  bool				m_txServicing = false, m_txDraining = false;
  uint64_t			m_dataStalls = 0, m_controlStalls = 0;
};

} // namespace Zutcp

#endif /* Zutcp_HH */
