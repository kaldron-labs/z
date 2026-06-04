//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bit streams (little-endian and big-endian)
// - successive written symbols are not padded to be byte-aligned
// - little-endian aligns with x86

#ifndef ZuBitStream_HH
#define ZuBitStream_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTuple.hh>

namespace ZuBitStream {

ZuInline constexpr uint8_t mask_(unsigned bits) {
  return ~(uint8_t(0xff)<<bits);
}

ZuInline constexpr uint64_t mask64_(unsigned bits) {
  return bits >= 64 ? ~uint64_t(0) : ~((~uint64_t(0))<<bits);
}

namespace LE {

class Reader {
public:
  Reader() = default;
  Reader(const Reader &) = default;
  Reader &operator =(const Reader &) = default;
  Reader(Reader &&) = default;
  Reader &operator =(Reader &&) = default;

  Reader(const uint8_t *start, const uint8_t *end) noexcept :
    m_pos{start}, m_end{end} { }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  const uint8_t *pos() const { return m_pos; }
  const uint8_t *end() const { return m_end; }
  unsigned inBits() const { return m_inBits; }

  // save input state
  ZuTuple<const uint8_t *, unsigned> save() const {
    return {m_pos, m_inBits};
  }

  // restore input state
  void load(const ZuTuple<const uint8_t *, unsigned> &saved) {
    m_pos = saved.p<0>();
    m_inBits = saved.p<1>();
  }

  // 0 <= Bits < 8
  template <unsigned Bits>
  bool avail() {
    return m_pos + ((m_inBits + Bits + 7)>>3) <= m_end;
  }
  // 0 <= bits < 64
  bool avail(unsigned bits) {
    return m_pos + ((m_inBits + bits + 7)>>3) <= m_end;
  }

  // 0 <= Bits < 8
  template <unsigned Bits>
  uint8_t in() {
    uint8_t v;
    if (ZuUnlikely(!m_inBits)) {
      m_inBits = Bits;
      v = (*m_pos) & mask_(Bits);
    } else {
      unsigned bits;
      unsigned lbits = 8 - m_inBits;
      if (ZuUnlikely(Bits < lbits)) {
	lbits = Bits;
	v = ((*m_pos)>>m_inBits) & mask_(Bits);
      } else
	v = (*m_pos)>>m_inBits;
      if ((m_inBits += lbits) >= 8) { m_pos++; m_inBits = 0; }
      if (bits = Bits - lbits) {
	// m_inBits must be zero here, and bits is < 8
	v |= ((*m_pos) & mask_(bits))<<lbits;
	m_inBits = bits;
      }
    }
    return v;
  }
  // 0 <= bits < 64
  uint64_t in(unsigned bits) {
    uint64_t v = 0;
    unsigned lbits = 0; // "head" bits
    if (ZuLikely(m_inBits > 0)) {
      lbits = 8 - m_inBits;
      if (ZuUnlikely(bits < lbits)) {
	lbits = bits;
	v = ((*m_pos)>>m_inBits) & mask_(lbits);
      } else
	v = (*m_pos)>>m_inBits;
      if ((m_inBits += lbits) >= 8) { m_pos++; m_inBits = 0; }
      if (!(bits -= lbits)) return v;
      v <<= (64 - lbits);
    }
    switch (bits>>3) {
      case 8: v = uint64_t(*m_pos++)<<56;
      case 7: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 6: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 5: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 4: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 3: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 2: v = (v>>8) | (uint64_t(*m_pos++)<<56);
      case 1: v = (v>>8) | (uint64_t(*m_pos++)<<56);
    }
    unsigned hbits = bits & 7;
    bits -= hbits;
    v >>= (64 - (bits + lbits));
    if (hbits) {
      m_inBits = hbits;
      v |= uint64_t((*m_pos) & mask_(hbits))<<(bits + lbits);
    }
    return v;
  }

  void extend(const uint8_t *end) { m_end = end; }

private:
  const uint8_t	*m_pos = nullptr;
  const uint8_t	*m_end = nullptr;
  unsigned	m_inBits = 0;
};

class Writer {
  Writer(const Writer &) = delete;
  Writer &operator =(const Writer &) = delete;

public:
  Writer() = default;

  Writer(uint8_t *start, uint8_t *end) noexcept :
    m_pos{start}, m_end{end} { }

  Writer(Writer &&w) noexcept :
    m_pos{w.m_pos}, m_end{w.m_end}, m_outBits{w.m_outBits}
  {
    w.m_pos = nullptr;
    w.m_end = nullptr;
    w.m_outBits = 0;
  }
  Writer &operator =(Writer &&w) noexcept {
    if (ZuLikely(this != &w)) {
      this->~Writer(); // nop
      new (this) Writer{ZuMv(w)};
    }
    return *this;
  }

  Writer(const Reader &r, uint8_t *end) noexcept :
    m_pos{const_cast<uint8_t *>(r.pos())}, m_end{end}
  {
    m_outBits = r.inBits();
  }

