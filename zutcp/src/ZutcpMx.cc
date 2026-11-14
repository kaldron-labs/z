//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZutcpIP.hh>
#include <zlib/Zutcp.hh>

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
  m_index->clean();
  m_order = ZuMv(next.m_order);
  m_index = ZuMv(next.m_index);
  m_slots = ZuMv(next.m_slots);
  if (count > capacity) m_evicted += count - capacity;
}

ClosedTupleStats ClosedTuples::stats() const
{
  return {unsigned(m_slots.length()), m_order.count_(), m_inserted, m_evicted};
}

CxnSlot::CxnSlot(CxnSlot &&other)
: m_owner{other.m_owner}, m_pending{other.m_pending}, m_closing{other.m_closing}
{
  other.m_owner = nullptr;
}
CxnSlot &CxnSlot::operator =(CxnSlot &&other)
{
  ZmAssert_(!m_owner);
  m_owner = other.m_owner;
  m_pending = other.m_pending;
  m_closing = other.m_closing;
  other.m_owner = nullptr;
  return *this;
}
CxnSlot::~CxnSlot() { ZmAssert_(!m_owner); }
void CxnSlot::established()
{
  if (!m_owner || !m_pending) return;
  m_pending = false;
  m_owner->established();
}
void CxnSlot::closing()
{
  if (!m_owner || m_closing) return;
  m_closing = true;
  m_owner->closing();
}
void CxnSlot::release()
{
  if (!m_owner) return;
  auto owner = m_owner;
  m_owner = nullptr;
  owner->release(m_pending, m_closing);
}
CxnSlots::~CxnSlots() { ZmAssert_(!m_total && !m_pending && !m_closing); }
CxnSlot CxnSlots::reserve(unsigned maxCxns, unsigned maxPendingCxns)
{
  if (m_total >= maxCxns || m_pending >= maxPendingCxns) {
    ++m_refused;
    return {};
  }
  ++m_total;
  ++m_pending;
  return CxnSlot{this};
}
void CxnSlots::established() { ZmAssert_(m_pending); --m_pending; }
void CxnSlots::closing() { ++m_closing; }
void CxnSlots::release(bool pending, bool closing)
{
  ZmAssert_(m_total && (!pending || m_pending) && (!closing || m_closing));
  --m_total;
  if (pending) --m_pending;
  if (closing) --m_closing;
}
CxnAdmission CxnSlots::admission(unsigned maxCxns, unsigned maxPendingCxns) const
{
  return {.refused = m_refused, .total = m_total, .pending = m_pending,
    .closing = m_closing, .maxCxns = maxCxns, .maxPendingCxns = maxPendingCxns};
}

// This owns native application lifetimes. Association callbacks navigate the
// existing Connection pointer; the lists are cold ownership/iteration state.
class Stack::Connections : public ZmNHeap<Connections> {
friend inline ZuStringT<"Zutcp.Mx.Connections"> ZmNHeapID(Connections *);
public:
  class Client;
  class Cli;
  class Listener;
  class Srv;
  struct CliLinks {
    Cli *ownerNext = nullptr;
    Cli **ownerPrev = nullptr;
  };
  struct ListenerLinks {
    Listener *ownerNext = nullptr;
    Listener **ownerPrev = nullptr;
  };
  using Done = ZmFn<void(), ZmFnHeapID<"Zutcp.Mx.Drain">>;

  Connections(Stack *stack) : m_stack{stack} { }
  ~Connections();
  Stack *stack() const { return m_stack; }
  Client *client() const { return m_client; }
  void connect(ZiConnectFn, ZiFailFn, ZiIP, uint16_t, ZiIP, uint16_t,
    Zi::IOBufAllocator *, ZiCxnOptions);
  void listen(ZiListenFn, ZiFailFn, ZiConnectFn, ZiIP, uint16_t, unsigned,
    Zi::IOBufAllocator *, ZiCxnOptions);
  void stopListening(ZiIP, uint16_t);
  void stop(Done);
  void remove(Cli *);
  void remove(Listener *);
  void drained();
  void objectRetired(ZiFailFn, bool);
  template <typename C> bool publish(C *, ZiConnectFn &, ZiCxnInfo);
  template <typename C, typename L> void retire(C *, bool, L &&);
  template <typename L> static void visit(ZiConnection *, L &&);

private:
  template <typename P> P params() const;
  Stack *m_stack;
  Client *m_client = nullptr;
  Cli *m_clients = nullptr;
  Listener *m_listeners = nullptr;
  unsigned m_retiring = 0, m_objects = 0;
  bool m_stopping = false;
  Done m_done;
};

class Stack::Connections::Client : public ZmNHeap<Client>, public Zutcp::Client<Client> {
friend inline ZuStringT<"Zutcp.Mx.Client"> ZmNHeapID(Client *);
public:
  using ZmNHeap<Client>::operator new;
  using ZmNHeap<Client>::operator delete;
  bool ioActive() const { return mx(); }
  bool init(ClientParams params, Stack *stack) {
    return init_(ZuMv(params), Vantage::Client,
      [](const ClientParams &) { return true; }, stack);
  }
};

