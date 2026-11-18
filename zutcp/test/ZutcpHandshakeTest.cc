//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmQueue.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>

#include <zlib/Zutcp.hh>

#include "ZutcpTestInstance.hh"

using namespace ZuTestUtil;

namespace Zutcp {
struct TCPStateTest {
  template <typename T> static bool application(const T &link) {
    return link.m_txAppClose.application;
  }
  template <typename T> static void stageEarly(T &link) {
    auto &c = static_cast<typename T::Base::Base &>(link);
    c.initServerPath({ZiIP{"198.18.0.1"}, 18181}, {ZiIP{"198.18.0.2"}, 18182}, 1200);
    c.setTCPOriginsRx({1000, 2000, true, true, true});
    c.startHandshake();
    c.installTCPOptionsRx({.seq = 2000, .flags = 0x02,
      .options = {.flags = TCPOptions::MSS, .mss = 1200}});
  }
  template <typename T> static int earlyInput(T &link) {
    auto &c = static_cast<typename T::Base::Base &>(link);
    auto packet = link.app()->packets.allocBuf();
    memcpy(packet->data(), "abc", 3);
    packet->length = 3;
    ByteData frame{.offset = 0, .length = 3, .payload = packet->cspan()};
    c.m_rxTFOAllow = true;
    int result = c.processFrame(frame, ZuMv(packet));
    c.m_rxTFOAllow = false;
    return result;
  }
  template <typename T> static bool earlyPending(T &link) {
    auto &c = static_cast<typename T::Base::Base &>(link);
    return c.m_mpsPendingRx && !c.m_mpsReadyRx;
  }
  template <typename T> static void peerACK(T &link) {
    auto &c = static_cast<typename T::Base::Base &>(link);
    c.m_rxSynAcked = true;
    // The raced establishment path encounters the existing pending checkpoint.
    c.prepareMPSRx();
  }

};
}

struct GateMx : ZiMultiplex {
  using ZiMultiplex::ZiMultiplex;
  ZmQueue<ZmFn<>> tx;
  template <typename L> void txRun(L l) {
    tx.push([l = ZuMv(l)]() mutable { (void)l(); });
  }
};

struct GateApp : public ZmNHeap<GateApp>, public ZutcpTest::Context<GateMx> {
friend inline ZuStringT<"Zutcp.HandshakeTest.GateApp"> ZmNHeapID(GateApp *);
  using ZmNHeap<GateApp>::operator new;
  using ZmNHeap<GateApp>::operator delete;
  using Fn = ZmFn<>;
  ZmQueue<Fn> rx, tx;
  Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, Zutcp::PacketBuf, Zutcp::BufSize> packets;
  ZmRef<ZiIOBuf> response;
  ZmFn<> onInvoke;
  unsigned owner = 1, shortSent = 0, longSent = 0, errors = 0;
  Zutcp::TCPHeader responseTCP;
  bool inlineRx = false, active = false;
  Zutcp_::TupleRouter<void> routes;
  unsigned retirements = 0, disconnects = 0;
  bool retiredBeforeDisconnect = false;
  ZuTime clock;
  Zi::IOBufAllocator *testTx = nullptr;
  Zi::IOBufPool *testPool = nullptr;

  Zi::IOBufAllocator *dataTxAllocator() {
    return testTx ? testTx : ZutcpTest::Context<GateMx>::dataTxAllocator();
  }
  Zi::IOBufPool *txPool(Zi::IOBufAllocator *allocator) const {
    if (allocator && allocator == testTx) return testPool;
    return ZutcpTest::Context<GateMx>::txPool(allocator);
  }
  bool ioActive() const { return active; }
  unsigned txThread() const { return active ? 2 : ZutcpTest::Context<GateMx>::txThread(); }
  GateMx *mx() const { return static_cast<GateMx *>(ZutcpTest::Context<GateMx>::mx()); }
  ZuTime transportNow() const { return *clock ? clock : ZutcpTest::Context<GateMx>::transportNow(); }

  GateApp(bool scale = false, bool ts = false, uint64_t maxData = 8192, bool ecn = false) {
    ZiAssert(init(ZutcpTest::Params{&multiplex}.maxData(maxData)
      .sackPermitted(false).windowScale(scale).timestamps(ts).ecn(ecn)
      .errorFn(ZutcpTest::Params::ErrorFn{[this](ZeException) { ++errors; }})), "Zutcp", (),
      "setup gate fixture initialization failed", abort());
  }
  ~GateApp() { response = nullptr; }
  bool rxInvoked() const { return owner == 1; }
  bool txInvoked() const { return owner == 2; }
  template <typename L> void rxRun(L l) {
    if (inlineRx && rxInvoked()) (void)l();
    else rx.push([l = ZuMv(l)]() mutable { (void)l(); });
  }
  template <typename L> void txRun(L l) { tx.push([l = ZuMv(l)]() mutable { (void)l(); }); }
  template <typename L> void txInvoke(L l) {
    if (onInvoke) {
      auto fn = ZuMv(onInvoke);
      fn();
    }
    if (txInvoked()) (void)l(); else txRun(ZuMv(l));
  }
  template <typename T, typename L> void txInvoke(T *, L l) { txInvoke(ZuMv(l)); }
  template <typename L> void rxInvoke(L l) {
    if (rxInvoked()) (void)l(); else rxRun(ZuMv(l));
  }
  template <typename T, typename L> void rxInvoke(T *, L l) { rxInvoke(ZuMv(l)); }
  void runRx() { owner = 1; while (auto fn = rx.shift()) fn(); }
  void runTx() { owner = 2; while (auto fn = tx.shift()) fn(); owner = 1; }
  void runBarrier() {
    owner = 2; while (auto fn = multiplex.tx.shift()) fn(); owner = 1;
  }
  void drain() {
    do { runTx(); runBarrier(); runRx(); }
    while (tx.count_() || multiplex.tx.count_() || rx.count_());
  }
  void stepTx() { owner = 2; if (auto fn = tx.shift()) fn(); owner = 1; }
  void refreshLinkRoutes(void *) { }
  template <typename T> void retireLinkRoutes(T *link) {
    if (routes.retire(link->closedKeyRx(), link)) ++retirements;
  }
  void disconnected(void *, bool) {
    ++disconnects;
    retiredBeforeDisconnect = !routes.count();
  }
  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool = false) {
    auto buf = packets.allocBuf();
    return ZmRef<Zutcp::PacketBuf>{static_cast<Zutcp::PacketBuf *>(buf.ptr())};
  }
  bool sendPktRaw(ZmRef<ZiIOBuf> buf, ZiSockAddr, Zutcp::EcnMark::T, bool,
      const Zutcp::IPPath &) {
    auto tcp = static_cast<Zutcp::PacketBuf *>(buf.ptr())->tcp;
    if (tcp.flags & 0x02) { ++longSent; responseTCP = tcp; response = ZuMv(buf); }
    else ++shortSent;
    return true;
  }
};

