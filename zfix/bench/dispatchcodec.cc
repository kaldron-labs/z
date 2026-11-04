//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfixDispatch.hh>
#include <zlib/ZfixRxMsg.hh>
#include "../example/zfix.hh"
#include "codec.hh"

namespace Example {
  ZtEnumImplNS(Side, FIX);
}

using namespace Zfix;
using namespace Zfix::Fmt;
using namespace ZfixLoad;

struct DispatchResult {
  ZuCSpan	mode;
  unsigned	parties, bytes, count;
  uint64_t	sink;
  Quantiles	decodeNs, clockNs;
};

ZfStruct(, (DispatchResult, JSON), (mode, , String), (parties, , UInt32),
  (bytes, , UInt32), (count, , UInt32), (sink, , UInt64),
  (decodeNs, , UDT), (clockNs, , UDT));

static ZuSpan<char> grow(void *owner, unsigned, unsigned needed) {
  auto &s = *static_cast<Text *>(owner); s.length(needed);
  return s;
}
template <typename Dictionary>
static bool run(ZuCSpan mode, unsigned parties, unsigned count, unsigned warmup,
    unsigned managed = 0) {
  Text storage;
  FrameWriter writer{{}, "FIX.4.4", grow, &storage};
  writer.body() << "35=8\1" "49=CLIENT\1" "56=SERVER\1" "34=7\1"
    "52=20261028-12:34:56\1" "37=1\1" "11=2\1" "17=3\1" "150=F\1"
    "39=2\1" "55=IBM\1" "54=1\1" "38=100\1" "44=12.5\1"
    "151=0\1" "14=100\1" "6=12.5\1" "32=100\1" "31=12.5\1"
    "453=" << parties << '\1';
  for (unsigned i = 0; i < parties; ++i)
    writer.body() << "448=P" << i << "\1" "447=D\1" "452=1\1";
  auto wire = writer.finish();
  if (!wire) return false;
  auto parsed = frame(wire.data);
  if (parsed.state != FrameState::Complete) return false;
  RxMsg input; input.frame = parsed.view;
  Samples decode{count}, clock{count};
  DispatchResult result{mode, parties, unsigned(wire.data.length()), count, 0, {}, {}};
  for (unsigned i = 0; i < count + warmup; ++i) {
    auto begin = monotonic(), end = monotonic();
    if (i >= warmup && !clock.add(end - begin)) return false;
    begin = monotonic();
    if (managed && catalogSession<Dictionary>(input)) return false;
    auto error = dispatch<Dictionary>(parsed.view,
      [&result, parties](auto *, const auto &o) -> Error {
	if constexpr (ZuIsSame<ZuDecay<decltype(o)>, Example::Execution>{}) {
	  if (o.key != 2 || o.parties.length() != parties) return {ErrorCode::Value};
	  result.sink += o.exec; return {};
	} else return {ErrorCode::Unresolved};
      });
    end = monotonic();
    if (error || (i >= warmup && !decode.add(end - begin))) return false;
  }
  result.decodeNs = decode.finish(); result.clockNs = clock.finish();
  output(result); return true;
}
int main(int argc, char **argv) {
  unsigned count = 10000, warmup = 1000;
  if (argc > 1) count = atoi(argv[1]);
  if (argc > 2) warmup = atoi(argv[2]);
  if (argc > 4 || !count || warmup > UINT_MAX - count) return 2;
  ZuCSpan filter = argc > 3 ? ZuCSpan{argv[3]} : ZuCSpan{};
  bool matched = false;
  using Plain = MTList<MT<Example::Execution, "8">>;
  using Shared = MTList<Example::Ack, Example::Fill>;
  using Mixed = MTList<Example::Fill,
    MT<Example::Event, "8", ZuFacet::FIX, Discrim<150, "0">>>;
  for (unsigned n : {0U, 8U}) {
    auto bench = [filter, &matched, n, count, warmup]<typename D>(ZuCSpan mode,
	unsigned managed = 0) {
      if (filter.length() && filter != mode) return true;
      matched = true;
      return run<D>(mode, n, count, warmup, managed);
    };
    if (!bench.template operator()<Plain>("primary") ||
        !bench.template operator()<Shared>("same-schema") ||
        !bench.template operator()<Mixed>("different-schema") ||
	!bench.template operator()<Plain>("managed-primary", 1) ||
	!bench.template operator()<Shared>("managed-secondary", 1)) return 1;
  }
  return matched ? 0 : 2;
}
