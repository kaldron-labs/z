//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP 1.1
//   - optionally chunked body
//   - optional chunked trailers (rarely used feature)
// - caller is responsible for body decompression (if required)

#ifndef Zhttp_HH
#define Zhttp_HH

#include <zlib/ZhttpLib.hh>

#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtLocalArray.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

// Headers typelist definition, e.g.
// - keys (variable values):
//   ZhttpHeaders("content-type", "server");
// - keys + values:
//   ZhttpHeaders(("user-agent", "zhttpclient"), ("accept", "*/*"));
#define Zhttp_HdrValue(Value) ZuStringT<Value>
#define Zhttp_HdrValues_(...) \
  ZuPP_Eval__(ZuPP_MapComma(Zhttp_HdrValue,  __VA_ARGS__))
#define Zhttp_HdrValues(Values) \
  ZuPP_Defer(Zhttp_HdrValues_)(ZuPP_Strip(Values))
#define Zhttp_Header_1(Key) \
  ZuStringT<Key>, void
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

constexpr unsigned DefltMaxHdr = (1<<16);	// 64K default
constexpr unsigned DefltMaxBody = (1<<20);	// 1M default

namespace Method {
  ZtEnum(Method, int8_t,
    GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE);
}

// deprecated transfer-encoding compression
namespace XferCompression {
  ZtEnum(XferCompression, int8_t, compress, deflate, gzip);
}

// HTTP message parser

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
  return stream.template consume<2, ZuStringT<"Zhttp.Header">>(
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
  if (ZuUnlikely(n < 0)) return false;
  line.offset(n); // skip white space
  ZuSpan value{line.data(), line.length()};
  n = eov(value);
  if (ZuUnlikely(n < 0)) return false; // should never happen
  value.trunc(n);
  normalize(key);
  kv(key, value);
  return true;
}

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public Parser<Impl, ...> {
  using Base = Parser<Impl, ...>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - requests
  void operation(Method::T method, ZuBSpan path);

  // optional - responses
  void status(unsigned);

  // optional - header key
  template <typename Key> void header(ZuBSpan value);

  // optional - header key+value
  template <typename Key, typename Value> void header();

  // optional - content-length header
  void contentLength(uint64_t);

  // optional - transfer-encoding compression
  // - this will only be called if compress/deflate/gzip is specified
  // - this is HTTP 1.1 only and not mainstream
  void xferCompression(XferCompression::T);

  // optional - transfer-encoding: chunked
  void chunked();

  // optional - body data
  void body(ZuBSpan);

  // end of message (end of body, or end of header if no body))
  void complete(ParserState::T);
};
#endif

namespace H1 {

  namespace ParserState {
    ZtEnum(ParserState, int8_t,
      Initial,		// first line - request operation or response status
      Headers,		// reading headers
      Body,		// reading body data (not chunked)
      ChunkHdr,		// chunk header (hex length + CRLF)
      Chunk,		// reading chunk data
      ChunkTrlr,		// chunk trailer (CRLF)
      Trailers,		// trailers after final chunk
      Complete,		// message completely read
      Error);		// invalid message
  }

  template <
    typename Impl,
    bool Request_ = false,
    typename Headers_ = ZuTypeList<>,
    uint64_t MaxBody_ = DefltMaxBody>
  struct Parser {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

    enum { Request = Request_ };
    using Headers = typename Headers_::template Unshift<
      ZuStringT<"transfer-encoding">, void,
      ZuStringT<"content-length">, void>;
    using Keys = ZuTypeSlice<2, 0, Headers>;
    using Values = ZuTypeSlice<2, 1, Headers>;
    static constexpr uint64_t MaxBody = MaxBody_;

  private:
    // process header with variable value
    template <typename Key> void header_(ZuBSpan value) {
      if constexpr (ZuIsSame<Key, ZuStringT<"transfer-encoding">>{}) {
	bool invalid = false;
	split(value, [this, &invalid](unsigned i, ZuBSpan token) -> bool {
	  // chunked must come last, anything else must be first
	  if (m_chunked) {
	    invalid = true;
	    return false;
	  } else if (token == "chunked") {
	    m_chunked = true;
	    impl()->chunked();
	  } else if (i) {
	    invalid = true;
	    return false;
	  } else {
	    auto xferCompression = XferCompression::lookup(token);
	    if (xferCompression < 0)
	      invalid = true;
	    else
	      impl()->xferCompression(xferCompression);
	  }
	  return true;
	});
	if (invalid) {
	  m_state = ParserState::Error;
	  ZiLOG(Error, "Zhttp", "invalid transfer-encoding");
	}
      } else if constexpr (ZuIsSame<Key, ZuStringT<"content-length">>{}) {
	uint64_t contentLength = ZuBox<uint64_t>{ZuCSpan{value}};
	if (contentLength > MaxBody) {
	  m_state = ParserState::Error;
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLength = contentLength;
	  impl()->contentLength(contentLength);
	}
      } else {
	impl()->template header<Key>(value);
      }
    }

    // process header key/value
    void header_(ZuBSpan key, ZuBSpan value) {
      if constexpr (Keys::N) {
	static constexpr auto kMatcher = ZuMatcher<Keys>();
	auto i = kMatcher.match(key);
	if (i < 0) return;
	ZuSwitch::dispatch<Keys::N>(i, [this, &value](auto i) {
	  using KValues = ZuType<i, Values>;
	  if constexpr (!ZuIsSame<KValues, void>{}) {
	    if constexpr (KValues::N) {
	      static constexpr auto vMatcher = ZuMatcher<KValues>();
	      auto j = vMatcher.match(value);
	      enum { I = i };
	      if (j >= 0) {
		ZuSwitch::dispatch<KValues::N>(j, [this](auto j) {
		  impl()->template header<ZuType<I, Keys>, ZuType<j, KValues>>();
		});
		return;
	      }
	    }
	  }
	  this->template header_<ZuType<i, Keys>>(value);
	});
      }
    }

