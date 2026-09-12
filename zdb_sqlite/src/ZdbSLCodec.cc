//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <math.h>
#include <string.h>

#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

template <typename T>
static void saveU(uint8_t *ptr, T v)
{
  T value = ZuBE(v);
  memcpy(ptr, &value, sizeof(value));
}

template <typename T>
static bool loadU(ZuBSpan data, T &v)
{
  if (data.length() != sizeof(T)) return false;
  T value;
  memcpy(&value, data.data(), sizeof(value));
  v = ZuBE(value);
  return true;
}

template <typename T, typename U>
static void saveS(uint8_t *ptr, T v)
{
  U u = ZuPun<T, U>(v).out ^ (U(1)<<(sizeof(U) * 8 - 1));
  saveU(ptr, u);
}

template <typename T, typename U>
static bool loadS(ZuBSpan data, T &v)
{
  U u;
  if (!loadU(data, u)) return false;
  u ^= U(1)<<(sizeof(U) * 8 - 1);
  v = ZuPun<U, T>(u).out;
  return true;
}

void saveU64(uint8_t *ptr, uint64_t v) { saveU(ptr, v); }
bool loadU64(ZuBSpan data, uint64_t &v) { return loadU(data, v); }
void saveU128(uint8_t *ptr, uint128_t v) { saveU(ptr, v); }
bool loadU128(ZuBSpan data, uint128_t &v) { return loadU(data, v); }
void saveS128(uint8_t *ptr, int128_t v) { saveS<int128_t, uint128_t>(ptr, v); }
bool loadS128(ZuBSpan data, int128_t &v) {
  return loadS<int128_t, uint128_t>(data, v);
}

void saveFloat(uint8_t *ptr, double v)
{
  uint64_t u;
  if (isnan(v))
    u = UINT64_C(0x7ff8000000000000);
  else if (!v)
    u = 0;
  else
    u = ZuPun<double, uint64_t>(v).out;
  u = (u>>63) ? ~u : (u ^ (UINT64_C(1)<<63));
  saveU(ptr, u);
}

bool loadFloat(ZuBSpan data, double &v)
{
  uint64_t u;
  if (!loadU(data, u)) return false;
  u = (u>>63) ? (u ^ (UINT64_C(1)<<63)) : ~u;
  v = ZuPun<uint64_t, double>(u).out;
  return true;
}

void saveTime(uint8_t *ptr, ZuTime v)
{
  saveS<int64_t, uint64_t>(ptr, v.sec());
  saveS<int32_t, uint32_t>(ptr + 8, v.nsec());
}

bool loadTime(ZuBSpan data, ZuTime &v)
{
  if (data.length() != sizeof(TimeData)) return false;
  int64_t sec;
  int32_t nsec;
  if (!loadS<int64_t, uint64_t>({data.data(), 8}, sec) ||
      !loadS<int32_t, uint32_t>({data.data() + 8, 4}, nsec)) return false;
  v = ZuTime{sec, nsec};
  return true;
}

void saveDateTime(uint8_t *ptr, ZuDateTime v)
{
  saveS<int32_t, uint32_t>(ptr, v.julian());
  saveS<int32_t, uint32_t>(ptr + 4, v.sec());
  saveS<int32_t, uint32_t>(ptr + 8, v.nsec());
}

bool loadDateTime(ZuBSpan data, ZuDateTime &v)
{
  if (data.length() != sizeof(DateTimeData)) return false;
  int32_t julian, sec, nsec;
  if (!loadS<int32_t, uint32_t>({data.data(), 4}, julian) ||
      !loadS<int32_t, uint32_t>({data.data() + 4, 4}, sec) ||
      !loadS<int32_t, uint32_t>({data.data() + 8, 4}, nsec)) return false;
  v = ZuDateTime{ZuDateTime::Julian{julian}, sec, nsec};
  return true;
}

void saveDecimal(uint8_t *ptr, ZuDecimal v) { saveS128(ptr, v.value); }

bool loadDecimal(ZuBSpan data, ZuDecimal &v)
{
  int128_t value;
  if (!loadS128(data, value)) return false;
  v = ZuDecimal{ZuDecimal::Unscaled{value}};
  return true;
}

void saveFixed(uint8_t *ptr, ZuFixed v)
{
  saveDecimal(ptr, *v ? v.decimal() : ZuDecimal{});
}

bool loadFixed(ZuBSpan data, ZuFixed &v)
{
  ZuDecimal decimal;
  if (!loadDecimal(data, decimal)) return false;
  v = decimal.value == ZuDecimal::null() ? ZuFixed{} : ZuFixed{decimal};
  return true;
}

} // ZdbSL
