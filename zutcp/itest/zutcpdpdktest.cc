//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <rte_mempool.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/Ztcp.hh>
#include <zlib/ZxdpDpdkPool.hh>

#include "zutcpdpdkfixture.hh"

using namespace ZuTestUtil;
using namespace ZutcpDpdkTest;

#define CHECK(condition, ...) ZuCheckRT((condition), log_(__VA_ARGS__))

enum { TCPPort = 18210, UDPPortA = 18211, UDPPortB = 18212 };

static constexpr auto TCPData = "DPDK carries the exact Ztcp byte stream"_z;
static const ZuBSpan UDPData[] = {"first datagram", "", "a longer final datagram"};

template <unsigned Kind> struct Input : ZiIOBuf { using ZiIOBuf::ZiIOBuf; };
using TCPInput = Input<0>;
using ReconnectInput = Input<1>;
using UDPInput = Input<2>;
struct StreamInput : Ztcp_::RxQueue::Node { using Ztcp_::RxQueue::Node::Node; };

static bool checked(Result result, const char *description)
{
  bool ok = result.is<void>();
  CHECK(ok, description, " [", ZxdpDpdkTest::errorText(result.p<ZeException>()), "]");
  return ok;
}

static bool wait(ZmSemaphore &event) { return !event.timedwait(Zm::now(6)); }

struct Peer {
  ZmRef<Zxdp::Instance> device;
  ZmRef<Zutcp::Instance> tcp;
  ZiMultiplex mx{ZiMxParams{}.rxBufSize(128).rxQuantum(16).scheduler([](auto &s) {
    s.nThreads(5)
      .thread(3, [](auto &t) { t.name("appRx"); t.isolated(true); })
      .thread(4, [](auto &t) { t.name("appTx"); t.isolated(true); });
  })};
  Zi::IOBufAllocator *rx = nullptr, *reconnectRx = nullptr, *udpRx = nullptr;
  Zi::IOBufAllocator *tx = nullptr, *smallTx = nullptr, *udpTx = nullptr, *app = nullptr;

