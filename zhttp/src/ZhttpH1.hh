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

  struct ParserState {
    ZtEnum(ParserState, int8_t,
      Initial,		// first line - request operation or response status
      Headers,		// reading headers
      Body,		// reading body data (not chunked)
      ChunkHdr,		// chunk header (hex length + CRLF)
      Chunk,		// reading chunk data
      ChunkTrlr,	// chunk trailer (CRLF)
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
	      }, [this](ZuBSpan span) { impl()->body(span); });
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
    void version(ZuBSpan) { }
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
	    impl()->template header<Key>([&stream]<typename Value>(Value &&value) {
	      ZuCSpan v{ZuFwd<Value>(value)};
	      if (v) stream << Key{}() << ": " << v << "\r\n";
	    });
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
    // H3::QPackTxTable *qpackTx() { return nullptr; }
  };

} // H1

} // Zhttp

#endif /* ZhttpH1_HH */
