//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z library intrinsics handling

#ifndef ZuIntrin_HH
#define ZuIntrin_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuInt.hh>

// --- clz

// Hacker's Delight code
// - used when intrinsics are unavailable
// - also used for compile-time constant evaluation

ZuInline constexpr uint32_t Zu_popcnt8(uint8_t v) {
  v = v - ((v>>1) & 0x55);
  v = ((v>>2) & 0x33) + (v & 0x33);
  v += (v>>4);
  return v & 0xf;
}

ZuInline constexpr uint8_t Zu_clz8_(uint8_t v) {
  v |= (v>>1);
  v |= (v>>2);
  v |= (v>>4);
  return 8 - Zu_popcnt8(v);
}
ZuInline constexpr uint8_t Zu_ctz8_(uint8_t v) {
  return Zu_popcnt8((v & -v) - 1);
}

#ifdef __GNUC__
#define Zu_clz8(v) (ZuConstEval() ? Zu_clz8_(v) : (__builtin_clz(v) - 24))
#define Zu_ctz8(v) (ZuConstEval() ? Zu_ctz8_(v) : __builtin_ctz(v))
#else
#define Zu_clz8(v) Zu_clz8_(v)
#define Zu_ctz8(v) Zu_ctz8_(v)
#endif

ZuInline constexpr uint32_t Zu_popcnt16(uint16_t v) {
  v = v - ((v>>1) & 0x5555);
  v = ((v>>2) & 0x3333) + (v & 0x3333);
  v = ((v>>4) + v) & 0x0f0f;
  v += (v>>8);
  return v & 0xff;
}

ZuInline constexpr uint16_t Zu_clz16_(uint16_t v) {
  v |= (v>>1);
  v |= (v>>2);
  v |= (v>>4);
  v |= (v>>8);
  return 16 - Zu_popcnt16(v);
}

ZuInline constexpr uint16_t Zu_ctz16_(uint16_t v) {
  return Zu_popcnt16((v & -v) - 1);
}

#ifdef __GNUC__
#define Zu_clz16(v) (ZuConstEval() ? Zu_clz16_(v) : (__builtin_clz(v) - 16))
#define Zu_ctz16(v) (ZuConstEval() ? Zu_ctz16_(v) : __builtin_ctz(v))
#else
#define Zu_clz16(v) Zu_clz16_(v)
#define Zu_ctz16(v) Zu_ctz16_(v)
#endif

ZuInline constexpr uint32_t Zu_popcnt32(uint32_t v) {
  v -= ((v>>1) & 0x55555555);
  v = ((v>>2) & 0x33333333) + (v & 0x33333333);
  v = ((v>>4) + v) & 0x0f0f0f0f;
  v += (v>>8);
  v += (v>>16);
  return v & 0x3f;
}

ZuInline constexpr uint32_t Zu_clz32_(uint32_t v) {
  v |= (v>>1);
  v |= (v>>2);
  v |= (v>>4);
  v |= (v>>8);
  v |= (v>>16);
  return 32 - Zu_popcnt32(v);
}

ZuInline constexpr uint32_t Zu_ctz32_(uint32_t v) {
  return Zu_popcnt32((v & -v) - 1);
}

#ifdef __GNUC__
#define Zu_clz32(v) (ZuConstEval() ? Zu_clz32_(v) : __builtin_clz(v))
#define Zu_ctz32(v) (ZuConstEval() ? Zu_ctz32_(v) : __builtin_ctz(v))
#else
#define Zu_clz32(v) Zu_clz32_(v)
#define Zu_ctz32(v) Zu_ctz32_(v)
#endif

ZuInline constexpr uint64_t Zu_clz64_(uint64_t v) {
  return (v>>32) ? Zu_clz32(v>>32) : Zu_clz32(v) + 32;
}

ZuInline constexpr uint64_t Zu_ctz64_(uint64_t v) {
  return uint32_t(v) ? Zu_ctz32(v) : Zu_ctz32(v>>32) + 32;
}

#ifdef __GNUC__
#define Zu_clz64(v) (ZuConstEval() ? Zu_clz64_(v) : __builtin_clzll(v))
#define Zu_ctz64(v) (ZuConstEval() ? Zu_ctz64_(v) : __builtin_ctzll(v))
#else
#define Zu_clz64(v) Zu_clz64_(v)
#define Zu_ctz64(v) Zu_ctz64_(v)
#endif

ZuInline constexpr uint64_t Zu_clz128_(uint128_t v) {
  return (v>>64) ? Zu_clz64(v>>64) : Zu_clz64(v) + 64;
}

ZuInline constexpr uint64_t Zu_ctz128_(uint128_t v) {
  return uint64_t(v) ? Zu_ctz64(v) : Zu_ctz64(v>>64) + 64;
}

#define Zu_clz128(v) Zu_clz128_(v)
#define Zu_ctz128(v) Zu_ctz128_(v)

// --- bswap (16, 32, 64 and 128 bit)

// first choice: gcc/clang intrinsic
#ifdef __GNUC__
#define Zu_bswap16(x) __builtin_bswap16(x)
#define Zu_bswap32(x) __builtin_bswap32(x)
#define Zu_bswap64(x) __builtin_bswap64(x)
#ifndef __llvm__
#define Zu_bswap128(x) __builtin_bswap128(x)
#endif
#endif