struct AbortCli : ZmNHeap<AbortCli>, Zutcp::CliCxn<GateApp, AbortCli> {
friend ZuStringT<"Zutcp.Handshake.AbortCli"> ZmNHeapID(AbortCli *);
  using Base = Zutcp::CliCxn<GateApp, AbortCli>;
  using State = Base::Base;
  using State::closingRx;
  unsigned disconnectedCount = 0, endpointDownCount = 0, connectFailures = 0, connects = 0;
  unsigned peerClosed = 0;
  bool reconnectOnDisconnect = false;
  unsigned setups = 0;
  ZiSockAddr setupTarget;
  unsigned resetPackets = 0, flushRequests = 0;
  Zutcp::TCPHeader lastReset;
  void connect_1(ZiIP ip, uint16_t port) {
    ++setups;
    setupTarget = ZiSockAddr{ip, port};
    Base::connect_1(ZuMv(ip), port);
  }
  explicit AbortCli(GateApp *app) : Base{app} { Base::configLocalParams(app); }
  void prepare(bool routed = true) {
    Base::resetLink();
    Base::configLocalParams(app());
    (void)Base::setTCPOriginsRx({UINT32_MAX, UINT32_MAX, true, true, false});
    if (routed) app()->txRun([link = this, gen = State::dataGen()] {
      if (gen != link->dataGen()) return;
      link->initClientPathTx({ZiIP{"198.18.0.1"}, 18191},
        {ZiIP{"198.18.0.2"}, 18192}, 1200);
    });
  }
  bool originsForTest(Zutcp::TCPOrigins origins) { return State::setTCPOriginsRx(origins); }
  void installOriginsForTest(Zutcp::TCPOrigins origins, uint64_t gen) {
    State::installTCPOriginsTx(origins, gen);
  }
  Zutcp::TCPHeader headerForTest() const { return State::tcpHeader(); }
  void delayedReady() { Base::endpointReady(this); }
  void gracefulForTest(Base::CloseFn fn) {
    Base::closeAfterDisconnect(true, false, {}, ZuMv(fn));
  }
  void finishForTest() { Base::nativeFinished(); }
  bool timerWork() const { return Base::timersActive(); }
  using State::closeErrorRx;
  using State::closeErrorTx;
  bool appCloseTxForTest() const { return Zutcp::TCPStateTest::application(*this); }
  void queueTxFlush() { ++flushRequests; }
  void queueTxFlush(ZiSockAddr) { ++flushRequests; }
  void establishForTest(bool established = true) {
    State::startHandshake();
    if (established) State::establish();
  }
  void idleForTest(ZuTime timeout) { State::startIdleTimerTx(timeout); }
  void idleTickForTest() { State::cxnTimer(Zutcp::CxnTimer::Idle); }
  bool sendTCPReset() {
    return State::sendTCP({}, false, 0x14, [this] { return app()->allocTxPkt_(); },
      [this](auto buf, auto) {
	++resetPackets;
	lastReset = buf->tcp;
	return app()->txInvoked();
      });
  }
  void connected(Zutcp::Connected) { ++connects; }
  int process(Zutcp::Reader &) { return 0; }
  void disconnected(bool peer) {
    peerClosed += unsigned(peer);
    ++disconnectedCount;
    if (reconnectOnDisconnect) {
      reconnectOnDisconnect = false;
      Base::connect(Zutcp::Host{"198.18.0.77"}, 18191);
    }
  }
  void endpointDown() { ++endpointDownCount; }
  void connectFailed(bool) { ++connectFailures; }
};

struct CloseSrv : ZmNHeap<CloseSrv>, Zutcp::SrvCxn<GateApp, CloseSrv> {
friend inline ZuStringT<"Zutcp.Handshake.CloseSrv"> ZmNHeapID(CloseSrv *);
  using Base = Zutcp::SrvCxn<GateApp, CloseSrv>;
  using State = Base::Base;
  unsigned resetPackets = 0;
  Zutcp::TCPHeader lastReset;
  using State::initServerPath;
  using State::disconnected;
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool control = false) {
    return app()->allocTxPkt_(control);
  }
  bool sendPkt(const ZmRef<ZiIOBuf> &packet) { return app()->sendPkt(packet); }
  bool sendPktRaw(ZmRef<ZiIOBuf> packet, ZiSockAddr addr, Zutcp::EcnMark::T ecn,
      bool priority, const Zutcp::IPPath &path) {
    return app()->sendPktRaw(ZuMv(packet), addr, ecn, priority, path);
  }
  void retireLinkRoutes() { app()->retireLinkRoutes(this); }
  void closedRx(bool peer) { app()->disconnected(this, peer); }
  explicit CloseSrv(GateApp *app) : Base{app} { State::configLocalParams(app); }
  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }
  void prepare() {
    State::resetLink();
    State::configLocalParams(app());
    (void)State::setTCPOriginsRx({UINT32_MAX, UINT32_MAX, true, true, true});
    app()->txRun([link = this, gen = State::dataGen()] {
      if (gen != link->dataGen()) return;
      link->initClientPathTx({ZiIP{"198.18.0.1"}, 18191},
        {ZiIP{"198.18.0.2"}, 18192}, 1200);
    });
    State::startHandshake();
    State::establish();
  }
  void idleForTest(ZuTime timeout) { State::startIdleTimerTx(timeout); }
  void idleTickForTest() { State::cxnTimer(Zutcp::CxnTimer::Idle); }
  bool timerWork() const { return State::timersActive(); }
  using State::closeErrorRx;
  using State::closeErrorTx;
  bool appCloseTxForTest() const { return Zutcp::TCPStateTest::application(*this); }
  bool sendTCPReset() {
    return State::sendTCP({}, false, 0x14, [this] { return app()->allocTxPkt_(); },
      [this](auto buf, auto) {
	++resetPackets;
	lastReset = buf->tcp;
	return app()->txInvoked();
      });
  }
};

