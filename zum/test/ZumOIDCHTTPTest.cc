//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmBlock.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiLog.hh>

#include "../src/zumd_oidc.hh"

using namespace ZuTestUtil;

static ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(3)
	.thread(1, [](auto &t) { t.name("rx"); })
	.thread(2, [](auto &t) { t.name("tx"); })
	.thread(3, [](auto &t) { t.name("oidc-http"); });
    })
    .rxThread(1).txThread(2);
}

static void oidcHTTP()
{
  ZuTestScope(oidcHTTP);
  ZiMultiplex mx{mxParams()};
  ZuCheck(mx.start());
  Zum::OIDCHTTP http;
  ZuCheck(!http.init(&mx, 0));
  ZuCheck(!http.init(&mx, 1));
  ZuCheck(!http.init(&mx, 2));
  ZuCheck(!http.init(&mx, 4));
  ZuCheck(!http.init(&mx, 3, 0));
  ZuCheck(http.init(&mx, 3));
  auto send = http.fn();

  for (ZuCSpan url: {"", "http://example.com", "https://",
      "https://example.com/#fragment"}) {
    ZuCheck(ZmBlock<bool>{}([&send, &mx, url](auto wake) mutable {
      send(Zum::OIDCHTTPRequest{.url = url},
	[&mx, wake = ZuMv(wake)](unsigned status, Zum::String body) mutable {
	  wake(!status && !body && mx.invoked(3));
	});
    }));
  }
  // final() posts behind this request and must drain its callback before
  // returning; the callback's stack references remain valid until then.
  bool drained = false;
  send(Zum::OIDCHTTPRequest{.url = "http://example.com"},
    [&mx, &drained](unsigned status, Zum::String body) {
      drained = !status && !body && mx.invoked(3);
    });
  http.final();
  ZuCheck(drained);
  ZuCheck(mx.stop());
}

static void publicOIDC()
{
  ZuTestScope(publicOIDC);
  ZiMultiplex mx{mxParams()};
  ZuCheck(mx.start());
  Zum::OIDCHTTP http;
  ZuCheck(http.init(&mx, 3, Zum::OIDCHTTP::DefaultOrigins,
      ZuCSpan{::getenv("ZUM_OIDC_TEST_CA")}));
  auto send = http.fn();
  ZmSemaphore done;
  Zum::String body;
  unsigned status = 0;
  ZuCSpan url = ::getenv("ZUM_OIDC_TEST_URL");
  send(Zum::OIDCHTTPRequest{
      .url = url ? url : ZuCSpan{"http://example.com"}},
    [&done, &status, &body](unsigned status_, Zum::String body_) mutable {
      status = status_;
      body = ZuMv(body_);
      done.post();
    });
  ZuCheck(!done.timedwait(Zm::now() + ZuTime{30}));
  ZuCheck(status == (url ? 200U : 0U));
  ZuCheck(bool(body) == bool(url));
  http.final();
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZumOIDCHTTPTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(oidcHTTP);
  ZuTestCall(publicOIDC);
  ZiLog::stop();
  return 0;
}