class Stack::Connections::Cli :
  public ZmNHeap<Cli>, public CliLinks, public CliCxn<Client, Cli> {
friend inline ZuStringT<"Zutcp.Mx.CliCxn"> ZmNHeapID(Cli *);
friend class Connections;
  using Base = CliCxn<Client, Cli>;
public:
  using State = Base::Base;
  using Base::connectReserved;
  using ZmNHeap<Cli>::operator new;
  using ZmNHeap<Cli>::operator delete;
  Cli(Connections *owner, ZiConnectFn connected, ZiFailFn fail,
    ZiSockAddr local, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
  : Base{owner->client()}, m_owner{owner}, m_connected{ZuMv(connected)},
    m_fail{ZuMv(fail)}, m_local{local}, m_allocator{allocator}, m_options{ZuMv(options)} { }
  ~Cli() {
    // Leave the complete derived/base destructor chain on either I/O owner
    // before publishing object retirement back to the Rx-owned application.
    app()->txRun([owner = m_owner,
      fail = ZuMv(m_fail), transient = m_transient]() mutable {
      owner->stack()->mx()->rxRun([
	owner, fail = ZuMv(fail), transient
      ]() mutable {
	owner->objectRetired(ZuMv(fail), transient);
      });
    });
  }
  ZiSockAddr localBind() const { return m_local; }
  bool deferCxnRelease() const { return true; }
  void connected(Connected) {
    ZiCxnInfo info{.type = ZiCxnType::TCPOut, .socket = Zi::nullSocket(),
      .options = m_options, .local = local(), .remote = remote(),
      .mps = dataMPS(), .rxAllocator = m_allocator};
    if (!m_owner->publish(this, m_connected, ZuMv(info))) abort();
    m_connected = {};
    m_fail = {};
  }
  void closeMx(bool peer) { m_peer = peer; abort(); }
  void connectFailed(bool transient) {
    m_transient = transient;
  }
  void disconnected(bool peer) {
    if (m_finishing) return;
    m_finishing = true;
    m_owner->retire(this, peer || m_peer, [this] {
      m_owner->remove(this);
    });
  }
  int process(Reader &) { return 0; }
  void txDequeued() { sendMx(); }

private:
  Connections *m_owner;
  ZiConnectFn m_connected;
  ZiFailFn m_fail;
  ZiSockAddr m_local;
  Zi::IOBufAllocator *m_allocator;
  ZiCxnOptions m_options;
  bool m_transient = false, m_finishing = false, m_peer = false;
};

class Stack::Connections::Listener : public ZmNHeap<Listener>,
  public ListenerLinks, public Server<Listener, Srv> {
friend inline ZuStringT<"Zutcp.Mx.Listener"> ZmNHeapID(Listener *);
friend class Connections;
  using Base = Server<Listener, Srv>;
public:
  using ZmNHeap<Listener>::operator new;
  using ZmNHeap<Listener>::operator delete;
  Listener(Connections *owner, ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept,
    ZiSockAddr local, unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
  : m_owner{owner}, m_listening{ZuMv(listening)}, m_fail{ZuMv(fail)},
    m_accept{ZuMv(accept)}, m_local{local}, m_count{count},
    m_allocator{allocator}, m_options{ZuMv(options)} { }
  bool ioActive() const { return mx(); }
  void wake() { rxRun([this] { this->stopped(); }); }
  bool init(ServerParams params, Stack *stack) {
    return init_(ZuMv(params), Vantage::Server,
      [](const ServerParams &) { return true; }, stack);
  }
  ZiIP localIP() const { return m_local.ip(); }
  uint16_t localPort() const { return m_local.port(); }
  ZmRef<Srv> accepted(const InitialInfo &);
  void disconnected(Srv *, bool);
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
  ZiFailFn takeFailure() { return ZuMv(m_fail); }
  void objectAdded() { ++m_objects; }
  void objectRetired() { --m_objects; drained(); }
  bool publish(Srv *, ZiCxnInfo);
  void begin();
  void end(bool all = false);
  void retired();
  void drained();

private:
  Connections *m_owner;
  ZiListenFn m_listening;
  ZiFailFn m_fail;
  ZiConnectFn m_accept;
  ZiSockAddr m_local;
  unsigned m_count, m_active = 0, m_objects = 0;
  Zi::IOBufAllocator *m_allocator;
  ZiCxnOptions m_options;
  bool m_closing = false, m_stopping = false, m_stopped = false, m_finalQueued = false;
};

class Stack::Connections::Srv : public ZmNHeap<Srv>, public SrvCxn<Listener, Srv> {
friend inline ZuStringT<"Zutcp.Mx.SrvCxn"> ZmNHeapID(Srv *);
  using Base = SrvCxn<Listener, Srv>;
public:
  using State = Base::Base;
  using ZmNHeap<Srv>::operator new;
  using ZmNHeap<Srv>::operator delete;
  Srv(Listener *listener, ZiCxnInfo info) : Base{listener}, m_info{ZuMv(info)} {
    listener->objectAdded();
  }
  ~Srv() {
    app()->txRun([listener = app()] {
      listener->mx()->rxRun([listener] {
	listener->objectRetired();
      });
    });
  }
  bool deferCxnRelease() const { return true; }
  void connected(Connected) {
    m_info.mps = dataMPS();
    if (!app()->publish(this, m_info))
      State::abortLocalRx(0, [this] { State::disconnect(false); });
  }
  int process(Reader &) { return 0; }
  void txDequeued() { sendMx(); }

private:
  ZiCxnInfo m_info;
};

template <typename P> P Stack::Connections::params() const
{
  const auto &config = m_stack->config();
  P params{m_stack->mx()};
  params.controlBufCount(config.controlBufCount).txControlCredits(config.txControlCredits)
    .rxRetainFrames(config.rxRetainFrames).rxRetainFramesPerCxn(config.rxRetainFramesPerCxn)
    .rxReserveFrames(config.rxReserveFrames ? config.rxReserveFrames : 1)
    .maxData(config.maxData).maxCxns(config.maxCxns).maxPendingCxns(config.maxPendingCxns)
    .closedTupleRingSize(config.closedTupleRingSize)
    .tfoCookieCacheSize(config.tfoCookieCacheSize)
    .connectTimeout(config.connectTimeout).userTimeout(config.userTimeout)
    .closeDrainTimeout(config.closeDrainTimeout).finWaitTimeout(config.finWaitTimeout);
  return params;
}

Stack::Connections::~Connections()
{
  ZmAssert_(!m_clients && !m_listeners && !m_retiring && !m_objects);
  if (m_client) { m_client->final(); delete m_client; }
}

void Stack::Connections::connect(
  ZiConnectFn connected, ZiFailFn fail, ZiIP local, uint16_t localPort,
  ZiIP remote, uint16_t remotePort, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  auto slot = m_stack->reserveCxn();
  if (!slot) { if (fail) fail(false); return; }
  if (!m_client) {
    m_client = new Client;
    if (!m_client->init(params<ClientParams>(), m_stack)) {
      delete m_client;
      m_client = nullptr;
      m_stack->releaseCxn(slot);
      if (fail) fail(false);
      return;
    }
  }
  ZmRef<Cli> cxn = new Cli{this, ZuMv(connected), ZuMv(fail),
    ZiSockAddr{local, localPort}, allocator, ZuMv(options)};
  ++m_objects;
  cxn->ownerPrev = &m_clients;
  cxn->ownerNext = m_clients;
  if (m_clients) m_clients->ownerPrev = &cxn->ownerNext;
  m_clients = cxn;
  ZmREF(cxn.ptr()); // active native connection ownership
  cxn->connectReserved(remote, remotePort, ZuMv(slot));
}

void Stack::Connections::listen(
  ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept, ZiIP local, uint16_t port,
  unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  for (auto listener = m_listeners; listener; listener = listener->ownerNext) {
    if (listener->local().ip() != local || listener->local().port() != port ||
	!listener->reopening()) continue;
    listener->reopen(ZuMv(listening), ZuMv(accept), count, allocator, ZuMv(options));
    return;
  }
  ZmRef<Listener> listener = new Listener{this, ZuMv(listening), ZuMv(fail),
    ZuMv(accept), ZiSockAddr{local, port}, count, allocator, ZuMv(options)};
  if (!listener->init(params<ServerParams>(), m_stack)) {
    auto failed = listener->takeFailure();
    if (failed) failed(false);
    return;
  }
  listener->ownerPrev = &m_listeners;
  listener->ownerNext = m_listeners;
  if (m_listeners) m_listeners->ownerPrev = &listener->ownerNext;
  m_listeners = listener;
  ZmREF(listener.ptr());
  listener->begin();
}

bool Stack::Connections::Listener::publish(Srv *cxn, ZiCxnInfo info)
{
  return !m_closing && m_owner->publish(cxn, m_accept, ZuMv(info));
}

void Stack::Connections::Listener::begin()
{
  HubCtl::start([this](bool ok) {
    if (ok && !m_closing) {
      auto listening = ZuMv(m_listening);
      m_fail = {};
      if (listening) listening(ZiListenInfo{.socket = Zi::nullSocket(), .nAccepts = m_count,
        .ip = local().ip(), .port = local().port(), .options = m_options,
        .rxAllocator = m_allocator});
    } else {
      auto fail = ZuMv(m_fail);
      m_listening = {};
      if (fail) fail(false);
      if (!ok) { m_stopped = true; drained(); }
    }
  });
}

ZmRef<Stack::Connections::Srv> Stack::Connections::Listener::accepted(const InitialInfo &info)
{
  if (m_closing) return {};
  ++m_active;
  return new Srv{this, {.type = ZiCxnType::TCPIn, .socket = Zi::nullSocket(),
    .options = m_options, .local = info.local, .remote = info.peer,
    .rxAllocator = m_allocator}};
}

void Stack::Connections::Listener::disconnected(Srv *cxn, bool peer)
{
  ZmRef<Srv> hold{cxn};
  Base::disconnected(cxn, peer);
  m_owner->retire(cxn, peer, [this] { retired(); });
}

void Stack::Connections::Listener::end(bool all)
{
  m_closing = true;
  if (m_stopping) return;
  Base::allLinks_([all](Ztc::Link *link) {
    auto cxn = static_cast<Srv *>(link);
    if (all || !cxn->established()) cxn->State::disconnect(false);
  });
  if (!all && m_active) return;
  m_stopping = true;
  HubCtl::stop([this](bool) { m_stopped = true; drained(); });
}

void Stack::Connections::Listener::retired()
{
  ZmAssert_(m_active);
  --m_active;
  if (m_closing && !m_active && !m_stopping) end();
  else drained();
}

void Stack::Connections::Listener::drained()
{
  if (!m_stopped || m_active || m_objects || m_finalQueued) return;
  m_finalQueued = true;
  // Finalization may release the listener; leave all engine/endpoint callbacks
  // before final() clears their application state.
  mx()->rxRun([listener = ZmRef<Listener>{this}] {
    listener->final();
    listener->m_owner->remove(listener);
  });
}

void Stack::Connections::stopListening(ZiIP ip, uint16_t port)
{
  for (auto listener = m_listeners; listener; listener = listener->ownerNext)
    if (listener->local().ip() == ip && listener->local().port() == port)
      listener->end();
}

void Stack::Connections::stop(Done done)
{
  m_stopping = true;
  m_done = ZuMv(done);
  for (auto listener = m_listeners; listener; listener = listener->ownerNext) listener->end(true);
  for (auto cxn = m_clients; cxn; cxn = cxn->ownerNext) cxn->abort();
  drained();
}

void Stack::Connections::remove(Cli *cxn)
{
  if (cxn->ownerNext) cxn->ownerNext->ownerPrev = cxn->ownerPrev;
  *cxn->ownerPrev = cxn->ownerNext;
  cxn->ownerNext = nullptr;
  cxn->ownerPrev = nullptr;
  ZmDEREF(cxn);
}

void Stack::Connections::remove(Listener *listener)
{
  if (listener->ownerNext) listener->ownerNext->ownerPrev = listener->ownerPrev;
  *listener->ownerPrev = listener->ownerNext;
  listener->ownerNext = nullptr;
  listener->ownerPrev = nullptr;
  ZmDEREF(listener);
  drained();
}

void Stack::Connections::drained()
{
  if (!m_stopping || m_clients || m_listeners || m_retiring || m_objects) return;
  // Retired associations can still have endpoint-close references. Wait for
  // actual connection destruction before releasing their application state.
  if (m_client) { m_client->final(); delete m_client; m_client = nullptr; }
  m_stopping = false;
  auto done = ZuMv(m_done);
  if (done) done();
}

void Stack::Connections::objectRetired(ZiFailFn fail, bool transient)
{
  ZmAssert_(m_objects);
  --m_objects;
  if (fail) fail(transient);
  drained();
}

template <typename C> bool Stack::Connections::publish(C *cxn, ZiConnectFn &fn, ZiCxnInfo info)
{
  if (m_stopping || !fn) return false;
  auto mx = fn(info);
  if (!mx) return false;
  ZmREF(mx);
  cxn->mxCxn = mx;
  mx->plugin(static_cast<typename C::State *>(cxn));
  m_stack->observed(cxn);
  mx->connected(mx->rxContext());
  if (mx->rxContext().disconnected()) mx->disconnect();
  else cxn->recvMx();
  return true;
}

template <typename C, typename L> void Stack::Connections::retire(C *cxn, bool peer, L &&done)
{
  ++m_retiring;
  auto mx = ZmRef<ZiConnection>::acquire(cxn->mxCxn);
  m_stack->mx()->txRun([
    this, cxn = ZmRef<C>{cxn}, mx = ZuMv(mx), peer, done = ZuFwd<L>(done)
  ]() mutable {
    if (mx) {
      mx->txUp() = 0;
      mx->txContext().complete();
      mx->plugin(nullptr);
    }
    m_stack->mx()->rxRun([
      this, cxn = ZuMv(cxn), mx = ZuMv(mx), peer, done = ZuMv(done)
    ]() mutable {
      if (mx) {
	mx->rxUp() = 0;
	mx->rxContext().complete();
        m_stack->unobserved(cxn);
      }
      done(); // Remove cold ownership while the drain reference still protects Cxn.
      cxn->releaseCxnSlotRx();
      if (mx) mx->disconnected(peer);
      cxn = nullptr;
      --m_retiring;
      drained();
    });
  });
}

template <typename L> void Stack::Connections::visit(ZiConnection *mx, L &&fn)
{
  if (!mx->plugin()) return;
  switch (mx->info().type) {
    case ZiCxnType::TCPOut:
      fn(static_cast<Cli::State *>(mx->plugin())->impl());
      break;
    case ZiCxnType::TCPIn:
      fn(static_cast<Srv::State *>(mx->plugin())->impl());
      break;
  }
}

static unsigned requireMTU(ZiMultiplex *mx, unsigned interface)
{
  ZtString<> error;
  auto mtu = mx ? interfaceMTU(interface, error) : 0;
  if (!mtu) throw ZeEXCEPT(Error, "Zutcp", ([error](auto &s) {
    s << "packet interface configuration: " << error;
  }));
  return mtu;
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

Stack::Stack(ZiMultiplex *mx, StackParams params)
: ZiMxPlugin{mx}, m_interface{requireTCPSecrets(m_isn, m_tfoKey, params.interface)},
  m_mtu{requireMTU(mx, params.interface)},
  m_spin{params.spin},
  m_endpoints{new EndpointBindings{ZmHashParams{}.bits(5).loadFactor(1).cBits(3)}},
  m_netmask{params.netmask}, m_packet{ZuMv(params.packet)},
  m_ingress{new Allocator{mx->rxBufSize() ? mx->rxBufSize() : ZiIOBuf_DefltCount,
    m_mtu + 14}}, m_rxAllocator{mx, m_ingress},
  m_packets{new Allocator{requirePackets(params.packetCount), m_mtu + 14, 14 + 40 + 60}},
  m_control{new Allocator{DefaultControlBufCount, m_mtu + 14, 14 + 40 + 60}},
  m_packetAllocator{mx, m_packets}, m_dataAllocator{mx, m_packets},
  m_controlAllocator{mx, m_control}
{
  ZmAssert_(mx->state() == ZmEngineState::Stopped);
  // TCP controls use separate native backing and never borrow application slots.
  m_ingress->rxFrames = new RxFrames{m_ingress->pool.count, m_ingress->pool.count};
  // A standalone packet shell has no connection-retention policy. Hub applies
  // its explicit transport limits before publishing any connection owner.
  m_config.rxRetainFrames = m_config.rxRetainFramesPerCxn = m_ingress->pool.count;
  m_config.rxReserveFrames = 0;
  m_ingress->returned = [this] { returned(); };
  addProvider(m_packets);
  addProvider(m_control);
}

Stack::~Stack()
{
  ZmAssert_(m_state == Stopped && !m_current && !unsigned(m_wakeUsers));
  ZmAssert_(!m_txPending && !m_ingress->pool.used() && !m_packets->pool.used());
  ZmAssert_(!m_control->pool.used());
  ZmAssert_(!m_routePending && !m_endpoints->count_());
  ZmAssert_(!m_observed && !m_observedCount);
  delete m_cxns;
  m_ingress->rxFrames->reconcile();
  m_ingress->returned = {};
  m_backend = nullptr;
  while (m_providers) {
    auto p = m_providers;
    m_providers = p->next;
    p->next = nullptr;
    ZmDEREF(p); // stopped, long-lived configuration ownership
  }
}

void Stack::addProvider(Allocator *p)
{
  ZmREF(p);
  m_txCapacity += p->pool.count;
  p->next = m_providers;
  p->pool.next = m_providers ? &m_providers->pool : nullptr;
  m_providers = p;
}

Zi::IOBufPlugin *Stack::allocator(
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
  auto p = new Allocator{count, unsigned(bytes), head};
  if (p->pool.size < align) p->pool.size = align; // physical alignment padding only
  addProvider(p);
  return p;
}

bool Stack::configureControl(unsigned count, unsigned credits, ZtString<> &error)
{
  ZmAssert_(mx()->state() == ZmEngineState::Stopped && m_state == Stopped);
  error = {};
  uint64_t capacity = m_txCapacity - m_control->pool.count + count;
  if (!count || !credits || credits >= capacity || capacity > UINT_MAX ||
      m_txPending || m_control->pool.used()) {
    error = ZtString<>{} << "invalid or busy control reserve count=" << count
      << " credits=" << credits << " Tx capacity=" << capacity
      << " held control=" << m_control->pool.used();
    return false;
  }
  m_control->pool.count = count;
  m_controlCredits = credits;
  m_txCapacity = capacity;
  return true;
}

TxAdmission Stack::txAdmission() const
{
  ZmAssert_(m_state == Stopped || mx()->invoked(mx()->txThread()));
  unsigned count = m_control->pool.count, used = m_control->pool.used();
  return {.capacity = m_txCapacity, .dataStalls = m_dataStalls,
    .controlStalls = m_controlStalls, .reserved = m_controlCredits,
    .outstanding = m_txPending, .controlCount = count,
    .controlFree = used < count ? count - used : 0};
}

bool Stack::configure(const StackConfig &config, ZtString<> &error)
{
  ZmAssert_(mx()->state() == ZmEngineState::Stopped && m_state == Stopped);
  error = {};
  if (!positiveTimeout(config.connectTimeout)) {
    error = "invalid connect timeout";
    return false;
  }
  if (!positiveTimeout(config.closeDrainTimeout) || !positiveTimeout(config.finWaitTimeout)) {
    error = "invalid close timeout";
    return false;
  }
  if (!positiveTimeout(config.userTimeout) && config.userTimeout != ZuTime{0}) {
    error = "invalid user timeout";
    return false;
  }
  if (!config.maxCxns || !config.maxPendingCxns || config.maxPendingCxns > config.maxCxns) {
    error = "invalid connection admission limits";
    return false;
  }
  if (!config.tfoCookieCacheSize) {
    error = "invalid Fast Open cookie cache size";
    return false;
  }
  if (!config.closedTupleRingSize) {
    error = "invalid closed tuple ring size";
    return false;
  }
  unsigned count = m_ingress->pool.count;
  if (!config.rxRetainFrames || !config.rxRetainFramesPerCxn ||
      !config.rxReserveFrames || count <= config.rxReserveFrames) {
    error = ZtString<>{} << "invalid Rx retention=" << config.rxRetainFrames
      << " per connection=" << config.rxRetainFramesPerCxn
      << " physical frames=" << count << " reserve=" << config.rxReserveFrames;
    return false;
  }
  if (!configureControl(config.controlBufCount, config.txControlCredits, error))
    return false;
  unsigned limit = count - config.rxReserveFrames;
  if (limit > config.rxRetainFrames) limit = config.rxRetainFrames;
  m_ingress->rxFrames->limit(limit); // no revocation if old charges exceed new cap
  m_closed.resize(config.closedTupleRingSize);
  m_tfoCookies.resize(config.tfoCookieCacheSize);
  m_config = config;
  return true;
}

CxnAdmission Stack::cxnAdmission() const
{
  ZmAssert_(m_state == Stopped || mx()->invoked(mx()->rxThread()));
  return m_cxnSlots.admission(m_config.maxCxns, m_config.maxPendingCxns);
}

CxnSlot Stack::reserveCxn()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) return {};
  return m_cxnSlots.reserve(m_config.maxCxns, m_config.maxPendingCxns);
}
void Stack::establishedCxn(CxnSlot &slot)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  ZmAssert_(!slot || slot.owner() == &m_cxnSlots);
  slot.established();
}
void Stack::closingCxn(CxnSlot &slot)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  ZmAssert_(!slot || slot.owner() == &m_cxnSlots);
  slot.closing();
}
void Stack::releaseCxn(CxnSlot &slot)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  ZmAssert_(!slot || slot.owner() == &m_cxnSlots);
  slot.release();
}

