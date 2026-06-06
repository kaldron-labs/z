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

using ResponseKeys = ZuStringTL<"content-type", "location", "server">;
using HttpParser =
  Zhttp::Parser<
    Zhttp::Response<ResponseKeys>, Zhttp::Body<MaxBufSize>, ZuEmpty>;
using HttpRequestBuilder =
  Zhttp::Builder<
    ZuStringTL<>,
    ZuStringTL<"user-agent: zhttptest/1.0", "accept: */*">>;

static constexpr const char *ResponseKeyName[] = {
  "content-type",
  "location",
  "server"
};

template <typename App>
struct Link : public Ztls::CliLink<App, Link<App>> {
  using Base = Ztls::CliLink<App, Link<App>>;

  using Base::app;
  Link(App *app) : Base{app} { }

  void connected(const char *alpn, int tlsver) {
    ZtArray<uint8_t> hostname = this->server();
    std::cerr << (ZeString{}
	<< "TLS handshake completed (hostname: " << ZuCSpan(hostname)
	<< " TLS: " << tlsver << " ALPN: " << alpn << ")\n")
      << std::flush;
    // connected() is called in TLS thread
    auto tx = this->txStream_();
    HttpRequestBuilder builder;
    builder.request(tx, Zhttp::Method::GET, "/", ZuCSpan(hostname),
      [](auto &, auto) { return ""; });
    builder.finish(tx);
    tx << Zi::flush();
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

  int process(Ztls::RxStream &rx) {
    while (!rx.empty()) {
      bool done = false;
      int consumed = parser.process(
	rx,
	[](auto &, int status) {
	  std::cerr << "status: " << status << '\n' << std::flush;
	},
	[](auto &, int i, ZuCSpan value) {
	  std::cerr << "header " << ResponseKeyName[i] << ": " << value <<
	    '\n' << std::flush;
	},
	[](auto &, int i) {
	  std::cerr << "fixed header match: " << i << '\n' << std::flush;
	},
	[this, &done](auto &) -> bool {
	  if (!framingLogged) {
	    std::cerr << "framing: ";
	    if (parser.body.chunked)
	      std::cerr << "chunked";
	    else
	      std::cerr << "content-length=" << parser.body.contentLength;
	    std::cerr << '\n' << std::flush;
	    framingLogged = true;
	  }
	  if (auto body = parser.body.span) {
	    if (!bodyFileOpen) {
	      bodyFile = ZiFile("index.html", ZiFile::Write | ZiFile::GC);
	      if (!bodyFile) {
		std::cerr << "failed to open index.html\n" << std::flush;
		return false;
	      }
	      bodyFileOpen = true;
	    }
	    if (bodyFile.write(body.data(), body.length()) != Zi::OK) {
	      std::cerr << "failed to write body chunk\n" << std::flush;
	      return false;
	    }
	    bodyBytes += body.length();
	    ++bodyChunks;
	    std::cerr << "body chunk: " << body.length() << " bytes\n" <<
	      std::flush;
	  }
	  if (auto trailer = parser.body.chunkTrlr) {
	    trailerBytes += trailer.length();
	    ++trailerChunks;
	    auto text = ZuCSpan{
	      reinterpret_cast<const char *>(trailer.data()), trailer.length()};
	    std::cerr << "trailer chunk: " << trailer.length() <<
	      " bytes\n" << text << std::flush;
	  }
	  if (parser.body.complete) {
	    std::cerr << "body complete: " << bodyBytes << " bytes in " <<
	      bodyChunks << " chunks";
	    if (trailerChunks)
	      std::cerr << ", trailers: " << trailerBytes << " bytes in " <<
		trailerChunks << " chunks";
	    std::cerr << '\n' << std::flush;
	    done = true;
	  }
	  return true;
      });
      if (consumed < 0) return -1;
      if (done) return -1;
      if (!consumed) return 0;
    }
    return 1;
  }

  HttpParser	parser;
  ZiFile	bodyFile;
  uint64_t	bodyBytes = 0;
  uint64_t	trailerBytes = 0;
  unsigned	bodyChunks = 0;
  unsigned	trailerChunks = 0;
  bool		bodyFileOpen = false;
  bool		framingLogged = false;
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

  ZuCSpan alpn[] = { "http/1.1" };

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

  if (!app.init(
	Ztls::ClientParams(&mx, "3", alpn)
	  .caPath(argc == 4 ? argv[3] : nullptr))) {
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
