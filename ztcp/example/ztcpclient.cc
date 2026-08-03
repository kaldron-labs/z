//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Ztcp.hh>

struct App : public Ztcp::Client<App> {
  struct Link;
  using RxBufAlloc = Ztcp::RxBufAlloc<8<<10>;
  using TxBufAlloc = Ztcp::TxBufAlloc<8<<10>;

  App(unsigned repeat_) : repeat{repeat_} { }

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }

  ZmSemaphore		sem;
  unsigned		repeat = 1;
  ZmAtomic<unsigned>	count{0};
};

struct App::Link :
  public Ztcp::CliLink<App, App::Link, App::RxBufAlloc, App::TxBufAlloc> {
  using Base = Ztcp::CliLink<App, Link, App::RxBufAlloc, App::TxBufAlloc>;

  Link(App *app) : Base{app} {
    txErrorFn(ZiTxErrorFn{[](bool transient, ZeException &e) {
      ZiLOG(Error, "ztcpclient", ([transient, e](auto &s) {
	s << "transmit error" << (transient ? " (transient): " : ": ") << e;
      }));
      return false;
    }});
  }

  void sendPing() {
    auto tx = txStream();
    tx << "ping\r\n" << Zi::flush();
  }

  void connected(Ztcp::Connected) {
    std::cerr << "TCP connected\n" << std::flush;
    sendPing();
  }
  void disconnected(bool) {
    std::cerr << "disconnected\n" << std::flush;
    app()->done();
  }
  void connectFailed(bool transient) {
    std::cerr << "connect failed" << (transient ? " (transient)" : "") <<
      '\n' << std::flush;
    app()->done();
  }
  int process(Ztcp::RxStream &rx) {
    unsigned remaining = 6;
    bool ok = false;
    rx.consume(
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      },
      [&ok](ZuBSpan span) { ok = ZuCSpan{span} == "pong\r\n"; });
    if (!ok) return 0;
    std::cerr << "pong\n" << std::flush;
    if (app()->count.xchAdd(1) + 1 >= app()->repeat) {
      disconnect();
      return 1;
    }
    sendPing();
    return 1;
  }
};

void usage()
{
  std::cerr << "Usage: ztcpclient SERVER PORT [REPEAT]\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 4) usage();
  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);
  unsigned repeat = 1;
  if (argc == 4) repeat = ZuBox<unsigned>(argv[3]);
  if (!port || !repeat) usage();

  ZiLog::init("ztcpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  App app(repeat);
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
  if (!app.init(Ztcp::ClientParams(&mx, "3", "4"))) return 1;
  {
    ZmRef<App::Link> link = new App::Link(&app);
    link->connect(server, port);
    app.sem.wait();
  }
  app.final();
  mx.stop();
  ZiLog::stop();
  return 0;
}
