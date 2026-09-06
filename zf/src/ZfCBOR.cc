//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfCBOR.hh>

namespace ZfCBOR {

class Scanner {
public:
  Scanner(ZuBSpan data) :
    m_begin{data.begin()}, m_p{data.begin()}, m_end{data.end()} { }

  Result scan()
  {
    if (!item()) return result(Error::Syntax);
    return {unsigned(m_p - m_begin), Error::OK};
  }

private:
  Result result(int error) const {
    return {unsigned(m_p - m_begin), error};
  }

  bool data(uint64_t n)
  {
    if (n > uint64_t(m_end - m_p)) return false;
    m_p += unsigned(n);
    return true;
  }

  bool header(unsigned &major, unsigned &ai, uint64_t &value)
  {
    if (m_p >= m_end) return false;
    uint8_t initial = *m_p++;
    major = initial >> 5;
    ai = initial & 31;
    if (ai < 24) { value = ai; return true; }
    if (ai == 31) { value = UINT64_MAX; return true; }
    unsigned n;
    switch (ai) {
      case 24: n = 1; break;
      case 25: n = 2; break;
      case 26: n = 4; break;
      case 27: n = 8; break;
      default: return false;
    }
    if (unsigned(m_end - m_p) < n) return false;
    value = 0;
    do value = (value << 8) | *m_p++; while (--n);
    return true;
  }

  bool indefEnd()
  {
    if (m_p >= m_end || *m_p != 0xff) return false;
    ++m_p;
    return true;
  }

  bool string(unsigned major)
  {
    for (;;) {
      if (indefEnd()) return true;
      if (m_p >= m_end) return false;
      unsigned chunkMajor, ai;
      uint64_t n;
      if (!header(chunkMajor, ai, n) || chunkMajor != major || ai == 31)
        return false;
      if (!data(n)) return false;
    }
  }

  bool array()
  {
    for (;;) {
      if (indefEnd()) return true;
      if (!item()) return false;
    }
  }

  bool map()
  {
    for (;;) {
      if (indefEnd()) return true;
      if (!item() || !item()) return false;
    }
  }

  bool items(uint64_t n)
  {
    for (; n; --n)
      if (!item()) return false;
    return true;
  }

  bool item()
  {
    if (m_p >= m_end) return false;
    unsigned major, ai;
    uint64_t value;
    if (!header(major, ai, value)) return false;
    if (ai == 31)
      switch (major) {
        case 2: return string(major);
        case 3: return string(major);
        case 4: return array();
        case 5: return map();
        default: return false;
      }
    switch (major) {
      case 0:
      case 1:
      case 7:
        return true;
      case 2:
      case 3:
        return data(value);
      case 4:
        return items(value);
      case 5:
        if (value > UINT64_MAX / 2) return false;
        return items(value * 2);
      case 6:
        return item();
      default:
        return false;
    }
  }

  const uint8_t	*m_begin;
  const uint8_t	*m_p;
  const uint8_t	*m_end;
};

Result scan(ZuBSpan data)
{
  return Scanner{data}.scan();
}

} // namespace ZfCBOR
