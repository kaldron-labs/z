//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZxdpUMem.hh>

using namespace ZuTestUtil;

static void testPools()
{
  ZuTestScope(testPools);
  Zxdp::BufPool rx{3, 1514}, tx{5, 1514}, control{1, 128}, empty{0, 64};
  tx.next = &control;
  control.next = &empty;
  ZtString<> error;
  auto arena = Zxdp::UMem::create({.txPools = &tx, .rxPool = &rx}, 1500, error);
  ZuCHECK(!!arena, "intrusive caller-owned pool list provisions the arena");
  if (!arena) return;
  const auto &layout = arena->layout();
  ZuCHECK(layout.rxCount == 3 && layout.txCount == 6 && layout.chunk == 2048 &&
      layout.rxLength == 8192 && layout.txLength == 16384,
    "Rx region is dedicated; Tx regions use each pool's count and size");
  uint8_t *held[5]; // exact fixture pool capacity
  bool geometry = true;
  for (unsigned i = 0; i < 5; ++i) {
    auto p = held[i] = tx.alloc();
    geometry &= p && tx.slot(p) == i && tx.slot(p + 54) == i &&
      !rx.owns(p) && arena->pool(p) == &tx;
    if (p) memset(p, i + 1, tx.size);
  }
  ZuCHECK(geometry && tx.used() == 5 && !tx.alloc() && !empty.alloc(),
    "exact reserve exhausts without fallback; address identity is direct");
  auto ctl = control.alloc(), input = rx.alloc();
  ZuCHECK(ctl && input && !control.alloc() && arena->pool(ctl) == &control,
    "small Tx reserve and Rx reserve remain independent under saturation");
  ZmThread releaser{[&tx, &held] { for (auto p : held) tx.free(p + 54); }};
  releaser.join();
  ZuCHECK(!tx.used(), "off-owner return normalizes advanced raw packet pointers");
  auto late = tx.alloc();
  arena->retire(&tx, &rx);
  arena = nullptr;
  ZuCHECK(late && !tx.alloc() && late[54] && tx.owns(late),
    "retired pools retain mappings through application-held packets");
  tx.free(late);
  control.free(ctl);
  rx.free(input);
  ZuCHECK(!tx.used() && !control.used() && !rx.used(), "all slots return exactly once");
}

static void testInvalid()
{
  ZuTestScope(testInvalid);
  Zxdp::BufPool rx{2, 1514}, tx{1, 128};
  ZtString<> error;
  tx.next = &tx;
  ZuCHECK(!Zxdp::UMem::create({.txPools = &tx, .rxPool = &rx}, 1500, error) &&
      !rx.ready() && !tx.ready(), "cyclic configuration rejected before activation");
  tx.next = nullptr;
  rx.count = 0;
  ZuCHECK(!Zxdp::UMem::create({.rxPool = &rx}, 1500, error), "empty Rx reserve rejected");
  rx.count = 2;
  rx.size = 1500;
  ZuCHECK(!Zxdp::UMem::create({.rxPool = &rx}, 1500, error), "Rx fits complete Ethernet frame");
  rx.size = 1514;
  Zxdp::BufPool extra{1, 128};
  tx.next = &extra;
  tx.count = unsigned(-1);
  ZuCHECK(!Zxdp::UMem::create({.txPools = &tx, .rxPool = &rx}, 1500, error),
    "kernel arena count overflow rejected before mapping");
}

static void testContention()
{
  ZuTestScope(testContention);
  enum { Slots = 64, Threads = 6, Turns = 30000 }; // bounded contention fixture
  Zxdp::BufPool rx{2, 1514}, tx{Slots, 128};
  ZtString<> error;
  auto arena = Zxdp::UMem::create({.txPools = &tx, .rxPool = &rx}, 1500, error);
  ZuCHECK(!!arena, "contention fixture provisioned");
  if (!arena) return;
  ZmAtomic<unsigned> owners[Slots]{};
  ZmAtomic<unsigned> failures{0};
  ZmThread workers[Threads];
  for (unsigned t = 0; t < Threads; ++t) workers[t] = ZmThread{[&tx, &owners, &failures, t] {
    for (unsigned i = 0; i < Turns; ++i) {
      auto p = tx.alloc();
      if (!p) { ++failures; continue; }
      auto id = tx.slot(p);
      if (owners[id].cmpXch(t + 1, 0)) { ++failures; continue; }
      memset(p, t + 1, tx.size);
      for (unsigned j = 0; j < tx.size; ++j)
        if (p[j] != t + 1) { ++failures; break; }
      owners[id] = 0;
      tx.free(p);
    }
  }};
  for (auto &worker : workers) worker.join();
  ZuCHECK(!unsigned(failures) && !tx.used(), "contended slist preserves ownership and payload");
  uint8_t *held[Slots];
  bool full = true;
  for (auto &p : held) full &= !!(p = tx.alloc());
  ZuCHECK(full && !tx.alloc(), "no frame lost during contended reuse");
  for (auto p : held) if (p) tx.free(p);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPools);
  ZuTestCall(testInvalid);
  ZuTestCall(testContention);
}
