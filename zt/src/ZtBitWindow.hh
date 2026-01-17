//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// in-memory, dynamically allocated, sliding window bitfields
//
// supported bit widths:
// 1, 2, 3, 4, 5, 8, 10, 12, 16, 32, 64

#ifndef ZtBitWindow_HH
#define ZtBitWindow_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <stdlib.h>
#include <string.h>

// #include <iostream>

#include <zlib/ZuInt.hh>

#include <zlib/ZmVHeap.hh>

template <unsigned> struct ZtBitWindow_ { enum { OK = 0 }; };
template <> struct ZtBitWindow_<1>  { enum { OK = 1, Pow2 = 1, Shift =  0 }; };
template <> struct ZtBitWindow_<2>  { enum { OK = 1, Pow2 = 1, Shift =  1 }; };
template <> struct ZtBitWindow_<3>  { enum { OK = 1, Pow2 = 0,   Mul = 21 }; };
template <> struct ZtBitWindow_<4>  { enum { OK = 1, Pow2 = 1, Shift =  2 }; };
template <> struct ZtBitWindow_<5>  { enum { OK = 1, Pow2 = 0,   Mul = 12 }; };
template <> struct ZtBitWindow_<8>  { enum { OK = 1, Pow2 = 1, Shift =  3 }; };
template <> struct ZtBitWindow_<10> { enum { OK = 1, Pow2 = 0,   Mul =  6 }; };
template <> struct ZtBitWindow_<12> { enum { OK = 1, Pow2 = 0,   Mul =  5 }; };
template <> struct ZtBitWindow_<16> { enum { OK = 1, Pow2 = 1, Shift =  4 }; };
template <> struct ZtBitWindow_<32> { enum { OK = 1, Pow2 = 1, Shift =  5 }; };
template <> struct ZtBitWindow_<64> { enum { OK = 1, Pow2 = 1, Shift =  6 }; };

template <
  unsigned Bits = 1,
  bool = ZtBitWindow_<Bits>::OK,
  bool = ZtBitWindow_<Bits>::Pow2>
class ZtBitWindow;

using ZtBitWindow_ID = ZuStringT<"ZtBitWindow">;

template <unsigned Bits_>
class ZtBitWindow<Bits_, true, true> : private ZmVHeap_<ZtBitWindow_ID> {
  ZtBitWindow(const ZtBitWindow &) = delete;
  ZtBitWindow &operator =(const ZtBitWindow &) = delete;

public:
  enum { Bits = Bits_ };

private:
  enum { Shift = ZtBitWindow_<Bits>::Shift };
  static constexpr uint64_t Mask = (static_cast<uint64_t>(1)<<Bits) - 1;
  enum { IndexShift = (6 - Shift) };
  enum { IndexMask = (1<<IndexShift) - 1 };

public:
  ZtBitWindow() { }
  ~ZtBitWindow() { if (m_data) vfree(m_data); }

  ZtBitWindow(ZtBitWindow &&q) noexcept :
      m_data(q.m_data), m_size(q.m_size),
      m_head(q.m_head), m_offset(q.m_offset) {
    q.null();
  }
  ZtBitWindow &operator =(ZtBitWindow &&q) noexcept {
    if (ZuLikely(this != &q)) {
      if (m_data) vfree(m_data);
      m_data = q.m_data;
      m_size = q.m_size;
      m_head = q.m_head;
      q.null();
    }
    return *this;
  }

  void null() {
    if (m_data) vfree(m_data);
    m_data = nullptr;
    m_size = 0;
    m_head = 0;
    m_offset = 0;
  }

#if 0
  void debug() { m_debug = true; }