    // parse request operation line
    void parseOperation(ZuCSpan line) {
      auto error = [this]() {
	m_state = ParserState::Error;
	ZiLOG(Error, "Zhttp", "invalid HTTP operation");
      };
      unsigned n = line.length();
      int o = 0; // intentionally int
      for (o = 0; o < int(n) && line[o] != ' '; )
	if (ZuUnlikely(++o > 7)) { error(); return; } // unterminated method
      if (ZuUnlikely(!o || o >= int(n))) { error(); return; } // missing method
      Method::T method = Method::lookup({&line[0], unsigned(o)});
      unsigned b = ++o;
      while (o < int(n) && line[o] != ' ') ++o;
      if (ZuUnlikely(b == unsigned(o) || o >= int(n))) error();
      ZuCSpan path{&line[b], unsigned(o) - b};
      b = ++o;
      if (ZuUnlikely(b >= n)) { error(); return; } // missing protocol
      impl()->operation(method, path);
      m_state = ParserState::Headers;
    }

    // parse response status line
    void parseStatus(ZuCSpan line) {
      auto error = [this]() {
	m_state = ParserState::Error;
	ZiLOG(Error, "Zhttp", "invalid HTTP response status");
      };
      unsigned n = line.length();
      int o = 0; // intentionally int
      for (o = 0; o < int(n) && line[o] != ' '; )
	if (ZuUnlikely(++o > 8)) { error(); return; } // unterminated protocol
      if (ZuUnlikely(!o || o >= int(n))) { error(); return; } // missing protocol
      unsigned b = ++o;
      int c; // intentionally int
      unsigned code = 0;
      while (o < int(n) && (c = line[o]) != ' ') {
	if (c < '0' || c > '9') { error(); return; } // not a number
	c -= '0';
	code = code ? (code * 10) + c : c;
	if (ZuUnlikely(++o > int(b + 3))) { error(); return; }
      }
      if (ZuUnlikely(b == unsigned(o) || o >= int(n))) { error(); return; }
      impl()->status(code);
      m_state = ParserState::Headers;
    }

  public:
    // top-level process
    template <typename Stream>
    ParserState::T process(Stream &stream) {
      int64_t consumed = 0;
      do {
	consumed = 0;
	switch (m_state) {
	  default:
	    break;
	  case ParserState::Initial: { // parse first line
	    consumed = parseLine<false>(stream, [this](ZuSpan<uint8_t> line) {
	      if constexpr (Request)
		parseOperation(line);
	      else
		parseStatus(line);
	    });
	  } break;
	  case ParserState::Headers: { // parse headers
	    consumed = parseLine<true>(stream, [this](ZuSpan<uint8_t> line) {
	      if (!line) {
		if (m_chunked) {
		  m_state = ParserState::ChunkHdr;
		  m_contentLength = 0;
		} else if (m_contentLength > 0)
		  m_state = ParserState::Body;
		else
		  m_state = ParserState::Complete;
	      } else
		if (!parseKV(line, [this](ZuBSpan key, ZuBSpan value) {
		  this->header_(key, value);
		}))
		  m_state = ParserState::Error;
	    });
	  } break;
	  case ParserState::Body: { // parse body
	    consumed = stream.consume(
	      [this](ZuBSpan span) {
		auto n = span.length();
		if (n > m_contentLength) n = m_contentLength;
		if (!(m_contentLength -= n)) m_state = ParserState::Complete;
		return n;
	      }, [this](ZuBSpan span) { impl()->body(span); });
	    } break;
	  case ParserState::ChunkHdr: { // parse chunk header
	    consumed = stream.template consume<2, ZuStringT<"Zhttp.ChunkHdr">>(
	      crlf<false>(), [this](ZuSpan<uint8_t> span) {
		if (!span) { m_state = ParserState::Complete; return; }
		auto error = [this]() {
		  m_state = ParserState::Error;
		  ZiLOG(Error, "Zhttp", "invalid chunk-length");
		};
		ZuBox<uint64_t> l;
		auto n = l.scan<ZuFmt::Hex<>>(ZuCSpan{span});
		if (!n) { error(); return; }
		// span.offset(n); // chunk extensions are ignored
		m_chunkLength = l;
		if (!m_chunkLength) {
		  m_state = ParserState::Trailers;
		  return;
		}
		if (m_chunkLength > MaxBody) { error(); return; }
		if ((m_contentLength += m_chunkLength) > MaxBody) { error(); return; }
		m_state = ParserState::Chunk;
	      });
	  } break;
	  case ParserState::Chunk: { // parse chunk data
	    consumed = stream.consume(
	      [this](ZuBSpan span) {
		auto n = span.length();
		if (n > m_chunkLength) n = m_chunkLength;
		if (!(m_chunkLength -= n)) m_state = ParserState::ChunkTrlr;
		return n;
	      }, [this](ZuBSpan span) { impl()->body(span); });
	  } break;
	  case ParserState::ChunkTrlr: { // parse trailing "\r\n"
	    consumed = stream.template consume<2, ZuStringT<"Zhttp.ChunkTrlr">>(
	      [this, prevCR = false](ZuBSpan span) mutable -> int64_t {
		auto error = [this]() {
		  m_state = ParserState::Error;
		  ZiLOG(Error, "Zhttp", "invalid chunk trailer");
		  return -1;
		};
		if (prevCR && span[0] == '\n') {
		  m_state = ParserState::ChunkHdr;
		  return 1;
		}
		if (span.length() == 1) {
		  if (span[0] == '\r') { prevCR = true; return 0; }
		  return error();
		}
		if (span[0] != '\r' || span[1] != '\n') return error();
		m_state = ParserState::ChunkHdr;
		return 2;
	      }, [](ZuBSpan) { });
	  } break;
	  case ParserState::Trailers: { // parse trailers
	    consumed = parseLine<true>(stream, [this](ZuSpan<uint8_t> line) {
	      if (!line)
		m_state = ParserState::Complete;
	      else
		if (!parseKV(line, [this](ZuBSpan key, ZuBSpan value) {
		  this->header_(key, value);
		}))
		  m_state = ParserState::Error;
	    });
	  } break;
	}
	if (m_state == ParserState::Complete ||
	    m_state == ParserState::Error) {
	  impl()->complete(m_state);
	  break;
	}
      } while (consumed);
      return m_state;
    }

    // reset for next message
    void reset() {
      m_state = ParserState::Initial;
      m_chunked = false;
      m_contentLength = -1;
      m_chunkLength = -1;
    }