  bool init(Fixture &fixture, unsigned index)
  {
    device = Zxdp::instance(Zxdp::Type::Dpdk);
    auto config = deviceParams(fixture.ports[index].name(), fixture.ifindex[index]);
    auto &params = config.p<Zxdp::Dpdk::Params>();
    params.ifindex = fixture.ifindex[index ^ 1];
    auto wrong = device->init(&mx, config);
    CHECK(wrong.is<ZeException>() && !device->mx() && !device->mtu() &&
	strstr(ZxdpDpdkTest::errorText(wrong.p<ZeException>()).ndata(), "MTU or MAC"),
      "mismatched dummy MAC rolls back synchronous port acquisition [", index, "]");
    params.ifindex = fixture.ifindex[index];
    if (!checked(device->init(&mx, config), "reacquire the same port with the matching Linux link"))
      return false;
    const auto &link = Links[index];
    CHECK((device->mx() == &mx && device->ifindex() == fixture.ifindex[index] &&
	device->mtu() == 1500 && device->rxQueues() == 1 &&
	device->mac() == ZuBSpan{link.address.addr_bytes, 6} &&
	!device->allocated() && !device->everStarted()),
      "discover Linux identity and native Ethernet geometry before either handoff [", index, "]");
    CHECK(device->init(&mx, config).is<void>(), "identical stopped device init is idempotent");
    ++params.rxInterval;
    CHECK(device->init(&mx, config).is<ZeException>() && device->mx() == &mx,
      "changed idle interval preserves the originally opened configuration");
    --params.rxInterval;
    auto other = Zxdp::instance(Zxdp::Type::Dpdk);
    CHECK(other->init(&mx, config).is<ZeException>() && !other->mx(),
      "second instance cannot acquire the opened port");
    auto moving = device;
    tcp = new Zutcp::Instance;
    if (!checked(tcp->init(&mx, ZuMv(moving), tcpParams()), "move the initialized device into Zutcp"))
      return false;
    CHECK(!moving && tcp->init(&mx, ZuMv(moving), tcpParams()).is<void>(),
      "moved-from repeated TCP init identifies the owned device");
    if (!index) {
      auto alt = deviceParams(fixture.ports[1].name(), fixture.ifindex[1]);
      if (!checked(other->init(&mx, alt), "open another eligible device in the same mx")) return false;
      CHECK(tcp->init(&mx, ZuMv(other), tcpParams()).is<ZeException>() && other &&
	  other->mx() == &mx,
	"conflicting TCP init leaves a different eligible device with its caller");
      other->final();
    }
    auto plugin = tcp;
    mx.plugin(ZuMv(plugin));
    auto repeated = tcp;
    mx.plugin(ZuMv(repeated));
    CHECK(mx.plugin() == tcp.ptr() && repeated == tcp,
      "same initialized plugin registration is idempotent before allocating");

    if (index) rx = mx.allocator<Zi::IOBufType::Rx, StreamInput>(128);
    else rx = mx.allocator<Zi::IOBufType::Rx, TCPInput>(128);
    reconnectRx = mx.allocator<Zi::IOBufType::Rx, ReconnectInput, 272>(32);
    udpRx = mx.allocator<Zi::IOBufType::Rx, UDPInput>(64);
    tx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 65536>(16);
    smallTx = mx.allocator<Zi::IOBufType::TxTCP, ZiIOBuf, 192, 256>(1);
    udpTx = mx.allocator<Zi::IOBufType::TxUDP>(16);
    app = mx.allocator<Zi::IOBufType::App>();
    if (!rx || !reconnectRx || !udpRx || !tx || !smallTx || !udpTx || !app) {
      CHECK(false, "register every typed handle before the first allocation attempt");
      return false;
    }
    auto stub = rx->allocBuf();
    auto udpStub = udpRx->allocBuf();
    CHECK(rx != reconnectRx && rx != udpRx && rx->pool == reconnectRx->pool &&
	udpRx->pool == rx->pool && rx->pool->type == Zi::IOBufType::Rx &&
	stub && stub->registered() && !stub->data_() && udpStub && !udpStub->data_() &&
	dynamic_cast<UDPInput *>(udpStub.ptr()) && !tx->allocBuf() && !smallTx->allocBuf() &&
	!udpTx->allocBuf(),
      "typed Rx stubs share backingless ingress and Tx remains deferred");
    CHECK(tx->pool != smallTx->pool && tx->pool != udpTx->pool &&
	tx->pool->type == Zi::IOBufType::TxTCP && smallTx->pool->type == Zi::IOBufType::TxTCP &&
	udpTx->pool->type == Zi::IOBufType::TxUDP && tx->size == device->mtu() + 14 &&
	smallTx->size == 192 + 114 && smallTx->headRoom == 114 && smallTx->pool->align == 256,
      "registration retains distinct Tx pools, caps large Tx at MTU and preserves small geometry");
    return true;
  }
};

struct Peers {
  Fixture &fixture;
  Peer a, b;

  bool stop() {
    fixture.wire.quiesce();
    bool astop = a.mx.stop(), bstop = b.mx.stop();
    fixture.drain();
    return astop && bstop && !a.device->error() && !b.device->error();
  }
  ~Peers() { if (a.mx.running() || b.mx.running()) stop(); }
};

struct Operation;
class Connection : public ZmNHeap<Connection>, public ZiConnection {
friend inline ZuStringT<"Zutcp.DpdkTest.Connection"> ZmNHeapID(Connection *);
public:
  using ZmNHeap<Connection>::operator new;
  using ZmNHeap<Connection>::operator delete;
  Connection(ZiMultiplex *mx, const ZiCxnInfo &info, Operation *operation) :
    ZiConnection{mx, info}, m_operation{operation} { }
  void connected(ZiRxContext &) override;
  void disconnected(bool) override;
  void write(ZuBSpan, ZiSockAddr peer = {});

private:
  bool received(ZiRxContext &);
  Operation *m_operation;
  ZiIOBuf *m_stub = nullptr;
  unsigned m_bytes = 0, m_datagrams = 0;
};