static void testCloseRouteDrain()
{
  ZuTestScope(testCloseRouteDrain);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<CloseSrv> link = new CloseSrv{&app};
  link->prepare();
  link->initServerPath({ZiIP{"198.18.0.1"}, 18191}, {ZiIP{"198.18.0.2"}, 18192}, 1200);
  app.drain();
  auto key = link->closedKeyRx();
  bool routed = app.routes.add(key, link.ptr());
  app.active = true;
  link->disconnecting(true);
  link->disconnected(false);
  app.runRx();
  bool awaitingTx = app.tx.count_() && !app.multiplex.tx.count_();
  auto route = static_cast<CloseSrv *>(app.routes.find(key));
  Zutcp::Datagram late;
  late.tcp.flags = 0x10;
  bool absorbed = route && route->receiveSegment(ZuMv(late));
  ZuCHECK(routed && awaitingTx && absorbed && route == link.ptr() &&
      !app.retirements && !app.disconnects,
    "a closing route must absorb late packets while its Tx snapshot is pending");
  app.runTx();
  ZuCHECK(app.multiplex.tx.count_() == 1 && !app.rx.count_() &&
      app.routes.find(key) == link.ptr() && !app.retirements && !app.disconnects,
    "route membership must survive the multiplexer Tx drain barrier");
  app.runBarrier();
  ZuCHECK(app.rx.count_() == 1 && app.routes.find(key) == link.ptr() &&
      !app.retirements && !app.disconnects,
    "route retirement must wait for the final Rx owner continuation");
  app.runRx();
  ZuCHECK(!app.routes.find(key) && app.retirements == 1 && app.disconnects == 1 &&
      app.retiredBeforeDisconnect && !app.tx.count_() && !app.multiplex.tx.count_(),
    "final Rx must retire the route before publishing disconnected completion");
  app.active = false;
  link = nullptr;
  app.drain();
}

static void testNativeZeroDisconnect()
{
  ZuTestScope(testNativeZeroDisconnect);
  using NumericResult = decltype(ZuDeclVal<AbortCli &>().disconnect(0));
  ZuCHECK((ZuIsSame<NumericResult, bool>{}), "literal zero must select numeric acceptance");
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  ZmRef<CloseSrv> server = new CloseSrv{&app};
  server->prepare();
  app.drain();
  // No real Endpoint is installed for the client: exercise its numeric-zero
  // graceful entry through the same protected seam used by the queued close test.
  unsigned completed = 0;
  client->gracefulForTest([&completed] { ++completed; });
  client->disconnect(0);
  server->disconnect();
  app.runTx();
  app.owner = 2;
  bool waiting = client->finSent() && server->finSent() &&
    !client->txCompleted() && !server->txCompleted();
  app.owner = 1;
  ZuCHECK(waiting && !completed && !client->disconnectedCount &&
      !client->disconnecting() && !server->disconnecting() &&
      client->txAdmissionClosed() && server->txAdmissionClosed(),
    "zero/default close must keep both native roles alive while FIN is unacknowledged");
  auto packet = app.packets.allocBuf();
  memcpy(packet->data(), "peer-data", 9);
  packet->length = 9;
  Zutcp::ByteData frame{.offset = 0, .length = 9, .payload = packet->cspan()};
  ZuCHECK(server->processFrame(frame, packet) == 0 &&
      server->reader().length() == 9 && server->reader().span().data() == frame.payload.data() &&
      !server->disconnecting(),
    "local default FIN must preserve independent peer DATA and original Rx backing");
  client->abort();
  server->abortLocalRx(0, [server] { server->tcpResetAbort(false); });
  app.drain();
  ZuCHECK(completed == 1 && client->disconnectedCount == 1 && client->endpointDownCount == 1,
    "numeric zero must preserve normal client notification and queued completion");
  client = nullptr;
  server = nullptr;
  app.drain();
}

static void testNativeErrorDisconnect()
{
  ZuTestScope(testNativeErrorDisconnect);
  for (unsigned wide = 0; wide < 2; ++wide) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    ZmRef<CloseSrv> server = new CloseSrv{&app};
    server->prepare();
    app.drain();
    uint64_t error = wide ? (uint64_t(1)<<40) | 0x41 : 7;
    bool cliAccepted = wide ? client->disconnect(error) : client->disconnect(7);
    bool srvAccepted = wide ? server->disconnect(error) : server->disconnect(7);
    client->disconnect(error);
    server->disconnect(error);
    app.runTx();
    app.owner = 2;
    bool reset = client->resetPackets == 1 && server->resetPackets == 1 &&
      client->lastReset.flags == 0x14 && server->lastReset.flags == 0x14 &&
      !client->finSent() && !server->finSent() &&
      client->closeErrorTx() == error && server->closeErrorTx() == error &&
      client->appCloseTxForTest() && server->appCloseTxForTest();
    app.owner = 1;
    app.drain();
    bool notified = client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      server->closed() && client->closeErrorRx() == error &&
      server->closeErrorRx() == error;
    unsigned completed = 0;
    client->abort([&completed] { ++completed; });
    client->disconnect(error);
    server->disconnect(error);
    app.drain();
    app.owner = 2;
    bool quiet = !client->timerWork() && !server->timerWork() &&
      client->resetPackets == 1 && server->resetPackets == 1 &&
      client->closeErrorTx() == error && server->closeErrorTx() == error;
    app.owner = 1;
    ZuCHECK(cliAccepted && srvAccepted && reset && notified && quiet && completed == 1 &&
	client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
	server->closed() && server->disconnecting(),
      "literal/full-width numeric errors must reset native roles, retain local application "
      "error and notify once through repeated close and late abort completion");
    client = nullptr;
    server = nullptr;
    app.drain();
  }
}

