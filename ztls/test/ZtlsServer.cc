//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <iostream>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtArray.hh>

#include <zlib/Ztls.hh>

const char *Content =
  "<html><head>\n"
  "<meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
  "<title>200 OK</title>\n"
  "</head><body>\n"
  "<h1>OK</h1>\n"
  "Test document\n"
  "</body></html>";

const char *Response =
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html\r\n"
  "Content-Length: ";
const char *Response2 = "\r\n"
  "Accept: */*\r\n"
  "\r\n";

struct App : public Ztls::Server<App> {
  struct Link : public Ztls::SrvLink<App, Link> {
    Link(App *app) : Ztls::SrvLink<App, Link>(app) { }

    void connected(const char *alpn, int tlsver) {
      std::cerr << (ZuCArray<100>()
	  << "TLS handshake completed (TLS: " << tlsver
	  << " ALPN: " << alpn << ")\n")
	<< std::flush;
    }
    void disconnected() {
      std::cerr << "disconnected\n" << std::flush;
      app()->done();
    }

    int process(ZuSpan<uint8_t> rcvd) {
      ZtString<> response;
      ZtString<> content = Content;
      response << Response << content.length() << Response2;
      send_(
	reinterpret_cast<const uint8_t *>(response.data()),
	response.length());
      send_(
	reinterpret_cast<const uint8_t *>(content.data()),
	content.length());
      return rcvd.length();
    }
  };

  using Cxn = typename Link::Cxn;
  Cxn *accepted(const ZiCxnInfo &ci) {
    return new Cxn(new Link(this), ci);
  }

  App(ZiIP server, unsigned port) :
    m_localIP(server), m_localPort(port) { }

  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }

  void done() { m_sem.post(); }
  void wait() { m_sem.wait(); }

private:
  ZmSemaphore	m_sem;
  ZiIP		m_localIP;
  unsigned	m_localPort;
};

void usage()
{
  std::cerr << "Usage: ZtlsServer SERVER PORT CERT KEY\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc != 5) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);

  if (!port) usage();

  ZeLog::init("ZtlsServer");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::fileSink(ZeSinkOptions{}.path("&2")));
  ZeLog::start();

  static const char *alpn[] = { "http/1.1", 0 };

  App app(server, port);

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

  if (!app.init(&mx, "3", alpn, nullptr, argv[3], argv[4])) {
    std::cerr << "TLS server initialization failed\n" << std::flush;
    return 1;
  }

  app.listen();

  app.wait();

  mx.stop();

  ZeLog::stop();

  return 0;
}
