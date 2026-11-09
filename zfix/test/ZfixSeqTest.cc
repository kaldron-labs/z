//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuObject.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixSeq.hh>

using namespace ZuTestUtil;
using namespace Zfix;

struct Input {
  uint64_t	seqNo;
  unsigned	bytes;

  uint64_t key() const { return seqNo; }
  uint64_t length() const { return 1; }
  unsigned retainedBytes() const { return bytes; }
};

using Queue = ZmPQueue<Input, ZmPQueueHeapID<"Zfix.SeqTest",
  ZmPQueueNode<ZuObject, ZmPQueueOverlap<false, ZmPQueueOverwrite<false>>>>>;
using Values = ZtArray<uint64_t, ZtArrayHeapID<"Zfix.SeqTest.Values">>;
namespace RxHook {

  enum { None, Hold, Advance, Reset, Restore, Resume };
}

struct Rx : public SeqRx<Rx, Queue> {
  Values	processed;
  Values	tasks;
  Values	requests;
  Values	previous;
  Values	retries;
  uint64_t	retry = 0;
  unsigned	task = 0;
  unsigned	cancelled = 0;
  unsigned	retried = 0;
  unsigned	arms = 0;
  unsigned	requestHook = RxHook::None;
  unsigned	retryHook = RxHook::None;
  uint64_t	hookNext = 10;
  bool		hookOK = true;
  uint64_t	stopAt = 0;

  using SeqRx::SeqRx;

  void processRx(Msg *msg) {
    auto seqNo = msg->data().seqNo;
    processed.push(seqNo);
    if (seqNo == stopAt) hold_();
  }

  void requestRx(const Span &prev, const Span &gap) {
    previous.push(prev.key());
    previous.push(prev.length());
    requests.push(gap.key());
    requests.push(gap.length());
    hook(requestHook);
  }

  void reRequestRx(const Span &gap) {
    ++retried;
    retries.push(gap.key());
    retries.push(gap.length());
    hook(retryHook);
  }

  void postRx(uint64_t ticket) { tasks.push(ticket); }

  void armGap(uint64_t ticket) { retry = ticket; ++arms; }

  void cancelGap() { ++cancelled; retry = 0; }

  void hook(unsigned &value) {
    auto action = value;
    value = RxHook::None;
    switch (action) {
      case RxHook::Hold: hold_(); break;
      case RxHook::Advance: hookOK = advance_(hookNext) && hookOK; break;
      case RxHook::Reset:
	hold_();
	hookOK = advance_(hookNext) && reset_(hookNext) && hookOK;
	break;
      case RxHook::Restore:
	hold_();
	hookOK = restore_(hookNext, expected_()) && hookOK;
	break;
      case RxHook::Resume: hold_(); resume_(); break;
    }
  }

  unsigned receive(uint64_t seqNo, unsigned bytes = 20) {
    return receive_(new Msg{Input{seqNo, bytes}});
  }

  void drain() {
    while (task < tasks.length()) drain_(tasks[task++]);
  }
};

static void ordered()
{
  ZuTestScope(ordered);
  Rx rx;
  ZuCheck(rx.expected_() == 1 && !rx.retained_());
  ZuCheck(rx.receive(1) == SeqResult::Accepted);
  ZuCheck(rx.expected_() == 2 && rx.processed.length() == 1);
  ZuCheck(rx.receive(4) == SeqResult::Accepted);
  ZuCheck(rx.expected_() == 2 && rx.retained_() == 1 && rx.retainedBytes_() == 20);
  ZuCheck(rx.requests.length() == 2 && rx.requests[0] == 2 && rx.requests[1] == 2);
  ZuCheck(rx.receive(4, 99) == SeqResult::Duplicate && rx.retainedBytes_() == 20);
  ZuCheck(rx.receive(3) == SeqResult::Accepted);
  ZuCheck(rx.receive(2) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.expected_() == 5 && !rx.retained_() && !rx.retainedBytes_());
  ZuCheck(rx.processed.length() == 4);
  for (unsigned i = 0; i < 4; ++i) ZuCheck(rx.processed[i] == i + 1);
  ZuCheck(rx.receive(2) == SeqResult::Low && rx.expected_() == 5);
  auto retried = rx.retried;
  rx.retry_(rx.retry);
  ZuCheck(rx.retried == retried);
}

