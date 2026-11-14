//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"



using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


using TxBufAlloc = Zutcp::TxBufAlloc<>;

struct TestLink;
struct HubApp : public Zutcp::Hub<HubApp> {
  enum { Rx, Tx };
  using PendingQueue =
    ZmList<ZmFn<>, ZmListHeapID<"Zutcp.APITest.Pending">>;

  PendingQueue rxPendingQ;
  PendingQueue txPendingQ;
  unsigned shard = Rx;
  bool deferMode = false;

  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }

  bool rxInvoked() const { return !deferMode || shard == Rx; }
  bool txInvoked() const { return !deferMode || shard == Tx; }
  template <typename L> void rxRun(L l) {
    if (!deferMode) { l(); return; }
    rxPendingQ.push(ZmFn<>::Lambda::fn(
      [l = ZuMv(l)]() mutable { l(); }));
  }
  template <typename L> void rxInvoke(L l) { rxRun(ZuMv(l)); }
  template <typename L> void txRun(L l) {
    if (!deferMode) { l(); return; }
    txPendingQ.push(ZmFn<>::Lambda::fn(
      [l = ZuMv(l)]() mutable { l(); }));
  }
  template <typename L> void txInvoke(L l) { txRun(ZuMv(l)); }
  template <typename O, typename L> void txInvoke(O *, L l) {
    txRun(ZuMv(l));
  }

  void defer() {
    deferMode = true;
    shard = Rx;
  }
  void enterRx() { shard = Rx; }
  void enterTx() { shard = Tx; }
  bool rxPending() const { return rxPendingQ.count_(); }
  bool txPending() const { return txPendingQ.count_(); }
  void runRx() {
    assert(rxPendingQ.count_());
    shard = Rx;
    auto fn = rxPendingQ.shiftVal();
    fn();
  }
  void runTx() {
    assert(txPendingQ.count_());
    shard = Tx;
    auto fn = txPendingQ.shiftVal();
    fn();
  }

};
struct ClientApp : public Zutcp::Client<ClientApp> { };
struct ServerAppLink;
struct ServerApp : public Zutcp::Server<ServerApp, ServerAppLink> { };
struct ServerAppLink :
  public ZmNHeap<ServerAppLink>,
    public Zutcp::SrvCxn<
      ServerApp, ServerAppLink, TxBufAlloc> {
  friend inline ZuStringT<"Zutcp.API.SrvCxn"> ZmNHeapID(ServerAppLink *);
  using ZmNHeap<ServerAppLink>::operator new;
  using ZmNHeap<ServerAppLink>::operator delete;

  using Base = Zutcp::SrvCxn<
    ServerApp, ServerAppLink, TxBufAlloc>;

  ServerAppLink(ServerApp *app) : Base{app} { }

  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }
};

struct ClientShapeApp : public Zutcp::Client<ClientShapeApp> { };
struct ClientShapeLink;
struct ClientShapeLink :
    public ZmNHeap<ClientShapeLink>,
    public Zutcp::CliCxn<
      ClientShapeApp, ClientShapeLink,
      TxBufAlloc> {
friend inline ZuStringT<"Zutcp.APITest.CliCxn"> ZmNHeapID(ClientShapeLink *);
  using ZmNHeap<ClientShapeLink>::operator new;
  using ZmNHeap<ClientShapeLink>::operator delete;

  using Base = Zutcp::CliCxn<
    ClientShapeApp, ClientShapeLink,
    TxBufAlloc>;

  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> disconnects = 0;

  ClientShapeLink(ClientShapeApp *app) : Base{app} { }

  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }
  void connectFailed(bool) { ++failures; }
  void disconnected(bool) { ++disconnects; }
  using Base::diag;

};

struct ServerShapeLink;
struct ServerShapeApp :
    public Zutcp::Server<ServerShapeApp, ServerShapeLink> { };
