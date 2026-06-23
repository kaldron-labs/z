//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Ztcp.hh>

struct App : public Ztcp::Server<App> {
  struct Link;
  using RxBufAlloc = Ztcp::RxBufAlloc<8<<10>;
  using TxBufAlloc = Ztcp::TxBufAlloc<8<<10>;

  App(ZuCSpan ip_, unsigned port_) : ip{ip_}, port{port_} { }

  ZiConnection *accepted(const ZiCxnInfo &ci);
  ZiIP localIP() const { return ZiIP(ip); }
  unsigned localPort() const { return port; }
  unsigned nAccepts() const { return 8; }

  void listening(const ZiListenInfo &info) {
    std::cerr << "listening(" << info.ip << ':' << info.port << ")\n" <<
      std::flush;
  }
  void listenFailed(bool transient) {
    std::cerr << "listen failed" << (transient ? " (transient)" : "") <<
      '\n' << std::flush;
    done.post();
  }

  ZmSemaphore	done;
  ZuCSpan	ip;
  unsigned	port = 0;
};

struct App::Link :
  public Ztcp::SrvLink<App, App::Link, App::RxBufAlloc, App::TxBufAlloc> {
  using Base = Ztcp::SrvLink<App, Link, App::RxBufAlloc, App::TxBufAlloc>;

  Link(App *app) : Base{app} { }

  void connected(Zi::Connected) {
    std::cerr << "TCP accepted\n" << std::flush;
  }
  void disconnected(bool) {
    std::cerr << "TCP disconnected\n" << std::flush;
  }
  int process(Ztcp::RxStream &rx) {
    bool ok = false;
    rx.consume(
      [](ZuBSpan span) -> int64_t { return span.length() >= 6 ? 6 : 0; },
      [&ok](ZuBSpan span) { ok = ZuCSpan{span} == "ping\r\n"; });
    if (!ok) return 0;
    auto tx = txStream();
    tx << "pong\r\n" << Zi::flush();
    return 1;
  }
};

ZiConnection *App::accepted(const ZiCxnInfo &ci)
{
  return new Link::Cxn(new Link(this), ci);
}

void usage()
{
  std::cerr << "Usage: ZtcpServer SERVER PORT [REPEAT]\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 4) usage();
  ZuCSpan ip = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);
  if (!port) usage();

  ZiLog::init("ZtcpServer");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  App app(ip, port);
  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));
  if (!mx.start()) return 1;
  if (!app.init(Ztcp::ServerParams(&mx, "3", "4"))) return 1;
  app.listen();
  app.done.wait();
  app.stopListening();
  app.final();
  mx.stop();
  ZiLog::stop();
  return 0;
}
