//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Owner service progress, queued-work fairness and deterministic unregistration.

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

using namespace ZuTestUtil;

struct ServiceState {
  ZmScheduler *scheduler;
  ZmSemaphore entered, release, done;
  ZmAtomic<unsigned> completed{0};
  unsigned calls = 0, jobs = 0, last = 0, gap = 0, replacements = 0;
  bool ordered = true;

  void idle() {
    if (!calls++) entered.post();
    if (unsigned(completed)) {
      scheduler->serviceFn(1, {});
      done.post();
    }
  }
  void busy() {
    ++calls;
    unsigned n = jobs - last;
    if (n > gap) gap = n;
    last = jobs;
  }
  void replace() {
    ++calls;
    scheduler->serviceFn(1,
      ZmScheduler::ServiceFn{this, ZmFnPtr<&ServiceState::replacement>{}});
  }
  void replacement() {
    ++replacements;
    scheduler->serviceFn(1, {});
    done.post();
  }
  void persistent() {
    if (!calls++) entered.post();
  }
};

static ZmSchedParams params(bool ll)
{
  ZmSchedParams p;
  p.id("service-test").nThreads(1).queueSize(4096).ll(ll);
  p.thread(1).isolated(true);
  return p;
}

static bool await(ZmSemaphore &sem)
{
  return !sem.timedwait(Zm::now() + ZuTime{2});
}

static void idleProgress(bool ll)
{
  ZuTestScopeRT(idleProgress);
  ZmScheduler scheduler{params(ll)};
  ServiceState state{&scheduler};
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  scheduler.run([&state] {
    state.scheduler->serviceFn(1,
      ZmScheduler::ServiceFn{&state, ZmFnPtr<&ServiceState::idle>{}});
  }, 1);
  bool entered = await(state.entered);
  // Model a final CQ published from another core, with no scheduler task/wake.
  state.completed = 1;
  bool progressed = await(state.done);
  bool stopped = scheduler.stop();
  ZuCheckRT(entered && progressed && stopped && state.calls);
  uint64_t in = 0, out = 0;
  scheduler.queue(1).stats(in, out);
  ZuCheckRT(!in && !out && !scheduler.ring(1).count_());
}

static void busyProgress(bool ll)
{
  ZuTestScopeRT(busyProgress);
  ZmScheduler scheduler{params(ll)};
  ServiceState state{&scheduler};
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  scheduler.run([&state] {
    state.scheduler->serviceFn(1,
      ZmScheduler::ServiceFn{&state, ZmFnPtr<&ServiceState::busy>{}});
    state.entered.post();
    state.release.wait();
  }, 1);
  bool entered = await(state.entered);
  enum { Jobs = ZmScheduler::ServiceQuantum * 4 };
  for (unsigned i = 0; i < Jobs; ++i) {
    scheduler.run([&state, i] {
      state.ordered &= state.jobs == i;
      ++state.jobs;
    }, 1);
  }
  scheduler.run([&state] {
    state.scheduler->serviceFn(1, {});
    state.done.post();
  }, 1);
  state.release.post();
  bool progressed = await(state.done);
  bool stopped = scheduler.stop();
  ZuCheckRT(entered && progressed && stopped && state.jobs == Jobs && state.ordered);
  ZuCheckRT(state.calls >= 4 && state.gap <= ZmScheduler::ServiceQuantum &&
    state.jobs - state.last <= ZmScheduler::ServiceQuantum);
  uint64_t in = 0, out = 0;
  scheduler.queue(1).stats(in, out);
  ZuCheckRT(in && in == out && !scheduler.queue(1).count_() &&
    !scheduler.ring(1).count_());
}

static void replaceService(bool ll)
{
  ZuTestScopeRT(replaceService);
  ZmScheduler scheduler{params(ll)};
  ServiceState state{&scheduler};
  scheduler.serviceFn(1,
    ZmScheduler::ServiceFn{&state, ZmFnPtr<&ServiceState::replace>{}});
  bool started = scheduler.start();
  ZuCheckRT(started);
  if (!started) return;
  bool replaced = await(state.done);
  // An owner barrier proves self-unregistration precedes later queued work.
  scheduler.run([&state] { state.done.post(); }, 1);
  bool barrier = await(state.done);
  bool stopped = scheduler.stop();
  ZuCheckRT(replaced && barrier && stopped && state.calls == 1 && state.replacements == 1);
  // Re-arm on the same stopped slot; a continuously armed service must not
  // prevent EOF/join, and stopped unregistration releases the borrowed context.
  scheduler.serviceFn(1,
    ZmScheduler::ServiceFn{&state, ZmFnPtr<&ServiceState::persistent>{}});
  state.calls = 0;
  started = scheduler.start();
  bool entered = started && await(state.entered);
  stopped = scheduler.stop();
  scheduler.serviceFn(1, {});
  ZuCheckRT(started && entered && stopped && state.calls);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(idleProgress, false);
  ZuTestCall(idleProgress, true);
  ZuTestCall(busyProgress, false);
  ZuTestCall(busyProgress, true);
  ZuTestCall(replaceService, false);
  ZuTestCall(replaceService, true);
  return 0;
}