    // CRTP defaults
    void operation(Method::T, ZuBSpan) { }
    void status(unsigned) { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Key, typename Value> void header() { }
    void contentLength(uint64_t) { }
    void xferCompression(XferCompression::T) { }
    void chunked() { }
    void body(ZuBSpan) { }
    void complete(ParserState::T) { }

  private:
    int64_t		m_contentLength = -1;
    int64_t		m_chunkLength = -1;
    ParserState::T	m_state = ParserState::Initial;
    bool		m_chunked = false;
  };

} // H1

// QPack static table compile-time lookup definition
#define Zhttp_QPack_1(Key) \
  ZuTypeList<ZuStringT<Key>, void>
#define Zhttp_QPack_2(Key, Value) \
  ZuTypeList<ZuStringT<Key>, ZuStringT<Value>>
#define Zhttp_QPack_N(_0, _1, Fn, ...) Fn
#define Zhttp_QPack_(...) \
  Zhttp_QPack_N(__VA_ARGS__, \
    Zhttp_QPack_2, \
    Zhttp_QPack_1)(__VA_ARGS__)
#define Zhttp_QPack(KV) \
  ZuPP_Defer(Zhttp_QPack_)(ZuPP_Strip(KV))
#define ZhttpQPackTbl(...) \
  ZuTypeList<ZuPP_Eval_(ZuPP_MapComma(Zhttp_QPack,  __VA_ARGS__))>

namespace H3 {

  // QPACK static table

  template <typename Headers>
  using Keys = ZuTypeSlice<2, 0, Headers>;
  template <typename Headers>
  using Values = ZuTypeSlice<2, 1, Headers>;

  using QPackTbl = ZhttpQPackTbl(
    (":authority"),
    (":path", "/"),
    ("age", "0"),
    ("content-disposition"),
    ("content-length", "0"),
    ("cookie"),
    ("date"),
    ("etag"),
    ("if-modified-since"),
    ("if-none-match"),
    ("last-modified"),
    ("link"),
    ("location"),
    ("referer"),
    ("set-cookie"),
    (":method", "CONNECT"),
    (":method", "DELETE"),
    (":method", "GET"),
    (":method", "HEAD"),
    (":method", "OPTIONS"),
    (":method", "POST"),
    (":method", "PUT"),
    (":scheme", "http"),
    (":scheme", "https"),
    (":status", "103"),
    (":status", "200"),
    (":status", "304"),
    (":status", "404"),
    (":status", "503"),
    ("accept", "*/*"),
    ("accept", "application/dns-message"),
    ("accept-encoding", "gzip, deflate, br"),
    ("accept-ranges", "bytes"),
    ("access-control-allow-headers", "cache-control"),
    ("access-control-allow-headers", "content-type"),
    ("access-control-allow-origin", "*"),
    ("cache-control", "max-age=0"),
    ("cache-control", "max-age=2592000"),
    ("cache-control", "max-age=604800"),
    ("cache-control", "no-cache"),
    ("cache-control", "no-store"),
    ("cache-control", "public, max-age=31536000"),
    ("content-encoding", "br"),
    ("content-encoding", "gzip"),
    ("content-type", "application/dns-message"),
    ("content-type", "application/javascript"),
    ("content-type", "application/json"),
    ("content-type", "application/x-www-form-urlencoded"),
    ("content-type", "image/gif"),
    ("content-type", "image/jpeg"),
    ("content-type", "image/png"),
    ("content-type", "text/css"),
    ("content-type", "text/html; charset=utf-8"),
    ("content-type", "text/plain"),
    ("content-type", "text/plain;charset=utf-8"),
    ("range", "bytes=0-"),
    ("strict-transport-security", "max-age=31536000"),
    ("strict-transport-security", "max-age=31536000; includesubdomains"),
    ("strict-transport-security", "max-age=31536000; includesubdomains; preload"),
    ("vary", "accept-encoding"),
    ("vary", "origin"),
    ("x-content-type-options", "nosniff"),
    ("x-xss-protection", "1; mode=block"),
    (":status", "100"),
    (":status", "204"),
    (":status", "206"),
    (":status", "302"),
    (":status", "400"),
    (":status", "403"),
    (":status", "421"),
    (":status", "425"),
    (":status", "500"),
    ("accept-language"),
    ("access-control-allow-credentials", "FALSE"),
    ("access-control-allow-credentials", "TRUE"),
    ("access-control-allow-headers", "*"),
    ("access-control-allow-methods", "get"),
    ("access-control-allow-methods", "get, post, options"),
    ("access-control-allow-methods", "options"),
    ("access-control-expose-headers", "content-length"),
    ("access-control-request-headers", "content-type"),
    ("access-control-request-method", "get"),
    ("access-control-request-method", "post"),
    ("alt-svc", "clear"),
    ("authorization"),
    ("content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'"),
    ("early-data", "1"),
    ("expect-ct"),
    ("forwarded"),
    ("if-range"),
    ("origin"),
    ("purpose", "prefetch"),
    ("server"),
    ("timing-allow-origin", "*"),
    ("upgrade-insecure-requests", "1"),
    ("user-agent"),
    ("x-forwarded-for"),
    ("x-frame-options", "deny"),
    ("x-frame-options", "sameorigin"));

  // evaluates QPACK static table index I given <Key, Value>
  // - use <Key, void> for entries which are Key only
  // - evaluates to -1 if <Key, Value> are not in table
  template <typename Key, typename Value, typename = void>
  struct QPackIndex_ { using T = ZuInt<-1>; };
  template <typename Key, typename Value>
  struct QPackIndex_<
    Key, Value,
    decltype(ZuTypeIndex<ZuTypeList<Key, Value>, QPackTbl>(), void())>
  {
    using T = ZuTypeIndex<ZuTypeList<Key, Value>, QPackTbl>;
  };
  template <typename Key, typename Value>
  using QPackIndex = typename QPackIndex_<Key, Value>::T;

  // evaluates QPACK static table key, value given an index I
  // - undefined if I is out of range
  template <unsigned I>
  using QPackKV = ZuType<I, QPackTbl>;
  template <typename KV, bool = (KV::N > 1)>
  struct QPackValue_ { using T = void; };
  template <typename KV>
  struct QPackValue_<KV, true> { using T = ZuType<1, KV>; };
  template <typename KV>
  using QPackValue = typename QPackValue_<KV>::T;