  void dump_() { }
  template <typename Arg0, typename ...Args>
  void dump_(Arg0 &&arg0, Args &&...args) {
    std::cerr << ZuMv(arg0);
    dump_(ZuMv(args)...);
  }
  template <typename ...Args>
  void dump(Args &&...args) {
    dump_(ZuMv(args)...);
    std::cerr
      << " size=" << m_size
      << " head=" << m_head
      << " tail=" << m_tail
      << " offset=" << m_offset << '\n' << std::flush;
  }
#endif

private:
#if 0
  uint64_t ensure(uint64_t i) {
    if (m_debug) dump("PRE  ensure(", i, ")");
    uint64_t v = ensure_(i);
    if (m_debug) dump("POST ensure(", i, ")");
    return v;
  }
#endif
  uint64_t ensure(uint64_t i) {
    if (ZuUnlikely(!m_size))
      m_head = (i & ~IndexMask);
    else if (ZuUnlikely(i < m_head)) {
      uint64_t required = ((m_head - i) + IndexMask)>>IndexShift;
      {
	uint64_t avail = m_size -
	  (((m_tail + IndexMask)>>IndexShift) - (m_head>>IndexShift));
	if (required <= avail) {
	  m_head -= (required<<IndexShift);
	  m_offset += (m_size - required);
	  if (m_offset >= m_size) m_offset -= m_size;
	  return 0;
	}
      }
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      memset(data, 0, required<<3);
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data + required, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + required + tailOffset, m_data, m_offset<<3);
      if (m_data) vfree(m_data);
      m_data = data;
      m_size += required;
      m_head -= (required<<IndexShift);
      m_offset = 0;
      return (i - m_head)>>IndexShift;
    }
    if (i >= m_tail) m_tail = i + 1;
    i -= m_head;
    if (ZuUnlikely(i >= (m_size<<IndexShift))) {
      uint64_t required =
	(((i + 1) - (m_size<<IndexShift)) + IndexMask)>>IndexShift;
      if (required < (m_size>>3))
	required = m_size>>3; // grow by at least 12.5%
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + tailOffset, m_data, m_offset<<3);
      memset(data + m_size, 0, required<<3);
      if (m_data) vfree(m_data);
      m_data = data;
      m_size += required;
      m_offset = 0;
      return i>>IndexShift;
    }
    return index(i>>IndexShift);
  }
  uint64_t index(uint64_t i) const {
    i += m_offset;
    if (i >= m_size) i -= m_size;
    return i;
  }

public:
#if 0
  void set(uint64_t i) {
    if (m_debug) dump("PRE  set(", i, ")");
    set_(i);
    if (m_debug) dump("POST set(", i, ")");
  }
#endif
  void set(uint64_t i) {
    uint64_t j = ensure(i);
    m_data[j] |= (Mask<<((i & IndexMask)<<Shift));
  }
#if 0
  void set(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  set(", i, ", ", v, ")");
    set_(i, v);
    if (m_debug) dump("POST set(", i, ", ", v, ")");
  }
#endif
  void set(uint64_t i, uint64_t v) {
    uint64_t j = ensure(i);
    m_data[j] |= (v<<((i & IndexMask)<<Shift));
  }
#if 0
  void clr(uint64_t i) {
    if (m_debug) dump("PRE  clr(", i, ")");
    clr_(i);
    if (m_debug) dump("POST clr(", i, ")");
  }
#endif
  void clr(uint64_t i) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= (m_size<<IndexShift)) return;
    if (m_data[index(i>>IndexShift)] &=
	~(Mask<<((i & IndexMask)<<Shift))) return;
    if (i <= IndexMask) {
      uint64_t j;
      uint64_t k = (m_tail>>IndexShift) - (m_head>>IndexShift);
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += (j<<IndexShift);
      }
    }
  }
#if 0
  void clr(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  clr(", i, ", ", v, ")");
    clr_(i, v);
    if (m_debug) dump("POST clr(", i, ", ", v, ")");
  }
#endif
  void clr(uint64_t i, uint64_t v) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= (m_size<<IndexShift)) return;
    if (m_data[index(i>>IndexShift)] &= ~(v<<((i & IndexMask)<<Shift))) return;
    if (i <= IndexMask) {
      uint64_t j;
      uint64_t k = (m_tail>>IndexShift) - (m_head>>IndexShift);
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += (j<<IndexShift);
      }
    }
  }

  uint64_t val(uint64_t i) const {
    if (i < m_head || i >= m_tail) return 0;
    i -= m_head;
    return (m_data[index(i>>IndexShift)]>>((i & IndexMask)<<Shift)) & Mask;
  }

  unsigned head() const { return m_head; }
  unsigned tail() const { return m_tail; }
  unsigned size() const { return m_size<<IndexShift; }

  // l(unsigned index, unsigned value) -> bool
  template <typename L>
  bool all(L &&l) {
    for (unsigned i = 0, n = m_size; i < n; i++) {
      uint64_t w = m_data[index(i)];
      if (!w) continue;
      uint64_t m = Mask;
      for (unsigned j = 0; j < (1U<<IndexShift); j++) {
	if (uint64_t v = (w & m))
	  if (!ZuFwd<L>(l)(m_head + (i<<IndexShift) + j, v>>(j<<Shift)))
	    return false;
	m <<= (1U<<Shift);
      }
    }
    return true;
  }

