//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZutcpAlloc.hh>

using namespace ZuTestUtil;
using namespace Zutcp;

static void testDistinctFrames()
{
  ZuTestScope(testDistinctFrames);
  ZmRef<RxFrames> frames = new RxFrames{4, 2};
  ZmRef<RxAccount> first = new RxAccount{1, 1};
  ZmRef<RxAccount> second = new RxAccount{2, 1};
  ZuCHECK(frames->retain(0, first) && frames->retain(0, first) &&
      first->frames == 1 && frames->stats().retained == 1,
    "aliases of one physical slot charge once");
  ZuCHECK(!frames->retain(1, first) && frames->retain(1, second) &&
      !frames->retain(2, second) && !frames->retain(0, second),
    "per-account, shared and slot-identity admission must stay independent");
  auto stats = frames->stats();
  ZuCHECK(stats.capacity == 4 && stats.limit == 2 && stats.retained == 2 &&
      stats.admitted == 2 && stats.refused == 3 && !stats.retired,
    "frame admission snapshot must report distinct charges and refusals");
  frames->returned(0);
  ZuCHECK(first->frames == 1 && frames->reconcile() == 1 && !first->frames &&
      frames->retain(2, second) && second->frames == 2,
    "only owner reconciliation restores account and shared capacity");
  frames->returned(1);
  frames->returned(2);
  ZuCHECK(frames->reconcile() == 2 && !frames->stats().retained &&
      !second->frames && frames->stats().retired == 3,
    "final returns retire each distinct charge exactly once");
}

static void testRetiredAccount()
{
  ZuTestScope(testRetiredAccount);
  ZmRef<RxFrames> frames = new RxFrames{2, 2};
  ZmRef<RxAccount> owner = new RxAccount{2, 7};
  auto old = owner.ptr();
  ZuCHECK(frames->retain(0, owner), "old account must retain its slot");
  owner = nullptr; // the connection/generation has gone; no owner callback
  ZuCHECK(old->frames == 1 && old->refCount() == 1,
    "only the slot must retain a retired account");
  owner = new RxAccount{2, 8};
  ZuCHECK(frames->retain(1, owner) && !frames->retain(0, owner),
    "a new account must not take over backing held by a retired generation");
  frames->returned(0);
  ZuCHECK(frames->reconcile() == 1 && owner->generation == 8 && owner->frames == 1,
    "old return must not mutate the new generation");
  frames->returned(1);
  ZuCHECK(frames->reconcile() == 1 && !owner->frames,
    "new generation must retire independently");
}

static void testConcurrentReturns()
{
  ZuTestScope(testConcurrentReturns);
  enum { Count = 128, Workers = 4 };
  ZmRef<RxFrames> frames = new RxFrames{Count, Count};
  ZmRef<RxAccount> account = new RxAccount{Count, 1};
  bool admitted = true;
  for (unsigned i = 0; i < Count; i += 2)
    admitted &= frames->retain(i, account);
  ZuCHECK(admitted && account->frames == Count/2,
    "concurrent return fixture must charge half its slots");
  ZmAtomic<unsigned> done{0};
  ZmThread workers[Workers];
  for (unsigned i = 0; i < Workers; ++i)
    workers[i].run([frames = frames.ptr(), &done, i] {
      for (unsigned slot = i; slot < Count; slot += Workers)
	frames->returned(slot);
      ++done;
    });
  while (unsigned(done) != Workers) frames->reconcile();
  for (unsigned i = 0; i < Workers; ++i) workers[i].join();
  frames->reconcile();
  auto stats = frames->stats();
  ZuCHECK(!account->frames && !stats.retained && stats.retired == Count/2 &&
      !frames->reconcile(),
    "concurrent charged and uncharged returns must drain without lost or duplicate charges");
}

static void testSlotReuse()
{
  ZuTestScope(testSlotReuse);
  enum { Repeats = 4096 };
  ZmRef<RxFrames> frames = new RxFrames{1, 1};
  ZmRef<RxAccount> account = new RxAccount{1, 1};
  bool reused = true;
  for (unsigned i = 0; i < Repeats; ++i) {
    reused &= frames->retain(0, account);
    frames->returned(0);
    // The next retain must drain the old incarnation before charging again.
  }
  frames->reconcile();
  auto stats = frames->stats();
  ZuCHECK(reused && !account->frames && !stats.retained &&
      stats.admitted == Repeats && stats.retired == Repeats && !stats.refused,
    "same-layout slot reuse must reconcile before every new charge");
}

static void testLimitReduction()
{
  ZuTestScope(testLimitReduction);
  ZmRef<RxFrames> frames = new RxFrames{3, 3};
  ZmRef<RxAccount> account = new RxAccount{3, 1};
  bool admitted = frames->retain(0, account) && frames->retain(1, account);
  frames->limit(1);
  ZuCHECK(admitted && frames->stats().retained == 2 && account->frames == 2 &&
      frames->retain(0, account) && !frames->retain(2, account),
    "lowering a cap must preserve old charges and aliases while refusing new frames");
  frames->returned(0);
  frames->returned(1);
  ZuCHECK(frames->reconcile() == 2 && frames->retain(2, account),
    "new admission must resume after old backing falls below the new cap");
  frames->returned(2);
  frames->reconcile();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDistinctFrames);
  ZuTestCall(testRetiredAccount);
  ZuTestCall(testConcurrentReturns);
  ZuTestCall(testSlotReuse);
  ZuTestCall(testLimitReduction);
}