static void testPendingErrorDisconnect()
{
  ZuTestScope(testPendingErrorDisconnect);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare(false); // Keep the current generation's Tx reset queued.
  uint64_t error = (uint64_t(1)<<40) | 0x42;
  unsigned completed[2] = {};
  bool accepted = client->disconnect(error);
  client->abort([&completed] { ++completed[0]; });
  app.runRx();
  bool pending = !client->disconnecting() && !completed[0];
  app.drain();
  client->abort([&completed] { ++completed[1]; });
  client->disconnect(error);
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork() && !client->resetPackets &&
    client->closeErrorTx() == error && client->appCloseTxForTest();
  app.owner = 1;
  ZuCHECK(accepted && pending && quiet && completed[0] == 1 && completed[1] == 1 &&
      client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      client->closeErrorRx() == error,
    "pending numeric cancellation must preserve its error and both abort completions "
    "without inventing a peer reset or duplicating notification");
  client = nullptr;
  app.drain();
}

struct ErrorTxAllocator : Zi::IOBufAllocator {
  alignas(ZiIOBuf_Align) uint8_t bytes[512];
  mutable unsigned returned = 0;
  mutable bool valid = true;
  explicit ErrorTxAllocator(ZiMultiplex *mx) :
    IOBufAllocator{mx, Zi::IOBufType::TxTCP, 512, 256} {
    memset(bytes, 0xa5, sizeof(bytes));
  }
  uint8_t *alloc(unsigned) const override { return nullptr; }
  void free(uint8_t *p, unsigned n, bool registered) const override {
    valid &= p == bytes && n == sizeof(bytes) && registered;
    ++returned;
  }
  ZmRef<ZiIOBuf> allocBuf(void *, uint32_t, uint32_t) override { return nullptr; }
};

static void testNativeQueueError()
{
  ZuTestScope(testNativeQueueError);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  app.drain();
  client->txQueueMax(1);
  unsigned errors = 0, callbackOwner = 0;
  client->txErrorFn(ZiTxErrorFn{[&app, &errors, &callbackOwner](ZeException &) {
    ++errors;
    callbackOwner = app.owner;
    return false;
  }});
  app.owner = 3;
  auto buf = app.packets.allocBuf();
  buf->length = 4;
  bool admitted = client->enqueue(buf);
  auto held = app.tx.shift(); // Admission exists, but its backing has not reached Tx.
  bool refused = !client->enqueue(buf);
  bool deferred = !client->disconnecting() && !client->disconnectedCount &&
    !client->endpointDownCount;
  app.runRx();
  app.runTx();
  app.owner = 2;
  bool reset = client->resetPackets == 1 && client->lastReset.flags == 0x14 &&
    !client->finSent();
  app.owner = 1;
  app.drain();
  app.tx.push(ZuMv(held));
  app.drain();
  app.owner = 2;
  bool quiet = !client->txQueueCount() && !client->txRangeCount() && !client->timerWork();
  app.owner = 1;
  unsigned completed = 0;
  client->abort([&completed] { ++completed; });
  app.drain();
  ZuCHECK(admitted && refused && deferred && reset && quiet && errors == 1 &&
      callbackOwner == 3 && client->disconnectedCount == 1 &&
      client->endpointDownCount == 1 && completed == 1,
    "a terminating native queue error from an arbitrary producer must reset and drain "
    "through the client role before a late admitted handoff can install data");
  client = nullptr;
  app.drain();
}

static void testNativeBackingError()
{
  ZuTestScope(testNativeBackingError);
  for (unsigned serverRole = 0; serverRole < 2; ++serverRole) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    ZmRef<CloseSrv> server = new CloseSrv{&app};
    server->prepare();
    app.drain();
    ZiMultiplex foreignMx;
    ErrorTxAllocator allocator{&foreignMx};
    ZmRef<ZiIOBuf> buf = new Zi::IOBufStub<ZiIOBuf>{
      allocator.bytes, unsigned(sizeof(allocator.bytes)), &allocator, nullptr,
      unsigned(8), false, true};
    buf->skip = 256;
    memcpy(allocator.bytes + 256, "original", 8);
    unsigned errors = 0, callbackOwner = 0;
    auto error = ZiTxErrorFn{[&app, &errors, &callbackOwner](ZeException &) {
      ++errors;
      callbackOwner = app.owner;
      return false;
    }};
    if (serverRole) server->txErrorFn(ZuMv(error)); else client->txErrorFn(ZuMv(error));
    app.owner = 2;
    bool refused = serverRole ? !server->enqueue_(buf) : !client->enqueue_(buf);
    bool returned = !client->txQueueCount() && !server->txQueueCount() && !allocator.returned;
    app.owner = 1;
    bool deferred = !client->disconnecting() && !server->disconnecting() &&
      !client->disconnectedCount;
    app.drain();
    app.owner = 2;
    bool reset = serverRole ? server->resetPackets == 1 && server->lastReset.flags == 0x14 :
      client->resetPackets == 1 && client->lastReset.flags == 0x14;
    bool quiet = serverRole ? !server->timerWork() && !server->txRangeCount() :
      !client->timerWork() && !client->txRangeCount();
    app.owner = 1;
    bool retired = serverRole ? server->closed() && server->disconnecting() :
      client->disconnectedCount == 1 && client->endpointDownCount == 1;
    bool original = !memcmp(allocator.bytes + 256, "original", 8) && !allocator.returned;
    buf = nullptr;
    ZuCHECK(refused && returned && deferred && reset && quiet && retired && original &&
	errors == 1 && callbackOwner == 2 && allocator.valid && allocator.returned == 1,
      "foreign registered backing errors must retire admission before a once-only native "
      "role abort while the rejected original lease stays with its caller");
    client->abort();
    server->abortLocalRx(0, [server] { server->tcpResetAbort(false); });
    app.drain();
    client = nullptr;
    server = nullptr;
    app.drain();
  }
}

