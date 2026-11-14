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
#include <zlib/ZfJSON.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"


#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

using TxBufAlloc = Zutcp::TxBufAlloc<>;

#ifdef Zutcp_DEBUG
static Zi::Path g_residue;

static Zi::Path testPath(ZuCSpan name)
{
  return ZiFile::append(g_residue, name);
}

static ZtString<> readFile_(const Zi::Path &path)
{
  ZtString<> data;
  ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC};
  if (!file) return data;
  auto size = file.size();
  if (size <= 0 || size > (1<<20)) return data;
  data.length(unsigned(size));
  int n = file.read(data.data(), data.length());
  if (n <= 0)
    data.length(0);
  else
    data.length(unsigned(n));
  return data;
}

static unsigned parseJSONSeq(ZuCSpan data)
{
  unsigned n = 0;
  unsigned i = 0, l = data.length();
  while (i < l) {
    ZuCSpan rest{data.data() + i, l - i};
    if (!rest.match<"\x1e">()) return 0;
    ++i;
    unsigned start = i;
    while (i < l && data[i] != '\n') ++i;
    if (i >= l) return 0;
    ZtString<> json;
    json << ZuCSpan{data.data() + start, i - start};
    auto scan = ZfJSON::scan(json);
    if (scan.p<0>() != int(json.length())) return 0;
    ++n;
    ++i;
  }
  return n;
}

static void closeQLog(ZutcpLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZutcpLogger::close(trace, ZuMv(wake));
  });
}
#endif

struct TestLink;
struct HubApp : public Zutcp::Hub<HubApp> {
  Zi::IOBufAllocator *dataTxAllocator() const { return Zutcp::inlineAllocator(); }
  enum Shard { Rx, Tx };
  using PendingQueue =
    ZmList<ZmFn<>, ZmListHeapID<"Zutcp.APITest.Pending">>;




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

  PendingQueue rxPendingQ;
  PendingQueue txPendingQ;
  Shard shard = Rx;
  bool deferMode = false;
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

  ClientShapeLink(ClientShapeApp *app) : Base{app} { }

  int process(Zutcp::Reader &) { return 0; }
  void connected(Zutcp::Connected) { }
  void connectFailed(bool) { ++failures; }
  void disconnected(bool) { ++disconnects; }
  using Base::diag;

  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> disconnects = 0;
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
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
#ifdef Zutcp_DEBUG


#endif

};

