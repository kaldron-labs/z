//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// IEEE floating point type traits

#ifndef ZuFP_HH
#define ZuFP_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuFP_.hh>
#include <zlib/ZuTuple.hh>

// floating point support for all IEEE 754 sizes
// - x86, x86_64, ARM/Android and ARM/MacOSX are all IEEE754 little-endian
// - number of decimal places in each type's mantissa
// - NaN and infinity generators for each type
// - epsilon function for each type

// ZuFP<T>
//
// Bits - number of bits in mantissa
// MinDigits - minimum number of decimal significant figures in mantissa
// MaxDigits - maximum ''
// T inf() - return positive infinity (use -inf() for negative infinity)
// bool inf(T v) - true if v is positive infinite
// T nan() - NaN ("not a number" - null sentinel value)
// bool nan(T v) - true if v is not a number
// T epsilon(T v) - return decimal epsilon of v
//   (this is the worst case range within which values would compare equal
//    if converted into decimal and back again)
// {exponent, mantissa} decode(T v) - decompose v into exponent and mantissa

// sentinel return values from decode()
template <typename FP, typename T, typename I>
constexpr ZuTuple<I, I> ZuFP_<FP, T, I>::decodeNaN() {
  using U = typename FP::U;
  enum { IBits = sizeof(U)<<3 };
  return { U(1)<<(IBits - 1), 0 };
}
template <typename FP, typename T, typename I>
constexpr ZuTuple<I, I> ZuFP_<FP, T, I>::decodePosInf() {
  using U = typename FP::U;
  enum { IBits = sizeof(U)<<3 };
  return { ~(U(1)<<(IBits - 1)), ~(U(1)<<(IBits - 1)) };
}
template <typename FP, typename T, typename I>
constexpr ZuTuple<I, I> ZuFP_<FP, T, I>::decodeNegInf() {
  using U = typename FP::U;
  enum { IBits = sizeof(U)<<3 };
  return { ~(U(1)<<(IBits - 1)), U(1)<<(IBits - 1) };
}
// decode floating-point into integer exponent and mantissa
// - extracts directly from the native binary format
// - no FP math operations are performed
template <typename FP, typename T, typename I>
inline ZuTuple<I, I> ZuFP_<FP, T, I>::decode(T v_)
{
  // there are 4 special cases, handled with sentinel values up front
  // - nan, -inf, +inf and zero
  using U = typename FP::U;
  if constexpr (sizeof(T) <= 8) {
    // IEEE754 version
    enum { FBits = sizeof(T)<<3 };
    enum { EBits = FBits - FP::Bits - 1 };
    U u = ZuPun<T, U>(v_).out;
    bool negative = u & (U(1)<<(FBits - 1));	// sign bit
    if (negative) u &= ~(U(1)<<(FBits - 1));
    if (!u) { return {0, 0}; }			// zero
    I e = u;
    e = (e>>FP::Bits);				// shift down exponent
    if (e == ~(~U(0)<<EBits)) {			// nan or inf
      if (u & ~(~U(0)<<FP::Bits)) return decodeNaN();
      return negative ? decodeNegInf() : decodePosInf();
    }
    // from here, value is normal - extract and normalize exponent and mantissa
    e -= ((U(1)<<(EBits - 1)) - 1);	// apply exponent bias
    e -= FP::Bits;			// adjust for mantissa bits
    u = (u & ~((~U(0))<<FP::Bits)) | (U(1)<<FP::Bits); // mantissa hidden bit
    // normalize exponent and mantissa
    unsigned tz = ZuIntrin::ctz(u);
    u >>= tz;				// shift down mantissa
    e += tz;				// adjust exponent for mantissa shift
    I i = u;
    if (negative) i = -i;		// apply sign to mantissa
    return {e, i};
  } else {
    // Intel 8087 80bit extended precision version (long double)
    // - the extended precision mantissa has no hidden bit
    enum { EBits = 15 };
#pragma pack(push, 1)
    struct V {
      uint64_t m; // mantissa
      uint16_t e; // exponent - actually 15bits, sign bit is MSB
    };
#pragma pack(pop)
    ZuPun<T, V> pun(v_);
    U u = pun.out.m;
    I e = pun.out.e;
    bool negative = e < 0;
    if (negative) e &= ~(I(1)<<EBits);
    if (!e && !u) { return {0, 0}; }			// zero
    if (e == ~(~U(0)<<EBits)) {				// nan or inf
      if (u) return decodeNaN();
      return negative ? decodeNegInf() : decodePosInf();
    }
    // from here, value is normal - extract and normalize exponent and mantissa
    e -= ((U(1)<<(EBits - 1)) - 1);	// apply exponent bias
    e -= (FP::Bits - 1);		// adjust for mantissa bits
    // normalize exponent and mantissa
    unsigned tz = ZuIntrin::ctz(u);
    // the 8087 80-bit extended precision has a signed mantissa that is
    // effectively 65 bits including the sign bit, so regrettably the
    // occasional bit of precision is lost here in order to decode into a
    // signed int64_t return value; this only occurs when the mantissa is
    // completely full of significant bits, i.e. the LSB is non-zero
    if (!tz) tz = 1;
    u >>= tz;				// shift down mantissa
    e += tz;				// adjust exponent for mantissa shift
    I i = u;
    if (negative) i = -i;		// apply sign to mantissa
    return {e, i};
  }
}