// second choice: C code
#ifndef Zu_bswap16
ZuInline uint16_t Zu_bswap16_(uint16_t v) {
  return (v<<8) | (v>>8);
}
#define Zu_bswap16(v) Zu_bswap16_(v)
#endif
#ifndef Zu_bswap32
ZuInline uint32_t Zu_bswap32_(uint32_t v) {
  return
    ((uint32_t)(Zu_bswap16(v))<<16) |
    (uint32_t)Zu_bswap16(v>>16);
}
#define Zu_bswap32(v) Zu_bswap32_(v)
#endif
#ifndef Zu_bswap64
ZuInline uint64_t Zu_bswap64_(uint64_t v) {
  return
    ((uint64_t)(Zu_bswap32(v))<<32) |
    (uint64_t)Zu_bswap32(v>>32);
}
#define Zu_bswap64(v) Zu_bswap64_(v)
#endif
#ifndef Zu_bswap128
ZuInline uint128_t Zu_bswap128_(uint128_t v) {
  return
    ((uint128_t)(Zu_bswap64(v))<<64) |
    (uint128_t)Zu_bswap64(v>>64);
}
#define Zu_bswap128(v) Zu_bswap128_(v)
#endif

#ifdef __GNUC__
#define Zu_add(l, r, o) __builtin_add_overflow(l, r, o)
#define Zu_sub(l, r, o) __builtin_sub_overflow(l, r, o)
#define Zu_mul(l, r, o) __builtin_mul_overflow(l, r, o)
#endif

#ifndef Zu_add
#error "Broken platform - need integer overflow intrinsics"
#endif

// due to MSVC's continuing lack of 128bit type support, no
// attempt is made to support MSVC here; MSVC did finally add
// full-spectrum integer overflow intrinsics in 2023 -
// see unused reference code in ZuMSVCIntrin.cc

#ifdef __GNUC__
#define Zu_nanf() __builtin_nanf("0")
#define Zu_nan() __builtin_nan("0")
#define Zu_nanl() __builtin_nanl("0")
#define Zu_isnan(v) __builtin_isnan(v)
#endif

#ifndef Zu_nan
#error "Broken platform - need NaN generators"
#endif

namespace ZuIntrin {

// clz
template <typename T, ZuIfT<sizeof(T) == 1, int> = 0>
ZuInline constexpr unsigned clz(T v) { return Zu_clz8(v); }
template <typename T, ZuIfT<sizeof(T) == 2, int> = 0>
ZuInline constexpr unsigned clz(T v) { return Zu_clz16(v); }
template <typename T, ZuIfT<sizeof(T) == 4, int> = 0>
ZuInline constexpr unsigned clz(T v) { return Zu_clz32(v); }
template <typename T, ZuIfT<sizeof(T) == 8, int> = 0>
ZuInline constexpr unsigned clz(T v) { return Zu_clz64(v); }
template <typename T, ZuIfT<sizeof(T) == 16, int> = 0>
ZuInline constexpr unsigned clz(T v) { return Zu_clz128(v); }

// ctz
template <typename T, ZuIfT<sizeof(T) == 1, int> = 0>
ZuInline constexpr unsigned ctz(T v) { return Zu_ctz8(v); }
template <typename T, ZuIfT<sizeof(T) == 2, int> = 0>
ZuInline constexpr unsigned ctz(T v) { return Zu_ctz16(v); }
template <typename T, ZuIfT<sizeof(T) == 4, int> = 0>
ZuInline constexpr unsigned ctz(T v) { return Zu_ctz32(v); }
template <typename T, ZuIfT<sizeof(T) == 8, int> = 0>
ZuInline constexpr unsigned ctz(T v) { return Zu_ctz64(v); }
template <typename T, ZuIfT<sizeof(T) == 16, int> = 0>
ZuInline constexpr unsigned ctz(T v) { return Zu_ctz128(v); }

// bswap
template <typename T, ZuIfT<sizeof(T) == 2, int> = 0>
ZuInline constexpr T bswap(T v) { return T(Zu_bswap16(v)); }
template <typename T, ZuIfT<sizeof(T) == 4, int> = 0>
ZuInline constexpr T bswap(T v) { return T(Zu_bswap32(v)); }
template <typename T, ZuIfT<sizeof(T) == 8, int> = 0>
ZuInline constexpr T bswap(T v) { return T(Zu_bswap64(v)); }
template <typename T, ZuIfT<sizeof(T) == 16, int> = 0>
ZuInline constexpr T bswap(T v) { return T(Zu_bswap128(v)); }

// integer overflow
template <typename L, typename R, typename O>
ZuInline constexpr bool add(L l, R r, O *o) { return Zu_add(l, r, o); }
template <typename L, typename R, typename O>
ZuInline constexpr bool sub(L l, R r, O *o) { return Zu_sub(l, r, o); }
template <typename L, typename R, typename O>
ZuInline constexpr bool mul(L l, R r, O *o) { return Zu_mul(l, r, o); }

// NaN generators
template <typename T, ZuSame<float, T, int> = 0>
ZuInline constexpr T nan() { return Zu_nanf(); }
template <typename T, ZuSame<double, T, int> = 0>
ZuInline constexpr T nan() { return Zu_nan(); }
template <typename T, ZuSame<long double, T, int> = 0>
ZuInline constexpr T nan() { return Zu_nanl(); }

// NaN tests
template <typename T, ZuSame<float, T, int> = 0>
ZuInline constexpr bool isnan(T v) { return Zu_isnan(v); }
template <typename T, ZuSame<double, T, int> = 0>
ZuInline constexpr bool isnan(T v) { return Zu_isnan(v); }
template <typename T, ZuSame<long double, T, int> = 0>
ZuInline constexpr bool isnan(T v) { return Zu_isnan(v); }

// compile-time binary log (rounded up)
// - used for statically sizing hash tables
template <typename T>
constexpr unsigned log2(T v) {
  return v <= 1 ? 1 : ((sizeof(v)<<3) - ZuIntrin::clz(v - 1));
}

} // ZuIntrin

#endif /* ZuIntrin_HH */
