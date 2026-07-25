//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Ztcp.hh>

using namespace ZuTestUtil;

#define ZTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

namespace {

struct App : public Ztcp::Hub<App> {
};

Ztcp::ErrorFn countErrors(ZmAtomic<unsigned> &errors)
{
  return Ztcp::ErrorFn{[&errors](ZeException) { errors.xchAdd(1); }};
}

ZiMxParams mxParams()
{
  return ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
	.thread(1, [](auto &t) { t.isolated(1); })
	.thread(2, [](auto &t) { t.isolated(1); })
	.thread(3, [](auto &t) { t.isolated(1); })
	.thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
}

void testValidation()
{
  ZuTestScopeRT(testValidation);

  ZmAtomic<unsigned> errors{0};

  {
    App app;
    ZTCP_CHECK_RT(!app.init(Ztcp::HubParams(nullptr, "3", "4").
	errorFn(countErrors(errors))),
      "null multiplexer unexpectedly succeeded");
  }

  ZiMultiplex stoppedMx(mxParams());
  {
    App app;
    ZTCP_CHECK_RT(!app.init(Ztcp::HubParams(&stoppedMx, "9", "4").
	errorFn(countErrors(errors))),
      "invalid Rx thread unexpectedly succeeded");
  }
  {
    App app;
    ZTCP_CHECK_RT(!app.init(Ztcp::HubParams(&stoppedMx, "3", "9").
	errorFn(countErrors(errors))),
      "invalid Tx thread unexpectedly succeeded");
  }
  {
    App app;
    ZTCP_CHECK_RT(!app.init(Ztcp::HubParams(&stoppedMx, "3", "3").
	errorFn(countErrors(errors))),
      "same Rx/Tx thread unexpectedly succeeded");
  }
  {
    App app;
    ZTCP_CHECK_RT(!app.init(Ztcp::HubParams(&stoppedMx, "3", "4").
	errorFn(countErrors(errors))),
      "non-running multiplexer unexpectedly succeeded");
  }

  ZiMultiplex mx(mxParams());
  bool mxStarted = mx.start();
  ZTCP_CHECK_RT(mxStarted, "ZiMultiplex start failed");
  if (!mxStarted) return;

  {
    App app;
    ZTCP_CHECK_RT(app.init(Ztcp::HubParams(&mx, "3", "4").
	errorFn(countErrors(errors))),
      "valid TCP hub initialization failed");
    ZTCP_CHECK_RT(app.rxThread() == 3, "unexpected Rx thread");
    ZTCP_CHECK_RT(app.txThread() == 4, "unexpected Tx thread");
    app.final();
  }

  mx.stop();
  ZTCP_CHECK_RT(errors.load_() == 5, "unexpected validation error count");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZtcpInitTest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(testValidation);
  ZiLog::stop();
  return 0;
}
