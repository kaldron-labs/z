//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Upstream transport lifecycle against reserved loopback endpoints, not an IdP.

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiPlatform.hh>
#include "../src/ZumUpstream.hh"

using namespace ZuTestUtil;

class Endpoint {
public:
  ~Endpoint() { if (!Zi::nullSocket(m_socket)) Zi::closeSocket(m_socket); }
  bool open(bool listen = false) {
    m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (Zi::nullSocket(m_socket)) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(m_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)))
      return false;
#ifdef _WIN32
    int length = sizeof(addr);
#else
    socklen_t length = sizeof(addr);
#endif
    if (::getsockname(m_socket, reinterpret_cast<sockaddr *>(&addr), &length))
      return false;
    m_port = ntohs(addr.sin_port);
    // One pending TLS connection is enough to exercise shutdown/saturation.
    return !listen || !::listen(m_socket, 1);
  }
  Zum::String url() const {
    Zum::String value{"https://127.0.0.1:"};
    value << m_port << '/';
    return value;
  }
private:
  Zi::Socket m_socket = Zi::nullSocket();
  uint16_t m_port = 0;
};

static ZiMxParams params()
{
  return ZiMxParams().scheduler([](auto &s) {
    s.nThreads(3)
      .thread(1, [](auto &t) { t.isolated(true); t.name("rx"); })
      .thread(2, [](auto &t) { t.isolated(true); t.name("tx"); })
      .thread(3, [](auto &t) { t.isolated(true); t.name("upstream"); });
  }).rxThread(1).txThread(2);
}

static void eviction()
{
  ZuTestScope(eviction);
  Endpoint a, b;
  ZuCheck(a.open() && b.open());
  ZiMultiplex mx{params()};
  ZuCheck(mx.start());
  Zum::UpstreamHTTP http;
  ZuCheck(http.init(&mx, 3, 1));
  auto send = http.fn();
  // Bound but non-listening sockets reserve the ports and refuse connections.
  // Alternating two origins forces reuse of the sole idle origin slot.
  bool sent = true;
  for (unsigned i = 0; i < 4; ++i) {
    auto url = (i & 1) ? b.url() : a.url();
    sent &= ZmBlock<bool>{}([&send, &mx, url = ZuMv(url)](auto wake) mutable {
      send(Zum::OIDCHTTPRequest{.url = ZuMv(url)},
	[&mx, wake = ZuMv(wake)](unsigned status, Zum::String body) mutable {
	  wake(!status && !body && mx.invoked(3));
	});
    });
  }
  ZuCheck(sent);
  http.final();
  ZuCheck(mx.stop());
}

static void saturation()
{
  ZuTestScope(saturation);
  Endpoint held, other;
  ZuCheck(held.open(true) && other.open());
  ZiMultiplex mx{params()};
  ZuCheck(mx.start());
  Zum::UpstreamHTTP http;
  ZuCheck(http.init(&mx, 3, 1));
  auto send = http.fn();
  unsigned completed = 0;
  bool cancelled = false;
  send(Zum::OIDCHTTPRequest{.url = held.url()},
    [&mx, &completed, &cancelled](unsigned status, Zum::String body) {
      ++completed;
      cancelled = !status && !body && mx.invoked(3);
    });
  ZuCheck(ZmBlock<bool>{}([&send, &mx, &other](auto wake) mutable {
    send(Zum::OIDCHTTPRequest{.url = other.url()},
      [&mx, wake = ZuMv(wake)](unsigned status, Zum::String body) mutable {
	wake(status == 503 && !body && mx.invoked(3));
      });
  }));
  http.final();
  ZuCheck(completed == 1 && cancelled);
  ZuCheck(mx.stop());
}

static void startupDrain()
{
  ZuTestScope(startupDrain);
  Endpoint held;
  ZuCheck(held.open(true));
  ZiMultiplex mx{params()};
  ZuCheck(mx.start());
  Zum::UpstreamHTTP http;
  ZuCheck(http.init(&mx, 3, 1));
  auto send = http.fn();
  unsigned completed = 0;
  bool cancelled = false;
  send(Zum::OIDCHTTPRequest{.url = held.url()},
    [&mx, &completed, &cancelled](unsigned status, Zum::String body) {
      ++completed;
      cancelled = !status && !body && mx.invoked(3);
    });
  http.final();
  ZuCheck(completed == 1 && cancelled);
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("zumupstreamtest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(eviction);
  ZuTestCall(saturation);
  ZuTestCall(startupDrain);
  ZiLog::stop();
  return 0;
}
