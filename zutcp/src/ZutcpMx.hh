//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Phase-1 native packet service on the multiplexer I/O owners.

#ifndef ZutcpMx_HH
#define ZutcpMx_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmRWLock.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZutcpAlloc.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/ZutcpTFO.hh>
#include <zlib/ZutcpISN.hh>
#include <zlib/Zxdp.hh>

namespace Zutcp_ { struct EndpointLink; }

namespace Zutcp {

struct Datagram;

using PacketFn = ZmFn<void(ZmRef<Zi::IOBuf>), ZmFnHeapID<"Zutcp.Packet">>;
using RouteFn = ZmFn<void(IPPath, ZtString<>), ZmFnHeapID<"Zutcp.Route">>;

// Endpoint membership is control state. Packet dispatch holds no owner ref.
using EndpointFn = void (*)(
  void *, ZmRef<Zi::IOBuf>, ZiSockAddr, ZiSockAddr, unsigned, const TCPHeader &);
struct EndpointBinding {
  ZiSockAddr local;
  void *owner = nullptr;
  EndpointFn receive = nullptr;
};
inline const ZiSockAddr &bindingKey(const EndpointBinding &binding) {
  return binding.local;
}
ZmHashDerive(EndpointBindings, EndpointBinding,
  (ZmHashNode<EndpointBinding,
    ZmHashKey<bindingKey, ZmHashHeapID<"Zutcp.Endpoints">>>));

struct StackParams {
  unsigned interface = 0;
  ZuCSpan netmask;
  PacketFn packet;
  unsigned spin = 1000;
  unsigned packetCount = ZiIOBuf_DefltCount;
};

// Tx-owned runtime snapshot, or cold control access while stopped.
struct TxAdmission {
  uint64_t capacity = 0, dataStalls = 0, controlStalls = 0;
  unsigned reserved = 0, outstanding = 0, controlCount = 0, controlFree = 0;
};

inline constexpr uint64_t DefaultMaxData = (16<<20); // 16M
enum { DefaultMaxCxns = 1024, DefaultMaxPendingCxns = 128 };
enum { DefaultConnectTimeout = 30 };
enum { DefaultUserTimeout = 120 };
enum { DefaultCloseDrainTimeout = 30, DefaultFinWaitTimeout = 60 };
enum { DefaultClosedTupleRingSize = 4096 };

inline bool positiveTimeout(ZuTime v) {
  return *v && v > ZuTime{0} && unsigned(v.nsec()) < 1000000000;
}

// Rx-owned admission snapshot, or cold control access while stopped.
struct CxnAdmission {
  uint64_t refused = 0;
  unsigned total = 0, pending = 0, closing = 0;
  unsigned maxCxns = DefaultMaxCxns, maxPendingCxns = DefaultMaxPendingCxns;
};

// One embedded ledger per Stack. Tokens move from cold setup into a full owner;
// neither token nor ledger allocates or owns a connection. Mutation is Rx-only.
class CxnSlots;
class ZutcpAPI CxnSlot {
friend class CxnSlots;
public:
  CxnSlot() = default;
  CxnSlot(const CxnSlot &) = delete;
  CxnSlot &operator =(const CxnSlot &) = delete;
  CxnSlot(CxnSlot &&);
  CxnSlot &operator =(CxnSlot &&);
  ~CxnSlot();
  explicit operator bool() const { return m_owner; }
  const CxnSlots *owner() const { return m_owner; }
  void established();
  void closing();
  void release();
private:
  explicit CxnSlot(CxnSlots *owner) : m_owner{owner} { }
  CxnSlots *m_owner = nullptr;
  bool m_pending = true, m_closing = false;
};

class ZutcpAPI CxnSlots {
friend class CxnSlot;
public:
  CxnSlots() = default;
  CxnSlots(const CxnSlots &) = delete;
  CxnSlots &operator =(const CxnSlots &) = delete;
  ~CxnSlots();
  CxnSlot reserve(unsigned maxCxns, unsigned maxPendingCxns);
  CxnAdmission admission(unsigned maxCxns, unsigned maxPendingCxns) const;
private:
  void established();
  void closing();
  void release(bool pending, bool closing);
  uint64_t m_refused = 0;
  unsigned m_total = 0, m_pending = 0, m_closing = 0;
};

// Interface/routing scope belongs to the containing Stack. No packet or
// connection reference survives in a closed record. Copies cross owners.
using ClosedTupleKey = ZuTuple<ZiSockAddr, ZiSockAddr>; // local, remote
struct ClosedTuple {
  ClosedTupleKey key;
  uint64_t id = 0;
  ZuTime tsRecentTime;
  uint32_t txNext = 0, rxNext = 0, tsRecent = 0, lastSentTS = 0;
  bool timestamps = false, fin = false;
};
inline const ClosedTupleKey &closedTupleKey(const ClosedTuple &record) { return record.key; }

inline bool reuseSequence(uint32_t candidate, uint32_t previous) {
  uint32_t distance = candidate - previous;
  return distance && distance < 0x80000000U;
}
inline bool reuseCxnTuple(const ClosedTuple &record, uint32_t localISN, uint32_t peerISN) {
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
  unsigned capacity = 0, count = 0;
  uint64_t inserted = 0, evicted = 0;
};

// Rx-owned fixed slot ring. The intrusive FIFO preserves closure order while
// the hash indexes those same slots. Evidence updates never refresh FIFO order.
class ZutcpAPI ClosedTuples {
  ZmHashDerive(Index, ClosedTuple,
    (ZmHashNode<ClosedTuple,
      ZmHashKey<closedTupleKey,
	ZmHashShadow<ZmHashLock<ZmNoLock, ZmHashHeapID<"">>>>>));
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
  ClosedTupleStats stats() const;

private:
  void append(const ClosedTuple &);
  Slots m_slots;
  ZmRef<Index> m_index;
  Order m_order;
  uint64_t m_nextID = 0, m_inserted = 0, m_evicted = 0;
};

struct ClientPorts {
  enum { First = 49152, End = 65536, Count = End - First };
};
ZtEnumStruct(ZutcpAPI, CxnBindError, int8_t, None, Invalid, InUse, Retained, Exhausted);

// Cold binding policy shared by Stack and deterministic storage tests. Mutation
// in production is confined to Stack's Rx boundary; this owns no extra registry.
ZutcpAPI CxnBindError::T bindCxnTuple(
  ZiSockAddr &local, const ZiSockAddr &remote, unsigned &nextPort,
  EndpointBindings &bindings, const ClosedTuples &history, void *owner, EndpointFn);

// Scalar timestamp clock; Stack samples it only on Tx using the existing clock.
// A backward wall step adjusts the logical offset without freezing later ticks.
class TCPClock {
public:
  bool sample(ZuTime now, uint32_t &out) {
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

struct StackConfig {
  unsigned controlBufCount = DefaultControlBufCount;
  unsigned txControlCredits = DefaultTxControlCredits;
  unsigned rxRetainFrames = DefaultRxRetainFrames;
  unsigned rxRetainFramesPerCxn = DefaultRxRetainPerCxn;
  unsigned rxReserveFrames = DefaultRxReserveFrames;
  uint64_t maxData = DefaultMaxData;
  unsigned maxCxns = DefaultMaxCxns, maxPendingCxns = DefaultMaxPendingCxns;
  unsigned closedTupleRingSize = DefaultClosedTupleRingSize;
  unsigned tfoCookieCacheSize = DefaultTFOCookieCacheSize;
  ZuTime connectTimeout{DefaultConnectTimeout};
  ZuTime userTimeout{DefaultUserTimeout};
  ZuTime closeDrainTimeout{DefaultCloseDrainTimeout}, finWaitTimeout{DefaultFinWaitTimeout};
};

class ZutcpAPI Stack : public ZmNHeap<Stack>, public ZiMxPlugin {
friend inline ZuStringT<"Zutcp.Stack"> ZmNHeapID(Stack *);

public:
  Stack(ZiMultiplex *, StackParams);
  ~Stack() override;
  Zi::IOBufPlugin *allocator(Zi::IOBufType::T, unsigned, unsigned, unsigned) override;
  void start() override;
  void stop() override;
  void rx() override;
  void wake() override;

  // Native phase-1 entry: complete contiguous Ethernet packet on mx Tx.
  // Acceptance detaches backing; refusal leaves the buffer unchanged.
  bool sendPacket(const ZmRef<Zi::IOBuf> &, PacketLease * = nullptr);
  void reconcileTx(); // Tx owner: retire kernel leases before retained header changes
  ZmRef<PacketBuf> allocPacket(void *owner, ZiIPType::T, bool control = false);
  bool configureControl(unsigned count, unsigned credits, ZtString<> &error);
  bool configure(const StackConfig &, ZtString<> &error);
  const StackConfig &config() const { return m_config; }
  CxnAdmission cxnAdmission() const;
  CxnSlot reserveCxn();
  void establishedCxn(CxnSlot &);
  void closingCxn(CxnSlot &);
  void releaseCxn(CxnSlot &);
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
  unsigned rxCxnLimit() const;
  TxAdmission txAdmission() const;
  // Rx-owned physical retention. Aliases of the original packet charge once.
  // Heap-backed logical fixtures have no physical ingress charge.
  bool retainRx(Zi::IOBuf *, RxAccount *);
  RxFrameStats rxAdmission();
  bool ready() const;
  unsigned mtu() const { return m_mtu; }
  Allocator *control() const { return m_control; }
  Zi::IOBufAllocator *dataTxAllocator() { return &m_dataAllocator; }

  // Rx-owned native endpoint registry. Port zero selects an unused local port.
  // Exact local addresses take precedence over a same-family wildcard.
  bool bind(ZiSockAddr &local, void *owner, EndpointFn,
    const ZiSockAddr &remote = {}, ZtString<> *error = nullptr);
  bool unbind(const ZiSockAddr &local, void *owner);
  // Cold kernel lookup on an existing mx worker outside Rx/Tx. Completion is
  // always posted to Rx, including failure/cancellation. False posts nothing.
  // The endpoint must drain this completion before destroying its raw owner.
  bool resolvePath(IPPath, RouteFn);

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
  class Connections;
  enum { Stopped, Starting, Running, Stopping };
  void addProvider(Allocator *);
  void start_();
  void stopRx();
  void stopTx();
  void stopEndpoints();
  void returned();
  void received(uint8_t *, unsigned);
  bool closedPacket(const uint8_t *, const IPView &, const TCPView &);
  void replyClosed(ClosedTuple, IPPath);
  void resetReply(const TCPHeader &, unsigned, IPPath);
  void replyControl(TCPHeader, IPPath);
  void pollTx();
  void observed(Zutcp_::EndpointLink *);
  void unobserved(Zutcp_::EndpointLink *);

  using PacketAllocator = Zi::IOBufPluginAllocator<Zi::IOBufType::RxTCP>;
  ISN		m_isn;
  TFOKey	m_tfoKey;
  TFOCache	m_tfoCookies;
  TCPClock	m_tcpClock;
  unsigned	m_interface, m_mtu, m_spin;
  ZmRef<EndpointBindings> m_endpoints;
  unsigned	m_nextPort = ClientPorts::First, m_routePending = 0;
  ZtString<>	m_netmask;
  PacketFn	m_packet;
  ZmRef<Allocator> m_ingress;
  StackConfig m_config;
  CxnSlots m_cxnSlots;
  ClosedTuples m_closed;
  PacketAllocator m_rxAllocator;
  ZmRef<Allocator> m_packets;
  Allocator *m_providers = nullptr, *m_control;
  using NativeTxAllocator = Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Pkt.Tx">>;
  NativeTxAllocator m_packetAllocator;
  using NativeDataAllocator = Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Data.Tx">>;
  NativeDataAllocator m_dataAllocator;
  using NativeControlAllocator = Zi::IOBufPluginAllocator<Zi::IOBufType::TxTCP,
    PacketBuf, ZiIOBuf_DefltSize, ZiIOBuf_Align, ZuStringT<"Zutcp.Control.Tx">>;
  NativeControlAllocator m_controlAllocator;
  uint64_t m_txCapacity = 0;
  unsigned m_controlCredits = DefaultTxControlCredits;
  ZmRef<Zxdp::Instance> m_backend;
  ZmAtomic<Zxdp::Instance *> m_current{nullptr};
  ZmAtomic<unsigned> m_state{Stopped}, m_wakeUsers{0}, m_returnQueued{0};
  Connections *m_cxns = nullptr; // Rx-owned native application/connection lifetime
  bool		m_startFailure = false, m_endpointDrain = false;
  ZtString<>	m_error;
  mutable ZmRWLock m_observeLock, m_watchLock;
  Zutcp_::EndpointLink *m_observed = nullptr;
  unsigned m_observedCount = 0;
  AddCxnFn	m_addCxn;
  DelCxnFn	m_delCxn;
  alignas(Zm::CacheLineSize) unsigned m_rxPackets = 0;
  alignas(Zm::CacheLineSize) unsigned m_txPending = 0;
  uint64_t m_dataStalls = 0, m_controlStalls = 0;
  ZmScheduler::Timer m_txTimer;
};

} // namespace Zutcp

#endif /* ZutcpMx_HH */