struct ServerShapeLink :
    public ZmNHeap<ServerShapeLink>,
    public Zutcp::SrvCxn<
      ServerShapeApp, ServerShapeLink,
      TxBufAlloc> {
friend inline ZuStringT<"Zutcp.APITest.SrvCxn"> ZmNHeapID(ServerShapeLink *);
  using ZmNHeap<ServerShapeLink>::operator new;
  using ZmNHeap<ServerShapeLink>::operator delete;

  using Base = Zutcp::SrvCxn<
    ServerShapeApp, ServerShapeLink,
    TxBufAlloc>;

  ServerShapeLink(ServerShapeApp *app) : Base{app} { }

  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }

  void initSrvPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx(ZuMv(local), ZuMv(remote));
  }
#ifdef Zutcp_DEBUG


#endif

};

struct TestLink : public ZmNHeap<TestLink>,
  public Zutcp::Connection<HubApp, TestLink, TxBufAlloc> {
friend inline ZuStringT<"Zutcp.APITest.Link"> ZmNHeapID(TestLink *);
  using Base = Zutcp::Connection<HubApp, TestLink, TxBufAlloc>;
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;
  unsigned rxCalls = 0;

  TestLink(HubApp *app, bool server = false) : Base{app, server} { configLocalParams(app); }
  int process(Zutcp::Reader &) { ++rxCalls; return 0; }
  bool flushTx() { return false; }
  void queueTxFlush() { }
  void pto() { }
  void queueRetransmit() { }
  bool retransmit(bool = false, unsigned = 2) { return false; }
  void recoveryReset() { Base::resetTx_(); }
  void resetRuntime() { Base::resetLink(); }
  bool startHandshakeForTest() { return Base::startHandshake(); }
  bool handshakingRx() const { return Base::handshakeStartedRx() && !Base::establishedRx(); }
  bool handshakingTx() const { return Base::handshakeStartedTx() && !Base::establishedTx(); }
  bool closingRx() const { return Base::closingRx(); }
  bool closingTx() const { return Base::closingTx(); }
  bool linkClosedRx() const { return Base::linkClosedRx(); }
  bool linkClosedTx() const { return Base::linkClosedTx(); }
  uint64_t closeErrorRx() const { return Base::closeErrorRx(); }
  uint64_t closeErrorTx() const { return Base::closeErrorTx(); }
  void closeTxForTest(uint64_t error, bool application = false) {
    Base::closeStateTx(error, application);
  }
  void closeForTest(uint64_t error = 0) { Base::closeState(error); }
  void scheduleDataForTest() { Base::dataWritable(); }
  bool takeDataForTest() { return Base::takeTxReady(); }
  void clearDataForTest() { Base::clearWritableData(); }
  bool dataReadyForTest() const { return Base::txReady(); }
};
struct HubFixture {
  ZiMultiplex	mx;
  HubApp	app;
  Zi::IOBufAllocator *rx = nullptr;

  HubFixture() : mx{mxParams()} {
    rx = ZutcpTest::pktRxAllocator(&mx);
    ZiAssert(app.init(Zutcp::HubParams(&mx)),
      "Zutcp", (), "API test hub init failed", return);
    ZiAssert(mx.start(), "Zutcp", (),
      "API test multiplexer start failed", return);
  }
  ~HubFixture() {
    app.final();
    mx.stop();
  }

  static ZiMxParams mxParams() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

};

