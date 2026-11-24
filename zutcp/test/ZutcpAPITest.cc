//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <unistd.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmHashMgr.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtcHub.hh>

#include <zlib/Zutcp.hh>

#include "ZutcpTestAlloc.hh"
#include "ZutcpTestMxPlugin.hh"

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))


using TxBufAlloc = Zutcp::TxBufAlloc<>;

struct TestLink;
struct HubApp : public ZmNHeap<HubApp>, public ZutcpTest::MxPlugin {
friend inline ZuStringT<"Zutcp.APITest.HubApp"> ZmNHeapID(HubApp *);
  using ZmNHeap<HubApp>::operator new;
  using ZmNHeap<HubApp>::operator delete;
  enum { Rx, Tx };
  using PendingQueue =
    ZmList<ZmFn<>, ZmListHeapID<"Zutcp.APITest.Pending">>;

  PendingQueue rxPendingQ;
  PendingQueue txPendingQ;
  unsigned shard = Rx;
  bool deferMode = false;

  explicit HubApp(ZiMultiplex *mx) : ZutcpTest::MxPlugin{mx} { }

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
struct TestLink : public ZmNHeap<TestLink>,
  public Zutcp::Cxn_<HubApp, TestLink, TxBufAlloc> {
friend inline ZuStringT<"Zutcp.APITest.Link"> ZmNHeapID(TestLink *);
  using Base = Zutcp::Cxn_<HubApp, TestLink, TxBufAlloc>;
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;
  unsigned rxCalls = 0;

  TestLink(HubApp *app, bool server = false) : Base{app, server} { configLocalParams(app); Base::resetPath(); }
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
  ZmRef<HubApp> appRef = new HubApp{&mx};
  HubApp &app = *appRef;
  Zi::IOBufAllocator *rx = nullptr;

  HubFixture() : mx{mxParams()} {
    rx = ZutcpTest::pktRxAllocator(&mx);
    ZiAssert(app.init(ZutcpTest::Params(&mx)),
      "Zutcp", (), "API test hub init failed", return);
    ZiAssert(mx.start(), "Zutcp", (),
      "API test multiplexer start failed", return);
  }
  ~HubFixture() {
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
  auto params = ZutcpTest::Params(nullptr)
    .maxData(1<<20).heartBeat(ZuTime{7}).maxPayload(1200).maxCxns(7).maxPendingCxns(3);
  ZuCHECK(params.maxData() == 1<<20 && params.maxPayload() == 1200 &&
      params.heartBeat() == ZuTime{7}, "transport settings mismatch");
  ZuCHECK(params.maxCxns() == 7 && params.maxPendingCxns() == 3 &&
      ZutcpTest::Params(nullptr).maxCxns() == 1024 &&
      ZutcpTest::Params(nullptr).maxPendingCxns() == 128,
    "connection admission must use the adopted instance limits and explicit overrides");
  ZuCHECK(params.connectTimeout() == ZuTime{30} &&
      params.connectTimeout(ZuTime{7}).connectTimeout() == ZuTime{7},
    "connect timeout must retain its adopted default and explicit override");
  ZuCHECK(params.tfoCookieCacheSize() == 1024 &&
      params.tfoCookieCacheSize(7).tfoCookieCacheSize() == 7,
    "Fast Open cache has a bounded default and explicit stopped size override");
  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  ZuCHECK(!link->earlyData() && !link->maxEarlyData() &&
      !link->allowEarlyStream(0, true),
    "private connection policy denies early transmit and receive by default");
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
  auto zero = ZutcpTest::Params(nullptr).heartBeat(ZuTime{0});
  ZuCHECK(zero.heartBeat() == ZuTime{0}, "zero heartbeat must be disabled");
  ZiMultiplex policyMx{HubFixture::mxParams()};
  ZutcpTest::MxPlugin policy{&policyMx};
  Zutcp::MxParams invalid;
  invalid.heartBeat = ZuTime{-1};
  bool rejected = false;
  try { ZutcpTest::MxPlugin::apply(policy, invalid); }
  catch (ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    rejected = true;
  }
  ZuCHECK(rejected,
    "negative heartbeat was accepted");
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

void testPluginShape()
{
  ZuTestScope(testPluginShape);
  ZuCHECK((ZuIsBase<Zutcp::MxPlugin, ZiMxPlugin>{}),
    "MxPlugin implements the existing multiplexer plugin interface");
  ZuCHECK((ZuIsBase<Zutcp::MxPlugin, Ztc::Mx>{}),
    "MxPlugin exposes multiplexer telemetry through ZiMxPlugin");
  ZuCHECK((!ZuIsBase<Zutcp::MxPlugin, Ztc::Hub>{}),
    "MxPlugin has no separate hub telemetry or lifecycle owner");
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
    memcpy(buf->data(), "bytes", 5);
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
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    bool ok = app.init(ZutcpTest::Params(&mx).maxCxns(7).maxPendingCxns(3));
    ZuCHECK(ok && app.rxThread() == mx.rxThread() && app.txThread() == mx.txThread(),
      "transport owners must be the multiplexer I/O owners");
    ZuCHECK(ok && app.maxCxns() == 7 && app.maxPendingCxns() == 3,
      "initialized application must retain its connection limits");
    auto invalid = ZutcpTest::MxPlugin::parameters(app);
    invalid.maxCxns = 0;
    bool rejected = false;
    try { ZutcpTest::MxPlugin::apply(app, ZuMv(invalid)); }
    catch (ZeException &e) {
      ZiLogEvent(ZuMv(e));
      rejected = true;
    }
    ZuCHECK(rejected &&
        app.maxCxns() == 7 && app.maxPendingCxns() == 3,
      "invalid stopped initialization preserves the last accepted policy");
  }
  for (unsigned i = 0; i < 3; ++i) {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx).maxCxns(i ? 2 : 0)
      .maxPendingCxns(i == 1 ? 0 : 3)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero limits and a pending cap larger than the total cap must fail before initialization");
  }
  {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx).closedTupleRingSize(0)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero closed tuple capacity must fail before initialization");
  }
  {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx).tfoCookieCacheSize(0)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "zero Fast Open cookie capacity must fail before initialization");
  }
  for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx).connectTimeout(timeout)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset, zero and negative connect deadlines must fail before initialization");
  }
  for (bool finWait : {false, true}) for (auto timeout : {ZuTime{}, ZuTime{0}, ZuTime{-1}}) {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    if (finWait) params.finWaitTimeout(timeout);
    else params.closeDrainTimeout(timeout);
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset, zero and negative close deadlines must fail before initialization");
  }
  for (auto timeout : {ZuTime{}, ZuTime{-1}}) {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    unsigned errors = 0;
    auto params = ZutcpTest::Params(&mx).userTimeout(timeout)
      .errorFn(ZutcpTest::Params::ErrorFn{[&errors](ZeException) { ++errors; }});
    ZuCHECK(!app.init(ZuMv(params)) && errors == 1,
      "unset and negative user deadlines must fail before initialization");
  }
  {
    ZmRef<HubApp> appRef = new HubApp{&mx};
    auto &app = *appRef;
    bool ok = app.init(ZutcpTest::Params(&mx).userTimeout(ZuTime{0}));
    ZuCHECK(ok && app.userTimeout() == ZuTime{0},
      "disabled user policy must survive initialization");
  }
  ZiLog::stop();
}