  namespace ParserState {
    ZtEnum(ParserState, int8_t,
      Initial,        // expecting first HEADERS frame
      Body,           // after initial HEADERS; accepting DATA or trailers
      Trailers,       // trailing HEADERS received; no more frames allowed
      Complete,       // stream FIN / closed cleanly
      Cancelled,      // RESET_STREAM / STOP_SENDING / app cancellation
      Error);         // invalid frame sequence or decode failure
  }

  template <
    typename Impl,
    bool Request_ = false,
    typename Headers_ = ZuTypeList<>,
    uint64_t MaxBody_ = DefltMaxBody>
  struct Parser {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

    enum { Request = Request_ };
    using Headers = typename Headers_::template Unshift<
      ZuStringT<"content-length">, void>;
    using HeaderKeys = ZuTypeSlice<2, 0, Headers>;
    using HeaderValues = ZuTypeSlice<2, 1, Headers>;
    static constexpr uint64_t MaxBody = MaxBody_;

  private:
    int64_t consumeFrame_(ZuBSpan span) {
      for (unsigned o = 0; o < span.length(); ++o) {
	uint8_t c = span[o];
	if (!m_frameField) {
	  m_frameField = 1;
	  m_frameFieldLen = 1U << (c >> 6);
	  m_frameFieldBytes = 1;
	  m_frameType = c & 0x3f;
	  if (m_frameFieldBytes < m_frameFieldLen) continue;
	  m_frameField = 2;
	  m_frameFieldLen = 0;
	}
	if (m_frameField == 1) {
	  m_frameType = (m_frameType << 8) | c;
	  if (++m_frameFieldBytes < m_frameFieldLen) continue;
	  m_frameField = 2;
	  m_frameFieldLen = 0;
	}
	if (m_frameField == 2) {
	  m_frameFieldLen = 1U << (c >> 6);
	  m_frameFieldBytes = 1;
	  m_frameLength = c & 0x3f;
	  m_frameField = 3;
	  if (m_frameFieldBytes < m_frameFieldLen) continue;
	  m_frameField = 4;
	  m_frameSeen = 0;
	} else if (m_frameField == 3) {
	  m_frameLength = (m_frameLength << 8) | c;
	  if (++m_frameFieldBytes < m_frameFieldLen) continue;
	  m_frameField = 4;
	  m_frameSeen = 0;
	}
	if (m_frameField == 4) {
	  uint64_t avail = span.length() - o - 1;
	  if (avail >= m_frameLength - m_frameSeen)
	    return o + 1 + int64_t(m_frameLength - m_frameSeen);
	  m_frameSeen += avail + 1;
	  return 0;
	}
      }
      return 0;
    }

    static int decodePref_(ZuCSpan in, unsigned &o, unsigned bits,
      uint64_t &v, uint8_t *firstByte = nullptr) {
      if (o >= in.length() || !bits || bits > 8) return -1;
      uint8_t mask = uint8_t((1U << bits) - 1U);
      uint8_t first = uint8_t(in[o++]);
      if (firstByte) *firstByte = first;
      v = first & mask;
      if (v < mask) return 0;
      unsigned shift = 0;
      for (;;) {
	if (o >= in.length() || shift >= 56) return -1;
	uint8_t b = uint8_t(in[o++]);
	v += uint64_t(b & 0x7f) << shift;
	if (!(b & 0x80)) return 0;
	shift += 7;
      }
    }

    static int decodeString_(ZuCSpan in, unsigned &o, unsigned bits,
      uint8_t huffmanMask, ZuCSpan &out) {
      uint64_t len = 0;
      uint8_t first = 0;
      if (decodePref_(in, o, bits, len, &first) < 0 ||
	  (first & huffmanMask) || in.length() < o + len)
	return -1;
      out = ZuCSpan{in.data() + o, unsigned(len)};
      o += unsigned(len);
      return int(len);
    }

    template <unsigned I>
    void staticHeader_() {
      using KV = QPackKV<I>;
      using Key = ZuType<0, KV>;
      using Value = QPackValue<KV>;
      if constexpr (ZuIsSame<Value, void>{})
	qpackHeader_(Key{}(), "");
      else
	qpackHeader_(Key{}(), Value{}());
    }

    bool qpackHeader_(ZuCSpan name, ZuCSpan value) {
      if (!name) return false;
      if (name[0] == ':') {
	if constexpr (Request) {
	  if (name == ":method") {
	    Method::T method = Method::lookup(value);
	    if (method < 0) return false;
	    m_method = method;
	    m_hasMethod = true;
	    return true;
	  }
	  if (name == ":path") {
	    m_path = value;
	    m_hasPath = true;
	    return true;
	  }
	  return name == ":scheme" || name == ":authority";
	} else {
	  if (name != ":status" || value.length() != 3) return false;
	  unsigned status = 0;
	  for (unsigned i = 0; i < 3; ++i) {
	    if (value[i] < '0' || value[i] > '9') return false;
	    status = (status * 10) + unsigned(value[i] - '0');
	  }
	  impl()->status(status);
	  m_hasStatus = true;
	  return true;
	}
      }
      uint8_t key_[256];
      if (name.length() > 256) return false;
      ZuSpan key{key_, name.length()};
      memcpy(key.data(), name.data(), name.length());
      normalize(key);
      header_(key, ZuBSpan{
	reinterpret_cast<const uint8_t *>(value.data()), value.length()});
      return true;
    }

    template <typename Key> void header_(ZuBSpan value) {
      if constexpr (ZuIsSame<Key, ZuStringT<"content-length">>{}) {
	uint64_t contentLength = ZuBox<uint64_t>{ZuCSpan{value}};
	if (contentLength > MaxBody) {
	  m_state = ParserState::Error;
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLength = contentLength;
	  impl()->contentLength(contentLength);
	}
      } else {
	impl()->template header<Key>(value);
      }
    }