struct Operation {
  Zi::IOBufAllocator *rx, *tx;
  ZiSockAddr remote;
  bool udp, reconnect = false;
  ZmRef<Connection> cxn;
  ZmRef<ZiIOBuf> retained;
  ZmAtomic<unsigned> errors{0};
  ZmSemaphore opened, closed, input, output;

  ZiConnectFn accept(ZiMultiplex *mx) {
    return {[this, mx](const ZiCxnInfo &info) -> ZiConnection * {
      cxn = new Connection{mx, info, this};
      return cxn;
    }};
  }
  ZiFailFn failed() {
    return {[this](bool) { ++errors; opened.post(); }};
  }
  bool typed(ZiIOBuf *buf) const {
    if (udp) return dynamic_cast<UDPInput *>(buf);
    if (reconnect) return dynamic_cast<ReconnectInput *>(buf);
    return dynamic_cast<TCPInput *>(buf);
  }
};

void Connection::connected(ZiRxContext &io)
{
  auto state = m_operation;
  auto buf = info().rxAllocator->allocBuf(this);
  if (!up() || !plugin() || !Zi::nullSocket(info().socket) || info().rxAllocator != state->rx ||
      (info().type == ZiCxnType::UDP) != state->udp || !buf || !state->typed(buf.ptr()) ||
      buf->data_()) ++state->errors;
  if (buf) {
    m_stub = buf.ptr();
    io.init(ZiRxFn{this, ZmFnPtr<&Connection::received>{}}, ZuMv(buf));
  }
  state->opened.post();
}

void Connection::disconnected(bool)
{
  auto state = m_operation;
  if (up() || plugin() || !rxContext().completed() || !txContext().completed()) ++state->errors;
  state->closed.post();
}

bool Connection::received(ZiRxContext &io)
{
  auto state = m_operation;
  auto buf = io.buf.ptr();
  if (!state->udp && buf && !buf->length) return true; // TCP EOF is not a message
  if (!buf || buf != m_stub || !state->typed(buf) || buf->allocator() != state->rx ||
      !buf->registered() || buf->owner != this) {
    ++state->errors;
  } else if (state->udp) {
    auto i = m_datagrams++;
    if (i >= sizeof(UDPData)/sizeof(UDPData[0]) || buf->cspan() != UDPData[i] ||
	io.addr != state->remote || !io.tos.is<uint8_t>() || io.tos.p<uint8_t>() != 0x28)
      ++state->errors;
  } else if (buf->length > TCPData.length() - m_bytes ||
      memcmp(buf->data(), TCPData.data() + m_bytes, buf->length)) {
    ++state->errors;
  } else {
    m_bytes += buf->length;
  }
  if (buf && !state->retained) state->retained = io.buf;
  io.buf = state->rx->allocBuf(this);
  m_stub = io.buf.ptr();
  if (state->udp || m_bytes == TCPData.length() || state->errors) state->input.post();
  return true;
}

void Connection::write(ZuBSpan data, ZiSockAddr peer)
{
  send(ZiTxFn{[state = m_operation, data, peer, this](ZiTxContext &io) {
    auto buf = state->tx->allocBuf(this);
    if (!buf || !buf->append(data)) {
      ++state->errors;
      io.complete();
      state->output.post();
      return false;
    }
    io.init(ZiTxFn{[state, data](ZiTxContext &io) {
      if (io.length != int(data.length()) ||
	  (state->udp && (!io.buf || io.buf->cspan() != data))) ++state->errors;
      io.complete();
      state->output.post();
      return true;
    }}, ZuMv(buf), peer, ZiTOS{uint8_t(0x28)});
    return true;
  }});
}

