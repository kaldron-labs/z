//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// fast UTF-8/16/32 conversion

#ifndef ZuUTF_HH
#define ZuUTF_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuInt.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

struct ZuUTF8 {
  using Elem = uint8_t;

  static bool initial(uint8_t c) {
    return c < 0x80 || c >= 0xc0;
  }

  static unsigned in(uint8_t c) {
    if (ZuLikely(c < 0x80)) return 1;
    if (ZuLikely((c>>5) == 0x6)) return 2;
    if (ZuLikely((c>>4) == 0xe)) return 3;
    if (ZuLikely((c>>3U) == 0x1e)) return 4;
    return 0;
  }

  static unsigned in(const uint8_t *s, unsigned n) {
    if (ZuUnlikely(n < 1)) return 0;
    uint8_t c = *s;
    if (ZuLikely(c < 0x80)) return 1;
    if (ZuUnlikely(n < 2)) return 0;
    if (ZuLikely((c>>5) == 0x6)) return 2;
    if (ZuUnlikely(n < 3)) return 0;
    if (ZuLikely((c>>4) == 0xe)) return 3;
    if (ZuUnlikely(n < 4)) return 0;
    if (ZuLikely((c>>3U) == 0x1e)) return 4;
    return 0;
  }

  static unsigned in(const uint8_t *s, unsigned n, uint32_t &u_) {
    if (ZuUnlikely(n < 1)) return 0;
    uint8_t c = *s;
    if (ZuLikely(c < 0x80)) {
      u_ = c;
      return 1;
    }
    if (ZuUnlikely(n < 2)) return 0;
    uint32_t u = c;
    if (ZuLikely((c>>5) == 0x6)) {
      c = *++s;
      u_ = ((u<<6) & 0x7ff) + (c & 0x3f);
      return 2;
    }
    if (ZuUnlikely(n < 3)) return 0;
    if (ZuLikely((c>>4) == 0xe)) {
      c = *++s;
      u = ((u<<12) & 0xf000) + ((uint32_t(c)<<6) & 0xfc0);
      c = *++s;
      u_ = u + (c & 0x3f);
      return 3;
    }
    if (ZuUnlikely(n < 4)) return 0;
    if (ZuLikely((c>>3U) == 0x1e)) {
      c = *++s;
      u = ((u<<18) & 0x1c0000) + ((uint32_t(c)<<12) & 0x3f000);
      c = *++s;
      u += ((uint32_t(c)<<6) & 0xfc0);
      c = *++s;
      u_ = u + (c & 0x3f);
      return 4;
    }
    return 0;
  }

  static unsigned out(uint32_t u) {
    if (ZuLikely(u < 0x80)) return 1;
    if (ZuLikely(u < 0x800)) return 2;
    if (ZuLikely(u < 0x10000)) return 3;
    return 4;
  }

  static unsigned out(uint8_t *s, unsigned n, uint32_t u) {
    if (ZuUnlikely(n < 1)) return 0;
    if (ZuLikely(u < 0x80)) {
      *s = u;
      return 1;
    }
    if (ZuUnlikely(n < 2)) return 0;
    if (ZuLikely(u < 0x800)) {
      *s++ = (u>>6) | 0xc0;
      *s = (u & 0x3f) | 0x80;
      return 2;
    }
    if (ZuUnlikely(n < 3)) return 0;
    if (ZuLikely(u < 0x10000)) {
      *s++ = (u>>12) | 0xe0;
      *s++ = ((u>>6) & 0x3f) | 0x80;
      *s = (u & 0x3f) | 0x80;
      return 3;
    }
    if (ZuUnlikely(n < 4)) return 0;
    *s++ = (u>>18) | 0xf0;
    *s++ = ((u>>12) & 0x3f) | 0x80;
    *s++ = ((u>>6) & 0x3f) | 0x80;
    *s = (u & 0x3f) | 0x80;
    return 4;
  }
};

struct ZuUTF16 {
  using Elem = uint16_t;

  static bool initial(uint16_t c) {
    return c < 0xdc00 || c >= 0xe000;
  }

  static unsigned in(uint16_t c) {
    if (ZuLikely(c < 0xd800 || c >= 0xe000)) return 1;
    if (ZuUnlikely(c >= 0xdc00)) return 0;
    return 2;
  }

