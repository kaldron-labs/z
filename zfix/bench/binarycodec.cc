//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Body-only codec costs, independent of framing, transport and signing.
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfixFmt.hh>

#include "codec.hh"

using namespace Zfix::Fmt;
using namespace ZfixLoad;

struct TextValue {
  using Data = ZuCSpan;
  uint64_t	id = 1;
  Data data;
  uint32_t	after = 7;
};
ZfStruct(, (TextValue, FIX),
  (id, (FIX::ID<11>, Required), UInt64),
  (data, (FIX::ID<58>, Required), String),
  (after, (FIX::ID<5000>, Required), UInt32));
struct BinaryText {
  using Data = ZuCSpan;
  uint64_t	id = 1;
  Data data;
  uint32_t	after = 7;
};
ZfStruct(, (BinaryText, FIX),
  (id, (FIX::ID<11>, Required), UInt64),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), String),
  (after, (FIX::ID<5000>, Required), UInt32));
struct BinaryBytes {
  using Data = ZuBSpan;
  uint64_t	id = 1;
  Data data;
  uint32_t	after = 7;
};
ZfStruct(, (BinaryBytes, FIX),
  (id, (FIX::ID<11>, Required), UInt64),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes),
  (after, (FIX::ID<5000>, Required), UInt32));

struct BinaryResult {
  ZuCSpan	event{"binary-codec"}, mode, phase;
  unsigned	payloadBytes = 0, wireBytes = 0, count = 0, warmup = 0;
  uint64_t	sink = 0;
  Quantiles	encodeNs, decodeNs, clockNs;
};
ZfStruct(, (BinaryResult, JSON),
  (event, , String), (mode, , String), (phase, , String),
  (payloadBytes, , UInt32), (wireBytes, , UInt32),
  (count, , UInt32), (warmup, , UInt32), (sink, , UInt64),
  (encodeNs, , UDT), (decodeNs, , UDT), (clockNs, , UDT));

using Buffer = ZtArray<uint8_t, ZtArrayHeapID<"Zfix.BinaryBench">>;

static bool number(const char *text, unsigned &out)
{
  if (*text < '0' || *text > '9') return false;
  char *end;
  auto n = strtoull(text, &end, 10);
  if (*end || n > UINT_MAX) return false;
  out = unsigned(n); return true;
}

template <typename O, bool Indexed>
static bool run(ZuCSpan mode, ZuCSpan phase, unsigned size, unsigned count, unsigned warmup)
{
  constexpr bool Plain = ZuIsSame<O, TextValue>{};
  constexpr auto Hostile = "A\1" "35=X\1" "10=000\1" "\0=\xff"_z;
  // Literal extents cover the largest decimal fields and both binary prefixes.
  constexpr auto Metadata = "11=18446744073709551615\1" "212=4294967295\1"
    "213=\1" "5000=4294967295\1"_z;
  static constexpr auto Suffix = "5000=7\1"_z;
  Buffer payload, storage;
  payload.length(size, false);
  for (unsigned i = 0; i < size; ++i)
    payload[i] = Plain ? uint8_t('A' + i % 26) : uint8_t(Hostile[i % Hostile.length()]);
  unsigned capacity = size + Metadata.length();
  storage.length(capacity, false);
  Limits limits;
  limits.frameBytes = capacity;
  // Decimal uint64 values need up to twenty octets even for an empty payload.
  limits.fieldBytes = size > 20 ? size : 20;
  O object; object.data = typename O::Data{payload};
  auto encode = [&storage, &object, limits]() -> ZuCSpan {
    Sink out{storage, limits};
    if (!save(out, object) || !out.flush()) return {};
    return out.data();
  };
  auto input = encode();
  if (!input.length()) return false;
  auto decode = [&limits](ZuCSpan wire, O &value) {
    if constexpr (Indexed) {
      Zfix::Fmt::Indexed<O, ZuFacet::FIX> parsed{wire, limits};
      return parsed.valid() && parsed.load(value);
    } else return !load(value, wire, limits);
  };
  auto direct = [size](ZuCSpan wire, const O &value) {
    auto bytes = ZuBSpan{value.data};
    auto expected = wire.data() + wire.length() - Suffix.length() - 1 - size;
    return bytes.length() == size && bytes.data() ==
      reinterpret_cast<const uint8_t *>(expected);
  };
  // Full equality and direct input address are checked outside the timing loop.
  O value;
  if (!decode(input, value) || !direct(input, value) || ZuBSpan{value.data} != ZuBSpan{payload})
    return false;
  Samples encodeNs{count}, decodeNs{count}, clockNs{count};
  BinaryResult result{.mode = mode, .phase = phase, .payloadBytes = size,
    .count = count, .warmup = warmup};
  bool encodeOnly = phase == "encode", decodeOnly = phase == "decode";
  for (unsigned i = 0; i < count + warmup; ++i) {
    auto begin = monotonic(), end = monotonic();
    if (i >= warmup && !clockNs.add(end - begin)) return false;
    if (!decodeOnly) {
      object.id = i + 1;
      begin = monotonic(); input = encode(); end = monotonic();
      if (!input.length() || (i >= warmup && !encodeNs.add(end - begin))) return false;
    }
    if (!encodeOnly) {
      begin = monotonic(); bool ok = decode(input, value); end = monotonic();
      if (!ok || value.id != object.id || value.after != 7 || !direct(input, value) ||
	  (i >= warmup && !decodeNs.add(end - begin))) return false;
      result.sink += value.id;
    } else result.sink += input.length();
  }
  result.wireBytes = input.length();
  result.encodeNs = encodeNs.finish(); result.decodeNs = decodeNs.finish();
  result.clockNs = clockNs.finish(); output(result); return true;
}

int main(int argc, char **argv)
{
  // 4 KiB models a small opaque replay/drop-copy payload; CLI controls the size.
  unsigned	count = 10000, warmup = 1000, size = 4096;
  if (argc > 6 || (argc > 1 && !number(argv[1], count)) ||
      (argc > 2 && !number(argv[2], warmup)) || (argc > 3 && !number(argv[3], size)) ||
      !count || warmup > UINT_MAX - count || size > Limits::FieldBytes) return 2;
  ZuCSpan	filter = argc > 4 ? ZuCSpan{argv[4]} : ZuCSpan{};
  ZuCSpan	phase = argc > 5 ? ZuCSpan{argv[5]} : ZuCSpan{"both"};
  if (phase != "both" && phase != "encode" && phase != "decode") return 2;
  bool matched = false;
  auto mode = [filter, phase, size, count, warmup, &matched]<typename O, bool Indexed>(
      ZuCSpan name) {
    if (filter.length() && filter != name) return true;
    matched = true; return run<O, Indexed>(name, phase, size, count, warmup);
  };
  if (!mode.template operator()<TextValue, false>("text-live") ||
      !mode.template operator()<TextValue, true>("text-indexed") ||
      !mode.template operator()<BinaryText, false>("string-live") ||
      !mode.template operator()<BinaryText, true>("string-indexed") ||
      !mode.template operator()<BinaryBytes, false>("bytes-live") ||
      !mode.template operator()<BinaryBytes, true>("bytes-indexed")) return 1;
  return matched ? 0 : 2;
}
