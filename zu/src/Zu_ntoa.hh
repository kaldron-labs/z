//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// optimized integer and floating point printing, with both static
// compile-time formatting and variable run-time formatting
//
// simple usage with default formatting (no justification, no padding):
//
// double v = ...
// char buf[Zu_flen<double>() + 1];	// Zu_flen() is constexpr
// buf[Zu_ftoa(buf, v)] = 0;		// null terminates buf
// puts(buf);
//
// example of ZuFmt compile-time formatting (right-justified, space-padded):
//
// unsigned v = 42
// using Print = Zu_nprint<ZuFmt::Right<5, ' '> >; // specify formatting
// char buf[Print::ulen<unsigned>() + 1]; // could safely be char buf[6]
// buf[Print::utoa(v)] = 0;
// puts(buf); // prints "    42"
//
// see ZuFmt for all compile-time formatting options
//
// *len() are constexpr functions returning the buffer size required to hold
// the longest printed value given the compile-time formatting specified;
// right/left/fractional justification within a fixed width constrains the
// size returned accordingly
//
// *toa() returns the actual number of bytes printed, or 0 for overflow due
// to the value being too large to be printed within the justified width
//
// ulen/utoa - unsigned char/short/int/long/long long
// ilen/itoa - (signed) char/short/int/long/long long
// flen/ftoa - float/double/long double
//
// integer performance is worse than a canonical textbook itoa() unless
// compiler optimization is enabled; floating point performance is
// very significantly faster, regardless of optimization
//
// floating point values are printed in %f format; fallback to %e
// format (aka "scientific notation") is intentionally unimplemented
//
// this library is primarily intended for fast printing of values with up
// to 20 significant digits (i.e. 64 bits), with exponent range +20 to -20
//
// relative performance without/with compiler optimization:
// integer - Zu_ntoa vs canonical itoa(): .69x (-31%) / 1.07x (+7%)
// floating point - Zu_ntoa vs sprintf("%f"): 5x / 22x (!)

#ifndef Zu_ntoa_HH
#define Zu_ntoa_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <string.h>

#include <zlib/ZuInt.hh>
#include <zlib/ZuFP.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuDecimalFn.hh>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wenum-compare"
#pragma GCC diagnostic ignored "-Wint-in-bool-context"
#endif

namespace Zu_ntoa {
  using namespace ZuFmt;

  template <typename T_, unsigned Size = sizeof(T_)> struct Unsigned_;
  template <typename T_> struct Unsigned_<T_, 1> { using T = uint8_t; };
  template <typename T_> struct Unsigned_<T_, 2> { using T = uint16_t; };
  template <typename T_> struct Unsigned_<T_, 4> { using T = uint32_t; };
  template <typename T_> struct Unsigned_<T_, 8> { using T = uint64_t; };
  template <typename T_> struct Unsigned_<T_, 16> { using T = uint128_t; };
  template <typename T> using Unsigned = typename Unsigned_<T>::T;

  template <typename T, typename R = void>
  using Is128Bit = ZuIfT<(sizeof(T) > 8) && ZuTraits<T>::IsIntegral, R>;
  template <typename T, typename R = void>
  using Is64Bit = ZuIfT<(sizeof(T) <= 8) && ZuTraits<T>::IsIntegral, R>;

  // unsigned integers - maximum log10 for each size of integer
  template <unsigned Size> struct Log10_MaxLog;
  template <> struct Log10_MaxLog<1U> { enum { N = 3 }; };
  template <> struct Log10_MaxLog<2U> { enum { N = 5 }; };
  template <> struct Log10_MaxLog<4U> { enum { N = 10 }; };
  template <> struct Log10_MaxLog<8U> { enum { N = 20 }; };
  template <> struct Log10_MaxLog<16U> { enum { N = 39 }; };

  // unsigned integers - maximum log16 for each size of integer
  template <unsigned Size>
  struct Log16_MaxLog { enum { N = (Size<<1U) }; };

  // generic calculation of maximum logN of a printed integer type
  template <unsigned Size, bool Hex>
  struct LogN_MaxLog : public Log10_MaxLog<Size> { };
  template <unsigned Size>
  struct LogN_MaxLog<Size, 1> : public Log16_MaxLog<Size> { };

  // return log10(v) using binary clz, with a floor of 1 decimal digit
  template <unsigned Size> struct Log10;
  template <> struct Log10<1> {
    ZuInline static unsigned log(unsigned v) {
      return ZuLikely(v < 10U) ? 1U : ZuLikely(v < 100U) ? 2U : 3U;
    }
  };
  template <> struct Log10<4> {
    static unsigned log(unsigned v) {
      static constexpr uint8_t clz10[] = {
	20, 20, 19, 18, 18, 17, 16, 16, 15, 14,
	14, 14, 13, 12, 12, 11, 10, 10,  9,  8,
	 8,  8,  7,  6,  6,  5,  4,  4,  3,  2,
	 2,  2
      };
      unsigned l;
      if (ZuUnlikely(!v))
	l = 1U;
      else {
	unsigned n = clz10[ZuIntrin::clz(v)];
	if (ZuLikely(!(n & 1U)))
	  l = n>>1U;
	else {
	  n >>= 1U;
	  l = n + (v >= ZuDecimalFn::pow10_32(n));
	}
      }
      return l;
    }
  };
  template <> struct Log10<2> : public Log10<4> { };
  template <> struct Log10<8> {
    static unsigned log(uint64_t v) {
      static constexpr uint8_t clz10[] = {
	39, 38, 38, 38, 37, 36, 36, 35, 34, 34,
	33, 32, 32, 32, 31, 30, 30, 29, 28, 28,
	27, 26, 26, 26, 25, 24, 24, 23, 22, 22,
	21, 20, 20, 20, 19, 18, 18, 17, 16, 16,
	15, 14, 14, 14, 13, 12, 12, 11, 10, 10,
	 9,  8,  8,  8,  7,  6,  6,  5,  4,  4,
	 3,  2,  2,  2
      };
      unsigned l;
      if (ZuUnlikely(!v))
	l = 1U;
      else {
	unsigned n = clz10[ZuIntrin::clz(v)];
	if (ZuLikely(!(n & 1U)))
	  l = n>>1U;
	else {
	  n >>= 1U;
	  l = n + (v >= ZuDecimalFn::pow10_64(n));
	}
      }
      return l;
    }
  };
  template <> struct Log10<16> {
    static unsigned log(uint128_t v) {
      constexpr uint128_t f = static_cast<uint128_t>(10000000000000000000ULL);
      unsigned l;
      if (ZuLikely(v < f))
	l = Log10<8>::log(v);
      else {
	v /= f;
	if (ZuLikely(v < f))
	  l = Log10<8>::log(v) + 19U;
	else {
	  v /= f;
	  l = Log10<4>::log(v) + 38U;
	}
      }
      return l;
    }
  };

