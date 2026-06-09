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

namespace Zhttp {

constexpr unsigned DefltMaxHdr = (64<<10);	// 64K default
constexpr unsigned DefltMaxBody = (1<<20);	// 1M default

using HdrData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.HdrData">>;
using BodyData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.BodyData">>;
using TrailerData = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.TrailerData">>;

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
  bool			m_chunked = false;
};

// HTTP message builder

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

} // Zhttp

#endif /* Zhttp_HH */
