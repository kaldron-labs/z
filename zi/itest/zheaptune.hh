//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zheaptune_HH
#define zheaptune_HH

#include <zlib/ZuString.hh>
#include <zlib/ZtcHeap.hh>
#include <zlib/ZfStruct.hh>
#include <zlib/ZtArray.hh>

namespace HeapTest {

inline constexpr auto header = "id,partition,vshift,cacheSize,cpuset\n"_Zu;

struct Report : public Ztc::HeapTelemetry {
  uint8_t phase = 0;
};

ZfStruct(, Report,
  (((phase)), (UInt8)),
  (((id)), (String)),
  (((size)), (UInt32)),
  (((partition)), (UInt16)),
  (((sharded)), (Bool)),
  (((alignment)), (UInt16)),
  (((vshift)), (UInt8)),
  (((cacheSize)), (UInt64)),
  (((cpuset)), (UDT)),
  (((cacheAllocs)), (UInt64)),
  (((cacheFrees)), (UInt64)),
  (((crossFrees)), (UInt64)),
  (((heapAllocs)), (UInt64)),
  (((heapFrees)), (UInt64)),
  (((globalHeapAllocs)), (UInt64)),
  (((globalHeapFrees)), (UInt64)),
  (((globalHeapMax)), (UInt64)));

using Reports = ZtArray<Report, ZtArrayHeapID<"HeapTest.Reports">>;

inline bool workload(ZuID id) {
  ZuCSpan s{id};
  return s.length() >= 3 && s[0] == 'H' && s[1] == 'T' && s[2] == '.';
}

inline bool sameArena(const Report &a, const Report &b) {
  return a.id == b.id && a.partition == b.partition && a.size == b.size &&
    a.alignment == b.alignment && a.sharded == b.sharded;
}

} // HeapTest

#endif
