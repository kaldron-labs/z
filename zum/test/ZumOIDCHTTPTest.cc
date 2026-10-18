//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmBlock.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>

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

static void initFinal()
{
  ZuTestScopeRT(initFinal);
  ZiMultiplex mx{mxParams()}; // its workers are deliberately never started
  Zum::OIDCHTTP http;
  ZuCheckRT(http.init(&mx, 3));
  ZuCheckRT(http.state() == ZmEngineState::Stopped);
  auto send = http.fn();
  http.final();
  bool rejected = false;
  send(Zum::OIDCHTTPRequest{.url = "https://example.com"},
    [&rejected](unsigned status, Zum::String body) {
      rejected = !status && !body;
    });
  ZuCheckRT(rejected);
  ZuCheckRT(http.stop());
  http.final();
}

static void borrowedResolver()
{
  ZuTestScopeRT(borrowedResolver);
  ZiMultiplex mx{mxParams()};
  ZuCheckRT(mx.start());
  ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
  Zum::OIDCHTTP http;
  ZuCheckRT(http.init(&mx, 3));
  // An externally owned resolver must already be running. Its failure to meet
  // that startup dependency leaves no owner job or child client to drain.
  ZuCheckRT(!http.start());
  ZuCheckRT(http.state() == ZmEngineState::Stopped);
  http.final();
  ZuCheckRT(ZiResolver::instance()->initialized() &&
    ZiResolver::instance()->state() == ZmEngineState::Stopped);
  ZiResolver::final();
  ZuCheckRT(mx.stop());
}

static void oidcHTTP()
{
  ZuTestScopeRT(oidcHTTP);
  ZiMultiplex mx{mxParams()};
  ZuCheckRT(mx.start());
  Zum::OIDCHTTP http;
  ZuCheckRT(!http.init(&mx, 0));
  ZuCheckRT(!http.init(&mx, 1));
  ZuCheckRT(!http.init(&mx, 2));
  ZuCheckRT(!http.init(&mx, 4));
  ZuCheckRT(!http.init(&mx, 3, 0));
  ZuCheckRT(http.init(&mx, 3));
  ZuCheckRT(http.start());
  auto send = http.fn();

  for (ZuCSpan url: {"", "http://example.com", "https://",
      "https://example.com/#fragment"}) {
    ZuCheckRT(ZmBlock<bool>{}([&send, &mx, url](auto wake) mutable {
      send(Zum::OIDCHTTPRequest{.url = url},
	[&mx, wake = ZuMv(wake)](unsigned status, Zum::String body) mutable {
	  wake(!status && !body && mx.invoked(3));
	});
    }));
  }
  // stop() includes this accepted request and must drain its callback before
  // completing; the callback's stack references remain valid until then.
  bool drained = false;
  send(Zum::OIDCHTTPRequest{.url = "http://example.com"},
    [&mx, &drained](unsigned status, Zum::String body) {
      drained = !status && !body && mx.invoked(3);
    });
  ZuCheckRT(http.stop());
  ZuCheckRT(drained);
  ZuCheckRT(http.stop());
  ZuCheckRT(http.start());
  ZuCheckRT(http.start());
  ZuCheckRT(ZmBlock<bool>{}([&send, &mx](auto wake) mutable {
    send(Zum::OIDCHTTPRequest{.url = "http://example.com"},
      [&mx, wake = ZuMv(wake)](unsigned status, Zum::String body) mutable {
	wake(!status && !body && mx.invoked(3));
      });
  }));
  ZmSemaphore finalized;
  bool stoppedOK = false;
  http.stop([&http, &stoppedOK, &finalized](bool ok) {
    stoppedOK = ok;
    http.final();
    finalized.post();
  });
  finalized.wait();
  ZuCheckRT(stoppedOK && http.state() == ZmEngineState::Stopped);
  bool rejected = false;
  send(Zum::OIDCHTTPRequest{.url = "https://example.com"},
    [&rejected](unsigned status, Zum::String body) {
      rejected = !status && !body;
    });
  ZuCheckRT(rejected); // the retained function owns only quiescent closed state
  ZuCheckRT(mx.stop());
}

static void publicOIDC()
{
  ZuTestScope(publicOIDC);
  ZiMultiplex mx{mxParams()};
  ZuCheck(mx.start());
  Zum::OIDCHTTP http;
  Zum::String caPath;
  if (auto path = Zt::getpath("ZUM_OIDC_TEST_CA")) caPath = path;
  ZuCheck(http.init(&mx, 3, Zum::OIDCHTTP::DefaultOrigins,
      caPath));
  ZuCheck(http.start());
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
  ZuCheck(http.stop());
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
  ZuTestCall(initFinal);
  ZuTestCall(borrowedResolver);
  ZuTestCall(oidcHTTP);
  ZuTestCall(publicOIDC);
  ZiLog::stop();
  return 0;
}
