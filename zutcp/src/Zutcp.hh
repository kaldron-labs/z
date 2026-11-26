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
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zpkt.hh>
#include <zlib/ZpktEnv.hh>

#include <zlib/ZutcpAlloc.hh>
#include <zlib/ZutcpChecksum.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/ZutcpISN.hh>
#include <zlib/ZutcpOptions.hh>
#include <zlib/ZutcpTFO.hh>

namespace Zutcp_ {
  class CliCxn;
  class SrvCxn;
  class Listener;
}
namespace ZutcpTest { class MxPlugin; }

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

struct MxParams {
  PacketFn		packet;
  unsigned		packetCount = ZiIOBuf_DefltCount;

  unsigned		controlBufCount = DefaultControlBufCount;
  unsigned		txRetries = 1; // immediate retries after an idle refusal
  ZuTime		txBackoff{0, 1000000}; // 1ms between subsequent device attempts
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
  bool			txBatch = false;
  uint64_t		maxIdleTimeout = 0;
  ZuTime		heartBeat;
  unsigned		maxPayload = DefaultMaxPayload;
  RtoParams		rtoParams;
  ZuTime		tlpPeerAckDelay{0, 200000000};

  bool			sackPermitted = true, windowScale = true, timestamps = true, ecn = false;

  bool operator ==(const MxParams &) const = default;
};

// Shared endpoint data lives in the client/listener's intrusive binding node.
// MxPlugin indexes those nodes without owning or retaining the endpoints.
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
struct TxBatchResult {
  unsigned	offered = 0, accepted = 0, bytes = 0;
  bool		blocked = false;
};

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
// Rx owns membership and publishes the application connection.
struct ZutcpAPI Cxn : public ZmPolymorph {
  enum { Admitted = 1, Pending = 2, Closing = 4 };

  Cxn			*ownerNext = nullptr;
  Cxn			**ownerPrev = nullptr;
  ZiConnection		*mxCxn = nullptr;
  bool			retiring = false;
  uint8_t		admission = 0; // Rx-owned, independent of list membership

  // Tx-owned retry membership; independent of Rx ownership/publication.
  Cxn			*txWaitNext = nullptr;
  Cxn			**txWaitPrev = nullptr;

  ~Cxn() override { ZmAssert_(!admission && !txWaitPrev); }

  virtual void abortRx() = 0;
  virtual void resumeTx(Zu::IO::Result) { }
  virtual ClosedTupleKey closedKeyRx() const = 0;
  virtual bool established() const = 0;
  virtual const Zutcp_::Listener *listener() const { return nullptr; }
};

// Interface/routing scope belongs to the containing MxPlugin. No packet or
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
class ZutcpAPI ClosedTuples {
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

private:
  void append(const ClosedTuple &);

  Slots		m_slots;
  ZmRef<Index>	m_index;
  Order		m_order;
  uint64_t	m_nextID = 0, m_inserted = 0, m_evicted = 0;
};

struct ClientPorts {
  enum { First = 49152, End = 65536, Count = End - First };
};
ZtEnumStruct(ZutcpAPI, CxnBindError, int8_t, None, Invalid, InUse, Retained, Exhausted);

// Cold binding policy shared by MxPlugin and deterministic storage tests. Mutation
// in production is confined to MxPlugin's Rx boundary; this owns no extra registry.
ZutcpAPI CxnBindError::T bindCxnTuple(
  EndpointIndex::Node &, const ZiSockAddr &remote, unsigned &nextPort,
  EndpointIndex &, const ClosedTuples &history);

// Scalar timestamp clock; MxPlugin samples it only on Tx using the existing clock.
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

