//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP core types and parsing infrastructure

#ifndef ZhttpCore_HH
#define ZhttpCore_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <string.h>

#include <zlib/ZuAssert.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtEnum.hh>

// Headers typelist definition.  Each key is followed by a value typelist;
// headers without fixed values use ZuTypeList<>.  For example:
// - keys (variable values):
//   ZhttpHeaders("content-type", "server");
// - keys + values:
//   ZhttpHeaders(("user-agent", "zhttp"), ("accept", "*/*"));
#define Zhttp_HdrValue(Value) ZuStringT<Value>
#define Zhttp_HdrValues_(...) \
  ZuPP_Eval__(ZuPP_MapComma(Zhttp_HdrValue,  __VA_ARGS__))
#define Zhttp_HdrValues(Values) \
  ZuPP_Defer(Zhttp_HdrValues_)(ZuPP_Strip(Values))
#define Zhttp_Header_1(Key) \
  ZuStringT<Key>, ZuTypeList<>
#define Zhttp_Header_2(Key, Values) \
  ZuStringT<Key>, ZuTypeList<Zhttp_HdrValues(Values)>
#define Zhttp_Header_N(_0, _1, Fn, ...) Fn
#define Zhttp_Header_(...) \
  Zhttp_Header_N(__VA_ARGS__, \
    Zhttp_Header_2(__VA_ARGS__), \
    Zhttp_Header_1(__VA_ARGS__))
#define Zhttp_Header(KV) \
  ZuPP_Defer(Zhttp_Header_)(ZuPP_Strip(KV))
#define ZhttpHeaders(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_Header,  __VA_ARGS__))>

namespace Zhttp {

namespace Builder_ {

template <typename Write>
struct Writer {
  Write	&write;

  template <typename S>
  void print(S &s) const { write(s); }
  friend ZuPrintFn ZuPrintType(Writer *);
};

template <typename Write>
Writer<Write> writer(Write &write) { return {write}; }

} // namespace Builder_

constexpr unsigned DefltMaxBody = (1<<20);	// 1M default
constexpr unsigned DefltMaxStartLine = (1<<13);	// 8K
constexpr unsigned DefltMaxHeaderSection = (1<<16);	// 64K

ZtEnumStruct(ZhttpAPI, Version, int8_t, H1, H2, H3);

ZtEnumNS(ZhttpAPI, Method, int8_t,
  GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);

inline bool earlyDataSafeMethod(Method::T method)
{
  switch (method) {
    case Method::GET:
    case Method::HEAD:
    case Method::OPTIONS:
      return true;
    default:
      return false;
  }
}

inline bool idempotentMethod(Method::T method)
{
  switch (method) {
    case Method::GET:
    case Method::PUT:
    case Method::DELETE:
    case Method::HEAD:
    case Method::OPTIONS:
    case Method::TRACE:
      return true;
    default:
      return false;
  }
}

inline bool earlyDataSafeRequest(Method::T method, bool hasBody)
{
  return !hasBody && earlyDataSafeMethod(method);
}

ZtEnumStruct(ZhttpAPI, RequestErrorCode, int8_t,
  Malformed, ContentTooLarge, TargetTooLong, HeadersTooLarge,
  NotImplemented, VersionUnsupported, OperationRejected, BodyRejected);

ZtEnumStruct(ZhttpAPI, RequestErrorScope, int8_t,
  Request, Stream, Connection);

struct RequestError {
  RequestErrorCode::T code = RequestErrorCode::Malformed;
  RequestErrorScope::T scope = RequestErrorScope::Request;
  bool responsePossible = false;
};

struct BodyCommit {
  uint64_t	produced = 0;
  uint64_t	committed = 0;
  uint64_t	reset = 0;
  uint64_t	discarded = 0;
  bool		headers = false;
  bool		final = false;
};

ZtEnumNS(ZhttpAPI, BodyPolicy, int8_t,
  None, Fixed, OptionalFixed, Stream, OptionalStream);

ZtEnumStruct(ZhttpAPI, BodyType, int8_t, None, Streamed, Fixed);
ZtEnumStruct(ZhttpAPI, WriteOutcome, int8_t, End, Stream, Abort, Failed);

namespace BodyPolicy {

ZuInline bool hasBody(T v) { return v != None; }
ZuInline bool optional(T v) {
  return v == OptionalFixed || v == OptionalStream;
}
ZuInline bool streaming(T v) {
  return v == Stream || v == OptionalStream;
}

} // namespace BodyPolicy

template <typename HdrCatalog>
struct HeaderList {
  using Headers = typename HdrCatalog::List;
  ZuAssert(!(Headers::N & 1), "header list must contain key/value pairs");
  enum { N = Headers::N >> 1 };
  template <unsigned I> using Key = ZuType<I << 1, Headers>;
  template <unsigned I> using Value = ZuType<(I << 1) + 1, Headers>;
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = ZuTypeSlice<2, 1, Headers>;
};

template <typename Values>
struct HeaderValue_ {
  ZuAssert(Values::N == 1,
    "builder header must have exactly one fixed value");
  using T = ZuType<0, Values>;
};
template <typename Values>
using HeaderValue = typename HeaderValue_<Values>::T;

ZhttpAPI unsigned requestErrorStatus(RequestErrorCode::T);

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

// find end of key ':'
// - uses memchr to leverage any available performance advantage
ZuInline int eok(ZuBSpan data) {
  auto p = static_cast<const uint8_t *>(memchr(&data[0], ':', data.length()));
  if (!p) return -1;
  return p - &data[0];
}

// skip leading linear white space to find beginning of header value
ZuInline int bov(ZuBSpan data) {
  for (unsigned o = 0, n = data.length(); o < n; ++o)
    if (!islws(data[o])) return o;
  return -1;
}

// remove trailing linear white space to find end of header value
ZuInline int eov(ZuBSpan data) {
  for (int o = data.length(); --o >= 0; )
    if (!islws(data[o])) return o + 1;
  return -1;
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
      l(count++, ZuBSpan(&data[begin], unsigned(end - begin)));
    if (o >= n) break;
    // skip trailing linear white space
    while (++o < n) if (!islws(data[o])) break;
  }
}

// normalize key case to be consistent (mutates key in place)
// - ZuMatcher needs consistent casing for efficient key matching
inline void lowerASCII(ZuSpan<uint8_t> key) {
  unsigned n = key.length();
  int c; // intentionally int

  for (unsigned o = 0; o < n; o++) {
    c = key[o];
    if (c >= 'A' && c <= 'Z') key[o] = c + 'a' - 'A';
  }
}

// parses a key and value from a line
// - calls kv(key, value)
template <typename KV>
inline bool parseKV(ZuSpan<uint8_t> line, KV &&kv) {
  if (ZuUnlikely(!line || islws(line[0]))) return false;
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
  lowerASCII(key);
  kv(key, value);
  return true;
}

} // namespace Zhttp

#endif /* ZhttpCore_HH */