static bool nativeBacking(ZiIOBuf *);

struct Server : Ztcp::Server<Server> {
  struct Link;
  Zi::IOBufAllocator *input;
  ZiIP local;
  ZmSemaphore ready, retired;
  ZmAtomic<unsigned> errors{0}, connections{0}, messages{0}, nativeRx{0};

  Server(Zi::IOBufAllocator *input_, ZiIP local_) : input{input_}, local{local_} { }
  ZiIP localIP() const { return local; }
  uint16_t localPort() const { return TCPPort; }
  unsigned nAccepts() const { return 4; }
  ZiConnection *accepted(const ZiCxnInfo &);
  void listening(const ZiListenInfo &) { ready.post(); }
  void listenFailed(bool) { ++errors; ready.post(); }
};

struct Server::Link : ZmNHeap<Link>, Ztcp::SrvLink<Server, Link> {
friend inline ZuStringT<"Zutcp.DpdkTest.ZtcpLink"> ZmNHeapID(Link *);
  using Base = Ztcp::SrvLink<Server, Link>;
  using ZmNHeap<Link>::operator new;
  using ZmNHeap<Link>::operator delete;
  Link(Server *server) : Base{server} { }
  void connected(Ztcp::Connected) { ++app()->connections; }
  void disconnected(bool) { app()->retired.post(); }
  int process(Ztcp::RxStream &rx) {
    if (rx.length() < TCPData.length()) return 0;
    unsigned offset = 0;
    rx.each(TCPData.length(), [server = app(), &offset](auto bytes) {
      if (memcmp(bytes.data(), TCPData.data() + offset, bytes.length())) ++server->errors;
      offset += bytes.length();
      return bytes.length();
    });
    rx.advance(TCPData.length());
    ++app()->messages;
    auto tx = txStream();
    tx << TCPData << Zi::flush();
    return 1;
  }
};

class StreamConnection : public Server::Link::Cxn {
  using Base = Server::Link::Cxn;

public:
  StreamConnection(Server *app, const ZiCxnInfo &info) :
    Base{new Server::Link{app}, info}, m_server{app} { }
  void connected(ZiRxContext &io) override {
    Base::connected(io);
    auto buf = io.buf;
    if (info().rxAllocator != m_server->input || !buf || buf->allocator() != m_server->input ||
	!dynamic_cast<StreamInput *>(buf.ptr()) || buf->data_() || !buf->registered() ||
	!plugin() || !Zi::nullSocket(info().socket)) ++m_server->errors;
    m_receive = ZuMv(io.fn);
    io.fn = ZiRxFn{this, ZmFnPtr<&StreamConnection::received>{}};
  }

private:
  bool received(ZiRxContext &io) {
    if (io.buf && io.buf->length) {
      if (io.buf->allocator() == m_server->input &&
	  dynamic_cast<StreamInput *>(io.buf.ptr()) && nativeBacking(io.buf.ptr()))
	++m_server->nativeRx;
      else
	++m_server->errors;
    }
    return m_receive.as<ZiRxFn>()(io);
  }

  Server	*m_server;
  ZmAnyFn	m_receive;
};

ZiConnection *Server::accepted(const ZiCxnInfo &info) { return new StreamConnection{this, info}; }

static bool ready(Operation &operation)
{
  return wait(operation.opened) && operation.cxn && !operation.errors;
}

static bool sendUDP(Operation &a, Operation &b, unsigned index)
{
  auto data = UDPData[index];
  a.cxn->write(data);
  if (!wait(a.output) || !wait(b.input) || a.errors || b.errors) return false;
  b.cxn->write(data, b.remote);
  return wait(b.output) && wait(a.input) && !a.errors && !b.errors;
}

static bool connectTCP(Peer &client, Operation &input, ZiIP local, ZiIP remote)
{
  client.mx.connect(input.accept(&client.mx), input.failed(), local, 0, remote, TCPPort, input.rx);
  return ready(input);
}

