//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <new>
#include <stdint.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZutcpIP.hh>
#include <zlib/Zutcp.hh>
#include <zlib/ZutcpCliCxn.hh>
#include <zlib/ZutcpSrvCxn.hh>
#include <zlib/ZutcpUDP.hh>

namespace Zutcp {

Zi::IOBufAllocator *inlineAllocator()
{
  return ZmSingleton<TxBufAlloc<>>::instance();
}

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
}

ClosedTupleStats ClosedTuples::stats() const
{
  return {unsigned(m_slots.length()), m_order.count_(), m_inserted, m_evicted};
}

} // namespace Zutcp

namespace Zutcp_ {

using namespace Zutcp;

class CliCxn :
  public ZmNHeap<CliCxn>, public Zutcp::CliCxn<MxPlugin, CliCxn> {
friend inline ZuStringT<"Zutcp.Mx.CliCxn"> ZmNHeapID(CliCxn *);
  using Base = Zutcp::CliCxn<MxPlugin, CliCxn>;
public:
  using State = Base::Base;
  using Base::connectReserved;
  enum { BatchTx = true };
  using ZmNHeap<CliCxn>::operator new;
  using ZmNHeap<CliCxn>::operator delete;
  CliCxn(MxPlugin *owner, ZiConnectFn connected, ZiFailFn fail,
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
  Listener(MxPlugin *owner, ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept,
    ZiSockAddr local, unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
  : m_owner{owner}, m_listening{ZuMv(listening)}, m_fail{ZuMv(fail)},
    m_accept{ZuMv(accept)}, m_local{local}, m_count{count},
    m_allocator{allocator}, m_options{ZuMv(options)} { }
  MxPlugin *stack() const { return m_owner; }
  void endpointReady(Endpoint *);
  void endpointFailed(bool);
  void endpointDown_(Endpoint *) { }
  void disconnected(SrvCxn *, bool);
  void retireLinkRoutes(SrvCxn *);
  void cacheRoute(const ClosedTupleKey &, SrvCxn *);
  void receiveCached(SrvCxn *, ZmRef<ZiIOBuf>, const ClosedTupleKey &, unsigned, const TCPHeader &);
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
  using Endpoint::txBatch;
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr peer, EcnMark::T ecn,
    bool priority, const IPPath &path) {
    return Endpoint::send(ZuMv(buf), ZuMv(peer), ecn, priority, &path);
  }

private:
  MxPlugin *m_owner;
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
  public Zutcp::SrvCxn<MxPlugin, SrvCxn> {
friend inline ZuStringT<"Zutcp.Mx.SrvCxn"> ZmNHeapID(SrvCxn *);
  using Base = Zutcp::SrvCxn<MxPlugin, SrvCxn>;
public:
  using State = Base::Base;
  using Base::receivedRouted;
  enum { BatchTx = true };
  using State::stageReuseISNRx;
  using State::discardUnadoptedRx;
  using ZmNHeap<SrvCxn>::operator new;
  using ZmNHeap<SrvCxn>::operator delete;
  SrvCxn(MxPlugin *owner, Listener *listener, ZiCxnInfo info)
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
    m_published = m_listener->publish(this, m_info);
    if (!m_published) State::abortLocalRx(0, [this] { State::disconnect(false); });
  }
  bool cacheable() const { return m_published && !retiring && established(); }
  Listener *passiveListener() const { return m_listener; }
  void closedRx(bool peer) {
    if (retiring) return;
    m_listener->disconnected(this, peer);
  }
  void retireLinkRoutes() { m_listener->retireLinkRoutes(this); }
  ZmRef<PacketBuf> allocTxPkt_(bool control) { return m_listener->allocTxPkt_(control); }
  template <typename Run>
  TxBatchResult withTxBatch(const ZiSockAddr &peer, const IPPath &path, Run run) {
    return m_listener->txBatch(peer, &path, run);
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr peer, EcnMark::T ecn,
    bool priority, const IPPath &path) {
    return m_listener->sendPktRaw(ZuMv(buf), ZuMv(peer), ecn, priority, path);
  }
  int process(Reader &) { return 0; }
  void txDequeued() { sendMx(); }
  void abortRx() override { State::disconnect(false); }
  const Listener *listener() const override { return m_listener; }

private:
  Listener	*m_listener;
  ZiCxnInfo	m_info;
  bool		m_published = false;
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
  else if (Endpoint::tupleBound()) {
    // Zi publishes a successful passive bind on Rx before a following stop
    // barrier. Keep endpoint Tx setup queued ahead of any accepted output.
    endpointReady(this);
  }
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

void Listener::retireLinkRoutes(SrvCxn *cxn)
{
  m_owner->invalidatePassive(cxn);
  Passive::retireLinkRoutes(cxn);
}

void Listener::cacheRoute(const ClosedTupleKey &key, SrvCxn *cxn)
{
  if (cxn->cacheable()) m_owner->cachePassive(key, cxn);
}

void Listener::receiveCached(
  SrvCxn *cxn, ZmRef<ZiIOBuf> buf, const ClosedTupleKey &key, unsigned ecn,
  const TCPHeader &tcp)
{
  if (!Endpoint::admitReceive(key.p<1>(), buf->length) || !inputActive()) return;
  Datagram d{ZuMv(buf), key.p<1>(), ecnMarkFromTOS(uint8_t(ecn))};
  d.local = key.p<0>();
  d.tcp = tcp;
  Passive::deliver(cxn, ZuMv(d));
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
  m_owner->invalidatePassive();
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

// Plugin lifecycle
MxPlugin::MxPlugin() :
  m_endpoints{ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init("Zutcp.Endpoints")},
  m_udpBindings{ZmHashParams{}.bits(4).init("Zutcp.UDP.Bindings")}
{
}

MxPlugin::~MxPlugin() { if (m_xdp) final(); }

void MxPlugin::init(
  ZiMultiplex *mx_, Zpkt::Env *xdpEnv, ZmRef<Zpkt::Nif> &&xdp, Params config)
{
  ZmAssert_(mx_ && xdpEnv);
  ZmAssert_(!xdp || !xdp->allocated());
  ZmAssert_(!xdp || (xdp->mx() == mx_ && xdp->ifindex() && xdp->state() == ZmEngineState::Stopped));
  ZmAssert_(m_xdp || xdp);
  if (state() != ZmEngineState::Stopped || mx_->state() != ZmEngineState::Stopped)
    throw ZeEXCEPT(Error, "Zutcp", "finish multiplexer stop before initialization");
  if (config.heartBeat == ZuTime{0}) config.heartBeat = {};
  if (m_xdp) {
    if (xdpEnv == m_xdpEnv && mx_ == mx() &&
	(!xdp || xdp.ptr() == m_xdp.ptr()) && config == m_params) return;
    throw ZeEXCEPT(Error, "Zutcp", "conflicting repeated TCP initialization");
  }
  if (!positiveTimeout(config.txBackoff))
    throw ZeEXCEPT(Error, "Zutcp", "invalid idle Tx retry interval");
  if (!config.rtoParams.valid())
    throw ZeEXCEPT(Error, "Zutcp", "invalid RTO bounds or granularity");
  if (config.tlpPeerAckDelay < ZuTime{0})
    throw ZeEXCEPT(Error, "Zutcp", "negative peer ACK allowance");
  if (*config.heartBeat && (config.heartBeat < ZuTime{0} || config.heartBeat.nsec() < 0 ||
      config.heartBeat.nsec() >= 1000000000))
    throw ZeEXCEPT(Error, "Zutcp", "invalid TCP heartbeat interval");
  if (!config.maxPayload || config.maxPayload > BufSize)
    throw ZeEXCEPT(Error, "Zutcp", "invalid payload cap");
  auto rx = mx_->rxThread(), tx = mx_->txThread(), nThreads = mx_->params().nThreads();
  if (!rx || !tx || rx == tx || rx > nThreads || tx > nThreads)
    throw ZeEXCEPT(Error, "Zutcp", "invalid multiplexer Rx/Tx owners");
  if (!positiveTimeout(config.connectTimeout)) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid connect timeout");
  }
  if (!positiveTimeout(config.closeDrainTimeout) || !positiveTimeout(config.finWaitTimeout)) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid close timeout");
  }
  if (!positiveTimeout(config.userTimeout) && config.userTimeout != ZuTime{0}) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid user timeout");
  }
  if (config.ackHoldTimeout < ZuTime{0} ||
      config.ackHoldTimeout >= ZuTime{0, 500000000}) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid TCP ACK hold timeout");
  }
  if (!config.maxCxns || !config.maxPendingCxns || config.maxPendingCxns > config.maxCxns) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid connection admission limits");
  }
  if (!config.tfoCookieCacheSize) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid Fast Open cookie cache size");
  }
  if (!config.closedTupleRingSize) {
    throw ZeEXCEPT(Error, "Zutcp", "invalid closed tuple ring size");
  }
  if (!config.rxRetainFramesPerCxn)
    throw ZeEXCEPT(Error, "Zutcp", "invalid per-connection Rx retention limit");
  if (!config.packetCount)
    throw ZeEXCEPT(Error, "Zutcp", "empty native packet pool");
  uint64_t capacity = uint64_t(config.packetCount) + config.controlBufCount;
  if (!config.controlBufCount || !config.txControlCredits ||
      config.txControlCredits >= capacity || capacity > UINT_MAX)
    throw ZeEXCEPT(Error, "Zutcp", "invalid control reserve");
  auto mtu = xdp->mtu(), queues = xdp->rxQueues();
  if (mtu < 40 + 60 + 1 || mtu > UINT_MAX - 14 || !queues)
    throw ZeEXCEPT(Error, "Zutcp", "invalid XDP link geometry");
  // Each queue needs enough ingress backing to let a connection return its
  // oldest retained frame while continuing to receive.
  uint64_t ingressCount = mx_->rxBufSize();
  if (!ingressCount) {
    ingressCount = uint64_t(queues) * (uint64_t(config.rxRetainFramesPerCxn) + mx_->rxQuantum());
    if (ingressCount < ZiIOBuf_DefltCount) ingressCount = ZiIOBuf_DefltCount;
  }
  if (ingressCount > UINT_MAX || ingressCount < queues)
    throw ZeEXCEPT(Error, "Zutcp", "invalid ingress pool count");
  auto packets = xdp->bufPool(Zi::IOBufType::TxTCP, config.packetCount, mtu + 14, TCPHeadRoom);
  auto controls = xdp->bufPool(
    Zi::IOBufType::TxTCP, config.controlBufCount, TCPHeadRoom, TCPHeadRoom);
  if (!packets || !controls)
    throw ZeEXCEPT(Error, "Zutcp", "cannot create TCP data and control pools");
  if (!m_isn.init(xdp->ifindex()) || !m_tfoKey.init()) {
    m_isn.~ISN(); new (&m_isn) ISN;
    m_tfoKey.~TFOKey(); new (&m_tfoKey) TFOKey;
    throw ZeEXCEPT(Error, "Zutcp", "secure TCP sequence or cookie initialization failed");
  }
  m_closed.resize(config.closedTupleRingSize);
  m_tfoCookies.resize(config.tfoCookieCacheSize);
  Zi::IOBufPool *reserves[] = {packets.ptr(), controls.ptr()};
  xdp->reserveTxPools(reserves);
  new (m_allocators.new_<1>()) Allocators{mx_, packets, controls};
  m_mtu = mtu;
  m_ingressCount = unsigned(ingressCount);
  m_txCapacity = capacity;
  m_controlCredits = config.txControlCredits;
  m_packets = ZuMv(packets);
  m_control = ZuMv(controls);
  m_params = ZuMv(config);
  mx(mx_);
  m_xdpEnv = xdpEnv;
  m_xdp = ZuMv(xdp);
  return;
}

