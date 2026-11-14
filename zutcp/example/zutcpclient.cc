//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <zlib/ZiFile.hh>
#include <net/if.h>

#include <iostream>

#include <zlib/ZmHeap.hh>
#include <zlib/ZtString.hh>
#include <zlib/Zutcp.hh>

ZuDerive(AppRequest,
  (ZtString<ZtStringHeapID<"Zutcp.Example.Request">>));



class App : public Zutcp::Client<App> {
public:
  class Cxn;
  bool tfo() const { return m_tfo; }
  bool earlyData(Cxn *);
  bool allowEarlyStream(Cxn *, uint64_t, bool);
  void earlyDataAccepted(Cxn *);
  void earlyDataRejected(Cxn *);
  App(ZuCSpan request) { m_request << request; }
  ZuCSpan request() const { return m_request; }
  void done() { m_established.post(); m_done.post(); }
  void ready() { m_established.post(); }
  void waitReady() { m_established.wait(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      ZiLOG(Error, "zutcpclient", ([msg = ZeString{msg}](auto &out) {
	out << "error: " << msg;
      }));
  }
  bool error() const { return m_error.load_(); }

private:
  bool			m_tfo = getenv("ZUTCP_TEST_TFO") != nullptr;
  bool			m_allowEarly = getenv("ZUTCP_TEST_TFO_SEND_DENIED") == nullptr;
  ZmSemaphore		m_done, m_established;
  AppRequest		m_request;
  ZmAtomic<unsigned>	m_error = 0;
};

class App::Cxn : public ZmNHeap<App::Cxn>,
  public Zutcp::CliCxn<App, App::Cxn> {
friend inline ZuStringT<"Zutcp.Example.CliCxn"> ZmNHeapID(Cxn *);
  using Base = Zutcp::CliCxn<App, App::Cxn>;
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(App *app, unsigned tfoID = 0) : Base{app}, m_tfoID{tfoID} {
    txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zutcpclient", ([e](auto &out) { out << "transmit error: " << e; }));
      return false;
    }});
  }
  void connected(Zutcp::Connected) {
    std::cerr << "connection established\n" << std::flush;
    if (app()->tfo()) {
      std::cerr << "tfo connected=" << m_tfoID << '\n' << std::flush;
      if (startRequest(false) && !send(ZuBSpan{}, true)) {
	app()->setError("request FIN failed");
	app()->done();
      }
    } else if (getenv("ZUTCP_WIRE_GATE_DIR")) {
      auto out = writer(); // Empty public Writer observes the published producer geometry.
      unsigned capacity = out.maxSize() - out.headRoom() - out.tailRoom();
      std::cerr << "wire producer=" << capacity << '\n' << std::flush;
      app()->ready();
    } else startRequest();
  }
  unsigned tfoID() const { return m_tfoID; }
  bool startRequest(bool fin = true) {
    if (app()->tfo()) {
      if (m_requestQueued) return true;
      m_requestQueued = true;
      std::cerr << "tfo queued=" << m_tfoID << '\n' << std::flush;
    }
    if (!send(app()->request(), fin)) {
      app()->setError("request send failed");
      app()->done();
      return false;
    }
    return true;
  }
  int process(Zutcp::Reader &reader) {
    auto length = reader.length();
    if (length) {
      reader.each(length, [](auto part) -> int64_t {
	std::cout << ZuCSpan{part.data(), part.length()};
	return part.length();
      });
      reader.clean();
      if (!retireRx(length)) return -1;
    }
    if (finReceived() && rxComplete() && !m_responseSeen) {
      m_responseSeen = true;
      std::cout << '\n' << std::flush;
      app()->done();
    }
    return length ? 1 : 0;
  }
  void disconnected(bool) {
    if (!m_responseSeen) {
      app()->setError("disconnected before response");
      app()->done();
    }
  }
  void connectFailed(bool) { app()->setError("connect failed"); app()->done(); }

private:
  unsigned	m_tfoID = 0;
  bool		m_requestQueued = false;
  bool		m_responseSeen = false; // Rx
};

