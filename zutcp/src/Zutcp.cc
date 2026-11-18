//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <new>
#include <stdint.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZxdpBPF.hh>

#include <zlib/ZutcpIP.hh>
#include <zlib/Zutcp.hh>
#include <zlib/ZutcpCliCxn.hh>
#include <zlib/ZutcpSrvCxn.hh>

namespace Zutcp {

ClosedTuples::ClosedTuples(unsigned capacity)
: m_index{new Index{ZmHashParams{capacity}.cBits(0).loadFactor(1)}}
{
  ZmAssert_(capacity);
  m_slots.length(capacity);
}

void ClosedTuples::append(const ClosedTuple &record)
{
  auto node = &m_slots[m_order.count_()];
  static_cast<ClosedTuple &>(*node) = record;
  m_index->addNode(node);
  m_order.pushNode(node);
}

ClosedTuple ClosedTuples::remember(ClosedTuple record)
{
  Order::Node *node;
  if (auto found = m_index->findPtr(record.key)) {
    node = static_cast<Order::Node *>(found);
    m_order.delNode(node);
    m_index->delNode(node);
  } else if (m_order.count_() < m_slots.length()) {
    node = &m_slots[m_order.count_()];
  } else {
    node = m_order.shift();
    m_index->delNode(node); // remove old key before overwriting the slot
    ++m_evicted;
    ++m_full;
  }
  if (!++m_nextID) ++m_nextID;
  record.id = m_nextID;
  static_cast<ClosedTuple &>(*node) = record;
  m_index->addNode(node);
  m_order.pushNode(node);
  ++m_inserted;
  return record;
}

bool ClosedTuples::find(const ClosedTupleKey &key, ClosedTuple &record) const
{
  auto found = m_index->findPtr(key);
  if (!found) return false;
  record = found->data();
  return true;
}

bool ClosedTuples::update(const ClosedTuple &record)
{
  auto found = m_index->findPtr(record.key);
  if (!found || found->data().id != record.id) return false;
  found->data() = record;
  return true;
}

void ClosedTuples::resize(unsigned capacity)
{
  ZmAssert_(capacity);
  if (capacity == m_slots.length()) return;
  ClosedTuples next{capacity};
  unsigned count = m_order.count_();
  unsigned skip = count > capacity ? count - capacity : 0;
  auto iter = m_order.citer();
  while (auto node = iter()) {
    if (skip) { --skip; continue; }
    next.append(*node);
  }
  m_order.clean();
  if (m_index) m_index->clean();
  m_order = ZuMv(next.m_order);
  m_index = ZuMv(next.m_index);
  m_slots = ZuMv(next.m_slots);
  if (count > capacity) m_evicted += count - capacity;
}

void ClosedTuples::final()
{
  m_order.clean();
  m_index = nullptr;
  m_slots.null();
  m_nextID = m_inserted = m_evicted = 0;
  m_full = 0;
}

ClosedTupleStats ClosedTuples::stats() const
{
  return {unsigned(m_slots.length()), m_order.count_(), m_inserted, m_evicted};
}

} // namespace Zutcp