void MxPlugin::final()
{
  ZmAssert_(state() == ZmEngineState::Stopped && !unsigned(m_wakeUsers));
  ZmAssert_(!m_txServicing && !m_txPending);
  ZmAssert_(!m_endpoints.count_() && !m_udpBindings.count_());
  ZmAssert_(!m_cxns && !m_listeners && !m_cxnRetiring && !m_cxnObjects);
  ZmAssert_(!m_cxnTotal && !m_cxnPending && !m_cxnClosing);
  if (m_xdp) m_xdp->final();
  m_xdp = nullptr;
  m_xdpEnv = nullptr;
  m_packetRx.null();
  m_allocators.null();
  m_ingress = m_packets = m_control = m_requestedTx = nullptr;
  m_txCapacity = m_mtu = m_ingressCount = 0;
  m_cxnRefused = m_dataStalls = m_controlStalls = 0;
  m_params = {};
  m_isn.~ISN(); new (&m_isn) ISN;
  m_tfoKey.~TFOKey(); new (&m_tfoKey) TFOKey;
  m_closed.final();
  m_tfoCookies.final();
  m_ioState = Down;
  m_endpointDrain = DrainIdle;
  m_startFailed = false;
  m_rxPackets = 0;
  m_nextPort = m_nextUdpPort = ClientPorts::First;
  m_controlCredits = DefaultTxControlCredits;
  m_tcpClock = {};
  m_error = {};
  mx(nullptr);
}