static void testNativeErrorReplacement()
{
  ZuTestScope(testNativeErrorReplacement);
  for (unsigned queued = 0; queued < 2; ++queued) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    client->establishForTest();
    app.drain();
    client->txQueueMax(1);
    unsigned errors = 0;
    client->txErrorFn(ZiTxErrorFn{[client, &app, &errors, queued](ZeException &) {
      ++errors;
      if (queued) app.rxRun([client] { client->prepare(); });
      else { client->prepare(); app.runTx(); }
      return false;
    }});
    auto generation = client->dataGen();
    app.owner = queued ? 3 : 1;
    auto buf = app.packets.allocBuf();
    buf->length = 4;
    bool admitted = client->enqueue(buf);
    auto held = app.tx.shift();
    bool refused = !client->enqueue(buf);
    app.drain();
    app.tx.push(ZuMv(held));
    app.drain();
    app.owner = 2;
    bool open = !client->txAdmissionClosed() && !client->finSent() &&
      !client->txQueueCount() && !client->txRangeCount() && !client->resetPackets;
    app.owner = 1;
    ZuCHECK(admitted && refused && open && errors == 1 &&
	client->dataGen() == generation + 1 && !client->disconnecting() &&
	!client->disconnectedCount && !client->endpointDownCount,
      "an error callback replacement before return or before Rx dispatch must not "
      "abort or close producer admission in the replacement generation");
    client->txErrorFn({}); // Release the callback's fixture reference before owner teardown.
    client->abort();
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testNativeErrorPolicy()
{
  ZuTestScope(testNativeErrorPolicy);
  for (unsigned policy = 0; policy < 2; ++policy) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    app.drain();
    client->txQueueMax(1);
    unsigned errors = 0;
    if (policy) client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) {
      ++errors;
      return true;
    }});
    app.owner = 3;
    auto buf = app.packets.allocBuf();
    buf->length = 4;
    bool admitted = client->enqueue(buf);
    auto held = app.tx.shift();
    bool refused = !client->enqueue(buf);
    bool live = !client->disconnecting() && !client->disconnectedCount && !app.rx.count_();
    client->closeTxAdmission();
    bool closed = !client->enqueue(buf) && errors == policy;
    client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
    bool benign = !client->enqueue(buf) && errors == policy && !app.rx.count_();
    app.owner = 1;
    ZuCHECK(admitted && refused && live && closed && benign && client->txQueueCount() == 1,
      "true or absent error policy must remain nonterminating and a closed full queue "
      "must refuse benignly without calling even a terminating error callback");
    client->abort();
    app.drain();
    app.tx.push(ZuMv(held));
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testTCPOriginOwners()
{
  ZuTestScope(testTCPOriginOwners);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> link = new AbortCli{&app};
  // Constructor has no known origin: zero is a valid selected ISN.
  Zutcp::TCPOrigins first{0, 0xffffffff, true, true, false};
  uint64_t old = link->dataGen();
  bool chosen = link->originsForTest(first) && link->originsForTest(first) &&
    !link->originsForTest({1, 0xffffffff, true, true, false});
  app.runTx();
  app.owner = 2;
  auto initial = link->headerForTest();
  app.owner = 1;
  link->prepare(); // Reset/queue the new generation's logical-base0 fixture anchors.
  app.runTx();
  app.owner = 2;
  link->installOriginsForTest(first, old);
  auto replaced = link->headerForTest();
  app.owner = 1;
  ZuCHECK(chosen && initial.seq == 1 && !initial.ack && !replaced.seq && !replaced.ack &&
      link->dataGen() != old,
    "zero ISN stays valid, conflicts refuse and stale Tx posts cannot replace "
    "a new incarnation's selected anchors");
  link->abort();
  app.drain();
  link = nullptr;
  app.drain();
}

static void testNativeIdleReset()
{
  ZuTestScope(testNativeIdleReset);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  app.clock = Zm::now() + ZuTime{1000};
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  client->establishForTest();
  ZmRef<CloseSrv> server = new CloseSrv{&app};
  server->prepare();
  app.drain();
  app.owner = 2;
  client->idleTickForTest();
  server->idleTickForTest();
  bool disabled = !client->resetPackets && !server->resetPackets;
  client->idleForTest(ZuTime{1});
  server->idleForTest(ZuTime{1});
  app.clock += ZuTime{100};
  client->idleTickForTest();
  server->idleTickForTest();
  bool sent = client->resetPackets == 1 && server->resetPackets == 1 &&
    client->lastReset.flags == 0x14 && server->lastReset.flags == 0x14;
  app.owner = 1;
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork() && !server->timerWork();
  app.owner = 1;
  ZuCHECK(disabled && sent && quiet && client->disconnectedCount == 1 &&
      client->endpointDownCount == 1 && client->disconnecting() && server->disconnecting(),
    "configured idle expiry must submit meaningful RST before once-only native owner drain");
  client = nullptr;
  server = nullptr;
  app.drain();
}

static void testFinRequestReplacement()
{
  ZuTestScope(testFinRequestReplacement);
  for (unsigned retained = 0; retained < 2; ++retained) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> client = new AbortCli{&app};
    client->prepare();
    app.drain();
    auto generation = client->dataGen();
    unsigned errors = 0;
    client->txErrorFn(ZiTxErrorFn{[&errors](ZeException &) { ++errors; return false; }});
    // Complete replacement after the old payload reservation, before the
    // public caller has posted its FIN request. Both sends must keep old gen.
    app.onInvoke = [client, &app] {
      client->prepare();
      app.runTx();
    };
    bool accepted;
    if (retained) {
      auto buf = app.packets.allocBuf();
      buf->length = 4;
      accepted = client->send(buf, true);
    } else accepted = client->send("data", true);
    app.drain();
    app.owner = 2;
    bool open = !client->finSent() && !client->txAdmissionClosed() && !client->txQueueCount();
    app.owner = 1;
    ZuCHECK(accepted && open && !errors && !client->disconnecting() &&
	client->dataGen() == generation + 1,
      "an old accepted payload/FIN request must not close admission or FIN in its replacement");
    client->abort();
    app.drain();
    client = nullptr;
    app.drain();
  }
}