  // returns the number of decimal digits in the integer and fractional
  // portions of a floating point number, given:
  // Bits - the number of bits in the FP mantissa (compile-time constant)
  // v - the integer portion of the number
  // return value is ((integer digits)<<8) | (fractional digits)
  struct Log10FP {
    template <unsigned Bits>
    static unsigned log(uint64_t v) {
      static constexpr uint8_t clz10[] = {
	 2,  2,  2,  3,  4,  4,  5,  6,  6,  7,
	 8,  8,  8,  9, 10, 10, 11, 12, 12, 13,
	14, 14, 14, 15, 16, 16, 17, 18, 18, 19,
	20, 20, 20, 21, 22, 22, 23, 24, 24, 25,
	26, 26, 26, 27, 28, 28, 29, 30, 30, 31,
	32, 32, 32, 33, 34, 34, 35, 36, 36, 37,
	38, 38, 38, 39, 40
      };
      unsigned digits = clz10[Bits];
      digits = (digits>>1U) + (digits & 1U);
      unsigned l;
      if (ZuLikely(v)) {
	unsigned f = 64U - ZuIntrin::clz(v);
	unsigned i = clz10[f - 1];
	i = (i>>1U) +
	  ((ZuUnlikely(i & 1U)) && (v >= ZuDecimalFn::pow10_64(i>>1U)));
	if (ZuLikely(f < Bits)) {
	  f = clz10[(Bits - f) - 1];
	  f = (f>>1U) + (f & 1U);
	} else
	  f = 0U;
	if (ZuUnlikely((i + f) > digits))
	  f = (ZuUnlikely(i > digits) ? 0U : digits - i);
	l = (i<<8U) | f;
      } else {
	l = (1U<<8U) | (digits - 1U);
      }
      return l;
    }
  };

  // the below code carefully defends against a number of obscure pitfalls
  template <typename T>
  ZuInline uint64_t frac(T v, uint64_t &iv, unsigned &i, unsigned f) {
    uint64_t fv = 0;
    if (ZuLikely(f)) {
      uint64_t pow10 = ZuDecimalFn::pow10_64(f);
      v = (v - (T)iv) * (T)pow10;
      if (ZuLikely(v >= (T)0.5)) {
	fv = uint64_t(v);
	if (ZuUnlikely((T)0.5 < (v - (T)fv))) ++fv;
	if (ZuUnlikely(fv >= pow10)) {
	  if (ZuUnlikely(++iv >= ZuDecimalFn::pow10_64(i))) ++i;
	  fv = 0;
	}
      }
    }
    return fv;
  }

