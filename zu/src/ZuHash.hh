//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic hashing
// - WARNING - this library should NOT be used for cryptography
// - preimage resistance is intentionally omitted
// - intended to provide excellent performance and collision resistance
//   for use in in-memory hash tables and other similar applications
// - all these functions must return consistent hash codes for the
//   same string values, regardless of the string type
// - compile-time hashing is intentionally not a goal

// UDTs can implement a "uint32_t hash() const" public member function

// unsigned long i;
// signed char j;
// i == j implies ZuHash<unsigned long>::hash(i) == ZuHash<signed char>::hash(j)
//
// const char *s;
// ZtString<> t;
// s == t implies ZuHash<const char *>::hash(s) == ZuHash<ZtString<>>::hash(t)

#ifndef ZuHash_HH
#define ZuHash_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <math.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuCmp.hh>

template <typename T, bool = ZuTraits<T>::IsString> struct ZuHash_;

// golden ratio hash function, specialized for 32bit, 64bit and 128bit types
// - derived from Linux kernel source include/hash.h
// - Knuth vol 3, section 6.4, exercise 9
// - http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf

namespace ZuHash_GoldenRatio32 {
ZuInline constexpr uint32_t ratio() { return 0x61c88647; }
ZuInline constexpr uint32_t hash(uint32_t i) { return i * ratio(); }
}

namespace ZuHash_GoldenRatio64 {
ZuInline constexpr uint64_t ratio() { return 0x61c8864680b583ebULL; }
ZuInline constexpr uint32_t hash(uint64_t i) { return (i * ratio())>>32; }
}

namespace ZuHash_GoldenRatio128 {
ZuInline constexpr const uint128_t ratio() {
  return (uint128_t(0x61c8864680b583eaULL)<<64) | 0x0c633f9fa31237ccULL;
}
ZuInline constexpr const uint32_t hash(uint128_t i) {
  return (i * ratio())>>96; }
}

// Fowler / Noll / Vo (FNV) hash function (type FNV-1a)

#if defined(__x86_64__) || defined(_WIN64)
struct ZuHash_FNV_ {
  using Value = uint64_t;

  static Value initial_() { return 0xcbf29ce484222325ULL; }

  static Value hash_(Value v, Value i) {
    v ^= i;
    v *= 0x100000001b3ULL;
    return v;
  }
};
#else
struct ZuHash_FNV_ {
  using Value = uint32_t;

  static Value initial_() { return 0x811c9dc5UL; }

  static Value hash_(Value v, Value i) {
    v ^= i;
    v *= 0x1000193UL;
    return v;
  }
};
#endif

struct ZuHash_FNV : public ZuHash_FNV_ {
  using Value = ZuHash_FNV_::Value;

  static uint32_t hash(const uint8_t *p, int n) {
    Value v = initial_();
    while (--n >= 0) v = hash_(v, *p++);
    return uint32_t(v);
  }
};

// hashing of floats, doubles and long doubles

template <typename T> struct ZuHash_Floating;
template <> struct ZuHash_Floating<float> {
  static uint32_t hash(float v) {
    if (v == 0) return 0; // signed zero ambiguity
    if (ZuNull(v)) return uint32_t(1)<<31; // NaN ambiguity
    double d = v;
    return ZuHash_FNV::hash(
      reinterpret_cast<const uint8_t *>(&d), sizeof(double));
  }
};
template <> struct ZuHash_Floating<double> {
  static uint32_t hash(double v) {
    if (v == 0) return 0; // signed zero ambiguity
    if (ZuNull(v)) return uint32_t(1)<<31; // NaN ambiguity
    return ZuHash_FNV::hash(
      reinterpret_cast<const uint8_t *>(&v), sizeof(double));
  }
};
template <> struct ZuHash_Floating<long double> {
  static uint32_t hash(long double v) {
    if (v == 0) return 0; // signed zero ambiguity
    if (ZuNull(v)) return uint32_t(1)<<31; // NaN ambiguity
    double d = v;
    return ZuHash_FNV::hash(
      reinterpret_cast<const uint8_t *>(&d), sizeof(double));
  }
};

// hashing of integral types

template <typename T, int Size> struct ZuHash_Integral {
  ZuInline static constexpr uint32_t hash(const T &t) {
    return ZuHash_GoldenRatio32::hash(uint32_t(t));
  }
};

