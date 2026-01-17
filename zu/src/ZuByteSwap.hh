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

template <unsigned> struct ZuByteSwap_UInt;
template <> struct ZuByteSwap_UInt<2> { using T = uint16_t; };
template <> struct ZuByteSwap_UInt<4> { using T = uint32_t; };
template <> struct ZuByteSwap_UInt<8> { using T = uint64_t; };
template <> struct ZuByteSwap_UInt<16> { using T = uint128_t; };

#pragma pack(push, 1)

template <typename T_> class ZuByteSwap {
public:
  using T = T_;
  using U = ZuUnder<T>;
  using I = typename ZuByteSwap_UInt<sizeof(T)>::T;

  constexpr ZuByteSwap() noexcept { m_i = 0; }
  constexpr ZuByteSwap(const ZuByteSwap &i) noexcept { m_i = i.m_i; }
  constexpr ZuByteSwap &operator =(const ZuByteSwap &i) noexcept {
    if (this != &i) m_i = i.m_i;
    return *this;
  }

  template <typename R>
  ZuByteSwap(const R &r) noexcept { set(r); }
  template <typename R>
  ZuByteSwap &operator =(const R &r) noexcept { set(r); return *this; }

  constexpr operator U() const noexcept { return get<U>(); }

  ZuByteSwap operator -() { return ZuByteSwap(-get<U>()); }

  template <typename P> ZuByteSwap operator +(const P &p) const {
    return ZuByteSwap(get<U>() + p);
  }
  template <typename P> ZuByteSwap operator -(const P &p) const {
    return ZuByteSwap(get<U>() - p);
  }
  template <typename P> ZuByteSwap operator *(const P &p) const {
    return ZuByteSwap(get<U>() * p);
  }
  template <typename P> ZuByteSwap operator /(const P &p) const {
    return ZuByteSwap(get<U>() / p);
  }
  template <typename P> ZuByteSwap operator %(const P &p) const {
    return ZuByteSwap(get<U>() % p);
  }
  template <typename P> ZuByteSwap operator |(const P &p) const {
    return ZuByteSwap(get<U>() | p);
  }
  template <typename P> ZuByteSwap operator &(const P &p) const {
    return ZuByteSwap(get<U>() & p);
  }
  template <typename P> ZuByteSwap operator ^(const P &p) const {
    return ZuByteSwap(get<U>() ^ p);
  }

  ZuByteSwap operator ++(int) {
    ZuByteSwap o = *this;
    set(get<U>() + 1);
    return o;
  }
  ZuByteSwap &operator ++() {
    set(get<U>() + 1);
    return *this;
  }
  ZuByteSwap operator --(int) {
    ZuByteSwap o = *this;
    set(get<U>() - 1);
    return o;
  }
  ZuByteSwap &operator --() {
    set(get<U>() - 1);
    return *this;
  }

  template <typename P> ZuByteSwap &operator +=(const P &p) {
    set(get<U>() + p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator -=(const P &p) {
    set(get<U>() - p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator *=(const P &p) {
    set(get<U>() * p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator /=(const P &p) {
    set(get<U>() / p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator %=(const P &p) {
    set(get<U>() % p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator |=(const P &p) {
    set(get<U>() | p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator &=(const P &p) {
    set(get<U>() & p);
    return *this;
  }
  template <typename P> ZuByteSwap &operator ^=(const P &p) {
    set(get<U>() ^ p);
    return *this;
  }

private:
  // P is exactly ZuByteSwap<T>
  template <typename P>
  ZuSame<P, ZuByteSwap> set(const P &p) { m_i = p.m_i; }
  template <typename P>
  ZuSame<P, ZuByteSwap, const ZuByteSwap &> get() const { return *this; }

  // P is exactly U or T
  template <typename P>
  ZuIfT<bool(ZuIsSame<P, T>{}) || bool(ZuIsSame<P, U>{})> set(const P &p) {
    m_i = ZuIntrin::bswap(ZuPun<U, I>(p).out);
  }
  template <typename P>
  ZuIfT<bool(ZuIsSame<P, T>{}) || bool(ZuIsSame<P, U>{}), P> get() const {
    return ZuPun<I, U>(ZuIntrin::bswap(m_i)).out;
  }

  // P is integral (but not the same)
  template <typename P>
  ZuIfT<
      !ZuIsSame<P, ZuByteSwap>{} &&
      !ZuIsSame<P, T>{} && !ZuIsSame<P, U>{} &&
      ZuTraits<P>::IsIntegral>
  set(P p) {
    m_i = ZuIntrin::bswap(I(p));
  }
  template <typename P>
  ZuIfT<
      !ZuIsSame<P, ZuByteSwap>{} &&
      !ZuIsSame<P, T>{} && !ZuIsSame<P, U>{} &&
      ZuTraits<P>::IsIntegral>
  get() const {
    return ZuIntrin::bswap(m_i);
  }

  // P is non-integral and converts (but is not the same as U)
  template <typename P>
  ZuIfT<
      !ZuIsSame<P, ZuByteSwap>{} &&
      !ZuIsSame<P, T>{} && !ZuIsSame<P, U>{} &&
      !ZuTraits<P>::IsIntegral &&
      ZuIsConvertible<P, U>{}>
  set(P p) {
    m_i = ZuIntrin::bswap(ZuPun<U, I>(p).out);
  }
  template <typename P>
  ZuIfT<
      !ZuIsSame<P, ZuByteSwap>{} &&
      !ZuIsSame<P, T>{} && !ZuIsSame<P, U>{} &&
      !ZuTraits<P>::IsIntegral &&
      ZuIsConvertible<U, P>{}, P>
  get() const {
    return ZuPun<I, U>(ZuIntrin::bswap(m_i)).out;
  }

  // traits
  struct Traits : public ZuTraits<I> { enum { IsPrimitive = 0 }; };
  friend Traits ZuTraitsType(ZuByteSwap *);

  // underlying
  friend U ZuUnderType(ZuByteSwap *);

private:
  I	m_i;
};

#pragma pack(pop)

#if Zu_BIGENDIAN
template <typename T> using ZuBigEndian = T;
template <typename T> using ZuLittleEndian = ZuByteSwap<T>;
#else
template <typename T> using ZuBigEndian = ZuByteSwap<T>;
template <typename T> using ZuLittleEndian = T;
#endif

#endif /* ZuByteSwap_HH */