bool MxPlugin::ready() const { return state() == ZmEngineState::Running; }

void MxPlugin::reportError(ZeException error) { ZiLogEvent(ZuMv(error)); }

void MxPlugin::start()
{
  Engine::start([this](bool ok) {
    auto error = m_error;
    if (!ok && !error) error = ZeEXCEPT(Error, "Zutcp", "TCP start was superseded");
    mx()->pluginStarted(ok ? ZiMxPlugin::StartResult{} : ZiMxPlugin::StartResult{ZuMv(error)});
  });
}

void MxPlugin::start_()
{
  ZmAssert_(rxInvoked());
  invalidatePassive();
  m_endpointDrain = DrainIdle;
  m_error = {};
  ZmAssert_(m_xdp && m_xdp->allocated());
  m_xdp->start([this](uint8_t *p, unsigned n, ZiRxBurst *burst) { received(p, n, burst); }, {}, [this](bool ok) {
    auto error = m_xdp->error();
    mx()->rxRun([this, ok, error = ZuMv(error)]() mutable {
      if (ok && m_xdp->mtu() == m_mtu) {
	openUDPRouteMonitor();
	m_tcpChksumOffload = m_xdp->tcpChksumOffload();
	m_ioState = Up;
	Engine::started(true);
	return;
      }
      m_error = ZuMv(error);
      if (!ok) {
	if (!m_error) m_error = ZeEXCEPT(Error, "Zutcp", "XDP activation failed");
	Engine::started(false);
	return;
      }
      if (!m_error)
	m_error = ZeEXCEPT(Error, "Zutcp", "interface geometry changed before packet startup");
      m_startFailed = true;
      m_ioState = Closing;
      mx()->txRun([this] { stopTx(); });
    });
  });
}

void MxPlugin::stop()
{
  Engine::stop([this](bool ok) {
    auto error = m_error;
    if (!ok && !error) error = ZeEXCEPT(Error, "Zutcp", "TCP stop was superseded");
    mx()->pluginStopped(ok ? ZiMxPlugin::StopResult{} : ZiMxPlugin::StopResult{ZuMv(error)});
  });
}

void MxPlugin::stop_()
{
  ZmAssert_(rxInvoked());
  stopCxns();
}

void MxPlugin::stopEndpoints()
{
  // Existing wake calls finish before XDP retires its wait descriptor.
  m_ioState = Closing;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  ZmAssert_(!cxnAdmission().total);
  mx()->txRun([this] { stopTx(); });
}