template <typename T> struct ZuHash_Integral<T, 8> {
  ZuInline static constexpr uint32_t hash(const T &t) {
    return uint32_t(ZuHash_GoldenRatio64::hash(t));
  }
};

template <typename T> struct ZuHash_Integral<T, 16> {
  ZuInline static constexpr uint32_t hash(const T &t) {
    return uint32_t(ZuHash_GoldenRatio128::hash(t));
  }
};

// hashing of real types

template <typename T, bool IsIntegral> struct ZuHash_Real :
  public ZuHash_Integral<T, sizeof(T)> { };

template <typename T> struct ZuHash_Real<T, false> :
  public ZuHash_Floating<T> { };

// hashing of primitive types

template <typename T, bool IsReal, bool IsVoid> struct ZuHash_Primitive;

template <typename T> struct ZuHash_Primitive<T, true, false> :
  public ZuHash_Real<T, ZuTraits<T>::IsIntegral> { };

// hashing of non-primitive types

// test for hash()
template <typename> struct ZuCmp_Can_hash_;
template <> struct ZuCmp_Can_hash_<uint32_t> { using T = void; }; 
template <typename, typename = void>
struct ZuHash_Can_hash : public ZuFalse { };
template <typename T>
struct ZuHash_Can_hash<T, typename ZuCmp_Can_hash_<
  decltype(ZuDeclVal<const T &>().hash())>::T> :
    public ZuTrue { };

// test for hash_code() (STL cruft)
template <typename> struct ZuCmp_Can_hash_code_;
template <> struct ZuCmp_Can_hash_code_<std::size_t> { using T = void; }; 
template <typename, typename = void>
struct ZuHash_Can_hash_code : public ZuFalse { };
template <typename T>
struct ZuHash_Can_hash_code<T, typename ZuCmp_Can_hash_code_<
  decltype(ZuDeclVal<const T &>().hash_code())>::T> :
    public ZuTrue { };

template <typename T> struct ZuHash_NonPrimitive {
  template <typename U>
  static ZuIfT<ZuHash_Can_hash_code<U>{}, uint32_t>
  hash(const U &v) { return v.hash_code(); }
  template <typename U>
  static ZuIfT<ZuHash_Can_hash<U>{} && !ZuHash_Can_hash_code<U>{}, uint32_t>
  hash(const U &v) { return v.hash(); }
};

// hashing of pointers