void testParams()
{
  ZuTestScope(testParams);
  auto params = Zutcp::ClientParams(nullptr)
    .maxData(1<<20).heartBeat(ZuTime{7}).maxPayload(1200).maxCxns(7).maxPendingCxns(3);
  ZuCHECK(params.maxData() == 1<<20 && params.maxPayload() == 1200 &&
      params.heartBeat() == ZuTime{7}, "transport settings mismatch");
  ZuCHECK(params.maxCxns() == 7 && params.maxPendingCxns() == 3 &&
      Zutcp::HubParams(nullptr).maxCxns() == 1024 &&
      Zutcp::HubParams(nullptr).maxPendingCxns() == 128,
    "connection admission must use the adopted instance limits and explicit overrides");
  ZuCHECK(params.connectTimeout() == ZuTime{30} &&
      params.connectTimeout(ZuTime{7}).connectTimeout() == ZuTime{7},
    "connect timeout must retain its adopted default and explicit override");
  ZuCHECK(params.tfoCookieCacheSize() == 1024 &&
      params.tfoCookieCacheSize(7).tfoCookieCacheSize() == 7,
    "Fast Open cache has a bounded default and explicit stopped size override");
  HubApp policy;
  ZuCHECK(!policy.earlyData(static_cast<TestLink *>(nullptr)) &&
      !policy.maxEarlyData(static_cast<TestLink *>(nullptr)) &&
      !policy.allowEarlyStream(static_cast<TestLink *>(nullptr), 0, true),
    "Fast Open application policy denies transmission and receive delivery by default");
  ZuCHECK(params.closedTupleRingSize() == 4096 &&
      params.closedTupleRingSize(7).closedTupleRingSize() == 7,
    "closed tuple history defaults and stopped size overrides must be retained");
  ZuCHECK(params.closeDrainTimeout() == ZuTime{30} && params.finWaitTimeout() == ZuTime{60} &&
      params.closeDrainTimeout(ZuTime{3}).closeDrainTimeout() == ZuTime{3} &&
      params.finWaitTimeout(ZuTime{6}).finWaitTimeout() == ZuTime{6},
    "close policy defaults and overrides must be retained");
  ZuCHECK(params.userTimeout() == ZuTime{120} &&
      params.userTimeout(ZuTime{0}).userTimeout() == ZuTime{0},
    "user timeout must retain its adopted default and explicit disabled value");
  auto zero = Zutcp::ClientParams(nullptr).heartBeat(ZuTime{0});
  ZuCHECK(!*zero.heartBeat(), "zero heartbeat must be disabled");
  bool invalid = false;
  try { (void)Zutcp::ClientParams(nullptr).heartBeat(ZuTime{-1}); }
  catch (const ZeException &) { invalid = true; }
  ZuCHECK(invalid, "negative heartbeat was accepted");
  }

void testConnectionShape()
{
  ZuTestScope(testConnectionShape);
  HubFixture fixture;
  ZmRef<TestLink> client = new TestLink{&fixture.app};
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};
  ZuCHECK(!client->isServer() && server->isServer(), "connection role mismatch");
  { auto writer = client->writer_(); writer << "abc" << Zi::flush(); }
  ZuCHECK(client->txBytes() == 3, "connection writer byte accounting mismatch");
  client->fin();
  client->reset(7);
  ZuCHECK(client->finSent() && client->resetSent() && client->txAppError() == 7,
    "connection direction state was not recorded");
  client->closeForTest(42);
  ZuCHECK(client->closed() && client->closeError() == 42, "connection close mismatch");
}

void testDataReadiness()
{
  ZuTestScope(testDataReadiness);
  HubFixture fixture;
  ZmRef<TestLink> cxn = new TestLink{&fixture.app};
  int refs = cxn->refCount();
  cxn->scheduleDataForTest();
  cxn->scheduleDataForTest();
  ZuCHECK(cxn->dataReadyForTest() && cxn->refCount() == refs,
    "repeated readiness must not add objects or ownership");
  ZuCHECK(cxn->takeDataForTest() && !cxn->takeDataForTest(),
    "readiness must be consumed once");
  cxn->scheduleDataForTest();
  cxn->clearDataForTest();
  ZuCHECK(!cxn->dataReadyForTest() && cxn->refCount() == refs,
    "cancellation must clear readiness without ownership changes");
}