namespace Zutcp_ {

using namespace Zutcp;

class CliCxn :
  public ZmNHeap<CliCxn>, public Zutcp::CliCxn<Instance, CliCxn> {
friend inline ZuStringT<"Zutcp.Mx.CliCxn"> ZmNHeapID(CliCxn *);
  using Base = Zutcp::CliCxn<Instance, CliCxn>;
public:
  using State = Base::Base;
  using Base::connectReserved;
  using ZmNHeap<CliCxn>::operator new;
  using ZmNHeap<CliCxn>::operator delete;
  CliCxn(Instance *owner, ZiConnectFn connected, ZiFailFn fail,
    ZiSockAddr local, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
  : Base{owner}, m_connected{ZuMv(connected)},
    m_fail{ZuMv(fail)}, m_local{local}, m_allocator{allocator}, m_options{ZuMv(options)} { }
  ~CliCxn() {
    app()->txRun([owner = app(),
      fail = ZuMv(m_fail), transient = m_transient]() mutable {
      owner->rxRun([owner, fail = ZuMv(fail), transient]() mutable {
        owner->cxnRetired(ZuMv(fail), transient);
      });
    });
  }
  ZiSockAddr localBind() const { return m_local; }
  bool deferCxnRelease() const { return true; }
  void connected(Connected) {
    ZiCxnInfo info{.type = ZiCxnType::TCPOut, .socket = Zi::nullSocket(),
      .options = m_options, .local = local(), .remote = remote(),
      .mps = dataMPS(), .rxAllocator = m_allocator};
    if (!app()->publish(this, m_connected, ZuMv(info))) abort();
    m_connected = {};
    m_fail = {};
  }
  void closeMx(bool peer) { m_peer = peer; abort(); }
  void connectFailed(bool transient) { m_transient = transient; }
  void disconnected(bool peer) {
    if (retiring) return;
    app()->retire(this, peer || m_peer, [this] { app()->delCxn(this); });
  }
  int process(Reader &) { return 0; }
  void txDequeued() { sendMx(); }
  void abortRx() override { abort(); }

private:
  ZiConnectFn m_connected;
  ZiFailFn m_fail;
  ZiSockAddr m_local;
  Zi::IOBufAllocator *m_allocator;
  ZiCxnOptions m_options;
  bool m_transient = false, m_peer = false;
};

struct ListenerLinks {
  Listener *ownerNext = nullptr;
  Listener **ownerPrev = nullptr;
};

class Listener : public ZmNHeap<Listener>, public ZmPolymorph,
  public ListenerLinks, public Zutcp_::Listener_<Listener, SrvCxn> {
friend inline ZuStringT<"Zutcp.Mx.Listener"> ZmNHeapID(Listener *);
  using Passive = Zutcp_::Listener_<Listener, SrvCxn>;
  using Endpoint = Zutcp_::Endpoint_<Listener>;
public:
  enum { EndpointRef = true };
  using ZmNHeap<Listener>::operator new;
  using ZmNHeap<Listener>::operator delete;
  Listener(Instance *owner, ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept,
    ZiSockAddr local, unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
  : m_owner{owner}, m_listening{ZuMv(listening)}, m_fail{ZuMv(fail)},
    m_accept{ZuMv(accept)}, m_local{local}, m_count{count},
    m_allocator{allocator}, m_options{ZuMv(options)} { }
  Instance *stack() const { return m_owner; }
  void endpointReady(Endpoint *);
  void endpointFailed(bool);
  void endpointDown_(Endpoint *) { }
  void disconnected(SrvCxn *, bool);
  bool inputActive() const { return !m_stopping; }
  bool accepting() const { return !m_closing; }
  bool tupleClaimedLive(const ClosedTupleKey &tuple) const {
    return m_owner->tupleClaimed(this, tuple);
  }
  ZmRef<SrvCxn> acceptInitial(const InitialInfo &, const Zutcp_::PendingInitial &);
  void initialDone() { drained(); }
  bool reopening() const { return m_closing && !m_stopping; }
  void reopen(ZiListenFn listening, ZiConnectFn accept, unsigned count,
    Zi::IOBufAllocator *allocator, ZiCxnOptions options) {
    m_accept = ZuMv(accept);
    m_allocator = allocator;
    m_options = ZuMv(options);
    m_count = count;
    m_closing = false;
    if (listening) listening(ZiListenInfo{.socket = Zi::nullSocket(), .nAccepts = count,
      .ip = local().ip(), .port = local().port(), .options = m_options,
      .rxAllocator = allocator});
  }
  void objectRetired() { ZmAssert_(m_objects); --m_objects; drained(); }
  bool publish(SrvCxn *, ZiCxnInfo);
  void begin();
  void end(bool all = false);
  void retired();
  void drained();
  ZmRef<PacketBuf> allocTxPkt_(bool control) { return Endpoint::allocTxPkt(control); }
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr peer, EcnMark::T ecn,
    bool priority, const IPPath &path) {
    return Endpoint::send(ZuMv(buf), ZuMv(peer), ecn, priority, &path);
  }

private:
  Instance *m_owner;
  ZiListenFn m_listening;
  ZiFailFn m_fail;
  ZiConnectFn m_accept;
  ZiSockAddr m_local;
  unsigned m_count, m_active = 0, m_objects = 0;
  Zi::IOBufAllocator *m_allocator;
  ZiCxnOptions m_options;
  bool m_closing = false, m_stopping = false, m_stopped = false, m_finalQueued = false;
};

class SrvCxn : public ZmNHeap<SrvCxn>,
  public Zutcp::SrvCxn<Instance, SrvCxn> {
friend inline ZuStringT<"Zutcp.Mx.SrvCxn"> ZmNHeapID(SrvCxn *);
  using Base = Zutcp::SrvCxn<Instance, SrvCxn>;
public:
  using State = Base::Base;
  using Base::receivedRouted;
  using State::stageReuseISNRx;
  using State::discardUnadoptedRx;
  using ZmNHeap<SrvCxn>::operator new;
  using ZmNHeap<SrvCxn>::operator delete;
  SrvCxn(Instance *owner, Listener *listener, ZiCxnInfo info)
  : Base{owner}, m_listener{listener}, m_info{ZuMv(info)} { }
  ~SrvCxn() {
    app()->txRun([owner = app(), listener = m_listener] {
      owner->rxRun([owner, listener] {
        listener->objectRetired();
        owner->cxnRetired();
      });
    });
  }
  bool deferCxnRelease() const { return true; }
  void connected(Connected) {
    m_info.mps = dataMPS();
    if (!m_listener->publish(this, m_info))
      State::abortLocalRx(0, [this] { State::disconnect(false); });
  }
  void closedRx(bool peer) {
    if (retiring) return;
    m_listener->disconnected(this, peer);
  }
  void retireLinkRoutes() { m_listener->retireLinkRoutes(this); }
  ZmRef<PacketBuf> allocTxPkt_(bool control) { return m_listener->allocTxPkt_(control); }
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr peer, EcnMark::T ecn,
    bool priority, const IPPath &path) {
    return m_listener->sendPktRaw(ZuMv(buf), ZuMv(peer), ecn, priority, path);
  }
  int process(Reader &) { return 0; }
  void txDequeued() { sendMx(); }
  void abortRx() override { State::disconnect(false); }
  const Listener *listener() const override { return m_listener; }

private:
  Listener *m_listener;
  ZiCxnInfo m_info;
};

bool Listener::publish(SrvCxn *cxn, ZiCxnInfo info)
{
  return !m_closing && m_owner->publish(cxn, m_accept, ZuMv(info));
}

void Listener::begin()
{
  if (!Endpoint::init(stack()) ||
      !Endpoint::open(PathMode::ServerUnconnected, m_local.ip(), m_local.port()))
    endpointFailed(false);
}

void Listener::endpointReady(Endpoint *)
{
  if (m_closing) { endpointFailed(false); return; }
  auto listening = ZuMv(m_listening);
  m_fail = {};
  if (listening) listening(ZiListenInfo{.socket = Zi::nullSocket(), .nAccepts = m_count,
    .ip = local().ip(), .port = local().port(), .options = m_options,
    .rxAllocator = m_allocator});
}

void Listener::endpointFailed(bool transient)
{
  auto fail = ZuMv(m_fail);
  m_listening = {};
  if (fail) fail(transient);
  end();
}

ZmRef<Zutcp_::SrvCxn> Listener::acceptInitial(
  const InitialInfo &info, const Zutcp_::PendingInitial &claim)
{
  ZmRef<SrvCxn> cxn = new SrvCxn{m_owner, this,
    {.type = ZiCxnType::TCPIn, .socket = Zi::nullSocket(), .options = m_options,
     .local = info.local, .remote = info.peer, .rxAllocator = m_allocator}};
  ++m_objects;
  if (claim.evidenceID && !cxn->stageReuseISNRx(claim.localISN, claim.evidenceID)) {
    m_owner->cxnAdded();
    cxn->discardUnadoptedRx();
    return {};
  }
  ++m_active;
  m_owner->addCxn(cxn);
  ZmREF(cxn.ptr());
  return cxn;
}

void Listener::disconnected(SrvCxn *cxn, bool peer)
{
  ZmRef<SrvCxn> hold{cxn};
  retireLinkRoutes(cxn);
  m_owner->retire(cxn, peer, [this, cxn] {
    m_owner->delCxn(cxn);
    retired();
  });
}

void Listener::end(bool all)
{
  m_closing = true;
  if (m_stopping) return;
  m_owner->stopLinks(this, all);
  if (!all && m_active) return;
  m_stopping = true;
  // A stop during the endpoint Tx/Rx setup barrier still completes the original
  // listen request exactly once, even though endpointReady will be suppressed.
  bool pending = bool(m_listening);
  m_listening = {};
  auto fail = ZuMv(m_fail);
  if (pending && fail) fail(false);
  Passive::clearLinkRoutes();
  Endpoint::disconnect([this] {
    stack()->rxRun([this] { m_stopped = true; drained(); });
  });
}

void Listener::retired()
{
  ZmAssert_(m_active);
  --m_active;
  if (m_closing && !m_active && !m_stopping) end();
  else drained();
}

void Listener::drained()
{
  if (!m_stopped || m_active || m_objects || Passive::initialPending() || m_finalQueued) return;
  m_finalQueued = true;
  stack()->rxRun([listener = ZmRef<Listener>{this}] {
    listener->m_owner->delListener(listener);
  });
}

} // namespace Zutcp_

