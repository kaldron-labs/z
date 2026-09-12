//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb SQLite canonical storage codecs

#ifndef ZdbSLCodec_HH
#define ZdbSLCodec_HH

#include <string.h>

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTime.hh>

namespace ZdbSL {

template <typename T>
inline void saveUnsigned(uint8_t *ptr, T value)
{
  if constexpr (sizeof(T) == 1)
    *ptr = value;
  else {
    ZuBigEndian<T> data{value};
    memcpy(ptr, &data, sizeof(data));
  }
}

template <typename T>
inline T loadUnsigned_(const uint8_t *ptr)
{
  if constexpr (sizeof(T) == 1)
    return *ptr;
  else {
    T data;
    memcpy(&data, ptr, sizeof(data));
    return ZuBE(data);
  }
}

template <typename T>
inline bool loadUnsigned(ZuBSpan &data, T &value)
{
  if (data.length() < sizeof(T)) return false;
  value = loadUnsigned_<T>(data.data());
  data.offset(sizeof(T));
  return true;
}

template <typename T, typename U>
inline void saveSigned(uint8_t *ptr, T value)
{
  U encoded = ZuPun<T, U>(value).out ^
    U(U(1)<<(sizeof(U) * 8 - 1));
  saveUnsigned(ptr, encoded);
}

template <typename T, typename U>
inline T loadSigned_(const uint8_t *ptr)
{
  U value = loadUnsigned_<U>(ptr) ^ (U(1)<<(sizeof(U) * 8 - 1));
  return ZuPun<U, T>(value).out;
}

template <typename T, typename U>
inline bool loadSigned(ZuBSpan &data, T &value)
{
  if (data.length() < sizeof(U)) return false;
  value = loadSigned_<T, U>(data.data());
  data.offset(sizeof(U));
  return true;
}

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

using UNData = ZuBigEndian<uint64_t>;
using SNData = ZuBigEndian<uint128_t>;
ZuAssert(sizeof(UNData) == sizeof(uint64_t));
ZuAssert(sizeof(SNData) == sizeof(uint128_t));

void saveU64(uint8_t *, uint64_t);
uint64_t loadU64_(const uint8_t *);
bool loadU64(ZuBSpan, uint64_t &);
void saveU128(uint8_t *, uint128_t);
uint128_t loadU128_(const uint8_t *);
bool loadU128(ZuBSpan, uint128_t &);
void saveS128(uint8_t *, int128_t);
int128_t loadS128_(const uint8_t *);
bool loadS128(ZuBSpan, int128_t &);
void saveFloat(uint8_t *, double);
double loadFloat_(const uint8_t *);
bool loadFloat(ZuBSpan, double &);
void saveTime(uint8_t *, ZuTime);
ZuTime loadTime_(const uint8_t *);
bool loadTime(ZuBSpan, ZuTime &);
void saveDateTime(uint8_t *, ZuDateTime);
ZuDateTime loadDateTime_(const uint8_t *);
bool loadDateTime(ZuBSpan, ZuDateTime &);
void saveDecimal(uint8_t *, ZuDecimal);
ZuDecimal loadDecimal_(const uint8_t *);
bool loadDecimal(ZuBSpan, ZuDecimal &);
void saveFixed(uint8_t *, ZuFixed);
ZuFixed loadFixed_(const uint8_t *);
bool loadFixed(ZuBSpan, ZuFixed &);

} // ZdbSL

#endif /* ZdbSLCodec_HH */
