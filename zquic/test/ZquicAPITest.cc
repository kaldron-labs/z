//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>
#include <zlib/ZquicEndpoint.hh>

using namespace ZuTestUtil;

namespace {

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct TestLink;
struct TestStream :
    public Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc> {
  using Base = Zquic::Stream<TestLink, TestStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct EngineApp : public Zquic::Engine<EngineApp> {
  bool rxInvoked() const { return true; }
  bool txInvoked() const { return true; }
  template <typename L> void rxRun(L l) { l(); }
  template <typename L> void rxInvoke(L l) { l(); }
  template <typename L> void txRun(L l) { l(); }
  template <typename L> void txInvoke(L l) { l(); }
  template <typename O, typename L> void txInvoke(O *, L l) { l(); }
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

  void connected(Zi::Connected) { }
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

  void connected(Zi::Connected) { }
  void streamed(ZmRef<ClientShapeStream>) { }
  void connectFailed(bool) { ++failures; }
  void disconnected() { ++disconnects; }

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

  void connected(Zi::Connected) { }
  void streamed(ZmRef<ServerShapeStream>) { }
};

struct TestLink :
    public Zquic::Link<
      EngineApp, TestLink, StreamTxBufAlloc,
      TestStream> {
  using Base = Zquic::Link<
    EngineApp, TestLink, StreamTxBufAlloc,
    TestStream>;
  TestLink(EngineApp *app, bool isServer = false) : Base{app, isServer} {
    Base::configureLocalTransportParams_(app);
  }

  void streamed(ZmRef<TestStream>) { }
  void streamOpen(ZmRef<TestStream> stream, bool local) {
    lastOpened = ZuMv(stream);
    local ? ++localStreamOpens : ++peerStreamOpens;
    ++streamOpenCount;
  }
  void streamData(
    ZmRef<TestStream> stream, uint64_t offset, ZuCSpan payload, bool fin) {
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
    Zi::StreamType::T streamType, uint64_t maximum) {
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
  void statelessReset() { ++statelessResetCount; }
  void pathUpdate(
    const ZiSockAddr &local, const ZiSockAddr &remote,
    bool validated, unsigned maxUDP) {
    lastPathLocal = local;
    lastPathRemote = remote;
    lastPathValidated = validated;
    lastPathMaxUDP = maxUDP;
    ++pathUpdateCount;
  }
  void migrationFailure(const ZiSockAddr &local, const ZiSockAddr &remote) {
    lastMigrationLocal = local;
    lastMigrationRemote = remote;
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
  bool receiveNewConnectionID(const Zquic::Frame &frame) {
    return Base::receiveNewConnectionID_(frame);
  }
  bool receiveRetireConnectionID(const Zquic::Frame &frame) {
    return Base::receiveRetireConnectionID_(frame);
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
  bool localCIDRetired(uint64_t sequence) const {
    auto cid = Base::localCID_(sequence);
    return cid && cid->state == Zquic::CxnState::Retired;
  }
  void retiredLocalCID_(uint64_t sequence, const Zquic::CxnID &id) {
    retiredSeq = sequence;
    retiredCID = id;
    ++retiredCount;
  }
	  bool checkStatelessReset(ZuCSpan datagram) {
	    return Base::checkStatelessReset_(datagram);
	  }
	  bool draining() const { return Base::runtimeDraining_(); }
  void dataBlockedForTest(uint64_t maximum) {
    Base::dataBlocked_(maximum);
  }
  void closeTransportForTest(Zquic::FrameType::T type, uint64_t errorCode) {
    Base::closeRuntime_(errorCode);
    Base::transportClose_(type, errorCode);
  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote) {
    Base::initServerPathTx_(ZuMv(local), ZuMv(remote));
  }
#ifdef Zquic_DEBUG
  void observePath(ZiSockAddr local, ZiSockAddr remote) {
    Base::startPathValidationForTest_(ZuMv(local), ZuMv(remote));
  }
  ZuCSpan validatingChallenge() const {
    return Base::validatingChallenge_();
  }
#endif
  bool pathResponse(ZuCSpan data) { return Base::onPathResponse_(data); }
  void pathTimeout() { Base::pathExpired_(); }
  void flushTx_() { ++txFlushQueued; }
  void flushTx_(ZiSockAddr) { ++txFlushQueued; }
  void queueTxFlush_() { ++txFlushQueued; }
  void queueTxFlush_(ZiSockAddr) { ++txFlushQueued; }
  Zquic::RuntimeDiag runtimeDiag() const { return Base::runtimeDiag_(); }

  ZmRef<TestStream> lastOpened;
  ZmRef<TestStream> lastDataStream;
  ZmRef<TestStream> lastResetStream;
  ZmRef<TestStream> lastStopStream;
  ZiSockAddr lastPathLocal;
  ZiSockAddr lastPathRemote;
  ZiSockAddr lastMigrationLocal;
  ZiSockAddr lastMigrationRemote;
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
  Zi::StreamType::T lastFlowStreamType = Zi::StreamType::Duplex;
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

struct EngineFixture {
  EngineFixture() : mx{mxParams_()} {
    ZiAssert(mx.start(), "Zquic", (),
      "API test multiplexer start failed", return);
    ZiAssert(app.init(Zquic::EngineParams(&mx, "3", "4")),
      "Zquic", (), "API test engine init failed", return);
  }
  ~EngineFixture() {
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
  EngineApp	app;
};

static bool parseFrame_(const uint8_t *b, int n, Zquic::Frame &frame)
{
  unsigned used = 0;
  return n > 0 && !Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(n)}, frame, used) &&
    used == unsigned(n);
}

static ZmRef<ZiIOBuf> streamPkt_(
  uint64_t id, uint64_t offset, ZuCSpan payload, bool fin,
  Zquic::Frame &frame, unsigned &used)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  int n = Zquic::FrameCodec::writeStream(
    packet->data_(), packet->size, id, offset, payload, fin);
  if (n <= 0) return nullptr;
  packet->skip = 0;
  packet->length = unsigned(n);
  if (Zquic::FrameCodec::parse(packet->cspan(), frame, used) ||
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
    .maxStreamsBidi(16)
    .maxStreamsUni(4)
    .maxUDP(1200)
    .alpn(alpn)
    .errorFn(Zquic::defaultErrorFn());
  (void)clientParams;

  uint8_t h3[] = { 'h', '3' };
  ptls_iovec_t iov[] = { ptls_iovec_init(h3, sizeof(h3)) };
  auto serverParams = Zquic::ServerParams(nullptr, "1", "2")
    .caPath("ca.pem")
    .certPath("server.pem")
    .keyPath("server.key")
    .alpn(ZuSpan<const ptls_iovec_t>(iov, 1))
    .errorFn(Zquic::defaultErrorFn());
  (void)serverParams;
}

void testStreamShape()
{
  ZuTestScope(testStreamShape);

  EngineFixture fixture;
  ZmRef<TestLink> client = new TestLink{&fixture.app, false};
  ZmRef<TestLink> server = new TestLink{&fixture.app, true};

  auto c0 = client->stream();
  auto c1 = client->stream(Zi::StreamType::Simplex);
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

  client->close(42);
  ZuCHECK(client->closed() && client->closeError() == 42,
    "link close state mismatch");
}

void testAlignedSurfaceShape()
{
  ZuTestScope(testAlignedSurfaceShape);

  ZiMultiplex mx{EngineFixture::mxParams_()};
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
  auto c0 = client->stream();
  ZuCHECK(c0 && c0->id() == 0 && c0->link() == client.ptr() &&
      !client->isServer(),
    "client aligned link/stream shape mismatch");
  ZuCHECK(client->runtimeDiag().unhandledAppEvents,
    "default client stream-open hook was not visible in diagnostics");

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
  auto s0 = server->stream();
  ZuCHECK(s0 && s0->id() == 1 && s0->link() == server.ptr() &&
      server->isServer(),
    "server aligned link/stream shape mismatch");

  serverApp.final();
  clientApp.final();
  mx.stop();
}

void testStatelessResetDetection()
{
  ZuTestScope(testStatelessResetDetection);

  EngineFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken token{"0123456789abcdef"};
  uint8_t packet[64] = {};
  memset(packet, 0xa5, sizeof(packet));
  packet[0] = 0x65;
  memcpy(
    packet + sizeof(packet) - Zquic::ResetToken::Length,
    token.data(), Zquic::ResetToken::Length);
  ZuCSpan datagram{reinterpret_cast<const char *>(packet), sizeof(packet)};

  Zquic::ResetToken decoded;
  ZuCHECK(!Zquic::StatelessReset::decode(decoded, datagram) &&
      decoded == token,
    "stateless reset token decode mismatch");
  ZuCHECK(Zquic::StatelessReset::verify(datagram, token),
    "stateless reset token verify failed");
  ZuCHECK(!link->checkStatelessReset(datagram) && !link->draining(),
    "link accepted stateless reset before peer token was known");
  link->setPeerResetToken(token);
  ZuCHECK(link->checkStatelessReset(datagram) && link->draining(),
    "link did not enter draining on matching stateless reset");
  ZuCHECK(link->statelessResetCount == 1,
    "stateless reset callback did not fire");
}

void testApplicationCallbacks()
{
  ZuTestScope(testApplicationCallbacks);

  EngineFixture fixture;
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

  client->closeTransportForTest(Zquic::FrameType::ConnectionClose, 42);
  ZuCHECK(client->transportCloseCount == 1 &&
      client->lastCloseType == Zquic::FrameType::ConnectionClose &&
      client->lastCloseError == 42,
    "transport close callback mismatch");

  ZmRef<TestLink> path = new TestLink{&fixture.app, true};
  ZiSockAddr localAddr{ZiIP{0x0a000001}, 4433};
  ZiSockAddr oldRemote{ZiIP{0x0a000002}, 50000};
#ifdef Zquic_DEBUG
  ZiSockAddr newRemote{ZiIP{0x0a000002}, 50001};
  ZiSockAddr failRemote{ZiIP{0x0a000002}, 50002};
#endif
  path->initServerPath(localAddr, oldRemote);
#ifdef Zquic_DEBUG
  path->observePath(localAddr, newRemote);
  ZuCSpan challenge = path->validatingChallenge();
  uint8_t response[Zquic::PathChallenge::Length]{};
  memcpy(response, challenge.data(), challenge.length());
  ZuCHECK(path->pathResponse(ZuCSpan{response, sizeof(response)}) &&
      path->pathUpdateCount == 1 &&
      path->lastPathValidated &&
      path->lastPathMaxUDP >= Zquic::MinUDPPayload,
    "path update callback mismatch");

  path->observePath(localAddr, failRemote);
  path->pathTimeout();
  ZuCHECK(path->migrationFailureCount == 1,
    "migration failure callback mismatch");
#endif
}

void testConnectionIDFrameLifecycle()
{
  ZuTestScope(testConnectionIDFrameLifecycle);

  EngineFixture fixture;
  ZmRef<TestLink> link = new TestLink{&fixture.app};
  Zquic::ResetToken peerToken{"0123456789abcdef"};
  Zquic::Frame f;
  f.type = Zquic::FrameType::NewConnectionID;
  f.value = 5;
  f.offset = 0;
  f.length = 9;
  f.payload = "peerCID09";
  f.resetToken = peerToken;
  ZuCHECK(link->receiveNewConnectionID(f),
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
  ZuCHECK(link->receiveNewConnectionID(f) &&
      !link->peerCID(5, peerCID, foundToken) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "retired peer CID slot was not reused for NEW_CONNECTION_ID");

  ZuCHECK(link->receiveNewConnectionID(f) &&
      link->peerCID(6, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID10"} &&
      foundToken == peerToken2,
    "duplicate NEW_CONNECTION_ID tuple was not deterministic");

  Zquic::Frame duplicate = f;
  duplicate.payload = "peerCID11";
  ZuCHECK(!link->receiveNewConnectionID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different CID was accepted");

  duplicate = f;
  duplicate.resetToken = peerToken;
  ZuCHECK(!link->receiveNewConnectionID(duplicate),
    "duplicate NEW_CONNECTION_ID sequence with different token was accepted");

  duplicate = f;
  duplicate.value = 7;
  duplicate.payload = "peerCID11";
  duplicate.resetToken = peerToken2;
  ZuCHECK(!link->receiveNewConnectionID(duplicate),
    "duplicate reset token for different peer CID was accepted");

  Zquic::CxnID localCID{"local003"};
  Zquic::ResetToken localToken{"fedcba9876543210"};
  ZuCHECK(link->addLocalCID(localCID, 3, localToken),
    "local CID setup failed");
  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireConnectionID;
  retire.value = 3;
  ZuCHECK(link->receiveRetireConnectionID(retire),
    "RETIRE_CONNECTION_ID frame was rejected");
  ZuCHECK(link->localCIDRetired(3) && link->retiredCount == 1 &&
      link->retiredSeq == 3 && link->retiredCID == localCID,
    "RETIRE_CONNECTION_ID did not retire local CID and notify hook");
}

void testCliLinkUDPConnect()
{
  ZuTestScope(testCliLinkUDPConnect);

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
  ZuCHECK(mxStarted, "CliLink UDP multiplexer start failed");
  if (!mxStarted) return;

  Zquic::Endpoint sink;
  bool sinkReady = false;
  bool sinkFailed = false;
  ZuCHECK(sink.init(&mx), "CliLink UDP sink init failed");
  ZuCHECK(sink.openUDP(
      Zquic::PathMode::ServerUnconnected,
      ZiIP("127.0.0.1"), 0, ZiIP{}, 0,
      Zquic::Endpoint::DatagramFn{},
      Zquic::Endpoint::ReadyFn{[&sinkReady](Zquic::Endpoint *) {
	sinkReady = true;
      }},
      Zquic::Endpoint::FailFn{[&sinkFailed](bool) {
	sinkFailed = true;
      }}), "CliLink UDP sink open failed");
  bool sinkOpened = waitUntil([&sink, &sinkReady, &sinkFailed]() {
      return sinkFailed || (sinkReady && sink.listening() && sink.local().port());
    });
  if (sinkFailed) {
    ZuCHECK(!sink.listening() && sink.diag().failures,
      "CliLink UDP sink did not fail cleanly");
    ZmSemaphore sinkClosed;
    sink.closeUDP(Zquic::Endpoint::CloseFn{[&sinkClosed]() {
      sinkClosed.post();
    }});
    sinkClosed.wait();
    mx.stop();
    return;
  }
  ZuCHECK(sinkOpened, "CliLink UDP sink did not become ready");

  ClientShapeApp app;
  bool appOK = app.init(
    Zquic::ClientParams(&mx, "3", "4").alpn(ZuSpan<ZuCSpan>{"h3"}));
  ZuCHECK(appOK, "CliLink UDP client init failed");
  if (!appOK) {
    mx.stop();
    return;
  }

  ZmRef<ClientShapeLink> link = new ClientShapeLink{&app};
  link->connect(Zquic::Host{"127.0.0.1"}, sink.local().port());
  ZuCHECK(waitUntil([&link]() { return link->udpReady(); }),
    "CliLink UDP socket did not become ready");
  ZuCHECK(link->udpReadyCount() == 1 &&
      !link->cxnDiag().failures &&
      !link->failures,
    "CliLink UDP diagnostics mismatch");

  link->connect(Zquic::Host{"127.0.0.1"}, sink.local().port());
  ZuCHECK(waitUntil([&link]() {
      return link->udpReady() && link->udpReadyCount() == 2;
    }), "CliLink UDP reconnect did not become ready");
  ZuCHECK(!link->cxnDiag().failures &&
      !link->failures,
    "CliLink UDP reconnect diagnostics mismatch");

  link->disconnect();
  ZuCHECK(waitUntil([&link]() { return !link->udpReady(); }),
    "CliLink UDP socket did not disconnect");

  link = nullptr;
  ZmSemaphore sinkClosed;
  sink.closeUDP(Zquic::Endpoint::CloseFn{[&sinkClosed]() {
    sinkClosed.post();
  }});
  sinkClosed.wait();
  app.final();
  mx.stop();
}

void testInitValidation()
{
  ZuTestScope(testInitValidation);

  ZiLog::init("ZquicAPITest");
  ZiLog::level(0);
  ZiLog::start();

  {
    unsigned errors = 0;
    EngineApp engine;
    ZuCHECK(!engine.init(
	Zquic::EngineParams(nullptr, "1", "2")
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
    EngineApp app;
    ZuCHECK(!app.init(
	Zquic::EngineParams(&mx, "9", "2")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Rx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Rx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    EngineApp app;
    ZuCHECK(!app.init(
	Zquic::EngineParams(&mx, "1", "9")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "invalid Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "invalid Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    EngineApp app;
    ZuCHECK(!app.init(
	Zquic::EngineParams(&mx, "3", "3")
	  .errorFn(Zquic::ErrorFn{[&errors](ZeException) { ++errors; }})),
      "same Rx/Tx thread unexpectedly succeeded");
    ZuCHECK(errors == 1, "same Rx/Tx thread error callback mismatch");
  }
  {
    unsigned errors = 0;
    EngineApp app;
    ZuCHECK(!app.init(
	Zquic::EngineParams(&mx, "3", "4")
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

  ZuTestMain();
  ZuTestCall(testParams);
  ZuTestCall(testStreamShape);
  ZuTestCall(testAlignedSurfaceShape);
  ZuTestCall(testStatelessResetDetection);
  ZuTestCall(testApplicationCallbacks);
  ZuTestCall(testConnectionIDFrameLifecycle);
  ZuTestCall(testCliLinkUDPConnect);
  ZuTestCall(testInitValidation);
}
