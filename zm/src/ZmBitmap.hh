//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// hwloc bitmap

#ifndef ZmBitmap_HH
#define ZmBitmap_HH

#ifdef _MSC_VER
#pragma once
#pragma warning(push)
#pragma warning(disable:4800)
#endif

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <hwloc.h>

#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuBitmap.hh>

namespace ZmBitmap_ {

// subtly different than ZuIter, due to end() being infinite
// - i is signed and -1 is the sentinel value for infinite
template <typename Bitmap_, typename Bit_>
class Iter {
public:
  using Bitmap = Bitmap_;
  using Bit = Bit_;

  using iterator_category = std::random_access_iterator_tag;
  using value_type = Bit;
  using difference_type = ptrdiff_t;
  using pointer = Bit *;
  using reference = Bit &;

  Iter() = delete;
  Iter(Bitmap &bitmap_, int i) : bitmap{bitmap_}, i{i} { }
  Iter(const Iter &) = default;
  Iter &operator =(const Iter &) = default;
  Iter(Iter &&) = default;
  Iter &operator =(Iter &&) = default;

  bool operator *() const;

  Iter &operator++() { ++i; return *this; }
  Iter operator++(int) { Iter _ = *this; ++(*this); return _; }
  Iter &operator--() { --i; return *this; }
  Iter operator--(int) { Iter _ = *this; --(*this); return _; }

  bool operator ==(const Iter &r) const {
    return &bitmap == &r.bitmap && i == r.i;
  }

  friend inline ptrdiff_t operator -(const Iter &l, const Iter &r) {
    if (l.i < 0) return r.i < 0 ? 0 : PTRDIFF_MAX;
    if (r.i < 0) return PTRDIFF_MIN;
    return ptrdiff_t(l.i) - ptrdiff_t(r.i);
  }

protected:
  Bitmap	&bitmap;
  int		i;		// -1 means infinite
};

class Bitmap {
public:
  using Bit = ZuBitmap_::Bit<Bitmap>; // re-use Bit from ZuBitmap

  Bitmap() noexcept : m_map{static_cast<hwloc_bitmap_t>(nullptr)} { }
  Bitmap(const Bitmap &b) noexcept :
    m_map{b.m_map ?
      hwloc_bitmap_dup(b.m_map) :
      static_cast<hwloc_bitmap_t>(nullptr)} { }
  Bitmap(Bitmap &&b) noexcept : m_map{b.m_map} { b.m_map = 0; }
  Bitmap &operator =(const Bitmap &b) noexcept {
    if (this == &b) return *this;
    if (!b.m_map) {
      if (m_map) hwloc_bitmap_free(m_map);
      m_map = 0;
      return *this;
    }
    if (!m_map) m_map = hwloc_bitmap_alloc();
    hwloc_bitmap_copy(m_map, b.m_map);
    return *this;
  }
  Bitmap &operator =(Bitmap &&b) noexcept {
    m_map = b.m_map;
    b.m_map = 0;
    return *this;
  }
  ~Bitmap() noexcept { if (m_map) hwloc_bitmap_free(m_map); }

private:
  void lazy() const {
    if (ZuUnlikely(!m_map))
      const_cast<Bitmap *>(this)->m_map = hwloc_bitmap_alloc();
  }

public:
  bool get(unsigned i) const {
    if (!m_map) return false;
    return hwloc_bitmap_isset(m_map, i);
  }
  Bitmap &set(unsigned i) {
    lazy();
    hwloc_bitmap_set(m_map, i);
    return *this;
  }
  Bitmap &clr(unsigned i) {
    lazy();
    hwloc_bitmap_clr(m_map, i);
    return *this;
  }

  const Bit operator [](unsigned i) const {
    return {*const_cast<Bitmap *>(this), i};
  }
  Bit operator [](unsigned i) { return {*this, i}; }

