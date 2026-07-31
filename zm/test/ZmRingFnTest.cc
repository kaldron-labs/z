//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <sstream>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAlloc.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmBackTracer.hh>

using namespace ZuTestUtil;

static int g_statelessCalls = 0;
using RingFn = ZmRingFn_<ZmRingFn_Defaults>;

void testStatelessLambdaPath()
{
  ZuTestScope(testStatelessLambdaPath);

  auto stateless = [] { ++g_statelessCalls; };
  RingFn fn{stateless};

  unsigned n = fn.pushSize();
  ZuCheck(n > 0);

  auto msg = ZmAlloc(uint8_t, n);
  fn.push(msg.data);
  unsigned used = RingFn::invoke(msg.data);

  ZuCheck(used == n);
  ZuCheck(g_statelessCalls > 0);
}

void testStatefulMoveAndHeapPromotion()
{
  ZuTestScope(testStatefulMoveAndHeapPromotion);

  int total = 0;

  {
    int add = 7;
    auto stateful = [&total, add] { total += add; };

    RingFn fn{stateful};
    unsigned n = fn.pushSize();
    ZuCheck(n > sizeof(void *));

    auto msg = ZmAlloc(uint8_t, n);
    fn.push(msg.data);
    ZuCheck(RingFn::invoke(msg.data) == n);
  }

  // move-assignment path heap-promotes captured state
  RingFn moved;
  {
    int add = 9;
    auto stateful = [&total, add] { total += add; };
    moved = stateful;
  }

  unsigned n2 = moved.pushSize();
  ZuCheck(n2 > sizeof(void *));

  auto msg2 = ZmAlloc(uint8_t, n2);
  moved.push(msg2.data);
  ZuCheck(RingFn::invoke(msg2.data) == n2);

  ZuCheck(total == (7 + 9));
}

void testExceptionSwallowingAndBacktraceSmoke()
{
  ZuTestScope(testExceptionSwallowingAndBacktraceSmoke);

  int ran = 0;
  auto throwing = [&ran] {
    ++ran;
    throw 1;
  };

  RingFn fn{throwing};
  unsigned n = fn.pushSize();
  auto msg = ZmAlloc(uint8_t, n);
  fn.push(msg.data);

  // invoke() is expected to swallow exceptions thrown by lambda payloads
  ZuCheck(RingFn::invoke(msg.data) == n);
  ZuCheck(ran == 1);

  ZmBackTrace trace;
  trace.capture();
  ZuCheck(!(!trace));

  ZmBackTracer<4> tracer;
  tracer.capture();
  std::ostringstream os;
  tracer.dump(os);
  ZuCheck(os.str().length() >= 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStatelessLambdaPath);
  ZuTestCall(testStatefulMoveAndHeapPromotion);
  ZuTestCall(testExceptionSwallowingAndBacktraceSmoke);
  return 0;
}