void MxPlugin::stopTx()
{
  ZmAssert_(txInvoked());
  if (m_senderStopping) return;
  ZmAssert_(!m_txWaitCount);
  if (m_txResumeQueued) {
    mx()->txRun([this] { stopTx(); });
    return;
  }
  if (m_xdp->ready() && m_txPending) {
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
  if (m_udpRouteFD >= 0) { ::close(m_udpRouteFD); m_udpRouteFD = -1; }
  m_xdp->stop([this](bool ok) {
    auto error = m_xdp->error();
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

void MxPlugin::wake()
{
  ++m_wakeUsers;
  if (m_ioState == Up) m_xdp->wake();
  --m_wakeUsers;
  if (m_ioState == Up && Engine::stopping() &&
      !m_endpointDrain.cmpXch(DrainQueued, DrainIdle))
    mx()->rxRun([this] {
      if (m_ioState == Up && m_endpointDrain == DrainQueued) Engine::stopped();
    });
}

// Native packet pools
Zi::IOBufPool *MxPlugin::ingress(unsigned count, unsigned align)
{
  if (m_ingress) return m_ingress;
  if (mx()->rxBufSize() || count < m_ingressCount) count = m_ingressCount;
  m_ingress = m_xdp->bufPool(Zi::IOBufType::Rx, count, m_mtu + 14, TCPHeadRoom, 0, align);
  return m_ingress;
}

Zi::IOBufPool *MxPlugin::allocator(
  Zi::IOBufType::T type, unsigned size, unsigned align, unsigned count)
{
  ZmAssert_(m_xdp && mx()->state() == ZmEngineState::Stopped);
  enum { Ethernet = 14, IPv6 = 40, TCP = 60, UDP = 8 };
  unsigned head = Ethernet + IPv6;
  switch (type) {
    case Zi::IOBufType::Rx:
    case Zi::IOBufType::TxTCP: head += TCP; break;
    case Zi::IOBufType::TxUDP: head += UDP; break;
    default: return nullptr;
  }
  if (head >= m_mtu + Ethernet || !align || (align & (align - 1)) ||
      align > unsigned(sysconf(_SC_PAGESIZE))) return nullptr;
  if (type == Zi::IOBufType::Rx) return ingress(count, align);

  if (!count || count > UINT_MAX - m_txCapacity) return nullptr;
  uint64_t bytes = uint64_t(size) + head;
  if (bytes > m_mtu + Ethernet) bytes = m_mtu + Ethernet;
  // Keep the new pool alive until the multiplexer records its owning references.
  m_requestedTx = m_xdp->bufPool(type, count, unsigned(bytes), head, 0, align);
  if (m_requestedTx) m_txCapacity += m_requestedTx->count;
  return m_requestedTx;
}

MxPlugin::AllocResult MxPlugin::alloc(Zi::IOBufPool *rx, const Zi::IOBufPoolList &tx)
{
  ZmAssert_(m_xdp && (!rx || rx == m_ingress.ptr()));
  if (!rx) rx = ingress(0, ZiIOBuf_DefltAlign);
  if (!rx) return ZeEXCEPT(Error, "Zutcp", "cannot create the ingress pool");
  if (!m_packetRx.is<RxAllocator>())
    new (m_packetRx.new_<1>()) RxAllocator{mx(), rx};
  m_requestedTx = nullptr;
  return m_xdp->alloc(rx, tx);
}

// Connection admission and lifetime
CxnAdmission MxPlugin::cxnAdmission() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return {.refused = m_cxnRefused, .total = m_cxnTotal, .pending = m_cxnPending,
    .closing = m_cxnClosing, .maxCxns = m_params.maxCxns,
    .maxPendingCxns = m_params.maxPendingCxns};
}

bool MxPlugin::reserveCxn()
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

void MxPlugin::cancelCxn()
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  ZmAssert_(m_cxnTotal && m_cxnPending);
  --m_cxnTotal;
  --m_cxnPending;
}

void MxPlugin::admitCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  ZmAssert_(!cxn.admission && m_cxnTotal && m_cxnPending);
  cxn.admission = Cxn::Admitted | Cxn::Pending;
}

void MxPlugin::establishedCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (!(cxn.admission & Cxn::Pending)) return;
  ZmAssert_(m_cxnPending);
  cxn.admission &= ~Cxn::Pending;
  --m_cxnPending;
}

void MxPlugin::closingCxn(Cxn &cxn)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (!(cxn.admission & Cxn::Admitted) || (cxn.admission & Cxn::Closing)) return;
  cxn.admission |= Cxn::Closing;
  ++m_cxnClosing;
}

void MxPlugin::releaseCxn(Cxn &cxn)
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

void MxPlugin::addCxn(Cxn *cxn)
{
  ZmAssert_(rxInvoked());
  cxn->ownerPrev = &m_cxns;
  cxn->ownerNext = m_cxns;
  if (m_cxns) m_cxns->ownerPrev = &cxn->ownerNext;
  m_cxns = cxn;
  ++m_cxnObjects;
}

void MxPlugin::delCxn(Cxn *cxn)
{
  ZmAssert_(rxInvoked());
  if (cxn->ownerNext) cxn->ownerNext->ownerPrev = cxn->ownerPrev;
  *cxn->ownerPrev = cxn->ownerNext;
  cxn->ownerNext = nullptr;
  cxn->ownerPrev = nullptr;
  ZmDEREF(cxn);
}

void MxPlugin::delListener(Listener *listener)
{
  ZmAssert_(rxInvoked());
  if (listener->ownerNext) listener->ownerNext->ownerPrev = listener->ownerPrev;
  *listener->ownerPrev = listener->ownerNext;
  listener->ownerNext = nullptr;
  listener->ownerPrev = nullptr;
  ZmDEREF(listener);
  cxnsDrained();
}

bool MxPlugin::tupleClaimed(const Listener *listener, const ClosedTupleKey &key) const
{
  for (auto cxn = m_cxns; cxn; cxn = cxn->ownerNext)
    if (cxn->listener() == listener && cxn->closedKeyRx() == key) return true;
  return false;
}

void MxPlugin::stopLinks(const Listener *listener, bool all)
{
  for (auto cxn = m_cxns; cxn;) {
    auto next = cxn->ownerNext;
    if ((listener ? cxn->listener() == listener : !cxn->listener()) &&
        (all || !cxn->established())) cxn->abortRx();
    cxn = next;
  }
}

