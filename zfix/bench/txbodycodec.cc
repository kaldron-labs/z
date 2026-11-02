//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Actual retained TxBody allocation costs around pooled-buffer thresholds.
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZfixTxMsg.hh>
#include "codec.hh"

using namespace Zfix;
using namespace Zfix::Fmt;
using namespace ZfixLoad;

struct Dense {
  uint64_t	id = 1;
  ZuBSpan	data;
  uint32_t	after = 7;
};
ZfStruct(, (Dense, FIX),
  (id, (FIX::ID<11>, Required), UInt64),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (after, (FIX::ID<5000>, Required), UInt32));
struct Sparse {
  uint64_t	id = 1;
  ZuBSpan	data;
  uint32_t	after = 7;
  ZuCSpan	a, b, c, d, e, f, g, h, i, j, k, l;
};
ZfStruct(, (Sparse, FIX),
  (id, (FIX::ID<11>, Required), UInt64),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (a, (FIX::ID<5101>, FIX::Opt), String),
  (b, (FIX::ID<5102>, FIX::Opt), String),
  (c, (FIX::ID<5103>, FIX::Opt), String),
  (d, (FIX::ID<5104>, FIX::Opt), String),
  (e, (FIX::ID<5105>, FIX::Opt), String),
  (f, (FIX::ID<5106>, FIX::Opt), String),
  (g, (FIX::ID<5107>, FIX::Opt), String),
  (h, (FIX::ID<5108>, FIX::Opt), String),
  (i, (FIX::ID<5109>, FIX::Opt), String),
  (j, (FIX::ID<5110>, FIX::Opt), String),
  (k, (FIX::ID<5111>, FIX::Opt), String),
  (l, (FIX::ID<5112>, FIX::Opt), String),
  (after, (FIX::ID<5000>, Required), UInt32));

struct TxBodyResult {
  ZuCSpan	event{"txbody-codec"}, mode;
  unsigned	payloadBytes = 0, bodyBytes = 0, retainedBytes = 0, poolBytes = 0;
  unsigned	leaves = 0, count = 0, warmup = 0;
  uint64_t	memoryCharge = 0, sink = 0;
  Quantiles	encodeNs, clockNs;
};
ZfStruct(, (TxBodyResult, JSON),
  (event, , String), (mode, , String), (payloadBytes, , UInt32), (bodyBytes, , UInt32),
  (retainedBytes, , UInt32), (poolBytes, , UInt32), (leaves, , UInt32),
  (count, , UInt32), (warmup, , UInt32), (memoryCharge, , UInt64), (sink, , UInt64),
  (encodeNs, , UDT), (clockNs, , UDT));

static bool number(const char *text, unsigned &out)
{
  if (*text < '0' || *text > '9') return false;
  char *end; auto n = strtoull(text, &end, 10);
  if (*end || n > UINT_MAX) return false;
  out = unsigned(n); return true;
}

template <typename O> static bool run(
    ZuCSpan mode, unsigned size, unsigned count, unsigned warmup)
{
  using Buffer = ZtArray<uint8_t, ZtArrayHeapID<"Zfix.TxBodyBench">>;
  Buffer payload; payload.length(size, false);
  constexpr auto Pattern = "A\1" "35=X\1" "10=000\1" "\0=\xff"_z;
  for (unsigned i = 0; i < size; ++i) payload[i] = Pattern[i % Pattern.length()];
  O object; object.data = ZuBSpan{payload};
  Samples encodeNs{count}, clockNs{count};
  TxBody::Ref body;
  Fmt::Error error;
  TxBodyResult result{.mode = mode, .payloadBytes = size,
    .leaves = Schema<O, ZuFacet::FIX>::Leaves::N, .count = count, .warmup = warmup};
  for (unsigned i = 0; i < count + warmup; ++i) {
    // Destruction is outside the encode interval. The previous buffer is
    // released before the next factory call, permitting normal pool reuse.
    body = nullptr; object.id = i + 1;
    auto begin = monotonic(), end = monotonic();
    if (i >= warmup && !clockNs.add(end - begin)) return false;
    begin = monotonic();
    body = TxBody::encode<MT<O, "n">>(object, object.id, error);
    end = monotonic();
    if (!body || error || (i >= warmup && !encodeNs.add(end - begin))) return false;
    result.sink += body->body().length();
  }
  Dense value;
  if (Fmt::load(value, body->body()) || value.id != object.id || value.after != object.after ||
      value.data != ZuBSpan{payload}) return false;
  result.bodyBytes = body->body().length(); result.retainedBytes = body->length;
  result.poolBytes = body->size; result.memoryCharge = body->memory();
  result.encodeNs = encodeNs.finish(); result.clockNs = clockNs.finish();
  output(result); return true;
}

int main(int argc, char **argv)
{
  // The default straddles the configured pool's nominal size. Use a size scan
  // to find actual dense/sparse transitions rather than assuming a layout.
  unsigned	count = 10000, warmup = 1000, size = ZiIOBuf_DefltSize;
  if (argc > 5 || (argc > 1 && !number(argv[1], count)) ||
      (argc > 2 && !number(argv[2], warmup)) || (argc > 3 && !number(argv[3], size)) ||
      !count || warmup > UINT_MAX - count || size > Limits::FieldBytes) return 2;
  ZuCSpan	mode = argc > 4 ? ZuCSpan{argv[4]} : ZuCSpan{};
  if (mode.length() && mode != "dense" && mode != "sparse") return 2;
  if ((!mode.length() || mode == "dense") && !run<Dense>("dense", size, count, warmup)) return 1;
  if ((!mode.length() || mode == "sparse") &&
      !run<Sparse>("sparse", size, count, warmup)) return 1;
  return 0;
}
