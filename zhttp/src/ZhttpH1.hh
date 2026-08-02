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

inline ZmRef<ZiRxQueue::Node> allocRxBuf()
{
  return new BodyRx::BufAlloc{};
}

ZtEnumStruct(ZhttpAPI, ParserState, int8_t,
  Initial,		// first line - request operation or response status
  Headers,		// reading headers
  Body,		// reading body data (not chunked)
  ChunkHdr,		// chunk header (hex length + CRLF)
  Chunk,		// reading chunk data + trailing CRLF
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
  Parser() : m_bodyRx{MaxBody} { }

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
  void parseOperation(ZuSpan<uint8_t> line) {
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
    if (ZuUnlikely(b == unsigned(o) || o >= int(n))) { error(); return; }
    ZuSpan<uint8_t> target{&line[b], unsigned(o) - b};
    b = ++o;
    if (ZuUnlikely(b >= n)) { error(); return; } // missing protocol
    ZuCSpan protocol{&line[b], n - b};
    RequestTarget parsed;
    if (!RequestTarget::parseH1(parsed, method, target).ok()) {
      error();
      return;
    }
    impl()->operation(method, parsed);
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
	    if (m_eofBody) {
	      m_eofStream = &stream;
	      m_eofFn = [](Parser *parser, void *stream_) {
		return parser->eof_(*static_cast<Stream *>(stream_));
	      };
	      if (ZuUnlikely(stream.length() > MaxBody)) {
		m_state = State::Error;
		ZiLOG(Error, "Zhttp", "oversized body");
	      }
	      break;
	    }
	    uint64_t length = uint64_t(m_contentLength);
	    if (stream.length() < length) break;
	    uint64_t remaining = length;
	    consumed = m_bodyRx.splice(
	      stream, length,
	      [&remaining](ZuBSpan span) -> int64_t {
		if (remaining > span.length()) {
		  remaining -= span.length();
		  return 0;
		}
		return remaining;
	      }, allocRxBuf, allocRxBuf, 0, 0,
	      [this](auto &rx) { impl()->body(rx); });
	    if (consumed > 0) {
	      m_contentLength = 0;
	      m_state = State::Complete;
	    } else if (consumed < 0)
	      m_state = State::Error;
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
	  case State::Chunk: { // parse chunk data + trailing CRLF
	    uint64_t length = uint64_t(m_chunkLength);
	    uint64_t frameLen = length + 2;
	    if (stream.length() < frameLen) break;
	    consumed = m_bodyRx.splice(
	      stream, length,
	      [remaining = frameLen, prev = uint8_t{0}](
		  ZuBSpan span) mutable -> int64_t {
		unsigned n = remaining < span.length() ?
		  unsigned(remaining) : span.length();
		uint8_t last = n > 1 ? span[n - 2] : prev;
		prev = span[n - 1];
		remaining -= n;
		if (remaining) return 0;
		if (last != '\r' || prev != '\n') return -1;
		return n;
	      }, allocRxBuf, allocRxBuf, 0, 2,
	      [this](auto &rx) { impl()->body(rx); });
	    if (consumed > 0) {
	      m_chunkLength = 0;
	      m_state = State::ChunkHdr;
	    } else if (consumed < 0) {
	      m_state = State::Error;
	      ZiLOG(Error, "Zhttp", "invalid chunk trailer");
	    }
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
	  m_eofStream = nullptr;
	  m_eofFn = nullptr;
	  m_bodyRx.discard();
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
    if (m_state == State::Body && m_eofBody && m_eofFn)
      return m_eofFn(this, m_eofStream);
    m_state = State::Error;
    m_eofStream = nullptr;
    m_eofFn = nullptr;
    m_bodyRx.discard();
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
    m_eofStream = nullptr;
    m_eofFn = nullptr;
  }

  // CRTP defaults
  void operation(Method::T, const RequestTarget &) { }
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
  template <typename Stream>
  State::T eof_(Stream &stream) {
    uint64_t length = stream.length();
    if (ZuUnlikely(length > MaxBody)) {
      m_state = State::Error;
    } else if (length) {
      uint64_t remaining = length;
      int64_t n = m_bodyRx.splice(
	stream, length,
	[&remaining](ZuBSpan span) -> int64_t {
	  if (remaining > span.length()) {
	    remaining -= span.length();
	    return 0;
	  }
	  return remaining;
	}, allocRxBuf, allocRxBuf, 0, 0,
	[this](auto &rx) { impl()->body(rx); });
      m_state = n > 0 ? State::Complete : State::Error;
    } else
      m_state = State::Complete;
    m_eofStream = nullptr;
    m_eofFn = nullptr;
    m_bodyRx.discard();
    impl()->complete(m_state);
    return m_state;
  }

  bool interimResponse_() const {
    return m_statusCode >= 100 && m_statusCode < 200 &&
      m_statusCode != 101;
  }
  bool noResponseBody_() const {
    return (m_statusCode >= 100 && m_statusCode < 200) ||
	m_statusCode == 204 || m_statusCode == 304;
  }

  // Rx thread exclusive
  void		*m_eofStream = nullptr;
  State::T	(*m_eofFn)(Parser *, void *) = nullptr;
  uint64_t	m_contentLength = uint64_t(-1);
  uint64_t	m_chunkLength = uint64_t(-1);
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
class Builder_ {
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
	      stream << Key{}() << ": " << ZuFwd<Value>(value) << "\r\n";
	    });
	  }
	});
    }
    if constexpr (ZuIsSame<KVs, Headers>{})
      runtimeHeaders_([&stream]<typename Key, typename Value>(
	    Key &&key, Value &&value) {
	  stream << ZuFwd<Key>(key) << ": " << ZuFwd<Value>(value) << "\r\n";
      });
    // end of headers
    stream << "\r\n";
  }
  template <typename Stream>
  void headers(Stream &stream) {
    if constexpr (HasBody && Chunked)
      stream << "transfer-encoding: chunked\r\n";
    headers_<Headers>(stream);
  }
  // chunked trailers
  template <typename Stream>
  void trailers(Stream &stream) {
    headers_<Trailers>(stream);
  }

protected:
  // request
  template <typename Stream>
  bool request_(Stream &stream) {
    impl()->operation([&stream]<typename Target>(
	  Method::T method, Target &&target) {
	stream << Method::name(method) << ' ' << ZuFwd<Target>(target);
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
  void response_(Stream &stream) {
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

public:
  // body
  template <typename Stream, bool _ = HasBody && !Chunked>
  ZuIfT<_, BodyStream<Stream>>
  body(Stream &stream) { return bodyStream(stream, uint64_t(uint32_t(-1))); }
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
  template <typename L> void operation(L &&l) { l(Method::GET, "/"); }
  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  unsigned status() { return 200; }
  template <typename L> void reason(L &&l) { l(""); }
  template <typename Key, typename L>
  void header(L &&) { }
  template <typename L> void header(L &&) { }
  // H3::QPackTxTable *qpackTx() { return nullptr; }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Chunked = false>
class RequestBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Chunked> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Chunked>;

public:
  template <typename Stream>
  bool request(Stream &stream) { return Base::request_(stream); }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>,
  bool HasBody = false,
  bool Chunked = false>
class ResponseBuilder :
  public Builder_<Impl, Headers, Trailers, HasBody, Chunked> {
  using Base = Builder_<Impl, Headers, Trailers, HasBody, Chunked>;

public:
  template <typename Stream>
  void response(Stream &stream) { Base::response_(stream); }
};

} // namespace H1

} // namespace Zhttp

#endif /* ZhttpH1_HH */
