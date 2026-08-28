//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zquic.hh>

#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace {

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

#ifdef Zquic_DEBUG
static Zi::Path g_residue;

static Zi::Path testPath_(ZuCSpan name)
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

static unsigned parseJSONSeq_(ZuCSpan data)
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

static void closeQLog_(ZquicLogger::Trace &trace)
{
  ZmBlock<>{}([&trace](auto wake) {
    ZquicLogger::close(trace, ZuMv(wake));
  });
}
#endif

struct TestLink;
struct TestStream :
    public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc> {
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
  void txComplete_(bool) { ++txCompleteCount; }

  unsigned txCompleteCount = 0;
};

struct HubApp : public Zquic::Hub<HubApp> {
  enum Shard { Rx, Tx };
  using PendingQueue =
    ZmList<ZmFn<>, ZmListHeapID<"Zquic.APITest.Pending">>;

  void activeMigration(bool v) {
    migration = v ?
      Zquic::MigrationMode::Active : Zquic::MigrationMode::Passive;
  }
  void migrationMode(Zquic::MigrationMode::T v) {
    migration = v;
  }
  Zquic::MigrationMode::T migrationMode() const {
    return migration;
  }
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
  Zquic::MigrationMode::T migration = Zquic::MigrationMode::Passive;
  Shard shard = Rx;
  bool deferMode = false;
};
struct ClientApp : public Zquic::Client<ClientApp> { };
struct ServerAppLink;
struct ServerApp : public Zquic::Server<ServerApp, ServerAppLink> { };
struct ServerAppStream :
    public Zquic::SrvStream<
      ServerAppLink, ServerAppStream, StreamTxBufAlloc> {
  using Base = Zquic::SrvStream<
    ServerAppLink, ServerAppStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};
struct ServerAppLink :
    public Zquic::SrvLink<
      ServerApp, ServerAppLink, ServerAppStream, StreamTxBufAlloc> {
  using Base = Zquic::SrvLink<
    ServerApp, ServerAppLink, ServerAppStream, StreamTxBufAlloc>;

  ServerAppLink(ServerApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ServerAppStream>) { }
};

