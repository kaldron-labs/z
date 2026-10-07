//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zhashtune_HH
#define zhashtune_HH

#include <zlib/ZuString.hh>
#include <zlib/ZtcHash.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfStruct.hh>

namespace HashTest {

inline constexpr auto header = "id,bits,loadFactor,cBits\n"_z;

struct Report : public Ztc::HashTelemetry {
  uint8_t phase = 0;
};
ZfStruct(, Report,
  (phase,,		UInt8),
  (id,,			String),
  (addr, (Hex),		UInt64),
  (loadFactor,,		Float),
  (effLoadFactor,,	Float),
  (count,,		UInt64),
  (maxCount,,		UInt64),
  (nodeSize,,		UInt32),
  (resized,,		UInt32),
  (bits,,		UInt8),
  (cBits,,		UInt8),
  (linear,,		UInt8),
  (shadow,,		UInt8));

using Reports = ZtArray<Report, ZtArrayHeapID<"HashTest.Reports">>;

inline bool workload(ZuID id) {
  ZuCSpan s{id};
  return s.length() >= 3 && s[0] == 'H' && s[1] == 'S' && s[2] == '.';
}
inline bool sameTable(const Report &a, const Report &b) {
  return a.id == b.id && a.addr == b.addr;
}

} // HashTest
#endif /* zhashtune_HH */
