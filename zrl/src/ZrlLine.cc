//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#include <zlib/ZrlLine.hh>

namespace Zrl {

void Line::clear()
{
  m_data.clear();
  m_bytes.clear();
  m_positions.clear();
}

// byte offset -> display position
Line::Index Line::byte(unsigned off) const
{
  if (off >= m_bytes.length())
    return Index{unsigned(m_positions.length()), 1, 0};
  return m_bytes[off];
}

// display position -> byte offset
Line::Index Line::position(unsigned pos) const
{
  if (pos >= m_positions.length())
    return Index{unsigned(m_bytes.length()), 1, 0};
  return m_positions[pos];
}

// left-align position to glyph
unsigned Line::align(unsigned pos) const
{
  unsigned n = m_positions.length();
  if (ZuUnlikely(pos >= n)) return n;
  auto index = m_positions[pos];
  if (ZuUnlikely(index.padding())) {
    n = index.off() + 1;
    if (ZuUnlikely(pos < n)) return 0;
    pos -= n;
    index = m_positions[pos];
  }
  n = index.off();
  if (ZuUnlikely(pos < n)) return 0;
  return pos - n;
}

// forward glyph
unsigned Line::fwdGlyph(unsigned off) const
{
  unsigned n = m_bytes.length();
  if (ZuUnlikely(off >= n)) return n;
  auto index = m_bytes[off];
  return off + index.len() - index.off();
}

// backup glyph
unsigned Line::revGlyph(unsigned off) const
{
  unsigned n = m_bytes.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off >= n))
    off = n;
  else
    off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  off -= m_bytes[--off].off();
  return off;
}

// forward word, distinguishing alphanumeric + '_'
unsigned Line::fwdWord(unsigned off) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(off >= n)) return n;
  off -= m_bytes[off].off();
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (isword__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return !isword__(u); })) return n;
  } else if (!isspace__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return isspace__(u) || isword__(u); }))
      return n;
  }
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (isspace__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return !isspace__(u); })) return n;
  }
  return off;
}

// backup word, distinguishing alphanumeric + '_'
unsigned Line::revWord(unsigned off) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(off >= n))
    off = n;
  else
    off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  --off;
  off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (isspace__(u)) {
    if (!rev(off, [](uint32_t u) { return !isspace__(u); })) return 0;
  }
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (isword__(u)) {
    if (!rev(off, [](uint32_t u) { return !isword__(u); })) return 0;
  } else {
    if (!rev(off, [](uint32_t u) { return isspace__(u) || isword__(u); }))
      return 0;
  }
  return off + m_bytes[off].len();
}

// forward to end of word, distinguishing alphanumeric + '_'
unsigned Line::fwdWordEnd(unsigned off, bool past) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  off -= m_bytes[off].off();
  if (!past) {
    off += m_bytes[off].len();
    if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  }
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (isspace__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return !isspace__(u); })) off = n;
  }
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (isword__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return !isword__(u); })) off = n;
  } else {
    if (!fwd(off, n, [](uint32_t u) { return isspace__(u) || isword__(u); }))
      off = n;
  }
eol:
  if (!past) {
    --off;
    off -= m_bytes[off].off();
  }
  return off;
}

// backup to end of word, distinguishing alphanumeric + '_'
unsigned Line::revWordEnd(unsigned off, bool past) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (isword__(u)) {
    if (!rev(off, [](uint32_t u) { return !isword__(u); })) return 0;
  } else if (!isspace__(u)) {
    if (!rev(off, [](uint32_t u) { return isspace__(u) || isword__(u); }))
      return 0;
  }
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (isspace__(u)) {
eol:
    if (!rev(off, [](uint32_t u) { return !isspace__(u); })) return 0;
  }
  if (past) {
    off -= m_bytes[off].off();
    off += m_bytes[off].len();
  }
  return off;
}

// forward whitespace-delimited word
unsigned Line::fwdUnixWord(unsigned off) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(off >= n)) return n;
  off -= m_bytes[off].off();
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (!isspace__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return isspace__(u); })) return n;
  }
  if (!fwd(off, n, [](uint32_t u) { return !isspace__(u); })) return n;
  return off;
}