namespace Zutcp {

using Zutcp_::Listener;

void Instance::addCxn(Cxn *cxn)
{
  ZmAssert_(rxInvoked());
  ZmGuard guard(m_cxnsLock);
  cxn->ownerPrev = &m_cxns;
  cxn->ownerNext = m_cxns;
  if (m_cxns) m_cxns->ownerPrev = &cxn->ownerNext;
  m_cxns = cxn;
  ++m_cxnCount;
  ++m_cxnObjects;
}

void Instance::stopLinks(const Listener *listener, bool all)
{
  for (auto cxn = m_cxns; cxn;) {
    auto next = cxn->ownerNext;
    if ((listener ? cxn->listener() == listener : !cxn->listener()) &&
        (all || !cxn->established())) cxn->abortRx();
    cxn = next;
  }
}

bool Instance::tupleClaimed(const Listener *listener, const ClosedTupleKey &key) const
{
  for (auto cxn = m_cxns; cxn; cxn = cxn->ownerNext)
    if (cxn->listener() == listener && cxn->closedKeyRx() == key) return true;
  return false;
}

void Instance::stopCxns()
{
  ZmAssert_(rxInvoked());
  m_endpointDrain = DrainActive;
  for (auto listener = m_listeners; listener; listener = listener->ownerNext) listener->end(true);
  stopLinks(nullptr, true);
  cxnsDrained();
}

void Instance::delCxn(Cxn *cxn)
{
  ZmAssert_(rxInvoked());
  {
    ZmGuard guard(m_cxnsLock);
    if (cxn->ownerNext) cxn->ownerNext->ownerPrev = cxn->ownerPrev;
    *cxn->ownerPrev = cxn->ownerNext;
    cxn->ownerNext = nullptr;
    cxn->ownerPrev = nullptr;
    --m_cxnCount;
  }
  ZmDEREF(cxn);
}

void Instance::delListener(Listener *listener)
{
  ZmAssert_(rxInvoked());
  if (listener->ownerNext) listener->ownerNext->ownerPrev = listener->ownerPrev;
  *listener->ownerPrev = listener->ownerNext;
  listener->ownerNext = nullptr;
  listener->ownerPrev = nullptr;
  ZmDEREF(listener);
  cxnsDrained();
}

void Instance::cxnsDrained()
{
  if (m_endpointDrain != DrainActive || m_cxns || m_listeners || m_cxnRetiring || m_cxnObjects)
    return;
  m_ioState = Closing;
  m_endpointDrain = DrainIdle;
  stopEndpoints();
}

void Instance::cxnRetired(ZiFailFn fail, bool transient)
{
  ZmAssert_(rxInvoked());
  ZmAssert_(m_cxnObjects);
  --m_cxnObjects;
  if (fail) fail(transient);
  cxnsDrained();
}

template <typename C> bool Instance::publish(C *cxn, ZiConnectFn &fn, ZiCxnInfo info)
{
  ZmAssert_(rxInvoked());
  if (m_endpointDrain || !fn) return false;
  auto mx = fn(info);
  if (!mx) return false;
  ZmREF(mx);
  mx->plugin(static_cast<typename C::State *>(cxn));
  {
    ZmGuard guard(m_cxnsLock);
    cxn->mxCxn = mx;
  }
  {
    ZmReadGuard guard(m_watchLock);
    if (m_addCxn) m_addCxn(mx);
  }
  mx->connected(mx->rxContext());
  if (mx->rxContext().disconnected()) mx->disconnect();
  else cxn->recvMx();
  return true;
}

template <typename C, typename L> void Instance::retire(C *cxn, bool peer, L &&done)
{
  ZmAssert_(rxInvoked());
  ++m_cxnRetiring;
  ZmRef<ZiConnection> mx;
  {
    ZmGuard guard(m_cxnsLock);
    cxn->retiring = true;
    mx = ZmRef<ZiConnection>::acquire(cxn->mxCxn);
  }
  txRun([
    this, cxn = ZmRef<C>{cxn}, mx = ZuMv(mx), peer, done = ZuFwd<L>(done)
  ]() mutable {
    if (mx) {
      mx->txUp() = 0;
      mx->txContext().complete();
      mx->plugin(nullptr);
    }
    rxRun([
      this, cxn = ZuMv(cxn), mx = ZuMv(mx), peer, done = ZuMv(done)
    ]() mutable {
      if (mx) {
	mx->rxUp() = 0;
	mx->rxContext().complete();
	{
	  ZmGuard guard(m_cxnsLock);
	  cxn->mxCxn = nullptr;
	}
	ZmReadGuard guard(m_watchLock);
	if (m_delCxn) m_delCxn(mx.ptr());
      }
      done(); // Remove cold ownership while the drain reference still protects Cxn.
      cxn->releaseCxnRx();
      if (mx) mx->disconnected(peer);
      cxn = nullptr;
      --m_cxnRetiring;
      cxnsDrained();
    });
  });
}

template <typename L> void Instance::visit(ZiConnection *mx, L &&fn)
{
  if (!mx->plugin()) return;
  switch (mx->info().type) {
    case ZiCxnType::TCPOut:
      fn(static_cast<Zutcp_::CliCxn::State *>(mx->plugin())->impl());
      break;
    case ZiCxnType::TCPIn:
      fn(static_cast<Zutcp_::SrvCxn::State *>(mx->plugin())->impl());
      break;
  }
}

static unsigned requireMTU(ZiMultiplex *mx, unsigned interface)
{
  if (!mx) throw ZeEXCEPT(Error, "Zutcp", "packet interface requires a multiplexer");
  auto result = interfaceMTU(interface);
  if (result.is<ZeException>()) throw ZuMv(result.p<ZeException>());
  return result.p<unsigned>();
}

static unsigned ingressCount(ZiMultiplex *mx, unsigned interface)
{
  if (mx->rxBufSize()) return mx->rxBufSize();
  auto result = Zxdp::Xdp::queryLink(interface);
  if (result.is<ZeException>()) throw ZuMv(result.p<ZeException>());
  // Idle queues retain their FILL allocations. Each active queue needs room
  // beyond one connection's default retained list before its oldest can return.
  uint64_t count = uint64_t(result.p<Zxdp::Xdp::LinkState>().rxQueues) *
    (uint64_t(DefaultRxRetainPerCxn) + mx->rxQuantum());
  if (count > UINT_MAX) throw ZeEXCEPT(Error, "Zutcp", "Rx pool count overflow");
  return count > ZiIOBuf_DefltCount ? unsigned(count) : ZiIOBuf_DefltCount;
}

static unsigned requirePackets(unsigned count)
{
  if (!count) throw ZeEXCEPT(Error, "Zutcp", "empty native packet pool");
  return count;
}

static unsigned requireTCPSecrets(ISN &isn, TFOKey &tfo, unsigned interface)
{
  if (!isn.init(interface))
    throw ZeEXCEPT(Error, "Zutcp", "secure TCP sequence initialization failed");
  if (!tfo.init())
    throw ZeEXCEPT(Error, "Zutcp", "secure Fast Open cookie initialization failed");
  return interface;
}

Instance::Instance(ZiMultiplex *mx)
: ZiMxPlugin{mx},
  m_endpoints{ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init("Zutcp.Endpoints")}
{
  ZmAssert_(mx && mx->state() == ZmEngineState::Stopped);
  m_tfoCookies.queueOwner(mx->id());
  m_closed.queueOwner(mx->id());
}

// Fixture storage has no kernel interface; use the nonzero loopback identity
// solely for the ISN domain, without querying or opening that interface.
Instance::Instance(ZiMultiplex *mx, unsigned mtu, ZmRef<Zi::IOBufPool> ingress,
  ZmRef<Zi::IOBufPool> packets, ZmRef<Zi::IOBufPool> control)
: ZiMxPlugin{mx}, m_params{.interface = requireTCPSecrets(m_isn, m_tfoKey, 1), .spin = 0},
  m_mtu{mtu}, m_backend{Zxdp::instance(Zxdp::Type::Xdp)},
  m_ingress{new Allocator{ZuMv(ingress)}},
  m_packets{new Allocator{ZuMv(packets)}}, m_control{new Allocator{ZuMv(control)}},
  m_endpoints{ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init("Zutcp.Endpoints")}
{
  m_tfoCookies.queueOwner(mx->id());
  m_closed.queueOwner(mx->id());
  new (m_allocators.new_<1>()) Allocators{mx, m_ingress, m_packets, m_control};
  addProvider(m_packets);
  addProvider(m_control);
}

Instance::~Instance() { final(); }

void Instance::final()
{
  ZmAssert_(state() == ZmEngineState::Stopped && !unsigned(m_wakeUsers));
  ZmAssert_(!m_txServicing && !m_txPending);
  ZmAssert_(!m_endpoints.count_());
  ZmAssert_(!m_cxns && !m_cxnCount && !m_listeners && !m_cxnRetiring && !m_cxnObjects);
  ZmAssert_(!m_cxnTotal && !m_cxnPending && !m_cxnClosing);
  if (m_ingress) m_ingress->returned = {};
  if (m_backend) m_backend->final();
  m_backend = nullptr;
  while (m_providers) {
    auto p = m_providers;
    m_providers = p->nextProvider;
    p->nextProvider = nullptr;
    ZmDEREF(p); // stopped, long-lived configuration ownership
  }
  m_allocators.null();
  m_ingress = m_packets = nullptr;
  m_control = nullptr;
  m_txCapacity = m_mtu = 0;
  m_cxnRefused = m_dataStalls = m_controlStalls = 0;
  m_params = {};
  m_netmask = Zi::Name{};
  m_isn.~ISN(); new (&m_isn) ISN;
  m_tfoKey.~TFOKey(); new (&m_tfoKey) TFOKey;
  m_closed.final();
  m_tfoCookies.final();
  m_ioState = Down;
  m_endpointDrain = DrainIdle;
  m_startFailed = false;
  m_rxWakeQueued = m_rxPackets = 0;
  m_nextPort = ClientPorts::First;
  m_controlCredits = DefaultTxControlCredits;
  m_tcpClock = {};
  m_error = {};
  unwatch();
}

void Instance::addProvider(Allocator *p)
{
  ZmREF(p);
  m_txCapacity += p->pool->count;
  p->nextProvider = m_providers;
  m_providers = p;
}

Zi::IOBufPool *Instance::allocator(
  Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count)
{
  ZmAssert_(mx()->state() == ZmEngineState::Stopped);
  // Providers reserve complete output framing before Zi copies their geometry.
  enum { Ethernet = 14, IPv6 = 40, TCP = 60 };
  unsigned head;
  switch (type) {
    case Zi::IOBufType::RxTCP:
    case Zi::IOBufType::TxTCP: head = Ethernet + IPv6 + TCP; break;
    default: throw ZeEXCEPT(Error, "Zutcp", "unsupported network allocator role");
  }
  if (head >= m_mtu + Ethernet || !align || (align & (align - 1)) ||
      align > unsigned(sysconf(_SC_PAGESIZE)))
    throw ZeEXCEPT(Error, "Zutcp", "unsupported packet allocator geometry");
  if (type == Zi::IOBufType::RxTCP)
    return new RxAllocator{m_ingress, head, count, align};
  uint64_t bytes = uint64_t(size) + head;
  if (bytes > m_mtu + Ethernet) bytes = m_mtu + Ethernet;
  auto p = new Allocator{m_backend->bufPool(count, unsigned(bytes), head, 0, align)};
  addProvider(p);
  return p;
}

void Instance::initControl(unsigned count, unsigned credits)
{
  m_txCapacity = m_txCapacity - m_control->pool->count + count;
  m_control->count = m_control->pool->count = count;
  m_controlCredits = credits;
}

TxAdmission Instance::txAdmission() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->txThread()));
  unsigned count = m_control ? m_control->pool->count : 0;
  return {.capacity = m_txCapacity, .dataStalls = m_dataStalls,
    .controlStalls = m_controlStalls, .reserved = m_controlCredits,
    .outstanding = m_txPending, .controlCount = count};
}

