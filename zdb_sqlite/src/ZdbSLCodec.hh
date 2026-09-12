//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb SQLite canonical storage codecs

#ifndef ZdbSLCodec_HH
#define ZdbSLCodec_HH

#ifndef ZdbSLLib_HH
#include <zlib/ZdbSLLib.hh>
#endif

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>

namespace ZdbSL {

#pragma pack(push, 1)
struct TimeData {
  ZuBigEndian<uint64_t>	sec;
  ZuBigEndian<uint32_t>	nsec;
};
struct DateTimeData {
  ZuBigEndian<uint32_t>	julian;
  ZuBigEndian<uint32_t>	sec;
  ZuBigEndian<uint32_t>	nsec;
};
#pragma pack(pop)

ZuAssert(sizeof(TimeData) == 12);
ZuAssert(sizeof(DateTimeData) == 12);

ZdbSLAPI void saveU64(uint8_t *, uint64_t);
ZdbSLAPI bool loadU64(ZuBSpan, uint64_t &);
ZdbSLAPI void saveU128(uint8_t *, uint128_t);
ZdbSLAPI bool loadU128(ZuBSpan, uint128_t &);
ZdbSLAPI void saveS128(uint8_t *, int128_t);
ZdbSLAPI bool loadS128(ZuBSpan, int128_t &);
ZdbSLAPI void saveFloat(uint8_t *, double);
ZdbSLAPI bool loadFloat(ZuBSpan, double &);
ZdbSLAPI void saveTime(uint8_t *, ZuTime);
ZdbSLAPI bool loadTime(ZuBSpan, ZuTime &);
ZdbSLAPI void saveDateTime(uint8_t *, ZuDateTime);
ZdbSLAPI bool loadDateTime(ZuBSpan, ZuDateTime &);
ZdbSLAPI void saveDecimal(uint8_t *, ZuDecimal);
ZdbSLAPI bool loadDecimal(ZuBSpan, ZuDecimal &);
ZdbSLAPI void saveFixed(uint8_t *, ZuFixed);
ZdbSLAPI bool loadFixed(ZuBSpan, ZuFixed &);

} // ZdbSL

#endif /* ZdbSLCodec_HH */