  // decimal handling for each log10 0..20
  template <typename T>
  inline Is64Bit<T> Base10_print(T v_, unsigned n, char *buf) {
    uint64_t v = v_;
    while (ZuLikely(n)) { buf[--n] = (v % 10) + '0'; v /= 10; }
  }
  template <typename T>
  inline Is128Bit<T> Base10_print(T v_, unsigned n, char *buf) {
    uint128_t v = v_;
    if (ZuLikely(v < 10000000000000000000ULL))
      Base10_print(uint64_t(v), n, buf);
    else
      while (ZuLikely(n)) { buf[--n] = (v % 10) + '0'; v /= 10; }
  }
  template <typename T>
  inline Is64Bit<T> Base10_print_comma(
      T v_, unsigned n, char *buf, char comma) {
    if (ZuUnlikely(!n)) return;
    uint64_t v = v_;
    unsigned c = 3;
    if (ZuLikely(n))
      for (;;) {
	buf[--n] = (v % 10) + '0'; v /= 10;
	if (ZuUnlikely(!n)) break;
	if (ZuUnlikely(!--c)) { buf[--n] = comma; c = 3; }
      }
  }
  template <typename T>
  inline Is128Bit<T> Base10_print_comma(
      T v_, unsigned n, char *buf, char comma) {
    uint128_t v = v_;
    if (ZuLikely(v < 10000000000000000000ULL))
      Base10_print_comma(uint64_t(v), n, buf, comma);
    else {
      unsigned c = 3;
      if (ZuLikely(n))
	for (;;) {
	  buf[--n] = (v % 10) + '0'; v /= 10;
	  if (ZuUnlikely(!n)) break;
	  if (ZuUnlikely(!--c)) { buf[--n] = comma; c = 3; }
	}
    }
  }
  template <typename T>
  inline void Base10_print_frac(
      T v, unsigned m, unsigned n, char c, char *buf) {
    if (ZuUnlikely(n < m)) v /= ZuDecimalFn::pow10<sizeof(T)>(m - n);
    Base10_print(v, n, buf);
    if (ZuLikely(n))
      for (;;) {
	if (buf[--n] != '0') return;
	if (ZuUnlikely(n <= 1)) return;
	buf[n] = c;
      }
    // perversely the more elegant implementation below runs slower
    // (left in place as a reference)
#if 0
    unsigned d;
    while (!(d = v % 10)) {
      if (ZuUnlikely(n == 1)) break;
      buf[--n] = c;
      v /= 10;
    }
    for (;;) {
      buf[--n] = d + '0';
      if (ZuUnlikely(!n)) return;
      v /= 10;
      d = v % 10;
    }
#endif
  }
  template <typename T>
  inline unsigned Base10_print_frac_truncate(
      T v, unsigned m, unsigned n, char *buf) {
    if (ZuUnlikely(n < m)) v /= ZuDecimalFn::pow10<sizeof(T)>(m - n);
    Base10_print(v, n, buf);
    if (ZuUnlikely(!n)) return 0;
    for (;;) {
      if (buf[--n] != '0') return n + 1;
      if (ZuUnlikely(n <= 1)) return 1;
      buf[n] = 0;
    }
    // perversely the more elegant implementation below runs slower
    // (left in place as a reference)
#if 0
    unsigned d;
    while (!(d = v % 10)) {
      if (ZuUnlikely(n == 1)) break;
      buf[--n] = 0;
      v /= 10;
    }
    unsigned r = n;
    for (;;) {
      buf[--n] = d + '0';
      if (ZuUnlikely(!n)) return r;
      v /= 10;
      d = v % 10;
    }
#endif
  }
  template <char Comma> struct Base10 {
    template <typename T>
    ZuInline static void print(T v, unsigned n, char *buf) {
      Base10_print_comma(v, n, buf, Comma);
    }
  };
  template <> struct Base10<'\0'> {
    template <typename T>
    ZuInline static void print(T v, unsigned n, char *buf) {
      Base10_print(v, n, buf);
    }
  };

  // return log16(v) using binary clz, with a floor of 1 hex digit
  template <unsigned Size> struct Log16;
  template <> struct Log16<1> {
    ZuInline static unsigned log(unsigned v) {
      return ZuLikely(v < 0x10) ? 1U : 2U;
    }
  };
  template <> struct Log16<4> {
    ZuInline static unsigned log(uint32_t v) {
      return ZuUnlikely(!v) ? 1U : (35U - ZuIntrin::clz(v))>>2;
    }
  };
  template <> struct Log16<2> : public Log16<4> { };
  template <> struct Log16<8> {
    ZuInline static unsigned log(uint64_t v) {
      return ZuUnlikely(!v) ? 1U : (67U - ZuIntrin::clz(v))>>2;
    }
  };
  template <> struct Log16<16> {
    ZuInline static unsigned log(uint128_t v) {
      return ZuLikely(!(v>>64)) ? Log16<8>::log(v) : Log16<8>::log(v>>64) + 16U;
    }
  };

  // generic log(v)
  template <bool Hex, unsigned Size> struct LogN;
  template <unsigned Size> struct LogN<0, Size> : public Log10<Size> { };
  template <unsigned Size> struct LogN<1, Size> : public Log16<Size> { };

  // hexadecimal handling for each log16 0..16
  template <bool Upper>
  ZuInline static ZuIfT<Upper, char> hexDigit(unsigned v) {
    static constexpr const char digits[] = "0123456789ABCDEF";
    return digits[v];
    // return v < 10 ? v + '0' : v - 10 + 'A';
  }
  template <bool Upper>
  ZuInline static ZuIfT<!Upper, char> hexDigit(unsigned v) {
    static constexpr const char digits[] = "0123456789abcdef";
    return digits[v];
    // return v < 10 ? v + '0' : v - 10 + 'a';
  }
  template <typename T>
  Is64Bit<T> Base16_print(T v_, unsigned n, char *buf) {
    uint64_t v = v_;
    while (ZuLikely(n)) { buf[--n] = hexDigit<0>(v & 0xf); v >>= 4U; }
  }
  template <typename T>
  Is128Bit<T> Base16_print(T v_, unsigned n, char *buf) {
    uint128_t v = v_;
    if (ZuLikely(!(v>>64U)))
      Base16_print(uint64_t(v), n, buf);
    else
      while (ZuLikely(n)) { buf[--n] = hexDigit<0>(v & 0xf); v >>= 4U; }
  }
  template <typename T>
  Is64Bit<T> Base16_print_upper(
      T v_, unsigned n, char *buf) {
    uint64_t v = v_;
    while (ZuLikely(n)) { buf[--n] = hexDigit<1>(v & 0xf); v >>= 4U; }
  }
  template <typename T>
  Is128Bit<T> Base16_print_upper(
      T v_, unsigned n, char *buf) {
    uint128_t v = v_;
    if (ZuLikely(!(v>>64U)))
      Base16_print_upper(uint64_t(v), n, buf);
    else
      while (ZuLikely(n)) { buf[--n] = hexDigit<1>(v & 0xf); v >>= 4U; }
  }
  template <bool Upper> struct Base16 {
    template <typename T>
    ZuInline static void print(T v, unsigned n, char *buf) {
      Base16_print(v, n, buf);
    }
  };
  template <> struct Base16<1> {
    template <typename T>
    ZuInline static void print(T v, unsigned n, char *buf) {
      Base16_print_upper(v, n, buf);
    }
  };

