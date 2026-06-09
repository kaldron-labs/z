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

struct EngineApp : public Zquic::Engine<EngineApp> { };
struct ClientApp : public Zquic::Client<ClientApp> { };
struct ServerApp : public Zquic::Server<ServerApp> { };

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

  void streamed(ZmRef<ClientShapeStream>) { }
  void connectFailed(bool) { ++failures; }
  void disconnected() { ++disconnects; }

  ZmAtomic<unsigned> failures = 0;
  ZmAtomic<unsigned> disconnects = 0;
};

struct ServerShapeApp : public Zquic::Server<ServerShapeApp> { };
struct ServerShapeLink;
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

  void streamed(ZmRef<ServerShapeStream>) { }
};

using TestCxn = Zquic::Cxn<TestLink, TestLink *>;

struct TestLink :
    public Zquic::Link<
      EngineApp, TestLink, StreamTxBufAlloc,
      TestCxn, TestLink *, TestStream> {
  using Base = Zquic::Link<
    EngineApp, TestLink, StreamTxBufAlloc,
    TestCxn, TestLink *, TestStream>;
  using Base::Base;

  void streamed(ZmRef<TestStream>) { }
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

  EngineApp app;
  TestLink client{&app, false};
  TestLink server{&app, true};

  auto c0 = client.stream();
  auto c1 = client.stream(Zquic::StreamType::Uni);
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

  using ClientPhysical = Zquic::CliCxn<ClientShapeLink>;
  using ServerListener = Zquic::SrvCxn<ServerShapeApp>;
  (void)sizeof(ClientPhysical *);
  (void)sizeof(ServerListener *);

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
  ZuCHECK(sink.openUDP(
      &mx, Zquic::PathMode::ServerUnconnected,
      ZiIP("127.0.0.1"), 0, ZiIP{}, 0,
      Zquic::Endpoint::DatagramFn{},
      Zquic::Endpoint::ReadyFn{[&sinkReady](Zquic::Endpoint *) {
	sinkReady = true;
      }}), "CliLink UDP sink open failed");
  ZuCHECK(waitUntil([&sink, &sinkReady]() {
      return sinkReady && sink.listening() && sink.local().port();
    }), "CliLink UDP sink did not become ready");

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
      link->cxnDiag().endpointCxnAllocs == 1 &&
      link->cxnDiag().packetRxBufAllocs == 1 &&
      !link->cxnDiag().openFailures &&
      !link->failures,
    "CliLink UDP diagnostics mismatch");

  link->connect(Zquic::Host{"127.0.0.1"}, sink.local().port());
  ZuCHECK(waitUntil([&link]() {
      return link->udpReady() && link->udpReadyCount() == 2;
    }), "CliLink UDP reconnect did not become ready");
  ZuCHECK(link->cxnDiag().endpointCxnAllocs == 2 &&
      link->cxnDiag().packetRxBufAllocs == 2 &&
      !link->cxnDiag().openFailures &&
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
  ZuTestCall(testCliLinkUDPConnect);
  ZuTestCall(testInitValidation);
}
