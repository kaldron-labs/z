//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// simple tokenizers
// - zero-copy
// - no stateful escaping of delimiters, i.e. no FSM / BNF
//   - Examples: URL escaping is stateless, shell escaping is stateful
// - suitable for splitting HTTP paths, header values, etc.

#ifndef ZuTokenizer_HH
#define ZuTokenizer_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

namespace ZuTokenizer {

// - single-character delimited (: , ; / etc.)
template <char Delimiter>
struct Delimited {
  // returns {} once input is exhausted
  inline static ZuCSpan next(ZuCSpan &span) {
    for (unsigned o = 0, n = span.length(); o < n; ++o)
      if (span[o] == Delimiter) {
	ZuCSpan token(&span[0], o);
	span.offset(o + 1);
	return token;
      }
    ZuCSpan token = span;
    span = {};
    return token;
  }
};

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

// - white-space delimited
namespace WhiteSpace {
  // returns {} once input is exhausted
  inline ZuCSpan next(ZuCSpan &span) {
    for (unsigned o = 0, n = span.length(); o < n; ++o)
      if (isspace__(span[o])) {
	ZuCSpan token(&span[0], o);
	span.offset(o + 1);
	span.trim();
	return token;
      }
    ZuCSpan token = span;
    span = {};
    return token;
  }
};

}

#endif /* ZuTokenizer_HH */
