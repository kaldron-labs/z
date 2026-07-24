//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - message parsing utility functions

#ifndef ZhttpUtil_HH
#define ZhttpUtil_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

namespace Zhttp {

// hard-coded linear white space (ASCII/UTF8)
ZuInline constexpr bool islws(uint8_t c) {
  return c == '\t' || c == ' ';
}

// hard-coded Boyer-Moore to find end of header "\r\n\r\n"
ZuInline int eoh(ZuBSpan data) {
  unsigned n = data.length();

  if (ZuUnlikely(n < 4)) return -1;
  n -= 4;

  int j;
  uint8_t c;

  for (unsigned o = 0; o <= n; ) {
    j = 3;
    while (j >= 0 && ((j & 1) ? '\n' : '\r') == (c = data[o + j])) j--;
    if (j < 0) return o + 4;
    j -= (c == '\r' ? 2 : c == '\n' ? 3 : -1);
    o += j < 1 ? 1 : j;
  }
  return -1;
}

// hard-coded Boyer-Moore to find end of line "\r\n[^\t ]" or "\r\n"
template <bool CanFold = true> // set to false to just match "\r\n"
ZuInline int eol(ZuBSpan data) {
  unsigned n = data.length();

  if (ZuUnlikely(n < 2)) return -1;
  n -= 2;

  uint8_t c;

  for (unsigned o = 0; o <= n; ) {
    if (ZuLikely(o < n)) {
      c = data[o + 2];
      if constexpr (CanFold)
	if (c == '\t' || c == ' ') { o += 3; continue; }
    }
    if (data[o + 1] != '\n') { ++o; continue; }
    if (data[o] == '\r') return o;
    o += 2;
  }
  return -1;
}

// find end of key ':'
// - uses memchr to leverage any available performance advantage
ZuInline int eok(ZuBSpan data) {
  auto p = static_cast<const uint8_t *>(memchr(&data[0], ':', data.length()));
  if (!p) return -1;
  return p - &data[0];
}

// skip leading linear white space to find beginning of header value
ZuInline int bov(ZuBSpan data) {
  auto begin = data.data();
  data.trim(islws);
  return data ? int(data.data() - begin) : -1;
}

// remove trailing linear white space to find end of header value
ZuInline int eov(ZuBSpan data) {
  data.chomp(islws);
  return data ? int(data.length()) : -1;
}

// split and iterate over HTTP value delimited by \s+,\s+
// - strips leading/trailing white space
// - single-pass, no back-tracking
// - optional alternate delimiter character (';' is also frequently used)
template <uint8_t Delim = ',', typename L>
inline void split(ZuBSpan data, L &&l) {
  unsigned count = 0;
  int begin, end;
  unsigned o = 0, n = data.length();

  for (;;) {
    // skip leading linear white space
    for (; o < n; ++o) if (!islws(data[o])) break;
    begin = o; end = -1;
    // find delimiter or end of string, remembering last non-white-space
    for (; o < n; ++o) {
      auto c = data[o];
      if (c == Delim) break;
      if (end < 0 ) {
	if (islws(c)) end = o;
      } else {
	if (!islws(c)) end = -1;
      }
    }
    if (end < 0) end = o;
    if (ZuLikely(end > begin || count || o < n))
      if (!l(count++, ZuBSpan(&data[begin], unsigned(end - begin))))
	break;
    if (o >= n) break;
    // skip trailing linear white space
    while (++o < n) if (!islws(data[o])) break;
  }
}

// normalize key case to be consistent (mutates key in place)
// - ZuMatcher needs consistent casing for efficient key matching
inline void normalize(ZuSpan<uint8_t> key) {
  unsigned n = key.length();
  int c; // intentionally int

  for (unsigned o = 0; o < n; o++) {
    c = key[o];
    if (c >= 'A' && c <= 'Z') key[o] = c + 'a' - 'A';
  }
}

// CRLF framing
template <bool CanFold = true>
inline auto crlf() {
  return [prevCR = false](ZuBSpan span) mutable -> int64_t {
    if (prevCR && span[0] == '\n') return 1;
    if (int consumed = eol<CanFold>(span); consumed >= 0)
      return consumed + 2;
    prevCR = span[span.length() - 1] == '\r';
    return 0;
  };
}

// calls line(span)
// - CanFold should be false for the start line
// - span is empty for the last line before the body
template <bool CanFold = true, typename Stream, typename Line>
inline int64_t parseLine(Stream &stream, Line &&line) {
  return stream.template consume<2, "Zhttp.Header">(
    crlf<CanFold>(), ZuFwd<Line>(line));
}

// parses a key and value from a line
// - calls kv(key, value)
template <typename KV>
inline bool parseKV(ZuSpan<uint8_t> line, KV &&kv) {
  int n = eok(line);
  if (ZuUnlikely(n < 0)) return false;
  ZuSpan key(&line[0], unsigned(n));
  line.offset(n + 1); // skip key and delimiter
  n = bov(line);
  if (n < 0)
    line.trunc(0);
  else
    line.offset(n); // skip white space
  ZuSpan value{line.data(), line.length()};
  if (value) {
    n = eov(value);
    if (ZuUnlikely(n < 0)) return false; // should never happen
    value.trunc(n);
  }
  normalize(key);
  kv(key, value);
  return true;
}

inline bool parseUInt64Full_(ZuBSpan value, uint64_t &out) {
  ZuBox<uint64_t> box;
  int n = box.scan(ZuCSpan{value});
  if (ZuUnlikely(n < 0 || unsigned(n) != value.length())) return false;
  uint64_t v = 0;
  for (unsigned i = 0; i < value.length(); ++i) {
    int c = value[i];
    if (ZuUnlikely(c < '0' || c > '9')) return false;
    c -= '0';
    if (ZuUnlikely(v > (uint64_t(-1) - unsigned(c)) / 10)) return false;
    v = (v * 10) + unsigned(c);
  }
  out = v;
  return true;
}

} // namespace Zhttp

#endif /* ZhttpUtil_HH */
