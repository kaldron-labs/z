//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuLib.hh>

#include <zlib/ZmThread.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZxdpUMem.hh>
#include <zlib/Zxdp.hh>
#include <zlib/ZxdpPool.hh>

using namespace ZuTestUtil;

struct PlainPool : ZmNHeap<PlainPool>, Zi::IOBufPool {
friend inline ZuStringT<"Zxdp.Test.PlainPool"> ZmNHeapID(PlainPool *);
  uint8_t *alloc() override { return nullptr; }
  void free(uint8_t *) override { }
};

static auto createArena(
  const Zi::IOBufPoolList &pools, Zi::IOBufPool *rx, unsigned mtu)
{
  ZtArray<ZmRef<Zi::IOBufPool>> refs;
  refs.size(pools.count_());
  auto iter = pools.citer();
  while (auto pool = iter()) new (refs.push()) ZmRef<Zi::IOBufPool>{pool};
  return Zxdp::Xdp::UMem::create(refs.cspan(), rx, mtu);
}

static void testPools()
{
  ZuTestScope(testPools);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = backend->bufPool(3, 1514);
  auto tx = backend->bufPool(5, 1514);
  auto control = backend->bufPool(1, 128);
  auto empty = backend->bufPool(0, 64);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  pools.pushNode(control.ptr());
  pools.pushNode(empty.ptr());
  auto created = createArena(pools, rx.ptr(), 1500);
  ZuCHECK(created.is<ZmRef<Zxdp::Xdp::UMem>>(),
    "intrusive caller-owned pool list provisions the arena");
  if (created.is<ZeException>()) return;
  auto arena = ZuMv(created.p<ZmRef<Zxdp::Xdp::UMem>>());
  auto heapPool = backend->bufPool(0, 0);
  ZuCHECK(!(uintptr_t(heapPool.ptr()) % Zm::CacheLineSize),
    "framework heap preserves pool cacheline alignment");
  heapPool = nullptr;
  bool isolated = true;
  uintptr_t previous = 0;
  for (unsigned i = 0; i < tx->count; ++i) {
    auto address = uintptr_t(&arena->txSlot(*static_cast<Zxdp::Xdp::BufPool *>(tx.ptr()), i));
    isolated &= !(address % Zm::CacheLineSize) &&
      (!i || address - previous >= Zm::CacheLineSize);
    previous = address;
  }
  auto address = uintptr_t(&arena->txSlot(*static_cast<Zxdp::Xdp::BufPool *>(control.ptr()), 0));
  isolated &= !(address % Zm::CacheLineSize) && address - previous >= Zm::CacheLineSize;
  ZuCHECK(isolated,
    "actual arena slot array isolates adjacent metadata, including the pool boundary");
  const auto &layout = arena->layout();
  ZuCHECK(layout.rxCount == 3 && layout.txCount == 6 && layout.chunk == 2048 &&
      layout.rxLength == 8192 && layout.txLength == 16384,
    "Rx region is dedicated; Tx regions use each pool's count and size");
  uint8_t *held[5]; // exact fixture pool capacity
  bool geometry = true;
  for (unsigned i = 0; i < 5; ++i) {
    auto p = held[i] = tx->alloc();
    geometry &= p && !(uintptr_t(p) & (tx->align - 1)) &&
      (!i || p - held[i - 1] == 2048) && arena->pool(p) == tx.ptr();
    if (p) memset(p, i + 1, tx->size);
  }
  ZuCHECK(geometry && !tx->alloc() && !empty->alloc(),
    "exact reserve exhausts without fallback; address identity is direct");
  auto ctl = control->alloc(), input = rx->alloc();
  ZuCHECK(ctl && input && !control->alloc() && arena->pool(ctl) == control.ptr(),
    "small Tx reserve and Rx reserve remain independent under saturation");
  ZmThread releaser{[&tx, &held] { for (auto p : held) tx->free(p + 54); }};
  releaser.join();
  auto late = tx->alloc();
  arena->retire(pools, static_cast<Zxdp::Xdp::BufPool *>(rx.ptr()));
  arena = nullptr;
  ZuCHECK(late && !tx->alloc() && late[54],
    "retired pools retain mappings through application-held packets");
  tx->free(late);
  control->free(ctl);
  rx->free(input);
  ZuCHECK(!tx->alloc() && !control->alloc() && !rx->alloc(),
    "retired pools refuse allocations after all final returns");
}