private:
  uint64_t	*m_data = nullptr;
  uint64_t	m_size = 0;
  uint64_t	m_head = 0;
  uint64_t	m_tail = 0;
  uint64_t	m_offset = 0;
  // bool	m_debug = false;
};

template <unsigned Bits_>
class ZtBitWindow<Bits_, true, false> : private ZmVHeap_<ZtBitWindow_ID> {
  ZtBitWindow(const ZtBitWindow &) = delete;
  ZtBitWindow &operator =(const ZtBitWindow &) = delete;

public:
  enum { Bits = Bits_ };

private:
  static constexpr uint64_t Mask = (static_cast<uint64_t>(1)<<Bits) - 1;
  enum { IndexMul = ZtBitWindow_<Bits>::Mul };

public:
  ZtBitWindow() { }
  ~ZtBitWindow() { if (m_data) vfree(m_data); }

  ZtBitWindow(ZtBitWindow &&q) :
      m_data(q.m_data), m_size(q.m_size),
      m_head(q.m_head), m_offset(q.m_offset) {
    q.null();
  }
  ZtBitWindow &operator =(ZtBitWindow &&q) {
    if (ZuLikely(this != &q)) {
      if (m_data) vfree(m_data);
      m_data = q.m_data;
      m_size = q.m_size;
      m_head = q.m_head;
      q.null();
    }
    return *this;
  }

  void null() {
    if (m_data) vfree(m_data);
    m_data = nullptr;
    m_size = 0;
    m_head = 0;
    m_offset = 0;
  }

#if 0
  void debug() { m_debug = true; }

  void dump_() { }
  template <typename Arg0, typename ...Args>
  void dump_(Arg0 &&arg0, Args &&...args) {
    std::cerr << ZuMv(arg0);
    dump_(ZuMv(args)...);
  }
  template <typename ...Args>
  void dump(Args &&...args) {
    dump_(ZuMv(args)...);
    std::cerr
      << " size=" << m_size
      << " head=" << m_head
      << " tail=" << m_tail
      << " offset=" << m_offset << '\n' << std::flush;
  }
#endif

private:
#if 0
  uint64_t ensure(uint64_t i) {
    if (m_debug) dump("PRE  ensure(", i, ")");
    uint64_t v = ensure_(i);
    if (m_debug) dump("POST ensure(", i, ")");
    return v;
  }
#endif
  uint64_t ensure(uint64_t i) {
    if (ZuUnlikely(!m_size))
      m_head = i - (i % IndexMul);
    else if (ZuUnlikely(i < m_head)) {
      uint64_t required = ((m_head - i) + (IndexMul - 1)) / IndexMul;
      {
	uint64_t avail = m_size -
	  ((m_tail + (IndexMul - 1)) / IndexMul - m_head / IndexMul);
	if (required <= avail) {
	  m_head -= required * IndexMul;
	  m_offset += (m_size - required);
	  if (m_offset >= m_size) m_offset -= m_size;
	  return 0;
	}
      }
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      memset(data, 0, required<<3);
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data + required, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + required + tailOffset, m_data, m_offset<<3);
      vfree(m_data);
      m_data = data;
      m_size += required;
      m_head -= required * IndexMul;
      m_offset = 0;
      return (i - m_head) / IndexMul;
    }
    if (i >= m_tail) m_tail = i + 1;
    i -= m_head;
    uint64_t size = m_size * IndexMul;
    if (ZuUnlikely(i >= size)) {
      uint64_t required = (((i + 1) - size) + (IndexMul - 1)) / IndexMul;
      if (required < (m_size>>3))
	required = m_size>>3; // grow by at least 12.5%
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + tailOffset, m_data, m_offset<<3);
      memset(data + m_size, 0, required<<3);
      vfree(m_data);
      m_data = data;
      m_size += required;
      m_offset = 0;
      return i / IndexMul;
    }
    return index(i / IndexMul);
  }
  uint64_t index(uint64_t i) const {
    i += m_offset;
    if (i >= m_size) i -= m_size;
    return i;
  }

public:
#if 0
  void set(uint64_t i) {
    if (m_debug) dump("PRE  set(", i, ")");
    set_(i);
    if (m_debug) dump("POST set(", i, ")");
  }
#endif
  void set(uint64_t i) {
    uint64_t j = ensure(i);
    m_data[j] |= (Mask<<((i % IndexMul) * Bits));
  }