static void held()
{
  ZuTestScope(held);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.receive(2) == SeqResult::Accepted);
  ZuCheck(rx.receive(1) == SeqResult::Accepted);
  ZuCheck(!rx.processed.length() && rx.retainedBytes_() == 40);
  rx.resume_();
  ZuCheck(rx.tasks.length() == 1);
  auto stale = rx.tasks[0];
  rx.hold_();
  rx.resume_();
  ZuCheck(rx.tasks.length() == 2);
  rx.drain_(stale);
  ZuCheck(rx.expected_() == 1 && rx.retained_() == 2);
  rx.drain();
  ZuCheck(rx.expected_() == 3 && rx.processed.length() == 2 && !rx.retainedBytes_());
  rx.hold_();
  ZuCheck(rx.receive(4) == SeqResult::Accepted);
  rx.resume_();
  rx.drain();
  ZuCheck(rx.requests.length() == 2 && rx.requests[0] == 3);
  auto retry = rx.retry;
  rx.hold_();
  rx.retry_(retry);
  ZuCheck(!rx.retried);
  rx.resume_();
  rx.drain();
  ZuCheck(rx.retry != retry);
  rx.retry_(rx.retry);
  ZuCheck(rx.retried == 1);
}

static void gapFill()
{
  ZuTestScope(gapFill);
  Rx rx;
  ZuCheck(rx.receive(3) == SeqResult::Accepted);
  ZuCheck(rx.receive(5) == SeqResult::Accepted);
  ZuCheck(rx.receive(6) == SeqResult::Accepted);
  ZuCheck(rx.advance_(5));
  ZuCheck(rx.expected_() == 5 && rx.retained_() == 2 && rx.retainedBytes_() == 40);
  rx.drain();
  ZuCheck(rx.expected_() == 7 && rx.processed.length() == 2);
  ZuCheck(rx.processed[0] == 5 && rx.processed[1] == 6 && !rx.retainedBytes_());
  ZuCheck(!rx.advance_(6) && rx.expected_() == 7);
  ZuCheck(rx.advance_(7));
  // An advancement into an empty prefix must preserve the first later node.
  ZuCheck(rx.receive(12) == SeqResult::Accepted);
  ZuCheck(rx.advance_(10));
  rx.drain();
  ZuCheck(rx.expected_() == 10 && rx.retained_() == 1 && rx.retainedBytes_() == 20);
  ZuCheck(rx.requests.length() >= 2 && rx.requests[rx.requests.length() - 2] == 10);
  ZuCheck(rx.advance_(13));
  rx.drain();
  ZuCheck(!rx.retained_() && !rx.retainedBytes_() && rx.expected_() == 13);
}

static void limits()
{
  ZuTestScope(limits);
  Rx rx{{.sequenceLimit = 9, .readAheadBytes = 40}};
  ZuCheck(rx.receive(0) == SeqResult::Invalid);
  ZuCheck(rx.receive(10) == SeqResult::Invalid);
  ZuCheck(rx.receive(2, 0) == SeqResult::Invalid);
  ZuCheck(rx.receive(3, 41) == SeqResult::Overload);
  ZuCheck(rx.receive(3) == SeqResult::Accepted);
  ZuCheck(rx.receive(4) == SeqResult::Accepted);
  ZuCheck(rx.receive(2) == SeqResult::Overload);
  ZuCheck(rx.retainedBytes_() == 40 && rx.expected_() == 1);
  ZuCheck(rx.receive(1) == SeqResult::Accepted);
  ZuCheck(rx.expected_() == 2);
  rx.drain();
  ZuCheck(rx.advance_(3));
  rx.drain();
  ZuCheck(rx.expected_() == 5 && !rx.retainedBytes_());
  ZuCheck(!rx.advance_(11));
  ZuCheck(rx.advance_(9));
  ZuCheck(rx.receive(9) == SeqResult::Accepted && rx.expected_() == 10);
  ZuCheck(rx.receive(UINT64_MAX) == SeqResult::Invalid);
}

