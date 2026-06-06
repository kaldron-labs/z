//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

namespace {

using StreamTxBufAlloc = Zquic::StreamTxBufAlloc<>;

struct TestStream :
    public Zquic::Stream<TestStream, StreamTxBufAlloc> {
  using Base = Zquic::Stream<TestStream, StreamTxBufAlloc>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct EngineApp : public Zquic::Engine<EngineApp> { };
struct ClientApp : public Zquic::Client<ClientApp> { };
struct ServerApp : public Zquic::Server<ServerApp> { };

struct TestLink;
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
  ZuCHECK(client.streamCount() == 2, "client stream count mismatch");
  ZuCHECK(client.findStream(0) == c0, "stream hash lookup failed");

  {
    auto tx = c0->txStream();
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

void testInitValidation()
{
  ZuTestScope(testInitValidation);

  ZiLog::init("ZquicAPITest");
  ZiLog::level(0);
  ZiLog::start();

  EngineApp engine;
  ZuCHECK(!engine.init(Zquic::EngineParams(nullptr, "1", "2")),
    "null multiplexer init unexpectedly succeeded");

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
    EngineApp app;
    ZuCHECK(!app.init(Zquic::EngineParams(&mx, "9", "2")),
      "invalid Rx thread unexpectedly succeeded");
  }
  {
    EngineApp app;
    ZuCHECK(!app.init(Zquic::EngineParams(&mx, "1", "9")),
      "invalid Tx thread unexpectedly succeeded");
  }
  {
    EngineApp app;
    ZuCHECK(!app.init(Zquic::EngineParams(&mx, "3", "3")),
      "same Rx/Tx thread unexpectedly succeeded");
  }
  {
    EngineApp app;
    ZuCHECK(!app.init(
      Zquic::EngineParams(&mx, "3", "4").asyncThread("1")),
      "I/O async thread unexpectedly succeeded");
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
    bool ok = app.init(Zquic::ServerParams(&mx, {}, {}).alpn(
      ZuSpan<ZuCSpan>{"zquic-test"}));
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
  ZuTestCall(testInitValidation);
}