bool Stack::chooseISN(const ZiSockAddr &local, const ZiSockAddr &remote, uint32_t &isn) const
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_isn.generate(local, remote, isn);
}

bool Stack::tfoCookie(const ZiIP &client, const ZiSockAddr &server, TFOCookie &cookie) const
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_tfoCookies.find({client, server}, cookie);
}

void Stack::saveTFOCookie(
  const ZiIP &client, const ZiSockAddr &server, const TFOCookie &cookie)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (cookie.length) m_tfoCookies.store({client, server}, cookie.span());
  else m_tfoCookies.erase({client, server});
}

bool Stack::issueTFO(const ZiSockAddr &local, const ZiIP &client, TFOCookie &cookie) const
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_tfoKey.issue({client, local}, cookie);
}

bool Stack::validTFO(const ZiSockAddr &local, const ZiIP &client, const TFOCookie &cookie) const
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_tfoKey.validate({client, local}, cookie.span());
}

bool Stack::tcpTimestamp(ZuTime now, uint32_t &out)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  return m_tcpClock.sample(now, out);
}

ClosedTuple Stack::rememberClosed(ClosedTuple record)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_closed.remember(ZuMv(record));
}

bool Stack::closedTuple(const ClosedTupleKey &key, ClosedTuple &record) const
{
  ZmAssert_(m_state == Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.find(key, record);
}

bool Stack::updateClosed(const ClosedTuple &record)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  return m_closed.update(record);
}