void testAlignedSurfaceShape()
{
  ZuTestScope(testAlignedSurfaceShape);

  ZiMultiplex mx{HubFixture::mxParams()};
  ClientShapeApp clientApp;
  bool clientOK = clientApp.init(
    Zutcp::ClientParams(&mx));
  ZuCHECK(clientOK, "aligned client app init failed");
  if (!clientOK) {
    mx.stop();
    return;
  }
  ServerShapeApp serverApp;
  bool serverOK = serverApp.init(
    Zutcp::ServerParams(&mx)
      
      
      );
  ZuCHECK(serverOK, "aligned server app init failed");
  if (!serverOK) {
    clientApp.final();
    mx.stop();
    return;
  }
  bool mxOK = mx.start();
  ZuCHECK(mxOK, "aligned shape multiplexer start failed");
  if (!mxOK) return;

  ZmRef<ClientShapeLink> client = new ClientShapeLink{&clientApp};
  ZuCHECK(!client->isServer(), "client connection role mismatch");
  ZmRef<ServerShapeLink> server = new ServerShapeLink{&serverApp};
  ZuCHECK(server->isServer(), "server connection role mismatch");

  in_addr localIP;
  localIP.s_addr = htonl(0x0a000001);
  in_addr remoteIP;
  remoteIP.s_addr = htonl(0x0a000002);
  ZiSockAddr localAddr{ZiIP{localIP}, 4433};
  ZiSockAddr remoteAddr{ZiIP{remoteIP}, 50000};
  ZmBlock<>{}([&serverApp, &server, &localAddr, &remoteAddr](auto wake) {
    serverApp.txRun([
      server, localAddr, remoteAddr, wake = ZuMv(wake)
    ]() mutable {
      server->initSrvPath(localAddr, remoteAddr);
      wake();
    });
  });
  mx.stop();
  server = nullptr;
  client = nullptr;
  serverApp.final();
  clientApp.final();
}

void testLinkStateOwnership()
{
  ZuTestScope(testLinkStateOwnership);

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    ZuCHECK(link->startHandshakeForTest() && link->handshakingRx() &&
	fixture.app.txPending(),
      "Rx handshake transition was not published to Tx");
    fixture.app.enterTx();
    ZuCHECK(!link->handshakingTx(),
      "Tx observed handshake state before applying its snapshot");
    fixture.app.runTx();
    ZuCHECK(link->handshakingTx() && !fixture.app.txPending(),
      "Tx did not apply the Rx handshake snapshot");
    fixture.app.enterRx();
    link->closeForTest(0);
    while (fixture.app.txPending()) fixture.app.runTx();
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    link->closeForTest(17);
    ZuCHECK(link->closingRx() && link->linkClosedRx() &&
	link->closeErrorRx() == 17 && link->closed() &&
	link->closeError() == 17 && fixture.app.txPending(),
      "Rx close transition did not update canonical state");
    fixture.app.enterTx();
    ZuCHECK(!link->closingTx() && !link->linkClosedTx(),
      "Tx observed Rx close state before applying its snapshot");
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 17,
      "Tx did not apply the Rx close snapshot");
    while (fixture.app.txPending()) fixture.app.runTx();
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    ZuCHECK(link->startHandshakeForTest() && fixture.app.txPending(),
      "stale-snapshot close setup failed");
    fixture.app.enterTx();
    link->closeTxForTest(23, true);
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 23 && fixture.app.rxPending(),
      "Tx close transition did not update Tx-owned state");
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->linkClosedTx() &&
	link->closeErrorTx() == 23,
      "stale Rx snapshot regressed the Tx close state");
    fixture.app.runRx();
    ZuCHECK(link->closingRx() && link->linkClosedRx() &&
	link->closeErrorRx() == 23 && link->closed() &&
	link->closeError() == 23 && !fixture.app.rxPending() &&
	!fixture.app.txPending(),
      "Rx did not apply the Tx close snapshot exactly once");
  }

  {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    fixture.app.defer();
    fixture.app.enterRx();

    link->closeForTest(31);
    fixture.app.enterTx();
    link->closeTxForTest(37);
    fixture.app.runTx();
    ZuCHECK(link->closingTx() && link->closeErrorTx() == 31,
      "canonical Rx close did not supersede a racing Tx close");
    while (fixture.app.txPending()) fixture.app.runTx();
    fixture.app.runRx();
    ZuCHECK(link->closingRx() && link->closeErrorRx() == 31 &&
	link->closeError() == 31 && !fixture.app.rxPending(),
      "racing Tx close overwrote the canonical Rx close");
  }
}

