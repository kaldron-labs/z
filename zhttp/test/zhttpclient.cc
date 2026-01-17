//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// basic test HTTP client that retrieves index.html via TLS

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtHexDump.hh>

#include <zlib/Ztls.hh>

#include <zlib/Zhttp.hh>

constexpr unsigned BufSize = 8<<10;	// default built-in buffer size
constexpr unsigned MaxBufSize = 100<<20;// max HTTP body length (100Mb)

using IOBufAlloc = ZiIOBufAlloc<BufSize, MaxBufSize, "Zhttp.Buf">;
using RxMsg = Zhttp::RxMsg<Zhttp::Response<>, Zhttp::Body<MaxBufSize>>;

template <typename App>
struct Link : public Ztls::CliLink<App, Link<App>> {
  using Base = Ztls::CliLink<App, Link>;

  using Base::app;
  using Base::send_;

  Link(App *app) : Base{app}, rxMsg{new IOBufAlloc()} {
    // body.max = FIXME
  }

  void connected(const char *alpn, int tlsver) {
    ZtArray<uint8_t> hostname = this->server();
    std::cerr << (ZeString{}
	<< "TLS handshake completed (hostname: " << ZuCSpan(hostname)
	<< " TLS: " << tlsver << " ALPN: " << alpn << ")\n")
      << std::flush;
    // connected() is called in TLS thread
    Zhttp::Builder<> builder{new IOBufAlloc()};
    builder.request(Zhttp::Method::GET, "/", hostname,
      "User-Agent: zhttptest/1.0\r\nAccept: */*\r\n");
    auto buf = builder.finish();
    send_(ZuMv(buf));
  }
  void disconnected() {
    std::cerr << "disconnected\n" << std::flush;
    app()->done();
  }

  void connectFailed(bool transient) {
    if (transient)
      std::cerr << "failed to connect (transient)\n" << std::flush;
    else
      std::cerr << "failed to connect\n" << std::flush;
    app()->done();
  }

  int process(Ztls::RxCursor &rx) {
    while (!rx.empty()) {
      auto span = rx.span();
      int consumed = rxMsg.process(span, [this]() -> bool {
	const auto &header = rxMsg.header.span;
	const auto &body = rxMsg.body.span;
	if (auto file = ZiFile("index.hdr", ZiFile::Write | ZiFile::GC))
	  file.write(header.data(), header.length());
	if (auto file = ZiFile("index.html", ZiFile::Write | ZiFile::GC))
	  file.write(body.data(), body.length());
	// rxMsg.reset(); // to reuse rxMsg
	return false; // disconnect
      });
      if (consumed < 0) return -1;
      if (!consumed) return 0;
      rx.advance(consumed);
    }
    return 1;
  }

  RxMsg	rxMsg;
};

struct App : public Ztls::Client<App> {
  ZmSemaphore sem;

  using Link = ::Link<App>;

  void done() { sem.post(); }
};

void usage()
{
  std::cerr << "Usage: zhttpclient SERVER PORT [CA]\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 4) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);

  if (!port) usage();

  ZiLog::init("zhttpclient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  static const char *alpn[] = { "http/1.1", 0 };

  App app;

  ZiMultiplex mx(
      ZiMxParams()
	.scheduler([](auto &s) {
	  s.nThreads(4)
	  .thread(1, [](auto &t) { t.isolated(1); })
	  .thread(2, [](auto &t) { t.isolated(1); })
	  .thread(3, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    return 1;
  }

  if (!app.init(&mx, "3", alpn, argc == 4 ? argv[3] : nullptr)) {
    std::cerr << "TLS client initialization failed\n" << std::flush;
    return 1;
  }

  {
    ZmRef<App::Link> link = new App::Link(&app);

    link->connect(server, port);

    app.sem.wait();
  }

  mx.stop();

  ZiLog::stop();

  return 0;
}