ZuUnion<void, ZeException> Instance::init(Params config)
{
  if (state() != ZmEngineState::Stopped || mx()->state() != ZmEngineState::Stopped)
    return ZeEXCEPT(Error, "Zutcp", "finish multiplexer stop before initialization");
  if (!config.rtoParams.valid())
    return ZeEXCEPT(Error, "Zutcp", "invalid RTO bounds or granularity");
  if (config.tlpPeerAckDelay < ZuTime{0})
    return ZeEXCEPT(Error, "Zutcp", "negative peer ACK allowance");
  if (*config.heartBeat && (config.heartBeat < ZuTime{0} || config.heartBeat.nsec() < 0 ||
      config.heartBeat.nsec() >= 1000000000))
    return ZeEXCEPT(Error, "Zutcp", "invalid TCP heartbeat interval");
  if (!config.maxPayload || config.maxPayload > BufSize)
    return ZeEXCEPT(Error, "Zutcp", "invalid payload cap");
  if (!rxThread() || !txThread() || rxThread() == txThread() ||
      rxThread() > mx()->params().nThreads() || txThread() > mx()->params().nThreads())
    return ZeEXCEPT(Error, "Zutcp", "invalid multiplexer Rx/Tx owners");
  if (!positiveTimeout(config.connectTimeout)) {
    return ZeEXCEPT(Error, "Zutcp", "invalid connect timeout");
  }
  if (!positiveTimeout(config.closeDrainTimeout) || !positiveTimeout(config.finWaitTimeout)) {
    return ZeEXCEPT(Error, "Zutcp", "invalid close timeout");
  }
  if (!positiveTimeout(config.userTimeout) && config.userTimeout != ZuTime{0}) {
    return ZeEXCEPT(Error, "Zutcp", "invalid user timeout");
  }
  if (config.ackHoldTimeout < ZuTime{0} ||
      config.ackHoldTimeout >= ZuTime{0, 500000000}) {
    return ZeEXCEPT(Error, "Zutcp", "invalid TCP ACK hold timeout");
  }
  if (!config.maxCxns || !config.maxPendingCxns || config.maxPendingCxns > config.maxCxns) {
    return ZeEXCEPT(Error, "Zutcp", "invalid connection admission limits");
  }
  if (!config.tfoCookieCacheSize) {
    return ZeEXCEPT(Error, "Zutcp", "invalid Fast Open cookie cache size");
  }
  if (!config.closedTupleRingSize) {
    return ZeEXCEPT(Error, "Zutcp", "invalid closed tuple ring size");
  }
  if (!config.rxRetainFramesPerCxn)
    return ZeEXCEPT(Error, "Zutcp", "invalid per-connection Rx retention limit");
  if (!config.packetCount)
    return ZeEXCEPT(Error, "Zutcp", "empty native packet pool");
  uint64_t capacity = m_ingress ?
    m_txCapacity - m_control->pool->count + config.controlBufCount :
    uint64_t(config.packetCount) + config.controlBufCount;
  if (!config.controlBufCount || !config.txControlCredits ||
      config.txControlCredits >= capacity || capacity > UINT_MAX || m_txPending)
    return ZeEXCEPT(Error, "Zutcp", "invalid or busy control reserve");
  if (m_ingress && (config.interface != m_params.interface ||
      config.packetCount != m_params.packetCount))
    return ZeEXCEPT(Error, "Zutcp", "native interface and packet pool remain fixed after init");
  if (!m_ingress) {
    try {
      auto mtu = requireMTU(mx(), config.interface);
      requireTCPSecrets(m_isn, m_tfoKey, config.interface);
      auto backend = Zxdp::instance(Zxdp::Type::Xdp);
      ZmRef<Allocator> ingress = new Allocator{backend->bufPool(
	ingressCount(mx(), config.interface), mtu + 14)};
      ZmRef<Allocator> packets = new Allocator{backend->bufPool(
	requirePackets(config.packetCount), mtu + 14, 14 + 40 + 60)};
      ZmRef<Allocator> controls = new Allocator{backend->bufPool(
	config.controlBufCount, mtu + 14, 14 + 40 + 60)};
      new (m_allocators.new_<1>()) Allocators{mx(), ingress, packets, controls};
      m_mtu = mtu;
      m_backend = ZuMv(backend);
      m_ingress = ZuMv(ingress);
      m_packets = ZuMv(packets);
      m_control = controls;
      addProvider(m_packets);
      addProvider(m_control);
      m_ingress->returned = [this] {
	if (!ready() || mx()->invoked(mx()->rxThread()) || m_rxWakeQueued.xch(1)) return;
	mx()->rxRun([this] { m_rxWakeQueued = 0; });
      };
    } catch (ZeException &error) {
      return ZuMv(error);
    }
  }
  initControl(config.controlBufCount, config.txControlCredits);
  m_closed.resize(config.closedTupleRingSize);
  m_tfoCookies.resize(config.tfoCookieCacheSize);
  Zi::Name netmask{config.netmask};
  m_netmask = ZuMv(netmask);
  m_params = ZuMv(config);
  m_params.netmask = m_netmask;
  if (m_params.heartBeat == ZuTime{0}) m_params.heartBeat = {};
  return {};
}