void testDeferredByteReset()
{
  ZuTestScope(testDeferredByteReset);
  HubFixture fixture;
  ZmRef<TestLink> cxn = new TestLink{&fixture.app};
  auto alloc = [&fixture]() {
    auto buf = fixture.app.dataTxAllocator()->allocBuf();
    memcpy(buf->data_(), "bytes", 5);
    buf->length = 5;
    return buf;
  };
  auto installed = alloc(), pending = alloc(), fresh = alloc();
  ZuCHECK(cxn->enqueue_(installed) && cxn->txQueueCount() == 1,
    "reset setup must retain one installed producer range");
  fixture.app.defer();
  fixture.app.enterRx();
  ZuCHECK(cxn->enqueue(pending) && cxn->txQueueCount() == 2 &&
    fixture.app.txPending(), "off-owner admission must reserve queue credit");
  auto oldWriter = cxn->writer();
  oldWriter << "stale";
  fixture.app.enterTx();
  cxn->recoveryReset();
  ZuCHECK(!cxn->txRangeCount() && !cxn->txBytes() && cxn->txQueueCount() == 1,
    "reset must retire installed queue credit while preserving pending credit");
  ZuCHECK(cxn->enqueue_(fresh) && cxn->txRangeCount() == 1 &&
    cxn->txBytes() == 5 && cxn->txQueueCount() == 2,
    "new generation must admit new backing before old work dispatches");
  fixture.app.runTx();
  ZuCHECK(cxn->txRangeCount() == 1 && cxn->txBytes() == 5 &&
    cxn->txQueueCount() == 1,
    "stale admission must retire once without entering the new connection");
  // The deferred fixture also queues reset's independent timer cancellation.
  while (fixture.app.txPending()) fixture.app.runTx();
  Zutcp::TxRange range;
  Zutcp::PktBudget budget;
  bool fin = false;
  ZuCHECK(cxn->nextTxRange(budget, range, fin) && range.storage->pending == fresh &&
    !range.dataOffset && range.length == 5,
    "old dispatch must preserve the new generation's original backing");
  fixture.app.enterRx();
  ZuCHECK(!oldWriter.flush() && !fixture.app.txPending(),
    "a writer held across reset must reject rather than post stale bytes");
  fixture.app.enterTx();
  cxn->recoveryReset();
  ZuCHECK(!cxn->txQueueCount() && !cxn->txBytes() && !cxn->txRangeCount(),
    "repeated reset must drain without queue credit underflow");

  fixture.app.enterRx();
  cxn->resetRuntime();
  auto gen = cxn->dataGen();
  ZuCHECK(cxn->enqueue(fresh), "new admission must post behind its Tx reset");
  while (fixture.app.txPending()) fixture.app.runTx();
  ZuCHECK(cxn->dataGen() == gen && cxn->txRangeCount() == 1 &&
    cxn->txQueueCount() == 1 && cxn->txBytes() == 5,
    "Tx reset must install the published generation without discarding new work");
  cxn->recoveryReset();
}

