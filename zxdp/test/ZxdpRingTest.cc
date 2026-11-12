//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <sys/mman.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZxdpRing.hh>

using namespace ZuTestUtil;

// Deliberately use nonstandard, discontiguous offsets: the views must obey the
// offsets queried from the kernel rather than assuming a control-block layout.
struct Mapping {
  xdp_ring_offset off{128, 64, 256, 192};
  alignas(64) uint8_t bytes[4096]{};

  void seed(uint32_t cursor) {
    memcpy(bytes + off.producer, &cursor, sizeof(cursor));
    memcpy(bytes + off.consumer, &cursor, sizeof(cursor));
  }
};

static void testLayout()
{
  ZuTestScope(testLayout);
  Mapping map;
  size_t length = 0;
  ZuCHECK(Zxdp::TxRing::mapLength(map.off, 8, length) && length == 384,
    "mapping length includes descriptors at queried offset");
  ZuCHECK(!Zxdp::TxRing::mapLength(map.off, 0, length) &&
      !Zxdp::TxRing::mapLength(map.off, 3, length),
    "zero and non-power-of-two capacities rejected");
  auto bad = map.off;
  bad.desc = UINT64_MAX - 7;
  ZuCHECK(!Zxdp::TxRing::mapLength(bad, 8, length), "mapping overflow rejected");
  bad = map.off;
  bad.consumer = bad.producer;
  ZuCHECK(!Zxdp::TxRing::mapLength(bad, 8, length), "overlapping fields rejected");
  bad = map.off;
  ++bad.desc;
  ZuCHECK(!Zxdp::TxRing::mapLength(bad, 8, length), "unaligned descriptor rejected");
  Zxdp::TxRing tx;
  ZuCHECK(!tx.init(map.bytes, 383, map.off, 8) &&
      !tx.init(nullptr, sizeof(map.bytes), map.off, 8) &&
      !tx.init(map.bytes + 1, sizeof(map.bytes) - 1, map.off, 8),
    "mapping bounds and base alignment checked");
  uint32_t cursor = 9;
  memcpy(map.bytes + map.off.producer, &cursor, sizeof(cursor));
  ZuCHECK(!tx.init(map.bytes, sizeof(map.bytes), map.off, 8),
    "inconsistent initial cursor distance rejected");
  map.seed(0);
  ZuCHECK(tx.init(map.bytes, sizeof(map.bytes), map.off, 8), "valid mapped view");
  ZuCHECK(!tx.init(map.bytes, sizeof(map.bytes), map.off, 8),
    "cannot replace an attached view");
  uint32_t flag = XDP_RING_NEED_WAKEUP;
  memcpy(map.bytes + map.off.flags, &flag, sizeof(flag));
  ZuCHECK(tx.needWakeup(), "driver wake flag read at queried offset");
  tx.reset();
  ZuCHECK(!tx.count() && !tx.available() && !tx.reserve(1) && !tx.needWakeup(),
    "detached view exposes no entries");
}