// reverse a decode()
template <typename FP, typename T, typename I>
inline T ZuFP_<FP, T, I>::encode(I e, I m)
{
  // there are 4 special cases, handled with sentinel values up front
  // - nan, -inf, +inf and zero
  using U = typename FP::U;
  enum { IBits = sizeof(U)<<3 };
  if constexpr (sizeof(T) <= 8) {
    // IEEE754 version
    enum { FBits = sizeof(T)<<3 };
    enum { EBits = FBits - FP::Bits - 1 };
    if (!e && !m) return 0;
    if (ZuUnlikely(e == (U(1)<<(IBits - 1)) && !m)) return FP::nan();
    if (ZuUnlikely(e == ~(U(1)<<(IBits - 1)))) {
      if (ZuUnlikely(m == ~(U(1)<<(IBits - 1)))) {
	return FP::inf();
      } else if (ZuUnlikely(m == U(1)<<(IBits - 1))) {
	return -FP::inf();
      }
    }
    bool negative = m < 0;
    // do not be tempted to move this up (int(-INT_MIN) == int(INT_MIN))
    if (negative) m = -m;
    // from here, value is normal - need to encode exponent and mantissa
    U u = m;					// begin with mantissa
    unsigned lz = ZuIntrin::clz(u);		// leading zero-bits
    u <<= (lz + 1);				// shift mantissa per MSB
    u >>= (IBits - FP::Bits);			// shift mantissa down
    e -= (lz + 1);				// adjust for mantissa shift
    e += IBits;					// adjust for mantissa bits
    e += ((U(1)<<(EBits - 1)) - 1);		// apply exponent bias
    u |= (e<<FP::Bits);				// set exponent
    if (negative) u |= (U(1)<<(FBits - 1));	// set sign bit
    return ZuPun<U, T>(u).out;
  } else {
    // Intel 8087 80bit extended precision version (long double)
    // - the extended precision mantissa has no hidden bit
    enum { EBits = 15 };
    if (!e && !m) return 0;
    bool negative = m < 0;
    if (negative) m = -m;
    if (ZuUnlikely(e == (U(1)<<(IBits - 1)) && !m)) return FP::nan();
    if (ZuUnlikely(e == ~(U(1)<<(IBits - 1)) && m == ~(U(1)<<(IBits - 1))))
      return negative ? -FP::inf() : FP::inf();
    // from here, value is normal - need to encode exponent and mantissa
    uint64_t u = m;				// begin with mantissa
    unsigned lz = ZuIntrin::clz(u);		// leading zero-bits
    u <<= lz;					// shift mantissa up per MSB
    e -= lz;					// adjust for mantissa shift
    e += 63;					// adjust for mantissa bits
    e += ((U(1)<<(EBits - 1)) - 1);		// apply exponent bias
    if (negative) e |= (U(1)<<EBits);		// set sign bit
#pragma pack(push, 1)
    struct V {
      uint64_t m; // mantissa
      uint16_t e; // exponent - actually 15bits, sign bit is MSB
    };
#pragma pack(pop)
    ZuPun<V, T> pun;
    pun.in.m = u;
    pun.in.e = e;
    return pun.out;
  }
}

#endif /* ZuFP_HH */
