//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/1 message implementation

#ifndef ZhttpH1_HH
#define ZhttpH1_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

namespace Zhttp {

namespace H1 {

ZtEnumStruct(ParserState, int8_t,
  Initial,		// first line - request operation or response status
  Headers,		// reading headers
  Body,		// reading body data (not chunked)
  ChunkHdr,		// chunk header (hex length + CRLF)
  Chunk,		// reading chunk data
  ChunkTrlr,		// chunk trailer (CRLF)
  Trailers,		// trailers after final chunk
  Complete,		// message completely read
  Error);		// invalid message

// HTTP/1 request/response parser
template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxBody_ = DefltMaxBody>
class Parser {
public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"transfer-encoding">, void,
    ZuStringT<"content-length">, void>;
  static constexpr uint64_t MaxBody = MaxBody_;
  using State = ParserState;

private:
  // process header with variable value
  template <typename Key> void header_(ZuBSpan value) {
    if constexpr (Key{}() == "transfer-encoding") {
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
    } else if constexpr (Key{}() == "content-length") {
	uint64_t contentLength;
	if (!parseUInt64Full_(value, contentLength) ||
	    contentLength > MaxBody) {
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
    return key == name;
  }

  void runtimeHeader_(ZuBSpan key, ZuBSpan value) {
    if constexpr (Fields::HasRuntime<Impl>{})
	impl()->header(key, value);
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
    Fields::dispatch<Headers>(
      key, value,
      [this](auto key, ZuBSpan value) {
	this->template header_<ZuDecay<decltype(key)>>(value);
      },
      [this](auto key, auto value) {
	impl()->template header<
	  ZuDecay<decltype(key)>, ZuDecay<decltype(value)>>();
      },
      [this](ZuBSpan key, ZuBSpan value) {
	runtimeHeader_(key, value);
      });
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
    if (ZuUnlikely(method < 0)) { error(); return; }
    unsigned b = ++o;
    while (o < int(n) && line[o] != ' ') ++o;
    if (ZuUnlikely(b == unsigned(o) || o >= int(n))) error();
    ZuCSpan path{&line[b], unsigned(o) - b};
    b = ++o;
    if (ZuUnlikely(b >= n)) { error(); return; } // missing protocol
    ZuCSpan protocol{&line[b], n - b};
    impl()->operation(method, path);
    impl()->version(protocol);
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
    ZuCSpan protocol{&line[0], unsigned(o)};
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
    m_statusCode = code;
    impl()->version(protocol);
    impl()->status(code);
    m_state = State::Headers;
  }

public:
  // top-level process
  template <typename Stream>
  State::T process(Stream &stream) {
    int64_t consumed = 0;
    m_progressed = false;
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
		else if constexpr (Request) {
		  m_state = State::Complete;
		} else {
		  if (interimResponse_()) {
		    m_state = State::Initial;
		    m_chunked = false;
		    m_eofBody = false;
		    m_contentLength = -1;
		    m_chunkLength = -1;
		    m_statusCode = 0;
		  } else if (m_contentLength == 0 || noResponseBody_())
		    m_state = State::Complete;
		  else {
		    m_state = State::Body;
		    m_eofBody = true;
		    m_contentLength = 0;
		  }
		}
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
		if (m_eofBody) {
		  if (ZuUnlikely(m_contentLength + n > MaxBody)) {
		    m_state = State::Error;
		    ZiLOG(Error, "Zhttp", "oversized body");
		    return -1;
		  }
		  m_contentLength += n;
		} else {
		  if (n > m_contentLength) n = m_contentLength;
		  if (!(m_contentLength -= n)) m_state = State::Complete;
		}
		return n;
	      }, [this](ZuBSpan span) {
		if (!m_bodyRx.offer(span, m_state == State::Complete,
		    [this](auto &rx) { impl()->body(rx); }))
		  m_state = State::Error;
	      });
	    } break;
	  case State::ChunkHdr: { // parse chunk header
	    consumed = stream.template consume<2, "Zhttp.ChunkHdr">(
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
	      }, [this](ZuBSpan span) {
		if (!m_bodyRx.offer(span, false,
		    [this](auto &rx) { impl()->body(rx); }))
		  m_state = State::Error;
	      });
	  } break;
	  case State::ChunkTrlr: { // parse trailing "\r\n"
	    consumed = stream.template consume<2, "Zhttp.ChunkTrlr">(
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
	if (consumed > 0) m_progressed = true;
	if (m_state == State::Complete ||
	    m_state == State::Error) {
	  if (m_state == State::Complete && !m_bodyRx.finish())
	    m_state = State::Error;
	  State::T state = m_state;
	  impl()->complete(state);
	  return state;
	}
    } while (consumed);
    return m_state;
  }

  bool progressed() const { return m_progressed; }

  // complete an EOF-framed response body when the connection closes
  State::T eof() {
    if (m_state == State::Complete || m_state == State::Error)
	return m_state;
    if (m_state == State::Body && m_eofBody)
	m_state = State::Complete;
    else
	m_state = State::Error;
    if (m_state == State::Complete && !m_bodyRx.finish())
      m_state = State::Error;
    impl()->complete(m_state);
    return m_state;
  }