struct TestLink :
    public ZmNHeap<TestLink>,
    public Zutcp::Connection<
      HubApp, TestLink, TxBufAlloc> {
friend inline ZuStringT<"Zutcp.APITest.Link"> ZmNHeapID(TestLink *);
  using ZmNHeap<TestLink>::operator new;
  using ZmNHeap<TestLink>::operator delete;

  using Base = Zutcp::Connection<
    HubApp, TestLink, TxBufAlloc>;
  TestLink(HubApp *app, bool isServer = false) : Base{app, isServer} {
    Base::configLocalParams(app);
  }

  static constexpr unsigned RecBatch = Base::RecoveryScanBatch;

  bool recoveryPackets(unsigned count, bool ecn = false) {
    if (ecn) Base::setEcnDisabled_(false);
    Base::setTxPN(Zutcp::PktNumSpace::Initial, count + 1);
    for (unsigned pn = 1; pn <= count; ++pn)
      if (!Base::recordTxPkt(Zutcp::PktNumSpace::Initial, pn, 1200,
	  Zutcp::SentFrameRef{}, true, false, 0, Zutcp::PktNumSpace::N, 0,
	  Zutcp::PktType::Initial,
	  ecn ? Zutcp::EcnMark::ECT0 : Zutcp::EcnMark::NotECT))
	return false;
    return true;
  }
  void recoveryAck(unsigned largest, unsigned first, bool ecn = false) {
    Base::AckSnapshot ack;
    ack.level = Zutcp::PktNumSpace::Initial;
    ack.ranges.push(Zutcp::AckRange{largest, first});
    if (ecn) {
      ack.ecn.ect0 = largest - first;
      ack.ecn.ce = 1;
    }
    Base::processAckFrameTx_(ack);
  }
  auto recoveryDiag() {
    // Continuation assertions inspect the controller after partial retirement,
    // before production publishes its end-of-ACK diagnostic update.
    Base::updateCongestionDiag();
    return Base::txDiag_();
  }
  auto recoveryPathDiag() const { return Base::pathDiag_(); }
  void recoveryStop() { Base::cancelTimers_(); }
  void recoveryReset() { Base::resetTx_(); }
  void resetRuntime() { Base::resetLink_(); }
  void recoveryDiscard() {
    Base::discardPktNumSpace_1(Zutcp::PktNumSpace::Initial);
  }



  const Zutcp::TransportParams &localParams() const {
    return Base::localTransportParams();
  }

  int process(Zutcp::Reader &) { ++rxCalls; return 0; }
  void rxReset(uint64_t appError, uint64_t finalSize) {
    lastResetAppError = appError;
    lastResetFinalSize = finalSize;
    ++resetReceivedCount;
  }
  void txReset(uint64_t appError, uint64_t finalSize) {
    lastResetSentAppError = appError;
    lastResetSentFinalSize = finalSize;
    ++resetSentCount;
  }
  void txStopped(uint64_t appError) {
    lastStopSentAppError = appError;
    ++stopSentCount;
  }
  void txComplete(bool) { ++txCompleteCount; }
  void flowBlocked(uint64_t maximum) {
    lastFlowMaximum = maximum;
    ++flowBlockedCount;
  }
  void transportClose(Zutcp::FrameType::T type, uint64_t errorCode) {
    lastCloseType = type;
    lastCloseError = errorCode;
    ++transportCloseCount;
  }
  void pto_() { }
  void queueRetransmit() { }
  bool retransmit_(bool = false, unsigned = 2,
    Zutcp::PktNumSpace::T = Zutcp::PktNumSpace::N) { return false; }






	  void setRuntimeCIDs(const Zutcp::CxnID &local, const Zutcp::CxnID &peer) {
	    Base::setRuntimeCIDs_(Zutcp::CxnID{"initcid0"}, local, peer);
	  }

	  void setPeerTransportParams(const Zutcp::TransportParams &params) {
	    Base::peerParams(params);
	  }










  Zutcp::CxnID activeTxPeerCID() const {
    return Base::cid_(Base::CIDSel::Peer);
  }



	  bool draining() const { return Base::drainingRx(); }
	  bool startHandshakeForTest() { return Base::startHandshake(); }
	  bool handshakingRx() const {
	    return Base::handshakeStartedRx() && !Base::establishedRx();
	  }
	  bool handshakingTx() const {
	    return Base::handshakeStartedTx() && !Base::establishedTx();
	  }
	  bool closingRx() const { return Base::closingRx_(); }
	  bool closingTx() const { return Base::closingTx_(); }
	  bool linkClosedRx() const { return Base::linkClosedRx_(); }
	  bool linkClosedTx() const { return Base::linkClosedTx_(); }
	  uint64_t closeErrorRx() const { return Base::closeErrorRx_(); }
	  uint64_t closeErrorTx() const { return Base::closeErrorTx_(); }
	  void closeTxForTest(uint64_t errorCode, bool application = false) {
	    Base::closeStateTx(errorCode, application);
	  }
  void scheduleDataForTest() { Base::dataWritable(); }
  bool takeDataForTest() { return Base::takeTxReady(); }
  void clearDataForTest() { Base::clearWritableData(); }
  bool dataReadyForTest() const { return Base::txReady(); }
  bool flushControlSends() {
    return Base::flushData(
      ZiSockAddr{},
      [](Zutcp::PktBuild &) { return true; },
      [](Zutcp::PktBuild &, ZiSockAddr, const Base::TxPktRefs &) {
	return true;
      });
  }
  void dataBlockedForTest(uint64_t maximum) {
    Base::dataBlocked(maximum);
  }
	  void closeTransport(Zutcp::FrameType::T type, uint64_t errorCode) {
	    Base::closeLink(errorCode);
	    Base::transportClose_(type, errorCode);
	  }
	  void closeForTest(uint64_t errorCode = 0) {
	    Base::closeState(errorCode);
	  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
  void initClientPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx_(ZuMv(local), ZuMv(remote));
  }







  void flushTx_() { ++txFlushQueued; }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush() { ++txFlushQueued; }
  void queueTxFlush(ZiSockAddr) { ++txFlushQueued; }
  Zutcp::LinkDiag diag() const { return Base::diag_(); }

  uint64_t lastResetAppError = 0;
  uint64_t lastResetFinalSize = 0;
  uint64_t lastResetSentAppError = 0;
  uint64_t lastResetSentFinalSize = 0;
  uint64_t lastStopSentAppError = 0;
  uint64_t lastFlowMaximum = 0;
  uint64_t lastCloseError = 0;
  Zutcp::FrameType::T lastFlowType = Zutcp::FrameType::Unknown;
  Zutcp::FrameType::T lastCloseType = Zutcp::FrameType::Unknown;
  unsigned resetReceivedCount = 0;
  unsigned resetSentCount = 0;
  unsigned stopSentCount = 0;
  unsigned flowBlockedCount = 0;
  unsigned transportCloseCount = 0;
  unsigned txFlushQueued = 0;
  unsigned rxCalls = 0, txCompleteCount = 0;
	};