void testCongestionControl()
{
  ZuTestScope(testCongestionControl);
  ZiMultiplex mx{HubFixture::mxParams()};
  ZuCHECK(ZutcpTest::Params{}.congestionControl() && Zutcp::MxParams{}.congestionControl,
    "congestion control must remain enabled by default for both configuration paths");
  for (bool enabled : {true, false}) for (bool ecn : {true, false}) {
    ZmRef<HubApp> app = new HubApp{&mx};
    auto params = ZutcpTest::Params{&mx}.congestionControl(enabled).ecn(ecn);
    bool ok = app->init(params);
    ZuCHECK(ok && params.congestionControl() == enabled && params.ecn() == ecn &&
	app->congestionControl() == enabled && app->ecn() == (ecn && enabled),
      "MxPlugin must retain congestion control and suppress ECN when it is disabled");
    params.congestionControl(!enabled).ecn(!ecn);
    ZuCHECK(!app->init(params) && app->congestionControl() == enabled &&
        app->ecn() == (ecn && enabled),
      "conflicting stopped configuration must preserve the initialized policy");
  }
}

// The membership hash cannot allocate its inner node: retain the concrete heap.
struct HashMember : public ZmNHeap<HashMember>, public Zutcp_::ServerLinks::Node {
friend inline ZuStringT<"Zutcp.APITest.HashMember"> ZmNHeapID(HashMember *);
  void abortRx() override { }
  Zutcp::ClosedTupleKey closedKeyRx() const override { return {}; }
  bool established() const override { return false; }
  using ZmNHeap<HashMember>::operator new;
  using ZmNHeap<HashMember>::operator delete;
};

