//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <net/if.h>
#include <iostream>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtString.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Zutcp.hh>

ZuDerive(AppRequest, (ZtString<ZtStringHeapID<"Zutcp.Example.Request">>));

class App : public Ztcp::Client<App> {
public:
  class Cxn;
  using Link = Cxn;
  App(ZuCSpan request) { m_request << request; }
  ZuCSpan request() const { return m_request; }
  void ready() { m_ready.post(); }
  void waitReady() { m_ready.wait(); }
  void done() { m_done.post(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcp", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
  }
  bool error() const { return m_error.load_(); }
private:
  ZmSemaphore m_ready, m_done;
  AppRequest m_request;
  ZmAtomic<unsigned> m_error = 0;
};

class App::Cxn : public ZmNHeap<Cxn>, public Ztcp::CliLink<App, Cxn> {
friend inline ZuStringT<"Zutcp.Example.CliCxn"> ZmNHeapID(Cxn *);
  using Base = Ztcp::CliLink<App, Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(App *app) : Base{app} { }
  void connected(Ztcp::Connected) {
    std::cerr << "connection established\n" << std::flush;
    auto tx = txStream();
    tx << app()->request();
    if (!tx.flush()) { app()->setError("request send failed"); disconnect(); }
    app()->ready();
  }
  int process(Ztcp::RxStream &rx) {
    auto length = rx.length();
    while (rx) {
      auto part = rx.span();
      std::cout << ZuCSpan{part.data(), part.length()};
      m_received += part.length();
      rx.advance(part.length());
    }
    if (m_received >= app()->request().length() + 6) {
      if (m_received != app()->request().length() + 6)
	app()->setError("response length mismatch");
      m_responseSeen = true;
      std::cout << '\n' << std::flush;
    }
    return length ? 1 : 0;
  }
  void disconnected(bool) {
    if (!m_responseSeen) app()->setError("disconnected before response");
    app()->done();
  }
  void connectFailed(bool) { app()->setError("connect failed"); app()->ready(); app()->done(); }
private:
  uint64_t m_received = 0;
  bool m_responseSeen = false;
};

static void usage()
{
  std::cerr << "Usage: zutcp INTERFACE SERVER PORT [REQUEST]\n" << std::flush;
  exit(1);
}

static ZuUnion<Zpkt::Nif *, ZeException> openNif(
  Zpkt::Env *env, ZiMultiplex *mx, ZuCSpan name, unsigned sender, unsigned spin = 1000)
{
  using Node = ZfCf::AnyNode;
  auto root = ZfCf::newNode<Node::Object>(nullptr);
  auto interfaces = ZfCf::newNode<Node::Object>(root.ptr());
  auto nif = ZfCf::newNode<Node::Object>(interfaces.ptr());
  auto value = [](Node *parent, ZuCSpan key, auto value) {
    Node::String text;
    text << value;
    parent->data<Node::Object>().push(Node::Field{
      key, ZfCf::newNode<Node::String>(parent, ZuMv(text))});
  };
  value(root.ptr(), "module", ZUTCP_XDP_MODULE);
  value(nif.ptr(), "txSid", sender);
  value(nif.ptr(), "spin", spin);
  interfaces->data.push(Node::Field{name, ZuMv(nif)});
  root->data.push(Node::Field{"interfaces", ZuMv(interfaces)});
  try { env->init(root.ptr()); }
  catch (ZeException &e) { return ZuMv(e); }
  return env->nif(mx, name);
}

int main(int argc, char **argv)
{
  if (argc < 4 || argc > 5) usage();
  unsigned interface = if_nametoindex(argv[1]);
  unsigned port = ZuBox<unsigned>(argv[3]);
  if (!interface || !port || port > 65535) usage();
  ZuCSpan request = argc == 5 ? argv[4] : "zutcp-example";

  ZiLog::init("zutcp");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zutcp::MxPlugin> instance = new Zutcp::MxPlugin;
  ZiMultiplex mx{ZiMxParams{}.rxThread(1).txThread(2).scheduler([](auto &s) {
    s.nThreads(5);
    for (unsigned i = 1; i <= 5; ++i) s.thread(i, [](auto &t) { t.isolated(true); });
  }), instance};
  auto opened = openNif(env, &mx, argv[1], 5);
  if (opened.is<ZeException>()) {
    ZiLogEvent(ZuMv(opened).p<ZeException>());
    ZiLog::stop();
    return 1;
  }
  ZmRef<Zpkt::Nif> xdp = opened.p<Zpkt::Nif *>();
  try { instance->init(&mx, env, ZuMv(xdp), {}); }
  catch (ZeException &e) {
    xdp->final();
    if (auto error = xdp->error()) ZiLogEvent(ZuMv(error));
    ZiLogEvent(ZuMv(e));
    ZiLog::stop();
    return 1;
  }
  App app{request};
  Ztcp::ClientParams params{&mx, "3", "4"};
  params.rxAllocator(App::Cxn::rxAllocator(&mx)).txAllocator(App::Cxn::txAllocator(&mx));
  bool initialized = app.init(ZuMv(params));
  bool started = initialized && mx.start();
  if (!started) app.setError("client initialization/start failed");
  else {
    ZmRef<App::Cxn> link = new App::Cxn{&app};
    link->connect(Ztcp::Host{argv[2]}, uint16_t(port));
    app.waitReady();
    if (!app.error()) {
      ZmBlock<>{}([&app](auto wake) {
	app.txRun([wake = ZuMv(wake)]() mutable { wake(); });
      });
      if (auto cxn = link->cxn()) { cxn->sync(); cxn->disconnect(); }
    }
    app.wait();
  }
  if (initialized) app.final();
  mx.stop();
  ZiLog::stop();
  return app.error() ? 1 : 0;
}
