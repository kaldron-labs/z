//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zflatcvec_HH
#define zflatcvec_HH

// Canonical scalar/value vector fixtures; string/byte vectors are separate.
#define FlatcVecs(X) \
  X(int8_t, Int8, i8s, -128) \
  X(uint8_t, UInt8, u8s, 255) \
  X(int16_t, Int16, i16s, -32768) \
  X(uint16_t, UInt16, u16s, 65535) \
  X(int32_t, Int32, i32s, INT32_MIN) \
  X(uint32_t, UInt32, u32s, UINT32_MAX) \
  X(int64_t, Int64, i64s, INT64_MIN) \
  X(uint64_t, UInt64, u64s, UINT64_MAX) \
  X(int128_t, Int128, i128s, -123) \
  X(uint128_t, UInt128, u128s, (uint128_t(1) << 100)) \
  X(float, Float, f32s, 0x1p-149f) \
  X(double, Float, f64s, 0x1p-1074) \
  X(ZuFixed, Fixed, fixeds, 123.25) \
  X(ZuDecimal, Decimal, decimals, "456.125") \
  X(ZuTime, Time, times, (ZuTime{123, 456})) \
  X(ZuDateTime, DateTime, dates, (ZuDateTime{ZuDateTime::Julian{2461000}, 123, 456}))

template <typename V>
void flatcFill(V &v)
{
#define FlatcFill(T, C, M, Val) v.M.push(T{Val});
  FlatcVecs(FlatcFill)
#undef FlatcFill
}

template <typename V>
bool flatcVecs(const V &v)
{
#define FlatcCheck(T, C, M, Val) if (v.M.length() != 1 || v.M[0] != T{Val}) return false;
  FlatcVecs(FlatcCheck)
#undef FlatcCheck
  return true;
}

#endif // zflatcvec_HH