CxnAdmission Instance::cxnAdmission() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return {.refused = m_cxnRefused, .total = m_cxnTotal, .pending = m_cxnPending,
    .closing = m_cxnClosing, .maxCxns = m_params.maxCxns,
    .maxPendingCxns = m_params.maxPendingCxns};
}

bool Instance::reserveCxn()
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (m_cxnTotal >= m_params.maxCxns || m_cxnPending >= m_params.maxPendingCxns) {
    ++m_cxnRefused;
    return false;
  }
  ++m_cxnTotal;
  ++m_cxnPending;
  return true;
}

void Instance::cancelCxn()
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  ZmAssert_(m_cxnTotal && m_cxnPending);
  --m_cxnTotal;
  --m_cxnPending;
}

void Instance::admitCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  ZmAssert_(!cxn.admission && m_cxnTotal && m_cxnPending);
  cxn.admission = Cxn::Admitted | Cxn::Pending;
}

void Instance::establishedCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (!(cxn.admission & Cxn::Pending)) return;
  ZmAssert_(m_cxnPending);
  cxn.admission &= ~Cxn::Pending;
  --m_cxnPending;
}

void Instance::closingCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (!(cxn.admission & Cxn::Admitted) || (cxn.admission & Cxn::Closing)) return;
  cxn.admission |= Cxn::Closing;
  ++m_cxnClosing;
}

void Instance::releaseCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  auto state = cxn.admission;
  if (!(state & Cxn::Admitted)) return;
  ZmAssert_(m_cxnTotal && (!(state & Cxn::Pending) || m_cxnPending) &&
    (!(state & Cxn::Closing) || m_cxnClosing));
  cxn.admission = 0;
  --m_cxnTotal;
  if (state & Cxn::Pending) --m_cxnPending;
  if (state & Cxn::Closing) --m_cxnClosing;
}

bool Instance::chooseISN(const ZiSockAddr &local, const ZiSockAddr &remote, uint32_t &isn) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_isn.generate(local, remote, isn);
}

bool Instance::tfoCookie(const ZiIP &client, const ZiSockAddr &server, TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoCookies.find({client, server}, cookie);
}

void Instance::saveTFOCookie(
  const ZiIP &client, const ZiSockAddr &server, const TFOCookie &cookie)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (cookie.length) m_tfoCookies.store({client, server}, cookie.span());
  else m_tfoCookies.erase({client, server});
}

bool Instance::issueTFO(const ZiSockAddr &local, const ZiIP &client, TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoKey.issue({client, local}, cookie);
}

bool Instance::validTFO(const ZiSockAddr &local, const ZiIP &client, const TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoKey.validate({client, local}, cookie.span());
}

bool Instance::tcpTimestamp(ZuTime now, uint32_t &out)
{
  ZmAssert_(state() == ZmEngineState::Stopped || txInvoked());
  return m_tcpClock.sample(now, out);
}

ClosedTuple Instance::rememberClosed(ClosedTuple record)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_closed.remember(ZuMv(record));
}

bool Instance::closedTuple(const ClosedTupleKey &key, ClosedTuple &record) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.find(key, record);
}

bool Instance::updateClosed(const ClosedTuple &record)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_closed.update(record);
}

ClosedTupleStats Instance::closedTupleStats() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.stats();
}

bool Instance::ready() const { return state() == ZmEngineState::Running; }

CxnBindError::T bindCxnTuple(
  EndpointIndex::Node &endpoint, const ZiSockAddr &remote, unsigned &nextPort,
  EndpointIndex &bindings, const ClosedTuples &history)
{
  const auto &local = endpoint.bindAddr;
  if (!endpoint.receive ||
      (local.ip().type() != ZiIPType::V4 && local.ip().type() != ZiIPType::V6))
    return CxnBindError::Invalid;
  bool active = remote.ip() && remote.port();
  ClosedTuple record;
  ZiSockAddr key = local;
  if (!key.port()) {
    unsigned tries = ClientPorts::Count;
    do {
      key.init(local.ip(), nextPort);
      if (++nextPort == ClientPorts::End) nextPort = ClientPorts::First;
      if (!bindings.findPtr(key) && (!active || !history.find({key, remote}, record))) break;
    } while (--tries);
    if (!tries) return CxnBindError::Exhausted;
  } else {
    if (bindings.findPtr(key)) return CxnBindError::InUse;
    if (active && history.find({key, remote}, record)) return CxnBindError::Retained;
  }
  endpoint.bindAddr = key;
  bindings.addNode(&endpoint);
  return CxnBindError::None;
}

