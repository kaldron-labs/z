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
  using Base::Base;

  void streamed(ZmRef<TestStream>) { }
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
  uint64_t retiredSeq = 0;
  Zquic::CxnID retiredCID;
  unsigned retiredCount = 0;
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
  TestLink client{&fixture.app, false};
  TestLink server{&fixture.app, true};

  auto c0 = client.stream();
  auto c1 = client.stream(Zi::StreamType::Simplex);
  auto s0 = server.stream();

  ZuCHECK(c0 && c0->id() == 0, "client bidi stream ID mismatch");
  ZuCHECK(c1 && c1->id() == 2, "client uni stream ID mismatch");
  ZuCHECK(s0 && s0->id() == 1, "server bidi stream ID mismatch");
  ZuCHECK(c0->link() == &client && c1->link() == &client,
    "client stream owner link mismatch");
  ZuCHECK(s0->link() == &server, "server stream owner link mismatch");
  ZuCHECK(client.streamCount() == 2, "client stream count mismatch");
  ZuCHECK(client.findStream(0) == c0, "stream hash lookup failed");

  {
    auto tx = c0->txStream_();
    tx << "abc" << Zi::flush();
  }
  ZuCHECK(c0->txBytes() == 3, "stream Tx byte accounting mismatch");
  c0->fin();
  c0->reset(7);
  c0->stop(9);
  ZuCHECK(c0->finSent(), "FIN state was not recorded");

  client.close(42);
  ZuCHECK(client.closed() && client.closeError() == 42,
    "link close state mismatch");
}

void testAlignedSurfaceShape()
{
  ZuTestScope(testAlignedSurfaceShape);

  ClientShapeApp clientApp;
  ClientShapeLink client{&clientApp};
  auto c0 = client.stream();
  ZuCHECK(c0 && c0->id() == 0 && c0->link() == &client && !client.isServer(),
    "client aligned link/stream shape mismatch");

  ServerShapeApp serverApp;
  ServerShapeLink server{&serverApp};
  auto s0 = server.stream();
  ZuCHECK(s0 && s0->id() == 1 && s0->link() == &server && server.isServer(),
    "server aligned link/stream shape mismatch");
}

void testStatelessResetDetection()
{
  ZuTestScope(testStatelessResetDetection);

  EngineFixture fixture;
  TestLink link{&fixture.app};
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
  ZuCHECK(!link.checkStatelessReset(datagram) && !link.draining(),
    "link accepted stateless reset before peer token was known");
  link.setPeerResetToken(token);
  ZuCHECK(link.checkStatelessReset(datagram) && link.draining(),
    "link did not enter draining on matching stateless reset");
}

void testConnectionIDFrameLifecycle()
{
  ZuTestScope(testConnectionIDFrameLifecycle);

  EngineFixture fixture;
  TestLink link{&fixture.app};
  Zquic::ResetToken peerToken{"0123456789abcdef"};
  Zquic::Frame f;
  f.type = Zquic::FrameType::NewConnectionID;
  f.value = 5;
  f.offset = 0;
  f.length = 9;
  f.payload = "peerCID09";
  f.resetToken = peerToken;
  ZuCHECK(link.receiveNewConnectionID(f),
    "NEW_CONNECTION_ID frame was rejected");

  Zquic::CxnID peerCID;
  Zquic::ResetToken foundToken;
  ZuCHECK(link.peerCID(5, peerCID, foundToken) &&
      peerCID == Zquic::CxnID{"peerCID09"} && foundToken == peerToken,
    "NEW_CONNECTION_ID did not store peer CID and reset token");

  Zquic::CxnID localCID{"local003"};
  Zquic::ResetToken localToken{"fedcba9876543210"};
  ZuCHECK(link.addLocalCID(localCID, 3, localToken),
    "local CID setup failed");
  Zquic::Frame retire;
  retire.type = Zquic::FrameType::RetireConnectionID;
  retire.value = 3;
  ZuCHECK(link.receiveRetireConnectionID(retire),
    "RETIRE_CONNECTION_ID frame was rejected");
  ZuCHECK(link.localCIDRetired(3) && link.retiredCount == 1 &&
      link.retiredSeq == 3 && link.retiredCID == localCID,
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
  ZuCHECK(sink.openUDP(
      &mx, Zquic::PathMode::ServerUnconnected,
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
    sink.closeUDP();
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
  sink.closeUDP();
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
  ZuTestCall(testConnectionIDFrameLifecycle);
  ZuTestCall(testCliLinkUDPConnect);
  ZuTestCall(testInitValidation);
}
