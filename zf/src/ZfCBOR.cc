//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfCBOR.hh>

#include <zlib/ZuUTF.hh>

namespace ZfCBOR {

class Scanner {
public:
  Scanner(ZuBSpan data, const Limits &limits, void *ctx, Visit visit) :
    m_begin{data.begin()}, m_p{data.begin()}, m_end{data.end()},
    m_limits{limits}, m_ctx{ctx}, m_visit{visit} { }

  Result scan()
  {
    if (unsigned(m_end - m_begin) > m_limits.size)
      return result(Error::Size);
    if (!item(0)) return result(m_error);
    if (m_p != m_end) return result(Error::Syntax);
    return {unsigned(m_p - m_begin), Error::OK};
  }

private:
  Result result(int error) const {
    return {unsigned(m_p - m_begin), error};
  }

  bool arg(unsigned ai, uint64_t &value)
  {
    if (ai < 24) { value = ai; return true; }
    unsigned n;
    switch (ai) {
      case 24: n = 1; break;
      case 25: n = 2; break;
      case 26: n = 4; break;
      case 27: n = 8; break;
      default: m_error = Error::Unsupported; return false;
    }
    if (unsigned(m_end - m_p) < n) return false;
    value = 0;
    do value = (value << 8) | *m_p++; while (--n);
    return true;
  }

  static bool utf8(ZuBSpan data)
  {
    const uint8_t *p = data.begin();
    const uint8_t *end = data.end();
    while (p < end) {
      unsigned n = ZuUTF8::in(*p);
      if (!n || unsigned(end - p) < n) return false;
      uint32_t u;
      if (ZuUTF8::in(p, n, u) != n || ZuUTF8::out(u) != n ||
	  u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) return false;
      for (unsigned i = 1; i < n; ++i)
	if ((p[i] & 0xc0) != 0x80) return false;
      p += n;
    }
    return true;
  }

  bool item(unsigned depth)
  {
    if (m_p >= m_end) return false;
    if (m_items >= m_limits.items) {
      m_error = Error::Items;
      return false;
    }
    ++m_items;
    const uint8_t *begin = m_p;
    uint8_t initial = *m_p++;
    unsigned major = initial >> 5;
    uint64_t value;
    if (!arg(initial & 31, value)) return false;

    Item out{int(major), value, {}, {}, depth};
    switch (major) {
      case 0: out.type = Type::UInt; break;
      case 1: out.type = Type::NInt; break;
      case 2:
      case 3:
	if (value > m_limits.data) { m_error = Error::Data; return false; }
	if (value > uint64_t(m_end - m_p)) return false;
	out.type = major == 2 ? Type::Bytes : Type::Text;
	out.data = {m_p, unsigned(value)};
	m_p += value;
	if (major == 3 && !utf8(out.data)) {
	  m_error = Error::UTF8;
	  return false;
	}
	break;
      case 4:
      case 5: {
	if (depth >= m_limits.depth) {
	  m_error = Error::Depth;
	  return false;
	}
	uint64_t n = value;
	if (major == 5) {
	  if (n > (m_limits.items - m_items) / 2) {
	    m_error = Error::Items;
	    return false;
	  }
	  n *= 2;
	} else if (n > m_limits.items - m_items) {
	  m_error = Error::Items;
	  return false;
	}
	for (uint64_t i = 0; i < n; ++i)
	  if (!item(depth + 1)) return false;
	out.type = major == 4 ? Type::Array : Type::Map;
      } break;
      case 7:
	switch (initial & 31) {
	  case 20: out.type = Type::Bool; out.value = false; break;
	  case 21: out.type = Type::Bool; out.value = true; break;
	  case 22: out.type = Type::Null; out.value = 0; break;
	  default: m_error = Error::Unsupported; return false;
	}
	break;
      default: m_error = Error::Unsupported; return false;
    }
    out.encoded = {begin, unsigned(m_p - begin)};
    if (m_visit && !m_visit(m_ctx, out)) {
      m_error = Error::Stopped;
      return false;
    }
    return true;
  }

  const uint8_t	*m_begin;
  const uint8_t	*m_p;
  const uint8_t	*m_end;
  const Limits	&m_limits;
  void		*m_ctx;
  Visit		m_visit;
  unsigned	m_items = 0;
  int		m_error = Error::Syntax;
};

Result scan(ZuBSpan data, const Limits &limits, void *ctx, Visit visit)
{
  return Scanner{data, limits, ctx, visit}.scan();
}

} // namespace ZfCBOR
