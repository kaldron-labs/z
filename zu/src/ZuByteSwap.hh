//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// byte swap utility class

// Example:
// using UInt32N = typename ZuBigEndian<ZuBox0(uint32_t)>;
// #pragma pack(push, 1)
// struct Hdr {
//   ...
//   UInt32N	length;	// length in network byte order (bigendian)
//   ...
// };
// #pragma pack(pop)
// ...
// char buf[1472];
// ...
// char *ptr = buf;
// Hdr *hdr = (Hdr *)ptr;
// printf("%u\n", (uint32_t)hdr->length);
// ...
// ptr += sizeof(Hdr) + hdr->length;

#ifndef ZuByteSwap_HH
#define ZuByteSwap_HH

#include <stddef.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuIntrin.hh>
#include <zlib/ZuAssert.hh>

template <unsigned> struct ZuByteSwap_UInt;
template <> struct ZuByteSwap_UInt<2> { using T = uint16_t; };
template <> struct ZuByteSwap_UInt<4> { using T = uint32_t; };
template <> struct ZuByteSwap_UInt<8> { using T = uint64_t; };
template <> struct ZuByteSwap_UInt<16> { using T = uint128_t; };

#pragma pack(push, 1)

template <typename T_> class ZuByteSwap__ {
public:
  using T = T_;
  using U = ZuUnder<T>;
  ZuAssert(sizeof(T) == sizeof(U));
  ZuAssert(ZuTraits<U>::IsPrimitive && ZuTraits<U>::IsReal);
  using I = typename ZuByteSwap_UInt<sizeof(T)>::T;

  constexpr ZuByteSwap__() noexcept { m_i = 0; }
  constexpr ZuByteSwap__(const ZuByteSwap__ &i) noexcept { m_i = i.m_i; }
  constexpr ZuByteSwap__ &operator =(const ZuByteSwap__ &i) noexcept {
    if (this != &i) m_i = i.m_i;
    return *this;
  }

  template <typename R>
  ZuByteSwap__(const R &r) noexcept { set(r); }
  template <typename R>
  ZuByteSwap__ &operator =(const R &r) noexcept { set(r); return *this; }

  constexpr operator U() const noexcept { return get<U>(); }

  ZuByteSwap__ operator -() { return ZuByteSwap__(-get<U>()); }

  template <typename P> ZuByteSwap__ operator +(const P &p) const {
    return ZuByteSwap__(get<U>() + p);
  }
  template <typename P> ZuByteSwap__ operator -(const P &p) const {
    return ZuByteSwap__(get<U>() - p);
  }
  template <typename P> ZuByteSwap__ operator *(const P &p) const {
    return ZuByteSwap__(get<U>() *p);
  }
  template <typename P> ZuByteSwap__ operator /(const P &p) const {
    return ZuByteSwap__(get<U>() / p);
  }
  template <typename P> ZuByteSwap__ operator %(const P &p) const {
    return ZuByteSwap__(get<U>() % p);
  }
  template <typename P> ZuByteSwap__ operator |(const P &p) const {
    return ZuByteSwap__(get<U>() | p);
  }
  template <typename P> ZuByteSwap__ operator &(const P &p) const {
    return ZuByteSwap__(get<U>() &p);
  }
  template <typename P> ZuByteSwap__ operator ^(const P &p) const {
    return ZuByteSwap__(get<U>() ^ p);
  }

  ZuByteSwap__ operator ++(int) {
    ZuByteSwap__ o = *this;
    set(get<U>() + 1);
    return o;
  }
  ZuByteSwap__ &operator ++() {
    set(get<U>() + 1);
    return *this;
  }
  ZuByteSwap__ operator --(int) {
    ZuByteSwap__ o = *this;
    set(get<U>() - 1);
    return o;
  }
  ZuByteSwap__ &operator --() {
    set(get<U>() - 1);
    return *this;
  }

  template <typename P> ZuByteSwap__ &operator +=(const P &p) {
    set(get<U>() + p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator -=(const P &p) {
    set(get<U>() - p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator *=(const P &p) {
    set(get<U>() *p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator /=(const P &p) {
    set(get<U>() / p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator %=(const P &p) {
    set(get<U>() % p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator |=(const P &p) {
    set(get<U>() | p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator &=(const P &p) {
    set(get<U>() &p);
    return *this;
  }
  template <typename P> ZuByteSwap__ &operator ^=(const P &p) {
    set(get<U>() ^ p);
    return *this;
  }

private:
  // P is exactly ZuByteSwap__<T>

  // P is exactly U or T
  template <typename P,
    typename = ZuIfT<ZuIsSame<P, ZuByteSwap__>{} || ZuIsSame<P, T>{} || ZuIsSame<P, U>{}>>
  void set(const P &p) {
    if constexpr (ZuIsSame<P, ZuByteSwap__>{}) {
      m_i = p.m_i;
    } else {
      m_i = ZuIntrin::bswap(ZuPun<U, I>(p).out);
    }
  }

  // P is integral (but not the same)

  // P is non-integral and converts (but is not the same as U)
  template <typename P,
    typename = ZuIfT<!ZuIsSame<P, ZuByteSwap__>{} && !ZuIsSame<P, T>{} && !ZuIsSame<P,
      U>{} && (ZuTraits<P>::IsIntegral || ZuIsConvertible<P, U>{})>>
  void
  set(P p) {
    if constexpr (!ZuIsSame<P, ZuByteSwap__>{} && !ZuIsSame<P, T>{} && !ZuIsSame<P,
      U>{} && ZuTraits<P>::IsIntegral) {
      m_i = ZuIntrin::bswap(I(p));
    } else {
      m_i = ZuIntrin::bswap(ZuPun<U, I>(p).out);
    }
  }
  template <typename P,
    typename = ZuIfT<ZuIsSame<P, ZuByteSwap__>{} ||
      ZuIsSame<P, T>{} || ZuIsSame<P, U>{} ||
      ZuTraits<P>::IsIntegral || ZuIsConvertible<U, P>{}>>
  decltype(auto) get() const {
    if constexpr (ZuIsSame<P, ZuByteSwap__>{})
      return *this;
    else if constexpr (ZuIsSame<P, T>{} || ZuIsSame<P, U>{})
      return static_cast<P>(ZuPun<I, U>(ZuIntrin::bswap(m_i)).out);
    else if constexpr (ZuTraits<P>::IsIntegral)
      return static_cast<P>(ZuIntrin::bswap(m_i));
    else
      return static_cast<P>(ZuPun<I, U>(ZuIntrin::bswap(m_i)).out);
  }

  // traits
  struct Traits : public ZuTraits<I> { enum { IsPrimitive = 0 }; };
  friend Traits ZuTraitsType(ZuByteSwap__ *);

  // underlying
  friend U ZuUnderType(ZuByteSwap__ *);

private:
  I	m_i;
};

#pragma pack(pop)

template <typename U> struct ZuByteSwap_ { using T = ZuByteSwap__<U>; };
template <typename U> struct ZuByteSwap_<ZuByteSwap__<U>> { using T = U; };
template <typename U> using ZuByteSwap = typename ZuByteSwap_<U>::T;

#if Zu_BIGENDIAN
template <typename T> using ZuBigEndian = T;
template <typename T> using ZuLittleEndian = ZuByteSwap<T>;
template <typename T> constexpr T ZuBE(T v) { return v; }
template <typename T> constexpr T ZuLE(T v) { return ZuIntrin::bswap(v); }
#else
template <typename T> using ZuBigEndian = ZuByteSwap<T>;
template <typename T> using ZuLittleEndian = T;
template <typename T> constexpr T ZuBE(T v) { return ZuIntrin::bswap(v); }
template <typename T> constexpr T ZuLE(T v) { return v; }
#endif

#endif /* ZuByteSwap_HH */