  // generic invocation of either Base10 or Hex functions
  template <bool Hex, char Comma, bool Upper, bool Alt>
  struct BaseN : public Base10<Comma> { };
  template <char Comma, bool Upper>
  struct BaseN<1, Comma, Upper, 0> : public Base16<Upper> { };
  template <char Comma, bool Upper>
  struct BaseN<1, Comma, Upper, 1> : public Base16<Upper> {
    template <typename T>
    ZuInline static void print(T v, unsigned n, char *buf) {
      *buf++ = '0';
      *buf++ = 'x';
      Base16<Upper>::print(v, n - 2, buf);
    }
  };

  // length of a printed value given a run-time log (i.e. the number of digits)
  template <bool Hex, char Comma, bool Alt> struct Len;
  template <bool Alt>
  struct Len<0, '\0', Alt> {
    static constexpr unsigned len(unsigned n) { return n; }
  };
  template <char Comma, bool Alt>
  struct Len<0, Comma, Alt> {
    static constexpr unsigned len(unsigned n) { return n + ((n - 1U)/3U); }
  };
  template <char Comma>
  struct Len<1, Comma, 0> {
    static constexpr unsigned len(unsigned n) { return n; }
  };
  template <char Comma>
  struct Len<1, Comma, 1> {
    static constexpr unsigned len(unsigned n) { return 2 + n; }
  };

  // length of a printed value given a compile-time log (including padding)
  template <bool Hex, char Comma, bool Alt, int Justification, unsigned Width,
    int NDP, bool Negative>
  struct MaxLen {
    static constexpr unsigned len(unsigned) { return Width; }
  };
  template <bool Hex, char Comma, bool Alt, unsigned Width, int NDP,
    bool Negative>
  struct MaxLen<Hex, Comma, Alt, Just::Frac, Width, NDP, Negative> {
    static constexpr unsigned len(unsigned) { return NDP; }
  };
  template <bool Hex, char Comma, bool Alt, unsigned Width, int NDP,
    bool Negative>
  struct MaxLen<Hex, Comma, Alt, Just::None, Width, NDP, Negative> {
    static constexpr unsigned len(unsigned log) {
      return Negative +
	(Hex ? (Alt ? 2U : 0U) : Comma ? ((log - 1U)/3U) : 0U) + log;
    }
  };

