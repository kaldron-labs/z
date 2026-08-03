//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>

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
    Link(App *app) : Ztls::SrvLink<App, Link>(app) {
      txErrorFn(ZiTxErrorFn{[](bool transient, ZeException &e) {
	ZiLOG(Error, "ztlsserver", ([transient, e](auto &s) {
	  s << "transmit error" <<
	    (transient ? " (transient): " : ": ") << e;
	}));
	return false;
      }});
    }

    void connected(Ztls::Connected info) {
      std::cerr << (ZuCArray<100>()
	  << "TLS handshake completed (TLS: " << info.version
	  << " ALPN: " << info.alpn << ")\n")
	<< std::flush;
    }
    void disconnected(bool) {
      std::cerr << "disconnected\n" << std::flush;
      app()->done();
    }

    int process(Ztls::RxStream &rx) {
      while (!rx.empty()) {
	ZtString<> response;
	auto &content = app()->payload();
	response << Response << content.length() << Response2;
	{
	  auto tx = txStream();
	  tx << response << Zi::flush();
	}
	{
	  auto tx = txStream();
	  tx << content << Zi::flush();
	}
	int64_t consumed = rx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [](ZuBSpan) { });
	if (consumed < 0) return -1;
	if (!consumed) return 0;
      }
      return 1;
    }
  };

  using Cxn = typename Link::Cxn;
  Cxn *accepted(const ZiCxnInfo &ci) {
    return new Cxn(new Link(this), ci);
  }

  App(ZiIP server, unsigned port, unsigned repeats) :
    m_localIP(server), m_localPort(port), m_target(repeats) { }
  App(ZiIP server, unsigned port, unsigned repeats, unsigned payload_len) :
      m_localIP(server), m_localPort(port), m_target(repeats) {
    if (payload_len) {
      m_payload.length(payload_len);
      memset(m_payload.data(), 'X', payload_len);
    } else
      m_payload = Content;
  }

  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }
  ZtString<> &payload() { return m_payload; }

  void done() { if (++m_done >= m_target) m_sem.post(); }
  void wait() { m_sem.wait(); }

private:
  ZmSemaphore	m_sem;
  ZmAtomic<unsigned> m_done{0};
  ZiIP		m_localIP;
  unsigned	m_localPort;
  unsigned	m_target = 1;
  ZtString<>	m_payload;
};

void usage()
{
  std::cerr << "Usage: ztlsserver SERVER PORT CERT KEY [REPEAT]\n"
    << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc != 5 && argc != 6) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);

  if (!port) usage();

  ZiLog::init("ztlsserver");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZuCSpan alpn[] = { "http/1.1" };

  unsigned repeats = 1;
  unsigned payload_len = 0;
  if (argc == 6) repeats = ZuBox<unsigned>(argv[5]);
  if (!repeats) repeats = 1;
  auto is_number = [](const char *s) {
    if (!s || !*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
  };
  if (const char *payload = getenv("ZTLS_PAYLOAD")) {
    if (is_number(payload))
      payload_len = ZuBox<unsigned>(payload);
  }
  App app(server, port, repeats, payload_len);

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
	Ztls::ServerParams(&mx, "3", "4").alpn(alpn)
	  .certPath(argv[3])
	  .keyPath(argv[4]))) {
    std::cerr << "TLS server initialization failed\n" << std::flush;
    return 1;
  }

  app.listen();

  app.wait();

  mx.stop();

  ZiLog::stop();

  return 0;
}