static void testPendingCloseCallbacks()
{
  ZuTestScope(testPendingCloseCallbacks);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare(false); // Deliberately hold the new generation's queued Tx reset.
  unsigned completed[3] = {};
  client->disconnect([&completed] { ++completed[0]; });
  client->abort([&completed] { ++completed[1]; });
  app.runRx();
  ZuCHECK(!client->disconnecting() && !completed[0] && !completed[1],
    "pending graceful cancel must order reset/abort Tx work before starting the owner drain");
  app.drain();
  client->abort([&completed] { ++completed[2]; });
  app.drain();
  app.owner = 2;
  bool quiet = !client->timerWork();
  app.owner = 1;
  ZuCHECK(completed[0] == 1 && completed[1] == 1 && completed[2] == 1 &&
      client->disconnectedCount == 1 && client->endpointDownCount == 1 &&
      !client->resetPackets && quiet,
    "pending disconnect and every abort completion must finish once even after the same drain");
  client = nullptr;
  app.drain();
}

static void testRetainedAdmissionFinWake()
{
  ZuTestScope(testRetainedAdmissionFinWake);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> client = new AbortCli{&app};
  client->prepare();
  app.drain();
  auto buf = app.packets.allocBuf();
  buf->length = 4;
  ZuCHECK(client->send(buf, false) && client->txQueueCount() == 1,
    "public retained send must reserve its handoff before returning success");
  auto old = app.tx.shift(); // Keep that pre-admitted old-generation transfer delayed.
  client->prepare();
  app.runTx();
  app.owner = 2;
  client->txCredit(8);
  client->fin();
  bool blocked = !client->finReady() && client->txQueueCount() == 1;
  unsigned before = client->flushRequests;
  app.owner = 1;
  ZuCHECK(!client->send(buf, false) && client->txQueueCount() == 1,
    "a new retained offer after FIN must fail without consuming another reservation");
  app.tx.push(ZuMv(old));
  app.runTx();
  app.owner = 2;
  bool resumed = !client->txQueueCount() && client->finReady() &&
    client->flushRequests > before;
  app.owner = 1;
  ZuCHECK(blocked && resumed && !client->disconnecting(),
    "stale handoff retirement must wake the new FIN without accepting old payload");
  client->abort();
  app.drain();
  client = nullptr;
  app.drain();
}

static void testPeerResetCloseSnapshot()
{
  ZuTestScope(testPeerResetCloseSnapshot);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  for (unsigned established = 0; established < 2; ++established) {
    ZmRef<AbortCli> link = new AbortCli{&app};
    link->prepare();
    link->establishForTest(bool(established));
    app.drain();
    Zutcp::Datagram reset;
    reset.tcp = {.flags = 0x14}; // Fixture origins place the exact peer RST at wire zero.
    bool accepted = link->receiveSegment(ZuMv(reset));
    app.runTx();
    auto snapshot = app.rx.shift();
    bool queued = bool(snapshot);
    if (snapshot) snapshot();
    bool closing = link->closingRx() && !link->disconnectedCount && !link->connectFailures;
    app.drain();
    ZuCHECK(accepted && queued && closing && link->disconnectedCount == 1 &&
	link->peerClosed == established && link->connectFailures == !established &&
	link->endpointDownCount == 1 && !link->resetPackets && link->disconnecting(),
      "a Closing snapshot before peer-reset drain must preserve established peer notification "
      "while an unpublished handshake still fails once without reflecting reset");
    link = nullptr;
    app.drain();
  }
}

static void testAbortBeforeReady()
{
  ZuTestScope(testAbortBeforeReady);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  uint64_t gen = link->dataGen();
  unsigned completed[2] = {};
  link->abort([&completed] { ++completed[0]; });
  link->abort([&completed] { ++completed[1]; });
  // Route setup can publish endpoint readiness while the abort's Tx turn is
  // still queued. It must not reset runtime generation and lose completion.
  link->delayedReady();
  ZuCHECK(!completed[0] && !completed[1] && !link->readyCount() && link->dataGen() == gen,
    "late endpoint-ready must not start a new handshake during an already requested abort");
  app.drain();
  ZuCHECK(completed[0] == 1 && completed[1] == 1 && link->disconnectedCount == 1 &&
      !app.shortSent && link->disconnecting(),
    "each abort completion must run once while native disconnect notifies once per incarnation");
  link->prepare();
  app.drain();
  unsigned nextCompleted = 0;
  link->abort([&nextCompleted] { ++nextCompleted; });
  app.drain();
  ZuCHECK(nextCompleted == 1 && link->disconnectedCount == 2 && !app.shortSent,
    "a new native generation must get its own once-only abort notification");
  link = nullptr;
  app.drain();
}

static void testNativeGracefulCallbacks()
{
  ZuTestScope(testNativeGracefulCallbacks);
  for (unsigned finishFirst = 0; finishFirst < 2; ++finishFirst) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> link = new AbortCli{&app};
    link->prepare();
    app.drain();
    unsigned completed[3] = {};
    if (finishFirst) {
      // FIN completion posts its Rx drain, then an application close queues
      // FIN work behind that completed Tx turn but before Rx sees retirement.
      app.owner = 2;
      link->finishForTest();
      app.owner = 1;
    }
    link->gracefulForTest([&completed] { ++completed[0]; });
    link->disconnect([&completed] { ++completed[1]; });
    if (!finishFirst) {
      app.runTx();
      app.owner = 2;
      bool waiting = link->finSent() && !link->txCompleted();
      app.owner = 1;
      ZuCHECK(waiting && !link->disconnecting() &&
	  !completed[0] && !completed[1] && !link->disconnectedCount,
	"repeated graceful requests must retain both callbacks while local FIN is "
	"unacknowledged");
      app.owner = 2;
      link->finishForTest();
      app.owner = 1;
    } else {
      // Run Rx first: the already completed transport starts draining before
      // its later queued FIN request can run. That request must not rearm work.
      app.runRx();
      ZuCHECK(link->disconnecting() && !completed[0] && !completed[1],
	"queued native finish must start owner drain before late Tx close work");
    }
    app.drain();
    app.owner = 2;
    bool quiet = !link->timerWork();
    app.owner = 1;
    ZuCHECK(completed[0] == 1 && completed[1] == 1 &&
	link->disconnectedCount == 1 && link->endpointDownCount == 1 && quiet,
      "automatic native retirement must preserve every graceful completion once and leave no "
      "timer");
    link->disconnect([&completed] { ++completed[2]; });
    app.drain();
    app.owner = 2;
    quiet = !link->timerWork();
    app.owner = 1;
    ZuCHECK(completed[0] == 1 && completed[1] == 1 && completed[2] == 1 &&
	link->disconnectedCount == 1 && link->endpointDownCount == 1 && quiet,
      "a graceful completion after retirement must complete without another notification or "
      "timer");
    link = nullptr;
    app.drain();
  }
}