void MxPlugin::stopCxns()
{
  ZmAssert_(rxInvoked());
  m_endpointDrain = DrainActive;
  invalidatePassive();
  for (auto listener = m_listeners; listener; listener = listener->ownerNext) listener->end(true);
  stopLinks(nullptr, true);
  cxnsDrained();
}

void MxPlugin::cxnsDrained()
{
  if (m_endpointDrain != DrainActive || m_cxns || m_listeners || m_cxnRetiring || m_cxnObjects)
    return;
  m_ioState = Closing;
  m_endpointDrain = DrainIdle;
  stopEndpoints();
}

void MxPlugin::cxnRetired(ZiFailFn fail, bool transient)
{
  ZmAssert_(rxInvoked());
  ZmAssert_(m_cxnObjects);
  --m_cxnObjects;
  if (fail) fail(transient);
  cxnsDrained();
}

template <typename C> bool MxPlugin::publish(C *cxn, ZiConnectFn &fn, ZiCxnInfo info)
{
  ZmAssert_(rxInvoked());
  if (m_endpointDrain || !fn) return false;
  auto mxCxn = fn(info);
  if (!mxCxn) return false;
  ZmREF(mxCxn);
  mxCxn->plugin(static_cast<typename C::State *>(cxn));
  cxn->mxCxn = mxCxn;
  mxCxn->connected(mxCxn->rxContext());
  if (mxCxn->rxContext().disconnected()) mxCxn->disconnect();
  else cxn->recvMx();
  return true;
}

template <typename C, typename L> void MxPlugin::retire(C *cxn, bool peer, L &&done)
{
  ZmAssert_(rxInvoked());
  ++m_cxnRetiring;
  ZmRef<ZiConnection> mxCxn;
  cxn->retiring = true;
  mxCxn = ZmRef<ZiConnection>::acquire(cxn->mxCxn);
  txRun([
    this, cxn = ZmRef<C>{cxn}, mxCxn = ZuMv(mxCxn), peer, done = ZuFwd<L>(done)
  ]() mutable {
    if constexpr (ZuIsSame<C, UdpCxn>{}) cxn->cancelTx();
    if (mxCxn) {
      mxCxn->txUp() = 0;
      mxCxn->txContext().complete();
      mxCxn->plugin(nullptr);
    }
    rxRun([
      this, cxn = ZuMv(cxn), mxCxn = ZuMv(mxCxn), peer, done = ZuMv(done)
    ]() mutable {
      if (mxCxn) {
	mxCxn->rxUp() = 0;
	mxCxn->rxContext().complete();
	cxn->mxCxn = nullptr;
      }
      done(); // Remove cold ownership while the drain reference still protects Cxn.
      cxn->releaseCxnRx();
      if (mxCxn) mxCxn->disconnected(peer);
      cxn = nullptr;
      --m_cxnRetiring;
      cxnsDrained();
    });
  });
}

template <typename L> void MxPlugin::visit(ZiConnection *mxCxn, L &&fn)
{
  if (!mxCxn->plugin()) return;
  switch (mxCxn->info().type) {
    case ZiCxnType::TCPOut:
      fn(static_cast<Zutcp_::CliCxn::State *>(mxCxn->plugin())->impl());
      break;
    case ZiCxnType::TCPIn:
      fn(static_cast<Zutcp_::SrvCxn::State *>(mxCxn->plugin())->impl());
      break;
  }
}

// Endpoint binding and routing
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

ZuUnion<void, ZeException> MxPlugin::bind(
  EndpointIndex::Node &endpoint, const ZiSockAddr &remote)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready())
    return ZeEXCEPT(Error, "Zutcp", "packet service unavailable during endpoint binding");
  switch (bindCxnTuple(endpoint, remote, m_nextPort, m_endpoints, m_closed)) {
    case CxnBindError::None: invalidatePassive(); return {};
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

bool MxPlugin::unbind(EndpointIndex::Node &endpoint)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (m_endpoints.findPtr(endpoint.bindAddr) != &endpoint) return false;
  invalidatePassive();
  m_endpoints.delNode(&endpoint);
  return true;
}

ZuUnion<IPPath, ZeException> MxPlugin::resolve(ZiIP remote, ZiIP local)
{
  ZmAssert_(rxInvoked());
  if (!ready()) return ZeEXCEPT(Error, "Zutcp", "packet service unavailable during route setup");
  return resolveIPPath(m_xdp->ifindex(), IPPath{local, remote, {}, {}, m_mtu});
}

// TCP timestamps, initial sequence numbers and Fast Open cookies
bool MxPlugin::tcpTimestamp(ZuTime now, uint32_t &out)
{
  ZmAssert_(state() == ZmEngineState::Stopped || txInvoked());
  return m_tcpClock.sample(now, out);
}

bool MxPlugin::chooseISN(const ZiSockAddr &local, const ZiSockAddr &remote, uint32_t &isn) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_isn.generate(local, remote, isn);
}

bool MxPlugin::tfoCookie(const ZiIP &client, const ZiSockAddr &server, TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoCookies.find({client, server}, cookie);
}

void MxPlugin::saveTFOCookie(
  const ZiIP &client, const ZiSockAddr &server, const TFOCookie &cookie)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  if (cookie.length) m_tfoCookies.store({client, server}, cookie.span());
  else m_tfoCookies.erase({client, server});
}

bool MxPlugin::issueTFO(const ZiSockAddr &local, const ZiIP &client, TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoKey.issue({client, local}, cookie);
}