  uint8_t *pos() const { return m_pos; }
  uint8_t *end() const { return m_end; }
  unsigned outBits() const { return m_outBits; }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  // 0 <= Bits <= 8
  template <unsigned Bits>
  bool avail() {
    return m_pos + ((m_outBits + Bits + 7)>>3) <= m_end;
  }
  // 0 <= bits <= 64
  bool avail(unsigned bits) {
    return m_pos + ((m_outBits + bits + 7)>>3) <= m_end;
  }

  // 0 <= Bits <= 8
  template <unsigned Bits>
  void out(uint8_t v) {
    if (ZuUnlikely(m_outBits == 0)) {
      m_outBits = Bits;
      *m_pos = v;
      return;
    }
    *m_pos |= (v<<m_outBits);
    unsigned lbits = 8 - m_outBits;
    if (ZuUnlikely(Bits < lbits)) lbits = Bits;
    if ((m_outBits += lbits) >= 8) {
      m_pos++;
      m_outBits = 0;
    }
    if (uint8_t bits = Bits - lbits) {
      v >>= lbits;
      m_outBits = bits;
      *m_pos = v;
    }
  }
  // 0 <= bits <= 64
  void out(uint64_t v, unsigned bits) {
    if (ZuLikely(m_outBits > 0)) {
      *m_pos |= (v<<m_outBits);
      unsigned lbits = 8 - m_outBits;
      if (ZuUnlikely(bits < lbits)) lbits = bits;
      if ((m_outBits += lbits) >= 8) {
	m_pos++;
	m_outBits = 0;
      }
      v >>= lbits;
      if (!(bits -= lbits)) return;
    }
    switch (bits>>3) {
      case 8: *m_pos++ = v; v >>= 8;
      case 7: *m_pos++ = v; v >>= 8;
      case 6: *m_pos++ = v; v >>= 8;
      case 5: *m_pos++ = v; v >>= 8;
      case 4: *m_pos++ = v; v >>= 8;
      case 3: *m_pos++ = v; v >>= 8;
      case 2: *m_pos++ = v; v >>= 8;
      case 1: *m_pos++ = v; v >>= 8;
    }
    bits &= 7;
    if (bits) {
      m_outBits = bits;
      *m_pos = v;
    }
  }

  void finish() {
    if (ZuLikely(m_pos < m_end && m_outBits)) {
      ++m_pos;
      m_outBits = 0;
    }
    m_end = m_pos; // prevent further writing
  }

private:
  uint8_t	*m_pos = nullptr;
  uint8_t	*m_end = nullptr;
  unsigned	m_outBits = 0;	// output bits
};

} // LE

namespace BE {

class Reader {
public:
  Reader() = default;
  Reader(const Reader &) = default;
  Reader &operator =(const Reader &) = default;
  Reader(Reader &&) = default;
  Reader &operator =(Reader &&) = default;

  Reader(const uint8_t *start, const uint8_t *end) noexcept :
    m_pos{start}, m_end{end} { }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  const uint8_t *pos() const { return m_pos; }
  const uint8_t *end() const { return m_end; }
  unsigned inBits() const { return m_inBits; }

  // save input state
  ZuTuple<const uint8_t *, unsigned> save() const {
    return {m_pos, m_inBits};
  }

  // restore input state
  void load(const ZuTuple<const uint8_t *, unsigned> &saved) {
    m_pos = saved.p<0>();
    m_inBits = saved.p<1>();
  }

  // 0 <= Bits <= 8
  template <unsigned Bits>
  bool avail() {
    return m_pos + ((m_inBits + Bits + 7)>>3) <= m_end;
  }
  // 0 <= bits <= 64
  bool avail(unsigned bits) {
    return m_pos + ((m_inBits + bits + 7)>>3) <= m_end;
  }

  // 0 <= Bits <= 8
  template <unsigned Bits>
  uint8_t in() {
    if constexpr (!Bits) return 0;
    uint8_t v;
    if (ZuUnlikely(!m_inBits)) {
      m_inBits = Bits;
      v = (*m_pos) >> (8 - Bits);
    } else {
      unsigned bits;
      unsigned lbits = 8 - m_inBits;
      if (ZuUnlikely(Bits < lbits)) lbits = Bits;
      v = ((*m_pos) >> (8 - m_inBits - lbits)) & mask_(lbits);
      if ((m_inBits += lbits) >= 8) { m_pos++; m_inBits = 0; }
      if (bits = Bits - lbits) {
	v = (v << bits) | ((*m_pos) >> (8 - bits));
	m_inBits = bits;
      }
    }
    return v;
  }
  // 0 <= bits <= 64
  uint64_t in(unsigned bits) {
    if (ZuUnlikely(!bits)) return 0;
    uint64_t v = 0;
    if (ZuLikely(m_inBits > 0)) {
      unsigned lbits = 8 - m_inBits;
      if (ZuUnlikely(bits < lbits)) lbits = bits;
      v = ((*m_pos) >> (8 - m_inBits - lbits)) & mask_(lbits);
      if ((m_inBits += lbits) >= 8) { m_pos++; m_inBits = 0; }
      if (!(bits -= lbits)) return v;
    }
    switch (bits>>3) {
      case 8: v = (v<<8) | *m_pos++;
      case 7: v = (v<<8) | *m_pos++;
      case 6: v = (v<<8) | *m_pos++;
      case 5: v = (v<<8) | *m_pos++;
      case 4: v = (v<<8) | *m_pos++;
      case 3: v = (v<<8) | *m_pos++;
      case 2: v = (v<<8) | *m_pos++;
      case 1: v = (v<<8) | *m_pos++;
    }
    unsigned hbits = bits & 7;
    if (hbits) {
      m_inBits = hbits;
      v = (v << hbits) | ((*m_pos) >> (8 - hbits));
    }
    return v;
  }