static void testInvalid()
{
  ZuTestScope(testInvalid);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = backend->bufPool(2, 1514);
  auto tx = backend->bufPool(1, 128);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  ZuCHECK(createArena({}, rx.ptr(), 1500).is<ZeException>(),
    "an Rx pool without a Tx pool is rejected");
  rx->count = 0;
  ZuCHECK(createArena(pools, rx.ptr(), 1500).is<ZeException>(),
    "empty Rx reserve rejected");
  rx->count = 2;
  rx->size = 1500;
  ZuCHECK(createArena(pools, rx.ptr(), 1500).is<ZeException>(),
    "Rx fits complete Ethernet frame");
  rx->size = 1514;
  auto extra = backend->bufPool(1, 128);
  pools.pushNode(extra.ptr());
  tx->count = unsigned(-1);
  ZuCHECK(createArena(pools, rx.ptr(), 1500).is<ZeException>(),
    "kernel arena count overflow rejected before mapping");
  pools.clean();
  ZmRef<PlainPool> plain = new PlainPool;
  plain->count = 2;
  plain->size = 1514;
  pools.pushNode(plain.ptr());
  ZuCHECK(createArena({}, plain.ptr(), 1500).is<ZeException>() &&
      createArena(pools, rx.ptr(), 1500).is<ZeException>(),
    "generic Zi pools cannot supply raw registered arena slots");
}

static void testZiAllocator()
{
  ZuTestScope(testZiAllocator);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  auto rx = backend->bufPool(2, 1514);
  auto tx = backend->bufPool(2, 128);
  tx->align = 256;
  tx->headRoom = 17;
  tx->tailRoom = 11;
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto created = createArena(pools, rx.ptr(), 1500);
  ZuCHECK(created.is<ZmRef<Zxdp::Xdp::UMem>>(), "Zi pool geometry provisions registered backing");
  if (created.is<ZeException>()) return;
  auto arena = ZuMv(created.p<ZmRef<Zxdp::Xdp::UMem>>());
  Zi::IOBufPoolAllocator<Zi::IOBufType::TxTCP> allocator{nullptr, tx};
  auto buf = allocator.allocBuf(nullptr, 32);
  auto second = allocator.allocBuf(nullptr, 32);
  ZuCHECK(buf && second && buf->registered() && buf->allocator() == &allocator &&
      buf->skip == 17 && buf->length == 32 &&
      buf->size == 128 && second->data_() - buf->data_() == 256 &&
      !(uintptr_t(second->data_()) & 255) &&
      !allocator.allocBuf(nullptr, 1),
    "Zi metadata preserves native slot alignment, exact headroom and bounded admission");
  if (buf) memset(buf->data(), 0x5a, buf->length);
  arena->retire(pools, static_cast<Zxdp::Xdp::BufPool *>(rx.ptr()));
  arena = nullptr;
  ZuCHECK(buf && buf->data()[31] == 0x5a && !tx->alloc(),
    "registered metadata retains its original backing after arena retirement");
  second = nullptr;
  buf = nullptr;
  auto reprovisioned = createArena(pools, rx.ptr(), 1500);
  ZuCHECK(reprovisioned.is<ZmRef<Zxdp::Xdp::UMem>>(),
    "Zi final return permits reprovisioning the original pools");
  if (reprovisioned.is<ZmRef<Zxdp::Xdp::UMem>>())
    reprovisioned.p<ZmRef<Zxdp::Xdp::UMem>>()->retire(
      pools, static_cast<Zxdp::Xdp::BufPool *>(rx.ptr()));
}

