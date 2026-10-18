//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
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

struct InlineEngine : ZmNHeap<InlineEngine>, ZmEngine<InlineEngine> {
friend inline ZuStringT<"ZmEngineTest.Inline"> ZmNHeapID(InlineEngine *);
  unsigned *changes;
  bool startOK;

  InlineEngine(unsigned *changes_, bool startOK_) :
    changes{changes_}, startOK{startOK_} { }
  void stateChanged() { ++*changes; }
  void start_() { started(startOK); }
  void stop_() { stopped(true); }
};

void testInlineRelease()
{
  ZuTestScope(testInlineRelease);
  unsigned changes = 0, completedChanges = 0;
  auto engine = new InlineEngine{&changes, false};
  engine->start([&engine, &changes, &completedChanges](bool ok) {
    ZuCheck(!ok);
    delete engine;
    engine = nullptr;
    completedChanges = changes;
  });
  ZuCheck(!engine && changes == completedChanges);

  engine = new InlineEngine{&changes, true};
  ZuCheck(engine->start());
  engine->stop([&engine, &changes, &completedChanges](bool ok) {
    ZuCheck(ok);
    delete engine;
    engine = nullptr;
    completedChanges = changes;
  });
  ZuCheck(!engine && changes == completedChanges);
}

void testIdempotenceAndCallbacks()
{
  ZuTestScope(testIdempotenceAndCallbacks);

  using namespace ZmEngineState;

  TestEngine e;
  ZuCheck(e.state() == Stopped);
  ZuCheck(!e.running());
  ZuCheck(!e.stopping());

  int startCb = 0;
  e.start([&startCb](bool ok) {
    ZuCheck(ok);
    ++startCb;
  });
  ZuCheck(e.state() == Starting);
  ZuCheck(e.running());
  ZuCheck(!e.stopping());
  ZuCheck(e.startCalls == 1);

  e.completeStart(true);
  ZuCheck(e.state() == Running);
  ZuCheck(e.running());
  ZuCheck(!e.stopping());
  ZuCheck(startCb == 1);

  int startCb2 = 0;
  e.start([&startCb2](bool ok) {
    ZuCheck(ok);
    ++startCb2;
  });
  ZuCheck(startCb2 == 1); // idempotent when already running
  ZuCheck(e.startCalls == 1);

  int stopCb = 0;
  e.stop([&stopCb](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  });
  ZuCheck(e.state() == Stopping);
  ZuCheck(!e.running());
  ZuCheck(e.stopping());
  ZuCheck(e.stopCalls == 1);

  e.completeStop(true);
  ZuCheck(e.state() == Stopped);
  ZuCheck(!e.running());
  ZuCheck(!e.stopping());
  ZuCheck(stopCb == 1);

  int stopCb2 = 0;
  e.stop([&stopCb2](bool ok) {
    ZuCheck(ok);
    ++stopCb2;
  });
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

  e.start([&startCb](bool ok) {
    ZuCheck(ok);
    ++startCb;
  });
  ZuCheck(e.state() == Starting);

  e.stop([&stopCb](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  });
  ZuCheck(e.state() == StopPending);
  ZuCheck(!e.running());
  ZuCheck(!e.stopping());

  e.completeStart(true); // should immediately queue stop path
  ZuCheck(e.state() == Stopping);
  ZuCheck(!e.running());
  ZuCheck(e.stopping());
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

  e.start([&startCb1](bool ok) {
    ZuCheck(ok);
    ++startCb1;
  });
  e.completeStart(true);
  ZuCheck(e.state() == Running);

  e.stop([&stopCb](bool ok) {
    ZuCheck(ok);
    ++stopCb;
  });
  ZuCheck(e.state() == Stopping);
  ZuCheck(!e.running());
  ZuCheck(e.stopping());

  e.start([&startCb2](bool ok) {
    ZuCheck(ok);
    ++startCb2;
  });
  ZuCheck(e.state() == StartPending);
  ZuCheck(!e.running());
  ZuCheck(e.stopping());

  e.completeStop(true); // should trigger restart
  ZuCheck(e.state() == Starting);

  e.completeStart(true);
  ZuCheck(e.state() == Running);

  ZuCheck(startCb1 == 1);
  ZuCheck(stopCb == 1);
  ZuCheck(startCb2 == 1);
}

struct ReleaseEngine : ZmNHeap<ReleaseEngine>, ZmEngine<ReleaseEngine> {
friend inline ZuStringT<"ZmEngineTest.Release"> ZmNHeapID(ReleaseEngine *);
  unsigned *starts;
  unsigned *stops;
  bool deferStop = true;

  ReleaseEngine(unsigned *starts_, unsigned *stops_) : starts{starts_}, stops{stops_} { }
  void start_() { ++*starts; }
  void stop_() { ++*stops; if (!deferStop) stopped(true); }
  void completeStart(bool ok = true) { started(ok); }
  void completeStop(bool ok = true) { stopped(ok); }
};

