//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// join array

#ifndef ZtCase_HH
#define ZtCase_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZmScratch.hh>

#include <zlib/ZtString.hh>

namespace ZtCase {

ZuDerive(Buf,
  (ZtString<ZtStringHeapID<"ZtCase.Buf", ZtStringSharded<true>>>));

inline bool isupper__(char c) { return c >= 'A' && c <= 'Z'; }
inline char toupper__(char c) {
  return c + (static_cast<int>('A') - static_cast<int>('a'));
}
inline bool islower__(char c) { return c >= 'a' && c <= 'z'; }
inline char tolower__(char c) {
  return c + (static_cast<int>('a') - static_cast<int>('A'));
}

// lambda(const ZuCSpan &s)
template <typename L>
inline void snakeCamel(ZuCSpan s, L &&l) {
  unsigned n = s.length();
  unsigned o = 0;
  bool underscore = false;
  for (unsigned i = 0; i < n; i++) {
    auto c = s[i];
    if (underscore) {
      if (islower__(c)) ++o;
      underscore = false;
    } else {
      if (c == '_') underscore = true;
    }
  }
  unsigned m = n - o;
  unsigned z = m + 1;
  auto buf = ZmScratch(char, z, Buf::VHeap);
  buf.length(m);
  underscore = false;
  unsigned j = 0;
  for (unsigned i = 0; i < n; i++) {
    auto c = s[i];
    if (underscore) {
      if (islower__(c))
	buf[j++] = toupper__(c);
      else {
	buf[j++] = '_';
	buf[j++] = c;
      }
      underscore = false;
    } else {
      if (c == '_')
	underscore = true;
      else
	buf[j++] = c;
    }
  }
  if (underscore) buf[j++] = '_';
  ZmAssert(j == m);
  ZuFwd<L>(l)(buf);
}

// lambda(const ZuCSpan &s)
template <typename L>
inline void camelSnake(ZuCSpan s, L &&l) {
  unsigned n = s.length();
  unsigned o = 0;
  for (unsigned i = 0; i < n; i++) {
    auto c = s[i];
    if (isupper__(c)) ++o;
  }
  unsigned m = n + o;
  unsigned z = m + 1;
  auto buf = ZmScratch(char, z, Buf::VHeap);
  buf.length(m);
  unsigned j = 0;
  for (unsigned i = 0; i < n; i++) {
    auto c = s[i];
    if (isupper__(c)) {
      buf[j++] = '_';
      buf[j++] = tolower__(c);
    } else
      buf[j++] = c;
  }
  ZmAssert(j == m);
  ZuFwd<L>(l)(buf);
}

}

#endif /* ZtCase_HH */
