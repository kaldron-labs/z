//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Ztls.hh>

using namespace ZuTestUtil;

namespace {

struct ClientApp : public Ztls::Client<ClientApp> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<>;

  using RxPolicy = Zi::RxTLSBufAllocator<
    typename RxBufAlloc::Buf, RxBufAlloc::Size, RxBufAlloc::MaxSize>;
  using TxPolicy = Zi::TxTLSBufAllocator<
    typename TxBufAlloc::Buf, TxBufAlloc::Size, TxBufAlloc::MaxSize>;
  ZmRef<RxPolicy> rxBufAllocator;
  ZmRef<TxPolicy> txBufAllocator;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.rxAllocator()) {
      rxBufAllocator = new RxPolicy{};
      params.rxAllocator(rxBufAllocator.ptr());
    }
    if (!params.txAllocator()) {
      txBufAllocator = new TxPolicy{};
      params.txAllocator(txBufAllocator.ptr());
    }
    return params;
  }

  template <typename Params>
  bool init(Params params) {
    return Ztls::Client<ClientApp>::init(buffers(ZuMv(params)));
  }

  struct Link : public Ztls::CliLink<ClientApp, Link, RxBufAlloc, TxBufAlloc> {
    using Base = Ztls::CliLink<ClientApp, Link, RxBufAlloc, TxBufAlloc>;
    Link(ClientApp *app) : Base(app) { }
    void connected(Ztls::Connected) { }
    void disconnected(bool) { }
    void connectFailed(bool) { }
    int process(Ztls::RxStream &) { return 0; }
  };
};

struct ServerApp : public Ztls::Server<ServerApp> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<>;

  using RxPolicy = Zi::RxTLSBufAllocator<
    typename RxBufAlloc::Buf, RxBufAlloc::Size, RxBufAlloc::MaxSize>;
  using TxPolicy = Zi::TxTLSBufAllocator<
    typename TxBufAlloc::Buf, TxBufAlloc::Size, TxBufAlloc::MaxSize>;
  ZmRef<RxPolicy> rxBufAllocator;
  ZmRef<TxPolicy> txBufAllocator;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.rxAllocator()) {
      rxBufAllocator = new RxPolicy{};
      params.rxAllocator(rxBufAllocator.ptr());
    }
    if (!params.txAllocator()) {
      txBufAllocator = new TxPolicy{};
      params.txAllocator(txBufAllocator.ptr());
    }
    return params;
  }

  template <typename Params>
  bool init(Params params) {
    return Ztls::Server<ServerApp>::init(buffers(ZuMv(params)));
  }

  struct Link : public Ztls::SrvLink<ServerApp, Link, RxBufAlloc, TxBufAlloc> {
    using Base = Ztls::SrvLink<ServerApp, Link, RxBufAlloc, TxBufAlloc>;
    Link(ServerApp *app) : Base(app) { }
    void connected(Ztls::Connected) { }
    void disconnected(bool) { }
    int process(Ztls::RxStream &) { return 0; }
  };
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  unsigned localPort() const { return 0; }
  Link::Cxn *accepted(const ZiCxnInfo &ci) {
    return new Link::Cxn(new Link(this), ci);
  }
};

struct HubApp : public Ztls::Hub<HubApp> {
  using RxBufAlloc = Ztls::RxBufAlloc<>;
  using TxBufAlloc = Ztls::TxBufAlloc<>;
  using RxPolicy = Zi::RxTLSBufAllocator<
    typename RxBufAlloc::Buf, RxBufAlloc::Size, RxBufAlloc::MaxSize>;
  using TxPolicy = Zi::TxTLSBufAllocator<
    typename TxBufAlloc::Buf, TxBufAlloc::Size, TxBufAlloc::MaxSize>;
  ZmRef<RxPolicy> rxBufAllocator;
  ZmRef<TxPolicy> txBufAllocator;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.rxAllocator()) {
      rxBufAllocator = new RxPolicy{};
      params.rxAllocator(rxBufAllocator.ptr());
    }
    if (!params.txAllocator()) {
      txBufAllocator = new TxPolicy{};
      params.txAllocator(txBufAllocator.ptr());
    }
    return params;
  }

  template <typename Params>
  bool init(Params params) {
    return Ztls::Hub<HubApp>::init(buffers(ZuMv(params)));
  }

};

} // namespace