void testPendingRelease()
{
  ZuTestScope(testPendingRelease);
  unsigned starts = 0, stops = 0, stopped = 0;
  auto engine = new ReleaseEngine{&starts, &stops};
  engine->deferStop = false;
  engine->start([&engine](bool ok) {
    ZuCheck(ok && engine->state() == ZmEngineState::Stopping);
    // This reentrant stop joins the already reserved stop. It must not run
    // inline and release the owner before started() dispatches that hook.
    engine->stop([&engine](bool ok) {
      ZuCheck(ok);
      delete engine;
      engine = nullptr;
    });
  });
  engine->stop([&stopped](bool ok) { stopped += ok; });
  engine->completeStart();
  ZuCheck(!engine && starts == 1 && stops == 1 && stopped == 1);

  starts = stops = stopped = 0;
  unsigned restarted = 0;
  engine = new ReleaseEngine{&starts, &stops};
  engine->start([](bool ok) { ZuCheck(ok); });
  engine->completeStart();
  engine->stop([&engine, &stopped](bool ok) {
    ZuCheck(ok && engine->state() == ZmEngineState::Starting);
    ++stopped;
    engine->deferStop = false;
    engine->stop([&engine](bool ok) {
      ZuCheck(ok);
      delete engine;
      engine = nullptr;
    });
  });
  engine->start([&restarted](bool ok) { restarted += ok; });
  engine->completeStop();
  ZuCheck(engine && engine->state() == ZmEngineState::StopPending && starts == 2);
  engine->completeStart();
  ZuCheck(!engine && starts == 2 && stops == 2 && stopped == 1 && restarted == 1);
}

void testPendingFailures()
{
  ZuTestScope(testPendingFailures);
  unsigned starts = 0, stops = 0, stopped = 0;
  auto engine = new ReleaseEngine{&starts, &stops};
  engine->start([&engine](bool ok) {
    ZuCheck(!ok && engine->state() == ZmEngineState::Stopped);
    delete engine;
    engine = nullptr;
  });
  engine->stop([&stopped](bool ok) { stopped += ok; });
  engine->completeStart(false);
  ZuCheck(!engine && starts == 1 && !stops && stopped == 1);

  starts = stops = 0;
  unsigned restarted = 0;
  engine = new ReleaseEngine{&starts, &stops};
  engine->start([](bool ok) { ZuCheck(ok); });
  engine->completeStart();
  engine->stop([&engine](bool ok) {
    ZuCheck(!ok && engine->state() == ZmEngineState::Starting);
    engine->deferStop = false;
    engine->stop([&engine](bool ok) {
      ZuCheck(ok);
      delete engine;
      engine = nullptr;
    });
  });
  engine->start([&restarted](bool ok) { restarted += ok; });
  engine->completeStop(false);
  ZuCheck(engine && engine->state() == ZmEngineState::StopPending && starts == 2);
  engine->completeStart();
  ZuCheck(!engine && starts == 2 && stops == 2 && restarted == 1);
}

void testFailedStopRelease()
{
  ZuTestScope(testFailedStopRelease);
  unsigned starts = 0, stops = 0, completed = 0;
  auto engine = new ReleaseEngine{&starts, &stops};
  engine->start([](bool ok) { ZuCheck(ok); });
  engine->completeStart();
  engine->stop([&engine, &completed](bool ok) {
    ZuCheck(!ok && engine->state() == ZmEngineState::Stopped);
    ++completed;
    delete engine;
    engine = nullptr;
  });
  engine->stop([&completed](bool ok) {
    ZuCheck(!ok);
    ++completed;
  });
  engine->completeStop(false);
  ZuCheck(!engine && completed == 2 && starts == 1 && stops == 1);
}

void testCancelledOpposite(bool ok)
{
  ZuTestScope(testCancelledOpposite);
  TestEngine engine;
  unsigned starts = 0, stops = 0;
  engine.start([&starts, ok](bool result) { starts += result == ok; });
  engine.stop([&stops, ok](bool result) { stops += result == !ok; });
  engine.start([&starts, ok](bool result) { starts += result == ok; });
  ZuCheck(engine.state() == ZmEngineState::Starting && !starts && !stops);
  engine.completeStart(ok);
  ZuCheck(starts == 2 && stops == 1);
  ZuCheck(engine.state() == (ok ? ZmEngineState::Running : ZmEngineState::Stopped));
  if (!ok) {
    engine.start([](bool result) { ZuCheck(result); });
    engine.completeStart();
  }
  starts = stops = 0;
  engine.stop([&stops, ok](bool result) { stops += result == ok; });
  engine.start([&starts](bool result) { starts += !result; });
  engine.stop([&stops, ok](bool result) { stops += result == ok; });
  ZuCheck(engine.state() == ZmEngineState::Stopping && !starts && !stops);
  engine.completeStop(ok);
  ZuCheck(stops == 2 && starts == 1);
  ZuCheck(engine.state() == ZmEngineState::Stopped);
  unsigned calls = engine.stopCalls;
  engine.stop([](bool result) { ZuCheck(result); });
  ZuCheck(engine.stopCalls == calls);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testInlineRelease);
  ZuTestCall(testIdempotenceAndCallbacks);
  ZuTestCall(testStopPendingTransition);
  ZuTestCall(testStartPendingTransition);
  ZuTestCall(testPendingRelease);
  ZuTestCall(testPendingFailures);
  ZuTestCall(testFailedStopRelease);
  ZuTestCall(testCancelledOpposite, true);
  ZuTestCall(testCancelledOpposite, false);
  return 0;
}
