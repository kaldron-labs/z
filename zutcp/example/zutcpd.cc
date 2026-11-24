//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <iostream>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZtString.hh>
#include <zlib/Ztcp.hh>
#include <zlib/Zutcp.hh>

class AppCxn;
class App : public Ztcp::Server<App> {
public:
  using Link = AppCxn;
  App(ZiIP ip, uint16_t port, unsigned target, bool waitEOF) :
    m_localIP{ip}, m_localPort{port}, m_target{target}, m_waitEOF{waitEOF} { }
  ZiConnection *accepted(const ZiCxnInfo &);
  ZiIP localIP() const { return m_localIP; }
  uint16_t localPort() const { return m_localPort; }
  unsigned nAccepts() const { return m_target; }
  bool waitEOF() const { return m_waitEOF; }
  void listening(const ZiListenInfo &) { std::cerr << "listening\n" << std::flush; }
  void listenFailed(bool) { setError("listen failed"); }
  void eof(ZmRef<ZiConnection> cxn) { m_eof.push(ZuMv(cxn)); m_event.post(); }
  void done() { ++m_doneCount; m_event.post(); }
  void wait() {
    while (m_doneCount.load_() < m_target && !error()) {
      m_event.wait();
      while (auto cxn = m_eof.shift()) {
	ZmBlock<>{}([this](auto wake) {
	  txRun([wake = ZuMv(wake)]() mutable { wake(); });
	});
	cxn->sync();
	cxn->disconnect();
      }
    }
  }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcpd", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
    m_event.post();
  }
  bool error() const { return m_error.load_(); }
private:
  // EOF work transfers existing frozen ZiConnection owners to the control caller.
  // ZiConnection cannot embed this application's event membership.
  using EOFs = ZmQueue<ZmRef<ZiConnection>, ZmQueueLock<ZmLock,
    ZmQueueHeapID<"Zutcp.Example.EOF">>>;
  ZiIP m_localIP;
  uint16_t m_localPort;
  unsigned m_target;
  bool m_waitEOF;
  EOFs m_eof;
  ZmSemaphore m_event;
  ZmAtomic<unsigned> m_doneCount = 0, m_error = 0;
};

class AppCxn : public ZmNHeap<AppCxn>, public Ztcp::SrvLink<App, AppCxn> {
friend inline ZuStringT<"Zutcp.Example.SrvCxn"> ZmNHeapID(AppCxn *);
  using Base = Ztcp::SrvLink<App, AppCxn>;
public:
  using ZmNHeap<AppCxn>::operator new;
  using ZmNHeap<AppCxn>::operator delete;
  AppCxn(App *app) : Base{app} { }
  class Wire : public ZmNHeap<Wire>, public Ztcp::SrvCxn<AppCxn> {
  friend inline ZuStringT<"Zutcp.Example.Wire"> ZmNHeapID(Wire *);
    using Base = Ztcp::SrvCxn<AppCxn>;
  public:
    using ZmNHeap<Wire>::operator new;
    using ZmNHeap<Wire>::operator delete;
    Wire(AppCxn *link, const ZiCxnInfo &info) : Base{link, info}, m_link{link} { }
    void connected(ZiRxContext &io) override {
      Base::connected(io);
      io.fn = ZiRxFn{[link = m_link, data = ZuMv(io.fn)](ZiRxContext &io) mutable {
	if (io.buf && !io.buf->length) {
	  auto owner = ZmRef<AppCxn>{link};
	  io.complete();
	  owner->app()->rxRun([owner = ZuMv(owner)] { owner->eof(); });
	  return true;
	}
	return data.as<ZiRxFn>()(io);
      }};
    }
  private:
    AppCxn *m_link;
  };
  void connected(Ztcp::Connected) {
    std::cerr << "connection established\n" << std::flush;
  }
  int process(Ztcp::RxStream &rx) {
    auto length = rx.length();
    if (app()->waitEOF()) {
      while (rx) {
	auto part = rx.span();
	m_request << ZuCSpan{part.data(), part.length()};
	rx.advance(part.length());
      }
    } else {
      auto tx = txStream();
      if (!m_prefixSent) { tx << "echo: "; m_prefixSent = true; }
      while (rx) {
	auto part = rx.span();
	tx << ZuCSpan{part.data(), part.length()};
	rx.advance(part.length());
      }
      if (!tx.flush()) { app()->setError("echo send failed"); return -1; }
    }
    return length ? 1 : 0;
  }
  void eof() {
    m_eof = true;
    if (app()->waitEOF()) {
      std::cerr << "receive EOF; reply bytes=" << m_request.length() << '\n' << std::flush;
      auto tx = txStream();
      tx << "echo: " << m_request;
      if (!tx.flush()) app()->setError("echo send failed");
    } else if (!m_prefixSent) {
      auto tx = txStream();
      tx << "echo: ";
      if (!tx.flush()) app()->setError("echo send failed");
    }
    app()->eof(ZmRef<ZiConnection>{cxn()});
  }
  void disconnected(bool) {
    if (!m_eof) app()->setError("disconnected before request EOF");
    app()->done();
  }
private:
  ZtString<ZtStringHeapID<"Zutcp.Example.Request">> m_request;
  bool m_prefixSent = false, m_eof = false;
};

ZiConnection *App::accepted(const ZiCxnInfo &info)
{
  return new AppCxn::Wire{new AppCxn{this}, info};
}

static void usage()
{
  std::cerr << "Usage: zutcpd INTERFACE SERVER PORT [COUNT] [--wait-eof]\n";
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
  bool waitEOF = argc > 4 && !strcmp(argv[argc - 1], "--wait-eof");
  unsigned positional = unsigned(argc) - unsigned(waitEOF);
  if (positional != 4 && positional != 5) usage();
  unsigned interface = if_nametoindex(argv[1]);
  ZiIP server = argv[2];
  unsigned port = ZuBox<unsigned>(argv[3]);
  unsigned count = positional == 5 ? unsigned(ZuBox<unsigned>(argv[4])) : 1;
  if (!interface || !port || port > 65535 || !count) usage();

  ZiLog::init("zutcpd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZmRef<Zpkt::Env> env = new Zpkt::Env;
  ZmRef<Zutcp::Instance> instance = new Zutcp::Instance;
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
  auto configured = instance->init(&mx, env, ZuMv(xdp), {});
  if (configured.is<ZeException>()) {
    xdp->final();
    if (auto error = xdp->error()) ZiLogEvent(ZuMv(error));
    ZiLogEvent(ZuMv(configured).p<ZeException>());
    ZiLog::stop();
    return 1;
  }
  App app{server, uint16_t(port), count, waitEOF};
  Ztcp::ServerParams params{&mx, "3", "4"};
  params.rxAllocator(AppCxn::rxAllocator(&mx)).txAllocator(AppCxn::txAllocator(&mx));
  bool initialized = app.init(ZuMv(params));
  bool started = initialized && mx.start() && app.start();
  if (!started) app.setError("server initialization/start failed");
  else { app.listen(); app.wait(); }
  if (started) app.stop();
  if (initialized) app.final();
  mx.stop();
  ZiLog::stop();
  return app.error() ? 1 : 0;
}