bool MxPlugin::validTFO(
  const ZiSockAddr &local, const ZiIP &client, const TFOCookie &cookie) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_tfoKey.validate({client, local}, cookie.span());
}

// Receive and dispatch
void MxPlugin::cachePassive(const ClosedTupleKey &key, Zutcp_::SrvCxn *link)
{
  ZmAssert_(rxInvoked());
  if (!m_endpointDrain) m_passive[passiveIndex(key)] = {key, link};
}

void MxPlugin::invalidatePassive(Zutcp_::SrvCxn *link)
{
  ZmAssert_(rxInvoked());
  for (auto &route : m_passive)
    if (route.link == link) route.link = nullptr;
}

void MxPlugin::invalidatePassive()
{
  ZmAssert_(rxInvoked());
  for (auto &route : m_passive) route.link = nullptr;
}

void MxPlugin::rx()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) return;
  m_rxPackets = 0;
  m_xdp->rx();
}

void MxPlugin::received(uint8_t *p, unsigned n, ZiRxBurst *burst)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  IPView ip;
  TCPView tcp;
  EndpointIndex::Node *binding = nullptr;
  Zutcp_::SrvCxn *cached = nullptr;
  ClosedTupleKey key;
  bool validIP = readIP({p, n}, ip);
  if (validIP && ip.protocol == IPPROTO_UDP) {
    receivedUDP(p, n, ip, burst);
    rxProgress();
    return;
  }
  if (burst) burst->flush();
  bool valid = validIP && readTCP(ip, tcp);
#if 0
  if (!m_tcpChksumOffload) {
    if (valid && tcpChecksum(ip.local, ip.remote, ip.payload)) valid = false;
  }
#endif
  if (valid) {
    key = {ZiSockAddr{ip.local, tcp.header.dstPort}, ZiSockAddr{ip.remote, tcp.header.srcPort}};
    if (!(tcp.header.flags & 0x02)) {
      const auto &route = m_passive[passiveIndex(key)];
      if (route.link && route.key == key) cached = route.link;
    }
    if (!cached) {
      binding = m_endpoints.findPtr(key.p<0>());
      if (!binding) {
	ZiIP any;
	if (ip.local.v6()) any = in6addr_any;
	else any = in_addr{};
	binding = m_endpoints.findPtr(ZiSockAddr{any, tcp.header.dstPort});
      }
    }
  }
  // A current owner qualifies input before consulting retired tuple history.
  // In particular, newer-TS reuse can choose a peer ISN below an old frontier.
  bool closed = valid && !cached && !binding && closedPacket(p, ip, tcp);
  if (closed) m_ingress->free(p);
  else if (cached || binding || m_params.packet) {
    ZmRef<Zi::IOBuf> buf = new Zi::IOBufStub<Zi::IOBuf, ZiIOBuf_HeapID>{
      p, n, &m_packetRx.p<1>(), this, n, false, true};
    if (cached || binding) {
      buf->skip = tcp.payload.data() - p;
      buf->length = tcp.payload.length();
      if (cached)
	cached->passiveListener()->receiveCached(cached, ZuMv(buf), key, ip.ecn, tcp.header);
      else
	binding->receive(binding, ZuMv(buf), key.p<0>(), key.p<1>(), ip.ecn, tcp.header);
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
  rxProgress();
}

void MxPlugin::rxProgress()
{
  if (++m_rxPackets == mx()->rxQuantum()) {
    m_xdp->wake();
    mx()->push([this] { rx(); }, mx()->rxThread());
  }
}

// Closed tuples and control replies
ClosedTuple MxPlugin::rememberClosed(ClosedTuple record)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_closed.remember(ZuMv(record));
}

bool MxPlugin::closedTuple(const ClosedTupleKey &key, ClosedTuple &record) const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.find(key, record);
}

bool MxPlugin::updateClosed(const ClosedTuple &record)
{
  ZmAssert_(state() == ZmEngineState::Stopped || rxInvoked());
  return m_closed.update(record);
}

ClosedTupleStats MxPlugin::closedTupleStats() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.stats();
}

bool MxPlugin::recoverClosed(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14) return false;
  IPView ip{.local = d.local.ip(), .remote = d.addr.ip()};
  TCPView tcp{.header = d.tcp, .payload = d.buf->cspan()};
  return closedPacket(d.buf->data_(), ip, tcp);
}

bool MxPlugin::closedPacket(const uint8_t *frame, const IPView &ip, const TCPView &tcp)
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

void MxPlugin::replyClosed(ClosedTuple record, IPPath path)
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
  Zutcp::TCPLayout layout{14, uint16_t(ipHead(path.local.type()))};
  if (sendPacket(packet, nullptr, &layout) && record.timestamps)
    mx()->rxRun([this, key = record.key, id = record.id, sent = tcp.options.tsval] {
      ClosedTuple current;
      if (m_closed.find(key, current) && current.id == id) {
	current.lastSentTS = sent;
	m_closed.update(current);
      }
    });
}

void MxPlugin::resetUnmatched(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14 || (d.tcp.flags & 0x04)) return;
  IPPath path{.local = d.local.ip(), .remote = d.addr.ip(), .mtu = m_mtu};
  memcpy(path.localMAC, d.buf->data_(), 6);
  memcpy(path.remoteMAC, d.buf->data_() + 6, 6);
  resetReply(d.tcp, d.buf->length, path);
}

void MxPlugin::resetReply(const TCPHeader &incoming, unsigned payload, IPPath path)
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