ClosedTupleStats Stack::closedTupleStats() const
{
  ZmAssert_(m_state == Stopped || mx()->invoked(mx()->rxThread()));
  return m_closed.stats();
}

unsigned Stack::rxCxnLimit() const
{
  unsigned limit = m_ingress->rxFrames->stats().limit;
  return limit < m_config.rxRetainFramesPerCxn ? limit : m_config.rxRetainFramesPerCxn;
}

bool Stack::ready() const { return m_state == Running; }

CxnBindError::T bindCxnTuple(
  ZiSockAddr &local, const ZiSockAddr &remote, unsigned &nextPort,
  EndpointBindings &bindings, const ClosedTuples &history, void *owner, EndpointFn receive)
{
  if (!owner || !receive ||
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
  bindings.add(EndpointBinding{key, owner, receive});
  local = key;
  return CxnBindError::None;
}

bool Stack::bind(
  ZiSockAddr &local, void *owner, EndpointFn receive,
  const ZiSockAddr &remote, ZtString<> *error)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) {
    if (error) *error = "packet service unavailable during endpoint binding";
    return false;
  }
  auto result = bindCxnTuple(local, remote, m_nextPort, *m_endpoints, m_closed, owner, receive);
  if (error) {
    switch (result) {
      case CxnBindError::None: *error = {}; break;
      case CxnBindError::Invalid: *error = "invalid endpoint binding"; break;
      case CxnBindError::InUse: *error = "local endpoint is already bound"; break;
      case CxnBindError::Retained: *error = "client tuple remains in closed history"; break;
      case CxnBindError::Exhausted:
	*error = "client port range 49152..65535 exhausted by live endpoints or closed tuples";
	break;
      default: break;
    }
  }
  return result == CxnBindError::None;
}

