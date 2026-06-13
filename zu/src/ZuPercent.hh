//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Percent encode/decode mechanics

#ifndef ZuPercent_HH
#define ZuPercent_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <string.h>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTuple.hh>

namespace ZuPercent {

ZuInline constexpr int hex(char c) {
  c |= 0x20;
  return
    (ZuLikely(c >= '0' && c <= '9')) ?  c - '0' :
    (ZuLikely(c >= 'a' && c <= 'f')) ? (c - 'a') + 10 : -1;
}

ZuInline constexpr char digit(unsigned n) {
  return n < 10 ? n + '0' : (n - 10) + 'A';
}

struct Span {
  Span() = default;
  Span(uint64_t inLen_, uint64_t outLen_) : inLen{inLen_}, outLen{outLen_} { }

  uint64_t	inLen = 0;
  uint64_t	outLen = 0;

  bool operator !() const { return !inLen && !outLen; }
  ZuOpBool
};

struct Scan {
  int		out = -1;
  int		in = -1;
  char		term = 0;

  bool operator !() const { return out < 0; }
  ZuOpBool
};

struct NoTerm {
  static constexpr bool term(uint8_t) { return false; }
};

struct NoPlus {
  static constexpr bool plus() { return false; }
  static constexpr bool spacePlus() { return false; }
};

template <typename Policy>
struct Codec {
  static Span span(ZuBSpan src) {
    uint64_t out = 0;
    for (uint64_t i = 0, n = src.length(); i < n; i++) {
      uint8_t c = src[i];
      out += (Policy::spacePlus() && c == ' ') ? 1 :
	Policy::esc(c) ? 3 : 1;
    }
    return {src.length(), out};
  }

  static uint64_t len(ZuBSpan src) { return span(src).outLen; }

  static uint64_t encode(ZuSpan<uint8_t> dst, ZuBSpan src) {
    uint64_t o = 0, l = dst.length();
    for (uint64_t i = 0, n = src.length(); i < n; i++) {
      uint8_t c = src[i];
      if (Policy::spacePlus() && c == ' ') {
	if (ZuUnlikely(o >= l)) break;
	dst[o++] = '+';
      } else if (Policy::esc(c)) {
	if (ZuUnlikely(l - o < 3)) break;
	dst[o++] = '%';
	dst[o++] = digit(c>>4);
	dst[o++] = digit(c & 15);
      } else {
	if (ZuUnlikely(o >= l)) break;
	dst[o++] = c;
      }
    }
    return o;
  }

  static ZuTuple<uint64_t, bool>
  encode_overflow(ZuSpan<uint8_t> dst, ZuBSpan src) {
    uint64_t o = 0, l = dst.length();
    bool overflow = false;
    for (uint64_t i = 0, n = src.length(); i < n; i++) {
      uint8_t c = src[i];
      if (Policy::spacePlus() && c == ' ') {
	if (ZuUnlikely(o >= l)) { overflow = true; break; }
	dst[o++] = '+';
      } else if (Policy::esc(c)) {
	if (ZuUnlikely(l - o < 3)) { overflow = true; break; }
	dst[o++] = '%';
	dst[o++] = digit(c>>4);
	dst[o++] = digit(c & 15);
      } else {
	if (ZuUnlikely(o >= l)) { overflow = true; break; }
	dst[o++] = c;
      }
    }
    return {o, overflow};
  }

  template <typename S>
  static void print(S &s, ZuBSpan src) {
    for (uint64_t i = 0, n = src.length(); i < n; i++) {
      uint8_t c = src[i];
      if (Policy::spacePlus() && c == ' ') {
	s << '+';
      } else if (Policy::esc(c)) {
	s << '%' << digit(c>>4) << digit(c & 15);
      } else {
	s << c;
      }
    }
  }

  static Scan decode(ZuSpan<char> src) {
    unsigned n = src.length();

    if (ZuUnlikely(n < 1)) goto bad;

    unsigned i;
    for (i = 0; i < n; i++) {
      char c = src[i];
      if (ZuUnlikely(c == '%' || (Policy::plus() && c == '+'))) goto slow;
      if (ZuUnlikely(Policy::term(c))) {
	src[i] = 0;
	return {int(i), int(i + 1), c};
      }
    }
    return {int(i), int(i), 0};

  slow:
    {
      unsigned o = i;
      while (i < n) {
	char c = src[i];
	if (ZuUnlikely(Policy::plus() && c == '+')) {
	  ++i;
	  src[o++] = ' ';
	  continue;
	}
	if (ZuUnlikely(c == '%')) {
	  if (ZuUnlikely(++i > n - 2)) goto bad;
	  auto h = hex(src[i++]);
	  auto l = hex(src[i++]);
	  if (ZuUnlikely(h < 0 || l < 0)) goto bad;
	  src[o++] = char((h<<4) | l);
	  continue;
	}
	if (ZuUnlikely(Policy::term(c))) {
	  ++i;
	  memset(&src[o], 0, i - o);
	  return {int(o), int(i), c};
	}
	++i;
	src[o++] = c;
      }
      if (o < i) memset(&src[o], 0, i - o);
      return {int(o), int(i), 0};
    }

  bad:
    return {-1};
  }
};

} // namespace ZuPercent

#endif /* ZuPercent_HH */