void MxPlugin::replyControl(TCPHeader tcp, IPPath path)
{
  auto packet = allocPacket(this, path.local.type(), true);
  if (!packet) return;
  unsigned wire = writeTCP(packet->data_(), packet->size, path, tcp, 0);
  if (!wire) return;
  packet->skip = 0;
  packet->length = wire;
  Zutcp::TCPLayout layout{14, uint16_t(ipHead(path.local.type()))};
  sendPacket(packet, nullptr, &layout);
}

// Packet submission and completion service
ZmRef<PacketBuf> MxPlugin::allocPacket(void *owner, ZiIPType::T family, bool control)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  auto head = tcpHead(family);
  if (!ready() || !head) return nullptr;
  ZmRef<PacketBuf> buf = control ? m_allocators.p<1>().control.allocBuf(owner) :
    m_allocators.p<1>().tx.allocBuf(owner);
  if (control && !buf) ++m_controlStalls;
  if (buf) buf->skip = head;
  return buf;
}

bool MxPlugin::sendPacket(
  const ZmRef<Zi::IOBuf> &buf, PacketLease *lease, const Zutcp::TCPLayout *layout,
  TxRetry *retry)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (retry) retry->refused = false;
  if (!ready() || !buf || !buf->registered() || !buf->length ||
      buf->skip > buf->size || buf->length > buf->size - buf->skip) return false;
  bool control = buf->allocator_ == &m_allocators.p<1>().control;
  if (submitPacket(buf->data(), buf->length, control, layout, retry) != Zi::OK) return false;
  // Transfer the caller lease to recovery when requested. The independent
  // kernel lease ends at CQ; neither lease retains submitted Zi metadata.
  auto backing = buf->data_();
  auto size = buf->size;
  auto allocator = buf->allocator_;
  buf->data__ = 0;
  buf->size = buf->skip = buf->length = 0;
  bool nativePacket = control || allocator == &m_allocators.p<1>().tx;
  if (!lease && nativePacket) lease = static_cast<PacketBuf *>(buf.ptr())->retained;
  if (lease) lease->adopt(buf->allocator(), backing, size);
  else allocator->free(backing, size, true);
  submittedPackets(1);
  return true;
}

Zu::IO::Result MxPlugin::submitPacket(
  uint8_t *packet, unsigned length, bool control, const Zutcp::TCPLayout *layout, TxRetry *retry)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready()) return Zi::IOError;
  uint64_t limit = control ? m_txCapacity : m_txCapacity - m_controlCredits;
  // Reconcile only when completion credit is required for this admission.
  if (m_txPending >= limit) reconcileTx();
  if (m_txPending >= limit) {
    if (control) ++m_controlStalls; else ++m_dataStalls;
    return m_xdp->ready() ? Zi::NotReady : Zi::IOError;
  }
  unsigned previous = 0;
  if (layout) {
    if (layout->tcp > length || length - layout->tcp < 20) return Zi::IOError;
    previous = (unsigned(packet[layout->tcp + 16])<<8) | packet[layout->tcp + 17];
    if (!tcpChecksum(packet, length, *layout, m_tcpChksumOffload)) return Zi::IOError;
  }
  auto result = m_xdp->send(packet, length);
  if (retry && !m_txPending && result == Zi::NotReady) {
    unsigned left = retry->spin ? m_params.txRetries : 0;
    while (left && result == Zi::NotReady) {
      --left;
      result = ready() ? m_xdp->send(packet, length) : Zi::IOError;
    }
    retry->refused = result == Zi::NotReady;
  }
  if (result == Zi::OK) return result;
  if (layout) {
    packet[layout->tcp + 16] = previous>>8;
    packet[layout->tcp + 17] = previous;
  }
  return result;
}

unsigned MxPlugin::txDataSlots() const
{
  ZmAssert_(txInvoked());
  uint64_t limit = m_txCapacity - m_controlCredits;
  return m_txPending < limit ? unsigned(limit - m_txPending) : 0;
}

unsigned MxPlugin::submitPackets(
  ZuSpan<const Zpkt::TxPacket> packets, const TCPLayout &layout, TxRetry &retry)
{
  ZmAssert_(txInvoked() && packets.length() <= txDataSlots());
  retry.refused = false;
  if (!ready() || !packets.length()) return 0;

  unsigned prepared = 0;
  for (const auto &packet : packets) {
    if (!tcpChecksum(packet.data, packet.length, layout, m_tcpChksumOffload)) break;
    ++prepared;
  }
  if (!prepared) return 0;
  ZuSpan<const Zpkt::TxPacket> prefix{packets.data(), prepared};
  unsigned accepted = m_xdp->send(prefix);
  if (!accepted && !m_txPending) {
    unsigned left = retry.spin ? m_params.txRetries : 0;
    while (ready() && m_xdp->ready()) {
      if (!left) { retry.refused = true; break; }
      --left;
      if ((accepted = m_xdp->send(prefix))) break;
    }
  }
  // writeTCP initializes this field to zero. Keep refused bytes resubmittable.
  for (unsigned i = accepted; i < prepared; ++i) {
    auto checksum = packets[i].data + layout.tcp + 16;
    checksum[0] = checksum[1] = 0;
  }
  return accepted;
}

void MxPlugin::submittedPackets(unsigned count, Cxn *waiter)
{
  ZmAssert_(count);
  unsigned before = m_txPending;
  m_txPending += count;
  if (!before) txService(true);
  if (waiter) waitTx(waiter);
  // Count the complete accepted prefix before a provider can retire it.
  if ((before >> 6) != (m_txPending >> 6)) reconcileTx();
}