  bool equals(const Bitmap &b) const {
    if (this == &b || m_map == b.m_map) return true;
    if (!m_map || !b.m_map) return false;
    return hwloc_bitmap_isequal(m_map, b.m_map);
  }
  int cmp(const Bitmap &b) const {
    if (this == &b || m_map == b.m_map) return 0;
    if (!m_map) return -1;
    if (!b.m_map) return 1;
    return hwloc_bitmap_compare(m_map, b.m_map);
  }
  friend inline bool operator ==(const Bitmap &l, const Bitmap &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const Bitmap &l, const Bitmap &r) {
    return l.cmp(r);
  }

  using Range = ZuTuple<unsigned, unsigned>;

  template <typename T>
  ZuSame<Range, T, Bitmap &> set(const T &v) {
    lazy();
    hwloc_bitmap_set_range(m_map, v.p1(), v.p2());
    return *this;
  }
  template <typename T>
  ZuSame<Range, T, Bitmap &> clr(const T &v) {
    lazy();
    hwloc_bitmap_clr_range(m_map, v.p1(), v.p2());
    return *this;
  }

  bool operator &&(const Bitmap &b) const {
    if (!m_map) return !b.m_map;
    return hwloc_bitmap_isincluded(b.m_map, m_map);
  }
  bool operator ||(const Bitmap &b) const {
    if (!m_map) return false;
    return hwloc_bitmap_intersects(b.m_map, m_map);
  }

  Bitmap operator |(const Bitmap &b) const {
    lazy(); b.lazy();
    Bitmap r;
    hwloc_bitmap_or(r.m_map, m_map, b.m_map);
    return r;
  }
  Bitmap operator &(const Bitmap &b) const {
    lazy(); b.lazy();
    Bitmap r;
    hwloc_bitmap_and(r.m_map, m_map, b.m_map);
    return r;
  }
  Bitmap operator ^(const Bitmap &b) const {
    lazy(); b.lazy();
    Bitmap r;
    hwloc_bitmap_xor(r.m_map, m_map, b.m_map);
    return r;
  }
  Bitmap operator ~() const {
    lazy();
    Bitmap r;
    hwloc_bitmap_not(r.m_map, m_map);
    return r;
  }

  Bitmap &operator |=(const Bitmap &b) {
    lazy(); b.lazy();
    hwloc_bitmap_or(m_map, m_map, b.m_map);
    return *this;
  }
  Bitmap &operator &=(const Bitmap &b) {
    lazy(); b.lazy();
    hwloc_bitmap_and(m_map, m_map, b.m_map);
    return *this;
  }
  Bitmap &operator ^=(const Bitmap &b) {
    lazy(); b.lazy();
    hwloc_bitmap_xor(m_map, m_map, b.m_map);
    return *this;
  }

  void set(unsigned begin, int end) {
    hwloc_bitmap_set_range(m_map, begin, end);
  }

  Bitmap &zero() {
    lazy();
    hwloc_bitmap_zero(m_map);
    return *this;
  }
  Bitmap &fill() {
    lazy();
    hwloc_bitmap_fill(m_map);
    return *this;
  }

  bool operator !() const {
    return !m_map || hwloc_bitmap_iszero(m_map);
  }

  bool full() const {
    return m_map ? hwloc_bitmap_isfull(m_map) : false;
  }

  int first() const {
    return !m_map ? -1 : hwloc_bitmap_first(m_map);
  }
  int last() const {
    return !m_map ? -1 : hwloc_bitmap_last(m_map);
  }
  int next(int i) const {
    return !m_map ? -1 : hwloc_bitmap_next(m_map, i);
  }
  int count() const {
    return !m_map ? 0 : hwloc_bitmap_weight(m_map);
  }

  // hwloc_bitmap_t is a pointer
  operator hwloc_bitmap_t() {
    lazy();
    return m_map;
  }
  operator const hwloc_bitmap_t() const {
    return const_cast<Bitmap *>(this)->operator hwloc_bitmap_t();
  }