ZuDerive(RxAllocator, (Zi::IOBufPoolAllocator<Zi::IOBufType::Rx>));
ZuDerive(TxAllocator, (Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Pkt.Tx">>));
ZuDerive(TxCtrlAllocator, (Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, TCPHeadRoom, ZiIOBuf_Align, ZuStringT<"Zutcp.Control.Tx">>));

class ZutcpAPI MxPlugin :
  public ZmNHeap<MxPlugin>,
  public ZiMxPlugin,
  private ZmEngine<MxPlugin> {
friend inline ZuStringT<"Zutcp.MxPlugin"> ZmNHeapID(MxPlugin *);

friend class ZmEngine<MxPlugin>;

friend class Zutcp_::CliCxn;
friend class Zutcp_::SrvCxn;
friend class Zutcp_::Listener;

friend class ZutcpTest::MxPlugin;

public:
  using Engine = ZmEngine<MxPlugin>;
  using Engine::state;
  using Engine::running;
  using Engine::stopping;
  using Params = MxParams;

  MxPlugin();
  ~MxPlugin() override;
  // Takes ownership of the initialized XDP instance on the first successful call.
  void init(ZiMultiplex *, Zpkt::Env *, ZmRef<Zpkt::Nif> &&, Params params);

  bool ioActive() const { return state() != ZmEngineState::Stopped; }
  bool ready() const;
  void reportError(ZeException);

  unsigned rxThread() const { return mx()->rxThread(); }
  bool rxInvoked() const { return mx()->invoked(rxThread()); }
  template <typename ...Args>
  void rxRun(Args &&...args) {
    mx()->run(ZuFwd<Args>(args)..., rxThread());
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    mx()->invoke(ZuFwd<Args>(args)..., rxThread());
  }

  unsigned txThread() const { return mx()->txThread(); }
  bool txInvoked() const { return mx()->invoked(txThread()); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    mx()->run(ZuFwd<Args>(args)..., txThread());
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    mx()->invoke(ZuFwd<Args>(args)..., txThread());
  }
  template <typename L>
  void txPost(L &&fn) {
    ZmAssert_(txInvoked());
    mx()->push(ZuFwd<L>(fn), txThread());
  }

  void wake() override;
  void rx() override;

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

  bool sackPermitted() const { return m_params.sackPermitted; }
  bool windowScale() const { return m_params.windowScale; }
  bool timestamps() const { return m_params.timestamps; }
  bool ecn() const { return m_params.ecn && m_params.congestionControl; }
  bool congestionControl() const { return m_params.congestionControl; }
  bool txBatch() const { return m_params.txBatch; }

  uint64_t maxData() const { return m_params.maxData; }
  unsigned maxPayload() const { return m_params.maxPayload; }
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

  const RtoParams &rtoParams() const { return m_params.rtoParams; }
  ZuTime tlpPeerAckDelay() const { return m_params.tlpPeerAckDelay; }
  ZuTime ackHoldTimeout() const { return m_params.ackHoldTimeout; }

  unsigned rxRetainFramesPerCxn() const { return m_params.rxRetainFramesPerCxn; }
  unsigned controlBufCount() const { return m_params.controlBufCount; }
  unsigned txControlCredits() const { return m_params.txControlCredits; }
  unsigned txRetries() const { return m_params.txRetries; }
  ZuTime txBackoff() const { return m_params.txBackoff; }

  ZuTime transportNow() const { return Zm::now(); }
  bool tcpTimestamp(ZuTime, uint32_t &);
  bool chooseISN(const ZiSockAddr &, const ZiSockAddr &, uint32_t &) const;

  bool tfoCookie(const ZiIP &, const ZiSockAddr &, TFOCookie &) const;
  void saveTFOCookie(const ZiIP &, const ZiSockAddr &, const TFOCookie &);
  bool issueTFO(const ZiSockAddr &, const ZiIP &, TFOCookie &) const;
  bool validTFO(const ZiSockAddr &, const ZiIP &, const TFOCookie &) const;

  CxnAdmission cxnAdmission() const;
  bool reserveCxn(); // before constructing an owner
  void cancelCxn(); // release a reservation that acquired no owner
  void admitCxn(Cxn &); // attach an existing reservation
  void establishedCxn(Cxn &);
  void closingCxn(Cxn &);
  void releaseCxn(Cxn &);

  // Rx-owned native endpoint registry. Port zero selects an unused local port.
  // Exact local addresses take precedence over a same-family wildcard.
  ZuUnion<void, ZeException> bind(EndpointIndex::Node &, const ZiSockAddr &remote = {});
  bool unbind(EndpointIndex::Node &);
  // Rx-owned, direct route/static-neighbor lookup before protocol setup.
  ZuUnion<IPPath, ZeException> resolve(ZiIP remote, ZiIP local = {});

  ClosedTuple rememberClosed(ClosedTuple);
  bool closedTuple(const ClosedTupleKey &, ClosedTuple &) const;
  bool updateClosed(const ClosedTuple &);
  ClosedTupleStats closedTupleStats() const;
  // Live owners qualify their current window/PAWS first, then offer stale input.
  bool recoverClosed(const Datagram &);
  void resetUnmatched(const Datagram &);

  unsigned mtu() const { return m_mtu; }
  Zi::IOBufPool *txCtrlPool() const { return m_control; }
  Zi::IOBufAllocator *txAllocator() { return &m_allocators.p<1>().tx; }
  Zi::IOBufPool *txPool(Zi::IOBufAllocator *allocator) const {
    if (!allocator || allocator->mx != mx() || allocator->type != Zi::IOBufType::TxTCP)
      return nullptr;
    return allocator->pool.ptr();
  }

  void retainRx(ZiIOBuf *packet, Zi::IOQueue &retained) {
    if (retained.headPtr() == packet) return;
    retained.unshiftNode(packet);
    while (retained.count_() > m_params.rxRetainFramesPerCxn) retained.pop();
  }

  ZmRef<PacketBuf> allocPacket(void *owner, ZiIPType::T, bool control = false);
  // Native phase-1 entry: complete contiguous Ethernet packet on mx Tx.
  // Acceptance detaches backing; refusal leaves the buffer unchanged.
  Zu::IO::Result txReady(uint8_t *packet) const {
    return ready() ? m_xdp->txReady(packet) : Zi::IOError;
  }
  bool sendPacket(const ZmRef<Zi::IOBuf> &, PacketLease * = nullptr,
    const Zutcp::TCPLayout * = nullptr, TxRetry *retry = nullptr);

  unsigned txDataSlots() const;
  unsigned submitPackets(ZuSpan<const Zpkt::TxPacket>, const TCPLayout &, TxRetry &);
  void submittedPackets(unsigned count, Cxn *waiter = nullptr);

  TxAdmission txAdmission() const;
  void reconcileTx(); // Tx owner: retire kernel leases before retained header changes
  void waitTx(Cxn *); // coalesce refused connection work until CQ returns capacity
  void cancelTxWait(Cxn *);

  // Read-only checksum including the current field: zero validates a segment.
  static unsigned tcpChecksum(const ZiIP &, const ZiIP &, ZuBSpan);
  // Finalize software checksum or prepare a pseudo-header seed for offload.
  static bool tcpChecksum(uint8_t *, unsigned, TCPLayout, bool offload = false);

private:
  enum { Down, Up, Closing }; // physical I/O publication, not the control FSM
  enum { DrainIdle, DrainQueued, DrainActive };
  class UdpCxn;

  void start() override;
  void stop() override;
  void final() override;

  template <typename L> bool spawn(L &&fn) {
    mx()->rxRun(ZuFwd<L>(fn));
    return true;
  }
  void start_();
  void stop_();
  void stopEndpoints();
  void stopTx();

  Zi::IOBufPool *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override;
  bool deferredBacking() const override { return true; }
  AllocResult alloc(Zi::IOBufPool *, const Zi::IOBufPoolList &) override;
  Zi::IOBufPool *ingress(unsigned count, unsigned align);

  void addCxn(Cxn *);
  void delCxn(Cxn *);
  void delListener(Zutcp_::Listener *);
  void stopLinks(const Zutcp_::Listener *, bool);
  void stopCxns();
  bool tupleClaimed(const Zutcp_::Listener *, const ClosedTupleKey &) const;

  void cxnAdded() { ++m_cxnObjects; }
  void cxnRetired(ZiFailFn = {}, bool = false);
  void cxnsDrained();
  template <typename C> bool publish(C *, ZiConnectFn &, ZiCxnInfo);
  template <typename C, typename L> void retire(C *, bool, L &&);
  template <typename L> static void visit(ZiConnection *, L &&);

  void received(uint8_t *, unsigned, ZiRxBurst *);
  void rxProgress();

  void cachePassive(const ClosedTupleKey &, Zutcp_::SrvCxn *);
  void invalidatePassive(Zutcp_::SrvCxn *);
  void invalidatePassive();

  bool closedPacket(const uint8_t *, const IPView &, const TCPView &);
  void replyClosed(ClosedTuple, IPPath);
  void resetReply(const TCPHeader &, unsigned, IPPath);
  void replyControl(TCPHeader, IPPath);

  Zu::IO::Result submitPacket(
    uint8_t *, unsigned,
    bool control, const Zutcp::TCPLayout * = nullptr,
    TxRetry *retry = nullptr);
  void pollTx();
  void txService(bool);

  void scheduleTxResume();
  void resumeTx();

  void receivedUDP(uint8_t *, unsigned, const IPView &, ZiRxBurst *);
  void sendUDP(ZiConnection *);
  void closeUDP(ZiConnection *, bool);
  bool publishUDP(UdpCxn *, ZiConnectFn &, ZiCxnInfo);
  void retireUDP(UdpCxn *, bool);

  void openUDPRouteMonitor();
  uint64_t udpRouteGeneration();

  struct UdpBinding { unsigned portKey = 0; };
  static unsigned udpPortKey(const UdpBinding &binding) { return binding.portKey; }
  ZmHashDerive(UdpBindings, UdpBinding,
    (ZmHashNode<UdpBinding, ZmHashKey<udpPortKey,
      ZmHashShadow<ZmHashID<"Zutcp.UDP.Bindings", ZmHashHeapID<"">>>>>));

  struct Allocators {
    TxAllocator		tx;
    TxCtrlAllocator	control;

    Allocators(ZiMultiplex *mx, Zi::IOBufPool *packets, Zi::IOBufPool *controls) :
      tx{mx, packets}, control{mx, controls} { }
  };

  // Configuration and handshake secrets are fixed while running.
  MxParams				m_params;
  ISN					m_isn;
  TFOKey				m_tfoKey;

  // Native interface and capabilities published before packet service starts.
  Zpkt::Env				*m_xdpEnv = nullptr;
  ZmRef<Zpkt::Nif>			m_xdp;
  unsigned				m_mtu = 0;
  bool					m_tcpChksumOffload = false;

  // Registered backing and native packet allocators.
  unsigned				m_ingressCount = 0;
  ZmRef<Zi::IOBufPool>			m_ingress;
  ZmRef<Zi::IOBufPool>			m_packets;
  ZmRef<Zi::IOBufPool>			m_control;
  ZmRef<Zi::IOBufPool>			m_requestedTx;
  ZuUnion<void, RxAllocator>		m_packetRx;
  ZuUnion<void, Allocators>		m_allocators;

  // Device submission capacity and reserved control credits.
  uint64_t				m_txCapacity = 0;
  unsigned				m_controlCredits = DefaultTxControlCredits;

  // Start, stop and endpoint drain state.
  alignas(Zm::CacheLineSize)
  ZmAtomic<unsigned>			m_ioState{Down};
  ZmAtomic<unsigned>			m_endpointDrain{DrainIdle};
  bool					m_startFailed = false;
  ZeException				m_error;

  // Wake callers update this counter independently of lifecycle readers.
  alignas(Zm::CacheLineSize)
  ZmAtomic<unsigned>			m_wakeUsers{0};

  struct PassiveRoute {
    ClosedTupleKey		key;
    Zutcp_::SrvCxn		*link = nullptr;
  };
  enum { PassiveCacheSize = 32 };
  static unsigned passiveIndex(const ClosedTupleKey &key) {
    return key.hash() >> (32 - 5);
  }

  // Rx endpoint lookup and packet dispatch.
  alignas(Zm::CacheLineSize)
  PassiveRoute				m_passive[PassiveCacheSize];
  EndpointIndex				m_endpoints;
  UdpBindings				m_udpBindings;
  unsigned				m_nextPort = ClientPorts::First;
  unsigned				m_nextUdpPort = ClientPorts::First;
  unsigned				m_rxPackets = 0;

  // Rx connections, listeners and admission.
  Cxn					*m_cxns = nullptr;
  Zutcp_::Listener			*m_listeners = nullptr;
  unsigned				m_cxnObjects = 0;
  unsigned				m_cxnRetiring = 0;
  unsigned				m_cxnTotal = 0;
  unsigned				m_cxnPending = 0;
  unsigned				m_cxnClosing = 0;
  uint64_t				m_cxnRefused = 0;

  // Rx closed tuples and cached Fast Open cookies.
  ClosedTuples				m_closed;
  TFOCache				m_tfoCookies;

  // Tx timestamp generation and completion accounting.
  alignas(Zm::CacheLineSize)
  TCPClock				m_tcpClock;
  unsigned				m_txPending = 0;
  uint64_t				m_dataStalls = 0;
  uint64_t				m_controlStalls = 0;

  // Tx waiters, completion service and shutdown.
  Cxn					*m_txWait = nullptr;
  Cxn					**m_txWaitTail = &m_txWait;
  unsigned				m_txWaitCount = 0;
  unsigned				m_txResumeBudget = 0;
  bool					m_txResumeQueued = false, m_txResumeFailed = false;
  bool					m_txServicing = false;
  bool					m_txDraining = false;
  bool					m_senderStopping = false;

  // Tx UDP route monitoring.
  int					m_udpRouteFD = -1;
  uint64_t				m_udpRouteGeneration = 0;
  uint64_t				m_udpRouteCheck = 0;
};

} // namespace Zutcp

#endif /* Zutcp_HH */
