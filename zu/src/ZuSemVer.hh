//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Packed major.minor.patch version, using the Z_VERSION decimal encoding.
// Minor is 0..99, patch is 0..999; the packed value fits in uint32_t.
// Prerelease and build metadata are not represented.

#ifndef ZuSemVer_HH
#define ZuSemVer_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTraits.hh>

class ZuSemVer {
public:
  constexpr ZuSemVer() = default;
  constexpr explicit ZuSemVer(uint32_t value) : m_value{value} { }
  constexpr ZuSemVer(unsigned major, unsigned minor, unsigned patch) :
    m_value{major * 100000U + minor * 1000U + patch} { }

  template <typename S, typename = ZuMatchString<S>>
  ZuSemVer(const S &s) { scan(s); }
  template <typename S, typename = ZuMatchString<S>>
  ZuSemVer &operator =(const S &s) { scan(s); return *this; }

  constexpr explicit operator uint32_t() const { return m_value; }
  constexpr uint32_t value() const { return m_value; }
  constexpr void value(uint32_t v) { m_value = v; }
  constexpr unsigned major() const { return m_value/100000U; }
  constexpr unsigned minor() const { return m_value/1000U % 100U; }
  constexpr unsigned patch() const { return m_value % 1000U; }

  // Returns the consumed length, or zero on failure (leaving the value intact).
  unsigned scan(ZuCSpan s) {
    unsigned pos = 0;
    uint64_t value = 0;
    for (unsigned part = 0; part < 3; ++part) {
      unsigned limit = !part ? UINT32_MAX/100000U : part == 1 ? 99 : 999;
      unsigned v = 0, begin = pos;
      while (pos < s.length()) {
	unsigned digit = unsigned(s[pos] - '0');
	if (digit > 9) break;
	if (digit > limit || v > (limit - digit)/10U) return 0;
	v = v * 10U + digit;
	++pos;
      }
      if (pos == begin) return 0;
      value = value * (part == 2 ? 1000U : 100U) + v;
      if (part < 2 && (pos == s.length() || s[pos++] != '.')) return 0;
    }
    if (value > UINT32_MAX) return 0;
    m_value = uint32_t(value);
    return pos;
  }

  template <typename S> void print(S &s) const {
    s << ZuBoxed(major()) << '.' << ZuBoxed(minor()) << '.' << ZuBoxed(patch());
  }

  bool equals(const ZuSemVer &v) const { return m_value == v.m_value; }
  int cmp(const ZuSemVer &v) const { return ZuCompare(m_value, v.m_value); }
  friend bool operator ==(const ZuSemVer &l, const ZuSemVer &r) {
    return l.equals(r);
  }
  friend int operator <=>(const ZuSemVer &l, const ZuSemVer &r) {
    return l.cmp(r);
  }
  uint32_t hash() const { return ZuHash<uint32_t>::hash(m_value); }

  struct Traits : public ZuBaseTraits<ZuSemVer> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZuSemVer *);
  friend ZuPrintFn ZuPrintType(ZuSemVer *);

private:
  uint32_t m_value = 0;
};

#endif /* ZuSemVer_HH */