static void testAbortReentrantConnect()
{
  ZuTestScope(testAbortReentrantConnect);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  app.inlineRx = true;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  link->reconnectOnDisconnect = true;
  unsigned completed[2] = {};
  bool ordered = true;
  link->abort([&link, &completed, &ordered] {
    ++completed[0];
    ordered &= link->endpointDownCount == 1;
  });
  link->abort([&link, &completed, &ordered] {
    ++completed[1];
    ordered &= link->endpointDownCount == 1;
  });
  app.drain();
  // The real fixture Instance is cold: reentrant connect reaches stopped-owner
  // setup failure cleanup after the old drain and all completions.
  ZuCHECK(ordered && completed[0] == 1 && completed[1] == 1 &&
      link->disconnectedCount == 2 && link->endpointDownCount == 2 &&
      link->connectFailures == 1,
    "reentrant connect must join the old drain before the failed new attempt cleans itself up");
  link->abort();
  app.drain();
  ZuCHECK(link->disconnectedCount == 2 && link->endpointDownCount == 2,
    "repeated abort after failed setup must not repeat that generation's cleanup");
  link = nullptr;
  app.drain();
}

static void testCoalescedNativeConnect()
{
  ZuTestScope(testCoalescedNativeConnect);
  for (unsigned invalid = 0; invalid < 2; ++invalid) {
    ZmRef<GateApp> appRef = new GateApp;
    auto &app = *appRef;
    ZmRef<AbortCli> link = new AbortCli{&app};
    link->prepare();
    app.drain();
    uint64_t generation = link->dataGen();
    link->connect(Zutcp::Host{"198.18.0.77"}, 18191);
    link->connect(Zutcp::Host{"198.18.0.88"}, 18192);
    if (invalid) link->connect(Zutcp::Host{"198.18.0.99"}, 0);
    app.drain();
    // The cold Instance rejects the one adopted attempt before raw endpoint I/O.
    // A second continuation must neither reset its token nor mix target fields.
    ZiSockAddr expected{ZiIP{"198.18.0.77"}, 18191};
    ZuCHECK(link->dataGen() == generation + 1 && link->setups == 1 &&
	link->setupTarget == expected && link->port() == (invalid ? 0 : 18192) &&
	link->connectFailures == invalid + 1 && link->disconnectedCount == 1 &&
	link->endpointDownCount == 1,
      "coalesced connect must claim one validated tuple and preserve later configuration");
    link = nullptr;
    app.drain();
  }
}

static void testNativeSetupFailureDrain()
{
  ZuTestScope(testNativeSetupFailureDrain);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<AbortCli> link = new AbortCli{&app};
  link->prepare();
  app.drain();
  // Caller validation rejection is not a new adopted attempt and must not
  // cancel an old transport or coalesce separate invalid calls by dataGen.
  link->connect({}, 0, {});
  link->connect({}, 0, {});
  app.drain();
  ZuCHECK(link->connectFailures == 2 && !link->disconnecting() &&
      !link->disconnectedCount && !link->endpointDownCount,
    "distinct invalid connect calls must notify without claiming an adopted failure drain");
  link->endpointFailed(false);
  link->endpointFailed(false);
  ZuCHECK(link->connectFailures == 2 && !link->disconnectedCount,
    "an adopted setup failure must defer notification until its owner drain finishes");
  app.drain();
  ZuCHECK(link->connectFailures == 3 && link->disconnectedCount == 1 &&
      link->endpointDownCount == 1 && link->disconnecting(),
    "failed route/setup must clean up once even when connectFailed only records the failure");
  link->abort();
  app.drain();
  ZuCHECK(link->connectFailures == 3 && link->disconnectedCount == 1 &&
      link->endpointDownCount == 1,
    "repeated abort must not release or notify the failed attempt twice");
  link = nullptr;
  app.drain();
}

struct GateCxn : ZmNHeap<GateCxn>, Zutcp::SrvCxn<GateApp, GateCxn> {
friend ZuStringT<"Zutcp.Handshake.Gate"> ZmNHeapID(GateCxn *);
  unsigned connections = 0, deliveries = 0;
  unsigned *destroyed = nullptr;
  bool early = false, resetOnProcess = false, reply = false;
  using Base = Zutcp::SrvCxn<GateApp, GateCxn>;

  using Base::receivedRouted;
  enum { RecoveryScanBatch = Zutcp::RecoveryScanBatch };
  ZmRef<Zutcp::PacketBuf> allocTxPkt_(bool control = false) {
    return app()->allocTxPkt_(control);
  }
  bool sendPkt(const ZmRef<ZiIOBuf> &packet) { return app()->sendPkt(packet); }
  bool sendPktRaw(ZmRef<ZiIOBuf> packet, ZiSockAddr addr, Zutcp::EcnMark::T ecn,
      bool priority, const Zutcp::IPPath &path) {
    return app()->sendPktRaw(ZuMv(packet), addr, ecn, priority, path);
  }
  GateCxn(GateApp *app) : Base{app} { Base::configLocalParams(app); }
  ~GateCxn() { if (destroyed) ++*destroyed; }
  void connected(Zutcp::Connected) { ++connections; }
  int process(Zutcp::Reader &reader) {
    auto n = reader.length();
    if (!n) return 0;
    ++deliveries;
    early |= !established();
    if (resetOnProcess) { reset(); return 0; }
    reader.clean();
    if (!retireRx(n)) return -1;
    const uint8_t reply[] = {'r'};
    this->reply = send(ZuBSpan{reply, sizeof(reply)}, false);
    return this->reply ? 1 : -1;
  }
  void reset() { Base::resetLink(); }
  bool originForTest(uint32_t local) {
    return Base::setTCPOriginsRx({local, 0, true, false, true});
  }
  bool stageReuseForTest(uint32_t local, uint64_t evidence) {
    return Base::stageReuseISNRx(local, evidence);
  }
  bool reusePendingForTest() const { return Base::reusePendingRx(); }
  void discardForTest() { Base::discardUnadoptedRx(); }
  bool initialHeaderForTest(Zutcp::TCPHeader &header) const {
    return Base::initialTCPHeader(header, true);
  }
  void drainFailedReuseForTest() {
    using State = Base::Base;
    State::disconnect(false);
  }
};

