//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <stdio.h>
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
  App(ZuCSpan request) { m_request << request; }
  ZuCSpan request() const { return m_request; }
  void done() { m_established.post(); m_done.post(); }
  void ready() { m_established.post(); }
  void waitReady() { m_established.wait(); }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1)) std::cerr << "error: " << msg << '\n' << std::flush;
  }
  bool error() const { return m_error.load_(); }

private:
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
  Cxn(App *app) : Base{app} {
    txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zutcpclient", ([e](auto &out) { out << "transmit error: " << e; }));
      return false;
    }});
  }
  void connected(Zutcp::Connected info) {
    std::cerr << "connection established version=" << info.version << '\n' << std::flush;
    if (getenv("ZUTCP_WIRE_GATE_DIR")) app()->ready();
    else startRequest();
  }
  void startRequest() {
    if (!send(app()->request())) {
      app()->setError("request send failed");
      app()->done();
    }
  }
  int process(Zutcp::Reader &reader) {
    auto length = reader.length();
    if (length) {
      reader.each(length, [](auto part) -> int64_t {
        std::cout.write(reinterpret_cast<const char *>(part.data()), part.length());
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
  bool m_responseSeen = false; // Rx
};

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

  if (!app.init(
	Zutcp::ClientParams(&mx))) {
    std::cerr << "client initialization failed\n" << std::flush;
    mx.stop();
    ZiLog::stop();
    return 1;
  }
  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    app.final();
    ZiLog::stop();
    return 1;
  }


  {
    ZmRef<App::Cxn> link = new App::Cxn{&app};
    link->connect(Zutcp::Host{server}, port);
    if (const char *directory = getenv("ZUTCP_WIRE_GATE_DIR")) {
      app.waitReady();
      if (!app.error()) {
	ZtString<> path;
	path << directory << "/wire-release";
	auto file = fopen(path, "r"); // Fixture control thread waits before request output.
	bool released = file && fgetc(file) == 'R';
	if (file) fclose(file);
	if (released) link->startRequest();
	else { app.setError("wire fixture gate failed"); app.done(); }
      }
    }
    app.wait();
    ZmBlock<>{}([&link](auto wake) {
      link->disconnect([wake = ZuMv(wake)]() mutable { wake(); });
    });
  }

  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}