  template <bool Hex, char Comma, bool Upper, bool Alt, bool Negative>
  struct Print_;
  template <bool Hex, char Comma, bool Upper, bool Alt>
  struct Print_<Hex, Comma, Upper, Alt, 0> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n);
      BaseN<Hex, Comma, Upper, Alt>::print(v, n, buf);
      return n;
    }
  };
  template <bool Hex, char Comma, bool Upper, bool Alt>
  struct Print_<Hex, Comma, Upper, Alt, 1> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n);
      *buf++ = '-';
      BaseN<Hex, Comma, Upper, Alt>::print(v, n, buf);
      return n + 1;
    }
  };

  template <unsigned Width, bool Truncate> struct Print_LeftLen;
  template <unsigned Width> struct Print_LeftLen<Width, 0> {
    ZuInline static constexpr unsigned len(unsigned) { return Width; }
  };
  template <unsigned Width> struct Print_LeftLen<Width, 1> {
    ZuInline static constexpr unsigned len(unsigned n) { return n; }
  };
  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width, bool Negative>
  struct Print_Left;
  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width>
  struct Print_Left<Hex, Comma, Upper, Alt, Pad, Width, 0> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n);
      if (ZuUnlikely(n > Width)) return 0;
      BaseN<Hex, Comma, Upper, Alt>::print(v, n, buf);
      if (ZuLikely(n < Width)) memset(buf + n, Pad, Width - n);
      return Print_LeftLen<Width, Pad == 0>::len(n);
    }
  };
  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width>
  struct Print_Left<Hex, Comma, Upper, Alt, Pad, Width, 1> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n) + 1;
      if (ZuUnlikely(n > Width)) return 0;
      *buf++ = '-';
      BaseN<Hex, Comma, Upper, Alt>::print(v, n - 1, buf);
      if (ZuLikely(n < Width)) memset(buf + n - 1, Pad, Width - n);
      return Print_LeftLen<Width, Pad == 0>::len(n);
    }
  };

  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width, bool Negative>
  struct Print_Right;
  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width>
  struct Print_Right<Hex, Comma, Upper, Alt, Pad, Width, 0> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n);
      if (ZuUnlikely(n > Width)) return 0;
      if (ZuLikely(n < Width)) memset(buf, Pad, Width - n);
      BaseN<Hex, Comma, Upper, Alt>::print(v, n, buf + Width - n);
      return Width;
    }
  };
  template <bool Hex, char Comma, bool Upper, bool Alt,
    char Pad, unsigned Width>
  struct Print_Right<Hex, Comma, Upper, Alt, Pad, Width, 1> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n) + 1;
      if (ZuUnlikely(n > Width)) return 0;
      if (ZuLikely(n < Width)) memset(buf, Pad, Width - n);
      buf += Width - n;
      *buf++ = '-';
      BaseN<Hex, Comma, Upper, Alt>::print(v, n - 1, buf);
      return Width;
    }
  };
  template <bool Hex, char Comma, bool Upper, bool Alt, unsigned Width>
  struct Print_Right<Hex, Comma, Upper, Alt, '0', Width, 1> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<Hex, Comma, Alt>::len(n) + 1;
      if (ZuUnlikely(n > Width)) return 0;
      *buf++ = '-';
      if (ZuLikely(n < Width)) memset(buf, '0', Width - n);
      buf += Width - n;
      BaseN<Hex, Comma, Upper, Alt>::print(v, n - 1, buf);
      return Width;
    }
  };
  template <char Comma, bool Upper, unsigned Width>
  struct Print_Right<1, Comma, Upper, 1, '0', Width, 0> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<1, 0, 0>::len(n) + 2;
      if (ZuUnlikely(n > Width)) return 0;
      *buf++ = '0'; *buf++ = 'x';
      if (ZuLikely(n < Width)) memset(buf, '0', Width - n);
      BaseN<1, 0, Upper, 0>::print(v, n - 2, buf + Width - n);
      return Width;
    }
  };
  template <char Comma, bool Upper, unsigned Width>
  struct Print_Right<1, Comma, Upper, 1, '0', Width, 1> {
    template <typename T>
    ZuInline static unsigned print(T v, unsigned n, char *buf) {
      n = Len<1, 0, 0>::len(n) + 3;
      if (ZuUnlikely(n > Width)) return 0;
      *buf++ = '-'; *buf++ = '0'; *buf++ = 'x';
      if (ZuLikely(n < Width)) memset(buf, '0', Width - n);
      buf += Width - n;
      BaseN<1, 0, Upper, 0>::print(v, n - 3, buf);
      return Width;
    }
  };

  // generic Print - dispatches to none/left/right justification
  template <bool Hex, char Comma, bool Upper, bool Alt, int Justification,
    unsigned Width, char Pad, bool Negative>
  struct Print;
  template <bool Hex, char Comma, bool Upper, bool Alt,
    unsigned Width, char Pad, bool Negative>
  struct Print<
    Hex, Comma, Upper, Alt, Just::None, Width, Pad, Negative> :
      public Print_<Hex, Comma, Upper, Alt, Negative> { };
  template <bool Hex, char Comma, bool Upper, bool Alt,
    unsigned Width, char Pad, bool Negative>
  struct Print<
    Hex, Comma, Upper, Alt, Just::Left, Width, Pad, Negative> :
      public Print_Left<Hex, Comma, Upper, Alt, Pad, Width, Negative> { };
  template <bool Hex, char Comma, bool Upper, bool Alt,
    unsigned Width, char Pad, bool Negative>
  struct Print<
    Hex, Comma, Upper, Alt, Just::Right, Width, Pad, Negative> :
      public Print_Right<Hex, Comma, Upper, Alt, Pad, Width, Negative> { };

  template <typename T>
  ZuInline constexpr T ftoa_max() { return T(~uint64_t(0)); }
  template <typename T>
  unsigned ftoa_variable(T v, unsigned f, char *buf) {
    using FP = ZuFP<T>;
    if (ZuUnlikely(FP::nan(v))) {
      *buf++ = 'n', *buf++ = 'a', *buf = 'n';
      return 3U;
    }
    bool negative = false;
    if (v < 0) negative = true, v = -v, *buf++ = '-';
    if (ZuUnlikely(v > ftoa_max<T>())) {// (T)(~0ULL)
      *buf++ = 'i', *buf++ = 'n', *buf = 'f';
      return 3U + negative;
    }
    uint64_t iv = v;
    unsigned i = Log10FP::log<FP::Bits>(iv);
    {
      unsigned j = i & 0xffU;
      if (f > j) f = j;
    }
    i >>= 8U;
    if (!f) {
      Base10_print(iv, i, buf);
      return i + negative;
    }
    uint64_t fv = frac(v, iv, i, f);
    Base10_print(iv, i, buf);
    if (!f || !fv) return i + negative;
    buf += i;
    *buf++ = '.';
    return i + 1 + Base10_print_frac_truncate(fv, f, f, buf) + negative;
  }
  template <typename T>
  static unsigned ftoa_fixed(T v, unsigned f, char *buf) {
    using FP = ZuFP<T>;
    if (ZuUnlikely(FP::nan(v))) {
      *buf++ = 'n', *buf++ = 'a', *buf = 'n';
      return 3U;
    }
    bool negative = 0;
    if (v < 0) negative = 1, v = -v, *buf++ = '-';
    if (ZuUnlikely(v > ftoa_max<T>())) { // T(~uint64_t(0))
      *buf++ = 'i', *buf++ = 'n', *buf = 'f';
      return 3U + negative;
    }
    uint64_t iv = v;
    unsigned i = Log10<8>::log(iv);
    uint64_t fv = frac(v, iv, i, f);
    Base10_print(iv, i, buf);
    if (!f) return i + negative;
    buf += i;
    *buf++ = '.';
    Base10_print(fv, f, buf); 
    return i + 1 + f + negative;
  }

  template <char Comma, int NDP, char Trim, bool Fixed = NDP >= 0>
  struct FPrint {
    template <typename T>
    static unsigned ftoa(T v, char *buf) {
      using FP = ZuFP<T>;
      if (ZuUnlikely(FP::nan(v))) {
	*buf++ = 'n', *buf++ = 'a', *buf = 'n';
	return 3U;
      }
      bool negative = 0;
      if (ZuUnlikely(v < 0)) negative = 1, v = -v, *buf++ = '-';
      if (ZuUnlikely(v > Zu_ntoa::ftoa_max<T>())) {// (T)(~0ULL)
	*buf++ = 'i', *buf++ = 'n', *buf = 'f';
	return 3U + negative;
      }
      uint64_t iv = v;
      int f = NDP;
      unsigned i;
      if constexpr (!Fixed) {
	i = Log10FP::log<FP::Bits>(iv);
	unsigned j = i & 0xffU;
	if (unsigned(f = -f) > j) f = j;
	i >>= 8U;
      } else
	i = Log10<8>::log(iv);
      uint64_t fv = frac(v, iv, i, f);
      if constexpr (!Comma) {
	Base10_print(iv, i, buf);
      } else {
	i += ((i - 1U)/3U);
	Base10_print_comma(iv, i, buf, Comma);
      }
      if (!f || (!fv && !Fixed)) return i + negative;
      buf += i;
      *buf++ = '.';
      if constexpr (Fixed) {
	Base10_print(fv, f, buf); 
	return i + 1 + f + negative;
      }
      if constexpr (!Trim)
	return i + 1 + Base10_print_frac_truncate(fv, f, f, buf) + negative;
      Base10_print_frac(fv, f, f, Trim, buf);
      return i + 1 + f + negative;
    }
  };
  template <int NDP> struct FPrint<'\0', NDP, '\0', 0> {
    template <typename T>
    ZuInline static unsigned ftoa(T v, char *buf) {
      return ftoa_variable(v, -NDP, buf);
    }
  };
  template <int NDP> struct FPrint<'\0', NDP, '\0', 1> {
    template <typename T>
    ZuInline static unsigned ftoa(T v, char *buf) {
      return ftoa_fixed(v, NDP, buf);
    }
  };
}

