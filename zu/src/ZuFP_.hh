//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// IEEE floating point type traits

#ifndef ZuFP__HH
#define ZuFP__HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <math.h>
#include <float.h>

#ifdef _MSC_VER
#ifndef INFINITY
#define INFINITY HUGE_VAL
#endif
#define isnan _isnan
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuMostAligned.hh>

template <unsigned Size> struct ZuFPType_;
template <> struct ZuFPType_<sizeof(float)> { using T = float; };
template <> struct ZuFPType_<sizeof(double)> { using T = double; };
template <> struct ZuFPType_<sizeof(long double)> { using T = long double; };
template <unsigned Size> using ZuFPType = typename ZuFPType_<Size>::T;

template <typename T, unsigned Size = sizeof(T)> struct ZuFP;

template <typename T> struct ZuFP__;
template <> struct ZuFP__<float> {
  ZuInline static float floor_(float f) { return floorf(f); }
  ZuInline static float log10_(float f) { return log10f(f); }
  ZuInline static float frexp_(float f, int *n) { return frexpf(f, n); }
  ZuInline static float ldexp_(float f, int n) { return ldexpf(f, n); }
  ZuInline static float fabs_(float f) { return fabsf(f); }
};
template <> struct ZuFP__<double> {
  ZuInline static double floor_(double f) { return floor(f); }
  ZuInline static double log10_(double f) { return log10(f); }
  ZuInline static double frexp_(double f, int *n) { return frexp(f, n); }
  ZuInline static double ldexp_(double f, int n) { return ldexp(f, n); }
  ZuInline static double fabs_(double f) { return fabs(f); }
};
template <> struct ZuFP__<long double> {
  ZuInline static long double floor_(long double f) { return floorl(f); }
  ZuInline static long double log10_(long double f) { return log10l(f); }
  ZuInline static long double frexp_(long double f, int *n)
    { return frexpl(f, n); }
  ZuInline static long double ldexp_(long double f, int n)
    { return ldexpl(f, n); }
  ZuInline static long double fabs_(long double f) { return fabsl(f); }
};

namespace Zu_ { template <typename ...> class Tuple; }

// CRTP mixin
template <typename FP, typename T, typename I>
struct ZuFP_ : public ZuFP__<T> {
  ZuInline static constexpr T inf() { return T(INFINITY); }
  ZuInline static constexpr bool inf(T v) { return v == inf(); }

  // calculate decimal epsilon
  // - epsilon here is the decimal precision limit of a floating point number
  //   expressed as a quantum (q below)
  // - a =~ q is equivalent to a >= (b - q) && a <= (b + q) 
  // - q can also be used as a cutoff threshold for the last decimal place
  // - NaN, +infinity, -infinity and zero are returned as-is
  static T epsilon(T v_) {
    // before doing anything else deal with NaN, +/- Inf, and 0
    if (ZuUnlikely(FP::nan(v_))) return v_;
    if (v_ < 0) v_ = -v_;
    return epsilon_(v_);
  }
  // use epsilon_() if v is known to be not NaN and positive
  static T epsilon_(T v_) {
    if (ZuUnlikely(FP::inf(v_))) return v_;
    if (v_ == T(0)) return v_;
    if constexpr (sizeof(T) <= 8) {
      ZuPun<T, I> pun(v_);
      I i = pun.out;			// intentional aliasing
      I m = ~((~I(0))<<FP::Bits);	// mantissa mask
      I p = i & m;			// previous mantissa
      I n = (p + 5) & m;		// mantissa + 5 (i.e. half of 10)
      if (n < p)			// mantissa overflow
	pun.out = ((i & ~m) + (I(3)<<(FP::Bits - 1))) | (n>>1);
      else
	pun.out = (i & ~m) | n;
      return pun.in - v_;
    } else {
      // long double version
#pragma pack(push, 1)
      struct I_ {
	uint64_t m; // mantissa
	uint16_t e; // exponent - actually 15bits, sign bit is MSB
	ZuInline void inc() {
	  uint64_t p = m;
	  if (ZuUnlikely((m += 5) < p)) {
	    m = (m | (uint64_t(1)<<63)) | (m>>1);
	    ++e;
	  }
	}
      };
#pragma pack(pop)
      ZuPun<T, I_> pun(v_);
      pun.out.inc();
      return pun.in - v_;
    }
  }

  // decode() returns {exponent, mantissa}
  // - NaN, +infinity and -infinity are returned via sentinel values
  // - mantissa LSB will always be 1 unless value is zero {0, 0}
  using Decode = Zu_::Tuple<I, I>;
  // sentinel return values from decode()
  static constexpr Decode decodeNaN();
  static constexpr Decode decodePosInf();
  static constexpr Decode decodeNegInf();
  static Decode decode(T);
  static T encode(I, I);
};

// this is included for completeness, there is no "short float" in C/C++
template <typename T>
struct ZuFP<T, 2U> : public ZuFP_<ZuFP<T, 2U>, T, int16_t> { // 10+5
  using I = int16_t;
  using U = uint16_t;
  enum { Bits = 10, MinDigits = 4, MaxDigits = 4 };
  ZuInline static T nan() {
    if (ZuConstEval()) {
      return Zu_nanf();
    } else {
      return ZuPun<U, T>(~U(0)).out;
    }
  }
  ZuInline static constexpr bool nan(T v) {
    return ZuIntrin::isnan<float>(v);
  }
};
template <typename T>
struct ZuFP<T, 4U> : public ZuFP_<ZuFP<T, 4U>, T, int32_t> { // 23+8
  using I = int32_t;
  using U = uint32_t;
  enum { Bits = 23, MinDigits = 7, MaxDigits = 8 };
  ZuInline static constexpr T nan() {
    return ZuIntrin::nan<float>();
  }
  ZuInline static constexpr bool nan(T v) {
    return ZuIntrin::isnan<float>(v);
  }
};
template <typename T>
struct ZuFP<T, 8U> : public ZuFP_<ZuFP<T, 8U>, T, int64_t> { // 52+11
  using I = int64_t;
  using U = uint64_t;
  enum { Bits = 52, MinDigits = 16, MaxDigits = 16 };
  ZuInline static constexpr T nan() { return ZuIntrin::nan<double>(); }
  ZuInline static constexpr bool nan(T v) {
    return ZuIntrin::isnan<double>(v);
  }
};
template <typename ZuFP, typename T>
struct ZuFP_64 : public ZuFP_<ZuFP, T, int64_t> { // 64+15
  using I = int64_t;
  using U = uint64_t;
  enum { Bits = 64, MinDigits = 20, MaxDigits = 20 };
};
template <typename T>
struct ZuFP<T, 12U> : public ZuFP_64<ZuFP<T, 12U>, T> { // 64+15 (12 bytes)
  ZuInline static constexpr T nan() { return ZuIntrin::nan<long double>(); }
  ZuInline static constexpr bool nan(T v) {
    return ZuIntrin::isnan<long double>(v);
  }
};
template <typename T>
struct ZuFP<T, 16U> : public ZuFP_64<ZuFP<T, 16U>, T> { // 64+15 (16 bytes)
  ZuInline static constexpr T nan() { return ZuIntrin::nan<long double>(); }
  ZuInline static constexpr bool nan(T v) {
    return ZuIntrin::isnan<long double>(v);
  }
};

#endif /* ZuFP__HH */