  Bitmap(uint64_t v) : m_map{hwloc_bitmap_alloc()} {
    hwloc_bitmap_from_ulong(m_map, v);
  }
  uint64_t uint64() const {
    if (ZuLikely(!m_map)) return 0;
    return hwloc_bitmap_to_ulong(m_map);
  }
  Bitmap(uint128_t v) : m_map{hwloc_bitmap_alloc()} {
    hwloc_bitmap_from_ith_ulong(m_map, 0, (uint64_t)v);
    hwloc_bitmap_from_ith_ulong(m_map, 1, (uint64_t)(v >> 64U));
  }
  uint128_t uint128() const {
    if (ZuLikely(!m_map)) return 0;
    return (uint128_t)hwloc_bitmap_to_ith_ulong(m_map, 0) |
      ((uint128_t)hwloc_bitmap_to_ith_ulong(m_map, 1) << 64U);
  }
  template <typename S, decltype(ZuMatchCharString<S>(), int()) = 0>
  Bitmap(const S &s) : m_map{hwloc_bitmap_alloc()} { scan(s); }
  template <typename S>
  ZuMatchCharString<S, Bitmap &> operator =(const S &s) {
    if (m_map) hwloc_bitmap_zero(m_map);
    scan(s);
    return *this;
  }
  // hwloc_bitmap can represent an infinitely set or cleared bitmap
  // - this is subtly different than ZuBitmap, so re-use is not attempted
  unsigned scan(ZuCSpan s) {
    lazy();
    const char *data = s.data();
    unsigned length = s.length(), offset = 0;
    if (!length) return 0;
    ZuBox<int> begin, end;
    int j;
    while (offset < length) {
      if (data[offset] == ',') { ++offset; continue; }
      if ((j = begin.scan(data + offset, length - offset)) <= 0) break;
      offset += j;
      if (offset < length && data[offset] == '-') {
	if ((j = end.scan(data + offset + 1, length - offset - 1)) > 0)
	  offset += j + 1;
	else {
	  end = -1;
	  ++offset;
	}
      } else
	end = begin;
      set(begin, end);
    }
    return offset;
  }
  template <typename S> void print(S &s) const {
    if (!*this) return;
    Bitmap tmp = *this;
    ZuBox<int> begin = hwloc_bitmap_first(m_map);
    bool first = true;
    while (begin >= 0) {
      if (!first)
	s << ',';
      else
	first = false;
      ZuBox<int> end = begin, next;
      hwloc_bitmap_set_range(tmp.m_map, 0, begin);
      if (hwloc_bitmap_isfull(tmp.m_map)) { s << begin << '-'; return; }
      while ((next = hwloc_bitmap_next(m_map, end)) == end + 1) end = next;
      if (end == begin)
	s << begin;
      else
	s << begin << '-' << end;
      begin = next;
    }
  }

  struct Traits : public ZuBaseTraits<Bitmap> { enum { IsComparable = 1 }; };
  friend Traits ZuTraitsType(Bitmap *);

  friend ZuPrintFn ZuPrintType(Bitmap *);

  using iterator = Iter<Bitmap, Bit>;
  using const_iterator = Iter<const Bitmap, const Bit>;
  using iterator_category = std::bidirectional_iterator_tag;
  const_iterator begin() const { return const_iterator{*this, 0}; }
  const_iterator end() const { return const_iterator{*this, -1}; }
  const_iterator cbegin() const { return const_iterator{*this, 0}; }
  const_iterator cend() const { return const_iterator{*this, -1}; }
  iterator begin() { return iterator{*this, 0}; }
  iterator end() { return iterator{*this, -1}; }

private:
  hwloc_bitmap_t	m_map;
};

template <typename Bitmap, typename Bit>
inline bool Iter<Bitmap, Bit>::operator *() const {
  return bitmap[i];
}

} // ZmBitmap_

using ZmBitmap = ZmBitmap_::Bitmap;

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmBitmap_HH */
