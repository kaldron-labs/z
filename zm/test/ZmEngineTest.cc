//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmEngine.hh>

using namespace ZuTestUtil;

struct TestEngine : public ZmEngine<TestEngine> {
  int startCalls = 0;
  int stopCalls = 0;
  int stateChanges = 0;

  void start_() { ++startCalls; }
  void stop_() { ++stopCalls; }

  void completeStart(bool ok = true) { started(ok); }
  void completeStop(bool ok = true) { stopped(ok); }

  void stateChanged() {
    ++stateChanges;
  }

  template <typename L>
  bool spawn(L &&l) {
    ZuFwd<L>(l)();
    return true;
  }

  void wake() { stopped(); }
};

void testIdempotenceAndCallbacks()
{
  ZuTestScope(testIdempotenceAndCallbacks);

  using namespace ZmEngineState;

  TestEngine e;
  ZuCheck(e.state() == Stopped);

  int startCb = 0;
  e.start(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++startCb;
  }});
  ZuCheck(e.state() == Starting);
  ZuCheck(e.startCalls == 1);

  e.completeStart(true);
  ZuCheck(e.state() == Running);
  ZuCheck(startCb == 1);

  int startCb2 = 0;
  e.start(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++startCb2;
  }});
  ZuCheck(startCb2 == 1); // idempotent when already running
  ZuCheck(e.startCalls == 1);

  int stopCb = 0;
  e.stop(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  }});
  ZuCheck(e.state() == Stopping);
  ZuCheck(e.stopCalls == 1);

  e.completeStop(true);
  ZuCheck(e.state() == Stopped);
  ZuCheck(stopCb == 1);

  int stopCb2 = 0;
  e.stop(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++stopCb2;
  }});
  ZuCheck(stopCb2 == 1); // idempotent when already stopped
  ZuCheck(e.stopCalls == 1);
}

void testStopPendingTransition()
{
  ZuTestScope(testStopPendingTransition);

  using namespace ZmEngineState;

  TestEngine e;
  int startCb = 0;
  int stopCb = 0;

  e.start(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++startCb;
  }});
  ZuCheck(e.state() == Starting);

  e.stop(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  }});
  ZuCheck(e.state() == StopPending);

  e.completeStart(true); // should immediately queue stop path
  ZuCheck(e.state() == Stopping);
  ZuCheck(e.startCalls == 1);
  ZuCheck(e.stopCalls == 1);
  ZuCheck(startCb == 1);

  e.completeStop(true);
  ZuCheck(e.state() == Stopped);
  ZuCheck(stopCb == 1);
}

void testStartPendingTransition()
{
  ZuTestScope(testStartPendingTransition);

  using namespace ZmEngineState;

  TestEngine e;
  int startCb1 = 0;
  int stopCb = 0;
  int startCb2 = 0;

  e.start(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++startCb1;
  }});
  e.completeStart(true);
  ZuCheck(e.state() == Running);

  e.stop(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  }});
  ZuCheck(e.state() == Stopping);

  e.start(ZmFn<void(bool)>{[&](bool ok) {
    ZuCheck(ok);
    ++startCb2;
  }});
  ZuCheck(e.state() == StartPending);

  e.completeStop(true); // should trigger restart
  ZuCheck(e.state() == Starting);

  e.completeStart(true);
  ZuCheck(e.state() == Running);

  ZuCheck(startCb1 == 1);
  ZuCheck(stopCb == 1);
  ZuCheck(startCb2 == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testIdempotenceAndCallbacks);
  ZuTestCall(testStopPendingTransition);
  ZuTestCall(testStartPendingTransition);
  return 0;
}
