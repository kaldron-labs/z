//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZfixWork.hh>

using namespace ZuTestUtil;
using namespace Zfix;
static void budget() {
  ZuTestScope(budget);
  WorkBudget work{{1024, 2}};
  ZuCheck(!work.acquire_(0));
  ZuCheck(work.acquire_(512) && work.acquire_(512));
  ZuCheck(!work.acquire_(1));
  ZuCheck(!work.limits_({1023, 2}) && !work.limits_({1024, 1}));
  work.release_(512);
  ZuCheck(work.limits_({512, 1}) && !work.acquire_(1));
  work.release_(512);
  ZuCheck(work.limits_({1024, 0}) && !work.acquire_(1));
  ZuCheck(work.limits_({0, 1}) && !work.acquire_(1));
  auto state = work.state_();
  ZuCheck(!state.count && !state.bytes && state.peakBytes == 1024 && state.peakCount == 2
    && state.refused == 5);
}
static void overflow() {
  ZuTestScope(overflow);
  WorkBudget work{{UINT64_MAX, 3}};
  ZuCheck(work.acquire_(UINT64_MAX - 1));
  ZuCheck(!work.acquire_(2));
  ZuCheck(work.acquire_(1));
  ZuCheck(!work.acquire_(1));
  work.release_(1); work.release_(UINT64_MAX - 1);
  ZuCheck(!work.state_().count && !work.state_().bytes);
}
struct State {
  bool		rx = true;
  unsigned	destroyed = 0, bad = 0;
};

struct Input : ZmNHeap<Input>, ZmObject {
friend ZuStringT<"Zfix.WorkTest.Input"> ZmNHeapID(Input *);
  State		*state;
  unsigned	value;

  Input(State *s, unsigned v) : state{s}, value{v} { }

  ~Input() { ++state->destroyed; if (!state->rx) ++state->bad; }
};

struct Owner {
  using W = Work<ZmRef<Input>, Owner>;
  WorkBudget	budget{{1024, 1}};
  const W	*pending = nullptr;
  unsigned	posted = 0, bad = 0;

  void workDone(const W *work) { if (pending) ++bad; pending = work; ++posted; }

  void drain() {
    auto work = pending; pending = nullptr;
    auto bytes = work->bytes(); delete work; budget.release_(bytes);
  }
};

static void retirement() {
  ZuTestScope(retirement);
  State state; Owner owner;
  ZmRef<Input> input = new Input{&state, 7};
  ZuCheck(owner.budget.acquire_(128));
  auto work = Owner::W::alloc_(&owner, input, 128); auto copy = work;
  ZuCheck(work->input()->value == 7 && work->bytes() == 128);
  input = nullptr; work = nullptr;
  ZuCheck(!owner.pending && !state.destroyed && owner.budget.state_().count == 1);
  state.rx = false; copy = nullptr; state.rx = true;
  ZuCheck(owner.pending && owner.posted == 1 && !owner.bad && !state.destroyed
    && owner.budget.state_().count == 1);
  owner.drain();
  ZuCheck(state.destroyed == 1 && !state.bad && !owner.budget.state_().count);
}
static void callable() {
  ZuTestScope(callable);
  State state; Owner owner;
  ZmRef<Input> input = new Input{&state, 42};
  ZuCheck(owner.budget.acquire_(128));
  auto work = Owner::W::alloc_(&owner, input, 128);
  ZmFn<void()> fn{[work] { ZuCheck(work->input()->value == 42); }};
  input = nullptr; work = nullptr; fn();
  state.rx = false; fn = {}; state.rx = true;
  ZuCheck(owner.pending && owner.posted == 1 && !owner.bad && !state.destroyed);
  owner.drain();
  ZuCheck(state.destroyed == 1 && !state.bad && !owner.budget.state_().bytes);
}
int main(int argc, char **argv) {
  parse(argc, argv); ZuTestMain();
  ZuTestCall(budget);
  ZuTestCall(overflow);
  ZuTestCall(retirement);
  ZuTestCall(callable);
  return 0;
}
