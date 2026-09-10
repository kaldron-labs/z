//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiLog.hh>

#include "../src/ZumUpstream.hh"

using namespace ZuTestUtil;

static ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(2)
	.thread(1, [](auto &t) { t.name("rx"); })
	.thread(2, [](auto &t) { t.name("tx"); });
    })
    .rxThread(1).txThread(2);
}

static void upstream()
{
  ZuTestScope(upstream);
  ZiMultiplex mx{mxParams()};
  ZuCheck(mx.start());
  Zum::UpstreamHTTP http;
  ZuCheck(http.init(&mx));
  auto send = http.fn();

  unsigned status = unsigned(-1);
  send(Zum::OIDCHTTPRequest{.url = "http://example.com"},
    [&status](unsigned status_, Zum::String) { status = status_; });
  ZuCheck(status == 0);
  http.final();
  ZuCheck(mx.stop());
}

static void publicUpstream()
{
  ZuTestScope(publicUpstream);
  ZiMultiplex mx{mxParams()};
  ZuCheck(mx.start());
  Zum::UpstreamHTTP http;
  ZuCheck(http.init(&mx));
  auto send = http.fn();
  ZmSemaphore done;
  Zum::String body;
  unsigned status = 0;
  ZuCSpan url = ::getenv("ZUM_UPSTREAM_TEST_URL");
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
  ZiLog::init("ZumUpstreamTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(upstream);
  ZuTestCall(publicUpstream);
  ZiLog::stop();
  return 0;
}