bool Stack::unbind(const ZiSockAddr &local, void *owner)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  auto binding = m_endpoints->findPtr(local);
  if (!binding || binding->owner != owner) return false;
  m_endpoints->delNode(binding);
  return true;
}

bool Stack::resolvePath(IPPath path, RouteFn done)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready() || !done) return false;
  unsigned worker = 0;
  for (unsigned i = 0; i < mx()->nWorkers(); ++i) {
    auto id = mx()->workerID(i);
    if (id != mx()->rxThread() && id != mx()->txThread()) { worker = id; break; }
  }
  if (!worker) return false;
  ++m_routePending;
  mx()->run([this, path = ZuMv(path), done = ZuMv(done)]() mutable {
    ZmAssert_(!mx()->invoked(mx()->rxThread()) && !mx()->invoked(mx()->txThread()));
    ZtString<> error;
    resolveIPPath(m_interface, path, error);
    mx()->rxRun([this, path = ZuMv(path), error = ZuMv(error), done = ZuMv(done)]() mutable {
      ZmAssert_(m_routePending);
      --m_routePending;
      if (!ready()) error = "packet service stopped during route setup";
      done(ZuMv(path), ZuMv(error));
      if (m_state == Stopping && !m_endpointDrain && !m_routePending) {
	ZmAssert_(!cxnAdmission().total);
	mx()->txRun([this] { stopTx(); });
      }
    });
  }, worker);
  return true;
}