#if 0
  void set(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  set(", i, ", ", v, ")");
    set_(i, v);
    if (m_debug) dump("POST set(", i, ", ", v, ")");
  }
#endif
  void set(uint64_t i, uint64_t v) {
    uint64_t j = ensure(i);
    m_data[j] |= (v<<((i % IndexMul) * Bits));
  }
#if 0
  void clr(uint64_t i) {
    if (m_debug) dump("PRE  clr(", i, ")");
    clr_(i);
    if (m_debug) dump("POST clr(", i, ")");
  }
#endif
  void clr(uint64_t i) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= m_size * IndexMul) return;
    if (m_data[index(i / IndexMul)] &=
	~(Mask<<((i % IndexMul) * Bits))) return;
    if (i < IndexMul) {
      uint64_t j;
      uint64_t k = (m_tail / IndexMul) - (m_head / IndexMul);
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += j * IndexMul;
      }
    }
  }
#if 0
  void clr(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  clr(", i, ", ", v, ")");
    clr_(i, v);
    if (m_debug) dump("POST clr(", i, ", ", v, ")");
  }
#endif
  void clr(uint64_t i, uint64_t v) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= m_size * IndexMul) return;
    if (m_data[index(i / IndexMul)] &= ~(v<<((i % IndexMul) * Bits))) return;
    if (i < IndexMul) {
      uint64_t j;
      uint64_t k = (m_tail / IndexMul) - (m_head / IndexMul);
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += j * IndexMul;
      }
    }
  }

  uint64_t val(uint64_t i) const {
    if (i < m_head || i >= m_tail) return 0;
    i -= m_head;
    if (i >= m_size * IndexMul) return 0;
    return (m_data[index(i / IndexMul)]>>((i % IndexMul) * Bits)) & Mask;
  }

  unsigned head() const { return m_head; }
  unsigned tail() const { return m_tail; }
  unsigned size() const { return m_size * IndexMul; }

  // l(unsigned index, unsigned value) -> bool
  template <typename L>
  bool all(L &&l) {
    for (unsigned i = 0, k = 0, n = m_size; i < n; i++) {
      uint64_t w = m_data[index(i)];
      if (!w) continue;
      uint64_t m = Mask;
      for (unsigned j = 0, z = 0; j < IndexMul; j++, k++, z += Bits) {
	if (uint64_t v = (w & m))
	  if (!ZuFwd<L>(l)(m_head + k, v>>z)) return false;
	m <<= Bits;
      }
    }
    return true;
  }

private:
  uint64_t	*m_data = nullptr;
  uint64_t	m_size = 0;
  uint64_t	m_head = 0;
  uint64_t	m_tail = 0;
  uint64_t	m_offset = 0;
  // bool	m_debug = false;
};

template <>
class ZtBitWindow<64U, true, true> : private ZmVHeap_<ZtBitWindow_ID> {
  ZtBitWindow(const ZtBitWindow &) = delete;
  ZtBitWindow &operator =(const ZtBitWindow &) = delete;

public:
  enum { Bits = 64 };

public:
  ZtBitWindow() { }
  ~ZtBitWindow() { if (m_data) vfree(m_data); }

  ZtBitWindow(ZtBitWindow &&q) :
      m_data(q.m_data), m_size(q.m_size),
      m_head(q.m_head), m_offset(q.m_offset) {
    q.null();
  }
  ZtBitWindow &operator =(ZtBitWindow &&q) {
    if (ZuLikely(this != &q)) {
      if (m_data) vfree(m_data);
      m_data = q.m_data;
      m_size = q.m_size;
      m_head = q.m_head;
      q.null();
    }
    return *this;
  }

  void null() {
    if (m_data) vfree(m_data);
    m_data = nullptr;
    m_size = 0;
    m_head = 0;
    m_offset = 0;
  }

#if 0
  void debug() { m_debug = true; }

