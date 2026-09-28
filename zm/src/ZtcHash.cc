//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// hash telemetry and sizing

#include <math.h>

#include <zlib/ZuBox.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmHashMgr.hh>
#include <zlib/ZmRBTree.hh>

namespace Ztc {

// ID storage is bounded by ZuID; quote directly without temporary strings.
static void hashID(ZuVStream s, const ZuID &id) {
  s << '"';
  for (char c : ZuCSpan{id}) {
    if (c == '"') s << '"';
    s << c;
  }
  s << '"';
}

void HashCSV_print(ZuVStream s)
{
  s << "id,addr,shadow,linear,bits,cBits,loadFactor,nodeSize,"
    "count,maxCount,effLoadFactor,resized\n";
  HashMgr::capture({}, [&s](ZuSpan<const HashTelemetry> rows) {
    for (const auto &data : rows) {
      hashID(s, data.id);
      s << ',' << ZuBoxPtr(data.addr).hex() << ','
	<< unsigned(data.shadow) << ',' << unsigned(data.linear) << ','
	<< unsigned(data.bits) << ',' << unsigned(data.cBits) << ','
	<< ZuBoxed(data.loadFactor) << ',' << data.nodeSize << ','
	<< data.count << ',' << data.maxCount << ','
	<< ZuBoxed(data.effLoadFactor) << ',' << data.resized << '\n';
    }
  });
}

static unsigned hashBits(const HashTelemetry &data, double headroom) {
  if (data.resized) return data.bits;
  double target = ceil(double(data.maxCount) * (1 + headroom));
  ZmAssert_(isfinite(target) && data.loadFactor > 0);
  unsigned bits = 2;
  // Chained constructors cap bits at 28; linear tables use unsigned slots.
  unsigned limit = data.linear ? sizeof(unsigned) * 8 - 1 : 28;
  // Resizing tests count before insertion. The last insertion at the
  // threshold is allowed; fractional thresholds round upward.
  while (bits < limit && target > ceil(double(uint64_t{1} << bits) * data.loadFactor))
    ++bits;
  ZmAssert_(target <= ceil(double(uint64_t{1} << bits) * data.loadFactor));
  return bits;
}

ZmRBTreeKVDerive(HashTuneTree, ZuID, unsigned, ZmRBTreeHeapID<"">);

void HashTuneCSV_print(ZuVStream s, double headroom)
{
  ZmAssert_(isfinite(headroom) && headroom >= 0);
  HashTuneTree tree;
  HashMgr::capture({}, [&tree, headroom](ZuSpan<const HashTelemetry> rows) {
    for (const auto &data : rows) {
      unsigned bits = hashBits(data, headroom);
      if (auto node = tree.find(data.id)) {
	if (node->val() < bits) node->val() = bits;
      } else tree.add(data.id, bits);
    }
  });
  s << "id,bits,loadFactor,cBits\n";
  auto i = tree.citer();
  while (auto node = i()) {
    ZmHashParams params{node->key()};
    hashID(s, node->key());
    s << ',' << node->val() << ',' << ZuBoxed(params.loadFactor()) << ','
      << params.cBits() << '\n';
  }
}

} // Ztc