bool App::earlyData(Cxn *link)
{
  if (!m_tfo) return false;
  bool queued = link->startRequest(false);
  return queued && allowEarlyStream(link, 0, true);
}

bool App::allowEarlyStream(Cxn *link, uint64_t, bool)
{
  return m_tfo && (m_allowEarly || link->tfoID() == 1);
}

void App::earlyDataAccepted(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo accepted=" << link->tfoID() << '\n' << std::flush;
}

void App::earlyDataRejected(Cxn *link)
{
  if (m_tfo) std::cerr << "tfo rejected=" << link->tfoID() << '\n' << std::flush;
}

static void usage()
{
  std::cerr << "Usage: zutcpclient INTERFACE SERVER PORT [REQUEST]\n"
    << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 4 || argc > 5) usage();

  unsigned interface = if_nametoindex(argv[1]);
  ZuCSpan server = argv[2];
  unsigned port = ZuBox<unsigned>(argv[3]);
  if (!interface || !port) usage();

  ZuCSpan request = argc == 5 ? argv[4] : "zutcp-example";

  unsigned count = 1;
  if (getenv("ZUTCP_TEST_TFO")) {
    const char *value = getenv("ZUTCP_TEST_TFO_COUNT");
    count = value ? unsigned(ZuBox<unsigned>(value)) : 2;
    if (!count) usage();
  }

  ZiLog::init("zutcpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(3)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));
  mx.plugin(new Zutcp::Stack{&mx, {.interface = interface}});


  App app{request};

  auto params = Zutcp::ClientParams(&mx);
  if (const char *profile = getenv("ZUTCP_TEST_CLIENT_OPTIONS")) {
    unsigned options = ZuBox<unsigned>(profile);
    params.sackPermitted(options & 1).windowScale(options & 2).timestamps(options & 4)
      .ecn(options & 8);
    params.maxData(131072);
  }
  if (const char *delay = getenv("ZUTCP_TEST_TLP_DELAY_US"))
    params.tlpPeerAckDelay(Zutcp::timeUS(ZuBox<unsigned>(delay)));
  if (!app.init(params)) {
    ZiLOG(Error, "zutcpclient", ([](auto &out) { out << "client initialization failed"; }));
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  if (!mx.start()) {
    ZiLOG(Error, "zutcpclient", ([](auto &out) { out << "ZiMultiplex start failed"; }));
    app.final();
    ZiLog::stop();
    return 1;
  }


  for (unsigned i = 0; i < count && !app.error(); ++i) {
    ZmRef<App::Cxn> link = new App::Cxn{&app, i + 1};
    link->connect(Zutcp::Host{server}, port);
    if (const char *directory = getenv("ZUTCP_WIRE_GATE_DIR")) {
      app.waitReady();
      if (!app.error()) {
	ZtString<> path;
	path << directory << "/wire-release";
	ZiFile file{path, ZiFile::ReadOnly | ZiFile::GC}; // Fixture main waits before output.
	char value;
	bool released = file && file.read(&value, 1) == 1 && value == 'R';
	if (released) link->startRequest();
	else { app.setError("wire fixture gate failed"); app.done(); }
      }
    }
    app.wait();
    ZmBlock<>{}([&link](auto wake) {
      link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
    if (app.tfo() && !i && count > 1) {
      if (const char *directory = getenv("ZUTCP_TEST_TFO_GATE_DIR")) {
	link = nullptr;
	ZtString<> path;
	path << directory << "/tfo-first-complete";
	ZiFile marker{path, ZiFile::Write | ZiFile::GC};
	constexpr auto ready = "ready\n"_z;
	if (!marker || marker.write(ready.data(), ready.length()) != Zi::OK ||
	    marker.close() != Zi::OK) { app.setError("TFO fixture marker failed"); break; }
	ZtString<> next;
	next << directory << "/tfo-next";
	ZiFile gate{next, ZiFile::ReadOnly | ZiFile::GC}; // Fixture main waits for peer/fault.
	char value;
	bool released = gate && gate.read(&value, 1) == 1 && value == 'R';
	if (!released) { app.setError("TFO fixture gate failed"); break; }
      }
    }
  }

  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}