// static compile-time formatted utoa/itoa/ftoa
template <class Fmt = ZuFmt::Default,
  bool Frac = Fmt::Justification_ == ZuFmt::Just::Frac>
struct Zu_nprint {
  struct NFmt : public Fmt { enum { Negative_ = 1 }; };
  template <bool Negative>
  using MaxLen = Zu_ntoa::MaxLen<
    Fmt::Hex_, Fmt::Comma_, Fmt::Alt_,
    Fmt::Justification_, Fmt::Width_, Fmt::NDP_, Negative>;
  template <typename T>
  static constexpr unsigned maxLog() {
    return Zu_ntoa::LogN_MaxLog<sizeof(T), Fmt::Hex_>::N;
  }

  template <bool Negative>
  using Print_ = Zu_ntoa::Print<
    Fmt::Hex_, Fmt::Comma_, Fmt::Upper_, Fmt::Alt_,
    Fmt::Justification_, Fmt::Width_, Fmt::Pad_, Negative>;
  using Print = Print_<0>;
  using NPrint = Print_<1>;

  template <typename T>
  using Log = Zu_ntoa::LogN<Fmt::Hex_, sizeof(T)>;

  template <typename T>
  static constexpr unsigned ulen() {
    return MaxLen<0>::len(maxLog<T>());
  }
  template <typename T>
  static constexpr unsigned ulen(T /* v_ */) {
    // typename Zu_ntoa::Unsigned<T> v = v_;
    // return MaxLen<0>::len(Log<T>::log(v));
    return MaxLen<0>::len(maxLog<T>());
  }
  template <typename T>
  static unsigned utoa(T v_, char *buf) {
    typename Zu_ntoa::Unsigned<T> v = v_;
    return Print::print(v, Log<T>::log(v), buf);
  }

  template <typename T>
  static constexpr unsigned ilen() {
    return MaxLen<1>::len(maxLog<T>());
  }
  template <typename T>
  static constexpr unsigned ilen(T /* v_ */) {
    // typename Zu_ntoa::Unsigned<T> v = v_;
    // if (ZuUnlikely(v_ < 0)) v = ~v + 1;
    // return MaxLen<1>::len(Log<T>::log(v));
    return MaxLen<1>::len(maxLog<T>());
  }
  template <typename T>
  static unsigned itoa(T v_, char *buf) {
    unsigned n;
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(v_ < 0)) {
      v = ~v + 1;
      n = NPrint::print(v, Log<T>::log(v), buf);
    } else {
      n = Print::print(v, Log<T>::log(v), buf);
    }
    return n;
  }

  using FPrint = Zu_ntoa::FPrint<Fmt::Comma_, Fmt::NDP_, Fmt::Trim_>;

  // 20 integer digits with commas, with negative and .0, gives 26+3 = 29
  template <typename T>
  static constexpr unsigned flen() {
    return 29U + (Fmt::Trim_ ? (Fmt::NDP_ > 1 ? (Fmt::NDP_ - 1) : 0) : 0);
  }
  template <typename T>
  static constexpr unsigned flen(T) {
    return flen<T>();
  }
  template <typename T>
  ZuInline static unsigned ftoa(T v, char *buf) {
    return FPrint::ftoa(v, buf);
  }
};
template <unsigned NDP> struct Zu_nprint_frac_ {
  template <typename T>
  static constexpr unsigned ulen() { return NDP; }
  template <typename T>
  static constexpr unsigned ulen(T) { return NDP; }
  template <typename T>
  static constexpr unsigned ilen() { return NDP; }
  template <typename T>
  static constexpr unsigned ilen(T) { return NDP; }
};
template <unsigned Width, unsigned NDP, char Trim>
struct Zu_nprint_frac : public Zu_nprint_frac_<NDP> {
  template <typename T>
  static unsigned utoa(T v, char *buf) {
    Zu_ntoa::Base10_print_frac(v, Width, NDP, Trim, buf);
    return NDP;
  }
  template <typename T>
  static unsigned itoa(T v_, char *buf) {
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(v_ < 0)) v = ~v + 1;
    Zu_ntoa::Base10_print_frac(v, Width, NDP, Trim, buf);
    return NDP;
  }
};
template <unsigned Width, unsigned NDP>
struct Zu_nprint_frac<Width, NDP, '\0'> : public Zu_nprint_frac_<NDP> {
  template <typename T>
  static unsigned utoa(T v, char *buf) {
    return Zu_ntoa::Base10_print_frac_truncate(v, Width, NDP, buf);
  }
  template <typename T>
  static unsigned itoa(T v_, char *buf) {
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(v_ < 0)) v = ~v + 1;
    return Zu_ntoa::Base10_print_frac_truncate(v, Width, NDP, buf);
  }
};
template <unsigned Width, unsigned NDP>
struct Zu_nprint_frac<Width, NDP, '0'> : public Zu_nprint_frac_<NDP> {
  template <typename T>
  static unsigned utoa(T v, char *buf) {
    Zu_ntoa::Base10_print(v, NDP, buf);
    return NDP;
  }
  template <typename T>
  static unsigned itoa(T v_, char *buf) {
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(v_ < 0)) v = ~v + 1;
    Zu_ntoa::Base10_print(v, NDP, buf);
    return NDP;
  }
};
template <class Fmt>
struct Zu_nprint<Fmt, true> :
  public Zu_nprint_frac<Fmt::Width_, Fmt::NDP_, Fmt::Trim_> { };