static void countLimit()
{
  ZuTestScope(countLimit);
  Rx rx{{.readAheadBytes = UINT64_MAX, .readAheadCount = 2}};
  ZuCheck(rx.receive(3, 1) == SeqResult::Accepted);
  ZuCheck(rx.receive(4, 1) == SeqResult::Accepted);
  ZuCheck(rx.receive(4, 1) == SeqResult::Duplicate);
  ZuCheck(rx.receive(5, 1) == SeqResult::Overload);
  ZuCheck(rx.retained_() == 2 && rx.retainedBytes_() == 2);
  // Purging a skipped prefix releases count capacity, preserving the suffix.
  ZuCheck(rx.advance_(4));
  ZuCheck(rx.receive(5, 1) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.expected_() == 6 && !rx.retained_());
  // Zero disables queuing; the in-order fast path remains operational.
  Rx zero{{.readAheadCount = 0}};
  ZuCheck(zero.receive(2) == SeqResult::Overload);
  ZuCheck(zero.receive(1) == SeqResult::Accepted && zero.expected_() == 2);
  zero.hold_(); ZuCheck(zero.receive(2) == SeqResult::Overload);
  zero.resume_(); ZuCheck(zero.receive(2) == SeqResult::Accepted);
  // Filling the count bound cannot block the missing in-order recovery input.
  Rx gap{{.readAheadCount = 1}};
  ZuCheck(gap.receive(2) == SeqResult::Accepted);
  ZuCheck(gap.receive(1) == SeqResult::Accepted);
  gap.drain();
  ZuCheck(gap.expected_() == 3 && !gap.retained_());
}

static void staleDrain()
{
  ZuTestScope(staleDrain);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.receive(2) == SeqResult::Accepted);
  rx.resume_();
  rx.hold_();
  ZuCheck(rx.advance_(3) && !rx.retained_());
  rx.resume_();
  rx.drain();
  ZuCheck(rx.receive(3) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.expected_() == 4 && rx.processed.length() == 1);
}

static void callbackHold()
{
  ZuTestScope(callbackHold);
  Rx rx;
  for (uint64_t i = 2; i <= 5; ++i)
    ZuCheck(rx.receive(i) == SeqResult::Accepted);
  rx.stopAt = 1;
  ZuCheck(rx.receive(1) == SeqResult::Accepted);
  ZuCheck(rx.held_() && rx.expected_() == 2 && rx.retained_() == 4);
  rx.stopAt = 3;
  rx.resume_();
  rx.drain();
  ZuCheck(rx.held_() && rx.expected_() == 4 && rx.retainedBytes_() == 40);
  rx.stopAt = 0;
  rx.resume_();
  rx.drain();
  ZuCheck(rx.expected_() == 6 && !rx.retained_() && !rx.retainedBytes_());
  ZuCheck(rx.processed.length() == 5);
}