    void header_(ZuBSpan key, ZuBSpan value) {
      if constexpr (HeaderKeys::N) {
	static constexpr auto kMatcher = ZuMatcher<HeaderKeys>();
	auto i = kMatcher.match(key);
	if (i < 0) return;
	ZuSwitch::dispatch<HeaderKeys::N>(i, [this, &value](auto i) {
	  using KValues = ZuType<i, HeaderValues>;
	  if constexpr (!ZuIsSame<KValues, void>{}) {
	    if constexpr (requires { KValues::N; }) {
	      static constexpr auto vMatcher = ZuMatcher<KValues>();
	      auto j = vMatcher.match(value);
	      enum { I = i };
	      if (j >= 0) {
		ZuSwitch::dispatch<KValues::N>(j, [this](auto j) {
		  impl()->template header<
		    ZuType<I, HeaderKeys>, ZuType<j, KValues>>();
		});
		return;
	      }
	    } else if (ZuCSpan{value} == KValues{}()) {
	      if constexpr (requires(Impl *impl_) {
		impl_->template header<ZuType<i, HeaderKeys>, KValues>();
	      })
		impl()->template header<ZuType<i, HeaderKeys>, KValues>();
	      return;
	    }
	  }
	  this->template header_<ZuType<i, HeaderKeys>>(value);
	});
      }
    }

    bool parseFields_(ZuCSpan payload) {
      unsigned o = 0;
      uint64_t requiredInsertCount = 0, base = 0;
      uint8_t baseFirst = 0;
      if (decodePref_(payload, o, 8, requiredInsertCount) < 0 ||
	  requiredInsertCount ||
	  decodePref_(payload, o, 7, base, &baseFirst) < 0 ||
	  (baseFirst & 0x80) || base)
	return false;

      while (o < payload.length()) {
	uint8_t first = uint8_t(payload[o]);
	ZuCSpan name;
	ZuCSpan value;

	if (first & 0x80) {
	  uint64_t index = 0;
	  uint8_t indexFirst = 0;
	  if (decodePref_(payload, o, 6, index, &indexFirst) < 0 ||
	      !(indexFirst & 0x40) || index >= QPackTbl::N)
	    return false;
	  bool ok = false;
	  ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
	    [this, &ok](auto i) {
	      this->template staticHeader_<i>();
	      ok = true;
	    });
	  if (!ok) return false;
	  continue;
	}

	if ((first & 0xc0) == 0x40) {
	  uint64_t index = 0;
	  uint8_t nameFirst = 0;
	  if (decodePref_(payload, o, 4, index, &nameFirst) < 0 ||
	      !(nameFirst & 0x10) || index >= QPackTbl::N)
	    return false;
	  bool ok = false;
	  ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
	    [&name, &ok](auto i) {
	      using KV = QPackKV<i>;
	      name = ZuType<0, KV>{}();
	      ok = true;
	    });
	  if (!ok ||
	      decodeString_(payload, o, 7, 0x80, value) < 0 ||
	      !qpackHeader_(name, value))
	    return false;
	  continue;
	}

	if ((first & 0xe0) == 0x20) {
	  if (decodeString_(payload, o, 3, 0x08, name) < 0 ||
	      decodeString_(payload, o, 7, 0x80, value) < 0 ||
	      !qpackHeader_(name, value))
	    return false;
	  continue;
	}

	return false;
      }

      if constexpr (Request) {
	if (!m_hasMethod || !m_hasPath) return false;
	impl()->operation(m_method, ZuBSpan{
	  reinterpret_cast<const uint8_t *>(m_path.data()), m_path.length()});
      } else {
	if (!m_hasStatus) return false;
      }
      return true;
    }

    bool processFrame_(ZuCSpan frameBytes) {
      unsigned o = 0;
      uint64_t type = 0, len = 0;
      if (decodeVar_(frameBytes, o, type) < 0 ||
	  decodeVar_(frameBytes, o, len) < 0 ||
	  frameBytes.length() != o + len)
	return false;
      ZuCSpan payload{frameBytes.data() + o, unsigned(len)};

      if (type == 0x01) { // HEADERS
	if (m_state == ParserState::Initial) {
	  if (!parseFields_(payload)) return false;
	  m_state = ParserState::Body;
	  return true;
	}
	if (m_state != ParserState::Body) return false;
	if (!parseFields_(payload)) return false;
	m_state = ParserState::Trailers;
	return true;
      }
      if (type == 0x00) { // DATA
	if (m_state != ParserState::Body) return false;
	if (payload.length() > MaxBody ||
	    (m_bodyBytes += payload.length()) > MaxBody)
	  return false;
	impl()->body(ZuBSpan{
	  reinterpret_cast<const uint8_t *>(payload.data()), payload.length()});
	return true;
      }
      return true; // ignore unknown extension frames on request streams
    }

    static int decodeVar_(ZuCSpan in, unsigned &o, uint64_t &v) {
      if (o >= in.length()) return -1;
      uint8_t c = uint8_t(in[o++]);
      unsigned len = 1U << (c >> 6);
      v = c & 0x3f;
      if (in.length() < o + len - 1) return -1;
      for (unsigned i = 1; i < len; ++i) v = (v << 8) | uint8_t(in[o++]);
      return 0;
    }
 
  public:
    // top-level process
    template <typename Stream>
    ParserState::T process(Stream &stream) {
      if (stream.resetReceived() || stream.stopReceived()) {
	m_state = ParserState::Cancelled;
	impl()->complete(m_state);
	return m_state;
      }

      auto &rx = stream.rxStream();
      int64_t consumed = 0;
      do {
	consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Frame">>(
	  [this](ZuBSpan span) { return this->consumeFrame_(span); },
	  [this](ZuBSpan span) {
	    if (!this->processFrame_(ZuCSpan{
		  reinterpret_cast<const char *>(span.data()), span.length()}))
	      m_state = ParserState::Error;
	    m_frameField = 0;
	    m_frameFieldLen = m_frameFieldBytes = 0;
	    m_frameType = m_frameLength = m_frameSeen = 0;
	  });
	if (consumed < 0) m_state = ParserState::Error;
	if (m_state == ParserState::Error) break;
      } while (consumed);