static void testPoolGeometry()
{
  ZuTestScope(testPoolGeometry);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  enum { RxSize = 1514, TxSize = 128 }; // full Ethernet receive and small Tx fixtures
  auto rx = backend->bufPool(2, RxSize, 0, 0, 512);
  auto tx = backend->bufPool(1, TxSize);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto created = createArena(pools, rx.ptr(), 1500);
  ZuCHECK(created.is<ZmRef<Zxdp::Xdp::UMem>>(), "requested Rx alignment fits registered chunks");
  if (created.is<ZeException>()) return;
  auto arena = ZuMv(created.p<ZmRef<Zxdp::Xdp::UMem>>());
  auto first = rx->alloc(), second = rx->alloc();
  ZuCHECK(first && second && !(uintptr_t(first) & 511) && !(uintptr_t(second) & 511),
    "every Rx slot honors the requested alignment beyond the kernel's default headroom");
  arena->retire(pools, static_cast<Zxdp::Xdp::BufPool *>(rx.ptr()));
  if (first) rx->free(first);
  if (second) rx->free(second);
  arena = nullptr;

  struct Geometry {
    unsigned align, rxHead, rxTail, txHead, txTail;
  };
  const Geometry invalid[] = {
    {0, 0, 0, 0, 0}, {3, 0, 0, 0, 0},
    {unsigned(sysconf(_SC_PAGESIZE)), 0, 0, 0, 0},
    {64, RxSize + 1, 0, 0, 0}, {64, RxSize, 1, 0, 0},
    {64, 0, 0, TxSize + 1, 0}, {64, 0, 0, 100, TxSize - 99}
  };
  for (const auto &geometry : invalid) {
    rx->align = geometry.align;
    rx->headRoom = geometry.rxHead;
    rx->tailRoom = geometry.rxTail;
    tx->headRoom = geometry.txHead;
    tx->tailRoom = geometry.txTail;
    auto result = createArena(pools, rx.ptr(), 1500);
    ZuCHECK(result.is<ZeException>() && !rx->alloc() && !tx->alloc(),
      "bad alignment or header/trailer reserves fail before either pool activates");
    if (result.is<ZmRef<Zxdp::Xdp::UMem>>()) {
      result.p<ZmRef<Zxdp::Xdp::UMem>>()->retire(
        pools, static_cast<Zxdp::Xdp::BufPool *>(rx.ptr()));
      return;
    }
  }
}

static void testContention()
{
  ZuTestScope(testContention);
  auto backend = Zxdp::instance(Zxdp::Type::Xdp);
  enum { Slots = 64, Threads = 6, Turns = 30000 }; // bounded contention fixture
  auto rx = backend->bufPool(2, 1514);
  auto tx = backend->bufPool(Slots, 128);
  Zi::IOBufPoolList pools;
  pools.pushNode(tx.ptr());
  auto created = createArena(pools, rx.ptr(), 1500);
  ZuCHECK(created.is<ZmRef<Zxdp::Xdp::UMem>>(), "contention fixture provisioned");
  if (created.is<ZeException>()) return;
  auto arena = ZuMv(created.p<ZmRef<Zxdp::Xdp::UMem>>());
  auto origin = tx->alloc();
  uintptr_t begin = uintptr_t(origin);
  tx->free(origin);
  ZmAtomic<unsigned> owners[Slots]{};
  ZmAtomic<unsigned> failures{0};
  ZmThread workers[Threads];
  for (unsigned t = 0; t < Threads; ++t) workers[t] = ZmThread{[
    &tx, &owners, &failures, begin, t
  ] {
    for (unsigned i = 0; i < Turns; ++i) {
      auto p = tx->alloc();
      if (!p) { ++failures; continue; }
      auto id = (uintptr_t(p) - begin)/128;
      if (id >= Slots) { ++failures; tx->free(p); continue; }
      if (owners[id].cmpXch(t + 1, 0)) { ++failures; continue; }
      memset(p, t + 1, tx->size);
      for (unsigned j = 0; j < tx->size; ++j)
        if (p[j] != t + 1) { ++failures; break; }
      owners[id] = 0;
      tx->free(p);
    }
  }};
  for (auto &worker : workers) worker.join();
  ZuCHECK(!unsigned(failures), "contended slist preserves ownership and payload");
  uint8_t *held[Slots];
  bool full = true;
  for (auto &p : held) full &= !!(p = tx->alloc());
  ZuCHECK(full && !tx->alloc(), "no frame lost during contended reuse");
  for (auto p : held) if (p) tx->free(p);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPools);
  ZuTestCall(testInvalid);
  ZuTestCall(testZiAllocator);
  ZuTestCall(testPoolGeometry);
  ZuTestCall(testContention);
}