// Check the retained application's allocation against the receiver's actual
// native mempool. This diagnostic walks private objects only in the fixture.
static bool nativeBacking(ZiIOBuf *buf)
{
  if (!buf || !buf->registered() || !buf->data_()) return false;
  auto pool = ZxdpDpdkTest::native(buf->allocator()->pool);
  if (!pool || !rte_mempool_in_use_count(pool)) return false;
  struct Check {
    const void *packet;
    bool found = false;
  } check{buf->data_()};
  rte_mempool_obj_iter(pool, [](rte_mempool *, void *opaque, void *obj, unsigned) {
    auto check = static_cast<Check *>(opaque);
    auto slot = Zxdp::Dpdk::BufPool::slot(static_cast<rte_mbuf *>(obj));
    check->found |= slot->packet == check->packet;
  }, &check);
  return check.found;
}

static bool echoTCP(Operation &client)
{
  client.cxn->write(TCPData);
  return wait(client.output) && wait(client.input) && !client.errors && client.retained &&
    client.retained->allocator() == client.rx && client.typed(client.retained.ptr()) &&
    nativeBacking(client.retained.ptr());
}

static bool connectedUDP(Peer &a, Peer &b, Operation &one, Operation &two, ZiIP ipA, ZiIP ipB)
{
  auto options = ZiCxnOptions{}.udp(true);
  b.mx.udp(two.accept(&b.mx), two.failed(), ipB, UDPPortB, {}, 0, two.rx, options);
  a.mx.udp(one.accept(&a.mx), one.failed(), ipA, UDPPortA,
    one.remote.ip(), one.remote.port(), one.rx, options);
  return ready(one) && ready(two);
}

static bool allReturned(Peer &peer)
{
  auto pool = ZxdpDpdkTest::native(peer.rx->pool);
  return pool && !rte_mempool_in_use_count(pool);
}

static void traffic(Peers &pair, bool useTCP, bool useUDP, bool reconnect, bool v6)
{
  ZuTestScopeRT(traffic);
  auto &a = pair.a;
  auto &b = pair.b;
  ZiIP ipA{v6 ? Links[0].ip6 : Links[0].ip}, ipB{v6 ? Links[1].ip6 : Links[1].ip};
  Operation mainTCP{a.rx, a.tx, {ipB, TCPPort}, false};
  Operation again{a.reconnectRx, a.tx, {ipB, TCPPort}, false, true};
  Operation one{a.udpRx, a.udpTx, {ipB, UDPPortB}, true};
  Operation two{b.udpRx, b.udpTx, {ipA, UDPPortA}, true};
  Server server{b.rx, ipB};
  bool appInit = false;
  bool ok = checked(pair.fixture.startWire(a.rx->pool, b.rx->pool), "start independent ring wire");
  if (ok && useUDP) {
    ok = connectedUDP(a, b, one, two, ipA, ipB);
    ZuCheckRT(ok, log_("open connected and wildcard UDP with independent typed handles"));
  }
  if (ok && useTCP) {
    Ztcp::ServerParams params{&b.mx, "appRx", "appTx"};
    params.rxAllocator(b.rx).txAllocator(b.tx);
    appInit = server.init(ZuMv(params));
    ok = appInit && server.start();
    if (ok) {
      server.listen();
      ok = wait(server.ready) && !server.errors && connectTCP(a, mainTCP, ipA, ipB);
    }
    ZuCheckRT(ok, log_("connect Zi client to a real Ztcp listener over two DPDK ports"));
  }
  if (ok && useUDP) {
    ok = sendUDP(one, two, 0);
    ZuCheckRT(ok, log_("send connected datagram and addressed response with correct peers"));
  }
  if (ok && useTCP) {
    ok = echoTCP(mainTCP) && !server.errors &&
      server.connections == 1 && server.messages == 1 && server.nativeRx;
    ZuCheckRT(ok, log_("verify bidirectional Ztcp bytes, typed client delivery and accepted stub"));
    if (ok && reconnect) {
      mainTCP.retained = nullptr;
      mainTCP.cxn->disconnect();
      ok = wait(mainTCP.closed) && wait(server.retired) && !mainTCP.errors &&
	connectTCP(a, again, ipA, ipB) && echoTCP(again) &&
	!server.errors && server.connections == 2 && server.messages == 2 && server.nativeRx >= 2;
      ZuCheckRT(ok, log_("reconnect through the same listener with the separately typed client Rx"));
    }
  }
  if (ok && useUDP) {
    ok = sendUDP(one, two, 1) && sendUDP(one, two, 2);
    ZuCheckRT(ok && one.retained && two.retained &&
	one.retained->cspan() == UDPData[0] && two.retained->cspan() == UDPData[0] &&
	one.retained->allocator() == one.rx && two.retained->allocator() == two.rx &&
	nativeBacking(one.retained.ptr()) && nativeBacking(two.retained.ptr()),
      log_("preserve zero-length datagram, boundaries and retained ingress provenance"));
  }
  mainTCP.retained = again.retained = one.retained = two.retained = nullptr;
  if (appInit) {
    ZuCheckRT(server.stop(), log_("stop application while native I/O continues to drain"));
    server.final();
  }
  ZuCheckRT(pair.stop(), log_("stop both multiplexers without injecting a wake packet"));
  ZuCheckRT(!mainTCP.errors && !again.errors && !one.errors && !two.errors && !server.errors &&
      allReturned(a) && allReturned(b),
    log_("quiesce callbacks and return common ingress backing before restart"));
}

