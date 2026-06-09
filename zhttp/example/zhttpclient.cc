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

using ResponseHeaders = ZuTypeList<
  ZuStringT<"content-type">, void,
  ZuStringT<"location">, void,
  ZuStringT<"server">, void>;
using RequestHeaders = ZuTypeList<
  ZuStringT<"user-agent">, ZuStringT<"zhttptest/1.0">,
  ZuStringT<"accept">, ZuStringT<"*/*">>;

struct HttpRequestBuilder :
  public Zhttp::Builder<HttpRequestBuilder, RequestHeaders> {
  using Base = Zhttp::Builder<HttpRequestBuilder, RequestHeaders>;

  HttpRequestBuilder(ZuCSpan hostname_) : hostname{hostname_} { }

  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::GET, "/", ""); }
  template <typename L>
  void host(L &&l) { l(hostname); }

  ZuCSpan hostname;
};

template <typename App>
struct Link : public Ztls::CliLink<App, Link<App>> {
  using Base = Ztls::CliLink<App, Link<App>>;

  using Base::app;
  Link(App *app) : Base{app} { }

  struct HttpParser :
    public Zhttp::Parser<HttpParser, false, ResponseHeaders, MaxBufSize> {
    using Base =
      Zhttp::Parser<HttpParser, false, ResponseHeaders, MaxBufSize>;

    HttpParser(Link *link_) : link{link_} { }

    void status(unsigned status) {
      std::cerr << "status: " << status << '\n' << std::flush;
    }
    void contentLength(uint64_t contentLength) {
      link->contentLength = contentLength;
    }
    void chunked() { link->chunked = true; }

    template <typename Key>
    void header(ZuBSpan value) {
      std::cerr << "header " << Key{}() << ": " << ZuCSpan(value) <<
	'\n' << std::flush;
    }

    void body(ZuBSpan span) {
      link->logFraming();
      if (!span) return;
      if (!link->bodyFileOpen) {
	link->bodyFile = ZiFile("index.html", ZiFile::Write | ZiFile::GC);
	if (!link->bodyFile) {
	  std::cerr << "failed to open index.html\n" << std::flush;
	  link->done = true;
	  return;
	}
	link->bodyFileOpen = true;
      }
      if (link->bodyFile.write(span.data(), span.length()) != Zi::OK) {
	std::cerr << "failed to write body chunk\n" << std::flush;
	link->done = true;
	return;
      }
      link->bodyBytes += span.length();
      ++link->bodyChunks;
      std::cerr << "body chunk: " << span.length() << " bytes\n" <<
	std::flush;
    }

    void complete(Zhttp::ParserState::T) {
      std::cerr << "body complete: " << link->bodyBytes << " bytes in " <<
	link->bodyChunks << " chunks\n" << std::flush;
      link->done = true;
    }

    Link *link;
  };

  void connected(const char *alpn, int tlsver) {
    ZtArray<uint8_t> hostname = this->server();
    std::cerr << (ZeString{}
	<< "TLS handshake completed (hostname: " << ZuCSpan(hostname)
	<< " TLS: " << tlsver << " ALPN: " << alpn << ")\n")
      << std::flush;
    // connected() is called on the Ztls Rx thread.
    auto tx = this->txStream();
    HttpRequestBuilder builder{ZuCSpan(hostname)};
    builder.request(tx);
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
    auto state = parser.process(rx);
    if (done) return -1;
    if (state == Zhttp::ParserState::Error) return -1;
    if (state != Zhttp::ParserState::Complete) return 0;
    return 1;
  }

  void logFraming() {
    if (framingLogged) return;
    std::cerr << "framing: ";
    if (chunked)
      std::cerr << "chunked";
    else if (contentLength >= 0)
      std::cerr << "content-length=" << contentLength;
    else
      std::cerr << "no content-length";
    std::cerr << '\n' << std::flush;
    framingLogged = true;
  }

  HttpParser	parser{this};
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		framingLogged = false;
  bool		done = false;
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
	  .thread(3, [](auto &t) { t.isolated(1); })
	  .thread(4, [](auto &t) { t.isolated(1); }); })
	.rxThread(1).txThread(2));

  if (!mx.start()) {
    std::cerr << "ZiMultiplex start failed\n" << std::flush;
    return 1;
  }

  if (!app.init(
	Ztls::ClientParams(&mx, "3", "4").alpn(alpn)
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