static void permutations()
{
  ZuTestScope(permutations);
  // Deterministic shuffled arrivals, duplicate ingress and intermittent holds
  // over one default replay-sized batch. The oracle tracks delivered prefixes.
  enum { Cases = 128, Messages = 64 };
  uint64_t seed = 0x61929348;
  bool ok = true;
  for (unsigned test = 0; test < Cases && ok; ++test) {
    Rx rx;
    Values order;
    ZtArray<uint8_t, ZtArrayHeapID<"Zfix.SeqTest.Seen">> seen;
    seen.length(Messages + 1);
    for (unsigned i = 0; i <= Messages; ++i) seen[i] = 0;
    for (unsigned i = 1; i <= Messages; ++i) order.push(i);
    for (unsigned i = Messages; i > 1; --i) {
      seed = seed*6364136223846793005ULL + 1;
      unsigned j = (seed>>32)%i;
      auto value = order[i - 1];
      order[i - 1] = order[j];
      order[j] = value;
    }
    uint64_t expected = 1;
    for (unsigned i = 0; i < Messages; ++i) {
      auto seqNo = order[i];
      if (!(i%7)) rx.hold_();
      if (rx.receive(seqNo) != SeqResult::Accepted) { ok = false; break; }
      seen[seqNo] = 1;
      unsigned dup = rx.receive(seqNo);
      if (dup != SeqResult::Duplicate && dup != SeqResult::Low) {
	ok = false; break;
      }
      if (!(i%5)) rx.resume_();
      rx.drain();
      if (!rx.held_()) {
	while (expected <= Messages && seen[expected]) ++expected;
	if (rx.expected_() != expected) { ok = false; break; }
      }
    }
    rx.resume_();
    rx.drain();
    ok = ok && rx.expected_() == Messages + 1 && !rx.retainedBytes_() &&
      rx.processed.length() == Messages;
    for (unsigned i = 0; i < Messages && ok; ++i)
      ok = rx.processed[i] == i + 1;
  }
  ZuCheck(ok);
}

static void reset()
{
  ZuTestScope(reset);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.receive(2) == SeqResult::Accepted);
  rx.resume_();
  auto old = rx.tasks[0];
  ZuCheck(!rx.reset_(7) && rx.expected_() == 1 && rx.retainedBytes_() == 20);
  rx.drain();
  ZuCheck(rx.receive(1) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.processed.length() == 2 && !rx.retained_());
  rx.processed.clear();
  ZuCheck(!rx.reset_(7));
  rx.hold_();
  ZuCheck(rx.reset_(7));
  ZuCheck(!rx.retainedBytes_() && !rx.retained_() && rx.expected_() == 7);
  rx.drain_(old);
  ZuCheck(rx.expected_() == 7 && !rx.processed.length());
  rx.resume_();
  ZuCheck(rx.receive(7) == SeqResult::Accepted);
  ZuCheck(rx.expected_() == 8 && rx.processed.length() == 1);
  ZuCheck(!rx.reset_(0));
  rx.hold_();
  ZuCheck(rx.reset_(1) && rx.held_());
  ZuCheck(rx.receive(1) == SeqResult::Accepted && rx.processed.length() == 1);
  rx.resume_();
  rx.drain();
  ZuCheck(rx.expected_() == 2 && rx.processed.length() == 2);
  Rx logical;
  logical.hold_();
  ZuCheck(logical.restore_(9, 1, {1, 8}));
  ZuCheck(!logical.reset_(1) && logical.received_() == 9 && logical.expected_() == 1 &&
    !logical.retained_());
  ZuCheck(logical.advance_(9) && logical.reset_(1) && logical.held_());

}

static void restoredGap()
{
  ZuTestScope(restoredGap);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.restore_(10, 5, {7, 2}));
  ZuCheck(rx.received_() == 10 && rx.expected_() == 5 && !rx.retained_() &&
    rx.requested_().key() == 7 && rx.requested_().length() == 2);
  ZuCheck(!rx.restore_(4, 5) && rx.received_() == 10);
  rx.resume_();
  ZuCheck(rx.requests.length() == 2 && rx.requests[0] == 5 && rx.requests[1] == 5 && rx.retry);
  ZuCheck(rx.previous.length() == 2 && rx.previous[0] == 7 && rx.previous[1] == 2);
  auto retry = rx.retry;
  auto arms = rx.arms;
  rx.resume_();
  ZuCheck(rx.requests.length() == 2 && rx.retry == retry && rx.arms == arms);
  ZuCheck(!rx.restore_(10, 5));
  for (uint64_t key = 5; key < 10; ++key) ZuCheck(rx.receive(key) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.expected_() == 10 && rx.received_() == 10 && rx.processed.length() == 5 &&
    !rx.requested_().length() && !rx.retained_());
  ZuCheck(!rx.retry);
}