struct EarlyBacking : ZmNHeap<EarlyBacking>, Zi::IOBufPool {
friend inline ZuStringT<"Zutcp.Handshake.EarlyBacking"> ZmNHeapID(EarlyBacking *);
  using ZmNHeap<EarlyBacking>::operator new;
  using ZmNHeap<EarlyBacking>::operator delete;
  alignas(ZiIOBuf_Align) uint8_t bytes[512];
  unsigned acquired = 0, returned = 0;
  bool valid = true;
  EarlyBacking() { count = 1; size = sizeof(bytes); headRoom = 128; }
  uint8_t *alloc() override {
    if (acquired != returned) return nullptr;
    ++acquired;
    return bytes;
  }
  void free(uint8_t *p) override {
    valid &= p == bytes && acquired == returned + 1;
    ++returned;
  }
};

static void testEarlyWriterReadiness(unsigned scenario)
{
  ZuTestScopeRT(testEarlyWriterReadiness);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  ZmRef<EarlyBacking> backing = new EarlyBacking;
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP, ZiIOBuf, 512> allocator{
    app.mx(), backing};
  auto factory = Zxdp::instance(Zxdp::Type::Xdp);
  auto pool = factory->bufPool(1, sizeof(backing->bytes));
  app.testTx = &allocator; app.testPool = pool;
  ZmRef<GateCxn> link = new GateCxn{&app};
  Zutcp::TCPStateTest::stageEarly(*link);
  bool cold = !link->dataMPS();
  int accepted = Zutcp::TCPStateTest::earlyInput(*link);
  unsigned jobs = app.tx.count_();
  int duplicate = Zutcp::TCPStateTest::earlyInput(*link);
  if (scenario == 1 || scenario == 3) Zutcp::TCPStateTest::peerACK(*link);
  ZuCHECK(cold && accepted >= 0 && duplicate >= 0 && !link->deliveries &&
      link->reader().length() == 3 && !backing->acquired &&
      Zutcp::TCPStateTest::earlyPending(*link) && app.tx.count_() == jobs &&
      !link->connections,
    "early data waits in Reader with zero native writer capacity and one readiness checkpoint");
  app.runTx();
  ZuCHECK(link->dataMPS() && !link->deliveries && app.rx.count_() == 1,
    "route/options Tx work publishes writer capacity before early application delivery");
  if (scenario == 2) link->reset();
  if (scenario == 3) link->resetOnProcess = true;
  app.runRx(); app.runTx();
  if (scenario == 2) {
    ZuCHECK(!link->deliveries && !link->connections && !backing->acquired &&
	!link->reader().length(),
      "reset invalidates the old readiness completion without delivering retained data");
  } else if (scenario == 3) {
    ZuCHECK(link->deliveries == 1 && link->early && !link->connections && !backing->acquired,
      "an application reset during early delivery prevents establishment of the replacement");
  } else {
    Zutcp::TxRange range;
    bool queued = link->txRange(0, range);
    ZuCHECK(link->deliveries == 1 && link->early && link->reply &&
	link->connections == unsigned(scenario == 1) && backing->acquired == 1 &&
	queued && range.storage->native == &allocator && range.length == 1 &&
	range.storage->data[range.offset] == 'r',
      "early delivery writes a registered native reply before any raced connected notification");
  }
  link->reset(); app.drain(); link = nullptr; app.drain();
  ZuCheck(backing->valid && backing->returned == backing->acquired && !app.errors);
  app.testTx = nullptr; app.testPool = nullptr;
}

static void testRejectedOwnerDrain()
{
  ZuTestScope(testRejectedOwnerDrain);
  ZmRef<GateApp> appRef = new GateApp;
  auto &app = *appRef;
  unsigned destroyed = 0;
  auto factory = [&app, &destroyed]() -> ZmRef<GateCxn> {
    ZmRef<GateCxn> link = new GateCxn{&app};
    link->destroyed = &destroyed;
    link->originForTest(10);
    return link;
  };
  auto link = factory();
  bool conflict = !link->stageReuseForTest(11, 101);
  bool queued = app.tx.count_() >= 2;
  link->discardForTest();
  link = nullptr;
  ZuCHECK(conflict && queued && !destroyed,
    "a sole rejected factory reference must survive its queued raw owner work");
  app.runTx();
  ZuCHECK(!destroyed && !app.tx.count_(),
    "rejected owner must finish Tx work before its final Rx release");
  app.runRx();
  ZuCHECK(destroyed == 1 && !app.rx.count_() && !app.tx.count_() &&
      !app.longSent && !app.shortSent,
    "unadopted rejection must release once after owner work without protocol output");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testNativeZeroDisconnect);
  ZuTestCall(testNativeErrorDisconnect);
  ZuTestCall(testPendingErrorDisconnect);
  ZuTestCall(testNativeQueueError);
  ZuTestCall(testNativeBackingError);
  ZuTestCall(testNativeErrorReplacement);
  ZuTestCall(testNativeErrorPolicy);
  ZuTestCall(testTCPOriginOwners);
  ZuTestCall(testNativeIdleReset);
  ZuTestCall(testFinRequestReplacement);
  ZuTestCall(testPendingCloseCallbacks);
  ZuTestCall(testRetainedAdmissionFinWake);
  ZuTestCall(testPeerResetCloseSnapshot);
  ZuTestCall(testAbortBeforeReady);
  ZuTestCall(testNativeGracefulCallbacks);
  ZuTestCall(testAbortReentrantConnect);
  ZuTestCall(testCoalescedNativeConnect);
  ZuTestCall(testNativeSetupFailureDrain);
  ZuTestCall(testCloseRouteDrain);
  ZuTestCall(testRejectedOwnerDrain);
  ZuTestCall(testEarlyWriterReadiness, 0);
  ZuTestCall(testEarlyWriterReadiness, 1);
  ZuTestCall(testEarlyWriterReadiness, 2);
  ZuTestCall(testEarlyWriterReadiness, 3);
}