struct ClientShapeApp : public Zquic::Client<ClientShapeApp> { };
struct ClientShapeLink;
struct ClientShapeStream :
    public Zquic::CliStream<
      ClientShapeLink, ClientShapeStream, StreamTxBufAlloc> {
  using Base = Zquic::CliStream<
    ClientShapeLink, ClientShapeStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ClientShapeLink :
    public Zquic::CliLink<
      ClientShapeApp, ClientShapeLink, ClientShapeStream,
      StreamTxBufAlloc> {
  using Base = Zquic::CliLink<
    ClientShapeApp, ClientShapeLink, ClientShapeStream,
    StreamTxBufAlloc>;

  ClientShapeLink(ClientShapeApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ClientShapeStream>) { }
  void connectFailed(bool) { ++failures; }
  void disconnected(bool) { ++disconnects; }
  Zquic::LinkDiag diag() const { return Base::diag_(); }

  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> disconnects = 0;
};

struct ServerShapeLink;
struct ServerShapeApp :
    public Zquic::Server<ServerShapeApp, ServerShapeLink> { };
struct ServerShapeStream :
    public Zquic::SrvStream<
      ServerShapeLink, ServerShapeStream, StreamTxBufAlloc> {
  using Base = Zquic::SrvStream<
    ServerShapeLink, ServerShapeStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct ServerShapeLink :
    public Zquic::SrvLink<
      ServerShapeApp, ServerShapeLink, ServerShapeStream,
      StreamTxBufAlloc> {
  using Base = Zquic::SrvLink<
    ServerShapeApp, ServerShapeLink, ServerShapeStream,
    StreamTxBufAlloc>;

  ServerShapeLink(ServerShapeApp *app) : Base{app} { }

  void connected(Zquic::Connected) { }
  void streamed(ZmRef<ServerShapeStream>) { }
  void migrationStarted(const Zquic::MigrationResult &) {
    if (startedFn) startedFn();
  }
  void migrationPromoted(const Zquic::MigrationResult &) {
    if (promotedFn) promotedFn();
  }
  void onStarted(ZmFn<void()> fn) { startedFn = ZuMv(fn); }
  void onPromoted(ZmFn<void()> fn) { promotedFn = ZuMv(fn); }
  void initSrvPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
#ifdef Zquic_DEBUG
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::observePathRxTx_(ZuMv(local), ZuMv(remote));
  }
  bool promoteObservedPath() {
    ZuBSpan challenge = Base::validatingChallenge_();
    if (challenge.length() != Zquic::PathChallenge::Length) return false;
    uint8_t response[Zquic::PathChallenge::Length];
    memcpy(response, challenge.data(), sizeof(response));
    return Base::onPathResponse_(ZuBSpan{response, sizeof(response)});
  }
#endif

  ZmFn<void()> startedFn;
  ZmFn<void()> promotedFn;
};

struct TestLink :
    public Zquic::Link<
      HubApp, TestLink, StreamTxBufAlloc,
      TestStream> {
  using Base = Zquic::Link<
    HubApp, TestLink, StreamTxBufAlloc,
    TestStream>;
  TestLink(HubApp *app, bool isServer = false) : Base{app, isServer} {
    Base::configLocalParams_(app);
  }

  const Zquic::TransportParams &localParams() const {
    return Base::localTransportParams_();
  }

  void streamed(ZmRef<TestStream>) { }
  void streamOpen(ZmRef<TestStream> stream, bool local) {
    lastOpened = ZuMv(stream);
    local ? ++localStreamOpens : ++peerStreamOpens;
    ++streamOpenCount;
  }
  void streamData(
    ZmRef<TestStream> stream, uint64_t offset, ZuBSpan payload, bool fin) {
    lastDataStream = ZuMv(stream);
    lastDataOffset = offset;
    lastDataLength = payload.length();
    lastDataFin = fin;
    ++streamDataCount;
  }
  void streamResetReceived(
    ZmRef<TestStream> stream, uint64_t appError, uint64_t finalSize) {
    lastResetStream = ZuMv(stream);
    lastResetAppError = appError;
    lastResetFinalSize = finalSize;
    ++resetReceivedCount;
  }
  void streamResetSent(
    uint64_t streamID, uint64_t appError, uint64_t finalSize) {
    lastResetSentID = streamID;
    lastResetSentAppError = appError;
    lastResetSentFinalSize = finalSize;
    ++resetSentCount;
  }
  void streamStopSendingReceived(
    ZmRef<TestStream> stream, uint64_t appError) {
    lastStopStream = ZuMv(stream);
    lastStopAppError = appError;
    ++stopReceivedCount;
  }
  void streamStopSendingSent(uint64_t streamID, uint64_t appError) {
    lastStopSentID = streamID;
    lastStopSentAppError = appError;
    ++stopSentCount;
  }
  void flowBlocked(
    Zquic::FrameType::T type, uint64_t streamID,
    Zquic::StreamType::T streamType, uint64_t maximum) {
    lastFlowType = type;
    lastFlowStreamID = streamID;
    lastFlowStreamType = streamType;
    lastFlowMaximum = maximum;
    ++flowBlockedCount;
  }
  void transportClose(Zquic::FrameType::T type, uint64_t errorCode) {
    lastCloseType = type;
    lastCloseError = errorCode;
    ++transportCloseCount;
  }
  void pto_() { }
  void queueRetransmit_() { }
  void statelessReset() { ++statelessResetCount; }
  void migrationStarted(const Zquic::MigrationResult &result) {
    lastMigration = result;
    migrationCallbackOrder << "S";
    ++migrationStartedCount;
  }
  void pathUpdate(const Zquic::PathInfo &info) {
    lastPath = info;
    lastPathLocal = info.local;
    lastPathRemote = info.remote;
    lastPathValidated = info.validated;
    lastPathMaxUDP = info.activeMaxUDP;
    migrationCallbackOrder << "U";
    ++pathUpdateCount;
  }
  void migrationPromoted(const Zquic::MigrationResult &result) {
    lastMigration = result;
    migrationCallbackOrder << "P";
    ++migrationPromotedCount;
  }
  void migrationFailed(const Zquic::MigrationResult &result) {
    lastMigration = result;
    lastMigrationLocal = result.candidate.local;
    lastMigrationRemote = result.candidate.remote;
    migrationCallbackOrder << "F";
    ++migrationFailureCount;
  }
	  void setPeerResetToken(const Zquic::ResetToken &token) {
	    Base::setPeerResetToken_(token);
	  }
	  bool addLocalCID(
	    const Zquic::CxnID &id, uint64_t sequence,
	    const Zquic::ResetToken &token = {}) {
	    return Base::addLocalCID_(id, sequence, token);
	  }
	  bool addPeerCID(
	    const Zquic::CxnID &id, uint64_t sequence,
	    const Zquic::ResetToken &token = {}) {
	    return Base::addPeerCID_(id, sequence, token);
	  }
	  void setRuntimeCIDs(const Zquic::CxnID &local, const Zquic::CxnID &peer) {
	    Base::setRuntimeCIDs_(Zquic::CxnID{"initcid0"}, local, peer);
	  }
	  bool chooseMigrationPeerCID(
	    bool requireNewPeerCID, Zquic::CxnID &id, uint64_t &sequence,
	    Zquic::MigrationReason::T &reason) {
	    bool ok = Base::selectPeerCIDRx_(
	      requireNewPeerCID, id, sequence);
	    reason = ok ? Zquic::MigrationReason::T(Zquic::MigrationReason::None) :
	      Zquic::MigrationReason::T(Zquic::MigrationReason::NoPeerCID);
	    return ok;
	  }
	  void setPeerTransportParams(const Zquic::TransportParams &params) {
	    Base::crypto_().peerParams(params);
	  }
	  bool prepareActiveMigration(
	    const Zquic::MigrationParams &params,
	    Zquic::MigrationReason::T &reason) {
	    return Base::prepareMig_(params, reason);
	  }
	  bool activateActiveMigration() {
	    return Base::activateMig_(false);
	  }
	  void activeRebindStart() {
	    Base::migRebindStart_();
	  }
	  void activeRebindFail(Zquic::MigrationReason::T reason) {
	    Base::migRebindFail_(reason);
	    Base::failMig_(reason);
	  }
	  bool buildActiveMigrationChallenge(Zquic::Frame &frame) {
	    Zquic::PktBuild build;
	    typename Base::TxPktRefs refs;
	    Zquic::ControlFrame control;
	    if (!Base::buildMigChal_(build, refs, control))
	      return false;
	    if (refs.count() != 1) return false;
	    unsigned used = 0;
	    return !Zquic::FrameCodec::parse(
	      ZuBSpan{build.data()[0].base, build.data()[0].len},
	      frame, used) && used == build.data()[0].len;
	  }
	  bool installAppDataKeys(
	    const Zquic::TrafficSecret &rx, const Zquic::TrafficSecret &tx,
	    const Zquic::CxnID &localCID) {
	    return Base::installAppDataKeys_(rx, tx, localCID);
	  }
	  bool receiveNewCxnID(const Zquic::Frame &frame) {
	    return Base::receiveNewCxnID_(frame);
	  }
  bool receiveRetireCxnID(const Zquic::Frame &frame) {
    return Base::receiveRetireCxnID_(frame);
  }
  bool peerCID(
    uint64_t sequence, Zquic::CxnID &id,
    Zquic::ResetToken &token) const {
    auto cid = Base::peerCID_(sequence);
    if (!cid) return false;
    id = cid->id;
    token = cid->resetToken;
    return true;
  }
  bool peerCIDActive(uint64_t sequence) const {
    auto cid = Base::peerCID_(sequence);
    return cid && cid->state == Zquic::CxnState::Active;
  }
  Zquic::CxnID activeTxPeerCID() const {
    return Base::cid_(Base::CIDSel::Peer);
  }
  bool localCIDRetired(uint64_t sequence) const {
    auto cid = Base::localCID_(sequence);
    return cid && cid->state == Zquic::CxnState::Retired;
  }
  void retiredLocalCID_(uint64_t sequence, const Zquic::CxnID &id) {
    retiredSeq = sequence;
    retiredCID = id;
    ++retiredCount;
  }
	  bool checkStatelessReset(ZuBSpan datagram) {
	    return Base::checkStatelessReset_(datagram);
	  }
	  bool draining() const { return Base::drainingRx_(); }
	  bool startHandshakeForTest() { return Base::startHandshake_(); }
	  bool handshakingRx() const {
	    return Base::handshakeStartedRx_() && !Base::establishedRx_();
	  }
	  bool handshakingTx() const {
	    return Base::handshakeStartedTx_() && !Base::establishedTx_();
	  }
	  bool closingRx() const { return Base::closingRx_(); }
	  bool closingTx() const { return Base::closingTx_(); }
	  bool linkClosedRx() const { return Base::linkClosedRx_(); }
	  bool linkClosedTx() const { return Base::linkClosedTx_(); }
	  uint64_t closeErrorRx() const { return Base::closeErrorRx_(); }
	  uint64_t closeErrorTx() const { return Base::closeErrorTx_(); }
	  void closeTxForTest(uint64_t errorCode, bool application = false) {
	    Base::closeStateTx_(errorCode, application);
	  }
  void scheduleStream(const ZmRef<TestStream> &stream) {
    Base::streamWritable_(stream);
  }
  void postPeerLimits(uint64_t duplex, uint64_t simplex) {
    Base::postPeerStreamLimitsTx_(duplex, simplex);
  }
  bool receiveMaxStreams(
    Zquic::StreamType::T type, uint64_t maximum) {
    Zquic::Frame frame;
    frame.type = Zquic::FrameType::MaxStreams;
    frame.streamType = type;
    frame.value = maximum;
    return Base::rxApplyMaxStreams_(frame);
  }
  bool queueMaxStreams(
    Zquic::StreamType::T type, uint64_t maximum) {
    return Base::txQueueControl_(Zquic::ControlFrame::flowUpdate(
      Zquic::FlowUpdate{
	Zquic::FrameType::MaxStreams, 0, maximum, type}));
  }
  bool flushControlSends() {
    return Base::flushCtlStreams_(
      ZiSockAddr{},
      [](Zquic::PktBuild &) { return true; },
      [](Zquic::PktBuild &, ZiSockAddr, const typename Base::TxPktRefs &) {
	return true;
      });
  }
  unsigned scheduledStreams() const {
    return Base::scheduledStreamCount_();
  }
  ZmRef<TestStream> findTxStream(int64_t id) const {
    return Base::findTxStream_(id);
  }
  void dataBlockedForTest(uint64_t maximum) {
    Base::dataBlocked_(maximum);
  }
	  void closeTransport(Zquic::FrameType::T type, uint64_t errorCode) {
	    Base::closeLink_(errorCode);
	    Base::transportClose_(type, errorCode);
	  }
	  void closeForTest(uint64_t errorCode = 0) {
	    Base::closeState_(errorCode);
	  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
  void initClientPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initClientPathTx_(ZuMv(local), ZuMv(remote));
  }
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::startPathValidation_(ZuMv(local), ZuMv(remote));
  }
  void observePathRx(ZiSockAddr local, ZiSockAddr remote) {
    Base::observePathRxTx_(ZuMv(local), ZuMv(remote), false);
  }
  bool migrationActive() const { return Base::migrationActive_(); }
  Zquic::PathInfo candidatePathInfo() const {
    return Base::candidatePathInfo_();
  }
  ZuBSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
  bool pathResponse(ZuBSpan data) { return Base::onPathResponse_(data); }
  void pathTimeout() { Base::pathExpired_(); }
  void flushTx_() { ++txFlushQueued; }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush_() { ++txFlushQueued; }
  void queueTxFlush_(ZiSockAddr) { ++txFlushQueued; }
  Zquic::LinkDiag diag() const { return Base::diag_(); }

  ZmRef<TestStream> lastOpened;
  ZmRef<TestStream> lastDataStream;
  ZmRef<TestStream> lastResetStream;
  ZmRef<TestStream> lastStopStream;
  ZiSockAddr lastPathLocal;
  ZiSockAddr lastPathRemote;
  ZiSockAddr lastMigrationLocal;
  ZiSockAddr lastMigrationRemote;
  Zquic::PathInfo lastPath;
  Zquic::MigrationResult lastMigration;
  ZtString<> migrationCallbackOrder;
  uint64_t retiredSeq = 0;
  uint64_t lastDataOffset = 0;
  uint64_t lastResetAppError = 0;
  uint64_t lastResetFinalSize = 0;
  uint64_t lastResetSentID = 0;
  uint64_t lastResetSentAppError = 0;
  uint64_t lastResetSentFinalSize = 0;
  uint64_t lastStopAppError = 0;
  uint64_t lastStopSentID = 0;
  uint64_t lastStopSentAppError = 0;
  uint64_t lastFlowStreamID = 0;
  uint64_t lastFlowMaximum = 0;
  uint64_t lastCloseError = 0;
  Zquic::CxnID retiredCID;
  Zquic::FrameType::T lastFlowType = Zquic::FrameType::Unknown;
  Zquic::FrameType::T lastCloseType = Zquic::FrameType::Unknown;
  Zquic::StreamType::T lastFlowStreamType = Zquic::StreamType::Duplex;
  unsigned lastDataLength = 0;
  unsigned lastPathMaxUDP = 0;
  unsigned retiredCount = 0;
  unsigned streamOpenCount = 0;
  unsigned localStreamOpens = 0;
  unsigned peerStreamOpens = 0;
  unsigned streamDataCount = 0;
  unsigned resetReceivedCount = 0;
  unsigned resetSentCount = 0;
  unsigned stopReceivedCount = 0;
  unsigned stopSentCount = 0;
  unsigned flowBlockedCount = 0;
  unsigned transportCloseCount = 0;
  unsigned statelessResetCount = 0;
  unsigned migrationStartedCount = 0;
  unsigned migrationPromotedCount = 0;
  unsigned pathUpdateCount = 0;
  unsigned migrationFailureCount = 0;
  unsigned txFlushQueued = 0;
  bool lastDataFin = false;
  bool lastPathValidated = false;
	};

