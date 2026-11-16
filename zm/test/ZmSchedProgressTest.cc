//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Overflow publication between the last queue check and empty-ring polling.

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmRingFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include "ZmSchedProgressTest.hh"

using namespace ZuTestUtil;

struct ProgressGate {
  ZmScheduler *scheduler = nullptr;
  ZmSemaphore workerEntered, workerRelease;
  ZmSemaphore overflowEntered, overflowRelease, submitted;
  ZmSemaphore shiftEntered, shiftRelease, done;
  ZmAtomic<unsigned> phase{0};
  unsigned fillers = 0, order = 0;
  bool ordered = false;
};

static ProgressGate *active = nullptr;

void ZmSchedProgressTest::hook(
    unsigned phase, const ZmScheduler *scheduler, bool empty)
{
  auto gate = active;
  if (!gate || gate->scheduler != scheduler) return;
  if (phase == Overflow && gate->phase.cmpXch(1, 0) == 0) {
    gate->overflowEntered.post();
    gate->overflowRelease.wait();
  } else if (phase == Shift && empty && gate->phase.cmpXch(3, 2) == 2) {
    gate->shiftEntered.post();
    gate->shiftRelease.wait();
  }
}

// Failure-only recovery: the old LL1 shift cannot see queued overflow work.
// A direct ring item releases it without adding another overflow obligation.
static bool rescue(ZmScheduler &scheduler)
{
  using Ring = ZmRing<ZmRingMW<true>>;
  auto &ring = const_cast<Ring &>(static_cast<const Ring &>(scheduler.ring(1)));
  auto noop = [] {};
  ZmRingFn<ZmRingFn_Defaults> fn{noop};
  unsigned size = fn.pushSize();
  void *ptr = ring.tryPush(size);
  if (!ptr) return false;
  fn.push(ptr);
  ring.push2(ptr, size);
  return true;
}

static void overflowProgress()
{
  ZuTestScopeRT(overflowProgress);
  ProgressGate gate;
  ZmSchedParams params;
  params.id("overflow-progress").nThreads(1).queueSize(4096).ll(true);
  params.thread(1).isolated(true);
  ZmScheduler scheduler{ZuMv(params)};
  gate.scheduler = &scheduler;
  active = &gate;
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) { active = nullptr; return; }
  scheduler.run([&gate] {
    gate.workerEntered.post();
    gate.workerRelease.wait();
  }, 1);
  gate.workerEntered.wait();

  struct Large {
    ProgressGate *gate;
    char padding[512];
    void operator()() { gate->order = 1; }
  };
  unsigned fillers = 0;
  auto &ring = scheduler.ring(1);
  unsigned capacity = ring.size();
  while (capacity - ring.count_() >=
      sizeof(Large) + sizeof(void *)) {
    scheduler.run([&gate] { ++gate.fillers; }, 1);
    ++fillers;
  }
  ZmThread producer{[&scheduler, &gate] {
    scheduler.run(Large{&gate, {}}, 1);
    gate.submitted.post();
  }};
  gate.overflowEntered.wait();
  // The failed ring admission is known, but overflow is not published yet.
  gate.phase = 2;
  gate.workerRelease.post();
  gate.shiftEntered.wait();
  bool empty = !scheduler.ring(1).count_() && !scheduler.queue(1).count_();
  gate.overflowRelease.post();
  gate.submitted.wait();
  producer.join();
  bool overflowOnly = !scheduler.ring(1).count_() &&
    scheduler.queue(1).count_() == 1;
  // A small callback must remain behind the already published large callback.
  scheduler.run([&gate] {
    gate.ordered = gate.order == 1;
    ++gate.order;
    gate.done.post();
  }, 1);
  bool queued = scheduler.queue(1).count_() == 2;
  gate.shiftRelease.post();

  bool progressed = !gate.done.timedwait(Zm::now() + ZuTime{2});
  ZuCheckRT(empty && overflowOnly && queued);
  ZuCheckRT(progressed);
  if (!progressed) {
    // Bound failure cleanup; do not leave the baseline stuck in shift forever.
    bool rescued = rescue(scheduler);
    ZuCheckRT(rescued);
    if (!rescued || gate.done.timedwait(Zm::now() + ZuTime{2})) Zm::exit(1);
  }
  bool stopped = scheduler.stop();
  active = nullptr;
  uint64_t in = 0, out = 0;
  scheduler.queue(1).stats(in, out);
  ZuCheckRT(stopped && gate.fillers == fillers && gate.ordered && gate.order == 2);
  ZuCheckRT(in == 2 && out == 2 && !scheduler.queue(1).count_() &&
    !scheduler.ring(1).count_());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(overflowProgress);
  return 0;
}