static void noStartFinal(Fixture &fixture, bool allocate)
{
  ZuTestScopeRT(noStartFinal);
  ZmRef<Zxdp::Instance> device;
  ZmRef<Zutcp::Instance> plugin;
  ZmRef<Zi::IOBufPool> ingress, transmit;
  {
    Peer peer;
    if (!peer.init(fixture, 0)) return;
    device = peer.device;
    plugin = peer.tcp;
    ingress = peer.rx->pool;
    transmit = peer.tx->pool;
    if (allocate && !checked(peer.mx.alloc(), "allocate through the mx before final without start"))
      return;
  }
  ZuCheckRT(device && plugin && !device->mx() && !plugin->mx() && !device->allocated() &&
      !ingress->alloc() && !transmit->alloc(),
    log_("final without starting clears both borrowed mx pointers and releases backing"));
  {
    ZiMultiplex next;
    ZuCheckRT(device->init(&next, deviceParams(fixture.ports[0].name(), fixture.ifindex[0])).is<void>(),
      log_("previous final releases the exclusive port for a fresh open"));
    device->final();
  }
}

static void testDpdk()
{
  ZuTestScopeRT(testDpdk);
  Fixture fixture;
  if (!checked(fixture.network(), "set up two dummy links in an isolated network namespace")) return;
  CHECK(checksFatalHandoff("allocated", "!xdp || !xdp->allocated()"),
    "separate EAL process aborts specifically on preallocated handoff");
  CHECK(checksFatalHandoff("repeat", "!xdp || !xdp->allocated()"),
    "repeat init checks a supplied device's allocation before comparing ownership");
  CHECK(checksFatalHandoff("device-mx", "xdp->mx() == mx_"),
    "device handoff to a different multiplexer is fatal");
  CHECK(checksFatalHandoff("plugin-mx", "plugin->mx() == this"),
    "plugin registration with a different multiplexer is fatal");
  if (!checked(fixture.devices(), "initialize EAL and register two named stock ring ports")) return;
  ZuTestCallRT(noStartFinal, fixture, false);
  ZuTestCallRT(noStartFinal, fixture, true);
  ZmRef<Zxdp::Instance> device;
  ZmRef<Zutcp::Instance> plugin;
  {
    Peers pair{fixture};
    auto &a = pair.a;
    auto &b = pair.b;
    if (!a.init(fixture, 0) || !b.init(fixture, 1)) return;
    device = a.device;
    plugin = a.tcp;
    failAllocation(2);
    auto failure = a.mx.alloc();
    CHECK(failure.is<ZeException>() && allocationRolledBack() && !device->allocated() &&
	a.mx.state() == ZmEngineState::Stopped && !a.tx->allocBuf(),
      "partial native allocation failure frees provisional resources through the mux");
    CHECK((!a.mx.allocator<Zi::IOBufType::Rx, TCPInput>(128) &&
	!a.mx.allocator<Zi::IOBufType::TxTCP>() && !a.mx.allocator<Zi::IOBufType::App>() &&
	a.app->allocBuf()),
      "failed allocation freezes new registrations while existing App storage remains available");
    failAllocation(0);
    if (!checked(a.mx.alloc(), "retry allocation with the frozen pool collection")) return;
    auto nativeIngress = ZxdpDpdkTest::native(a.rx->pool);
    CHECK(nativeIngress && a.mx.alloc().is<void>() &&
	ZxdpDpdkTest::native(a.rx->pool) == nativeIngress && !b.device->allocated(),
      "repeated stopped alloc reuses A's backing and B still awaits implicit allocation");
    auto hold = a.smallTx->allocBuf(nullptr, 16);
    if (hold) memset(hold->data(), 0x5a, hold->length);
    auto address = hold ? hold->data_() : nullptr;
    auto intact = [aTx = a.smallTx, &hold, address] {
      if (!hold || hold->data_() != address || (uintptr_t(address) & 255) ||
	  hold->allocator() != aTx || hold->length != 16 || aTx->allocBuf()) return false;
      for (auto octet : hold->cspan()) if (octet != 0x5a) return false;
      return true;
    };
    failActivation();
    bool activated = a.mx.start();
    CHECK(!activated && activationFailed() && device->allocated() && !device->ready() &&
	a.mx.state() == ZmEngineState::Stopped && portStopped(fixture.ports[0].name()) && intact(),
      "failure after native start deactivates both queues and preserves caller-held Tx backing");
    if (activated || !activationFailed()) return;
    for (bool v6 : {false, true}) for (unsigned cycle = 0; cycle < 3; ++cycle) {
      bool startA = a.mx.start();
      bool startB = b.mx.start();
      CHECK(startA && startB && a.device->ready() && b.device->ready() &&
	  a.device->allocated() && b.device->allocated() && intact(),
	"start/restart both plugins with retained backing [IPv", v6 ? 6 : 4, ", cycle ", cycle, "]");
      if (!startA || !startB) return;
      ZuTestCallRT(traffic, pair, cycle != 1, cycle != 0, cycle == 0, v6);
      auto other = Zxdp::instance(Zxdp::Type::Dpdk);
      CHECK(a.mx.state() == ZmEngineState::Stopped && b.mx.state() == ZmEngineState::Stopped &&
	  a.device->allocated() && b.device->allocated() && intact() &&
	  other->init(&a.mx, deviceParams(fixture.ports[0].name(), fixture.ifindex[0])).is<ZeException>(),
	"stop preserves native pool addresses and exclusive port use [", cycle, "]");
    }
    hold = nullptr;
    auto recycled = a.smallTx->allocBuf();
    CHECK(recycled && recycled->data_() == address,
      "returning the caller-held buffer makes that exact slot reusable");
  }
  CHECK(device && plugin && !device->mx() && !plugin->mx() && !device->allocated(),
    "mux destruction finalizes both instances after the last stop");
  ZiMultiplex next;
  auto fresh = Zxdp::instance(Zxdp::Type::Dpdk);
  CHECK(fresh->init(&next, deviceParams(fixture.ports[0].name(), fixture.ifindex[0])).is<void>(),
    "a fresh device can claim the port after mux finalization");
  fresh->final();
}

int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--fatal-handoff")) return fatalHandoff(argv[2]);
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDpdk);
}