template <typename L>
bool waitUntil(L l)
{
  for (unsigned i = 0; i < 2000; ++i) {
    if (l()) return true;
    usleep(1000);
  }
  return false;
}

struct HubFixture {
  HubFixture() : mx{mxParams_()} {
    ZiAssert(mx.start(), "Zquic", (),
      "API test multiplexer start failed", return);
    ZiAssert(app.init(Zquic::HubParams(&mx, "3", "4")),
      "Zquic", (), "API test hub init failed", return);
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

static bool parseFrame_(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuBSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt_(
  uint64_t id, uint64_t offset, ZuBSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(
      ZuBSpan{packet->data(), packet->length}, frame, used) ||
      used != packet->length)
    return nullptr;
  return packet;
}

} // namespace

void testParams()
{
  ZuTestScope(testParams);

  ZuCSpan alpn[] = { "zquic-test" };

  auto clientParams = Zquic::ClientParams(nullptr, "1", "2")
    .caPath("ca.pem")
    .certPath("client.pem")
    .keyPath("client.key")
    .asyncThread("4")
    .maxData(1<<20)
    .maxStreamData(1<<16)
    .maxStreamsDuplex(16)
    .maxStreamsSimplex(4)
    .heartBeat(ZuTime{7})
    .maxUDP(1200)
    .migrationMode(Zquic::MigrationMode::Disabled)
    .activeMigration(true)
    .migCIDRes(Zquic::LocalCIDLimit + 8)
    .migCloseOnFail(true)
    .alpn(alpn)
    .errorFn(Zquic::defaultErrorFn());
  ZuCHECK(clientParams.migrationMode() == Zquic::MigrationMode::Active,
    "activeMigration(true) did not enable active migration mode");
  ZuCHECK(clientParams.activeMigration(),
    "active migration getter did not report enabled mode");
  ZuCHECK(clientParams.migCIDRes() ==
      Zquic::LocalCIDLimit - 1,
    "migration CID reserve was not clamped");
  ZuCHECK(clientParams.migCloseOnFail(),
    "migration close-on-failure getter did not report enabled policy");
  ZuCHECK(clientParams.heartBeat() == ZuTime{7},
    "heartBeat getter did not report configured interval");
  auto zeroParams = Zquic::ClientParams(nullptr, "1", "2")
    .heartBeat(ZuTime{0});
  ZuCHECK(!*zeroParams.heartBeat(),
    "explicit zero heartBeat interval was not normalized to null");
  bool badHeartBeat = false;
  try {
    (void)Zquic::ClientParams(nullptr, "1", "2").heartBeat(ZuTime{-1});
  } catch (const ZeException &) {
    badHeartBeat = true;
  }
  ZuCHECK(badHeartBeat, "negative heartBeat interval was accepted");

  uint8_t h3[] = { 'h', '3' };
  ptls_iovec_t iov[] = { ptls_iovec_init(h3, sizeof(h3)) };
  auto serverParams = Zquic::ServerParams(nullptr, "1", "2")
    .caPath("ca.pem")
    .certPath("server.pem")
    .keyPath("server.key")
    .alpn(ZuSpan<const ptls_iovec_t>(iov, 1))
    .errorFn(Zquic::defaultErrorFn());
  ZuCHECK(serverParams.migrationMode() == Zquic::MigrationMode::Passive,
    "default migration mode was not passive");
  ZuCHECK(!serverParams.activeMigration(),
    "default active migration getter unexpectedly reported active");
  ZuCHECK(!serverParams.migCloseOnFail(),
    "default migration close-on-failure policy was not false");
  ZuCHECK(!*serverParams.heartBeat(),
    "default heartBeat interval was not disabled");

  HubFixture fixture;
  {
    ZmRef<TestLink> link = new TestLink{&fixture.app, false};
    ZuCHECK(link->localParams().disableActiveMigration,
      "default local transport params enabled active migration");
  }
  {
    fixture.app.activeMigration(true);
    ZmRef<TestLink> link = new TestLink{&fixture.app, false};
    ZuCHECK(!link->localParams().disableActiveMigration,
      "active migration mode did not clear disable_active_migration");
  }
}

void testStreamShape()
{
  ZuTestScope(testStreamShape);

  HubFixture fixture;
  ZmRef<TestLink> client = new TestLink{&fixture.app, false};
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};

  auto c0 = client->stream();
  auto c1 = client->stream(Zquic::StreamType::Simplex);
  auto s0 = server->stream();

  ZuCHECK(c0 && c0->id() == 0, "client bidi stream ID mismatch");
  ZuCHECK(c1 && c1->id() == 2, "client uni stream ID mismatch");
  ZuCHECK(s0 && s0->id() == 1, "server bidi stream ID mismatch");
  ZuCHECK(c0->link() == client.ptr() && c1->link() == client.ptr(),
    "client stream owner link mismatch");
  ZuCHECK(s0->link() == server.ptr(), "server stream owner link mismatch");
  ZuCHECK(client->streamCount() == 2, "client stream count mismatch");
  ZuCHECK(client->findStream(0) == c0, "stream hash lookup failed");

  {
    auto tx = c0->txStream_();
    tx << "abc" << Zi::flush();
  }
  ZuCHECK(c0->txBytes() == 3, "stream Tx byte accounting mismatch");
  c0->fin();
  c0->reset(7);
  c0->stop(9);
  ZuCHECK(c0->finSent(), "FIN state was not recorded");

  client->closeForTest(42);
  ZuCHECK(client->closed() && client->closeError() == 42,
    "link close state mismatch");
}