TxAdmission MxPlugin::txAdmission() const
{
  ZmAssert_(state() == ZmEngineState::Stopped || mx()->invoked(mx()->txThread()));
  unsigned count = m_control ? m_control->count : 0;
  return {.capacity = m_txCapacity, .dataStalls = m_dataStalls,
    .controlStalls = m_controlStalls, .reserved = m_controlCredits,
    .outstanding = m_txPending, .controlCount = count};
}

void MxPlugin::txService(bool active)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (m_txServicing == active) return;
  m_txServicing = active;
  mx()->serviceFn(mx()->txThread(), active ?
    ZmScheduler::ServiceFn{this, ZmFnPtr<&MxPlugin::pollTx>{}} :
    ZmScheduler::ServiceFn{});
}

void MxPlugin::pollTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready() && !m_txDraining) { txService(false); return; }
  reconcileTx();
  bool providerUp = m_xdp->ready();
  if (m_txDraining && (!m_txPending || !providerUp)) {
    m_txDraining = false;
    stopTx();
    return;
  }
  if (!providerUp && m_txWaitCount) {
    m_txResumeFailed = true;
    m_txResumeBudget = m_txWaitCount;
    scheduleTxResume();
  }
  if (!m_txPending || !providerUp) txService(false);
}

void MxPlugin::reconcileTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!m_xdp || !m_xdp->ready()) return;
  auto n = m_xdp->pollTx();
  ZmAssert_(n <= m_txPending);
  m_txPending -= n;
  if (n && m_txWaitCount) {
    if (m_txResumeBudget < m_txWaitCount) m_txResumeBudget = m_txWaitCount;
    scheduleTxResume();
  }
  if (!m_txPending) txService(false);
}

void MxPlugin::waitTx(Cxn *cxn)
{
  if (!ready() || !m_txPending || cxn->txWaitPrev || !m_xdp->ready()) return;
  ZmAssert_(txInvoked());
  cxn->txWaitNext = nullptr;
  cxn->txWaitPrev = m_txWaitTail;
  *m_txWaitTail = cxn;
  m_txWaitTail = &cxn->txWaitNext;
  ++m_txWaitCount;
}

void MxPlugin::cancelTxWait(Cxn *cxn)
{
  if (!cxn->txWaitPrev) return;
  ZmAssert_(txInvoked());
  if (cxn->txWaitNext) cxn->txWaitNext->txWaitPrev = cxn->txWaitPrev;
  else m_txWaitTail = cxn->txWaitPrev;
  *cxn->txWaitPrev = cxn->txWaitNext;
  cxn->txWaitNext = nullptr;
  cxn->txWaitPrev = nullptr;
  --m_txWaitCount;
}

void MxPlugin::scheduleTxResume()
{
  if (m_txResumeQueued || !m_txResumeBudget) return;
  m_txResumeQueued = true;
  // Never reenter a packet's publication/recovery accounting from admission.
  mx()->txRun([this] { resumeTx(); });
}

void MxPlugin::resumeTx()
{
  ZmAssert_(txInvoked());
  m_txResumeQueued = false;
  if (!ready()) { m_txResumeBudget = 0; m_txResumeFailed = false; return; }
  enum { Batch = 64 };
  auto result = m_txResumeFailed || !m_xdp->ready() ? Zi::IOError : Zi::OK;
  for (unsigned i = 0; i < Batch && m_txResumeBudget && m_txWait; ++i) {
    --m_txResumeBudget;
    auto cxn = m_txWait;
    cancelTxWait(cxn);
    cxn->resumeTx(result);
  }
  if (!m_txWait) m_txResumeBudget = 0;
  if (!m_txResumeBudget) m_txResumeFailed = false;
  scheduleTxResume();
}

// Zi connections
void MxPlugin::listen(
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

void MxPlugin::stopListening(ZiIP local, uint16_t port)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  for (auto listener = m_listeners; listener; listener = listener->ownerNext)
    if (listener->local().ip() == local && listener->local().port() == port) listener->end();
}

void MxPlugin::connect(
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

bool MxPlugin::publishUDP(UdpCxn *endpoint, ZiConnectFn &fn, ZiCxnInfo info)
{
  admitCxn(*endpoint);
  establishedCxn(*endpoint);
  addCxn(endpoint);
  ZmREF(endpoint);
  m_udpBindings.addNode(endpoint);
  return publish(endpoint, fn, ZuMv(info));
}

void MxPlugin::retireUDP(UdpCxn *endpoint, bool peer)
{
  retire(endpoint, peer, [this, endpoint] { delCxn(endpoint); });
}

void MxPlugin::recv(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!cxn->plugin()) { cxn->rxContext().complete(); return; }
  if (cxn->info().type == ZiCxnType::UDP) return;
  visit(cxn, [](auto native) {
    if (!native->disconnecting()) native->recvMx();
  });
}

void MxPlugin::send(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!cxn->plugin()) { cxn->txContext().complete(); return; }
  if (cxn->info().type == ZiCxnType::UDP) { sendUDP(cxn); return; }
  visit(cxn, [](auto native) { native->sendMx(); });
}

void MxPlugin::disconnect(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  auto &io = cxn->txContext();
  if (!peer && io.closed()) { close(cxn, false); return; }
  if (!peer && cxn->txUp() && cxn->plugin() && !io.completed() && io.buf) {
    io.disconnect();
    send(cxn);
    return;
  }
  if (cxn->info().type == ZiCxnType::UDP) { closeUDP(cxn, peer); return; }
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

void MxPlugin::close(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (cxn->info().type == ZiCxnType::UDP) { closeUDP(cxn, peer); return; }
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

} // namespace Zutcp