ZuUnion<void, ZeException> Instance::bind(
  EndpointIndex::Node &endpoint, const ZiSockAddr &remote)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready())
    return ZeEXCEPT(Error, "Zutcp", "packet service unavailable during endpoint binding");
  switch (bindCxnTuple(endpoint, remote, m_nextPort, m_endpoints, m_closed)) {
    case CxnBindError::None: return {};
    case CxnBindError::Invalid:
      return ZeEXCEPT(Error, "Zutcp", "invalid endpoint binding");
    case CxnBindError::InUse:
      return ZeEXCEPT(Error, "Zutcp", "local endpoint is already bound");
    case CxnBindError::Retained:
      return ZeEXCEPT(Error, "Zutcp", "client tuple remains in closed history");
    case CxnBindError::Exhausted:
      return ZeEXCEPT(Error, "Zutcp",
	"client port range 49152..65535 exhausted by live endpoints or closed tuples");
    default:
      return ZeEXCEPT(Error, "Zutcp", "invalid endpoint binding result");
  }
}

bool Instance::unbind(EndpointIndex::Node &endpoint)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (m_endpoints.findPtr(endpoint.bindAddr) != &endpoint) return false;
  m_endpoints.delNode(&endpoint);
  return true;
}

ZuUnion<IPPath, ZeException> Instance::resolve(ZiIP remote, ZiIP local)
{
  ZmAssert_(rxInvoked());
  if (!ready()) return ZeEXCEPT(Error, "Zutcp", "packet service unavailable during route setup");
  return resolveIPPath(m_params.interface, IPPath{local, remote, {}, {}, 0});
}

Zi::IOBufAllocator *Instance::dataRxAllocator() const { return inlineAllocator(); }

void Instance::reportError(ZeException error) { ZiLogEvent(ZuMv(error)); }

void Instance::start()
{
  Engine::start([this](bool ok) {
    auto error = m_error;
    if (!ok && !error) error = ZeEXCEPT(Error, "Zutcp", "TCP start was superseded");
    mx()->pluginStarted(ok ? ZiMxPlugin::StartResult{} : ZiMxPlugin::StartResult{ZuMv(error)});
  });
}

void Instance::start_()
{
  ZmAssert_(rxInvoked());
  m_endpointDrain = DrainIdle;
  m_rxWakeQueued = 0;
  m_error = {};
  if (!m_ingress) {
    m_error = ZeEXCEPT(Error, "Zutcp", "initialize TCP before start");
    Engine::started(false);
    return;
  }
  Zxdp::Params config(
    Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
  auto &params = config.p<Zxdp::Xdp::Params>();
  params.ifindex = m_params.interface;
  params.netmask = m_netmask;
  params.rxPool = m_ingress->pool;
  params.rx = [this](uint8_t *p, unsigned n) { received(p, n); };
  params.spin = m_params.spin;
  params.scheduler = mx();
  params.txSid = m_params.txSid;
  for (auto p = m_providers; p; p = p->nextProvider)
    params.txPools.pushNode(p->pool.ptr());
  auto initialized = m_backend->init(config);
  if (initialized.is<ZeException>()) {
    m_error = ZuMv(initialized).p<ZeException>();
    Engine::started(false);
    return;
  }
  m_backend->start([this](bool ok) {
    auto error = m_backend->error();
    mx()->rxRun([this, ok, error = ZuMv(error)]() mutable {
      if (ok && m_backend->mtu() == m_mtu) {
	m_ioState = Up;
	Engine::started(true);
	return;
      }
      m_error = ZuMv(error);
      if (!m_error)
	m_error = ZeEXCEPT(Error, "Zutcp", "interface geometry changed before packet startup");
      m_startFailed = true;
      m_ioState = Closing;
      mx()->txRun([this] { stopTx(); });
    });
  });
}

void Instance::stop()
{
  Engine::stop([this](bool ok) {
    auto error = m_error;
    if (!ok && !error) error = ZeEXCEPT(Error, "Zutcp", "TCP stop was superseded");
    mx()->pluginStopped(ok ? ZiMxPlugin::StopResult{} : ZiMxPlugin::StopResult{ZuMv(error)});
  });
}

void Instance::stop_()
{
  ZmAssert_(rxInvoked());
  stopCxns();
}

void Instance::stopEndpoints()
{
  // Existing wake calls finish before XDP retires its wait descriptor.
  m_ioState = Closing;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  ZmAssert_(!cxnAdmission().total);
  mx()->txRun([this] { stopTx(); });
}

void Instance::stopTx()
{
  ZmAssert_(txInvoked());
  if (m_senderStopping) return;
  if (m_backend->ready() && m_txPending) {
    m_txDraining = true;
    txService(true);
    return;
  }
  m_txDraining = false;
  txService(false);
  if (unsigned(m_wakeUsers)) {
    mx()->push([this] { stopTx(); }, txThread());
    return;
  }
  m_senderStopping = true;
  m_backend->stop([this](bool ok) {
    auto error = m_backend->error();
    mx()->txRun([this, ok, error = ZuMv(error)]() mutable {
      m_senderStopping = false;
      if (!m_error && !!error) m_error = ZuMv(error);
      m_txPending = 0;
      m_ioState = Down;
      if (m_startFailed) {
	m_startFailed = false;
	Engine::started(false);
      } else Engine::stopped(ok && !m_error);
    });
  });
}

void Instance::wake()
{
  ++m_wakeUsers;
  if (m_ioState == Up) m_backend->wake();
  --m_wakeUsers;
  if (m_ioState == Up && Engine::stopping() &&
      !m_endpointDrain.cmpXch(DrainQueued, DrainIdle))
    mx()->rxRun([this] {
      if (m_ioState == Up && m_endpointDrain == DrainQueued) Engine::stopped();
    });
}

void Instance::rx()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) return;
  m_rxPackets = 0;
  m_backend->rx();
}