// backup whitespace-delimited word
unsigned Line::revUnixWord(unsigned off) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(off >= n))
    off = n;
  else
    off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  --off;
  off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (isspace__(u)) {
    if (!rev(off, [](uint32_t u) { return !isspace__(u); })) return 0;
  }
  if (!rev(off, [](uint32_t u) { return isspace__(u); })) return 0;
  return off + m_bytes[off].len();
}

// forward to end of whitespace-delimited word
unsigned Line::fwdUnixWordEnd(unsigned off, bool past) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  off -= m_bytes[off].off();
  off += m_bytes[off].len();
  if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) return off;
  if (isspace__(u)) {
    if (!fwd(off, n, [](uint32_t u) { return !isspace__(u); })) off = n;
  }
  if (!fwd(off, n, [](uint32_t u) { return isspace__(u); })) off = n;
eol:
  if (!past) {
    --off;
    off -= m_bytes[off].off();
  }
  return off;
}

// backup to end of whitespace-delimited word
unsigned Line::revUnixWordEnd(unsigned off, bool past) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off >= n)) { off = n; goto eol; }
  off -= m_bytes[off].off();
  if (ZuUnlikely(!off)) return 0;
  uint32_t u;
  if (!ZuUTF8::in(&m_data[off], n - off, u)) u = 0;
  if (!isspace__(u)) {
    if (!rev(off, [](uint32_t u) { return isspace__(u); })) return 0;
  }
eol:
  if (!rev(off, [](uint32_t u) { return !isspace__(u); })) return 0;
  if (past && off > 0) {
    --off;
    off -= m_bytes[off].off();
  }
  return off;
}

// forward glyph search - returns (adjusted) origin if not found
unsigned Line::fwdSearch(unsigned off, uint32_t glyph) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(off >= n)) return n;
  unsigned orig = off;
  unsigned l;
  do {
    uint32_t u;
    l = ZuUTF8::in(&m_data[off], n - off, u);
    if (ZuUnlikely(!l)) break;
    if (u == glyph) return off;
  } while ((off += l) < n);
  return orig;
}

// reverse glyph search - returns (adjusted) origin if not found
unsigned Line::revSearch(unsigned off, uint32_t glyph) const
{
  unsigned n = m_data.length();
  if (ZuUnlikely(!n)) return 0;
  if (ZuUnlikely(off > n)) off = n - 1;
  ++off; while (--off > 0 && !ZuUTF8::initial(m_data[off]));
  unsigned orig = off;
  while (off) {
    uint32_t u;
    unsigned l = ZuUTF8::in(&m_data[off], n - off, u);
    if (ZuUnlikely(!l)) break;
    if (u == glyph) return off;
    while (--off > 0 && !ZuUTF8::initial(m_data[off]));
  }
  return orig;
}

// reflow, given offset and display width
void Line::reflow(unsigned off, unsigned dwidth)
{
  unsigned len = m_data.length();

  ZmAssert(off <= len);
  ZmAssert(dwidth >= 2);

  m_bytes.grow(len);
  m_positions.grow(len);

  unsigned pos;

  if (!off) {
    pos = 0;
  } else {
    if (auto byte = this->byte(off - 1)) {
      pos = byte.mapping();
      pos += position(pos).len();
    }
  }

  while (off < len) {
    auto span = ZuUTF<uint32_t, uint8_t>::gspan(
	ZuSpan<uint8_t>(&m_data[off], len - off));
    if (ZuUnlikely(!span)) break;
    unsigned glen = span.inLen();
    unsigned gwidth = span.width();
    m_bytes.grow(off + glen);
    {
      unsigned x = pos % dwidth;
      unsigned padding = (x + gwidth > dwidth) ? (dwidth - x) : 0U;
      m_positions.grow(pos + padding + gwidth);
      for (unsigned i = 0; i < padding; i++)
	m_positions[pos++] = Index{off, padding, i, true};
    }
    for (unsigned i = 0; i < glen; i++)
      m_bytes[off + i] = Index{pos, glen, i};
    for (unsigned i = 0; i < gwidth; i++)
      m_positions[pos + i] = Index{off, gwidth, i};
    off += glen;
    pos += gwidth;
  }
  m_bytes.length(off);
  m_positions.length(pos);
}

} // namespace Zrl