static void restoreValidation()
{
  ZuTestScope(restoreValidation);
  Rx rx{{.sequenceLimit = 20}};
  rx.hold_();
  ZuCheck(rx.restore_(10, 5, {7, 2}));
  auto check = [&rx](uint64_t received, uint64_t processed, Rx::Span gap) {
    ZuCheck(!rx.restore_(received, processed, gap));
    ZuCheck(rx.held_() && rx.received_() == 10 && rx.expected_() == 5 &&
      rx.requested_() == Rx::Span(7, 2) && !rx.retained_() && !rx.retainedBytes_() &&
      !rx.requests.length() && !rx.arms && !rx.tasks.length());
  };
  check(10, 0, {});
  check(4, 5, {});
  check(22, 5, {});
  check(10, 5, {1, 0});
  check(10, 5, {4, 1});
  check(10, 5, {9, 2});
  check(10, 5, {UINT64_MAX, 2});
  rx.resume_();
  auto ticket = rx.retry;
  auto arms = rx.arms;
  ZuCheck(!rx.restore_(10, 5));
  ZuCheck(rx.retry == ticket && rx.arms == arms);
  rx.retry_(ticket);
  ZuCheck(rx.retried == 1 && rx.retries[0] == 5 && rx.retries[1] == 5);
  rx.hold_();
  auto cancelled = rx.cancelled;
  ZuCheck(rx.restore_(8, 8));
  rx.retry_(ticket);
  ZuCheck(rx.retried == 1 && rx.cancelled == cancelled && !rx.requested_().length());
  rx.resume_();
  ZuCheck(rx.received_() == 8 && rx.expected_() == 8 && !rx.retry);
}

static void trailingGap()
{
  ZuTestScope(trailingGap);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.restore_(10, 5));
  ZuCheck(!rx.requested_().length() && !rx.requests.length() && !rx.retry);
  rx.resume_();
  auto retry = rx.retry;
  ZuCheck(rx.receive(5) == SeqResult::Accepted);
  ZuCheck(rx.expected_() == 6 && rx.received_() == 10 && !rx.retained_() &&
    rx.gap_() == Rx::Span(6, 4) && rx.requested_() == Rx::Span(6, 4));
  ZuCheck(rx.requests.length() == 4 && rx.requests[2] == 6 && rx.requests[3] == 4 &&
    rx.previous[2] == 5 && rx.previous[3] == 5 && rx.retry != retry);
  rx.retry_(retry);
  ZuCheck(!rx.retried);
  rx.retry_(rx.retry);
  ZuCheck(rx.retried == 1 && rx.retries[0] == 6 && rx.retries[1] == 4);
  rx.hold_();
  ZuCheck(!rx.reset_(1));
  ZuCheck(rx.advance_(8) && rx.gap_() == Rx::Span(8, 2) &&
    rx.requested_() == Rx::Span(8, 2));
  rx.resume_();
  ZuCheck(rx.requests.length() >= 2 && rx.requests[rx.requests.length() - 2] == 8);
  retry = rx.retry;
  ZuCheck(rx.advance_(10));
  rx.drain();
  rx.retry_(retry);
  ZuCheck(!rx.gap_().length() && !rx.requested_().length() && !rx.retry && rx.retried == 1);
  rx.hold_();
  ZuCheck(rx.reset_(1) && rx.received_() == 1);
}