      if (stream.finReceived() && rx.empty()) {
	if (m_state == ParserState::Initial)
	  m_state = ParserState::Error;
	else if (m_state != ParserState::Error)
	  m_state = ParserState::Complete;
      }
      if (m_state == ParserState::Complete ||
	  m_state == ParserState::Cancelled ||
	  m_state == ParserState::Error)
	impl()->complete(m_state);
      return m_state;
    }

    void reset() {
      m_state = ParserState::Initial;
      m_method = -1;
      m_path = {};
      m_contentLength = -1;
      m_bodyBytes = 0;
      m_hasMethod = m_hasPath = m_hasStatus = false;
      m_frameField = 0;
      m_frameFieldLen = m_frameFieldBytes = 0;
      m_frameType = m_frameLength = m_frameSeen = 0;
    }

    void operation(Method::T, ZuBSpan) { }
    void status(unsigned) { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Key, typename Value> void header() { }
    void contentLength(uint64_t) { }
    void body(ZuBSpan) { }
    void complete(ParserState::T) { }

  private:
    int64_t		m_contentLength = -1;
    uint64_t		m_bodyBytes = 0;
    Method::T		m_method = -1;
    ZuCSpan		m_path;
    ParserState::T	m_state = ParserState::Initial;
    uint8_t		m_frameField = 0;
    unsigned		m_frameFieldLen = 0;
    unsigned		m_frameFieldBytes = 0;
    uint64_t		m_frameType = 0;
    uint64_t		m_frameLength = 0;
    uint64_t		m_frameSeen = 0;
    bool		m_hasMethod = false;
    bool		m_hasPath = false;
    bool		m_hasStatus = false;
  };

} // H3

// HTTP message builder

namespace H1 {

  // HTTP 1.1 non-chunked body Tx streaming
  template <typename Lower>
  struct BodyStream : public ZiTxLayer<BodyStream<Lower>, Lower> {
    using Base = ZiTxLayer<BodyStream<Lower>, Lower>;

    BodyStream(Lower &lower, uint64_t contentLength_) :
      Base(lower, 0, 0), m_contentLength(contentLength_)  { }

    void prepareBuf_(ZiIOBuf *buf) {
      ZiAssert(m_contentLength >= buf->length,
	"Zhttp", (), "oversized body", buf->length = m_contentLength);
      m_contentLength -= buf->length;
    }

  private:
    uint64_t		m_contentLength;
  };
  template <typename Lower>
  auto bodyStream(Lower &lower, uint64_t contentLength) {
    return BodyStream<Lower>(lower, contentLength);
  }

  // HTTP 1.1 chunked body Tx streaming
  template <typename Lower>
  struct ChunkedStream : public ZiTxLayer<ChunkedStream<Lower>, Lower> {
    enum { HdrSize = 10 };	// 8 bytes of hex length + CRLF
    enum { TrlrSize = 2 };	// CRLF

    using Base = ZiTxLayer<ChunkedStream<Lower>, Lower>;

    ChunkedStream(Lower &lower) : Base(lower, HdrSize, TrlrSize) { }

    void prepareBuf_(ZiIOBuf *buf) {
      ZuBox<uint32_t> n = buf->length;
      ZiAssert(buf->skip >= HdrSize,
	"Zhttp", (), "ChunkedStream headroom error", return);
      // chunk header
      buf->rewind(HdrSize);
      {
	ZuStream s(buf->data(), HdrSize);
	using Fmt = ZuFmt::Hex<1, ZuFmt::Right<8>>;
	s << n.fmt<Fmt>() << "\r\n";
      }
      // chunk trailer
      *buf << "\r\n";
    }
  };
  template <typename Lower>
  auto chunkedStream(Lower &lower) {
    return ChunkedStream<Lower>(lower);
  }

} // H1

namespace H3 {

  template <typename Bytes>
  inline int putVar(Bytes &out, uint64_t v) {
    if (v < (1ULL<<6)) {
      out.push(uint8_t(v));
      return 0;
    }
    if (v < (1ULL<<14)) {
      out.push(uint8_t(0x40 | (v >> 8)));
      out.push(uint8_t(v));
      return 0;
    }
    if (v < (1ULL<<30)) {
      out.push(uint8_t(0x80 | (v >> 24)));
      out.push(uint8_t(v >> 16));
      out.push(uint8_t(v >> 8));
      out.push(uint8_t(v));
      return 0;
    }
    if (v < (1ULL<<62)) {
      out.push(uint8_t(0xc0 | (v >> 56)));
      out.push(uint8_t(v >> 48));
      out.push(uint8_t(v >> 40));
      out.push(uint8_t(v >> 32));
      out.push(uint8_t(v >> 24));
      out.push(uint8_t(v >> 16));
      out.push(uint8_t(v >> 8));
      out.push(uint8_t(v));
      return 0;
    }
    return -1;
  }

  template <typename Bytes>
  inline int putPref(Bytes &out, uint8_t prefix, unsigned bits, uint64_t v) {
    if (!bits || bits > 8) return -1;
    uint8_t mask = uint8_t((1U << bits) - 1U);
    if (v < mask) {
      out.push(prefix | uint8_t(v));
      return 0;
    }
    out.push(prefix | mask);
    v -= mask;
    while (v >= 128) {
      out.push(uint8_t((v & 0x7f) | 0x80));
      v >>= 7;
    }
    out.push(uint8_t(v));
    return 0;
  }

  template <typename Bytes>
  inline int putString(Bytes &out, uint8_t prefix, unsigned bits, ZuCSpan s) {
    if (putPref(out, prefix, bits, s.length()) < 0) return -1;
    for (unsigned i = 0; i < s.length(); ++i) out.push(uint8_t(s[i]));
    return 0;
  }

  template <typename Bytes, typename Stream>
  inline int writeFrame(Stream &stream, uint64_t type, const Bytes &payload) {
    auto tx = stream.txStream();
    Bytes frame;
    if (putVar(frame, type) < 0 || putVar(frame, payload.length()) < 0)
      return -1;
    for (unsigned i = 0; i < payload.length(); ++i)
      frame.push(payload.data()[i]);
    tx << ZuCSpan{
      reinterpret_cast<const char *>(frame.data()), frame.length()};
    tx.flush();
    return frame.length();
  }

  template <typename Bytes>
  inline int encodeFieldPrefix(Bytes &out) {
    out.push(0); // Required Insert Count
    out.push(0); // Delta Base
    return 0;
  }

  template <typename Key, typename Value>
  inline constexpr bool HasQPackIndex =
    (QPackIndex<Key, Value>{} >= 0);