void Stack::start()
{
  mx()->rxRun([this] { start_(); });
}

void Stack::start_()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (ready()) { mx()->pluginStarted({}); return; }
  ZmAssert_(m_state == Stopped);
  m_state = Starting;
  m_returnQueued = 0;
  m_ingress->rxFrames->reconcile();
  m_error = {};
  bool busy = m_ingress->pool.used();
  for (auto p = m_providers; p; p = p->next) busy |= p->pool.used();
  if (busy) {
    m_state = Stopped;
    mx()->pluginStarted(ZeEXCEPT(Error, "Zutcp", "retained packet pools are busy"));
    return;
  }
  m_backend = Zxdp::Instance::create({.interface = m_interface, .netmask = m_netmask,
    .txPools = &m_providers->pool, .rxPool = &m_ingress->pool,
    .rx = [this](uint8_t *p, unsigned n) { received(p, n); },
    .spin = m_spin}, m_error);
  if (m_backend && m_backend->ready() && m_backend->mtu() == m_mtu) {
    m_current = m_backend.ptr();
    m_state = Running;
    mx()->pluginStarted({});
    return;
  }
  if (!m_error) m_error = "interface geometry changed before packet startup";
  m_startFailure = true;
  m_state = Stopping;
  mx()->txRun([this] { stopTx(); });
}

void Stack::stop()
{
  mx()->rxRun([this] { stopRx(); });
}

void Stack::stopRx()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (m_state == Stopped) { mx()->pluginStopped({}); return; }
  m_state = Stopping;
  if (m_cxns) {
    m_endpointDrain = true;
    m_cxns->stop([this] { m_endpointDrain = false; stopEndpoints(); });
  } else stopEndpoints();
}

void Stack::stopEndpoints()
{
  // Existing wake calls finish before Tx closes the backend's wait descriptor.
  // The closed instance itself stays retained until the stopped lifecycle ends.
  m_current = nullptr;
  __atomic_thread_fence(__ATOMIC_SEQ_CST); // publish the gate before observing users
  if (!m_routePending) {
    ZmAssert_(!cxnAdmission().total);
    mx()->txRun([this] { stopTx(); });
  }
}

void Stack::stopTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  mx()->del(&m_txTimer);
  ZtString<> error;
  if (!unsigned(m_wakeUsers) && (!m_backend || m_backend->close(error))) {
    m_txPending = 0; // successful close has retired and recycled every submission
    m_state = Stopped;
    if (m_startFailure) {
      m_startFailure = false;
      mx()->pluginStarted(ZeEXCEPT(Error, "Zutcp", ([text = m_error](auto &s) { s << text; })));
    } else if (!!m_error) {
      mx()->pluginStopped(ZeEXCEPT(Error, "Zutcp", ([text = m_error](auto &s) { s << text; })));
    } else mx()->pluginStopped({});
    return;
  }
  if (!m_error && !!error) m_error = ZuMv(error);
  // Mx control waits for plugin completion during start/stop, so its timer
  // loop cannot drive these retries. Yield each bounded retirement step back
  // to the Tx queue instead of waiting or spinning inside one callback.
  mx()->push([this] { stopTx(); }, mx()->txThread());
}

void Stack::wake()
{
  ++m_wakeUsers;
  Zxdp::Instance *p = m_current;
  if (p) p->wake();
  --m_wakeUsers;
}

void Stack::rx()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) return;
  m_ingress->rxFrames->reconcile();
  m_rxPackets = 0;
  m_backend->rx();
  m_ingress->rxFrames->reconcile();
}

void Stack::returned()
{
  if (!ready()) return; // stopped returns drain on next start or destruction
  if (mx()->invoked(mx()->rxThread())) {
    m_ingress->rxFrames->reconcile();
    return;
  }
  if (m_returnQueued.xch(1)) return;
  // Existing mx wakeFn queues receive re-entry and interrupts the plugin wait.
  // Coalesce a burst of off-owner returns into one ordinary scheduler task.
  mx()->rxRun([this] {
    m_returnQueued = 0;
    m_ingress->rxFrames->reconcile();
  });
}

