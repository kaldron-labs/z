//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// process shutdown and diagnostic wait coordination tests

#include <signal.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpRuntime.hh>

using namespace ZuTestUtil;

namespace ZhttpRuntimeTest_ {

void lifecycle()
{
  ZuTestScope(lifecycle);
  Zhttp::Runtime runtime;
  Zhttp::Runtime other;

  ZuCHECK(runtime.init(), "initialize");
  ZuCHECK(!runtime.init(), "reject repeated initialization");
  ZuCHECK(!other.init(), "reject competing runtime");
  runtime.stop();
  runtime.wait();
  runtime.final();

  ZuCHECK(other.init(), "initialize after finalization");
  other.stop();
  other.wait();
  other.final();
}

void interval()
{
  ZuTestScope(interval);
  Zhttp::Runtime runtime;
  unsigned calls = 0;

  ZuCHECK(runtime.init(), "initialize");
  runtime.add(1, Zhttp::DiagnosticFn{[&runtime, &calls]() {
    ++calls;
    runtime.stop();
  }});
  ZuCHECK(runtime.wait(1), "completion wins at timeout boundary");
  ZuCHECK(calls == 1, "diagnostic interval");
}

void timeout()
{
  ZuTestScope(timeout);
  Zhttp::Runtime runtime;

  ZuCHECK(runtime.init(), "initialize");
  ZuCHECK(!runtime.wait(1), "timeout");
}

void signal()
{
  ZuTestScope(signal);
  Zhttp::Runtime runtime;

  ZuCHECK(runtime.init(), "initialize");
  ZuCHECK(!raise(SIGINT), "raise SIGINT");
  runtime.wait();
  runtime.final();
}

} // namespace ZhttpRuntimeTest_

int main(int argc, char **argv)
{
  using namespace ZhttpRuntimeTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(lifecycle);
  ZuTestCall(interval);
  ZuTestCall(timeout);
  ZuTestCall(signal);
  return 0;
}
