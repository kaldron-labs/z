//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmHeap telemetry

#include <zlib/ZtcHeap.hh>

#include <zlib/ZmRBTree.hh>

namespace Ztc {

void HeapCSV_print(ZuVStream s)
{
  s <<
    "id,size,partition,sharded,alignment,vshift,cacheSize,cpuset,"
    "cacheAllocs,cacheFrees,crossFrees,heapAllocs,heapFrees,"
    "globalHeapAllocs,globalHeapFrees,globalHeapMax\n";
  HeapMgr::all([&s](Heap *heap) {
    HeapTelemetry data;
    heap->telemetry(data);
    if (!data.cacheAllocs && !data.heapAllocs &&
	!data.cacheFrees && !data.heapFrees) return;
    s <<
      '"' << data.id << "\"," <<	// assume no need to quote embedded "
      ZuBoxed(data.size) << ',' <<
      ZuBoxed(data.partition) << ',' <<
      ZuBoxed(data.sharded) << ',' <<
      ZuBoxed(data.alignment) << ',' <<
      ZuBoxed(data.vshift) << ',' <<
      ZuBoxed(data.cacheSize) << ',' <<
      '"' << data.cpuset << "\"," <<
      ZuBoxed(data.cacheAllocs) << ',' <<
      ZuBoxed(data.cacheFrees) << ',' <<
      ZuBoxed(data.crossFrees) << ',' <<
      ZuBoxed(data.heapAllocs) << ',' <<
      ZuBoxed(data.heapFrees) << ',' <<
      ZuBoxed(data.globalHeapAllocs) << ',' <<
      ZuBoxed(data.globalHeapFrees) << ',' <<
      ZuBoxed(data.globalHeapMax) << '\n';
  });
}

struct HeapTune {
  uint64_t	count = 0;
  uint64_t	cacheSize = 0;
  uint64_t	heapAllocs = 0;
  uint64_t	globalHeapAllocs = 0;
  uint64_t	globalHeapMax = 0;
  ZmBitmap	cpuset;
};
ZmRBTreeKVDerive(HeapTuneTree,
  (ZuTuple<ZuID, uint16_t, uint8_t>), HeapTune, ZmRBTreeHeapID<"">);

void HeapTuneCSV_print(ZuVStream s, double headroom)
{
  HeapTuneTree tree;
  s << "id,partition,vshift,cacheSize,cpuset\n";
  HeapMgr::all([&tree](Heap *heap) {
    HeapTelemetry data;
    heap->telemetry(data);
    auto &&key = ZuFwdTuple(data.id, data.partition, data.vshift);
    if (auto node = tree.find(key)) {
      auto &tune = node->val();
      ++tune.count;
      tune.cacheSize += data.cacheSize;
      tune.heapAllocs += data.heapAllocs;
      tune.globalHeapAllocs += data.globalHeapAllocs;
      tune.globalHeapMax += data.globalHeapMax;
    } else {
      tree.add(key, HeapTune{
	.count = 1,
	.cacheSize = data.cacheSize,
	.heapAllocs = data.heapAllocs,
	.globalHeapAllocs = data.globalHeapAllocs,
	.globalHeapMax = data.globalHeapMax,
	.cpuset = data.cpuset
      });
    }
  });
  auto i = tree.citer();
  double factor = headroom + 1;
  while (auto node = i()) {
    const auto &data = node->val();
    double cacheSize = double(data.cacheSize) / data.count;
    double weight = !data.globalHeapAllocs ? 1.0 :
      double(data.heapAllocs) / double(data.globalHeapAllocs);
    double heapMax = double(data.globalHeapMax) / data.count;
    cacheSize += (heapMax * factor) * weight;
    s <<
      '"' << node->key().p<0>() << "\"," <<
      ZuBoxed(node->key().p<1>()) << ',' <<
      ZuBoxed(node->key().p<2>()) << ',' <<
      ZuBoxed(uint64_t(cacheSize)) << ',' <<
      '"' << data.cpuset << "\"\n";
  }
}

} // Ztc