void Instance::received(uint8_t *p, unsigned n)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  IPView ip;
  TCPView tcp;
  EndpointIndex::Node *binding = nullptr;
  bool valid = readIP({p, n}, ip) && readTCP(ip, tcp);
  if (valid) {
    binding = m_endpoints.findPtr(ZiSockAddr{ip.local, tcp.header.dstPort});
    if (!binding) {
      ZiIP any;
      if (ip.local.v6()) any = in6addr_any;
      else any = in_addr{};
      binding = m_endpoints.findPtr(ZiSockAddr{any, tcp.header.dstPort});
    }
  }
  // A current owner qualifies input before consulting retired tuple history.
  // In particular, newer-TS reuse can choose a peer ISN below an old frontier.
  bool closed = valid && !binding && closedPacket(p, ip, tcp);
  if (closed) m_ingress->free(p);
  else if (binding || m_params.packet) {
    ZmRef<Zi::IOBuf> buf = new Zi::IOBufStub<Zi::IOBuf, ZiIOBuf_HeapID>{
      p, n, &m_allocators.p<1>().rx, this, n, false, true};
    if (binding) {
      buf->skip = tcp.payload.data() - p;
      buf->length = tcp.payload.length();
      auto receive = binding->receive;
      receive(binding, ZuMv(buf), ZiSockAddr{ip.local, tcp.header.dstPort},
	ZiSockAddr{ip.remote, tcp.header.srcPort}, ip.ecn, tcp.header);
    } else m_params.packet(ZuMv(buf));
  } else {
    if (valid && !(tcp.header.flags & 0x04)) {
      IPPath path{.local = ip.local, .remote = ip.remote, .mtu = m_mtu};
      memcpy(path.localMAC, p, 6);
      memcpy(path.remoteMAC, p + 6, 6);
      resetReply(tcp.header, tcp.payload.length(), path);
    }
    m_ingress->free(p);
  }
  if (++m_rxPackets >= mx()->rxQuantum()) {
    m_backend->wake();
    mx()->push([this] { rx(); }, mx()->rxThread());
  }
}

bool Instance::recoverClosed(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14) return false;
  IPView ip{.local = d.local.ip(), .remote = d.addr.ip()};
  TCPView tcp{.header = d.tcp, .payload = d.buf->cspan()};
  return closedPacket(d.buf->data_(), ip, tcp);
}

void Instance::resetUnmatched(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14 || (d.tcp.flags & 0x04)) return;
  IPPath path{.local = d.local.ip(), .remote = d.addr.ip(), .mtu = m_mtu};
  memcpy(path.localMAC, d.buf->data_(), 6);
  memcpy(path.remoteMAC, d.buf->data_() + 6, 6);
  resetReply(d.tcp, d.buf->length, path);
}

void Instance::resetReply(const TCPHeader &incoming, unsigned payload, IPPath path)
{
  TCPHeader tcp{.srcPort = incoming.dstPort, .dstPort = incoming.srcPort, .flags = 0x04};
  if (incoming.flags & 0x10) tcp.seq = incoming.ack;
  else {
    tcp.flags |= 0x10;
    tcp.ack = incoming.seq + payload + unsigned(bool(incoming.flags & 0x02)) +
      unsigned(bool(incoming.flags & 0x01));
  }
  mx()->txRun([this, tcp, path] { replyControl(tcp, path); });
}

void Instance::replyControl(TCPHeader tcp, IPPath path)
{
  auto packet = allocPacket(this, path.local.type(), true);
  if (!packet) return;
  unsigned wire = writeTCP(packet->data_(), packet->size, path, tcp, 0);
  if (!wire) return;
  packet->skip = 0;
  packet->length = wire;
  sendPacket(packet);
}

bool Instance::closedPacket(const uint8_t *frame, const IPView &ip, const TCPView &tcp)
{
  ClosedTuple record;
  ClosedTupleKey key{ZiSockAddr{ip.local, tcp.header.dstPort},
    ZiSockAddr{ip.remote, tcp.header.srcPort}};
  if (!m_closed.find(key, record) || (tcp.header.flags & 0x02)) return false;
  // This belongs to the retired incarnation. Never deliver it to a new owner.
  // A lost final ACK can leave older DATA ahead of FIN in peer recovery.
  // ACK that DATA too; pure ACKs and resets must never create ACK loops.
  if (tcp.header.flags & 0x04) return true;
  bool reply = record.fin && (tcp.payload.length() || (tcp.header.flags & 0x01)) &&
    int32_t(tcp.header.seq - record.rxNext) < 0;
  bool refreshed = false;
  if (record.timestamps) {
    if (!(tcp.header.options.flags & TCPOptions::TS)) return true;
    auto now = Zm::now();
    bool recent = *record.tsRecentTime && (now < record.tsRecentTime ||
      now - record.tsRecentTime <= ZuTime{24*24*60*60});
    uint32_t distance = tcp.header.options.tsval - record.tsRecent;
    if ((reply || tcp.header.seq == record.rxNext) &&
	(!recent || distance < uint32_t(0x80000000))) {
      record.tsRecent = tcp.header.options.tsval;
      record.tsRecentTime = now;
      refreshed = true;
    }
  }
  if (!reply) {
    if (refreshed) m_closed.update(record);
    return true;
  }
  auto response = record;
  if (refreshed) m_closed.update(record);
  IPPath path{.local = ip.local, .remote = ip.remote, .mtu = m_mtu};
  memcpy(path.localMAC, frame, 6);
  memcpy(path.remoteMAC, frame + 6, 6);
  mx()->txRun([this, response, path] { replyClosed(response, path); });
  return true;
}

void Instance::replyClosed(ClosedTuple record, IPPath path)
{
  auto packet = allocPacket(this, path.local.type(), true);
  if (!packet) return;
  TCPHeader tcp{.srcPort = record.key.p<0>().port(), .dstPort = record.key.p<1>().port(),
    .seq = record.txNext, .ack = record.rxNext, .flags = 0x10};
  if (record.timestamps) {
    tcp.options.flags = TCPOptions::TS;
    tcp.options.tsecr = record.tsRecent;
    if (!tcpTimestamp(Zm::now(), tcp.options.tsval)) return;
    int options = tcp.options.encodedLength(false);
    if (options < 0 || unsigned(options) > packet->avail()) return;
    packet->skip += unsigned(options);
  }
  unsigned wire = writeTCP(packet->data_(), packet->size, path, tcp, 0);
  if (!wire) return;
  packet->skip = 0;
  packet->length = wire;
  if (sendPacket(packet) && record.timestamps)
    mx()->rxRun([this, key = record.key, id = record.id, sent = tcp.options.tsval] {
      ClosedTuple current;
      if (m_closed.find(key, current) && current.id == id) {
	current.lastSentTS = sent;
	m_closed.update(current);
      }
    });
}

ZmRef<PacketBuf> Instance::allocPacket(void *owner, ZiIPType::T family, bool control)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  auto head = tcpHead(family);
  if (!ready() || !head) return nullptr;
  ZmRef<PacketBuf> buf = control ? m_allocators.p<1>().control.allocBuf(owner) :
    m_allocators.p<1>().packet.allocBuf(owner);
  if (control && !buf) ++m_controlStalls;
  if (buf) buf->skip = head;
  return buf;
}