template <typename T>
ZuInline constexpr unsigned Zu_ulen() { return Zu_nprint<>::ulen<T>(); }
template <typename T>
ZuInline constexpr unsigned Zu_ulen(T v) { return Zu_nprint<>::ulen(v); }
template <typename T>
ZuInline unsigned Zu_utoa(T v, char *buf) { return Zu_nprint<>::utoa(v, buf); }

template <typename T>
ZuInline constexpr unsigned Zu_ilen() { return Zu_nprint<>::ilen<T>(); }
template <typename T>
ZuInline constexpr unsigned Zu_ilen(T v) { return Zu_nprint<>::ilen(v); }
template <typename T>
ZuInline unsigned Zu_itoa(T v, char *buf) { return Zu_nprint<>::itoa(v, buf); }

template <typename T>
ZuInline constexpr unsigned Zu_flen() { return Zu_nprint<>::flen<T>(); }
template <typename T>
ZuInline constexpr unsigned Zu_flen(T) { return Zu_nprint<>::flen<T>(); }
template <typename T>
ZuInline unsigned Zu_ftoa(T v, char *buf) { return Zu_nprint<>::ftoa(v, buf); }

// variable run-time formatted utoa/itoa/ftoa
struct Zu_vprint {
  template <typename T>
  static unsigned ulen(const ZuVFmt &fmt) {
    switch (fmt.justification()) {
      default:
	break;
      case ZuFmt::Just::Left:
      case ZuFmt::Just::Right:
	return fmt.width();
      case ZuFmt::Just::Frac:
	return fmt.ndp();
    }
    if (fmt.hex())
      return Zu_ntoa::Log16_MaxLog<sizeof(T)>::N + (fmt.alt()<<1U);
    unsigned i = Zu_ntoa::Log10_MaxLog<sizeof(T)>::N;
    if (fmt.comma()) i += ((i - 1U)/3U);
    return i;
  }
  template <typename T>
  ZuInline static unsigned ulen(const ZuVFmt &fmt, T) {
    return ulen<T>(fmt);
  }
  template <typename T>
  static unsigned utoa_hex(const ZuVFmt &fmt, T v, char *buf) {
    unsigned n = Zu_ntoa::Log16<sizeof(T)>::log(v);
    unsigned a = unsigned(fmt.alt())<<1;
    if (ZuLikely(fmt.justification() == ZuFmt::Just::None)) {
      if (a) *buf++ = '0', *buf++ = 'x';
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      return n + a;
    }
    int w = int(fmt.width()) - a;
    if (w < int(n)) return 0U;
    char pad = fmt.pad();
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Right)) {
      if (a && pad == '0') *buf++ = '0', *buf++ = 'x';
      if (unsigned(w) > n) memset(buf, pad, unsigned(w) - n);
      buf += unsigned(w) - n;
      if (a && pad != '0') *buf++ = '0', *buf++ = 'x';
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      return unsigned(w) + a;
    }
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Left)) {
      if (a) { *buf++ = '0', *buf++ = 'x'; }
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      if (unsigned(w) > n) memset(buf + n, pad, unsigned(w) - n);
      return unsigned(w) + a;
    }
    return 0;
  }
  template <typename T>
  static unsigned utoa(const ZuVFmt &fmt, T v, char *buf) {
    if (ZuUnlikely(fmt.hex())) return utoa_hex(fmt, v, buf);
    if (ZuUnlikely(fmt.justification() == ZuFmt::Just::Frac)) {
      unsigned w = fmt.width();
      unsigned n = fmt.ndp();
      char trim = fmt.trim();
      if (ZuLikely(!trim))
	return Zu_ntoa::Base10_print_frac_truncate(v, w, n, buf);
      if (ZuUnlikely(trim == '0')) {
	Zu_ntoa::Base10_print(v, n, buf);
	return n;
      }
      Zu_ntoa::Base10_print_frac(v, w, n, trim, buf);
      return n;
    }
    unsigned n = Zu_ntoa::Log10<sizeof(T)>::log(v);
    char comma = fmt.comma();
    if (comma) n += ((n - 1U)/3U);
    if (ZuLikely(fmt.justification() == ZuFmt::Just::None)) {
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      return n;
    }
    unsigned w = fmt.width();
    if (w < n) return 0U;
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Right)) {
      if (w > n) {
	memset(buf, fmt.pad(), w - n);
	buf += w - n;
      }
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      return w;
    }
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Left)) {
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      if (w > n) memset(buf + n, fmt.pad(), w - n);
      return w;
    }
    return 0;
  }
  template <typename T>
  ZuInline static unsigned ilen(const ZuVFmt &fmt) {
    return ulen<T>(fmt) + 1;
  }
  template <typename T>
  ZuInline static unsigned ilen(const ZuVFmt &fmt, T) {
    return ulen<T>(fmt) + 1;
  }
  template <typename T>
  static unsigned itoa_hex(const ZuVFmt &fmt, T v_, char *buf) {
    bool m = v_ < 0;
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(m)) v = ~v + 1;
    unsigned n = Zu_ntoa::Log16<sizeof(T)>::log(v);
    unsigned a = unsigned(fmt.alt())<<1;
    if (ZuLikely(fmt.justification() == ZuFmt::Just::None)) {
      if (m) *buf++ = '-';
      if (a) *buf++ = '0', *buf++ = 'x';
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      return n + a + m;
    }
    int w = (int)fmt.width() - (a + m);
    if (w < (int)n) return 0U;
    char pad = fmt.pad();
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Right)) {
      if (m && pad == '0') *buf++ = '-';
      if (a && pad == '0') *buf++ = '0', *buf++ = 'x';
      if (unsigned(w) > n) {
	memset(buf, pad, unsigned(w) - n);
	buf += unsigned(w) - n;
      }
      if (m && pad != '0') *buf++ = '-';
      if (a && pad != '0') *buf++ = '0', *buf++ = 'x';
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      return unsigned(w) + a + m;
    }
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Left)) {
      if (m) *buf++ = '-';
      if (a) *buf++ = '0', *buf++ = 'x';
      if (!fmt.upper())
	Zu_ntoa::Base16_print(v, n, buf);
      else
	Zu_ntoa::Base16_print_upper(v, n, buf);
      if (unsigned(w) > n) memset(buf + n, pad, unsigned(w) - n);
      return unsigned(w) + a + m;
    }
    return 0;
  }
  template <typename T>
  static unsigned itoa(const ZuVFmt &fmt, T v_, char *buf) {
    if (ZuUnlikely(fmt.hex())) return itoa_hex(fmt, v_, buf);
    if (ZuUnlikely(fmt.justification() == ZuFmt::Just::Frac)) {
      unsigned w = fmt.width();
      unsigned n = fmt.ndp();
      typename Zu_ntoa::Unsigned<T> v = v_;
      if (ZuUnlikely(v_ < 0)) v = ~v + 1;
      char trim = fmt.trim();
      if (ZuLikely(!trim))
	return Zu_ntoa::Base10_print_frac_truncate(v, w, n, buf);
      if (ZuUnlikely(trim == '0')) {
	Zu_ntoa::Base10_print(v, n, buf);
	return n;
      }
      Zu_ntoa::Base10_print_frac(v, w, n, trim, buf);
      return n;
    }
    bool m = v_ < 0;
    typename Zu_ntoa::Unsigned<T> v = v_;
    if (ZuUnlikely(m)) v = ~v + 1;
    unsigned n = Zu_ntoa::Log10<sizeof(T)>::log(v);
    char comma = fmt.comma();
    if (comma) n += ((n - 1U)/3U);
    if (ZuLikely(fmt.justification() == ZuFmt::Just::None)) {
      if (m) *buf++ = '-';
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      return n + m;
    }
    int w = (int)fmt.width() - m;
    if (w < (int)n) return 0U;
    char pad = fmt.pad();
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Right)) {
      if (m && pad == '0') *buf++ = '-';
      if (unsigned(w) > n) {
	memset(buf, pad, unsigned(w) - n);
	buf += unsigned(w) - n;
      }
      if (m && pad != '0') *buf++ = '-';
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      return unsigned(w) + m;
    }
    if (ZuLikely(fmt.justification() == ZuFmt::Just::Left)) {
      if (m) *buf++ = '-';
      if (ZuLikely(!comma))
	Zu_ntoa::Base10_print(v, n, buf);
      else
	Zu_ntoa::Base10_print_comma(v, n, buf, comma);
      if (unsigned(w) > n) memset(buf + n, pad, unsigned(w) - n);
      return unsigned(w) + m;
    }
    return 0;
  }
  template <typename T>
  static unsigned flen(const ZuVFmt &fmt) {
    return 29U + (fmt.trim() ? (fmt.ndp() > 1 ? (fmt.ndp() - 1) : 0) : 0);
  }
  template <typename T>
  static unsigned flen(const ZuVFmt &fmt, T) {
    return flen<T>(fmt);
  }
  template <typename T>
  static unsigned ftoa(const ZuVFmt &fmt, T v, char *buf) {
    using FP = ZuFP<T>;
    if (ZuUnlikely(FP::nan(v))) {
      *buf++ = 'n', *buf++ = 'a', *buf = 'n';
      return 3U;
    }
    bool negative = 0;
    if (v < 0) negative = 1, v = -v, *buf++ = '-';
    if (ZuUnlikely(v > (T)(~0ULL))) {
      *buf++ = 'i', *buf++ = 'n', *buf = 'f';
      return 3U + negative;
    }
    uint64_t iv = v;
    int f = fmt.ndp();
    bool fixed = f >= 0;
    unsigned i;
    if (!fixed) {
      i = Zu_ntoa::Log10FP::log<FP::Bits>(iv);
      unsigned j = i & 0xffU;
      if (unsigned(f = -f) > j) f = j;
      i >>= 8U;
    } else
      i = Zu_ntoa::Log10<8>::log(iv);
    uint64_t fv = Zu_ntoa::frac(v, iv, i, f);
    {
      char comma = fmt.comma();
      if (ZuLikely(!comma)) {
	Zu_ntoa::Base10_print(iv, i, buf);
      } else {
	i += ((i - 1U)/3U);
	Zu_ntoa::Base10_print_comma(iv, i, buf, comma);
      }
    }
    if (!f || (!fv && !fixed)) return i + negative;
    buf += i;
    *buf++ = '.';
    if (fixed) {
      Zu_ntoa::Base10_print(fv, f, buf); 
      return i + 1 + f + negative;
    }
    char trim = fmt.trim();
    if (!trim)
      return
	i + 1 + Zu_ntoa::Base10_print_frac_truncate(fv, f, f, buf) + negative;
    Zu_ntoa::Base10_print_frac(fv, f, f, trim, buf);
    return i + 1 + f + negative;
  }
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif /* Zu_ntoa_HH */