void testInitValidate()
{
  ZuTestScope(testInitValidate);

  ZiLog::init("ZutcpAPITest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  {
    unsigned errors = 0;
    HubApp hub;
    ZuCHECK(!hub.init(
	Zutcp::HubParams(nullptr)
	  .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "null multiplexer init unexpectedly succeeded");
    ZuCHECK(errors == 1, "null multiplexer error callback mismatch");
  }


  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  {
    HubApp app;
    bool ok = app.init(Zutcp::HubParams(&mx).maxCxns(7).maxPendingCxns(3));
    ZuCHECK(ok && app.rxThread() == mx.rxThread() && app.txThread() == mx.txThread(),
      "transport owners must be the multiplexer I/O owners");
    ZuCHECK(ok && app.maxCxns() == 7 && app.maxPendingCxns() == 3,
      "initialized application must retain its connection limits");
    if (ok) app.final();
  }
  for (unsigned i = 0; i < 3; ++i) {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx).maxCxns(i ? 2 : 0)
      .maxPendingCxns(i == 1 ? 0 : 3)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero limits and a pending cap larger than the total cap must fail before initialization");
  }
  {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx).closedTupleRingSize(0)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero closed tuple capacity must fail before initialization");
  }
  {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx).tfoCookieCacheSize(0)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero Fast Open cookie capacity must fail before initialization");
  }
  for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx).connectTimeout(timeout)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset, zero and negative connect deadlines must fail before initialization");
  }
  for (bool finWait : {false, true}) for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    if (finWait) params.finWaitTimeout(timeout);
    else params.closeDrainTimeout(timeout);
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset, zero and negative close deadlines must fail before initialization");
  }
  for (auto timeout : {ZuTime{}, ZuTime{-1}}) {
    HubApp app;
    unsigned errors = 0;
    auto params = Zutcp::HubParams(&mx).userTimeout(timeout)
      .errorFn(Zutcp::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset and negative user deadlines must fail before initialization");
  }
  {
    HubApp app;
    bool ok = app.init(Zutcp::HubParams(&mx).userTimeout(ZuTime{0}));
    ZuCHECK(ok && app.userTimeout() == ZuTime{0},
      "disabled user policy must survive initialization");
    if (ok) app.final();
  }
  {
    ClientApp app;
    bool ok = app.init(
      Zutcp::ClientParams(&mx));
    ZuCHECK(ok, "valid client init failed");
    if (ok) app.final();
  }
  {
    ServerApp app;
    bool ok = app.init(
      Zutcp::ServerParams(&mx)
	
	
	);
    ZuCHECK(ok, "valid server default-thread init failed");
    if (ok) app.final();
  }

  ZiLog::stop();
}

void testApplicationByteSurface()
{
  ZuTestScope(testApplicationByteSurface);
  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  auto buf = fixture.rx->allocBuf(nullptr, 3);
  memcpy(buf->data(), "abc", 3);
  buf->length = 3;
  Zutcp::ByteData data{.offset = 0, .length = 3, .payload = buf->cspan()};
  ZuCHECK(link->processFrame(data, buf) == 0 && link->rxCalls == 1 &&
      link->reader().length() == 3 && link->rxAdmitted() == 3,
    "application byte delivery retains its original packet-backed input");
  auto consumed = link->reader().consume(
    [](auto part) -> int64_t { return part.length(); }, [](auto) { });
  ZuCHECK(consumed == 3 && link->retireRx(3), "consumed bytes retire through the public API");
  Zutcp::ByteData fin{.offset = 3, .fin = true};
  ZuCHECK(link->processFrame(fin, {}) == 0 && link->rxCalls == 2 && link->rxComplete(),
    "a payload-free FIN reports EOF independently of application bytes");
  link->closeRx();
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParams);
  ZuTestCall(testConnectionShape);
  ZuTestCall(testDataReadiness);
  ZuTestCall(testAlignedSurfaceShape);
  ZuTestCall(testLinkStateOwnership);
  ZuTestCall(testDeferredByteReset);
  ZuTestCall(testInitValidate);
  ZuTestCall(testApplicationByteSurface);
}