static void restoredOrder()
{
  ZuTestScope(restoredOrder);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.restore_(10, 5, {7, 2}));
  rx.resume_();
  ZuCheck(rx.receive(12) == SeqResult::Accepted);
  for (uint64_t key = 9; key > 5; --key) {
    ZuCheck(rx.receive(key) == SeqResult::Accepted);
    ZuCheck(rx.receive(key) == SeqResult::Duplicate);
    rx.hold_();
    rx.resume_();
    rx.drain();
    ZuCheck(rx.expected_() == 5 && rx.gap_().key() == 5);
  }
  auto retry = rx.retry;
  ZuCheck(rx.receive(5) == SeqResult::Accepted);
  rx.drain();
  ZuCheck(rx.expected_() == 10 && rx.received_() == 13 && rx.retained_() == 1 &&
    rx.gap_() == Rx::Span(10, 2) && rx.requested_() == Rx::Span(10, 2));
  ZuCheck(rx.processed.length() == 5);
  for (unsigned i = 0; i < 5; ++i) ZuCheck(rx.processed[i] == i + 5);
  rx.retry_(retry);
  ZuCheck(!rx.retried);
  ZuCheck(rx.advance_(12));
  rx.drain();
  ZuCheck(rx.expected_() == 13 && rx.processed.length() == 6 && rx.processed[5] == 12 &&
    !rx.retained_() && !rx.retainedBytes_() && !rx.retry);
}

static void requestReentry()
{
  ZuTestScope(requestReentry);
  for (unsigned action = RxHook::Hold; action <= RxHook::Resume; ++action) {
    Rx rx;
    rx.hold_();
    ZuCheck(rx.restore_(10, 5));
    rx.requestHook = action;
    rx.resume_();
    ZuCheck(rx.hookOK);
    switch (action) {
      case RxHook::Hold:
	ZuCheck(rx.held_() && !rx.retry && !rx.arms);
	break;
      case RxHook::Advance:
	ZuCheck(rx.expected_() == 10 && !rx.retry && !rx.arms);
	rx.drain();
	ZuCheck(!rx.requested_().length());
	break;
      case RxHook::Reset:
	ZuCheck(rx.held_() && rx.expected_() == 10 && !rx.retry && !rx.arms);
	break;
      case RxHook::Restore:
	ZuCheck(rx.held_() && rx.expected_() == 5 && !rx.retry && !rx.arms);
	break;
      case RxHook::Resume:
	ZuCheck(!rx.held_() && rx.requests.length() == 4 && rx.arms == 1);
	break;
    }
  }
}

static void retryReentry()
{
  ZuTestScope(retryReentry);
  for (unsigned action = RxHook::Hold; action <= RxHook::Resume; ++action) {
    Rx rx;
    rx.hold_();
    ZuCheck(rx.restore_(10, 5));
    rx.resume_();
    auto ticket = rx.retry;
    rx.retryHook = action;
    rx.retry_(ticket);
    ZuCheck(rx.hookOK);
    ZuCheck(rx.retried == 1);
    rx.retry_(ticket);
    ZuCheck(rx.retried == 1);
    switch (action) {
      case RxHook::Hold:
      case RxHook::Reset:
      case RxHook::Restore:
	ZuCheck(rx.held_() && !rx.retry && rx.arms == 1);
	break;
      case RxHook::Advance:
	ZuCheck(rx.expected_() == 10 && !rx.retry && rx.arms == 1);
	rx.drain();
	ZuCheck(!rx.requested_().length());
	break;
      case RxHook::Resume:
	ZuCheck(!rx.held_() && rx.requests.length() == 4 && rx.arms == 2);
	break;
    }
  }
}