template <typename Entry> static void exercise()
{
  Mapping map;
  // Both the logical cursor and descriptor-array index wrap in this exercise.
  map.seed(UINT32_MAX - 2);
  Zxdp::Ring<Entry, true> prod;
  Zxdp::Ring<Entry, false> cons;
  ZuCheckRT(prod.init(map.bytes, sizeof(map.bytes), map.off, 8), log_("producer attached"));
  ZuCheckRT(cons.init(map.bytes, sizeof(map.bytes), map.off, 8), log_("consumer attached"));
  ZuCheckRT(prod.available() == 8 && !cons.available(), log_("initial empty ring"));
  ZuCheckRT(!prod.reserve(9) && !prod.reserve(0), log_("bounded vector reservation"));
  ZuCheckRT(prod.reserve(6) == 6, log_("reserve across array/cursor wrap"));
  for (unsigned i = 0; i < 6; ++i) {
    if constexpr (sizeof(Entry) == sizeof(xdp_desc)) prod.entry(i) = {100 + i, 64, 0};
    else prod.entry(i) = 100 + i;
  }
  ZuCheckRT(!cons.available(), log_("unpublished descriptors remain invisible"));
  prod.commit();
  ZuCheckRT(!prod.reserve(3) && prod.available() == 2,
    log_("insufficient vector capacity publishes nothing"));
  ZuCheckRT(cons.reserve(8) == 6, log_("consumer receives available bounded batch"));
  bool values = true;
  for (unsigned i = 0; i < 6; ++i) {
    if constexpr (sizeof(Entry) == sizeof(xdp_desc))
      values &= cons.entry(i).addr == 100 + i && cons.entry(i).len == 64;
    else values &= cons.entry(i) == 100 + i;
  }
  ZuCheckRT(values, log_("wrapped descriptor contents preserved"));
  ZuCheckRT(prod.available() == 2, log_("peek does not release descriptor slots"));
  cons.cancel();
  ZuCheckRT(cons.reserve(3) == 3, log_("cancel preserves unread entries"));
  cons.commit();
  ZuCheckRT(prod.available() == 5, log_("consumer release returns exactly its batch"));
  ZuCheckRT(prod.reserve(5) == 5, log_("reserve remaining capacity"));
  prod.cancel();
  ZuCheckRT(prod.available() == 5 && cons.available() == 3,
    log_("producer cancellation leaves published state unchanged"));
  ZuCheckRT(prod.reserve(5) == 5, log_("cancelled vector can be reserved again"));
  for (unsigned i = 0; i < 5; ++i) prod.entry(i) = {};
  prod.commit();
  ZuCheckRT(!prod.available() && !prod.reserve(1), log_("full ring backpressures"));
  ZuCheckRT(cons.reserve(16) == 8, log_("consumer budget capped to ring capacity"));
  cons.commit();
  ZuCheckRT(prod.available() == 8 && !cons.available(), log_("ring completely recycled"));
}

static void testWrap()
{
  ZuTestScopeRT(testWrap);
  exercise<xdp_desc>();
  exercise<uint64_t>();
}

static void testPublication()
{
  ZuTestScope(testPublication);
  xdp_ring_offset off{0, 64, 192, 128};
  size_t length;
  ZuCHECK(Zxdp::TxRing::mapLength(off, 64, length), "mapped fixture length");
  auto mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  ZuCHECK(mem != MAP_FAILED, "shared ring fixture mapped");
  if (mem == MAP_FAILED) return;
  Zxdp::TxRing prod;
  Zxdp::RxRing cons;
  ZuCHECK(prod.init(mem, length, off, 64) && cons.init(mem, length, off, 64),
    "both peers attached to mapped fixture");
  bool valid = true;
  enum { N = 200000 };
  ZmThread writer{[&prod] {
    for (unsigned i = 0; i < N;) {
      unsigned n = ((N - i) < 7 ? (N - i) : 7);
      if (!prod.reserve(n)) { Zm::yield(); continue; }
      for (unsigned j = 0; j < n; ++j)
	prod.entry(j) = {uint64_t(i + j), (i + j) ^ 0xdeadbeefU, 0};
      prod.commit();
      i += n;
    }
  }};
  ZmThread reader{[&cons, &valid] {
    for (unsigned i = 0; i < N;) {
      unsigned n = cons.reserve(11);
      if (!n) { Zm::yield(); continue; }
      for (unsigned j = 0; j < n; ++j) {
	const auto &entry = cons.entry(j);
	valid &= entry.addr == i + j && entry.len == ((i + j) ^ 0xdeadbeefU);
      }
      cons.commit();
      i += n;
    }
  }};
  writer.join();
  reader.join();
  ZuCHECK(valid && prod.available() == 64 && !cons.available(),
    "release/acquire publishes intact descriptors and protects slots until release");
  prod.reset();
  cons.reset();
  munmap(mem, length);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLayout);
  ZuTestCall(testWrap);
  ZuTestCall(testPublication);
}