  template <typename Bytes, typename Key, typename Value>
  inline int encodeKnownField(Bytes &out) {
    if constexpr (HasQPackIndex<Key, Value>) {
      return putPref(out, 0xc0, 6, QPackIndex<Key, Value>{});
    } else if constexpr (HasQPackIndex<Key, void>) {
      if (putPref(out, 0x50, 4, QPackIndex<Key, void>{}) < 0)
	return -1;
      return putString(out, 0, 7, Value{}());
    } else {
      if (putString(out, 0x20, 3, Key{}()) < 0) return -1;
      return putString(out, 0, 7, Value{}());
    }
  }

  template <typename Bytes, typename Key>
  inline int encodeVariableField(Bytes &out, ZuCSpan value) {
    if constexpr (HasQPackIndex<Key, void>) {
      if (putPref(out, 0x50, 4, QPackIndex<Key, void>{}) < 0)
	return -1;
      return putString(out, 0, 7, value);
    } else {
      if (putString(out, 0x20, 3, Key{}()) < 0) return -1;
      return putString(out, 0, 7, value);
    }
  }

  template <typename Lower>
  struct DataStream : public ZiTxLayer<DataStream<Lower>, Lower> {
    using Base = ZiTxLayer<DataStream<Lower>, Lower>;

    DataStream(Lower &lower) : Base(lower, 9, 0) { }

    void prepareBuf_(ZiIOBuf *buf) {
      uint8_t hdr[16];
      ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.FrameHdr">> frameHdr;
      putVar(frameHdr, 0);
      putVar(frameHdr, buf->length);
      ZiAssert(buf->skip >= frameHdr.length(),
	"Zhttp", (), "H3 DataStream headroom error", return);
      memcpy(hdr, frameHdr.data(), frameHdr.length());
      buf->rewind(frameHdr.length());
      memcpy(buf->data(), hdr, frameHdr.length());
    }
  };

  template <typename Lower>
  auto dataStream(Lower &lower) { return DataStream<Lower>(lower); }

} // H3

// CRTP - implementation must conform to the following interface:
#if 0
struct Impl : public Builder<Impl, ...> {
  using Base = Builder<Impl, ...>;

  // optional - if implemented, must call Base::reset()
  void reset();

  // optional - defaults to true
  bool request();

  // optional - defaults to { l(Method::GET, "/", ""); }
  template <typename L> void operation(L &&l);

  // optional - defaults to { l("127.0.0.1"); }
  template <typename L> void host(L &&l);

  // optional - defaults to 200
  unsigned status();

  // optional - defaults to { l(""); }
  template <typename L> void reason(L &&l);

  // optional - defaults to { l(""); }
  template <typename Key, typename L>
  void header(L &&l);

  // optional - defaults to 0
  // - only called if HasBody && !Chunked
  uint64_t contentLength();
};
#endif

namespace H1 {

  template <
    typename Impl,
    typename Headers_ = ZuTypeList<>,
    typename Trailers_ = ZuTypeList<>,	// ignored if not chunked
    bool HasBody_ = false,		// has a body
    bool Chunked_ = false>		// body is chunked
  struct Builder {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

    using Headers = Headers_;
    using Trailers = Trailers_;
    enum { HasBody = HasBody_ };
    enum { Chunked = Chunked_ };

  private:
    template <typename KVs = Headers, typename Stream>
    void headers_(Stream &stream) {
      // header key/values
      {
	using Keys = ZuTypeSlice<2, 0, KVs>;
	using Values = ZuTypeSlice<2, 1, KVs>;
	ZuUnroll::all<Keys>([this, &stream]<typename Key>() {
	  using Value = ZuType<ZuTypeIndex<Key, Keys>{}, Values>;
	  if constexpr (!ZuIsSame<Value, void>{})
	    stream << Key{}() << ": " << Value{}() << "\r\n";
	  else {
	    stream << Key{}() << ": ";
	    impl()->template header<Key>([&stream]<typename Value>(Value &&value) {
	      stream << ZuFwd<Value>(value);
	    });
	    stream << "\r\n";
	  }
	});
      }
      // end of headers
      stream << "\r\n";
    }
    template <typename Stream>
    void headers(Stream &stream) {
      if constexpr (HasBody) {
	if constexpr (Chunked)
	  stream << "transfer-encoding: chunked\r\n";
	else
	  stream << "content-length: " << impl()->contentLength() << "\r\n";
      }
      headers_<Headers>(stream);
    }
    // chunked trailers
    template <typename Stream>
    void trailers(Stream &stream) {
      headers_<Trailers>(stream);
    }

  public:
    // request
    template <typename Stream>
    void request(Stream &stream) {
      impl()->operation([&stream]<typename Path, typename Query>(
	  Method::T method, Path &&path, Query &&query) {
	stream << Method::name(method) << ' ' << ZuFwd<Path>(path) << ZuFwd<Query>(query);
      });
      // host
      stream << " HTTP/1.1\r\nhost: ";
      impl()->host([&stream]<typename Host>(Host &&host) {
	stream << ZuFwd<Host>(host);
      });
      stream << "\r\n";
      // remaining headers
      headers(stream);
    }

    // response
    template <typename Stream>
    void response(Stream &stream) {
      // status + reason
      stream << "HTTP/1.1 " <<
	ZuBox<unsigned>{impl()->status()}.fmt<ZuFmt::Right<3>>() << ' ';
      impl()->reason([&stream]<typename Reason>(Reason &&reason) {
	stream << ZuFwd<Reason>(reason);
      });
      stream << "\r\n";
      // remaining headers
      headers(stream);
    }

    // body
    template <typename Stream, bool _ = HasBody && !Chunked>
    ZuIfT<_, BodyStream<Stream>>
    body(Stream &stream) { return bodyStream(stream, impl()->contentLength()); }

    template <typename Stream, bool _ = HasBody && Chunked>
    ZuIfT<_, ChunkedStream<Stream>>
    body(Stream &stream) { return chunkedStream(stream); }

    // finish
    template <typename Stream>
    void finish(Stream &stream) {
      if constexpr (Chunked) {
	stream << "0\r\n";
	trailers(stream);
      }
      stream.flush();
    }

    void reset() { }

    // CRTP defaults
    bool request() { return true; }
    template <typename L> void operation(L &&l) { l(Method::GET, "/", ""); }
    template <typename L> void host(L &&l) { l("127.0.0.1"); }
    unsigned status() { return 200; }
    template <typename L> void reason(L &&l) { l(""); }
    template <typename Key, typename L>
    void header(L &&l) { l(""); }
    uint64_t contentLength() { return 0; }
  };

} // H1

