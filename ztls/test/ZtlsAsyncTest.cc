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

  Zi::IOBufAllocator *rxBufAllocator = nullptr;
  Zi::IOBufAllocator *txBufAllocator = nullptr;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.multiplex()) return params;
    if (!params.rxAllocator()) {
      rxBufAllocator = Link::rxAllocator(params.multiplex());
      params.rxAllocator(rxBufAllocator);
    }
    if (!params.txAllocator()) {
      txBufAllocator = Link::txAllocator(params.multiplex());
      params.txAllocator(txBufAllocator);
    }
    if (!params.txPacketAllocator()) {
      params.txPacketAllocator(params.multiplex()->template allocator<
	Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514, ZiIOBuf_DefltAlign,
	ZuStringT<"Ztls.Test.Packet">>(1024));
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

  Zi::IOBufAllocator *rxBufAllocator = nullptr;
  Zi::IOBufAllocator *txBufAllocator = nullptr;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.multiplex()) return params;
    if (!params.rxAllocator()) {
      rxBufAllocator = Link::rxAllocator(params.multiplex());
      params.rxAllocator(rxBufAllocator);
    }
    if (!params.txAllocator()) {
      txBufAllocator = Link::txAllocator(params.multiplex());
      params.txAllocator(txBufAllocator);
    }
    if (!params.txPacketAllocator()) {
      params.txPacketAllocator(params.multiplex()->template allocator<
	Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514, ZiIOBuf_DefltAlign,
	ZuStringT<"Ztls.Test.Packet">>(1024));
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
  Zi::IOBufAllocator *rxBufAllocator = nullptr;
  Zi::IOBufAllocator *txBufAllocator = nullptr;

  template <typename Params>
  Params buffers(Params params) {
    if (!params.multiplex()) return params;
    if (!params.rxAllocator()) {
      rxBufAllocator = params.multiplex()->template allocator<Zi::IOBufType::RxTLS, ZiRxQueue::Node,
	Ztls_::RxRecordCapacity, ZiIOBuf_Align, ZuStringT<"Ztls.RxBuf">>();
      params.rxAllocator(rxBufAllocator);
    }
    if (!params.txAllocator()) {
      txBufAllocator = params.multiplex()->template allocator<Zi::IOBufType::TxTLS, ZiTxBuf,
	Ztls_::TxRecordCapacity, ZiIOBuf_Align, ZuStringT<"Ztls.TxBuf">>();
      params.txAllocator(txBufAllocator);
    }
    if (!params.txPacketAllocator()) {
      params.txPacketAllocator(params.multiplex()->template allocator<
	Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514, ZiIOBuf_DefltAlign,
	ZuStringT<"Ztls.Test.Packet">>(1024));
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

  {
    ClientApp app;
    Ztls::Hub<ClientApp> &hub = app;
    bool initialized = hub.init(app.buffers(Ztls::HubParams(&mx, "3", "4").alpn(alpn)));
    ZuCHECK(initialized, "client stream fixture hub init failed");
    if (initialized) {
      Zi::IOBufAllocator *output = mx.allocator<Zi::IOBufType::TxTLS, ZiTxBuf, ZiIOBuf_DefltSize * 2>();
      bool prepared = hub.allocator(output);
      ZuCHECK(prepared, "client stream fixture allocator registration failed");
      ZuCHECK(!hub.allocator(app.rxBufAllocator), "RxTLS allocator accepted for Tx");
      if (prepared) {
	auto buf = output->allocBuf();
	ZuCHECK(buf && buf->allocator() == output && output->mx == &mx,
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
      bool prepared = hub.allocator(app.txBufAllocator);
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

  HubApp app;
  bool initialized = app.init(
    Ztls::HubParams(&mx, "3", "4").alpn(alpn).asyncThread("5"));
  ZuCHECK(initialized, "cold async hub init failed");

  bool mxStarted = mx.start();
  ZuCHECK(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) {
    ZiLog::stop();
    return;
  }

  if (initialized) {
    bool started = app.start();
    ZuCHECK(started, "async hub start failed");
    if (started) ZuCHECK(app.stop(), "async hub stop failed");
    app.final();
  }

  mx.stop();
  ZiLog::stop();
}

void testPreparedAllocators()
{
  ZuTestScope(testPreparedAllocators);
  ZiMultiplex mx, other;
  auto rx = mx.allocator<Zi::IOBufType::RxTLS, ZiRxQueue::Node, Ztls_::RxRecordCapacity>();
  auto tx = mx.allocator<Zi::IOBufType::TxTLS, ZiTxBuf, Ztls_::TxRecordCapacity>();
  auto txPacket = mx.allocator<Zi::IOBufType::TxTCP, Ztls::TxPacket, 1514>();
  auto socket = mx.allocator<Zi::IOBufType::RxTCP, ZiRxQueue::Node, Ztls_::RxPacketCapacity>();
  auto wrong = mx.allocator<Zi::IOBufType::App, ZiRxQueue::Node>();
  auto otherSocket = other.allocator<Zi::IOBufType::RxTCP>();
  auto params = [&mx, rx, tx, txPacket]() {
    return Ztls::HubParams{&mx}.rxAllocator(rx).txAllocator(tx).txPacketAllocator(txPacket)
      .errorFn(Ztls::ErrorFn{[](ZeException) { }});
  };

  {
    auto small = mx.allocator<Zi::IOBufType::RxTLS, ZiRxQueue::Node, 1024>();
    HubApp app;
    auto p = params();
    p.rxAllocator(small).socketRxAllocator(socket);
    ZuCheck(!app.init(ZuMv(p)));
  }
  for (auto invalid : {wrong, otherSocket}) {
    HubApp app;
    auto p = params();
    p.socketRxAllocator(invalid);
    ZuCheck(!app.init(ZuMv(p)));
  }
  {
    HubApp app;
    auto p = params();
    p.socketRxAllocator(socket);
    bool initialized = app.init(ZuMv(p));
    ZuCheck(initialized);
    if (initialized) {
      ZuCheck(app.socketRxAllocator() == socket);
      app.final();
    }
  }

  // A later origin reuses all four roles without registering on a running Mx.
  bool started = mx.start();
  ZuCheck(started);
  if (!started) return;
  {
    HubApp app;
    auto p = params();
    p.socketRxAllocator(socket);
    bool initialized = app.init(ZuMv(p));
    ZuCheck(initialized);
    if (initialized) {
      ZuCheck(app.rxAllocator() == rx && app.txAllocator() == tx);
      ZuCheck(app.txPacketAllocator() == txPacket &&
	tx->type == Zi::IOBufType::TxTLS && txPacket->type == Zi::IOBufType::TxTCP);
      ZuCheck(app.socketRxAllocator() == socket);
      app.final();
    }
  }
  {
    HubApp app;
    ZuCheck(!app.init(params()));
  }
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);

  ZuTestMain();
  ZuTestCall(testInitValidation);
  ZuTestCall(testPreparedAllocators);
}