template <typename T, int Size> struct ZuHash_Pointer;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4311)
#endif
template <typename T> struct ZuHash_Pointer<T, 4> {
  static uint32_t hash(T v) {
    return ZuHash_GoldenRatio32::hash(uint32_t(v));
  }
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

template <typename T> struct ZuHash_Pointer<T, 8> {
  static uint32_t hash(T v) {
    return uint32_t(ZuHash_GoldenRatio64::hash(uint64_t(v)));
  }
};

// pointer hashing

template <typename T> struct ZuHash_PrimitivePointer;

template <typename T> struct ZuHash_PrimitivePointer<T *> :
  public ZuHash_Pointer<const T *, sizeof(T *)> { };

// non-string hashing

template <typename T, bool IsPrimitive, bool IsPointer> struct ZuHash_NonString;

template <typename T> struct ZuHash_NonString<T, false, false> :
  public ZuHash_NonPrimitive<T> { };

template <typename T> struct ZuHash_NonString<T, false, true> :
  public ZuHash_Pointer<T, sizeof(T)> { };

template <typename T> struct ZuHash_NonString<T, true, false> :
  public ZuHash_Primitive<T, ZuTraits<T>::IsReal, ZuTraits<T>::IsVoid> { };

template <typename T> struct ZuHash_NonString<T, true, true> :
  public ZuHash_PrimitivePointer<T> { };

// string hashing

// Paul Hsieh's hash, better than FNV if length is known
// adapted from http://www.azillionmonkeys.com/qed/hash.html

#if (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))) || \
    defined(_WIN32)
#define ZuCSpanHash_Misaligned16BitLoadOK
#endif

template <typename T> struct ZuCSpanHash;
template <> struct ZuCSpanHash<char> {
  static uint32_t hash(const char *data_, size_t len) {
    auto data = reinterpret_cast<const uint8_t *>(data_);
    uint32_t hash = len;

    if (len <= 0 || !data) return 0;

    // main loop
    while (len>>2) {
#ifdef ZuCSpanHash_Misaligned16BitLoadOK
      hash += reinterpret_cast<const uint16_t *>(data)[0];
      hash =
	(hash<<16) ^ (reinterpret_cast<const uint16_t *>(data)[1]<<11) ^ hash;
#else
      hash += data[0] + (data[1]<<8);
      hash = (hash<<16) ^ ((data[2] + (data[3]<<8))<<11) ^ hash;
#endif
      hash += hash>>11;
      data += 4, len -= 4;
    }

    // handle end cases
    switch (len & 3) {
      case 3:
#ifdef ZuCSpanHash_Misaligned16BitLoadOK
	hash += reinterpret_cast<const uint16_t *>(data)[0];
#else
	hash += data[0] + (data[1]<<8);
#endif
	hash ^= hash<<16;
	hash ^= data[2]<<18;
	hash += hash>>11;
	break;
      case 2:
#ifdef ZuCSpanHash_Misaligned16BitLoadOK
	hash += reinterpret_cast<const uint16_t *>(data)[0];
#else
	hash += data[0] + (data[1]<<8);
#endif
	hash ^= hash<<11;
	hash += hash>>17;
	break;
      case 1:
	hash += data[0];
	hash ^= hash<<10;
	hash += hash>>1;
    }

    // "avalanche" the final character
    hash ^= hash<<3;
    hash += hash>>5;
    hash ^= hash<<4;
    hash += hash>>17;
    hash ^= hash<<25;
    hash += hash>>6;

    return hash;
    // return ZuHash_GoldenRatio32::hash(hash);
  }
};
template <> struct ZuCSpanHash<uint8_t> {
  static uint32_t hash(const uint8_t *data, size_t len) {
    return ZuCSpanHash<char>::hash(
      reinterpret_cast<const char *>(data), len);
  }
};
template <int WCharSize> struct ZuWSpanHash;
template <> struct ZuWSpanHash<2> {
  static uint32_t hash(const wchar_t *data_, size_t len) {
    auto data = reinterpret_cast<const uint16_t *>(data_);
    uint32_t hash = len;

    if (len <= 0 || !data) return 0;

    // main loop
    while (len>>1) {
      hash += data[0];
      hash = (hash<<16) ^ (data[1]<<11) ^ hash;
      hash += hash>>11;
      data += 2, len -= 2;
    }

    // handle end case
    if (len & 1) {
      hash += data[0];
      hash ^= hash<<11;
      hash += hash>>17;
    }

    // force "avalanching" of final character
    hash ^= hash<<3;
    hash += hash>>5;
    hash ^= hash<<4;
    hash += hash>>17;
    hash ^= hash<<25;
    hash += hash>>6;

    return hash;
    // return ZuHash_GoldenRatio32::hash(hash);
  }
};
template <> struct ZuWSpanHash<4> {
  static uint32_t hash(const wchar_t *data_, size_t len) {
    auto data = reinterpret_cast<const uint16_t *>(data_);
    uint32_t hash = len;

    if (len <= 0 || !data) return 0;

    // main loop
    while (len) {
      hash += data[0];
      hash = (hash<<16) ^ (data[1]<<11) ^ hash;
      hash += hash>>11;
      data++, len--;
    }

    // force "avalanching" of final character
    hash ^= hash<<3;
    hash += hash>>5;
    hash ^= hash<<4;
    hash += hash>>17;
    hash ^= hash<<25;
    hash += hash>>6;

    return hash;
  }
};
template <>
struct ZuCSpanHash<wchar_t> : public ZuWSpanHash<sizeof(wchar_t)> { };

// generic hashing function

template <typename T>
struct ZuHash_<T, false> : public ZuHash_NonString<
  T, ZuTraits<T>::IsPrimitive, ZuTraits<T>::IsPointer> { };

template <typename T> struct ZuHash_<T, true> {
  template <typename S>
  static uint32_t hash(const S &s) {
    using Traits = ZuTraits<S>;
    using Char = ZuDecay<typename Traits::Elem>;
    return ZuCSpanHash<Char>::hash(Traits::data(s), Traits::length(s));
  }
};

// generic template

template <typename T> struct ZuHash : public ZuHash_<ZuDecay<T>> { };

#endif /* ZuHash_HH */