void testAlignedSurfaceShape()
{
  ZuTestScope(testAlignedSurfaceShape);

  ZiMultiplex mx{HubFixture::mxParams_()};
  bool mxOK = mx.start();
  ZuCHECK(mxOK, "aligned shape multiplexer start failed");
  if (!mxOK) return;

  ClientShapeApp clientApp;
  bool clientOK = clientApp.init(
    Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZuCHECK(clientOK, "aligned client app init failed");
  if (!clientOK) {
    mx.stop();
    return;
  }
  ZmRef<ClientShapeLink> client = new ClientShapeLink{&clientApp};
  ZmRef<ClientShapeStream> c0;
  ZmBlock<>{}([&](auto wake) {
    clientApp.txRun([client, &c0, wake = ZuMv(wake)]() mutable {
      c0 = client->stream();
      wake();
    });
  });
  ZuCHECK(c0 && c0->id() == 0 && c0->link() == client.ptr() &&
      !client->isServer(),
    "client aligned link/stream shape mismatch");
#ifdef Zquic_DEBUG
  ZuCHECK(client->diag().unhandledAppEvents(),
    "default client stream-open hook was not visible in diagnostics");
#endif

  ServerShapeApp serverApp;
  bool serverOK = serverApp.init(
    Zquic::ServerParams(&mx, "3", "4")
      .certPath("server.pem")
      .keyPath("server.key")
      .alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZuCHECK(serverOK, "aligned server app init failed");
  if (!serverOK) {
    clientApp.final();
    mx.stop();
    return;
  }
  ZmRef<ServerShapeLink> server = new ServerShapeLink{&serverApp};
  ZmRef<ServerShapeStream> s0;
  ZmBlock<>{}([&](auto wake) {
    serverApp.txRun([server, &s0, wake = ZuMv(wake)]() mutable {
      s0 = server->stream();
      wake();
    });
  });
  ZuCHECK(s0 && s0->id() == 1 && s0->link() == server.ptr() &&
      server->isServer(),
    "server aligned link/stream shape mismatch");

  in_addr localIP;
  localIP.s_addr = htonl(0x0a000001);
  in_addr remoteIP;
  remoteIP.s_addr = htonl(0x0a000002);
  ZiSockAddr localAddr{ZiIP{localIP}, 4433};
  ZiSockAddr remoteAddr{ZiIP{remoteIP}, 50000};
  ZmBlock<>{}([&](auto wake) {
    serverApp.txRun([
      server, localAddr, remoteAddr, wake = ZuMv(wake)
    ]() mutable {
      server->initSrvPath(localAddr, remoteAddr);
      wake();
    });
  });
  bool pathInfoSeen = false;
  Zquic::PathInfo activePath;
  Zquic::PathInfo candidatePath;
  ZmBlock<>{}([&](auto wake) {
    server->pathInfo([
      &activePath, &candidatePath, &pathInfoSeen, wake = ZuMv(wake)
    ](const Zquic::PathInfo &active, const Zquic::PathInfo &candidate)
	mutable {
      activePath = active;
      candidatePath = candidate;
      pathInfoSeen = true;
      wake();
    });
  });
	  ZuCHECK(pathInfoSeen &&
	      activePath.role == Zquic::PathRole::Active &&
	      activePath.local == localAddr &&
	      activePath.remote == remoteAddr &&
	      candidatePath.role == Zquic::PathRole::Active &&
	      !candidatePath.remote,
	    "posted server pathInfo snapshot mismatch");
  bool migrationStateSeen = false;
  Zquic::MigrationResult migrationState;
  ZmBlock<>{}([&](auto wake) {
    server->migrationState([
      &migrationState, &migrationStateSeen, wake = ZuMv(wake)
    ](const Zquic::MigrationResult &result) mutable {
      migrationState = result;
      migrationStateSeen = true;
      wake();
    });
  });
  ZuCHECK(migrationStateSeen &&
      migrationState.state == Zquic::MigrationState::Idle &&
      migrationState.active.role == Zquic::PathRole::Active &&
      migrationState.active.remote == remoteAddr,
    "posted server migrationState snapshot mismatch");

#ifdef Zquic_DEBUG
  ZiSockAddr promotedRemote{ZiIP{remoteIP}, 50001};
  bool promoted = false;
  ZiSockAddr peerAfterPromotion;
  ZmBlock<>{}([&](auto wake) {
    server->onStarted(ZuMv(wake));
    serverApp.txRun([server, localAddr, promotedRemote]() mutable {
      server->observePath(localAddr, promotedRemote);
    });
  });
  ZmBlock<>{}([&](auto wake) {
    server->onPromoted(ZuMv(wake));
    serverApp.txRun([server, &promoted]() mutable {
      promoted = server->promoteObservedPath();
    });
  });
  ZmBlock<>{}([&](auto wake) {
    serverApp.txRun([server, &peerAfterPromotion, wake = ZuMv(wake)]() mutable {
      peerAfterPromotion = server->peer();
      wake();
    });
  });
  ZuCHECK(promoted &&
      peerAfterPromotion == promotedRemote,
    "server peer address was not updated after path promotion");
#endif

  // Drain path-promotion work while its app and links remain valid.
  mx.stop();
  s0 = nullptr;
  server = nullptr;
  c0 = nullptr;
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

void testStreamLimitOwnership()
{
  ZuTestScope(testStreamLimitOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(ZiSockAddr{}, ZiSockAddr{});
  fixture.app.defer();

  fixture.app.enterTx();
  link->setPeerStreamLimit(Zquic::StreamType::Duplex, 0);
  link->setPeerStreamLimit(Zquic::StreamType::Simplex, 0);
  fixture.app.enterRx();
  link->postPeerLimits(1, 2);
  ZuCHECK(fixture.app.txPending(),
    "initial peer stream limits were not posted to Tx");
  fixture.app.enterTx();
  ZuCHECK(!link->peerStreamLimit(Zquic::StreamType::Duplex) &&
      !link->peerStreamLimit(Zquic::StreamType::Simplex),
    "Tx observed peer stream limits before applying the publication");
  fixture.app.runTx();
  ZuCHECK(link->peerStreamLimit(Zquic::StreamType::Duplex) == 1 &&
      link->peerStreamLimit(Zquic::StreamType::Simplex) == 2,
    "Tx did not apply the initial peer stream limits");

  ZuCHECK(link->stream(), "initial peer stream limit did not open a stream");
  ZuCHECK(!link->stream() &&
      link->queuedLocalStreams(Zquic::StreamType::Duplex) == 1,
    "saturated peer stream limit did not queue a local stream");
  while (fixture.app.txPending()) fixture.app.runTx();

  fixture.app.enterRx();
  ZuCHECK(link->receiveMaxStreams(Zquic::StreamType::Duplex, 2) &&
      fixture.app.txPending(),
    "received MAX_STREAMS was not posted to Tx");
  fixture.app.enterTx();
  ZuCHECK(link->peerStreamLimit(Zquic::StreamType::Duplex) == 1 &&
      link->queuedLocalStreams(Zquic::StreamType::Duplex) == 1,
    "Tx applied MAX_STREAMS before its queued handoff");
  fixture.app.runTx();
  ZuCHECK(link->peerStreamLimit(Zquic::StreamType::Duplex) == 2 &&
      link->localStreamsOpened(Zquic::StreamType::Duplex) == 2 &&
      !link->queuedLocalStreams(Zquic::StreamType::Duplex),
    "MAX_STREAMS did not open the queued local stream on Tx");
  while (fixture.app.txPending()) fixture.app.runTx();
  fixture.app.enterRx();
  while (fixture.app.rxPending()) fixture.app.runRx();

  link->setLocalStreamLimit(Zquic::StreamType::Duplex, 4);
  fixture.app.enterTx();
  ZuCHECK(link->queueMaxStreams(Zquic::StreamType::Duplex, 3) &&
      link->flushControlSends(),
    "Tx rejected its current MAX_STREAMS control using Rx-owned state");
}

void testStatelessResetDetection()
{
  ZuTestScope(testStatelessResetDetection);

#ifdef Zquic_DEBUG
	Zi::Path qlogPath = testPath_("ZquicAPIStatelessReset.sqlog");
	ZiFile::remove(qlogPath);
	ZquicLogParams params;
	params.enabled(true).path(qlogPath);
#endif

	HubFixture fixture;
#ifdef Zquic_DEBUG
	ZuCHECK(ZquicLogger::init(
	    fixture.app.qlogTrace(), params, Zquic::Vantage::Unknown),
	  "stateless reset qlog init failed");
	ZquicLogger::start();
#endif
	ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken token{"0123456789abcdef"};
  uint8_t packet[64] = {};
  memset(packet, 0xa5, sizeof(packet));
  packet[0] = 0x65;
  memcpy(
    packet + sizeof(packet) - Zquic::ResetToken::Length,
    token.data(), Zquic::ResetToken::Length);
  ZuBSpan datagram{packet, sizeof(packet)};

  Zquic::ResetToken decoded;
  ZuCHECK(!Zquic::StatelessReset::decode(decoded, datagram) &&
      decoded == token,
    "stateless reset token decode mismatch");
  ZuCHECK(Zquic::StatelessReset::verify(datagram, token),
    "stateless reset token verify failed");
  ZuCHECK(!link->checkStatelessReset(datagram) && !link->draining(),
    "link accepted stateless reset before peer token was known");
  link->setPeerResetToken(token);
  auto stream = link->stream();
  link->scheduleStream(stream);
  ZuCHECK(link->scheduledStreams() == 1,
    "stateless reset queued-stream setup failed");
  fixture.app.defer();
  ZuCHECK(link->checkStatelessReset(datagram) && link->draining() &&
      fixture.app.txPending() && !fixture.app.rxPending() &&
      link->scheduledStreams() == 1 && !link->statelessResetCount,
    "stateless reset did not defer Tx cleanup");
  ZuCHECK(link->checkStatelessReset(datagram) && fixture.app.txPending() &&
      !fixture.app.rxPending() && !link->statelessResetCount,
    "duplicate stateless reset queued duplicate completion");
  fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && fixture.app.rxPending() &&
      !link->scheduledStreams() && !link->statelessResetCount,
    "stateless reset Tx cleanup/completion ordering mismatch");
  fixture.app.runRx();
  ZuCHECK(!fixture.app.rxPending() && link->statelessResetCount == 1,
    "stateless reset Rx completion did not fire exactly once");

#ifdef Zquic_DEBUG
	closeQLog_(fixture.app.qlogTrace());
	ZquicLogger::stop();
	ZquicLogDiag diag = ZquicLogger::diag();
	ZquicLogger::final(fixture.app.qlogTrace());
  ZuCHECK(diag.recordsEnqueued >= 1,
    "stateless reset qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 1,
    "stateless reset qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0,
    "stateless reset qlog writer failure");
  ZtString<> qlog = readFile_(qlogPath);
  ZuCHECK(qlog, "stateless reset qlog output missing");
  ZuCHECK(qlog.find<"zquic:stateless_reset">() >= 0,
    "stateless reset qlog event missing");
  ZuCHECK(qlog.find<"\"trigger\":\"received\"">() >= 0,
    "stateless reset qlog trigger missing");
  ZuCHECK(qlog.find<"\"reason\":\"token_match\"">() >= 0,
    "stateless reset qlog reason missing");
#endif
}

void testApplicationCallbacks()
{
  ZuTestScope(testApplicationCallbacks);

  HubFixture fixture;
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};
  ZmRef<TestLink> client = new TestLink{&fixture.app};
  uint8_t b[128];
  Zquic::Frame frame;
  unsigned used = 0;

  auto local = client->stream();
  ZuCHECK(local && client->streamOpenCount == 1 &&
      client->localStreamOpens == 1 && client->lastOpened == local,
    "local stream-open callback mismatch");

  auto packet = streamPkt_(0, 0, "abc", false, frame, used);
  ZuCHECK(packet && server->receiveFrame(frame, packet) == 0,
    "peer STREAM callback setup failed");
  auto peer = server->findStream(0);
  ZuCHECK(peer && server->streamOpenCount == 1 &&
      server->peerStreamOpens == 1 &&
      server->streamDataCount == 1 &&
      server->lastDataStream == peer &&
      !server->lastDataOffset &&
      server->lastDataLength == 3 &&
      !server->lastDataFin,
    "peer stream/data callbacks mismatch");

  int n = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 0, 7, 3);
  ZuCHECK(parseFrame_(b, n, frame) &&
      server->receiveFrame(frame) == 0 &&
      server->resetReceivedCount == 1 &&
      server->lastResetStream == peer &&
      server->lastResetAppError == 7 &&
      server->lastResetFinalSize == 3,
    "RESET_STREAM receive callback mismatch");

  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), local->id(), 9);
  ZuCHECK(parseFrame_(b, n, frame) &&
      client->receiveFrame(frame) == 0 &&
      client->stopReceivedCount == 1 &&
      client->lastStopStream == local &&
      client->lastStopAppError == 9,
    "STOP_SENDING receive callback mismatch");

  auto reset = client->stream();
  reset->reset(11);
  ZuCHECK(client->resetSentCount == 1 &&
      client->lastResetSentID == uint64_t(reset->id()) &&
      client->lastResetSentAppError == 11,
    "local RESET_STREAM callback mismatch");

  auto stop = client->stream();
  stop->stop(12);
  ZuCHECK(client->stopSentCount == 1 &&
      client->lastStopSentID == uint64_t(stop->id()) &&
      client->lastStopSentAppError == 12,
    "local STOP_SENDING callback mismatch");

  client->dataBlockedForTest(4096);
  ZuCHECK(client->flowBlockedCount == 1 &&
      client->lastFlowType == Zquic::FrameType::DataBlocked &&
      client->lastFlowMaximum == 4096,
    "flow blocked callback mismatch");

  client->closeTransport(Zquic::FrameType::ConnectionClose, 42);
  ZuCHECK(client->transportCloseCount == 1 &&
      client->lastCloseType == Zquic::FrameType::ConnectionClose &&
      client->lastCloseError == 42,
    "transport close callback mismatch");

  ZmRef<TestLink> path = new TestLink{&fixture.app, true};
  in_addr localIP;
  localIP.s_addr = htonl(0x0a000001);
  in_addr oldRemoteIP;
  oldRemoteIP.s_addr = htonl(0x0a000002);
  ZiSockAddr localAddr{ZiIP{localIP}, 4433};
  ZiSockAddr oldRemote{ZiIP{oldRemoteIP}, 50000};
