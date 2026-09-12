//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <math.h>
#include <string.h>

#include <zlib/ZdbSLCodec.hh>

namespace ZdbSL {

void saveU64(uint8_t *ptr, uint64_t v) { saveUnsigned(ptr, v); }
uint64_t loadU64_(const uint8_t *ptr) { return loadUnsigned_<uint64_t>(ptr); }
bool loadU64(ZuBSpan data, uint64_t &v) {
  if (data.length() != sizeof(v)) return false;
  v = loadU64_(data.data());
  return true;
}
void saveU128(uint8_t *ptr, uint128_t v) { saveUnsigned(ptr, v); }
uint128_t loadU128_(const uint8_t *ptr) {
  return loadUnsigned_<uint128_t>(ptr);
}
bool loadU128(ZuBSpan data, uint128_t &v) {
  if (data.length() != sizeof(v)) return false;
  v = loadU128_(data.data());
  return true;
}
void saveS128(uint8_t *ptr, int128_t v) {
  saveSigned<int128_t, uint128_t>(ptr, v);
}
int128_t loadS128_(const uint8_t *ptr) {
  return loadSigned_<int128_t, uint128_t>(ptr);
}
bool loadS128(ZuBSpan data, int128_t &v) {
  if (data.length() != sizeof(v)) return false;
  v = loadS128_(data.data());
  return true;
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
  saveUnsigned(ptr, u);
}

bool loadFloat(ZuBSpan data, double &v)
{
  if (data.length() != sizeof(v)) return false;
  v = loadFloat_(data.data());
  return true;
}

double loadFloat_(const uint8_t *ptr)
{
  uint64_t u = loadUnsigned_<uint64_t>(ptr);
  u = (u>>63) ? (u ^ (UINT64_C(1)<<63)) : ~u;
  return ZuPun<uint64_t, double>(u).out;
}

void saveTime(uint8_t *ptr, ZuTime v)
{
  saveSigned<int64_t, uint64_t>(ptr, v.sec());
  saveSigned<int32_t, uint32_t>(ptr + sizeof(uint64_t), v.nsec());
}

bool loadTime(ZuBSpan data, ZuTime &v)
{
  if (data.length() != sizeof(TimeData)) return false;
  v = loadTime_(data.data());
  return true;
}

ZuTime loadTime_(const uint8_t *ptr)
{
  return ZuTime{
    loadSigned_<int64_t, uint64_t>(ptr),
    loadSigned_<int32_t, uint32_t>(ptr + sizeof(uint64_t))};
}

void saveDateTime(uint8_t *ptr, ZuDateTime v)
{
  saveSigned<int32_t, uint32_t>(ptr, v.julian());
  saveSigned<int32_t, uint32_t>(ptr + sizeof(uint32_t), v.sec());
  saveSigned<int32_t, uint32_t>(
    ptr + 2 * sizeof(uint32_t), v.nsec());
}

bool loadDateTime(ZuBSpan data, ZuDateTime &v)
{
  if (data.length() != sizeof(DateTimeData)) return false;
  v = loadDateTime_(data.data());
  return true;
}

ZuDateTime loadDateTime_(const uint8_t *ptr)
{
  int32_t julian = loadSigned_<int32_t, uint32_t>(ptr);
  int32_t sec = loadSigned_<int32_t, uint32_t>(
    ptr + sizeof(uint32_t));
  int32_t nsec = loadSigned_<int32_t, uint32_t>(
    ptr + 2 * sizeof(uint32_t));
  return ZuDateTime{ZuDateTime::Julian{julian}, sec, nsec};
}

void saveDecimal(uint8_t *ptr, ZuDecimal v) { saveS128(ptr, v.value); }

bool loadDecimal(ZuBSpan data, ZuDecimal &v)
{
  if (data.length() != sizeof(int128_t)) return false;
  v = loadDecimal_(data.data());
  return true;
}

ZuDecimal loadDecimal_(const uint8_t *ptr) {
  return ZuDecimal{ZuDecimal::Unscaled{loadS128_(ptr)}};
}

void saveFixed(uint8_t *ptr, ZuFixed v)
{
  saveDecimal(ptr, *v ? v.decimal() : ZuDecimal{});
}

bool loadFixed(ZuBSpan data, ZuFixed &v)
{
  if (data.length() != sizeof(int128_t)) return false;
  v = loadFixed_(data.data());
  return true;
}

ZuFixed loadFixed_(const uint8_t *ptr) {
  ZuDecimal decimal = loadDecimal_(ptr);
  return decimal.value == ZuDecimal::null() ? ZuFixed{} : ZuFixed{decimal};
}

} // ZdbSL