  template <typename C>
  static unsigned in(const C *s, unsigned n) {
    if (ZuUnlikely(n < 1)) return 0;
    uint16_t c = uint16_t(*s);
    if (ZuLikely(c < 0xd800 || c >= 0xe000)) return 1;
    if (ZuUnlikely(n < 2 || c >= 0xdc00)) return 0;
    c = uint16_t(*++s);
    if (ZuUnlikely(c < 0xdc00 || c >= 0xe000)) return 0;
    return 2;
  }

  template <typename C>
  static unsigned in(const C *s, unsigned n, uint32_t &u_) {
    if (ZuUnlikely(n < 1)) return 0;
    uint16_t c = uint16_t(*s);
    if (ZuLikely(c < 0xd800 || c >= 0xe000)) {
      u_ = c;
      return 1;
    }
    if (ZuUnlikely(n < 2 || c >= 0xdc00)) return 0;
    uint32_t u = c;
    c = uint16_t(*++s);
    if (ZuUnlikely(c < 0xdc00 || c >= 0xe000)) return 0;
    u_ = (((u - 0xd800)<<10) | 0x10000) + (uint32_t(c) - 0xdc00);
    return 2;
  }

  static unsigned out(uint32_t u) {
    if (ZuLikely(u < 0xd800 || (u >= 0xe000 && u < 0x10000))) return 1;
    return 2;
  }

  template <typename C>
  static unsigned out(C *s, unsigned n, uint32_t u) {
    if (ZuUnlikely(n < 1)) return 0;
    if (ZuLikely(u < 0xd800 || (u >= 0xe000 && u < 0x10000))) {
      *s = C(u);
      return 1;
    }
    if (ZuUnlikely(n < 2)) return 0;
    *s++ = C(((u & 0xffff)>>10) + 0xd800);
    *s = C((u & 0x3ff) + 0xdc00);
    return 2;
  }
};

struct ZuAPI ZuUTF32 {
  using Elem = uint32_t;

  template <typename C>
  static unsigned in(const C *s, unsigned n, uint32_t &u) {
    if (ZuUnlikely(n < 1)) return 0;
    u = uint32_t(*s);
    return 1;
  }

  static constexpr unsigned out(uint32_t) { return 1; }

  template <typename C>
  static unsigned out(C *s, unsigned n, uint32_t u) {
    if (ZuUnlikely(n < 1)) return 0;
    *s = C(u);
    return 1;
  }

  // http://www.unicode.org/reports/tr11/tr11-36.html
  // http://www.unicode.org/Public/12.0.0/ucd/EastAsianWidth.txt
  static unsigned width(uint32_t); // 1 for normal, 2 for full-width
};

template <unsigned> struct ZuUTF_;
template <> struct ZuUTF_<1> { using T = ZuUTF8; };
template <> struct ZuUTF_<2> { using T = ZuUTF16; };
template <> struct ZuUTF_<4> { using T = ZuUTF32; };

// encodes an input length, an output length and a display width into 64bits
class ZuUTFSpan {
  static constexpr unsigned shift() { return 21; }
  static constexpr unsigned mask() { return ((1<<shift()) - 1); }

public:
  ZuUTFSpan() = default;
  ZuUTFSpan(const ZuUTFSpan &) = default;
  ZuUTFSpan &operator =(const ZuUTFSpan &) = default;
  ZuUTFSpan(ZuUTFSpan &&) = default;
  ZuUTFSpan &operator =(ZuUTFSpan &&) = default;
  ~ZuUTFSpan() = default;

  ZuUTFSpan(uint64_t inLen, uint64_t outLen, uint64_t width) :
      m_value{inLen | (outLen<<shift()) | (width<<(shift()<<1))} { }

private:
  explicit ZuUTFSpan(uint64_t value) : m_value{value} { }

public:
  unsigned inLen() const { return m_value & mask(); }
  unsigned outLen() const { return (m_value>>shift()) & mask(); }
  unsigned width() const { return m_value>>(shift()<<1); }

  bool operator !() const { return !m_value; }
  ZuOpBool

  ZuUTFSpan operator +(const ZuUTFSpan &o) {
    return ZuUTFSpan(m_value + o.m_value);
  }
  ZuUTFSpan &operator +=(const ZuUTFSpan &o) {
    m_value += o.m_value;
    return *this;
  }
  ZuUTFSpan operator -(const ZuUTFSpan &o) {
    if (m_value <= o.m_value) return ZuUTFSpan();
    return ZuUTFSpan(m_value - o.m_value);
  }
  ZuUTFSpan &operator -=(const ZuUTFSpan &o) {
    if (m_value <= o.m_value)
      m_value = 0;
    else
      m_value -= o.m_value;
    return *this;
  }