#ifdef Zquic_DEBUG
  ZiSockAddr newRemote{ZiIP{oldRemoteIP}, 50001};
  ZiSockAddr failRemote{ZiIP{oldRemoteIP}, 50002};
  ZiSockAddr disabledRemote{ZiIP{oldRemoteIP}, 50003};
#endif
  path->initServerPath(localAddr, oldRemote);
#ifdef Zquic_DEBUG
  fixture.app.migrationMode(Zquic::MigrationMode::Disabled);
  path->observePathRx(localAddr, disabledRemote);
  Zquic::LinkDiag disabledDiag = path->diag();
  ZuCHECK(!path->migrationActive() &&
      disabledDiag.tx.pathRxObserved == 1 &&
      disabledDiag.tx.pathValidationDisabled == 1 &&
      disabledDiag.tx.migration.policyReject == 1 &&
      disabledDiag.tx.pathValidating == 0,
    "disabled migration mode did not ignore peer tuple change");
  fixture.app.migrationMode(Zquic::MigrationMode::Passive);
  path->observePathRx(localAddr, newRemote);
  Zquic::PathInfo candidate = path->candidatePathInfo();
  ZuCHECK(path->migrationActive() &&
      candidate.role == Zquic::PathRole::Candidate &&
      candidate.migrationState == Zquic::MigrationState::Validating &&
      candidate.reason == Zquic::MigrationReason::Passive &&
      !candidate.validated,
    "candidate path migration snapshot mismatch");
  ZuBSpan challenge = path->validatingChallenge();
  uint8_t response[Zquic::PathChallenge::Length]{};
  memcpy(response, challenge.data(), challenge.length());
  ZuCHECK(path->pathResponse(ZuBSpan{response, sizeof(response)}),
    "path response did not promote candidate path");
  ZuCHECK(path->migrationStartedCount == 1,
    "migration-start callback count mismatch");
  ZuCHECK(path->pathUpdateCount == 1,
    "path-update callback count mismatch");
  ZuCHECK(path->migrationPromotedCount == 1,
    "migration-promoted callback count mismatch");
  ZuCHECK(path->migrationCallbackOrder == "SUP",
    "migration callback order mismatch: ", path->migrationCallbackOrder);
  ZuCHECK(path->lastPathValidated,
    "path-update callback did not report a validated path");
  ZuCHECK(path->lastPathMaxUDP >= Zquic::MinUDPPayload,
    "path-update callback MTU mismatch");
  Zquic::MigrationDiag passiveDiag = path->diag().tx.migration;
  ZuCHECK(passiveDiag.requested == 1 &&
      passiveDiag.started == 1 &&
      passiveDiag.promoted == 1 &&
      passiveDiag.peerObserved == 1 &&
      passiveDiag.natRebind == 1,
    "passive migration diagnostic counters mismatch");

  ZmRef<TestLink> timeoutPath = new TestLink{&fixture.app, true};
  timeoutPath->initServerPath(localAddr, oldRemote);
  timeoutPath->observePath(localAddr, failRemote);
  timeoutPath->pathTimeout();
  ZuCHECK(timeoutPath->migrationStartedCount == 1 &&
      timeoutPath->migrationFailureCount == 1 &&
      timeoutPath->migrationCallbackOrder == "SF" &&
      timeoutPath->lastMigration.state == Zquic::MigrationState::Failed &&
      timeoutPath->lastMigration.reason == Zquic::MigrationReason::Timeout,
    "migration failure callback mismatch");
  Zquic::MigrationDiag timeoutDiag = timeoutPath->diag().tx.migration;
  ZuCHECK(timeoutDiag.requested == 1 &&
      timeoutDiag.started == 1 &&
      timeoutDiag.abandoned == 1 &&
      timeoutDiag.failed == 1 &&
      timeoutDiag.timeouts == 1 &&
      timeoutDiag.peerObserved == 0,
    "timeout migration diagnostic counters mismatch");
#endif
}

void testStopSendingOwnership()
{
  ZuTestScope(testStopSendingOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  auto stream = link->stream();
  uint8_t b[128];
  Zquic::Frame frame;
  int n = Zquic::FrameCodec::writeStopSending(
    b, sizeof(b), stream->id(), 9);
  ZuCHECK(parseFrame_(b, n, frame),
    "STOP_SENDING ownership setup failed");

  fixture.app.defer();
  ZuCHECK(link->receiveFrame(frame) == 0 && stream->stopReceived() &&
      fixture.app.txPending() && !stream->txCompleteCount &&
      stream->rxError() == Zquic::StreamError::None &&
      link->stopReceivedCount == 1,
    "STOP_SENDING did not defer completion to Tx");
  ZuCHECK(link->receiveFrame(frame) == 0 && fixture.app.txPending() &&
      !stream->txCompleteCount && link->stopReceivedCount == 1,
    "duplicate STOP_SENDING queued duplicate completion");

  fixture.app.enterTx();
  stream->reset(11);
  ZuCHECK(stream->txCompleteCount == 1 &&
      stream->txError() == Zquic::StreamError::Reset &&
      stream->txAppError() == 11,
    "racing reset did not complete stream on Tx");
  fixture.app.runTx();
  ZuCHECK(stream->txCompleteCount == 1 &&
      stream->txError() == Zquic::StreamError::Reset &&
      stream->txAppError() == 11,
    "STOP_SENDING/reset race completed Tx more than once");
  for (unsigned i = 0; fixture.app.txPending() && i < 4; ++i)
    fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && stream->txCompleteCount == 1,
    "STOP_SENDING/reset race did not drain queued Tx work exactly once");

  HubFixture resetFixture;
  ZmRef<TestLink> resetLink = new TestLink{&resetFixture.app};
  auto resetStream = resetLink->stream();
  resetFixture.app.defer();
  resetFixture.app.enterTx();
  resetStream->stop(13);
  ZuCHECK(resetFixture.app.rxPending() &&
      resetStream->txError() == Zquic::StreamError::None,
    "local STOP_SENDING did not defer its Rx cause");

  n = Zquic::FrameCodec::writeResetStream(
    b, sizeof(b), resetStream->id(), 17, 0);
  ZuCHECK(parseFrame_(b, n, frame),
    "RESET_STREAM ownership setup failed");
  resetFixture.app.enterRx();
  ZuCHECK(resetLink->receiveFrame(frame) == 0 &&
      resetStream->rxError() == Zquic::StreamError::Reset &&
      resetStream->rxAppError() == 17,
    "racing peer reset did not establish the Rx cause");
  resetFixture.app.runRx();
  ZuCHECK(resetStream->rxError() == Zquic::StreamError::Reset &&
      resetStream->rxAppError() == 17,
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

void testStreamRegistryOwnership()
{
  ZuTestScope(testStreamRegistryOwnership);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app, true};
  fixture.app.defer();
  fixture.app.enterRx();

  Zquic::Frame frame;
  unsigned used = 0;
  auto packet = streamPkt_(2, 0, "", true, frame, used);
  ZuCHECK(packet && link->receiveFrame(frame, packet) == 0,
    "peer stream ownership setup failed");
  ZmRef<TestStream> stream = link->lastDataStream;
  ZuCHECK(stream && link->findStream(2).ptr() == stream.ptr() &&
      fixture.app.txPending(),
    "Rx stream was not published locally before Tx notification");

  bool txPublished = false;
  for (unsigned i = 0;
      fixture.app.txPending() && !fixture.app.rxPending() && i < 8; ++i) {
    fixture.app.runTx();
    if (link->findTxStream(2).ptr() == stream.ptr()) txPublished = true;
  }
  ZuCHECK(txPublished && fixture.app.rxPending() &&
      link->findTxStream(2).ptr() == stream.ptr(),
    "Tx stream publication or retirement request was lost");

  fixture.app.runRx();
  ZuCHECK(!link->findStream(2) && fixture.app.txPending(),
    "Rx retirement did not remove the Rx-owned stream index");

  fixture.app.enterTx();
  for (unsigned i = 0; fixture.app.txPending() && i < 8; ++i)
    fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() && !link->findTxStream(2),
    "Tx retirement did not remove the Tx-owned stream index");
}

