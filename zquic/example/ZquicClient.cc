//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>

#include <iostream>

#include <zlib/ZtString.hh>
#include <zlib/Zquic.hh>

namespace {

ZuCSpan cspan(const ZtString<> &s)
{
  return ZuCSpan{s.data(), s.length()};
}

bool isNumber(const char *s)
{
  if (!s || !*s) return false;
  for (; *s; ++s) if (*s < '0' || *s > '9') return false;
  return true;
}

struct App : public Zquic::Client<App> {
  struct Link;
  struct Stream;

  App(ZuCSpan request) { m_request << request; }

  ZuCSpan request() const { return cspan(m_request); }
  void done() { m_done.post(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      std::cerr << "error: " << msg << '\n' << std::flush;
  }
  bool error() const { return m_error.load_(); }

  ZmSemaphore		m_done;
  ZtString<>		m_request;
  ZmAtomic<unsigned>	m_error{0};
};

struct App::Stream :
  public Zquic::CliStream<App::Link, App::Stream> {
  using Base = Zquic::CliStream<App::Link, App::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &) { return 0; }
};

struct App::Link :
  public Zquic::CliLink<App, App::Link, App::Stream> {
  using Base = Zquic::CliLink<App, App::Link, App::Stream>;
  using Base::Base;

  Link(App *app) : Base{app} { }

  void connected(Zi::Connected info) {
    std::cerr << "QUIC handshake completed"
      << " version=" << info.version
      << " alpn=" << info.alpn << '\n' << std::flush;
    auto s = stream(Zi::StreamType::Duplex);
    if (!s || !send(s, app()->request())) {
      app()->setError("stream send failed");
      app()->done();
    }
  }

  void disconnected(bool) {
    if (!m_responseSeen) {
      app()->setError("disconnected before response");
      app()->done();
    }
  }

  void connectFailed(bool) {
    app()->setError("connect failed");
    app()->done();
  }

  void streamed(ZmRef<Stream>) { }

  void streamFrame(uint64_t, uint64_t, ZuCSpan payload, bool fin) {
    std::cout.write(payload.data(), payload.length());
    if (fin) {
      std::cout << '\n' << std::flush;
      m_responseSeen = 1;
      app()->done();
    }
  }

  ZmAtomic<unsigned>	m_responseSeen = 0;
};

void usage()
{
  std::cerr << "Usage: ZquicClient SERVER PORT [CA] [REQUEST]\n"
    << std::flush;
  ::exit(1);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 5) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);
  if (!port) usage();

  const char *ca = nullptr;
  ZuCSpan request = "zquic-example";
  if (argc == 4) {
    if (isNumber(argv[3]))
      usage();
    else
      ca = argv[3];
  } else if (argc == 5) {
    ca = argv[3];
    request = argv[4];
  }

  ZiLog::init("ZquicClient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    ZiLog::stop();
    return 1;
  }

  App app{request};

  if (!app.init(
	Zquic::ClientParams(&mx, "3", "4")
	  .caPath(ca)
	  .alpn(ZuSpan<ZuCSpan>{"zquic-echo"}))) {
    std::cerr << "QUIC client initialization failed\n" << std::flush;
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  {
    ZmRef<App::Link> link = new App::Link{&app};
    link->connect(Zquic::Host{server}, port);
    app.m_done.wait();
    link->disconnect();
  }

  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}
