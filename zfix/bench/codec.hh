//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixBenchCodec_HH
#define ZfixBenchCodec_HH

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "../util/load_run.hh"

namespace ZfixBench {

using namespace ZfixLoad;
struct CodecResult {
  ZuCSpan	event{"codec"}, engine, shape;
  uint64_t	bytes = 0, sink = 0;
  unsigned	count = 0, warmup = 0;
  Quantiles	encodeNs, decodeNs, clockNs;
};

ZfStruct(, (CodecResult, JSON), (event, , String), (engine, , String),
  (shape, , String), (bytes, , UInt64), (sink, , UInt64),
  (count, , UInt32), (warmup, , UInt32), (encodeNs, , UDT),
  (decodeNs, , UDT), (clockNs, , UDT));
template <typename Engine> int codec(Engine &engine, int argc, char **argv) {
  unsigned count = 10000, warmup = 1000;
  if (argc > 1) {
    char *end; auto n = strtoull(argv[1], &end, 10);
    if (*end || !n || n > UINT_MAX) return 2;
    count = unsigned(n);
  }
  if (argc > 2) {
    char *end; auto n = strtoull(argv[2], &end, 10);
    if (end == argv[2] || *end || n > UINT_MAX - count) return 2;
    warmup = unsigned(n);
  }
  if (argc > 3 || warmup > UINT_MAX - count) return 2;
  auto run = [count, warmup, &engine](ZuCSpan name, Shape shape) {
    Model model{shape};
    Samples encode{count}, decode{count}, clock{count};
    CodecResult result; result.engine = Engine::name;
    result.shape = name; result.count = count; result.warmup = warmup;
    // Identical input in both engines; trailing fractional zeros would let
    // FIXDeflt trim bytes that QuickFIX's fixed precision retains.
    auto time = ZuDateTime{ZuDateTimeScan::FIX{}, "20261028-12:34:56.123456789"};
    for (unsigned i = 0; i < count + warmup; ++i) {
      const auto &order = model.next(i + 1, time);
      auto begin = monotonic(); auto end = monotonic();
      if (i >= warmup && !clock.add(end - begin)) return false;
      begin = monotonic(); auto bytes = engine.encode(order); end = monotonic();
      if (!bytes.length()) return false;
      if (i >= warmup && !encode.add(end - begin)) return false;
      begin = monotonic(); auto key = engine.decode(bytes, order.qty); end = monotonic();
      if (key != order.key) return false;
      if (i >= warmup && !decode.add(end - begin)) return false;
      result.bytes = bytes.length(); result.sink += key;
    }
    result.encodeNs = encode.finish(); result.decodeNs = decode.finish();
    result.clockNs = clock.finish(); output(result); return true;
  };
  return !(run("scalar", {}) && run("groups", {8, 4, 32, 0}) &&
    run("binary", {0, 0, 0, 4096}));
}
} // ZfixBench

#endif /* ZfixBenchCodec_HH */
