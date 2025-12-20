//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtArray.hh>

#include <zlib/Ztls.hh>

const char *Request =
  "GET / HTTP/1.1\r\n"
  "Host: ";
const char *Request2 = "\r\n"
  "User-Agent: ZtlsClient/1.0\r\n"
  "Accept: */*\r\n"
  "\r\n";

struct App : public Ztls::Client<App> {
  struct Link : public Ztls::CliLink<App, Link> {
    using Base = Ztls::CliLink<App, Link>;

    Link(App *app) : Base{app} { }

    void connected(const char *alpn, int tlsver) {
      ZtArray<uint8_t> hostname = this->server();
      std::cerr << (ZtString<>{}
	  << "TLS handshake completed (hostname: " << ZuCSpan(hostname)
	  << " TLS: " << tlsver << " ALPN: " << alpn << ")\n")
	<< std::flush;
      ZtArray<uint8_t> request;
      request << Request << hostname << Request2;
      send_(request.data(), request.length()); // in TLS thread
    }
    void disconnected() {
      std::cerr << "disconnected\n" << std::flush;
      close();
      app()->done();
    }

    void connectFailed(bool transient) {
      if (transient)
	std::cerr << "failed to connect (transient)\n" << std::flush;
      else
	std::cerr << "failed to connect\n" << std::flush;
      close();
      app()->done();
    }

    int process(ZuSpan<uint8_t> rcvd) {
      if (!file) {
	header << ZuCSpan(rcvd);
	ZtRegexCaptures(c, 0);
	if (ZtREGEX("\n\r\n").m(header, c)) {
	  ZtRegexCaptures(d, 1);
	  if (ZtREGEX("\nContent-Length: (\d+)").m(header, d)) {
	    length = ZuBox<unsigned>(d[2]);
	  } else if (ZtREGEX("\nTransfer-Encoding: chunked\r").m(header)) {
	    // just read the first chunk for testing purposes
	    if (ZtREGEX("\n\r\n([\dA-F]+)\r\n").m(header, d)) {
	      length = ZuBox<unsigned>(ZuFmt::Hex<true>{}, d[2]);
	      c[2] = d[3];
	    } else
	      return rcvd.length();
	  }
	  file = fopen("index.hdr", "w");
	  ZmAssert(file);
	  fwrite(c[0].data(), 1, c[0].length() + 1, file);
	  fclose(file);
	  file = fopen("index.html", "w");
	  ZmAssert(file);
	  fwrite(c[2].data(), 1, c[2].length(), file);
	  ZmAssert(length >= c[2].length());
	  length -= c[2].length();
	  header = {};
	}
      } else {
	fwrite(rcvd.data(), 1, rcvd.length(), file);
	if (length <= rcvd.length()) return -1;
	length -= rcvd.length();
      }
      return rcvd.length();
    }

    void close() {
      if (file) { fclose(file); file = nullptr; }
    }

    unsigned	length = 0;
    ZtString<>	header;
    FILE	*file = nullptr;
  };

  void done() { sem.post(); }

  ZmSemaphore sem;
};

void usage()
{
  std::cerr << "Usage: ZtlsClient SERVER PORT [CA]\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 4) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);

  if (!port) usage();

  ZeLog::init("ZtlsClient");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::fileSink(ZeSinkOptions{}.path("&2")));
  ZeLog::start();

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

  ZeLog::stop();

  return 0;
}
