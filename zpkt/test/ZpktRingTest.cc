//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <sys/mman.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmThread.hh>
#include <zlib/ZmAtomic.hh>

#include <zlib/ZtcQueue.hh>

#include <zlib/ZpktRing.hh>

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
  ZuCHECK(Zpkt::Xdp::TxRing::mapLength(map.off, 8, length) && length == 384,
    "mapping length includes descriptors at queried offset");
  ZuCHECK(!Zpkt::Xdp::TxRing::mapLength(map.off, 0, length) &&
      !Zpkt::Xdp::TxRing::mapLength(map.off, 3, length),
    "zero and non-power-of-two capacities rejected");
  auto bad = map.off;
  bad.desc = UINT64_MAX - 7;
  ZuCHECK(!Zpkt::Xdp::TxRing::mapLength(bad, 8, length), "mapping overflow rejected");
  bad = map.off;
  bad.consumer = bad.producer;
  ZuCHECK(!Zpkt::Xdp::TxRing::mapLength(bad, 8, length), "overlapping fields rejected");
  bad = map.off;
  ++bad.desc;
  ZuCHECK(!Zpkt::Xdp::TxRing::mapLength(bad, 8, length), "unaligned descriptor rejected");
  Zpkt::Xdp::TxRing tx;
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
  Zpkt::Xdp::Ring<Entry, true> prod;
  Zpkt::Xdp::Ring<Entry, false> cons;
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

static void testProgress()
{
  ZuTestScope(testProgress);
  Mapping map;
  map.seed(UINT32_MAX - 1);
  Zpkt::Xdp::TxRing prod;
  Zpkt::Xdp::RxRing cons;
  ZuCHECK(prod.init(map.bytes, sizeof(map.bytes), map.off, 8) &&
      cons.init(map.bytes, sizeof(map.bytes), map.off, 8), "progress fixture attached");
  ZuCHECK(prod.reserve(2) == 2, "initial descriptors reserved");
  prod.entry(0) = {}; prod.entry(1) = {};
  prod.commit();
  auto consumed = prod.consumed();
  auto pending = prod.published();
  ZuCHECK(cons.reserve(2) == 2, "kernel consumes initial batch");
  cons.commit();
  ZuCHECK(prod.reserve(2) == 2, "producer replaces consumed batch");
  prod.entry(0) = {}; prod.entry(1) = {};
  prod.commit();
  ZuCHECK(prod.published() == pending && prod.consumed() != consumed &&
      prod.consumed() == 0,
    "consumption advances across wrap despite unchanged ring occupancy");
  consumed = prod.consumed();
  ZuCHECK(prod.reserve(1) == 1, "producer can grow pending batch");
  prod.entry(0) = {};
  prod.commit();
  ZuCHECK(prod.consumed() == consumed && prod.published() == pending + 1,
    "publication alone is not kernel progress");
}

static void testPublication()
{
  ZuTestScope(testPublication);
  xdp_ring_offset off{0, 64, 192, 128};
  size_t length;
  ZuCHECK(Zpkt::Xdp::TxRing::mapLength(off, 64, length), "mapped fixture length");
  auto mem = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  ZuCHECK(mem != MAP_FAILED, "shared ring fixture mapped");
  if (mem == MAP_FAILED) return;
  Zpkt::Xdp::TxRing prod;
  Zpkt::Xdp::RxRing cons;
  ZuCHECK(prod.init(mem, length, off, 64) && cons.init(mem, length, off, 64),
    "both peers attached to mapped fixture");
  ZuCHECK(!prod.published(), "sender sees an initially empty mapped Tx ring");
  bool valid = true;
  enum { N = 200000 };
  ZmAtomic<unsigned> done{0}, implausible{0};
  ZmThread observer{[&prod, &done, &implausible] {
    // A producer-first snapshot can underflow if the consumer overtakes it.
    // With only N publications, a valid snapshot cannot report more than N.
    while (!unsigned(done))
      if (prod.published() > N) ++implausible;
    if (prod.published() > N) ++implausible;
  }};
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
  ZmThread reader{[&cons, &valid, &done] {
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
    done = 1;
  }};
  writer.join();
  reader.join();
  observer.join();
  ZuCHECK(valid && !unsigned(implausible) && !prod.published() &&
      prod.available() == 64 && !cons.available(),
    "release/acquire publishes intact descriptors and bounds concurrent Tx snapshots");
  prod.reset();
  cons.reset();
  munmap(mem, length);
}

static void testTelemetry()
{
  ZuTestScope(testTelemetry);
  Mapping offsets;
  void *mapping = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ZuCHECK(mapping != MAP_FAILED, "telemetry fixture mapping");
  if (mapping == MAP_FAILED) return;
  Zpkt::Xdp::TxRing producer;
  Zpkt::Xdp::RxRing consumer;
  producer.identify("test", "xdp.1.0.tx", Ztc::QueueType::Tx);
  consumer.identify("test", "xdp.1.0.rx", Ztc::QueueType::Rx);
  bool attached = producer.init(mapping, 4096, offsets.off, 8) &&
    consumer.init(mapping, 4096, offsets.off, 8);
  ZuCHECK(attached, "both telemetry views attach to the real mapped positions");
  if (!attached) { producer.reset(); consumer.reset(); munmap(mapping, 4096); return; }
  ZuCheck(producer.reserve(8) == 8);
  for (unsigned i = 0; i < 8; ++i) producer.entry(i) = {i, 64, 0};
  producer.commit();
  ZuCheck(!producer.reserve(1) && !producer.reserve(9) && !producer.reserve(0));
  ZuCheck(consumer.reserve(3) == 3);
  consumer.commit();
  ZuCheck(producer.reserve(2) == 2);
  producer.cancel();
  Ztc::Queue *queue = &producer;
  Ztc::QueueTelemetry data;
  queue->telemetry(data);
  ZuCHECK(data.ownerID == "test" && data.id == "xdp.1.0.tx" &&
      data.type == Ztc::QueueType::Tx && data.size == 8 && data.count == 5 &&
      data.inCount == 8 && data.outCount == 3 && data.full == 2,
    "actual Queue telemetry counts capacity refusals without counting cancellation or zero work");
  consumer.telemetry(data);
  ZuCHECK(data.type == Ztc::QueueType::Rx && data.size == 8 && data.count == 5 &&
      data.inCount == 8 && data.outCount == 3,
    "consumer telemetry uses mapped peer progress rather than its cached reserve state");
  producer.reset(); consumer.reset();
  munmap(mapping, 4096);
  queue->telemetry(data);
  ZuCHECK(data.size == 8 && data.count == 5 && data.full == 2 &&
      data.inCount == 8 && data.outCount == 3,
    "closed ring retains its final telemetry after the descriptor mapping is gone");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testLayout);
  ZuTestCall(testWrap);
  ZuTestCall(testProgress);
  ZuTestCall(testPublication);
  ZuTestCall(testTelemetry);
}