struct HubFixture {
  HubFixture() : mx{mxParams_()} {
    ZiAssert(app.init(Zutcp::HubParams(&mx)),
      "Zutcp", (), "API test hub init failed", return);
    ZiAssert(mx.start(), "Zutcp", (),
      "API test multiplexer start failed", return);
  }
  ~HubFixture() {
    app.final();
    mx.stop();
  }

  static ZiMxParams mxParams_() {
    return ZiMxParams()
      .scheduler([](auto &s) {
	s.nThreads(4);
      })
      .rxThread(1).txThread(2);
  }

  ZiMultiplex	mx;
  HubApp	app;
};

static bool parseFrame(const uint8_t *b, int n, Zutcp::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> dataPkt(
  uint64_t offset, ZuBSpan payload, bool fin,
  Zutcp::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  int n = Zutcp::FrameCodec::writeData(
    packet->data_(), packet->size, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zutcp::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

} // namespace



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
  client->stop(9);
  ZuCHECK(client->finSent() && client->resetSent() && client->stopSent(),
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

  ZiMultiplex mx{HubFixture::mxParams_()};
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

  cxn->stop(11);
  fixture.app.enterRx();
  cxn->resetRuntime();
  ZuCHECK(fixture.app.rxPending(), "STOP must have queued its Rx handoff");
  while (fixture.app.rxPending()) fixture.app.runRx();
  ZuCHECK(cxn->rxError() == Zutcp::DataError::None && !cxn->rxAppError(),
    "an old STOP handoff must not restore an Rx error after reset");
  while (fixture.app.txPending()) fixture.app.runTx();
  fixture.app.enterRx();
  Zutcp::Frame stop;
  stop.type = Zutcp::FrameType::StopSending;
  stop.errorCode = 13;
  ZuCHECK(cxn->receiveStop(stop) && fixture.app.txPending(),
    "received STOP must queue its Tx completion");
  fixture.app.enterTx();
  cxn->recoveryReset();
  while (fixture.app.txPending()) fixture.app.runTx();
  ZuCHECK(cxn->txError() == Zutcp::DataError::None && !cxn->txAppError() &&
    !cxn->txCompleted() && !cxn->txCompleteCount,
    "an old STOP completion must not complete the new byte direction");

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

void testPeerParamOwnership()
{
  ZuTestScope(testPeerParamOwnership);
  HubFixture fixture;
  ZmRef<TestLink> cxn = new TestLink{&fixture.app};
  fixture.app.defer();
  fixture.app.enterTx();
  ZuCHECK(!cxn->txCreditLimit(), "unset peer limits must grant no credit");
  Zutcp::TransportParams params;
  params.initialMaxData = 16384;
  fixture.app.enterRx();
  cxn->setPeerTransportParams(params);
  params = {};
  ZuCHECK(cxn->peerParamsSet() && cxn->peerParams().initialMaxData == 16384,
    "Rx owns its parameter record independently of decoded storage");
  fixture.app.enterTx();
  ZuCHECK(!cxn->txCreditLimit(), "Tx must wait for its parameter handoff");
  while (fixture.app.txPending()) fixture.app.runTx();
  ZuCHECK(cxn->txCreditLimit() == 16384 && cxn->txFrameMax() == 16384,
    "Tx must install copied connection credit and the initial writer limit");
  Zutcp::Frame update;
  update.type = Zutcp::FrameType::MaxData;
  update.value = 32768;
  ZuCHECK(cxn->consumeTxCredit(1024) && cxn->applyMaxData(update),
    "credit extension setup failed");
  fixture.app.enterRx();
  params.initialMaxData = 16384;
  cxn->setPeerTransportParams(params);
  fixture.app.enterTx();
  while (fixture.app.txPending()) fixture.app.runTx();
  ZuCHECK(cxn->txCreditLimit() == 32768 && cxn->txCreditAvailable() == 31744 &&
    cxn->txFrameMax() == 16384,
    "a repeated setup record must not shrink current credit or reset consumed bytes");
  while (fixture.app.rxPending()) fixture.app.runRx();
}



void testRecoveryRuntimeOrdering()
{
  ZuTestScope(testRecoveryRuntimeOrdering);
  for (unsigned mode = 0; mode < 3; ++mode) {
    bool ecn = mode != 0;
    unsigned count = mode == 2 ? 2 : 4;
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->recoveryReset();
    uint64_t initial = link->recoveryDiag().congestionWindow;
    ZuCHECK(link->recoveryPackets(count, ecn), "runtime recovery packet setup");
    link->recoveryAck(count, count, ecn);
    auto diag = link->recoveryDiag();
    ZuCHECK(diag.congestionWindow == initial/2 &&
      diag.bytesInFlight == (mode == 2 ? 1200 : 2400),
      "runtime same ACK must establish loss/ECN before growth");
    ZuCHECK(link->recoveryPathDiag().ecnCEEvents == unsigned(ecn) &&
      !link->recoveryPathDiag().ecnValidationFailures,
      "ECN variant must exercise the validated CE response");
    link->recoveryStop();
  }
  for (bool reset : {false, true}) {
    HubFixture fixture;
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->recoveryReset();
    uint64_t initial = link->recoveryDiag().congestionWindow;
    ZuCHECK(link->recoveryPackets(TestLink::RecBatch + 1),
      "runtime continuation packet setup");
    fixture.app.defer();
    fixture.app.enterTx();
    link->recoveryAck(TestLink::RecBatch + 1, 1);
    ZuCHECK(fixture.app.txPending() &&
      link->recoveryDiag().congestionWindow == initial &&
      link->recoveryDiag().bytesInFlight == 1200,
      "runtime first continuation retires without committing growth");
    if (reset) link->recoveryReset();
    else link->recoveryDiscard();
    while (fixture.app.txPending()) fixture.app.runTx();
    ZuCHECK(link->recoveryDiag().congestionWindow == initial &&
      !link->recoveryDiag().bytesInFlight,
      "runtime reset/discard must reject queued growth and retire once");
    while (fixture.app.rxPending()) fixture.app.runRx();
    fixture.app.enterTx();
    link->recoveryStop();
  }
}

void testApplicationCallbacks()
{
  ZuTestScope(testApplicationCallbacks);

  HubFixture fixture;
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};
  ZmRef<TestLink> client = new TestLink{&fixture.app};
  uint8_t b[128];
  Zutcp::Frame frame;
  unsigned used = 0;

  auto packet = dataPkt(0, "abc", false, frame, used);
  ZuCHECK(packet && server->receiveFrame(frame, packet) == 0 &&
    server->rxCalls == 1 && server->rxBytes() == 3,
    "connection receive delivery callback mismatch");
  int n = Zutcp::FrameCodec::writeReset(b, sizeof(b), 7, 3);
  ZuCHECK(parseFrame(b, n, frame) && server->receiveFrame(frame) == 0 &&
    server->resetReceivedCount == 1 && server->lastResetAppError == 7 &&
    server->lastResetFinalSize == 3, "reset receive callback mismatch");
  n = Zutcp::FrameCodec::writeStopSending(b, sizeof(b), 9);
  ZuCHECK(parseFrame(b, n, frame) && client->receiveFrame(frame) == 0 &&
    client->stopReceived() && client->txCompleteCount == 1,
    "peer stop must complete the Tx direction once");
  client->reset(11);
  ZuCHECK(client->resetSentCount == 1 && client->lastResetSentAppError == 11,
    "local reset callback mismatch");
  client->stop(12);
  ZuCHECK(client->stopSentCount == 1 && client->lastStopSentAppError == 12,
    "local stop callback mismatch");
  client->dataBlockedForTest(4096);
  ZuCHECK(client->flowBlockedCount == 1 && client->lastFlowMaximum == 4096,
    "flow blocked callback mismatch");
  client->closeTransport(Zutcp::FrameType::ConnectionClose, 42);
  ZuCHECK(client->transportCloseCount == 1 &&
    client->lastCloseType == Zutcp::FrameType::ConnectionClose &&
    client->lastCloseError == 42, "transport close callback mismatch");


}

void testStopSendingOwnership()
{
  ZuTestScope(testStopSendingOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  uint8_t b[128];
  Zutcp::Frame frame;
  int n = Zutcp::FrameCodec::writeStopSending(
    b, sizeof(b), 9);
  ZuCHECK(parseFrame(b, n, frame),
    "STOP_SENDING ownership setup failed");

  fixture.app.defer();
  ZuCHECK(link->receiveFrame(frame) == 0 && link->stopReceived() &&
      fixture.app.txPending() && !link->txCompleteCount &&
      link->rxError() == Zutcp::DataError::None,
    "STOP_SENDING did not defer completion to Tx");
  ZuCHECK(link->receiveFrame(frame) == 0 && fixture.app.txPending() &&
      !link->txCompleteCount,
    "duplicate STOP_SENDING queued duplicate completion");

  fixture.app.enterTx();
  link->reset(11);
  ZuCHECK(link->txCompleteCount == 1 &&
      link->txError() == Zutcp::DataError::Reset &&
      link->txAppError() == 11,
    "racing reset did not complete stream on Tx");
  fixture.app.runTx();
  ZuCHECK(link->txCompleteCount == 1 &&
      link->txError() == Zutcp::DataError::Reset &&
      link->txAppError() == 11,
    "STOP_SENDING/reset race completed Tx more than once");
  for (unsigned i = 0; fixture.app.txPending() && i < 4; ++i)
    fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && link->txCompleteCount == 1,
    "STOP_SENDING/reset race did not drain queued Tx work exactly once");

  HubFixture resetFixture;
  ZmRef<TestLink> resetLink = new TestLink{&resetFixture.app};
  resetFixture.app.defer();
  resetFixture.app.enterTx();
  resetLink->stop(13);
  ZuCHECK(resetFixture.app.rxPending() &&
      resetLink->txError() == Zutcp::DataError::None,
    "local STOP_SENDING did not defer its Rx cause");

  n = Zutcp::FrameCodec::writeReset(
    b, sizeof(b), 17, 0);
  ZuCHECK(parseFrame(b, n, frame),
    "RESET_STREAM ownership setup failed");
  resetFixture.app.enterRx();
  ZuCHECK(resetLink->receiveFrame(frame) == 0 &&
      resetLink->rxError() == Zutcp::DataError::Reset &&
      resetLink->rxAppError() == 17,
    "racing peer reset did not establish the Rx cause");
  resetFixture.app.runRx();
  ZuCHECK(resetLink->rxError() == Zutcp::DataError::Reset &&
      resetLink->rxAppError() == 17,
    "deferred STOP_SENDING overwrote the Rx reset cause");
  for (unsigned i = 0;
      (resetFixture.app.rxPending() || resetFixture.app.txPending()) && i < 8;
      ++i) {
    if (resetFixture.app.rxPending()) resetFixture.app.runRx();
    if (resetFixture.app.txPending()) resetFixture.app.runTx();
  }
  ZuCHECK(!resetFixture.app.rxPending() && !resetFixture.app.txPending(),
    "STOP_SENDING/RESET_STREAM race did not drain deferred work");
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

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiTestResidue::init("ZutcpAPITest");
#ifdef Zutcp_DEBUG
  g_residue = ZiTestResidue::dir("qlog");
#endif

  ZuTestMain();
  ZuTestCall(testParams);
  ZuTestCall(testConnectionShape);
  ZuTestCall(testDataReadiness);
  ZuTestCall(testAlignedSurfaceShape);
  ZuTestCall(testLinkStateOwnership);
  ZuTestCall(testDeferredByteReset);
  ZuTestCall(testPeerParamOwnership);
  ZuTestCall(testRecoveryRuntimeOrdering);
  ZuTestCall(testApplicationCallbacks);
  ZuTestCall(testStopSendingOwnership);
  ZuTestCall(testInitValidate);
}
