//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP 1.1 and HTTP/3
// - HTTP 1.1:
//   - optionally chunked body
//   - optional chunked trailers (rarely used feature)
// - caller is responsible for body decompression (if required)

#ifndef Zhttp_HH
#define Zhttp_HH

#include <zlib/ZhttpLib.hh>
#include <zlib/ZhttpQPack.hh>

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

  struct ParserState {
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
  };

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
    using State = ParserState;

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
	  m_state = State::Error;
	  ZiLOG(Error, "Zhttp", "invalid transfer-encoding");
	}
      } else if constexpr (ZuIsSame<Key, ZuStringT<"content-length">>{}) {
	uint64_t contentLength = ZuBox<uint64_t>{ZuCSpan{value}};
	if (contentLength > MaxBody) {
	  m_state = State::Error;
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLength = contentLength;
	  impl()->contentLength(contentLength);
	}
      } else {
	impl()->template header<Key>(value);
      }
    }

    static bool headerKey_(ZuBSpan key, ZuCSpan name) {
      return key.length() == name.length() &&
	!Zu::stricmp_(
	  reinterpret_cast<const char *>(key.data()), name.data(), key.length());
    }

    // process header key/value
    void header_(ZuBSpan key, ZuBSpan value) {
      if (headerKey_(key, "transfer-encoding")) {
	this->template header_<ZuStringT<"transfer-encoding">>(value);
	return;
      }
      if (headerKey_(key, "content-length")) {
	this->template header_<ZuStringT<"content-length">>(value);
	return;
      }
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
	m_state = State::Error;
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
      m_state = State::Headers;
    }

    // parse response status line
    void parseStatus(ZuCSpan line) {
      auto error = [this]() {
	m_state = State::Error;
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
      m_state = State::Headers;
    }

  public:
    // top-level process
    template <typename Stream>
    State::T process(Stream &stream) {
      int64_t consumed = 0;
      do {
	consumed = 0;
	switch (m_state) {
	  default:
	    break;
	  case State::Initial: { // parse first line
	    consumed = parseLine<false>(stream, [this](ZuSpan<uint8_t> line) {
	      if constexpr (Request)
		parseOperation(line);
	      else
		parseStatus(line);
	    });
	  } break;
	  case State::Headers: { // parse headers
	    consumed = parseLine<true>(stream, [this](ZuSpan<uint8_t> line) {
	      if (!line) {
		if (m_chunked) {
		  m_state = State::ChunkHdr;
		  m_contentLength = 0;
		} else if (m_contentLength > 0)
		  m_state = State::Body;
		else
		  m_state = State::Complete;
	      } else
		if (!parseKV(line, [this](ZuBSpan key, ZuBSpan value) {
		  this->header_(key, value);
		}))
		  m_state = State::Error;
	    });
	  } break;
	  case State::Body: { // parse body
	    consumed = stream.consume(
	      [this](ZuBSpan span) -> int64_t {
		auto n = span.length();
		if (n > m_contentLength) n = m_contentLength;
		if (!(m_contentLength -= n)) m_state = State::Complete;
		return n;
	      }, [this](ZuBSpan span) { impl()->body(span); });
	    } break;
	  case State::ChunkHdr: { // parse chunk header
	    consumed = stream.template consume<2, ZuStringT<"Zhttp.ChunkHdr">>(
	      crlf<false>(), [this](ZuSpan<uint8_t> span) {
		if (!span) { m_state = State::Complete; return; }
		auto error = [this]() {
		  m_state = State::Error;
		  ZiLOG(Error, "Zhttp", "invalid chunk-length");
		};
		ZuBox<uint64_t> l;
		auto n = l.scan<ZuFmt::Hex<>>(ZuCSpan{span});
		if (!n) { error(); return; }
		// span.offset(n); // chunk extensions are ignored
		m_chunkLength = l;
		if (!m_chunkLength) {
		  m_state = State::Trailers;
		  return;
		}
		if (m_chunkLength > MaxBody) { error(); return; }
		if ((m_contentLength += m_chunkLength) > MaxBody) { error(); return; }
		m_state = State::Chunk;
	      });
	  } break;
	  case State::Chunk: { // parse chunk data
	    consumed = stream.consume(
	      [this](ZuBSpan span) -> int64_t {
		auto n = span.length();
		if (n > m_chunkLength) n = m_chunkLength;
		if (!(m_chunkLength -= n)) m_state = State::ChunkTrlr;
		return n;
	      }, [this](ZuBSpan span) { impl()->body(span); });
	  } break;
	  case State::ChunkTrlr: { // parse trailing "\r\n"
	    consumed = stream.template consume<2, ZuStringT<"Zhttp.ChunkTrlr">>(
	      [this, prevCR = false](ZuBSpan span) mutable -> int64_t {
		auto error = [this]() {
		  m_state = State::Error;
		  ZiLOG(Error, "Zhttp", "invalid chunk trailer");
		  return -1;
		};
		if (prevCR && span[0] == '\n') {
		  m_state = State::ChunkHdr;
		  return 1;
		}
		if (span.length() == 1) {
		  if (span[0] == '\r') { prevCR = true; return 0; }
		  return error();
		}
		if (span[0] != '\r' || span[1] != '\n') return error();
		m_state = State::ChunkHdr;
		return 2;
	      }, [](ZuBSpan) { });
	  } break;
	  case State::Trailers: { // parse trailers
	    consumed = parseLine<true>(stream, [this](ZuSpan<uint8_t> line) {
	      if (!line)
		m_state = State::Complete;
	      else
		if (!parseKV(line, [this](ZuBSpan key, ZuBSpan value) {
		  this->header_(key, value);
		}))
		  m_state = State::Error;
	    });
	  } break;
	}
	if (m_state == State::Complete ||
	    m_state == State::Error) {
	  impl()->complete(m_state);
	  break;
	}
      } while (consumed);
      return m_state;
    }

    // reset for next message
    void reset() {
      m_state = State::Initial;
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
    void complete(State::T) { }

  private:
    int64_t	m_contentLength = -1;
    int64_t	m_chunkLength = -1;
    State::T	m_state = State::Initial;
    bool	m_chunked = false;
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

  // HTTP/3 connection state
  struct CxnState {
    ZtEnum(CxnState, int8_t,
      Init,
      LocalControlOpen,
      LocalQPackOpen,
      PeerControlOpen,
      PeerSettingsReceived,
      Ready,
      Goaway,
      Draining,
      Error);
  };

  // QPACK static table
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
  template <typename Key, typename Value,
    bool = ZuTypeIn<ZuTypeList<Key, Value>, QPackTbl>{}>
  struct QPackIndex_ {
    using T = ZuInt<-1>;
  };
  template <typename Key, typename Value>
  struct QPackIndex_<Key, Value, true> {
    using T = ZuTypeIndex<ZuTypeList<Key, Value>, QPackTbl>;
  };
  template <typename Key, typename Value>
  using QPackIndex = typename QPackIndex_<Key, Value>::T;

  template <ZuString Key, ZuString Value>
  using QPackKVIndex = QPackIndex<ZuStringT<Key>, ZuStringT<Value>>;
  template <typename KV>
  using QPackKey = ZuType<0, KV>;
  using QPackKeys = ZuTypeMap<QPackKey, QPackTbl>;
  template <typename Key, bool = ZuTypeIn<Key, QPackKeys>{}>
  struct QPackKeyIndex_ {
    using T = ZuInt<-1>;
  };
  template <typename Key>
  struct QPackKeyIndex_<Key, true> {
    using T = ZuTypeIndex<Key, QPackKeys>;
  };
  template <ZuString Key>
  using QPackKeyIndex = typename QPackKeyIndex_<ZuStringT<Key>>::T;

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

  struct ParserState {
    ZtEnum(ParserState, int8_t,
      Initial,		// expecting first HEADERS frame
      Body,		// after initial HEADERS; accepting DATA or trailers
      Trailers,		// trailing HEADERS received; no more frames allowed
      Complete,		// stream FIN / closed cleanly
      Cancelled,	// RESET_STREAM / STOP_SENDING / app cancellation
      Error);		// invalid frame sequence or decode failure
  };

  namespace FrameState {
    ZtEnum(FrameState, int8_t,
      Type = 0,		// expecting frame type prefix
      TypeCont,		// reading remaining frame type bytes
      Length,		// expecting frame length prefix
      LengthCont,	// reading remaining frame length bytes
      Payload);		// reading frame payload bytes
  }

  struct CxnStreamState {
	  ZtEnum(CxnStreamState, int8_t,
	    Type,		// reading stream type
	    Control,		// HTTP/3 control stream frames
	    QPackEncoder,	// peer QPACK encoder stream bytes
	    QPackDecoder,	// peer QPACK decoder stream bytes
	    Extension,	// unknown extension stream bytes
      Complete,		// stream FIN / closed cleanly
      Cancelled,	// RESET_STREAM / STOP_SENDING
      Error);		// invalid connection stream
  };

  static inline int decodeVar(ZuCSpan in, unsigned &o, uint64_t &v) {
    if (o >= in.length()) return -1;
    uint8_t c = uint8_t(in[o++]);
    unsigned len = 1U << (c >> 6);
    v = c & 0x3f;
    if (in.length() < o + len - 1) return -1;
    for (unsigned i = 1; i < len; ++i) v = (v << 8) | uint8_t(in[o++]);
    return 0;
  }

  template <typename Impl>
  struct CxnParser {
    auto impl() const { return static_cast<const Impl *>(this); }
    auto impl() { return static_cast<Impl *>(this); }

    using State = CxnState;
    using StreamState = CxnStreamState;

  private:
    auto state_() const {
      if constexpr (requires(const Impl *impl_) { impl_->h3State(); })
	return impl()->h3State();
      else
	return m_cxnState;
    }
    void state_(State::T state) {
      if constexpr (requires(Impl *impl_) { impl_->h3State(state); })
	impl()->h3State(state);
      else
	m_cxnState = state;
    }

    bool peerControlStream_() {
      if constexpr (requires(Impl *impl_) { impl_->peerControlStream(); })
	return impl()->peerControlStream();
      else
	return true;
    }
    bool peerEncoderStream_() {
      if constexpr (requires(Impl *impl_) { impl_->peerEncoderStream(); })
	return impl()->peerEncoderStream();
      else
	return true;
    }
    bool peerDecoderStream_() {
      if constexpr (requires(Impl *impl_) { impl_->peerDecoderStream(); })
	return impl()->peerDecoderStream();
      else
	return true;
    }
    bool peerExtensionStream_(uint64_t type) {
      if constexpr (requires(Impl *impl_) { impl_->peerExtensionStream(type); })
	return impl()->peerExtensionStream(type);
      else
	return true;
    }
    void setting_(uint64_t key, uint64_t value) {
      if (key == 0x01)
	if (auto tx = qpackTx_()) {
	  if (value > uint32_t(-1) || !tx->setMaxCapacity(uint32_t(value)))
	    m_streamState = StreamState::Error;
	}
      if constexpr (requires(Impl *impl_) { impl_->setting(key, value); })
	impl()->setting(key, value);
    }
    void goaway_(uint64_t id) {
      if constexpr (requires(Impl *impl_) { impl_->goaway(id); })
	impl()->goaway(id);
    }
    QPackRxTable *qpackRx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackRx(); })
	return impl()->qpackRx();
      else
	return nullptr;
    }
    QPackTxTable *qpackTx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackTx(); })
	return impl()->qpackTx();
      else
	return nullptr;
    }

    bool applyEncoderInstruction_(const QPackDecodedInstruction &i) {
      auto rx = qpackRx_();
      if (!rx) return i.type == QPackInstruction::SetCapacity && !i.value;
      if (i.type == QPackInstruction::SetCapacity) {
	if (i.value > uint32_t(-1)) return false;
	return rx->setCapacity(uint32_t(i.value));
      }
      if (i.type == QPackInstruction::InsertWithoutNameRef)
	return rx->insert(i.header);
      if (i.type == QPackInstruction::Duplicate)
	return rx->duplicate(i.value);
      if (i.type != QPackInstruction::InsertWithNameRef) return false;
      HeaderName name;
      if (i.nameRefDynamic) {
	Header h;
	if (!rx->lookupRelative(rx->insertCount(), i.value, h)) return false;
	name = h.name;
      } else if (!QPack::staticName(i.value, name))
	return false;
      return rx->insert(Header{name, i.header.value});
    }
    bool applyDecoderInstruction_(const QPackDecodedInstruction &i) {
      auto tx = qpackTx_();
      if (!tx) return true;
      if (i.type == QPackInstruction::SectionAck)
	return tx->sectionAck(i.value);
      if (i.type == QPackInstruction::StreamCancellation)
	return tx->streamCancellation(i.value);
      if (i.type == QPackInstruction::InsertCountIncrement)
	return tx->insertCountIncrement(i.value);
      return false;
    }

    template <typename Decode, typename Apply>
    bool parseQPack_(ZuBSpan span, Decode decode, Apply apply) {
      for (unsigned i = 0; i < span.length(); ++i) m_qpackBytes.push(span[i]);
      for (;;) {
	QPackDecodedInstruction insn;
	int n = decode(ZuCSpan{
	  reinterpret_cast<const char *>(m_qpackBytes.data()),
	  m_qpackBytes.length()}, insn);
	if (n == -2) return m_qpackBytes.length() <= DefltMaxHdr;
	if (n < 0) return false;
	if (!apply(insn)) return false;
	m_qpackBytes.splice(0, n);
	if (!m_qpackBytes.length()) return true;
      }
    }

    int64_t consumeVar_(ZuBSpan span) {
      for (unsigned o = 0; o < span.length(); ++o) {
	uint8_t c = span[o];
	if (!m_varBytes) {
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  if (m_varBytes == m_varLen) return o + 1;
	  continue;
	}
	if (++m_varBytes == m_varLen) return o + 1;
      }
      return 0;
    }

    int64_t consumeFrame_(ZuBSpan span) {
      for (unsigned o = 0; o < span.length(); ++o) {
	uint8_t c = span[o];
	if (m_frameState == FrameState::Type) {
	  m_frameState = FrameState::TypeCont;
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  if (m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Length;
	  continue;
	}
	if (m_frameState == FrameState::TypeCont) {
	  if (++m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Length;
	  continue;
	}
	if (m_frameState == FrameState::Length) {
	  m_varLen = 1U << (c >> 6);
	  m_varBytes = 1;
	  m_frameLen = c & 0x3f;
	  m_frameState = FrameState::LengthCont;
	  if (m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Payload;
	  m_frameOff = 0;
	  if (!m_frameLen) return o + 1;
	  continue;
	}
	if (m_frameState == FrameState::LengthCont) {
	  m_frameLen = (m_frameLen << 8) | c;
	  if (++m_varBytes < m_varLen) continue;
	  m_frameState = FrameState::Payload;
	  m_frameOff = 0;
	  if (!m_frameLen) return o + 1;
	  continue;
	}
	if (m_frameState == FrameState::Payload) {
	  uint64_t avail = span.length() - o;
	  if (avail >= m_frameLen - m_frameOff)
	    return o + int64_t(m_frameLen - m_frameOff);
	  m_frameOff += avail;
	  return 0;
	}
      }
      return 0;
    }

    void resetFrame_() {
      m_frameState = FrameState::Type;
      m_varLen = m_varBytes = 0;
      m_frameLen = m_frameOff = 0;
    }

    bool parseType_(ZuCSpan bytes) {
      unsigned o = 0;
      uint64_t type = 0;
      if (decodeVar(bytes, o, type) < 0 || o != bytes.length())
	return false;
      m_streamType = type;
      switch (type) {
	case 0x00: // control stream
	  if (!peerControlStream_()) return false;
	  m_streamState = StreamState::Control;
	  state_(State::PeerControlOpen);
	  return true;
	case 0x02: // QPACK encoder stream
	  if (!peerEncoderStream_()) return false;
	  m_streamState = StreamState::QPackEncoder;
	  return true;
	case 0x03: // QPACK decoder stream
	  if (!peerDecoderStream_()) return false;
	  m_streamState = StreamState::QPackDecoder;
	  return true;
	default:
	  if (!peerExtensionStream_(type)) return false;
	  m_streamState = StreamState::Extension;
	  return true;
      }
    }

    bool parseSettings_(ZuCSpan payload) {
      unsigned o = 0;
      while (o < payload.length()) {
	uint64_t key = 0, value = 0;
	if (decodeVar(payload, o, key) < 0 ||
	    decodeVar(payload, o, value) < 0)
	  return false;
	for (unsigned i = 0; i < m_nSettingsKeys; ++i)
	  if (m_settingsKeys[i] == key) return false;
	if (m_nSettingsKeys < MaxSettingsKeys)
	  m_settingsKeys[m_nSettingsKeys++] = key;
	setting_(key, value);
	if (m_streamState == StreamState::Error) return false;
      }
      state_(State::PeerSettingsReceived);
      return true;
    }

    bool parseControlFrame_(ZuCSpan frameBytes) {
      unsigned o = 0;
      uint64_t type = 0, len = 0;
      if (decodeVar(frameBytes, o, type) < 0 ||
	  decodeVar(frameBytes, o, len) < 0 ||
	  frameBytes.length() != o + len)
	return false;
      ZuCSpan payload{frameBytes.data() + o, unsigned(len)};
      switch (type) {
	case 0x04: // SETTINGS
	  if (m_settings) return false;
	  m_settings = true;
	  return parseSettings_(payload);
	case 0x07: { // GOAWAY
	  unsigned p = 0;
	  uint64_t id = 0;
	  if (decodeVar(payload, p, id) < 0 || p != payload.length())
	    return false;
	  goaway_(id);
	  state_(State::Goaway);
	  return true;
	}
	case 0x0d: { // MAX_PUSH_ID
	  unsigned p = 0;
	  uint64_t id = 0;
	  return decodeVar(payload, p, id) >= 0 && p == payload.length();
	}
	case 0x00: // DATA
	case 0x01: // HEADERS
	  return false;
	default:
	  return true;
      }
    }

    int drain_(auto &rx) {
      int64_t consumed;
      do {
	consumed = rx.consume(
	  [](ZuBSpan span) -> int64_t { return span.length(); },
	  [](ZuBSpan) { });
	if (consumed < 0) return -1;
      } while (consumed);
      return 0;
    }

  public:
    template <typename Stream>
    State::T process(Stream &stream) {
      if (stream.resetReceived() || stream.stopReceived()) {
	m_streamState = StreamState::Cancelled;
	return state_();
      }

      auto &rx = stream.rxStream();
      int64_t consumed = 0;
      do {
	consumed = 0;
	switch (m_streamState) {
	  default:
	    break;
	  case StreamState::Type: {
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.CxnType">>(
	      [this](ZuBSpan span) { return this->consumeVar_(span); },
	      [this](ZuBSpan span) {
		if (!this->parseType_(ZuCSpan{
		      reinterpret_cast<const char *>(span.data()), span.length()}))
		  m_streamState = StreamState::Error;
		m_varLen = m_varBytes = 0;
	      });
	  } break;
	  case StreamState::Control: {
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen, frameOff = m_frameOff;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.CxnFrame">>(
	      [this](ZuBSpan span) { return this->consumeFrame_(span); },
	      [this](ZuBSpan span) {
		if (!this->parseControlFrame_(ZuCSpan{
		      reinterpret_cast<const char *>(span.data()), span.length()}))
		  m_streamState = StreamState::Error;
		resetFrame_();
	      });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	      m_frameOff = frameOff;
	    }
	  } break;
	  case StreamState::QPackEncoder:
	    consumed = rx.consume(
	      [](ZuBSpan span) -> int64_t { return span.length(); },
	      [this](ZuBSpan span) {
		if (!this->parseQPack_(span,
		    [](ZuCSpan bytes, QPackDecodedInstruction &i) {
		      return QPack::decodeEncoderInstructionOne(bytes, i);
		    },
		    [this](const QPackDecodedInstruction &i) {
		      return this->applyEncoderInstruction_(i);
		    }))
		  m_streamState = StreamState::Error;
	      });
	    break;
	  case StreamState::QPackDecoder:
	    consumed = rx.consume(
	      [](ZuBSpan span) -> int64_t { return span.length(); },
	      [this](ZuBSpan span) {
		if (!this->parseQPack_(span,
		    [](ZuCSpan bytes, QPackDecodedInstruction &i) {
		      return QPack::decodeDecoderInstructionOne(bytes, i);
		    },
		    [this](const QPackDecodedInstruction &i) {
		      return this->applyDecoderInstruction_(i);
		    }))
		  m_streamState = StreamState::Error;
	      });
	    break;
	  case StreamState::Extension:
	    if (drain_(rx) < 0) m_streamState = StreamState::Error;
	    break;
	}
	if (consumed < 0 || m_streamState == StreamState::Error) {
	  state_(State::Error);
	  break;
	}
      } while (consumed);

      if (stream.finReceived() && rx.empty() &&
	  m_streamState != StreamState::Error &&
	  m_streamState != StreamState::Cancelled)
	m_streamState = StreamState::Complete;
      return state_();
    }

    void reset() {
      m_streamState = StreamState::Type;
      m_streamType = -1;
      m_settings = false;
      m_nSettingsKeys = 0;
      m_qpackBytes.length(0);
      resetFrame_();
    }

    State::T h3State() const { return m_cxnState; }
    void h3State(State::T state) { m_cxnState = state; }
    bool peerControlStream() { return true; }
    bool peerEncoderStream() { return true; }
    bool peerDecoderStream() { return true; }
    bool peerExtensionStream(uint64_t) { return true; }
    void setting(uint64_t, uint64_t) { }
    void goaway(uint64_t) { }

  private:
    State::T		m_cxnState = State::Init;
    StreamState::T	m_streamState = StreamState::Type;
    uint64_t		m_streamType = -1;
    bool		m_settings = false;
    FrameState::T	m_frameState = FrameState::Type;
    unsigned		m_varLen = 0;
    unsigned		m_varBytes = 0;
    uint64_t		m_frameLen = 0;
    uint64_t		m_frameOff = 0;
    HeaderBytes		m_qpackBytes;
    static constexpr unsigned MaxSettingsKeys = 32;
    uint64_t		m_settingsKeys[MaxSettingsKeys];
    unsigned		m_nSettingsKeys = 0;
  };

  struct QPackStringRef {
    ZuCSpan	raw;
    bool	huffman = false;
  };

  template <bool Request> struct FieldState_ {
    Method::T		method = -1;
    QPackStringRef	path;
    bool		pathSeen = false;
  };
  template <> struct FieldState_<false> {
    int		status = -1;
  };

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
    using State = ParserState;

  private:
    int64_t consumePayload_(ZuBSpan span) {
      auto n = span.length();
      uint64_t remaining = m_frameLen - m_frameOff;
      if (n > remaining) n = remaining;
      if (!(m_frameOff += n)) return n;
      if (m_frameOff == m_frameLen) m_frameState = FrameState::Type;
      return n;
    }

    int64_t consumeFullPayload_(ZuBSpan span) {
      auto n = span.length();
      uint64_t remaining = m_frameLen - m_frameOff;
      if (n >= remaining) {
	m_frameOff = m_frameLen;
	m_frameState = FrameState::Type;
	return remaining;
      }
      m_frameOff += n;
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

    static int parseString_(
      ZuCSpan in, unsigned &o, unsigned bits, uint8_t huffmanMask,
      QPackStringRef &out) {
      uint64_t len = 0;
      uint8_t first = 0;
      if (decodePref_(in, o, bits, len, &first) < 0 ||
	  in.length() < o + len)
	return -1;
      out.raw = ZuCSpan{in.data() + o, unsigned(len)};
      out.huffman = first & huffmanMask;
      o += unsigned(len);
      return int(out.raw.length());
    }

    template <typename L>
    static bool withString_(QPackStringRef ref, L l) {
      if (!ref.huffman) return l(ref.raw);
      uint8_t storage[DefltMaxHdr];
      int64_t n = HPack::decode(
	ZuSpan<uint8_t>{storage, DefltMaxHdr},
	ZuBSpan{
	  reinterpret_cast<const uint8_t *>(ref.raw.data()), ref.raw.length()});
      if (n < 0) return false;
      return l(ZuCSpan{reinterpret_cast<const char *>(storage), unsigned(n)});
    }

    using FieldState = FieldState_<Request>;

    template <unsigned I>
    bool staticHeader_(FieldState &fields, bool initial) {
      using KV = QPackKV<I>;
      using Key = ZuType<0, KV>;
      using Value = QPackValue<KV>;
      if constexpr (ZuIsSame<Value, void>{})
	return qpackHeader_(fields, initial, Key{}(), "");
      else
	return qpackHeader_(fields, initial, Key{}(), Value{}());
    }

    bool qpackHeaderRef_(
      FieldState &fields, bool initial, ZuCSpan name, QPackStringRef valueRef) {
      if (!name) return false;
      if (name[0] == ':') {
	if (!initial) return false;
	if constexpr (Request) {
	  static constexpr auto matcher =
	    ZuMatcher<":method", ":path", ":scheme", ":authority">();
	  switch (matcher.match(name)) {
	    case 0:
	      return withString_(valueRef, [&fields](ZuCSpan value) {
		Method::T method = Method::lookup(value);
		if (method < 0) return false;
		fields.method = method;
		return true;
	      });
	    case 1:
	      fields.path = valueRef;
	      fields.pathSeen = true;
	      return true;
	    case 2:
	    case 3:
	      return true;
	    default:
	      return false;
	  }
	} else {
	  if (name != ":status") return false;
	  return withString_(valueRef, [this, &fields](ZuCSpan value) {
	    unsigned n = value.length();
	    int o = 0; // intentionally int
	    int c; // intentionally int
	    unsigned code = 0;
	    while (o < int(n)) {
	      if ((c = value[o]) < '0' || c > '9') return false;
	      c -= '0';
	      code = code ? (code * 10) + c : c;
	      if (ZuUnlikely(++o > 3)) return false;
	    }
	    if (ZuUnlikely(o != 3)) return false;
	    impl()->status(code);
	    fields.status = code;
	    return true;
	  });
	}
      }
      return withString_(valueRef, [this, name](ZuCSpan value) {
	uint8_t key_[256];
	if (name.length() > 256) return false;
	ZuSpan key{key_, name.length()};
	memcpy(key.data(), name.data(), name.length());
	normalize(key);
	header_(key, ZuBSpan{value});
	return true;
      });
    }

    bool qpackHeader_(
      FieldState &fields, bool initial, ZuCSpan name, ZuCSpan value) {
      return qpackHeaderRef_(fields, initial, name, { value, false });
    }

    QPackRxTable *qpackRx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackRx(); })
	return impl()->qpackRx();
      else
	return nullptr;
    }
    const Params &h3Params_() const {
      if constexpr (requires(const Impl *impl_) { impl_->h3Params(); })
	return impl()->h3Params();
      else {
	static const Params params;
	return params;
      }
    }
    uint64_t streamID_() const {
      if constexpr (requires(const Impl *impl_) { impl_->streamID(); })
	return impl()->streamID();
      else
	return 0;
    }
    QPackEncoderTx *qpackDecoderTx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackDecoderTx(); })
	return impl()->qpackDecoderTx();
      else
	return nullptr;
    }

    template <typename Key> void header_(ZuBSpan value) {
      if constexpr (ZuIsSame<Key, ZuStringT<"content-length">>{}) {
	uint64_t contentLength = ZuBox<uint64_t>{ZuCSpan{value}};
	if (contentLength > MaxBody) {
	  m_state = State::Error;
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLen = contentLength;
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

    bool parseFields_(ZuCSpan payload, bool initial) {
      auto rx = qpackRx_();
      uint64_t insertCount = rx ? rx->insertCount() : 0;
      uint64_t maxCapacity = rx ? rx->maxCapacity() : 0;
      FieldSectionPrefix prefix;
      int prefixLen = QPack::decodeFieldSectionPrefix(
	payload, prefix, insertCount, maxCapacity);
      if (prefixLen < 0 || prefix.requiredInsertCount > insertCount)
	return false;
      if (prefix.requiredInsertCount && !rx) return false;

      unsigned o = unsigned(prefixLen);
      uint64_t base = prefix.base;
      FieldState fields;
      unsigned headerBytes = 0;
      auto countHeader = [&headerBytes](ZuCSpan name, ZuCSpan value) {
	headerBytes += name.length() + value.length();
      };
      auto countHeaderRef = [&headerBytes](
	ZuCSpan name, const QPackStringRef &valueRef) {
	return withString_(valueRef, [&headerBytes, name](ZuCSpan value) {
	  headerBytes += name.length() + value.length();
	  return true;
	});
      };

      while (o < payload.length()) {
	uint8_t first = uint8_t(payload[o]);
	Header indexed;

	if (first & 0x80) {
	  uint64_t index = 0;
	  uint8_t indexFirst = 0;
	  if (decodePref_(payload, o, 6, index, &indexFirst) < 0)
	    return false;
	  if (indexFirst & 0x40) {
	    if (index >= QPackTbl::N) return false;
	    bool ok = false;
	    ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
	      [this, &fields, initial, &ok, &countHeader](auto i) {
		ok = this->template staticHeader_<i>(fields, initial);
		if (ok) {
		  using KV = QPackKV<i>;
		  using Key = ZuType<0, KV>;
		  using Value = QPackValue<KV>;
		  if constexpr (ZuIsSame<Value, void>{})
		    countHeader(Key{}(), "");
		  else
		    countHeader(Key{}(), Value{}());
		}
	      });
	    if (!ok) return false;
	  } else {
	    if (!rx || !rx->lookupRelative(base, index, indexed) ||
		!qpackHeader_(fields, initial, indexed.name, indexed.value))
	      return false;
	    countHeader(indexed.name, indexed.value);
	  }
	  continue;
	}

	if ((first & 0xf0) == 0x10) {
	  uint64_t index = 0;
	  if (decodePref_(payload, o, 4, index) < 0 ||
	      !rx || !rx->lookupPostBase(base, index, indexed) ||
	      !qpackHeader_(fields, initial, indexed.name, indexed.value))
	    return false;
	  countHeader(indexed.name, indexed.value);
	  continue;
	}

	if ((first & 0xc0) == 0x40) {
	  ZuCSpan name;
	  QPackStringRef valueRef;
	  uint64_t index = 0;
	  uint8_t nameFirst = 0;
	  if (decodePref_(payload, o, 4, index, &nameFirst) < 0)
	    return false;
	  if (nameFirst & 0x10) {
	    bool ok = false;
	    if (index >= QPackTbl::N) return false;
	    ZuSwitch::dispatch<QPackTbl::N>(unsigned(index),
	      [&name, &ok](auto i) {
		using KV = QPackKV<i>;
		name = ZuType<0, KV>{}();
		ok = true;
	      });
	    if (!ok) return false;
	  } else {
	    if (!rx || !rx->lookupRelative(base, index, indexed)) return false;
	    name = indexed.name;
	  }
	  if (parseString_(payload, o, 7, 0x80, valueRef) < 0 ||
	      !countHeaderRef(name, valueRef) ||
	      !qpackHeaderRef_(fields, initial, name, valueRef))
	    return false;
	  continue;
	}

	if ((first & 0xf0) == 0x00) {
	  QPackStringRef valueRef;
	  uint64_t index = 0;
	  if (decodePref_(payload, o, 3, index) < 0 ||
	      !rx || !rx->lookupPostBase(base, index, indexed) ||
	      parseString_(payload, o, 7, 0x80, valueRef) < 0 ||
	      !countHeaderRef(indexed.name, valueRef) ||
	      !qpackHeaderRef_(fields, initial, indexed.name, valueRef))
	    return false;
	  continue;
	}

	if ((first & 0xe0) == 0x20) {
	  QPackStringRef nameRef;
	  QPackStringRef valueRef;
	  if (parseString_(payload, o, 3, 0x08, nameRef) < 0 ||
	      parseString_(payload, o, 7, 0x80, valueRef) < 0 ||
	      !withString_(nameRef, [this, &fields, initial, valueRef](
		ZuCSpan name) {
		return qpackHeaderRef_(fields, initial, name, valueRef);
	      }) ||
	      !withString_(nameRef,
		[&countHeaderRef, valueRef](ZuCSpan name) {
		  return countHeaderRef(name, valueRef);
		}))
	    return false;
	  continue;
	}

	return false;
      }

      if (headerBytes > h3Params_().maxHeaderListSize()) return false;
      if (prefix.requiredInsertCount)
	if (auto tx = qpackDecoderTx_()) {
	  HeaderBytes ack;
	  if (QPack::encodeSectionAck(ack, streamID_()) < 0) return false;
	  tx->write(ZuCSpan{
	    reinterpret_cast<const char *>(ack.data()), ack.length()});
	}

      if (initial) {
	if constexpr (Request) {
	  if (fields.method < 0 || !fields.pathSeen) return false;
	  return withString_(fields.path, [this, &fields](ZuCSpan path) {
	    impl()->operation(fields.method, ZuBSpan{path});
	    return true;
	  });
	} else {
	  if (fields.status < 0) return false;
	}
      }
      return true;
    }

    bool bodyComplete_() const {
      return m_contentLen < 0 || m_bodyLen == uint64_t(m_contentLen);
    }

    bool processPayloadFrame_(uint64_t type, ZuCSpan payload) {
      if (type == 0x01) { // HEADERS
	if (m_state == State::Initial) {
	  if (!parseFields_(payload, true)) return false;
	  m_state = State::Body;
	  return true;
	}
	if (m_state != State::Body) return false;
	if (!bodyComplete_() || !parseFields_(payload, false)) return false;
	m_state = State::Trailers;
	return true;
      }
      if (type == 0x00) { // DATA
	return processDataPayload_(payload);
      }
      return true; // ignore unknown extension frames on request streams
    }

    void resetFrame_() {
      m_frameState = FrameState::Type;
      m_varLen = m_varBytes = 0;
      m_frameType = m_frameLen = m_frameOff = 0;
    }

    bool processDataPayload_(ZuCSpan payload) {
      if (m_state != State::Body) return false;
      if (payload.length() > MaxBody ||
	  m_bodyLen > MaxBody - payload.length())
	return false;
      m_bodyLen += payload.length();
      if (m_contentLen >= 0 && m_bodyLen > uint64_t(m_contentLen))
	return false;
      impl()->body(payload);
      return true;
    }

    template <typename Stream, typename Rx>
    static bool streamComplete_(Stream &stream, Rx &rx) {
      if constexpr (requires { stream.rxComplete(); })
	return stream.rxComplete() && rx.empty();
      else
	return stream.finReceived() && rx.empty();
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
    State::T process(Stream &stream) {
      if (stream.resetReceived() || stream.stopReceived()) {
	m_state = State::Cancelled;
	impl()->complete(m_state);
	return m_state;
      }

      auto &rx = stream.rxStream();
      int64_t consumed = 0;
      do {
	consumed = 0;
	switch (m_frameState) {
	  default:
	    m_state = State::Error;
	    break;
	  case FrameState::Type: { // parse frame type
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameType = m_frameType;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Type">>(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  uint8_t c = span[o];
		  if (m_frameState == FrameState::Type) {
		    m_frameState = FrameState::TypeCont;
		    m_varLen = 1U << (c >> 6);
		    m_varBytes = 1;
		    m_frameType = c & 0x3f;
		    if (m_varBytes < m_varLen) continue;
		    m_frameState = FrameState::Length;
		    return o + 1;
		  }
		  m_frameType = (m_frameType << 8) | c;
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Length;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameType = frameType;
	    }
	  } break;
	  case FrameState::TypeCont: { // parse frame type continuation
	    auto frameState = m_frameState;
	    unsigned varBytes = m_varBytes;
	    uint64_t frameType = m_frameType;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Type">>(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  m_frameType = (m_frameType << 8) | uint8_t(span[o]);
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Length;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varBytes = varBytes;
	      m_frameType = frameType;
	    }
	  } break;
	  case FrameState::Length: { // parse frame length
	    auto frameState = m_frameState;
	    unsigned varLen = m_varLen, varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Length">>(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  uint8_t c = span[o];
		  if (m_frameState == FrameState::Length) {
		    m_varLen = 1U << (c >> 6);
		    m_varBytes = 1;
		    m_frameLen = c & 0x3f;
		    m_frameState = FrameState::LengthCont;
		    if (m_varBytes < m_varLen) continue;
		    m_frameState = FrameState::Payload;
		    m_frameOff = 0;
		    return o + 1;
		  }
		  m_frameLen = (m_frameLen << 8) | c;
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Payload;
		  m_frameOff = 0;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varLen = varLen;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	    }
	  } break;
	  case FrameState::LengthCont: { // parse frame length continuation
	    auto frameState = m_frameState;
	    unsigned varBytes = m_varBytes;
	    uint64_t frameLen = m_frameLen;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Length">>(
	      [this](ZuBSpan span) -> int64_t {
		for (unsigned o = 0; o < span.length(); ++o) {
		  m_frameLen = (m_frameLen << 8) | uint8_t(span[o]);
		  if (++m_varBytes < m_varLen) continue;
		  m_frameState = FrameState::Payload;
		  m_frameOff = 0;
		  return o + 1;
		}
		return 0;
	      },
	      [](ZuBSpan) { });
	    if (!consumed) {
	      m_frameState = frameState;
	      m_varBytes = varBytes;
	      m_frameLen = frameLen;
	    }
	  } break;
	  case FrameState::Payload: { // parse frame payload
	    if (!m_frameLen) {
	      if (!processPayloadFrame_(m_frameType, {}))
		m_state = State::Error;
	      resetFrame_();
	      consumed = 1;
	      break;
	    }
	    if (m_frameType == 0x00) { // DATA
	      consumed = rx.consume(
		[this](ZuBSpan span) {
		  return this->consumePayload_(span);
		}, [this](ZuBSpan span) {
		  ZuCSpan payload{
		    reinterpret_cast<const char *>(span.data()), span.length()};
		  if (!this->processDataPayload_(payload))
		    m_state = State::Error;
		  if (m_frameOff == m_frameLen) resetFrame_();
		});
	      break;
	    }
	    uint64_t frameOff = m_frameOff;
	    consumed = rx.template consume<0, ZuStringT<"Zhttp.H3.Payload">>(
	      [this](ZuBSpan span) {
		return this->consumeFullPayload_(span);
	      }, [this](ZuBSpan span) {
		ZuCSpan payload{
		  reinterpret_cast<const char *>(span.data()), span.length()};
		if (!this->processPayloadFrame_(m_frameType, payload))
		  m_state = State::Error;
		resetFrame_();
	      });
	    if (!consumed) m_frameOff = frameOff;
	  } break;
	}
	if (consumed < 0) m_state = State::Error;
	if (m_state == State::Error) break;
      } while (consumed);

      if (streamComplete_(stream, rx)) {
	if (m_state == State::Initial)
	  m_state = State::Error;
	else if (!bodyComplete_())
	  m_state = State::Error;
	else if (m_state != State::Error)
	  m_state = State::Complete;
      }
      if (m_state == State::Complete ||
	  m_state == State::Cancelled ||
	  m_state == State::Error)
	impl()->complete(m_state);
      return m_state;
    }

    void reset() {
      m_state = State::Initial;
      m_contentLen = -1;
      m_bodyLen = 0;
      m_frameState = FrameState::Type;
      m_varLen = m_varBytes = 0;
      m_frameType = m_frameLen = m_frameOff = 0;
    }

    void operation(Method::T, ZuBSpan) { }
    void status(unsigned) { }
    template <typename Key> void header(ZuBSpan) { }
    template <typename Key, typename Value> void header() { }
    void contentLength(uint64_t) { }
    void body(ZuBSpan) { }
    void complete(State::T) { }

  private:
    int64_t		m_contentLen = -1;
    uint64_t		m_bodyLen = 0;
    State::T		m_state = State::Initial;
    FrameState::T	m_frameState = FrameState::Type;
    unsigned		m_varLen = 0;
    unsigned		m_varBytes = 0;
    uint64_t		m_frameLen = 0;
    uint64_t		m_frameType = 0;
    uint64_t		m_frameOff = 0;
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

  template <typename Bytes>
  inline int putString(
    Bytes &out, uint8_t prefix, unsigned bits,
    ZuCSpan s1, char sep, ZuCSpan s2) {
    if (putPref(out, prefix, bits, s1.length() + 1 + s2.length()) < 0)
      return -1;
    for (unsigned i = 0; i < s1.length(); ++i) out.push(uint8_t(s1[i]));
    out.push(uint8_t(sep));
    for (unsigned i = 0; i < s2.length(); ++i) out.push(uint8_t(s2[i]));
    return 0;
  }

  struct CountBytes {
    void push(uint8_t) { ++n; }
    uint64_t length() const { return n; }
    uint64_t	n = 0;
  };

  template <typename Stream>
  struct TxBytes {
    TxBytes(Stream &stream_) : stream(stream_) { }

    void push(uint8_t c) {
      stream << char(c);
      ++n;
    }
    uint64_t length() const { return n; }

    Stream	&stream;
    uint64_t	n = 0;
  };

  template <typename Stream>
  inline int writeFrameHeader(Stream &stream, uint64_t type, uint64_t length) {
    TxBytes out{stream};
    if (putVar(out, type) < 0 || putVar(out, length) < 0)
      return -1;
    return out.length();
  }

  template <typename Bytes>
  inline int encodeFieldPrefix(Bytes &out) {
    out.push(0); // Required Insert Count
    out.push(0); // Delta Base
    return 0;
  }

  template <typename Bytes, ZuString Key, ZuString Value>
  inline int encodeKnownField(Bytes &out) {
    if constexpr (QPackKVIndex<Key, Value>{} >= 0) {
      return putPref(out, 0xc0, 6, QPackKVIndex<Key, Value>{});
    } else if constexpr (QPackKeyIndex<Key>{} >= 0) {
      if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
      return putString(out, 0, 7, Value);
    } else {
      if (putString(out, 0x20, 3, Key) < 0) return -1;
      return putString(out, 0, 7, Value);
    }
  }

  template <typename Bytes, ZuString Key>
  inline int encodeVariableField(Bytes &out, ZuCSpan value) {
    if constexpr (QPackKeyIndex<Key>{} >= 0) {
      if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
      return putString(out, 0, 7, value);
    } else {
      if (putString(out, 0x20, 3, Key) < 0) return -1;
      return putString(out, 0, 7, value);
    }
  }

  template <typename Bytes, ZuString Key>
  inline int encodeVariableField(
    Bytes &out, ZuCSpan value1, char sep, ZuCSpan value2) {
    if constexpr (QPackKeyIndex<Key>{} >= 0) {
      if (putPref(out, 0x50, 4, QPackKeyIndex<Key>{}) < 0)
	return -1;
      return putString(out, 0, 7, value1, sep, value2);
    } else {
      if (putString(out, 0x20, 3, Key) < 0) return -1;
      return putString(out, 0, 7, value1, sep, value2);
    }
  }

  template <typename Lower>
  struct DataStream : public ZiTxLayer<DataStream<Lower>, Lower> {
    using Base = ZiTxLayer<DataStream<Lower>, Lower>;

    DataStream(Lower &lower) : Base(lower, 9, 0) { }

    void prepareBuf_(ZiIOBuf *buf) {
      uint8_t hdr[16];
      using FrameHdr = ZtArray<uint8_t,
	ZtArrayHeapID<"Zhttp.H3.FrameHdr">>;
      auto frameHdr = ZtLocalArray(FrameHdr, 16);
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
	ZuCSpan query_{ZuFwd<Query>(query)};
	stream << Method::name(method) << ' ' << ZuFwd<Path>(path);
	if (query_) stream << '?' << query_;
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

    QPackTxTable *qpackTx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackTx(); })
	return impl()->qpackTx();
      else
	return nullptr;
    }
    QPackEncoderTx *qpackEncoderTx_() {
      if constexpr (requires(Impl *impl_) { impl_->qpackEncoderTx(); })
	return impl()->qpackEncoderTx();
      else
	return nullptr;
    }
    const Params &h3Params_() const {
      if constexpr (requires(const Impl *impl_) { impl_->h3Params(); })
	return impl()->h3Params();
      else {
	static const Params params;
	return params;
      }
    }
    uint64_t streamID_() const {
      if constexpr (requires(const Impl *impl_) { impl_->streamID(); })
	return impl()->streamID();
      else
	return 0;
    }

    struct Build {
      HeaderBytes	body;
      HeaderBytes	encoder;
      QPackTxRefs	refs;
      QPackTxTable	*tx = nullptr;
      Params		params;
      uint64_t		base = 0;
      uint64_t		required = 0;
      bool		ok = true;

      static void append_(HeaderBytes &out, const HeaderBytes &in) {
	for (unsigned i = 0; i < in.length(); ++i) out.push(in[i]);
      }
      void field(ZuCSpan name, ZuCSpan value) {
	if (!ok) return;
	Header h{name, value};
	if (int i = QPack::staticIndex(name, value); i >= 0) {
	  ok = QPack::encodeFieldLine(body, h, params) >= 0;
	  return;
	}
	if (tx)
	  if (auto e = tx->find(name, value))
	    if (e->abs < base && e->abs + 1 <= tx->knownReceivedCount()) {
	      ok = QPack::encodeDynamicIndexed(body, base - e->abs - 1) >= 0;
	      if (required < e->abs + 1) required = e->abs + 1;
	      refs.push(e->abs);
	      return;
	    }
	ok = QPack::encodeFieldLine(body, h, params) >= 0;
	if (!ok || !tx || !tx->capacity() || params.neverIndex(name)) return;
	if (tx->find(name, value)) return;
	HeaderBytes insn;
	uint64_t nameIndex = 0;
	if (QPack::staticNameIndex(name, nameIndex))
	  ok = QPack::encodeInsertWithNameRef(insn, nameIndex, false, value) >= 0;
	else
	  ok = QPack::encodeInsertLiteral(insn, h) >= 0;
	if (!ok) return;
	append_(encoder, insn);
	tx->insert(h);
      }
    };

    template <typename KVs>
    bool headers_(Build &build) {
      using HeaderKeys = ZuTypeSlice<2, 0, KVs>;
      using HeaderValues = ZuTypeSlice<2, 1, KVs>;
      ZuUnroll::all<HeaderKeys>([this, &build]<typename Key>() {
	using Value = ZuType<ZuTypeIndex<Key, HeaderKeys>{}, HeaderValues>;
	if constexpr (!ZuIsSame<Value, void>{}) {
	  build.field(Key{}(), Value{}());
	} else {
	  impl()->template header<Key>([&build]<typename V>(V &&v) {
	    build.field(Key{}(), ZuCSpan{ZuFwd<V>(v)});
	  });
	}
      });
      return build.ok;
    }

    bool contentLength_(Build &build) {
      if constexpr (HasBody) {
	char buf[32];
	build.field("content-length", uintSpan_(impl()->contentLength(), buf));
	return build.ok;
      }
      return true;
    }

    static void encodeMethod_(Build &build, Method::T method) {
      build.field(":method", Method::name(method));
    }

    static void encodePath_(Build &build, ZuCSpan path, ZuCSpan query) {
      if (!query) {
	build.field(":path", path);
	return;
      }
      HeaderBytes value;
      for (unsigned i = 0; i < path.length(); ++i) value.push(path[i]);
      value.push('?');
      for (unsigned i = 0; i < query.length(); ++i) value.push(query[i]);
      build.field(":path", ZuCSpan{
	reinterpret_cast<const char *>(value.data()), value.length()});
    }

    template <typename Stream, typename Encode>
    void writeHeaders_(Stream &stream, Encode &&encode) {
      Build build;
      build.tx = qpackTx_();
      build.params = h3Params_();
      if (build.tx) {
	if (!build.tx->maxCapacity() && build.params.qpackTableCapacity())
	  build.tx->setMaxCapacity(build.params.qpackTableCapacity());
	if (!build.tx->capacity() && build.tx->maxCapacity()) {
	  build.tx->setCapacity(build.tx->maxCapacity());
	  if (!build.tx->capacitySent) {
	    HeaderBytes setCap;
	    if (QPack::encodeSetCapacity(setCap, build.tx->capacity()) >= 0)
	      Build::append_(build.encoder, setCap);
	    build.tx->capacitySent = true;
	  }
	}
	build.base = build.tx->insertCount();
      }
      if (!encode(build) || !build.ok) {
	ZiLOG(Error, "Zhttp", "failed to write H3 headers");
	return;
      }
      HeaderBytes prefix;
      FieldSectionPrefix p;
      p.requiredInsertCount = build.required;
      p.base = build.required ? build.base : 0;
      if (QPack::encodeFieldSectionPrefix(
	    prefix, p, build.params.qpackTableCapacity()) < 0) {
	ZiLOG(Error, "Zhttp", "failed to write H3 headers");
	return;
      }
      if (auto tx = qpackEncoderTx_())
	if (build.encoder.length())
	  tx->write(ZuCSpan{
	    reinterpret_cast<const char *>(build.encoder.data()),
	    build.encoder.length()});
      if (build.tx && build.refs.length())
	build.tx->trackSection(streamID_(), build.refs);
      if (writeFrameHeader(
	    stream, 0x01, prefix.length() + build.body.length()) < 0) {
	ZiLOG(Error, "Zhttp", "failed to write H3 headers");
	return;
      }
      TxBytes out{stream};
      for (unsigned i = 0; i < prefix.length(); ++i) out.push(prefix[i]);
      for (unsigned i = 0; i < build.body.length(); ++i) out.push(build.body[i]);
      stream.flush();
    }

    // request
  public:
    template <typename Stream>
    void request(Stream &stream) {
      writeHeaders_(stream, [this](Build &build) {
	impl()->operation([&build]<typename Path, typename Query>(
	    Method::T method, Path &&path, Query &&query) {
	  encodeMethod_(build, method);
	});
	build.field(":scheme", "https");
	impl()->host([&build]<typename Host>(Host &&host) {
	  build.field(":authority", ZuCSpan{ZuFwd<Host>(host)});
	});
	impl()->operation([&build]<typename Path, typename Query>(
	    Method::T, Path &&path, Query &&query) {
	  encodePath_(build, ZuCSpan{ZuFwd<Path>(path)},
	    ZuCSpan{ZuFwd<Query>(query)});
	});
	contentLength_(build);
	headers_<Headers>(build);
	return build.ok;
      });
    }

    // response
    template <typename Stream>
    void response(Stream &stream) {
      writeHeaders_(stream, [this](Build &build) {
	unsigned status = impl()->status();
	char buf[4];
	build.field(":status", statusSpan_(status, buf));
	contentLength_(build);
	headers_<Headers>(build);
	return build.ok;
      });
    }

    // body
    template <typename Stream>
    auto body(Stream &stream) { return dataStream(stream); }

    // finish
    template <typename Stream>
    void finish(Stream &stream) {
      if constexpr (Trailers::N) {
	writeHeaders_(stream, [this](Build &build) {
	  headers_<Trailers>(build);
	  return build.ok;
	});
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

} // H3

} // Zhttp

#endif /* Zhttp_HH */