void testCxnIDFrameLifecycle()
{
  ZuTestScope(testCxnIDFrameLifecycle);

  HubFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken peerToken{"0123456789abcdef"};
  Zquic::Frame f;
  f.type = Zquic::FrameType::NewCxnID;
  f.value = 5;
  f.offset = 0;
  f.length = 9;
  f.payload = "peerCID09";
  f.resetToken = peerToken;
  ZuCHECK(link->receiveNewCxnID(f),
    "NEW_CONNECTION_ID frame was rejected");

  Zquic::CxnID peerCID;
  Zquic::ResetToken foundToken;
  ZuCHECK(link->peerCID(5, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID09"} && foundToken == peerToken,
    "NEW_CONNECTION_ID did not store peer CID and reset token");

  Zquic::ResetToken peerToken2{"1234567890abcdef"};
  f.value = 6;
  f.offset = 6;
  f.length = 9;
  f.payload = "peerCID10";
  f.resetToken = peerToken2;
  ZuCHECK(link->receiveNewCxnID(f) &&
      !link->peerCID(5, peerCID, foundToken) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "retired peer CID slot was not reused for NEW_CONNECTION_ID");

  ZuCHECK(link->receiveNewCxnID(f) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "duplicate NEW_CONNECTION_ID tuple was not deterministic");

  Zquic::Frame duplicate = f;
  duplicate.payload = "peerCID11";
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different CID was accepted");

  duplicate = f;
  duplicate.resetToken = peerToken;
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different token was accepted");

  duplicate = f;
  duplicate.value = 7;
  duplicate.payload = "peerCID11";
  duplicate.resetToken = peerToken2;
  ZuCHECK(!link->receiveNewCxnID(duplicate),
    "duplicate reset token for different peer CID was accepted");

  Zquic::CxnID localCID{"local003"};
  Zquic::ResetToken localToken{"fedcba9876543210"};
  ZuCHECK(link->addLocalCID(localCID, 3, localToken),
    "local CID setup failed");
  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireCxnID;
  retire.value = 3;
  ZuCHECK(link->receiveRetireCxnID(retire),
    "RETIRE_CONNECTION_ID frame was rejected");
	  ZuCHECK(link->localCIDRetired(3) && link->retiredCount == 1 &&
	      link->retiredSeq == 3 && link->retiredCID == localCID,
	    "RETIRE_CONNECTION_ID did not retire local CID and notify hook");

	  Zquic::CxnID selectedCID;
	  uint64_t selectedSeq = 0;
	  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
	  ZmRef<TestLink> cidLink = new TestLink{&fixture.app};
	  cidLink->setRuntimeCIDs(
	    Zquic::CxnID{"local000"}, Zquic::CxnID{"peer0000"});
	  ZuCHECK(cidLink->addPeerCID(
	      Zquic::CxnID{"peer0001"}, 1,
	      Zquic::ResetToken{"abcdef0123456789"}) &&
	      cidLink->chooseMigrationPeerCID(
		true, selectedCID, selectedSeq, reason) &&
	      selectedCID == Zquic::CxnID{"peer0001"} && selectedSeq == 1 &&
	      reason == Zquic::MigrationReason::None,
	    "active migration did not select spare peer CID");

	  ZmRef<TestLink> noSpare = new TestLink{&fixture.app};
	  noSpare->setRuntimeCIDs(
	    Zquic::CxnID{"local010"}, Zquic::CxnID{"peer0100"});
	  selectedCID = {};
	  selectedSeq = 0;
	  reason = Zquic::MigrationReason::None;
	  ZuCHECK(noSpare->chooseMigrationPeerCID(
	      false, selectedCID, selectedSeq, reason) &&
	      selectedCID == Zquic::CxnID{"peer0100"} && selectedSeq == 0,
	    "passive migration did not reuse active peer CID");
	  ZuCHECK(!noSpare->chooseMigrationPeerCID(
	      true, selectedCID, selectedSeq, reason) &&
	      reason == Zquic::MigrationReason::NoPeerCID,
	    "active migration without spare peer CID was accepted");

	  ZmRef<TestLink> zeroCID = new TestLink{&fixture.app};
	  selectedCID = {};
	  selectedSeq = 0;
	  reason = Zquic::MigrationReason::None;
	  ZuCHECK(zeroCID->chooseMigrationPeerCID(
	      true, selectedCID, selectedSeq, reason) &&
	      !selectedCID && selectedSeq == 0 &&
	      reason == Zquic::MigrationReason::None,
	    "zero-length peer CID migration was rejected");
	}

void testActiveMigrationAPI()
{
  ZuTestScope(testActiveMigrationAPI);

  HubFixture fixture;
  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr newRemote{ZiIP{"127.0.0.1"}, 50001};
  ZiSockAddr otherRemote{ZiIP{"127.0.0.1"}, 50002};
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;

  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(localAddr, oldRemote);
  link->setRuntimeCIDs(
    Zquic::CxnID{"localA00"}, Zquic::CxnID{"peerA000"});
  link->installAppDataKeys(rx, tx, Zquic::CxnID{"localA00"});
  Zquic::TransportParams peerParams;
  peerParams.disableActiveMigration = false;
  link->setPeerTransportParams(peerParams);
  ZuCHECK(link->addPeerCID(
      Zquic::CxnID{"peerA001"}, 1,
      Zquic::ResetToken{"0123456789abcdef"}),
    "active migration spare CID setup failed");

  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = newRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(link->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::None,
    "active migration preparation failed");
  Zquic::PathInfo candidate = link->candidatePathInfo();
  ZuCHECK(link->migrationActive() &&
      candidate.remote == newRemote &&
      candidate.peerCIDSequence == 1 &&
      candidate.migrationState == Zquic::MigrationState::Requested,
    "active migration candidate snapshot mismatch");

  Zquic::MigrationParams second = params;
  second.remote = otherRemote;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!link->prepareActiveMigration(second, reason) &&
      reason == Zquic::MigrationReason::Validation &&
      link->candidatePathInfo().remote == newRemote,
    "concurrent active migration request replaced candidate");

  ZuCHECK(link->activateActiveMigration() &&
      link->migrationStartedCount == 1 &&
      link->candidatePathInfo().migrationState ==
	Zquic::MigrationState::Validating,
    "active migration activation callback/state mismatch");
  Zquic::Frame challenge;
  ZuCHECK(link->buildActiveMigrationChallenge(challenge) &&
      challenge.type == Zquic::FrameType::PathChallenge &&
      challenge.payload.length() == Zquic::PathChallenge::Length,
    "active migration PATH_CHALLENGE was not built");
  ZuCHECK(link->migrationActive(),
    "active migration candidate cleared after PATH_CHALLENGE build");
  uint8_t response[Zquic::PathChallenge::Length];
  ZuBSpan challengeData = link->validatingChallenge();
  ZuCHECK(challengeData.length() == sizeof(response),
    "active migration challenge snapshot was empty");
  if (challengeData.length() != sizeof(response)) return;
  for (unsigned i = 0; i < sizeof(response); ++i)
    response[i] = challengeData[i];
  ZuCHECK(link->pathResponse(ZuBSpan{response, sizeof(response)}),
    "active migration matching PATH_RESPONSE was rejected");
  ZuCHECK(!link->migrationActive(),
    "active migration candidate was not cleared after promotion");
  ZuCHECK(link->migrationPromotedCount == 1,
    "active migration promotion callback was not emitted");
  ZuCHECK(link->pathUpdateCount == 1,
    "active migration path update callback was not emitted");
  ZuCHECK(link->migrationCallbackOrder == "SUP",
    "active migration promotion callback order mismatch");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag activeDiag = link->diag().tx.migration;
  ZuCHECK(activeDiag.requested == 2 &&
      activeDiag.started == 1 &&
      activeDiag.promoted == 1 &&
      activeDiag.policyReject == 0 &&
      activeDiag.noPeerCID == 0,
    "active migration diagnostic counters mismatch");
#endif

  ZmRef<TestLink> disabled = new TestLink{&fixture.app};
  disabled->initClientPath(localAddr, oldRemote);
  disabled->setRuntimeCIDs(
    Zquic::CxnID{"localB00"}, Zquic::CxnID{"peerB000"});
  disabled->installAppDataKeys(rx, tx, Zquic::CxnID{"localB00"});
  peerParams.disableActiveMigration = true;
  disabled->setPeerTransportParams(peerParams);
  ZuCHECK(disabled->addPeerCID(
      Zquic::CxnID{"peerB001"}, 1,
      Zquic::ResetToken{"1123456789abcdef"}),
    "disabled active migration spare CID setup failed");
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!disabled->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::PeerDisabled &&
      !disabled->migrationActive(),
    "active migration ignored peer disable_active_migration");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag disabledMigrationDiag =
    disabled->diag().tx.migration;
  ZuCHECK(disabledMigrationDiag.requested == 1 &&
      disabledMigrationDiag.policyReject == 1 &&
      disabledMigrationDiag.started == 0,
    "peer-disabled migration diagnostic counters mismatch");