static void restoreFootprint()
{
  ZuTestScope(restoreFootprint);
  using Adapter = SeqRxQueue<Queue>;
  ZuAssert((ZuIsSame<Adapter::Node, Queue::Node>{}));
  ZuAssert((ZuIsSame<Adapter::Fn, Queue::Fn>{}));
  ZuAssert((ZuIsSame<Adapter::HeapID, Queue::HeapID>{}));
  using Cache = ZmHeapCacheT<Queue::HeapID, ZmHeapAllocSize<sizeof(Queue::Node)>{},
    alignof(Queue::Node), Queue::Sharded>;
  auto cache = Cache::instance()->cache();
  auto allocs = uint64_t(cache->stats().allocs);
  Rx rx;
  rx.hold_();
  ZuCheck(rx.restore_(10, 5, {7, 2}));
  ZuCheck(uint64_t(cache->stats().allocs) == allocs && !rx.retained_());
  ZuCheck(sizeof(Adapter) <= sizeof(Queue) + sizeof(Queue::Key));
  log("queue=", sizeof(Queue), " adapter=", sizeof(Adapter),
    " node=", sizeof(Queue::Node), " seqRx=", sizeof(SeqRx<Rx, Queue>));
}

class CountQueue : public Queue {
public:
  static inline unsigned	queries = 0;

  Span gap() const { ++queries; return Queue::gap(); }
};

struct CountRx : public SeqRx<CountRx, CountQueue> {
  Values	tasks;
  unsigned	task = 0;
  unsigned	processed = 0;
  unsigned	requests = 0;
  uint64_t	retry = 0;

  void processRx(Msg *) { ++processed; }

  void requestRx(const Span &, const Span &) { ++requests; }

  void reRequestRx(const Span &) { }

  void postRx(uint64_t ticket) { tasks.push(ticket); }

  void armGap(uint64_t ticket) { retry = ticket; }

  void cancelGap() { retry = 0; }

  unsigned receive(uint64_t seqNo) { return receive_(new Msg{Input{seqNo, 20}}); }

  void drain() { while (task < tasks.length()) drain_(tasks[task++]); }
};

static void gapWork()
{
  ZuTestScope(gapWork);
  // A default replay-sized batch exercises existing one-message continuations.
  enum { Messages = 64 };
  CountRx ordered;
  CountQueue::queries = 0;
  for (unsigned i = 1; i <= Messages; ++i)
    ZuCheck(ordered.receive(i) == SeqResult::Accepted);
  ZuCheck(ordered.processed == Messages && !ordered.tasks.length() &&
    CountQueue::queries == Messages);

  CountRx queued;
  for (unsigned i = 2; i <= Messages; ++i)
    ZuCheck(queued.receive(i) == SeqResult::Accepted);
  ZuCheck(queued.receive(1) == SeqResult::Accepted);
  auto queries = CountQueue::queries;
  queued.drain();
  ZuCheck(queued.processed == Messages && queued.tasks.length() == Messages - 1 &&
    CountQueue::queries - queries == 1 && !queued.retry);

  CountRx restored;
  restored.hold_();
  queries = CountQueue::queries;
  ZuCheck(restored.restore_(Messages + 1, 1));
  ZuCheck(CountQueue::queries == queries && !restored.tasks.length());
  restored.resume_();
  auto requests = restored.requests;
  auto retry = restored.retry;
  restored.resume_();
  ZuCheck(restored.requests == requests && restored.retry == retry);
  for (unsigned i = 1; i <= Messages; ++i)
    ZuCheck(restored.receive(i) == SeqResult::Accepted);
  ZuCheck(restored.processed == Messages && !restored.tasks.length() && !restored.retry);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ordered);
  ZuTestCall(held);
  ZuTestCall(gapFill);
  ZuTestCall(limits);
  ZuTestCall(countLimit);
  ZuTestCall(reset);
  ZuTestCall(staleDrain);
  ZuTestCall(callbackHold);
  ZuTestCall(permutations);
  ZuTestCall(restoredGap);
  ZuTestCall(restoreValidation);
  ZuTestCall(trailingGap);
  ZuTestCall(restoredOrder);
  ZuTestCall(requestReentry);
  ZuTestCall(retryReentry);
  ZuTestCall(restoreFootprint);
  ZuTestCall(gapWork);
  return 0;
}
