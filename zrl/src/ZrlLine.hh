//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Zrl::Line encapsulates a wrapped left-to-right UTF8 mono-spaced line
// of text displayed on a terminal, comprised of variable-width glyphs.
// Each glyph is either regular width, i.e. 1 display position, or full-width,
// i.e. 2 display positions. The line wraps around the display width and
// is re-flowed such that full-width glyphs are always intact on a line.

#ifndef ZrlLine_HH
#define ZrlLine_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <ZtArray.hh>

namespace Zrl {

ZuDerive(LineData, (ZtArray<uint8_t, ZtArrayHeapID<"Zrl.Line">>));

class ZrlAPI Line {
public:
  // encodes a 28bit index, padding, an offset and a length into 32bits
  class Index {
  public:
    Index() = default;
    Index(const Index &) = default;
    Index &operator =(const Index &) = default;
    Index(Index &&) = default;
    Index &operator =(Index &&) = default;
    ~Index() = default;

    Index(unsigned index, unsigned len, unsigned off) :
	m_value{(index<<5) | ((len - 1)<<3) | (off<<1)} { }
    Index(unsigned index, unsigned len, unsigned off, bool padding) :
	m_value{(index<<5) | ((len - 1)<<3) | (off<<1) | padding} { }

    bool operator !() const { return (m_value & 0x1f) == 4; }
    ZuOpBool

    // mapping() maps display positions <-> byte offsets
    unsigned mapping() const { return m_value>>5; }
    // len() returns the number of elements (bytes, positions) within the glyph
    unsigned len() const { return ((m_value>>3) & 0x3) + 1; }
    // off() returns the offset of this element within the glyph
    unsigned off() const { return (m_value>>1) & 0x3; }
    // padding is only used for indexing display positions, to indicate
    // empty display positions at the right edge due to wrapping
    // full-width glyphs around to the next row; padding is unused
    // by UTF8 byte data indices
    bool padding() const { return m_value & 0x1; }

  private:
    uint32_t	m_value = 4; // sentinel null value
  };
  ZuDerive(IndexData, (ZtArray<Index, ZtArrayHeapID<"Zrl.Index">>));

  void clear();

  LineData &data() { return m_data; }
  const LineData &data() const { return m_data; }

  // length in bytes
  unsigned length() const { return m_bytes.length(); }
  // width in display positions
  unsigned width() const { return m_positions.length(); }

  // substring
  ZuCSpan substr(unsigned off, unsigned len) const {
    return {reinterpret_cast<const char *>(&m_data[off]), len};
  }

  // byte offset -> display position
  Index byte(unsigned off) const;

  // display position -> byte offset
  Index position(unsigned pos) const;

  // align display position to left-side of glyph (if glyph is full-width)
  unsigned align(unsigned pos) const;

  // glyph-based motions - given origin byte offset, returns destination
  unsigned fwdGlyph(unsigned off) const; // forward glyph
  unsigned revGlyph(unsigned off) const; // backup glyph
  unsigned fwdWord(unsigned off) const; // forward word
  unsigned revWord(unsigned off) const; // backup word
  unsigned fwdWordEnd(unsigned off, bool past) const; // forward to end ''
  unsigned revWordEnd(unsigned off, bool past) const; // backup to end ''
  unsigned fwdUnixWord(unsigned off) const; // forward "Unix" word
  unsigned revUnixWord(unsigned off) const; // backup ''
  unsigned fwdUnixWordEnd(unsigned off, bool past) const; // forward to end ''
  unsigned revUnixWordEnd(unsigned off, bool past) const; // backup to end ''

  // glyph search within line - returns (adjusted) origin if not found
  unsigned fwdSearch(unsigned off, uint32_t glyph) const; // forward search
  unsigned revSearch(unsigned off, uint32_t glyph) const; // reverse ''

  // reflow, given offset and display width
  void reflow(unsigned off, unsigned dwidth);

  bool isword_(unsigned off) const { return isword__(m_data[off]); }
  bool isspace_(unsigned off) const { return isspace__(m_data[off]); }

private:
  ZuInline static constexpr bool iswspace__(uint32_t u) { // unicode white space
    return
      u == 0xa0 ||
      u == 0x1680 ||
      (u >= 0x2000 && u <= 0x200a) ||
      u == 0x2028 ||
      u == 0x2029 ||
      u == 0x202f ||
      u == 0x205f ||
      u == 0x3000;
  }
  static constexpr bool isword__(uint32_t u) {
    return
      (u >= '0' && u <= '9') ||
      (u >= 'a' && u <= 'z') ||
      (u >= 'A' && u <= 'Z') ||
      u == '_' || (u > 0x80 && !iswspace__(u));
  }
  ZuInline static constexpr bool isspace__(uint32_t u) {
    return (u >= '\t' && u <= '\r') || u == ' ' || iswspace__(u);
  }
  template <typename L>
  bool fwd(unsigned &off, unsigned n, L &&l) const {
    uint32_t u;
    while (off < n) {
      auto o = ZuUTF8::in(&m_data[off], n - off, u);
      if (ZuUnlikely(!o)) return false;
      if (ZuFwd<L>(l)(u)) return true;
      off += o;
    }
    return false;
  }
  template <typename L>
  bool rev(unsigned &off, L &&l) const {
    unsigned n = off;
    uint32_t u;
    while (off > 0) {
      --off;
      off -= m_bytes[off].off();
      auto o = ZuUTF8::in(&m_data[off], n - off, u);
      if (ZuUnlikely(!o)) u = 0;
      if (ZuFwd<L>(l)(u)) return true;
    }
    return false;
  }

private:
  LineData	m_data;		// UTF-8 byte data
  IndexData	m_bytes;	// index offset -> display position
  IndexData	m_positions;	// index display position -> offset
};

} // Zrl

#endif /* ZrlLine_HH */