namespace H3 {

  template <
    typename Impl,
    typename Headers_ = ZuTypeList<>,
    typename Trailers_ = ZuTypeList<>,	// ignored if not chunked
    bool HasBody_ = false,		// has a body
    bool = false>			// ignored for HTTP/3
  struct Builder {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

    using Headers = Headers_;
    using Trailers = Trailers_;
    enum { HasBody = HasBody_ };

  private:
    using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3.Builder">>;

    static ZuCSpan uintSpan_(uint64_t v, char (&buf)[32]) {
      unsigned o = sizeof(buf);
      do {
	buf[--o] = char('0' + (v % 10));
	v /= 10;
      } while (v);
      return ZuCSpan{buf + o, unsigned(sizeof(buf) - o)};
    }

    static ZuCSpan statusSpan_(unsigned status, char (&buf)[4]) {
      buf[0] = char('0' + ((status / 100) % 10));
      buf[1] = char('0' + ((status / 10) % 10));
      buf[2] = char('0' + (status % 10));
      buf[3] = 0;
      return ZuCSpan{buf, 3};
    }

    template <typename KVs = Headers>
    void headers_(Bytes &fields) {
      using HeaderKeys = ZuTypeSlice<2, 0, KVs>;
      using HeaderValues = ZuTypeSlice<2, 1, KVs>;
      ZuUnroll::all<HeaderKeys>([this, &fields]<typename Key>() {
	using Value = ZuType<ZuTypeIndex<Key, HeaderKeys>{}, HeaderValues>;
	if constexpr (!ZuIsSame<Value, void>{}) {
	  if (encodeKnownField<Bytes, Key, Value>(fields) < 0) {
	    ZiLOG(Error, "Zhttp", "failed to encode H3 header");
	    return;
	  }
	} else {
	  impl()->template header<Key>([&fields]<typename V>(V &&v) {
	    if (encodeVariableField<Bytes, Key>(
		  fields, ZuCSpan{ZuFwd<V>(v)}) < 0)
	      ZiLOG(Error, "Zhttp", "failed to encode H3 header");
	  });
	}
      });
    }

    void contentLength_(Bytes &fields) {
      if constexpr (HasBody) {
	char buf[32];
	encodeVariableField<Bytes, ZuStringT<"content-length">>(
	  fields, uintSpan_(impl()->contentLength(), buf));
      }
    }

    static int encodeMethod_(Bytes &fields, Method::T method) {
      if (method == Method::GET)
	return putPref(fields, 0xc0, 6,
	  QPackIndex<ZuStringT<":method">, ZuStringT<"GET">>{});
      if (method == Method::POST)
	return putPref(fields, 0xc0, 6,
	  QPackIndex<ZuStringT<":method">, ZuStringT<"POST">>{});
      return encodeVariableField<Bytes, ZuStringT<":method">>(
	fields, Method::name(method));
    }

    static int encodePath_(Bytes &fields, ZuCSpan path, ZuCSpan query) {
      if (!query && path == "/")
	return putPref(fields, 0xc0, 6,
	  QPackIndex<ZuStringT<":path">, ZuStringT<"/">>{});
      if (!query)
	return encodeVariableField<Bytes, ZuStringT<":path">>(fields, path);
      Bytes value;
      for (unsigned i = 0; i < path.length(); ++i)
	value.push(uint8_t(path[i]));
      value.push('?');
      for (unsigned i = 0; i < query.length(); ++i)
	value.push(uint8_t(query[i]));
      return encodeVariableField<Bytes, ZuStringT<":path">>(
	fields, ZuCSpan{
	  reinterpret_cast<const char *>(value.data()), value.length()});
    }

    template <typename Stream>
    void writeHeaders_(Stream &stream, Bytes &fields) {
      if (writeFrame<Bytes>(stream, 0x01, fields) < 0)
	ZiLOG(Error, "Zhttp", "failed to write H3 headers");
    }

    // request
  public:
    template <typename Stream>
    void request(Stream &stream) {
      Bytes fields;
      encodeFieldPrefix(fields);
      impl()->operation([this, &fields]<typename Path, typename Query>(
	  Method::T method, Path &&path, Query &&query) {
	encodeMethod_(fields, method);
	encodePath_(fields, ZuCSpan{ZuFwd<Path>(path)},
	  ZuCSpan{ZuFwd<Query>(query)});
      });
      putPref(fields, 0xc0, 6,
	QPackIndex<ZuStringT<":scheme">, ZuStringT<"https">>{});
      impl()->host([&fields]<typename Host>(Host &&host) {
	encodeVariableField<Bytes, ZuStringT<":authority">>(
	  fields, ZuCSpan{ZuFwd<Host>(host)});
      });
      contentLength_(fields);
      headers_(fields);
      writeHeaders_(stream, fields);
    }

    // response
    template <typename Stream>
    void response(Stream &stream) {
      Bytes fields;
      encodeFieldPrefix(fields);
      unsigned status = impl()->status();
      if (status == 200)
	putPref(fields, 0xc0, 6,
	  QPackIndex<ZuStringT<":status">, ZuStringT<"200">>{});
      else {
	char buf[4];
	encodeVariableField<Bytes, ZuStringT<":status">>(
	  fields, statusSpan_(status, buf));
      }
      contentLength_(fields);
      headers_(fields);
      writeHeaders_(stream, fields);
    }

    // body
    template <typename Stream>
    auto body(Stream &stream) {
      auto tx = stream.txStream();
      return dataStream(tx);
    }

    // finish
    template <typename Stream>
    void finish(Stream &stream) {
      if constexpr (Trailers::N) {
	Bytes fields;
	encodeFieldPrefix(fields);
	headers_<Trailers>(fields);
	writeHeaders_(stream, fields);
      }
      stream.fin();
    }

    void reset() { }

    bool request() { return true; }
    template <typename L> void operation(L &&l) { l(Method::GET, "/", ""); }
    template <typename L> void host(L &&l) { l("127.0.0.1"); }
    unsigned status() { return 200; }
    template <typename L> void reason(L &&l) { l(""); }
    template <typename Key, typename L>
    void header(L &&l) { l(""); }
    uint64_t contentLength() { return 0; }
  };

} // H3

} // Zhttp

#endif /* Zhttp_HH */