  void extend(const uint8_t *end) { m_end = end; }

private:
  const uint8_t	*m_pos = nullptr;
  const uint8_t	*m_end = nullptr;
  unsigned	m_inBits = 0;
};

class Writer {
  Writer(const Writer &) = delete;
  Writer &operator =(const Writer &) = delete;

public:
  Writer() = default;

  Writer(uint8_t *start, uint8_t *end) noexcept :
    m_pos{start}, m_end{end} { }

  Writer(Writer &&w) noexcept :
    m_pos{w.m_pos}, m_end{w.m_end}, m_outBits{w.m_outBits}
  {
    w.m_pos = nullptr;
    w.m_end = nullptr;
    w.m_outBits = 0;
  }
  Writer &operator =(Writer &&w) noexcept {
    if (ZuLikely(this != &w)) {
      this->~Writer(); // nop
      new (this) Writer{ZuMv(w)};
    }
    return *this;
  }

  Writer(const Reader &r, uint8_t *end) noexcept :
    m_pos{const_cast<uint8_t *>(r.pos())}, m_end{end}
  {
    m_outBits = r.inBits();
  }

  uint8_t *pos() const { return m_pos; }
  uint8_t *end() const { return m_end; }
  unsigned outBits() const { return m_outBits; }

  bool operator !() const { return !m_pos; }
  ZuOpBool

  // 0 <= Bits <= 8
  template <unsigned Bits>
  bool avail() {
    return m_pos + ((m_outBits + Bits + 7)>>3) <= m_end;
  }
  // 0 <= bits <= 64
  bool avail(unsigned bits) {
    return m_pos + ((m_outBits + bits + 7)>>3) <= m_end;
  }

  // 0 <= Bits <= 8
  template <unsigned Bits>
  void out(uint8_t v) {
    if constexpr (!Bits) return;
    if (ZuUnlikely(m_outBits == 0)) {
      m_outBits = Bits;
      *m_pos = v << (8 - Bits);
      return;
    }
    unsigned bits = Bits;
    unsigned lbits = 8 - m_outBits;
    if (ZuUnlikely(Bits < lbits)) lbits = Bits;
    *m_pos |= (v >> (bits - lbits)) << (8 - m_outBits - lbits);
    if ((m_outBits += lbits) >= 8) {
      m_pos++;
      m_outBits = 0;
    }
    if (bits -= lbits) {
      m_outBits = bits;
      *m_pos = v << (8 - bits);
    }
  }
  // 0 <= bits <= 64
  void out(uint64_t v, unsigned bits) {
    if (ZuUnlikely(!bits)) return;
    if (ZuLikely(m_outBits > 0)) {
      unsigned lbits = 8 - m_outBits;
      if (ZuUnlikely(bits < lbits)) lbits = bits;
      *m_pos |= (v >> (bits - lbits)) << (8 - m_outBits - lbits);
      if ((m_outBits += lbits) >= 8) {
	m_pos++;
	m_outBits = 0;
      }
      if (!(bits -= lbits)) return;
    }
    switch (bits>>3) {
      case 8: *m_pos++ = v >> (bits -= 8);
      case 7: *m_pos++ = v >> (bits -= 8);
      case 6: *m_pos++ = v >> (bits -= 8);
      case 5: *m_pos++ = v >> (bits -= 8);
      case 4: *m_pos++ = v >> (bits -= 8);
      case 3: *m_pos++ = v >> (bits -= 8);
      case 2: *m_pos++ = v >> (bits -= 8);
      case 1: *m_pos++ = v >> (bits -= 8);
    }
    if (bits) {
      m_outBits = bits;
      *m_pos = v << (8 - bits);
    }
  }

  void finish() {
    if (ZuLikely(m_pos < m_end && m_outBits)) {
      ++m_pos;
      m_outBits = 0;
    }
    m_end = m_pos; // prevent further writing
  }

private:
  uint8_t	*m_pos = nullptr;
  uint8_t	*m_end = nullptr;
  unsigned	m_outBits = 0;	// output bits
};

} // BE

} // ZuBitStream

#endif /* ZuBitStream_HH */