  // reset for next message
  void reset() {
    m_bodyRx.reset();
    m_state = State::Initial;
    m_chunked = false;
    m_eofBody = false;
    m_contentLength = -1;
    m_chunkLength = -1;
    m_statusCode = 0;
    m_progressed = false;
  }

  // CRTP defaults
  void operation(Method::T, ZuBSpan) { }
  void version(ZuBSpan) { }
  void status(unsigned) { }
  template <typename Key> void header(ZuBSpan) { }
  template <typename Key, typename Value> void header() { }
  void header(ZuBSpan, ZuBSpan) { }
  void contentLength(uint64_t) { }
  void xferCompression(XferCompression::T) { }
  void chunked() { }
  template <typename Rx>
  void body(Rx &rx) { bodyDrain(rx); }
  void complete(State::T) { }

private:
  bool interimResponse_() const {
    return m_statusCode >= 100 && m_statusCode < 200 &&
      m_statusCode != 101;
  }
  bool noResponseBody_() const {
    return (m_statusCode >= 100 && m_statusCode < 200) ||
	m_statusCode == 204 || m_statusCode == 304;
  }

  // Rx thread exclusive
  int64_t	m_contentLength = -1;
  int64_t	m_chunkLength = -1;
  BodyRx	m_bodyRx;
  unsigned	m_statusCode = 0;
  State::T	m_state = State::Initial;
  bool	m_chunked = false;
  bool	m_eofBody = false;
  bool	m_progressed = false;
};

// HTTP 1.1 non-chunked body Tx streaming
template <typename Lower>
class BodyStream : public ZiTxLayer<BodyStream<Lower>, Lower> {
public:
  using Base = ZiTxLayer<BodyStream<Lower>, Lower>;

  BodyStream(Lower &lower, uint64_t contentLength_) :
    Base(lower, 0, 0), m_contentLength(contentLength_) { }

  void prepareBuf_(ZiIOBuf *buf, bool) {
    if (ZuUnlikely(m_contentLength < buf->length)) {
      m_valid = false;
      buf->length = m_contentLength;
    }
    m_contentLength -= buf->length;
    m_produced += buf->length;
  }
  uint64_t produced() const { return m_produced; }
  bool valid() const { return m_valid; }
  bool complete() const { return m_valid && !m_contentLength; }

private:
  // Tx thread exclusive
  uint64_t		m_contentLength;
  uint64_t		m_produced = 0;
  bool			m_valid = true;
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

  void prepareBuf_(ZiIOBuf *buf, bool) {
    m_produced += buf->length;
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

  uint64_t produced() const { return m_produced; }
  bool valid() const { return true; }

private:
  uint64_t	m_produced = 0;
};
template <typename Lower>
auto chunkedStream(Lower &lower) {
  return ChunkedStream<Lower>(lower);
}

// HTTP/1 message builder
template <
  typename Impl,
  typename Headers_ = ZuTypeList<>,
  typename Trailers_ = ZuTypeList<>,	// ignored if not chunked
  bool HasBody_ = false,		// has a body
  bool Chunked_ = false>		// body is chunked
class Builder {
public:
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  using Trailers = Trailers_;
  enum { HasBody = HasBody_ };
  enum { Chunked = Chunked_ };

private:
  template <typename L>
  void runtimeHeaders_(L &&l) {
    if constexpr (Fields::HasRuntimeBuilder<Impl, L>{})
	impl()->header(ZuFwd<L>(l));
  }

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
	    impl()->template header<Key>([&stream]<typename Value>(Value &&value) {
	      ZuCSpan v{ZuFwd<Value>(value)};
	      if (v) stream << Key{}() << ": " << v << "\r\n";
	    });
	  }
	});
    }
    runtimeHeaders_([&stream](ZuCSpan key, ZuCSpan value) {
	if (key) stream << key << ": " << value << "\r\n";
    });
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
  bool request(Stream &stream) {
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
    return true;
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
  template <typename Stream, bool _ = HasBody && !Chunked>
  ZuIfT<_, BodyStream<Stream>>
  body(Stream &stream, uint64_t remaining) {
    return bodyStream(stream, remaining);
  }

  template <typename Stream, bool _ = HasBody && Chunked>
  ZuIfT<_, ChunkedStream<Stream>>
  body(Stream &stream) { return chunkedStream(stream); }
  template <typename Stream, bool _ = HasBody && Chunked>
  ZuIfT<_, ChunkedStream<Stream>>
  body(Stream &stream, uint64_t) { return chunkedStream(stream); }

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
  void header(L &&) { }
  template <typename L> void header(L &&) { }
  uint64_t contentLength() { return 0; }
  // H3::QPackTxTable *qpackTx() { return nullptr; }
};

} // namespace H1

} // namespace Zhttp

#endif /* ZhttpH1_HH */
