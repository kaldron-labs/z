//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Ztls.hh>

#include <iostream>

namespace {

struct ClientApp : public Ztls::Client<ClientApp> {
  using BufAlloc = Ztls::BufAlloc<>;
  struct Link : public Ztls::CliLink<ClientApp, Link, BufAlloc> {
    Link(ClientApp *app) : Ztls::CliLink<ClientApp, Link, BufAlloc>(app) { }
    void connected(const char *, int) { }
    void disconnected() { }
    void connectFailed(bool) { }
    int process(Ztls::RxStream &) { return 0; }
  };
};

struct ServerApp : public Ztls::Server<ServerApp> {
  using BufAlloc = Ztls::BufAlloc<>;
  struct Link : public Ztls::SrvLink<ServerApp, Link, BufAlloc> {
    Link(ServerApp *app) : Ztls::SrvLink<ServerApp, Link, BufAlloc>(app) { }
    void connected(const char *, int) { }
    void disconnected() { }
    int process(Ztls::RxStream &) { return 0; }
  };
  ZiIP localIP() const { return ZiIP("127.0.0.1"); }
  unsigned localPort() const { return 0; }
  Link::Cxn *accepted(const ZiCxnInfo &ci) {
    return new Link::Cxn(new Link(this), ci);
  }
};

struct EngineApp : public Ztls::Engine<EngineApp> { };

bool expect_fail(bool ok, const char *label)
{
  if (!ok) return true;
  std::cerr << "error: " << label << " unexpectedly succeeded\n";
  return false;
}

} // namespace

int main()
{
  ZiLog::init("ZtlsAsyncTest");
  ZiLog::level(0);
  ZiLog::start();

  ZuCSpan alpn[] = { "ztls-test" };

  auto clientParams = Ztls::ClientParams(nullptr, "1", alpn)
    .caPath("ca.pem")
    .certPath("client.pem")
    .keyPath("client.key")
    .asyncThread("4")
    .errorFn(Ztls::defaultErrorFn());
  (void)clientParams;

  auto serverParams = Ztls::ServerParams(nullptr, "1", alpn)
    .caPath("ca.pem")
    .certPath("server.pem")
    .keyPath("server.key")
    .asyncThread("4")
    .mTLS(true)
    .cacheTimeout(7)
    .errorFn(Ztls::defaultErrorFn());
  (void)serverParams;

  ClientApp client;
  if (!expect_fail(client.init(Ztls::ClientParams(nullptr, "1", alpn)),
	"null multiplexer init"))
    return 1;
  if (!expect_fail(client.init(
	Ztls::ClientParams(nullptr, "1", alpn).certPath("client.pem")),
	"client cert/key XOR init"))
    return 1;

  ServerApp server;
  if (!expect_fail(server.init(Ztls::ServerParams(nullptr, "1", alpn)),
	"missing server cert/key init"))
    return 1;

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(5)
	    .thread(1, [](auto &t) { t.isolated(1); })
	    .thread(2, [](auto &t) { t.isolated(1); })
	    .thread(3, [](auto &t) { t.isolated(1); })
	    .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  if (!mx.start()) {
    std::cerr << "error: ZiMultiplex start failed\n";
    return 1;
  }

  {
    EngineApp app;
    if (!expect_fail(app.init(Ztls::EngineParams(&mx, "9", alpn)),
	  "invalid TLS thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!expect_fail(app.init(
	  Ztls::EngineParams(&mx, "3", alpn).asyncThread("9")),
	  "invalid async thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!expect_fail(app.init(
	  Ztls::EngineParams(&mx, "3", alpn).asyncThread("3")),
	  "TLS async thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!expect_fail(app.init(
	  Ztls::EngineParams(&mx, "3", alpn).asyncThread("1")),
	  "rx async thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!expect_fail(app.init(
	  Ztls::EngineParams(&mx, "3", alpn).asyncThread("2")),
	  "tx async thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!expect_fail(app.init(
	  Ztls::EngineParams(&mx, "3", alpn).asyncThread("5")),
	  "non-isolated async thread"))
      return 1;
  }
  {
    EngineApp app;
    if (!app.init(Ztls::EngineParams(&mx, "3", alpn).asyncThread("4"))) {
      std::cerr << "error: valid async engine init failed\n";
      return 1;
    }
    app.final();
  }

  mx.stop();
  ZiLog::stop();

  return 0;
}