  // shorthand for += ZuUTFSpan(1, 1, 1);
  ZuUTFSpan &operator ++() {
    m_value += ZuUTFSpan(1, 1, 1).m_value;
    return *this;
  }

private:
  uint64_t	m_value = 0;
};

template <typename OutChar, typename InChar> struct ZuUTF {
  using OutUTF = typename ZuUTF_<sizeof(OutChar)>::T;
  using OutElem = typename OutUTF::Elem;
  using InUTF = typename ZuUTF_<sizeof(InChar)>::T;
  using InElem = typename InUTF::Elem;

  using Span = ZuUTFSpan;

private:
  static auto inData_(const InChar *s) {
    if constexpr (sizeof(InChar) == 1)
      return reinterpret_cast<const InElem *>(s);
    else
      return s;
  }
  static auto outData_(OutChar *s) {
    if constexpr (sizeof(OutChar) == 1)
      return reinterpret_cast<OutElem *>(s);
    else
      return s;
  }

public:
  static Span span(ZuSpan<const InChar> s_) {
    auto s = inData_(s_.data());
    unsigned n = s_.length();
    uint32_t u;
    unsigned l = 0;
    unsigned w = 0;
    unsigned i = n;
    while (i) {
      unsigned j = InUTF::in(s, i, u);
      if (ZuUnlikely(!j || j > i)) break;
      s += j, i -= j;
      l += OutUTF::out(u);
      w += ZuUTF32::width(u);
    }
    return Span(n - i, l, w);
  }

  static Span nspan(ZuSpan<const InChar> s_, unsigned nglyphs) {
    auto s = inData_(s_.data());
    unsigned n = s_.length();
    uint32_t u;
    unsigned l = 0;
    unsigned w = 0;
    unsigned i = n;
    while (i && nglyphs) {
      unsigned j = InUTF::in(s, i, u);
      if (ZuUnlikely(!j || j > i)) break;
      s += j, i -= j;
      l += OutUTF::out(u);
      w += ZuUTF32::width(u);
      --nglyphs;
    }
    return Span(n - i, l, w);
  }

  static Span gspan(ZuSpan<const InChar> s_) { // single glyph
    auto s = inData_(s_.data());
    unsigned n = s_.length();
    uint32_t u;
    if (ZuUnlikely(!n)) return Span();
    unsigned j = InUTF::in(s, n, u);
    if (ZuUnlikely(!j || j > n)) return Span();
    return Span(j, OutUTF::out(u), ZuUTF32::width(u));
  }

  static uint64_t len(ZuSpan<const InChar> s_) {
    auto s = inData_(s_.data());
    uint64_t n = s_.length();
    uint32_t u;
    uint64_t l = 0;
    for (uint64_t i = n; i; ) {
      unsigned j = InUTF::in(s, i, u);
      if (ZuUnlikely(!j || j > i)) break;
      s += j, i -= j;
      l += OutUTF::out(u);
    }
    return l;
  }

  // conversion with silent truncation
  static uint64_t cvt(ZuSpan<OutChar> o_, ZuSpan<const InChar> s_) {
    auto o = outData_(o_.data());
    uint64_t l = o_.length();
    auto s = inData_(s_.data());
    uint64_t n = s_.length();
    uint32_t u;
    for (uint64_t i = n; i; ) {
      unsigned j = InUTF::in(s, i, u);
      if (ZuUnlikely(!j || j > i)) break;
      s += j, i -= j;
      j = OutUTF::out(o, l, u);
      if (!j) break;
      o += j, l -= j;
    }
    return o_.length() - l;
  }

  // conversion with overflow detection
  static ZuTuple<uint64_t, bool>
  cvt_overflow(ZuSpan<OutChar> o_, ZuSpan<const InChar> s_) {
    auto o = outData_(o_.data());
    uint64_t l = o_.length();
    auto s = inData_(s_.data());
    uint64_t n = s_.length();
    uint32_t u;
    bool overflow = false;
    for (uint64_t i = n; i; ) {
      unsigned j = InUTF::in(s, i, u);
      if (ZuUnlikely(!j || j > i)) break;
      s += j, i -= j;
      j = OutUTF::out(o, l, u);
      if (!j) { overflow = true; break; }
      o += j, l -= j;
    }
    return {o_.length() - l, overflow};
  }
};

#endif /* ZuUTF_HH */