#endif

  ZmRef<TestLink> noSpare = new TestLink{&fixture.app};
  noSpare->initClientPath(localAddr, oldRemote);
  noSpare->setRuntimeCIDs(
    Zquic::CxnID{"localC00"}, Zquic::CxnID{"peerC000"});
  noSpare->installAppDataKeys(rx, tx, Zquic::CxnID{"localC00"});
  peerParams.disableActiveMigration = false;
  noSpare->setPeerTransportParams(peerParams);
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!noSpare->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::NoPeerCID &&
      !noSpare->migrationActive() &&
      noSpare->migrationStartedCount == 0,
    "active migration without spare CID advanced to validation");
#ifdef Zquic_DEBUG
  Zquic::MigrationDiag noSpareDiag = noSpare->diag().tx.migration;
  ZuCHECK(noSpareDiag.requested == 1 &&
      noSpareDiag.started == 1 &&
      noSpareDiag.noPeerCID == 1 &&
      noSpareDiag.failed == 1 &&
      noSpareDiag.promoted == 0,
    "no-CID migration diagnostic counters mismatch");
#endif
}

void testCxnIDMigrationOverlap()
{
  ZuTestScope(testCxnIDMigrationOverlap);

  HubFixture fixture;
  fixture.app.activeMigration(true);
  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr newRemote{ZiIP{"127.0.0.1"}, 50001};
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  link->initClientPath(localAddr, oldRemote);
  link->setRuntimeCIDs(
    Zquic::CxnID{"localA00"}, Zquic::CxnID{"peerA000"});
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;
  link->installAppDataKeys(rx, tx, Zquic::CxnID{"localA00"});
  Zquic::TransportParams peerParams;
  link->setPeerTransportParams(peerParams);
  ZuCHECK(link->addPeerCID(
      Zquic::CxnID{"peerA001"}, 1,
      Zquic::ResetToken{"0123456789abcdef"}),
    "migration-overlap spare CID setup failed");

  fixture.app.defer();
  fixture.app.enterTx();
  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = newRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(link->prepareActiveMigration(params, reason) &&
      fixture.app.rxPending() && !fixture.app.txPending(),
    "migration-overlap selection was not posted to Rx");
  fixture.app.runRx();
  ZuCHECK(!fixture.app.rxPending() && fixture.app.txPending(),
    "migration-overlap selection was not returned to Tx");
  fixture.app.runTx();
  ZuCHECK(!fixture.app.txPending() &&
      link->candidatePathInfo().peerCIDSequence == 1,
    "migration-overlap selected the wrong peer CID");
  ZuCHECK(link->activateActiveMigration(),
    "migration-overlap candidate activation failed");
  Zquic::Frame challenge;
  ZuCHECK(link->buildActiveMigrationChallenge(challenge),
    "migration-overlap challenge setup failed");

  fixture.app.enterRx();
  Zquic::Frame replacement;
  replacement.type = Zquic::FrameType::NewCxnID;
  replacement.value = 2;
  replacement.offset = 2;
  replacement.length = 8;
  replacement.payload = "peerA002";
  replacement.resetToken = Zquic::ResetToken{"1234567890abcdef"};
  ZuCHECK(link->receiveNewCxnID(replacement) &&
      !link->peerCIDActive(1) && link->peerCIDActive(2),
    "migration-overlap did not retire and replace the selected CID");

  uint8_t response[Zquic::PathChallenge::Length];
  ZuBSpan challengeData = link->validatingChallenge();
  ZuCHECK(challengeData.length() == sizeof(response),
    "migration-overlap challenge snapshot was empty");
  if (challengeData.length() != sizeof(response)) return;
  memcpy(response, challengeData.data(), sizeof(response));
  fixture.app.enterTx();
  ZuCHECK(link->pathResponse(ZuBSpan{response, sizeof(response)}) &&
      fixture.app.rxPending() && !link->migrationPromotedCount,
    "migration-overlap promotion did not defer CID binding to Rx");
  fixture.app.runRx();
  ZuCHECK(fixture.app.txPending() && !link->migrationPromotedCount,
    "migration-overlap CID rejection was not returned to Tx");
  fixture.app.runTx();
  ZuCHECK(!link->migrationActive() && link->migrationFailureCount == 1 &&
      !link->migrationPromotedCount && !link->pathUpdateCount &&
      link->activeTxPeerCID() == Zquic::CxnID{"peerA000"},
    "retired migration CID was promoted or reverted the active CID");
  ZuCHECK(fixture.app.rxPending(),
    "retired migration CID reservation was not released on Rx");
  fixture.app.runRx();

  Zquic::CxnID selected;
  uint64_t sequence = 0;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(link->chooseMigrationPeerCID(
      true, selected, sequence, reason) &&
      selected == Zquic::CxnID{"peerA002"} && sequence == 2,
    "replacement peer CID was not selectable after overlap failure");
}