void testInitValidation()
{
  ZuTestScope(testInitValidation);

  ZiLog::init("ZtlsAsyncTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZuCSpan alpn[] = { "ztls-test" };

  auto clientParams = Ztls::ClientParams(nullptr, "1", "2").hub(
    [&alpn](auto &p) {
      p.alpn(alpn).caPath("ca.pem")
	.certPath("client.pem")
	.keyPath("client.key")
	.asyncThread("4")
	.errorFn(Ztls::defaultErrorFn());
    });
  (void)clientParams;

  auto serverParams = Ztls::ServerParams(nullptr, "1", "2").alpn(alpn)
    .caPath("ca.pem")
    .certPath("server.pem")
    .keyPath("server.key")
    .asyncThread("4")
    .mTLS(true)
    .cacheTimeout(7)
    .errorFn(Ztls::defaultErrorFn());
  (void)serverParams;

  ClientApp client;
  {
    ClientApp::Link link(&client);
    ZuCHECK(link.stream() == &link, "TLS client bidi stream is not link");
  }
  ZuCHECK(!client.init(Ztls::ClientParams(nullptr, "1", "2").hub(
      [&alpn](auto &p) { p.alpn(alpn); })),
    "null multiplexer init unexpectedly succeeded");
  ZuCHECK(!client.init(Ztls::ClientParams(nullptr, "1", "2").hub(
      [&alpn](auto &p) { p.alpn(alpn).certPath("client.pem"); })),
    "client cert/key XOR init unexpectedly succeeded");

  ServerApp server;
  {
    ServerApp::Link link(&server);
    ZuCHECK(link.stream() == &link, "TLS server bidi stream is not link");
  }
  ZuCHECK(!server.init(Ztls::ServerParams(nullptr, "1", "2").alpn(alpn)),
    "missing server cert/key init unexpectedly succeeded");

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(6)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); })
	    .thread(5, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) {
    ZiLog::stop();
    return;
  }

  {
    ClientApp app;
    Ztls::Hub<ClientApp> &hub = app;
    bool initialized = hub.init(app.buffers(Ztls::HubParams(&mx, "3", "4").alpn(alpn)));
    ZuCHECK(initialized, "client stream fixture hub init failed");
    if (initialized) {
      using OutputPolicy = Zi::TxTLSBufAllocator<ZiTxBuf, ZiIOBuf_DefltSize * 2>;
      ZmRef<OutputPolicy> output = new OutputPolicy{};
      bool prepared = hub.allocator(output.ptr());
      ZuCHECK(prepared, "client stream fixture allocator registration failed");
      ZuCHECK(!hub.allocator(app.rxBufAllocator.ptr()), "RxTLS allocator accepted for Tx");
      if (prepared) {
	auto buf = output->allocBuf();
	ZuCHECK(buf->allocator == output.ptr() && output->mx() == &mx,
	  "additional TLS output lost its registered allocator");
	ClientApp::Link link(&app);
	ZuCHECK(link.stream() == &link, "TLS client bidi stream is not link");
      }
      hub.final();
    }
  }
  {
    ServerApp app;
    Ztls::Hub<ServerApp> &hub = app;
    bool initialized = hub.init(app.buffers(Ztls::HubParams(&mx, "3", "4").alpn(alpn)));
    ZuCHECK(initialized, "server stream fixture hub init failed");
    if (initialized) {
      bool prepared = hub.allocator(app.txBufAllocator.ptr());
      ZuCHECK(prepared, "server stream fixture allocator registration failed");
      if (prepared) {
	ServerApp::Link link(&app);
	ZuCHECK(link.stream() == &link, "TLS server bidi stream is not link");
      }
      hub.final();
    }
  }

  {
    HubApp app;
    ZuCHECK(!app.init(Ztls::HubParams(&mx, "9", "4").alpn(alpn)),
      "invalid TLS Rx thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("9")),
      "invalid async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("3")),
      "TLS Rx async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("4")),
      "TLS Tx async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("1")),
      "rx async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("2")),
      "tx async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    ZuCHECK(!app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("6")),
      "non-isolated async thread unexpectedly succeeded");
  }
  {
    HubApp app;
    bool ok = app.init(
      Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("5"));
    ZuCHECK(ok, "valid async hub init failed");
    if (ok) app.final();
  }

  mx.stop();
  ZiLog::stop();
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZuTestMain();
  ZuTestCall(testInitValidation);
}