  void dump_() { }
  template <typename Arg0, typename ...Args>
  void dump_(Arg0 &&arg0, Args &&...args) {
    std::cerr << ZuMv(arg0);
    dump_(ZuMv(args)...);
  }
  template <typename ...Args>
  void dump(Args &&...args) {
    dump_(ZuMv(args)...);
    std::cerr
      << " size=" << m_size
      << " head=" << m_head
      << " tail=" << m_tail
      << " offset=" << m_offset << '\n' << std::flush;
  }
#endif

private:
#if 0
  uint64_t ensure(uint64_t i) {
    if (m_debug) dump("PRE  ensure(", i, ")");
    uint64_t v = ensure_(i);
    if (m_debug) dump("POST ensure(", i, ")");
    return v;
  }
#endif
  uint64_t ensure(uint64_t i) {
    if (ZuUnlikely(!m_size))
      m_head = i;
    else if (ZuUnlikely(i < m_head)) {
      uint64_t required = m_head - i;
      {
	uint64_t avail = m_size - (m_tail - m_head);
	if (required <= avail) {
	  m_head -= required;
	  m_offset += (m_size - required);
	  if (m_offset >= m_size) m_offset -= m_size;
	  return 0;
	}
      }
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      memset(data, 0, required<<3);
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data + required, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + required + tailOffset, m_data, m_offset<<3);
      if (m_data) vfree(m_data);
      m_data = data;
      m_size += required;
      m_head -= required;
      m_offset = 0;
      return i - m_head;
    }
    if (i >= m_tail) m_tail = i + 1;
    i -= m_head;
    if (ZuUnlikely(i >= m_size)) {
      uint64_t required = (i + 1) - m_size;
      if (required < (m_size>>3))
	required = m_size>>3; // grow by at least 12.5%
      auto data = static_cast<uint64_t *>(valloc((m_size + required)<<3));
      uint64_t tailOffset = m_size - m_offset;
      if (tailOffset) memcpy(data, m_data + m_offset, tailOffset<<3);
      if (m_offset) memcpy(data + tailOffset, m_data, m_offset<<3);
      memset(data + m_size, 0, required<<3);
      if (m_data) vfree(m_data);
      m_data = data;
      m_size += required;
      m_offset = 0;
      return i;
    }
    return index(i);
  }
  uint64_t index(uint64_t i) const {
    i += m_offset;
    if (i >= m_size) i -= m_size;
    return i;
  }

public:
#if 0
  void set(uint64_t i) {
    if (m_debug) dump("PRE  set(", i, ")");
    set_(i);
    if (m_debug) dump("POST set(", i, ")");
  }
#endif
  void set(uint64_t i) {
    uint64_t j = ensure(i);
    m_data[j] = ~static_cast<uint64_t>(0);
  }
#if 0
  void set(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  set(", i, ", ", v, ")");
    set_(i, v);
    if (m_debug) dump("POST set(", i, ", ", v, ")");
  }
#endif
  void set(uint64_t i, uint64_t v) {
    uint64_t j = ensure(i);
    m_data[j] = v;
  }
#if 0
  void clr(uint64_t i) {
    if (m_debug) dump("PRE  clr(", i, ")");
    clr_(i);
    if (m_debug) dump("POST clr(", i, ")");
  }
#endif
  void clr(uint64_t i) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= m_size) return;
    m_data[index(i)] = 0;
    if (!i) {
      uint64_t j;
      uint64_t k = m_tail - m_head;
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += j;
      }
    }
  }
#if 0
  void clr(uint64_t i, uint64_t v) {
    if (m_debug) dump("PRE  clr(", i, ", ", v, ")");
    clr_(i, v);
    if (m_debug) dump("POST clr(", i, ", ", v, ")");
  }
#endif
  void clr(uint64_t i, uint64_t v) {
    if (i < m_head) return;
    i -= m_head;
    if (i >= m_size) return;
    m_data[index(i)] = 0;
    if (!i) {
      uint64_t j;
      uint64_t k = m_tail - m_head;
      for (j = 0; j < k && !m_data[index(j)]; j++);
      if (j) {
	if (j < k)
	  if ((m_offset += j) >= m_size) m_offset -= m_size;
	m_head += j;
      }
    }
  }

  uint64_t val(uint64_t i) const {
    if (i < m_head) return 0;
    i -= m_head;
    if (i >= m_size) return 0;
    return m_data[index(i)];
  }

  unsigned head() const { return m_head; }
  unsigned tail() const { return m_tail; }
  unsigned size() const { return m_size; }

  // l(unsigned index, unsigned value) -> bool
  template <typename L>
  bool all(L &&l) {
    for (unsigned i = 0, n = m_size; i < n; i++) {
      uint64_t v = m_data[index(i)];
      if (!v) continue;
      if (!ZuFwd<L>(l)(m_head + i, v)) return false;
    }
    return true;
  }

private:
  uint64_t	*m_data = nullptr;
  uint64_t	m_size = 0;
  uint64_t	m_head = 0;
  uint64_t	m_tail = 0;
  uint64_t	m_offset = 0;
  // bool	m_debug = false;
};

#endif /* ZtBitWindow_HH */