void testHashSizing(bool tuned)
{
  ZuTestScopeRT(testHashSizing);
  static constexpr const char *ids[] = {
    "Zutcp.Endpoints", "Zutcp.Endpoint.TupleRouter", "Zutcp.ServerLinks"};
  ZmHashParams saved[3];
  for (unsigned i = 0; i < 3; ++i) {
    saved[i] = ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init(ids[i]);
    if (tuned) ZmHashMgr::init(ids[i], ZmHashParams{}.bits(8).loadFactor(1).cBits(2));
  }
  {
    Zutcp::EndpointIndex::Node endpointNodes[128];
    ZiMultiplex mx{ZiMxParams{}.rxBufSize(8)};
    // The supplied-pool fixture embeds the real MxPlugin endpoint index.
    ZmRef<ZutcpTest::MxPlugin> stack = new ZutcpTest::MxPlugin{&mx};
    Zutcp_::TupleRouter<HashMember> router;
    ZmRef<Zutcp_::ServerLinkTable> membership = new Zutcp_::ServerLinkTable{
      ZmHashParams{}.bits(5).loadFactor(1).cBits(3).init("Zutcp.ServerLinks")};
    using Routes = Zutcp_::TupleRoutes<HashMember>;
    Zutcp::EndpointIndex *endpoints = nullptr;
    Routes *routes = nullptr;
    Zutcp_::ServerLinks *links = nullptr;
    // capture() also visits embedded tables. Their containing owners stay alive
    // for this entire scope; never take an independent reference to a member.
    Ztc::HashMgr::capture([&endpoints, &routes, &links](Ztc::Hash *table) {
      auto id = table->telKey().p<0>();
      if (id == ids[0]) endpoints = dynamic_cast<Zutcp::EndpointIndex *>(table);
      else if (id == ids[1]) routes = dynamic_cast<Routes *>(table);
      else if (id == ids[2]) links = dynamic_cast<Zutcp_::ServerLinks *>(table);
      return false;
    }, [](ZuSpan<const Ztc::HashTelemetry>) { });
    bool found = endpoints && routes && links;
    auto geometry = [tuned](auto &table, unsigned count) {
      Ztc::HashTelemetry data;
      table->telemetry(data);
      // These owner-local tables use ZmNoLock; configured cBits add no stripes.
      return data.bits == (tuned ? 8 : 5) && !data.cBits &&
	data.loadFactor == 1 && data.count == count && !data.resized;
    };
    ZuCHECK(found && geometry(endpoints, 0) && geometry(routes, 0) && geometry(links, 0),
      "private endpoint, routing and membership constructors honor named hash settings");
    if (found && tuned) {
      for (unsigned i = 0; i < 128; ++i) {
	ZiSockAddr local{ZiIP{"198.18.0.1"}, uint16_t(20000 + i)};
	ZiSockAddr peer{ZiIP{"198.18.0.2"}, uint16_t(30000 + i)};
	endpointNodes[i].bindAddr = local;
	endpoints->addNode(&endpointNodes[i]);
	routes->add(Zutcp_::TupleRoute<HashMember>{{local, peer}, nullptr});
	links->addNode(new HashMember);
      }
      Ztc::HashTelemetry data;
      endpoints->telemetry(data);
      ZuCHECK(data.shadow && geometry(endpoints, 128) && geometry(routes, 128) &&
	  geometry(links, 128),
	"128 embedded endpoint, tuple and membership nodes require no hash resize");
    }
    if (endpoints) endpoints->clean();
    if (routes) routes->clean();
    if (links) links->clean();
  }
  // HashMgr has no removal API; restore the previously effective settings.
  if (tuned)
    for (unsigned i = 0; i < 3; ++i) ZmHashMgr::init(ids[i], saved[i]);
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
  ZuTestCall(testPluginShape);
  ZuTestCall(testLinkStateOwnership);
  ZuTestCall(testDeferredByteReset);
  ZuTestCall(testInitValidate);
  ZuTestCall(testCongestionControl);
  ZuTestCall(testApplicationByteSurface);
  ZuTestCall(testHashSizing, false);
  ZuTestCall(testHashSizing, true);
}