bool Stack::retainRx(Zi::IOBuf *buf, RxAccount *account)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!buf->registered() || buf->allocator() != &m_rxAllocator) return true;
  return m_ingress->rxFrames->retain(m_ingress->pool.slot(buf->data_()), account);
}

RxFrameStats Stack::rxAdmission()
{
  ZmAssert_(mx()->invoked(mx()->rxThread()) ||
    (m_state == Stopped && mx()->state() == ZmEngineState::Stopped));
  m_ingress->rxFrames->reconcile();
  auto stats = m_ingress->rxFrames->stats();
  stats.configured = m_config.rxRetainFrames;
  stats.configuredPerCxn = m_config.rxRetainFramesPerCxn;
  stats.perCxnLimit = rxCxnLimit();
  stats.reserve = m_config.rxReserveFrames;
  return stats;
}

void Stack::received(uint8_t *p, unsigned n)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  m_ingress->rxFrames->reconcile();
  IPView ip;
  TCPView tcp;
  EndpointBindings::Node *binding = nullptr;
  bool valid = readIP({p, n}, ip) && readTCP(ip, tcp);
  if (valid) {
    binding = m_endpoints->findPtr(ZiSockAddr{ip.local, tcp.header.dstPort});
    if (!binding) {
      ZiIP any;
      if (ip.local.v6()) any = in6addr_any;
      else any = in_addr{};
      binding = m_endpoints->findPtr(ZiSockAddr{any, tcp.header.dstPort});
    }
  }
  // A current owner qualifies input before consulting retired tuple history.
  // In particular, newer-TS reuse can choose a peer ISN below an old frontier.
  bool closed = valid && !binding && closedPacket(p, ip, tcp);
  if (closed) m_ingress->free(p);
  else if (binding || m_packet) {
    ZmRef<Zi::IOBuf> buf = new Zi::IOBufStub<Zi::IOBuf, ZiIOBuf_HeapID>{
      p, n, &m_rxAllocator, this, n, false, true};
    if (binding) {
      buf->skip = tcp.payload.data() - p;
      buf->length = tcp.payload.length();
      auto receive = binding->receive;
      auto owner = binding->owner;
      receive(owner, ZuMv(buf), ZiSockAddr{ip.local, tcp.header.dstPort},
	ZiSockAddr{ip.remote, tcp.header.srcPort}, ip.ecn, tcp.header);
    } else m_packet(ZuMv(buf));
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

bool Stack::recoverClosed(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14) return false;
  IPView ip{.local = d.local.ip(), .remote = d.addr.ip()};
  TCPView tcp{.header = d.tcp, .payload = d.buf->cspan()};
  return closedPacket(d.buf->data_(), ip, tcp);
}

void Stack::resetUnmatched(const Datagram &d)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!d.buf || d.buf->skip < 14 || (d.tcp.flags & 0x04)) return;
  IPPath path{.local = d.local.ip(), .remote = d.addr.ip(), .mtu = m_mtu};
  memcpy(path.localMAC, d.buf->data_(), 6);
  memcpy(path.remoteMAC, d.buf->data_() + 6, 6);
  resetReply(d.tcp, d.buf->length, path);
}

void Stack::resetReply(const TCPHeader &incoming, unsigned payload, IPPath path)
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

void Stack::replyControl(TCPHeader tcp, IPPath path)
{
  auto packet = allocPacket(this, path.local.type(), true);
  if (!packet) return;
  unsigned wire = writeTCP(packet->data_(), packet->size, path, tcp, 0);
  if (!wire) return;
  packet->skip = 0;
  packet->length = wire;
  sendPacket(packet);
}

bool Stack::closedPacket(const uint8_t *frame, const IPView &ip, const TCPView &tcp)
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

void Stack::replyClosed(ClosedTuple record, IPPath path)
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

ZmRef<PacketBuf> Stack::allocPacket(void *owner, ZiIPType::T family, bool control)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  auto head = tcpHead(family);
  if (!ready() || !head) return nullptr;
  ZmRef<PacketBuf> buf = control ? m_controlAllocator.allocBuf(owner) :
    m_packetAllocator.allocBuf(owner);
  if (control && !buf) ++m_controlStalls;
  if (buf) buf->skip = head;
  return buf;
}

bool Stack::sendPacket(const ZmRef<Zi::IOBuf> &buf, PacketLease *lease)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready() || !buf || !buf->registered() || !buf->length ||
      buf->skip > buf->size || buf->length > buf->size - buf->skip) return false;
  // Keep bounded CQ service abreast of continuous producers. A queued poll
  // alone can fall behind the work that needs its returned physical slots.
  enum { batch = 64 };
  if (m_txPending && !(m_txPending % batch)) reconcileTx();
  bool control = buf->allocator_ == &m_controlAllocator;
  uint64_t limit = control ? m_txCapacity : m_txCapacity - m_controlCredits;
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
    (allocator == &m_packetAllocator || allocator == &m_dataAllocator ?
      m_packets.ptr() : nullptr);
  if (!lease && provider) lease = static_cast<PacketBuf *>(buf.ptr())->retained;
  if (lease) lease->adopt(buf->allocator(), backing, size);
  else allocator->free(backing, size, true);
  if (!m_txPending++) mx()->txRun([this] { pollTx(); });
  return true;
}

void Stack::reconcileTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready()) return;
  auto n = m_backend->pollTx();
  ZmAssert_(n <= m_txPending);
  m_txPending -= n;
}

void Stack::pollTx()
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!ready()) return;
  unsigned before = m_txPending;
  reconcileTx();
  unsigned n = before - m_txPending;
  if (!m_txPending) return;
  if (n) { mx()->push([this] { pollTx(); }, mx()->txThread()); return; }
  // CQ retry only; application transmission never waits for a batching timer.
  mx()->add(&m_txTimer, Zm::now() + ZuTime{0, 1000000},
    ZmScheduler::Update,
    [this](auto &&arm) { return arm([this] { pollTx(); }); }, mx()->txThread());
}