bool Instance::sendPacket(const ZmRef<Zi::IOBuf> &buf, PacketLease *lease)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready() || !buf || !buf->registered() || !buf->length ||
      buf->skip > buf->size || buf->length > buf->size - buf->skip) return false;
  bool control = buf->allocator_ == &m_allocators.p<1>().control;
  uint64_t limit = control ? m_txCapacity : m_txCapacity - m_controlCredits;
  // Eligible application DATA is published first. Reconcile before submission
  // only when completion credit is required to admit this packet.
  if (m_txPending >= limit) reconcileTx();
  if (m_txPending >= limit) {
    if (control) ++m_controlStalls; else ++m_dataStalls;
    return false;
  }
  if (!m_backend->send(buf->data(), buf->length)) return false;
  // Transfer the caller lease to recovery when requested. The independent
  // kernel lease ends at CQ; neither lease retains submitted Zi metadata.
  auto backing = buf->data_();
  auto size = buf->size;
  auto allocator = buf->allocator_;
  buf->data__ = 0;
  buf->size = buf->skip = buf->length = 0;
  auto provider = control ? m_control :
    (allocator == &m_allocators.p<1>().packet || allocator == &m_allocators.p<1>().data ?
      m_packets.ptr() : nullptr);
  if (!lease && provider) lease = static_cast<PacketBuf *>(buf.ptr())->retained;
  if (lease) lease->adopt(buf->allocator(), backing, size);
  else allocator->free(backing, size, true);
  // The dedicated sender was notified by publication. Completion work follows
  // the accepted packet on this owner; idle service keeps the last CQ live.
  if (!m_txPending++) txService(true);
  enum { Batch = 64 }; // bounded CQ service during continuous submission
  if (!(m_txPending % Batch)) reconcileTx();
  return true;
}

void Instance::reconcileTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!m_backend || !m_backend->ready()) return;
  auto n = m_backend->pollTx();
  ZmAssert_(n <= m_txPending);
  m_txPending -= n;
  if (!m_txPending) txService(false);
}

void Instance::txService(bool active)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (m_txServicing == active) return;
  m_txServicing = active;
  mx()->serviceFn(mx()->txThread(), active ?
    ZmScheduler::ServiceFn{this, ZmFnPtr<&Instance::pollTx>{}} :
    ZmScheduler::ServiceFn{});
}

void Instance::pollTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready() && !m_txDraining) { txService(false); return; }
  reconcileTx();
  if (m_txDraining && (!m_txPending || !m_backend->ready())) {
    m_txDraining = false;
    stopTx();
    return;
  }
  if (!m_txPending || !m_backend->ready()) txService(false);
}

void Instance::listen(
  ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept, ZiIP local, uint16_t port,
  unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) { if (fail) fail(false); return; }
  for (auto listener = m_listeners; listener; listener = listener->ownerNext) {
    if (listener->local().ip() != local || listener->local().port() != port ||
        !listener->reopening()) continue;
    listener->reopen(ZuMv(listening), ZuMv(accept), count, allocator, ZuMv(options));
    return;
  }
  ZmRef<Listener> listener = new Listener{this, ZuMv(listening), ZuMv(fail),
    ZuMv(accept), ZiSockAddr{local, port}, count, allocator, ZuMv(options)};
  listener->ownerPrev = &m_listeners;
  listener->ownerNext = m_listeners;
  if (m_listeners) m_listeners->ownerPrev = &listener->ownerNext;
  m_listeners = listener;
  ZmREF(listener.ptr());
  listener->begin();
}

void Instance::stopListening(ZiIP local, uint16_t port)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  for (auto listener = m_listeners; listener; listener = listener->ownerNext)
    if (listener->local().ip() == local && listener->local().port() == port) listener->end();
}

void Instance::connect(
  ZiConnectFn connected, ZiFailFn fail, ZiIPType::T family, ZiIP local, uint16_t localPort,
  ZiIP remote, uint16_t remotePort, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready() || remote.type() != family || !remote || !remotePort ||
      (local && local.type() != family)) { if (fail) fail(false); return; }
  if (!reserveCxn()) { if (fail) fail(false); return; }
  ZmRef<Zutcp_::CliCxn> cxn;
  try {
    cxn = new Zutcp_::CliCxn{this, ZuMv(connected), ZuMv(fail),
      ZiSockAddr{local, localPort}, allocator, ZuMv(options)};
  } catch (...) {
    cancelCxn();
    throw;
  }
  addCxn(cxn);
  ZmREF(cxn.ptr());
  cxn->connectReserved(remote, remotePort);
}

void Instance::udp(ZiConnectFn, ZiFailFn fail, ZiIPType::T, ZiIP, uint16_t, ZiIP,
  uint16_t, Zi::IOBufAllocator *, ZiCxnOptions) { if (fail) fail(false); }

void Instance::recv(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!cxn->plugin()) { cxn->rxContext().complete(); return; }
  visit(cxn, [](auto native) {
    if (!native->disconnecting()) native->recvMx();
  });
}
void Instance::send(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!cxn->plugin()) { cxn->txContext().complete(); return; }
  visit(cxn, [](auto native) { native->sendMx(); });
}
void Instance::disconnect(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (peer) { close(cxn, true); return; }
  cxn->txUp() = 0;
  cxn->txContext().complete();
  visit(cxn, [this](auto native) {
    using C = ZuDecay<decltype(*native)>;
    mx()->rxRun([native = ZmRef<C>{native}, gen = native->dataGen()] {
      if (gen == native->dataGen() && !native->disconnecting()) native->closeMxRx();
    });
    native->shutdownMx();
  });
}

void Instance::close(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  cxn->txUp() = 0;
  cxn->txContext().complete();
  visit(cxn, [this, peer](auto native) {
    if (!peer) native->abortTCPMx();
    using C = ZuDecay<decltype(*native)>;
    mx()->rxRun([native = ZmRef<C>{native}, peer] {
      if constexpr (ZuIsSame<C, Zutcp_::CliCxn>{}) native->closeMx(peer);
      else native->State::disconnect(peer);
    });
  });
}
void Instance::telemetry(Ztc::MxTelemetry &data) const { mx()->telemetry(data); }

unsigned Instance::allCxns(AllCxnsFn fn) const
{
  ZtArray<ZmRef<ZiConnection>, ZtArrayHeapID<"Zutcp.Mx.CxnRefs">> refs;
  {
    ZmReadGuard guard(m_cxnsLock);
    refs.size(m_cxnCount);
    for (auto cxn = m_cxns; cxn; cxn = cxn->ownerNext)
      if (cxn->mxCxn && !cxn->retiring)
        new (refs.push()) ZmRef<ZiConnection>{cxn->mxCxn};
  }
  for (auto &cxn : refs) fn(cxn.ptr());
  return refs.length();
}

unsigned Instance::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  return m_backend ? m_backend->allQueues(ZuMv(fn)) : 0;
}

void Instance::watch(AddCxnFn add, DelCxnFn del)
{
  ZmGuard guard(m_watchLock);
  ZmAssert_(!m_addCxn && !m_delCxn);
  m_addCxn = ZuMv(add);
  m_delCxn = ZuMv(del);
}

void Instance::unwatch()
{
  ZmGuard guard(m_watchLock);
  m_addCxn = {};
  m_delCxn = {};
}

} // namespace Zutcp