void testMigrationQLogEvents()
{
  ZuTestScope(testMigrationQLogEvents);

#ifdef Zquic_DEBUG
  Zi::Path qlogPath = testPath_("ZquicAPIMigrationQLog.sqlog");
  ZiFile::remove(qlogPath);
  ZquicLogParams qlogParams;
  qlogParams.enabled(true).path(qlogPath).ringSize(1<<15);

  HubFixture fixture;
  ZuCHECK(ZquicLogger::init(
      fixture.app.qlogTrace(), qlogParams, Zquic::Vantage::Unknown),
    "migration runtime qlog init failed");
  ZquicLogger::start();

  ZiSockAddr localAddr{ZiIP{"127.0.0.1"}, 4433};
  ZiSockAddr localAddr2{ZiIP{"127.0.0.1"}, 4434};
  ZiSockAddr oldRemote{ZiIP{"127.0.0.1"}, 50000};
  ZiSockAddr activeRemote{ZiIP{"127.0.0.1"}, 50001};
  ZiSockAddr passiveRemote{ZiIP{"127.0.0.1"}, 50002};
  ZiSockAddr timeoutRemote{ZiIP{"127.0.0.1"}, 50003};
  ZiSockAddr mismatchRemote{ZiIP{"127.0.0.1"}, 50004};
  Zquic::TrafficSecret rx;
  Zquic::TrafficSecret tx;
  Zquic::TransportParams peerParams;
  peerParams.disableActiveMigration = false;

  auto initActive = [&](ZuCSpan localCID, ZuCSpan peerCID,
      ZuCSpan spareCID, ZuCSpan token) {
    ZmRef<TestLink> link = new TestLink{&fixture.app};
    link->initClientPath(localAddr, oldRemote);
    link->setRuntimeCIDs(Zquic::CxnID{localCID}, Zquic::CxnID{peerCID});
    link->installAppDataKeys(rx, tx, Zquic::CxnID{localCID});
    link->setPeerTransportParams(peerParams);
    if (spareCID)
      link->addPeerCID(Zquic::CxnID{spareCID}, 1, Zquic::ResetToken{token});
    return link;
  };

  ZmRef<TestLink> active =
    initActive("localA00", "peerA000", "peerA001", "0123456789abcdef");
  Zquic::MigrationParams params;
  params.local = localAddr;
  params.remote = activeRemote;
  Zquic::MigrationReason::T reason = Zquic::MigrationReason::None;
  ZuCHECK(active->prepareActiveMigration(params, reason) &&
      active->activateActiveMigration(),
    "qlog active migration setup failed");
  Zquic::Frame frame;
  ZuCHECK(active->buildActiveMigrationChallenge(frame),
    "qlog active migration challenge build failed");
  uint8_t response[Zquic::PathChallenge::Length]{};
  ZuBSpan challenge = active->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  ZuCHECK(active->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog active migration response failed");

  fixture.app.migrationMode(Zquic::MigrationMode::Passive);
  ZmRef<TestLink> passive = new TestLink{&fixture.app, true};
  passive->initServerPath(localAddr, oldRemote);
  passive->observePathRx(localAddr, passiveRemote);
  challenge = passive->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  ZuCHECK(passive->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog passive migration response failed");

  ZmRef<TestLink> timeout = new TestLink{&fixture.app, true};
  timeout->initServerPath(localAddr, oldRemote);
  timeout->observePath(localAddr, timeoutRemote);
  timeout->pathTimeout();

  ZmRef<TestLink> mismatch = new TestLink{&fixture.app, true};
  mismatch->initServerPath(localAddr, oldRemote);
  mismatch->observePath(localAddr, mismatchRemote);
  challenge = mismatch->validatingChallenge();
  memcpy(response, challenge.data(), sizeof(response));
  response[0] ^= 1;
  ZuCHECK(!mismatch->pathResponse(ZuBSpan{response, sizeof(response)}),
    "qlog mismatch PATH_RESPONSE was accepted");
  mismatch->pathTimeout();

  ZmRef<TestLink> disabled =
    initActive("localB00", "peerB000", "peerB001", "1123456789abcdef");
  peerParams.disableActiveMigration = true;
  disabled->setPeerTransportParams(peerParams);
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!disabled->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::PeerDisabled,
    "qlog peer-disabled migration was accepted");

  peerParams.disableActiveMigration = false;
  ZmRef<TestLink> noCID = initActive("localC00", "peerC000", {}, {});
  reason = Zquic::MigrationReason::None;
  ZuCHECK(!noCID->prepareActiveMigration(params, reason) &&
      reason == Zquic::MigrationReason::NoPeerCID,
    "qlog no-CID migration was accepted");

  ZmRef<TestLink> rebind =
    initActive("localD00", "peerD000", "peerD001", "2123456789abcdef");
  Zquic::MigrationParams rebindParams;
  rebindParams.local = localAddr2;
  rebindParams.remote = oldRemote;
  rebindParams.rebindLocal = true;
  rebindParams.closeOnFailure = true;
  reason = Zquic::MigrationReason::None;
  ZuCHECK(rebind->prepareActiveMigration(rebindParams, reason),
    "qlog rebind migration setup failed");
  rebind->activeRebindStart();
  rebind->activeRebindFail(Zquic::MigrationReason::Endpoint);

  closeQLog_(fixture.app.qlogTrace());
  ZquicLogger::stop();
  ZquicLogDiag diag = ZquicLogger::diag();
  ZquicLogger::final(fixture.app.qlogTrace());

  ZuCHECK(diag.recordsEnqueued >= 24,
    "migration runtime qlog enqueue mismatch");
  ZuCHECK(diag.recordsWritten >= 25,
    "migration runtime qlog write mismatch");
  ZuCHECK(diag.writerFailures == 0,
    "migration runtime qlog writer failure");

  ZtString<> qlog = readFile_(qlogPath);
  ZuCHECK(qlog, "migration runtime qlog output missing");
  ZuCHECK(parseJSONSeq_(qlog) >= 25,
    "migration runtime qlog JSON-SEQ parse failed");
  ZuCHECK(qlog.find<"zquic:migration_updated">() >= 0,
    "runtime migration qlog event missing");
  ZuCHECK(qlog.find<"\"action\":\"requested\"">() >= 0 &&
      qlog.find<"\"action\":\"started\"">() >= 0 &&
      qlog.find<"\"action\":\"cid_selected\"">() >= 0 &&
      qlog.find<"\"action\":\"challenge_queued\"">() >= 0 &&
      qlog.find<"\"action\":\"response_matched\"">() >= 0 &&
      qlog.find<"\"action\":\"response_mismatch\"">() >= 0 &&
      qlog.find<"\"action\":\"promoted\"">() >= 0 &&
      qlog.find<"\"action\":\"abandoned\"">() >= 0 &&
      qlog.find<"\"action\":\"failed\"">() >= 0 &&
      qlog.find<"\"action\":\"rejected\"">() >= 0 &&
      qlog.find<"\"action\":\"cid_unavailable\"">() >= 0 &&
      qlog.find<"\"action\":\"rebind_start\"">() >= 0 &&
      qlog.find<"\"action\":\"rebind_fail\"">() >= 0 &&
      qlog.find<"\"action\":\"closed\"">() >= 0,
    "runtime migration qlog action coverage missing");
  ZuCHECK(qlog.find<"\"reason\":\"peer_disabled\"">() >= 0 &&
      qlog.find<"\"reason\":\"no_peer_cid\"">() >= 0 &&
      qlog.find<"\"reason\":\"validation\"">() >= 0 &&
      qlog.find<"\"reason\":\"timeout\"">() >= 0 &&
      qlog.find<"\"reason\":\"endpoint\"">() >= 0,
    "runtime migration qlog reason coverage missing");
  ZuCHECK(qlog.find<"\"local_rebind\":true">() >= 0 &&
      qlog.find<"\"close_on_failure\":true">() >= 0 &&
      qlog.find<"\"candidate_remote\"">() >= 0 &&
      qlog.find<"\"peer_cid_sequence\":1">() >= 0,
    "runtime migration qlog correlation fields missing");

  auto ordered = [&](ZuCSpan a0, ZuCSpan a1, ZuCSpan a2 = {},
      ZuCSpan a3 = {}, ZuCSpan a4 = {}, ZuCSpan a5 = {}) {
    unsigned off = 0;
    auto next = [&](ZuCSpan action) {
      if (!action) return true;
      int pos = ZuCSpan{qlog.data() + off, qlog.length() - off}.find(action);
      if (pos < 0) return false;
      off += unsigned(pos) + action.length();
      return true;
    };
    return next(a0) && next(a1) && next(a2) && next(a3) &&
      next(a4) && next(a5);
  };
  ZuCHECK(ordered(
      "\"action\":\"requested\"", "\"action\":\"started\"",
      "\"action\":\"cid_selected\"", "\"action\":\"challenge_queued\"",
      "\"action\":\"response_matched\"", "\"action\":\"promoted\""),
    "active success qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"response_mismatch\"", "\"action\":\"abandoned\"",
      "\"action\":\"failed\""),
    "validation failure qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"cid_unavailable\"", "\"action\":\"failed\""),
    "no-CID qlog stream order mismatch");
  ZuCHECK(ordered(
      "\"action\":\"rebind_start\"", "\"action\":\"rebind_fail\"",
      "\"action\":\"failed\"", "\"action\":\"closed\""),
    "rebind failure qlog stream order mismatch");
#endif
}

void testInitValidate()
{
  ZuTestScope(testInitValidate);

  ZiLog::init("ZquicAPITest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  {
    unsigned errors = 0;
    HubApp hub;
    ZuCHECK(!hub.init(
	Zquic::HubParams(nullptr, "1", "2")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "null multiplexer init unexpectedly succeeded");
    ZuCHECK(errors == 1, "null multiplexer error callback mismatch");
  }
  {
    unsigned errors = 0;
    ClientApp app;
    ZuCHECK(!app.init(
	Zquic::ClientParams(nullptr, "1", "2")
	  .certPath("client.pem")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "client cert/key mismatch unexpectedly succeeded");
    ZuCHECK(errors == 1, "client cert/key error callback mismatch");
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

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) {
    ZiLog::stop();
    return;
  }

  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "9", "2")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Rx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Rx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "1", "9")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "3", "3")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "same Rx/Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "same Rx/Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    HubApp app;
    ZuCHECK(!app.init(
	Zquic::HubParams(&mx, "3", "4")
	  .asyncThread("1")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "I/O async thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "I/O async thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    ServerApp app;
    ZuCHECK(!app.init(
	Zquic::ServerParams(&mx, "3", "4")
	  .certPath("server.pem")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "server cert/key mismatch unexpectedly succeeded");
    ZuCHECK(errors == 1, "server cert/key error callback mismatch");
  }
  {
    ClientApp app;
    bool ok = app.init(
      Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
    ZuCHECK(ok, "valid client init failed");
    ZuCHECK(app.alpn_count() == 1, "client ALPN count mismatch");
    if (ok) app.final();
  }
  {
    ServerApp app;
    bool ok = app.init(
      Zquic::ServerParams(&mx, {}, {})
	.certPath("server.pem")
	.keyPath("server.key")
	.alpn(ZuSpan<ZuCSpan>{"zquic-test"}));
    ZuCHECK(ok, "valid server default-thread init failed");
    if (ok) app.final();
  }

  mx.stop();
  ZiLog::stop();
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZiTestResidue::init("ZquicAPITest");
#ifdef Zquic_DEBUG
  g_residue = ZiTestResidue::dir("qlog");
#endif

  ZuTestMain();
  ZuTestCall(testParams);
  ZuTestCall(testStreamShape);
  ZuTestCall(testAlignedSurfaceShape);
  ZuTestCall(testLinkStateOwnership);
  ZuTestCall(testStreamLimitOwnership);
  ZuTestCall(testStatelessResetDetection);
  ZuTestCall(testApplicationCallbacks);
  ZuTestCall(testStopSendingOwnership);
  ZuTestCall(testStreamRegistryOwnership);
  ZuTestCall(testCxnIDFrameLifecycle);
  ZuTestCall(testActiveMigrationAPI);
  ZuTestCall(testCxnIDMigrationOverlap);
  ZuTestCall(testMigrationQLogEvents);
  ZuTestCall(testInitValidate);
}
