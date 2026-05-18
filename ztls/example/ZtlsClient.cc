//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>

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
      ++round;
      bool resumed = ptls_is_psk_handshake(this->tls());
      if (round > 1) {
	if (!resumed) app()->setError("session not resumed");
	if (this->maxEarlyData() != 0)
	  app()->setError("early data unexpectedly enabled");
      }
      ZtArray<uint8_t> hostname = this->server();
      std::cerr << (ZtString<>{}
	  << "TLS handshake completed (hostname: " << ZuCSpan(hostname)
	  << " TLS: " << tlsver << " ALPN: " << alpn
	  << " resumed: " << (resumed ? "yes" : "no")
	  << " early_data: " << this->maxEarlyData()
	  << ")\n")
	<< std::flush;
      ZtArray<uint8_t> request;
      request << Request << hostname << Request2;
      {
	auto tx = txStream_();
	tx.append(request.data(), request.length());
	tx << Zi::flush();
      }
      if (app()->payload().length()) {
	auto tx = txStream_();
	tx.append(app()->payload().data(), app()->payload().length());
	tx << Zi::flush();
      }
    }
    void disconnected() {
      std::cerr << "disconnected\n" << std::flush;
      close();
      if (round < app()->repeats())
	connect_();
      else
	app()->done();
    }

    void connectFailed(bool transient) {
      if (transient) {
	std::cerr << "failed to connect (transient)\n" << std::flush;
	app()->setError("transient connect failure");
      } else {
	std::cerr << "failed to connect\n" << std::flush;
	app()->setError("connect failure");
      }
      close();
      app()->done();
    }

    int process(Ztls::RxStream &rx) {
      while (!rx.empty()) {
	auto span = rx.span();
	int n = process_span_(span);
	if (n < 0) return -1;
	if (!n) return 0;
	rx.advance(n);
      }
      return 1;
    }

    int process_span_(ZuSpan<uint8_t> rcvd) {
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
      header = {};
      length = 0;
    }

    unsigned	round = 0;
    unsigned	length = 0;
    ZtString<>	header;
    FILE	*file = nullptr;
  };

  App(unsigned repeats) : m_repeats(repeats) { }
  App(unsigned repeats, unsigned payload_len) : m_repeats(repeats) {
    if (!payload_len) return;
    m_payload.length(payload_len);
    for (unsigned i = 0; i < payload_len; ++i)
      m_payload[i] = uint8_t(i);
  }

  void done() { sem.post(); }
  unsigned repeats() const { return m_repeats; }
  void setError(const char *msg) {
    if (!m_error.xch(1)) {
      std::cerr << "error: " << msg << "\n" << std::flush;
    }
  }
  bool error() const { return m_error.load_(); }
  const ZtArray<uint8_t> &payload() const { return m_payload; }

  ZmSemaphore	sem;
  unsigned	m_repeats = 1;
  ZmAtomic<unsigned> m_error{0};
  ZtArray<uint8_t> m_payload;
};

void usage()
{
  std::cerr << "Usage: ZtlsClient SERVER PORT [CA] [REPEAT]\n" << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 5) usage();

  ZuCSpan server = argv[1];
  unsigned port = ZuBox<unsigned>(argv[2]);

  if (!port) usage();
  auto is_number = [](const char *s) {
    if (!s || !*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
  };
  const char *ca = nullptr;
  unsigned payload_len = 0;
  unsigned repeats = 1;
  if (argc == 4) {
    if (is_number(argv[3]))
      repeats = ZuBox<unsigned>(argv[3]);
    else
      ca = argv[3];
  } else if (argc == 5) {
    ca = argv[3];
    repeats = ZuBox<unsigned>(argv[4]);
  }
  if (!repeats) repeats = 1;
  if (const char *payload = getenv("ZTLS_PAYLOAD")) {
    if (is_number(payload))
      payload_len = ZuBox<unsigned>(payload);
  }

  ZiLog::init("ZtlsClient");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZuCSpan alpn[] = { "http/1.1" };

  App app(repeats, payload_len);

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

  if (!app.init(Ztls::ClientParams(&mx, "3", alpn).caPath(ca))) {
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

  return app.error() ? 1 : 0;
}
