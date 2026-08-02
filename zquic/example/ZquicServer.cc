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

struct AppLink;
struct AppStream;
struct App : public Zquic::Server<App, AppLink> {
  using Link = AppLink;
  using Stream = AppStream;

  App(ZiIP localIP, unsigned localPort, unsigned target) :
    m_localIP{localIP}, m_localPort{localPort}, m_target{target} { }

  ZmRef<Link> accepted(const Zquic::InitialInfo &);

  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }
  void listenFailed(bool) {
    setError("listen failed");
    done();
  }
  void done() {
    if (++m_doneCount >= m_target) m_done.post();
  }
  void wait() { m_done.wait(); }
  void setError(const char *msg) {
    if (!m_error.xch(1))
      std::cerr << "error: " << msg << '\n' << std::flush;
  }
  bool error() const { return m_error.load_(); }

  ZmSemaphore		m_done;
  ZiIP			m_localIP;
  unsigned		m_localPort = 0;
  unsigned		m_target = 1;
  ZmAtomic<unsigned>	m_doneCount = 0;
  ZmAtomic<unsigned>	m_error{0};
};

struct AppStream :
  public Zquic::SrvStream<AppLink, AppStream> {
  using Base = Zquic::SrvStream<AppLink, AppStream>;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    uint64_t length = rx.length();
    if (!length) return 0;
    rx.clean();
    return retireRx(length) ? 1 : -1;
  }
};

struct AppLink :
  public Zquic::SrvLink<App, AppLink, AppStream> {
  using Base = Zquic::SrvLink<App, AppLink, AppStream>;
  using Base::Base;

  AppLink(App *app) : Base{app} { }

  void connected(Zquic::Connected info) {
    std::cerr << "QUIC handshake completed"
      << " version=" << info.version
      << " alpn=" << info.alpn << '\n' << std::flush;
  }

  void disconnected(bool) { }
  void streamed(ZmRef<Stream>) { }

  void streamFrame(uint64_t streamID, uint64_t, ZuCSpan payload, bool fin) {
    auto stream = findStream(int64_t(streamID));
    if (!stream) {
      app()->setError("received STREAM frame without stream object");
      app()->done();
      return;
    }

    ZtString<> response;
    response << "echo: " << payload;
    if (!send(stream, cspan(response), fin)) {
      app()->setError("stream echo send failed");
      app()->done();
      return;
    }
    if (fin) app()->done();
  }
};

ZmRef<App::Link> App::accepted(const Zquic::InitialInfo &)
{
  return new Link{this};
}

void usage()
{
  std::cerr << "Usage: ZquicServer SERVER PORT CERT KEY [COUNT]\n"
    << std::flush;
  ::exit(1);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc != 5 && argc != 6) usage();

  ZiIP server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);
  unsigned count = 1;
  if (argc == 6) count = ZuBox<unsigned>(argv[5]);
  if (!port || !count) usage();

  ZiLog::init("ZquicServer");
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

  App app{server, port, count};

  if (!app.init(
	Zquic::ServerParams(&mx, "3", "4")
	  .certPath(argv[3])
	  .keyPath(argv[4])
	  .alpn(ZuSpan<ZuCSpan>{"zquic-echo"}))) {
    std::cerr << "QUIC server initialization failed\n" << std::flush;
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  if (!app.start()) {
    std::cerr << "QUIC server listen failed\n" << std::flush;
    app.final();
    mx.stop();
    ZiLog::stop();
    return 1;
  }

  app.wait();

  app.stop();
  app.final();
  mx.stop();
  ZiLog::stop();

  return app.error() ? 1 : 0;
}