void Stack::listen(
  ZiListenFn listening, ZiFailFn fail, ZiConnectFn accept, ZiIP local, uint16_t port,
  unsigned count, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready()) { if (fail) fail(false); return; }
  if (!m_cxns) m_cxns = new Connections{this};
  m_cxns->listen(ZuMv(listening), ZuMv(fail), ZuMv(accept), local, port, count,
    allocator, ZuMv(options));
}

void Stack::stopListening(ZiIP local, uint16_t port)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (m_cxns) m_cxns->stopListening(local, port);
}

void Stack::connect(
  ZiConnectFn connected, ZiFailFn fail, ZiIPType::T family, ZiIP local, uint16_t localPort,
  ZiIP remote, uint16_t remotePort, Zi::IOBufAllocator *allocator, ZiCxnOptions options)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!ready() || remote.type() != family || !remote || !remotePort ||
      (local && local.type() != family)) { if (fail) fail(false); return; }
  if (!m_cxns) m_cxns = new Connections{this};
  m_cxns->connect(ZuMv(connected), ZuMv(fail), local, localPort, remote, remotePort,
    allocator, ZuMv(options));
}

void Stack::udp(ZiConnectFn, ZiFailFn fail, ZiIPType::T, ZiIP, uint16_t, ZiIP,
  uint16_t, Zi::IOBufAllocator *, ZiCxnOptions) { if (fail) fail(false); }

void Stack::recv(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->rxThread()));
  if (!cxn->plugin()) { cxn->rxContext().complete(); return; }
  Connections::visit(cxn, [](auto native) {
    if (!native->disconnecting()) native->recvMx();
  });
}
void Stack::send(ZiConnection *cxn)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (!cxn->plugin()) { cxn->txContext().complete(); return; }
  Connections::visit(cxn, [](auto native) { native->sendMx(); });
}
void Stack::disconnect(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  if (peer) { close(cxn, true); return; }
  cxn->txUp() = 0;
  cxn->txContext().complete();
  Connections::visit(cxn, [this](auto native) {
    using C = ZuDecay<decltype(*native)>;
    mx()->rxRun([native = ZmRef<C>{native}, gen = native->dataGen()] {
      if (gen == native->dataGen() && !native->disconnecting()) native->closeMxRx();
    });
    native->shutdownMx();
  });
}

void Stack::close(ZiConnection *cxn, bool peer)
{
  ZmAssert_(mx()->invoked(mx()->txThread()));
  cxn->txUp() = 0;
  cxn->txContext().complete();
  Connections::visit(cxn, [this, peer](auto native) {
    if (!peer) native->abortTCPMx();
    using C = ZuDecay<decltype(*native)>;
    mx()->rxRun([native = ZmRef<C>{native}, peer] {
      if constexpr (ZuIsSame<C, Connections::Cli>{}) native->closeMx(peer);
      else native->State::disconnect(peer);
    });
  });
}
void Stack::telemetry(Ztc::MxTelemetry &data) const { mx()->telemetry(data); }
void Stack::observed(Zutcp_::EndpointLink *cxn)
{
  {
    ZmGuard guard(m_observeLock);
    cxn->observedNext = m_observed;
    cxn->observedPrev = &m_observed;
    if (m_observed) m_observed->observedPrev = &cxn->observedNext;
    m_observed = cxn;
    ++m_observedCount;
  }
  ZmReadGuard guard(m_watchLock);
  if (m_addCxn) m_addCxn(cxn->mxCxn);
}

void Stack::unobserved(Zutcp_::EndpointLink *cxn)
{
  auto mx = cxn->mxCxn;
  {
    ZmGuard guard(m_observeLock);
    if (cxn->observedNext) cxn->observedNext->observedPrev = cxn->observedPrev;
    *cxn->observedPrev = cxn->observedNext;
    cxn->observedNext = nullptr;
    cxn->observedPrev = nullptr;
    cxn->mxCxn = nullptr;
    --m_observedCount;
  }
  ZmReadGuard guard(m_watchLock);
  if (m_delCxn) m_delCxn(mx);
}

unsigned Stack::allCxns(AllCxnsFn fn) const
{
  ZtArray<ZmRef<ZiConnection>, ZtArrayHeapID<"Zutcp.Mx.CxnRefs">> refs;
  {
    ZmReadGuard guard(m_observeLock);
    refs.size(m_observedCount);
    for (auto cxn = m_observed; cxn; cxn = cxn->observedNext)
      new (refs.push()) ZmRef<ZiConnection>{cxn->mxCxn};
  }
  for (auto &cxn : refs) fn(cxn.ptr());
  return refs.length();
}

unsigned Stack::allQueues(Ztc::QueueMgr::AllFn fn) const
{
  struct Snapshot final : public Ztc::Queue {
    Ztc::QueueTelemetry data;
    TelKey telKey() const override { return {data.ownerID, data.id, data.type}; }
    void telemetry(Ztc::QueueTelemetry &out) const override { out = data; }
  };
  ZtArray<Snapshot, ZtArrayHeapID<"Zutcp.Mx.QueueSnapshots">> snapshots;
  {
    ZmReadGuard guard(m_observeLock);
    snapshots.size(m_observedCount<<1);
    for (auto cxn = m_observed; cxn; cxn = cxn->observedNext)
      dynamic_cast<Ztc::Link *>(cxn)->allQueues([&snapshots](Ztc::Queue *queue) {
        auto snapshot = new (snapshots.push()) Snapshot;
        queue->telemetry(snapshot->data);
      });
  }
  for (auto &snapshot : snapshots) fn(&snapshot);
  return snapshots.length();
}

void Stack::watch(AddCxnFn add, DelCxnFn del)
{
  ZmGuard guard(m_watchLock);
  ZmAssert_(!m_addCxn && !m_delCxn);
  m_addCxn = ZuMv(add);
  m_delCxn = ZuMv(del);
}

void Stack::unwatch()
{
  ZmGuard guard(m_watchLock);
  m_addCxn = {};
  m_delCxn = {};
}

} // namespace Zutcp
