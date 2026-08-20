//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP/1 message implementation

#ifndef ZhttpH1_HH
#define ZhttpH1_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <limits.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZmScratch.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpFields.hh>

namespace Zhttp {

namespace H1 {

inline ZmRef<ZiRxQueue::Node> allocRxBuf()
{
  return new BodyRx::BufAlloc{};
}

inline ZmRef<ZiRxQueue::Node> allocWireRxBuf()
{
  return new BodyRx::WireBufAlloc{};
}

ZtEnumStruct(ZhttpAPI, ParserState, int8_t,
  Initial,	// first line - request operation or response status
  Headers,	// reading headers
  Body,		// reading body data (not chunked)
  ChunkHdr,	// chunk header (hex length + CRLF)
  Chunk,	// reading chunk data + trailing CRLF
  Trailers,	// trailers after final chunk
  Complete,	// message completely read
  Error);	// invalid message

struct ChunkHdr {
  uint64_t length = 0;

  // parse a chunk length; chunk extensions are ignored
  bool parse(ZuBSpan data) {
    unsigned n = data.length();
    if (ZuUnlikely(!n ||
	(n > 1 && data[0] == '0' && (data[1] | 0x20) == 'x')))
      return false;
    ZuCSpan s{data};
    unsigned o = Zu_nscan<ZuFmt::Hex<>>::atou(length, s.data(), n);
    if (ZuUnlikely(!o || o > 16)) return false;
    return o == n || data[o] == ';';
  }
};

// hard-coded Boyer-Moore to find CRLF within one contiguous span
ZuInline int eol(ZuBSpan data) {
  unsigned n = data.length();
  if (ZuUnlikely(n < 2)) return -1;
  n -= 2;
  for (unsigned o = 0; o <= n; ) {
    if (data[o + 1] != '\n') { ++o; continue; }
    if (data[o] == '\r') return o;
    o += 2;
  }
  return -1;
}

class LineScan {
public:
  void reset() {
    m_offset = 0;
    m_prevCR = false;
  }

  template <typename Stream>
  int64_t scan(Stream &stream, uint64_t max) {
    Zi::RxFramePos pos;
    int64_t n = stream.scan(
      [this](ZuBSpan span) -> int64_t {
	if (m_prevCR && span[0] == '\n') return 1;
	if (int o = eol(span); o >= 0) return o + 2;
	m_prevCR = span[span.length() - 1] == '\r';
	return 0;
      }, pos, m_offset);
    if (n > 0) return uint64_t(n) > max ? -2 : n;
    if (n < 0) return n;
    m_offset = pos.total;
    return m_offset > max ? -2 : 0;
  }

private:
  uint64_t	m_offset = 0;
  bool		m_prevCR = false;
};

// calls line(span)
// - span is empty for the last line before the body
template <typename Stream, typename Line>
inline int64_t parseLine(
  LineScan &scan, Stream &stream, Line &&line,
  uint64_t max = uint64_t(-1)) {
  int64_t n = scan.scan(stream, max);
  if (n <= 0) return n;
  uint64_t length = uint64_t(n) - 2;
  auto span = stream.span();
  if (span.length() >= length) {
    span.trunc(unsigned(length));
    ZuFwd<Line>(line)(span);
  } else {
    if (ZuUnlikely(length > UINT_MAX)) return -1;
    using Storage = ZtArray<
      uint8_t, ZtArrayHeapID<"Zhttp.Line.Rx">>;
    auto storage = ZmScratch(
      uint8_t, unsigned(length), typename Storage::VHeap);
    int64_t copied = stream.each(length,
      [&storage](ZuSpan<uint8_t> part) -> int64_t {
	storage << part;
	return part.length();
      });
    if (ZuUnlikely(copied < 0 || uint64_t(copied) != length)) return -1;
    ZuFwd<Line>(line)(storage.span());
  }
  scan.reset();
  stream.advance(uint64_t(n));
  return n;
}

// HTTP/1 request/response parser
template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>,
  uint64_t MaxStartLine_ = DefltMaxStartLine,
  uint64_t MaxHeaderSection_ = DefltMaxHeaderSection>
class Parser {
public:
  Parser(uint64_t bodyMax = DefltMaxBody) :
    m_bodyMax{bodyMax}, m_bodyRx{bodyMax} { }

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"transfer-encoding">, ZuTypeList<>,
    ZuStringT<"content-length">, ZuTypeList<>>;
  static constexpr uint64_t MaxStartLine = MaxStartLine_;
  static constexpr uint64_t MaxHeaderSection = MaxHeaderSection_;
  using State = ParserState;

private:
  void fail_(
    RequestErrorCode::T code = RequestErrorCode::Malformed,
    RequestErrorScope::T scope = RequestErrorScope::Connection,
    bool responsePossible = false) {
    if (!m_errorLatched) {
      m_error = {code, scope, responsePossible};
      m_errorLatched = true;
    }
    m_state = State::Error;
  }

  bool bodyInfo_(BodyType::T type, uint64_t length) {
    if (ZuLikely(impl()->bodyInfo(type, length))) return true;
    fail_(RequestErrorCode::BodyRejected,
      RequestErrorScope::Connection, Request);
    return false;
  }

  // process header with variable value
  template <typename Key> void header_(ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "transfer-encoding") {
	bool invalid = false;
	split(value, [this, &invalid](unsigned i, ZuBSpan token) {
	  if (i || m_chunked || token != "chunked") invalid = true;
	  else m_chunked = true;
	});
	if (invalid) {
	  fail_(RequestErrorCode::NotImplemented,
	    RequestErrorScope::Connection, Request);
	  ZiLOG(Error, "Zhttp", "invalid transfer-encoding");
	}
    } else if constexpr (Key{}() == "content-length") {
	uint64_t contentLength = 0;
      if (!Fields::uint64(value, contentLength) ||
	    contentLength > m_bodyMax) {
	  fail_(contentLength > m_bodyMax ?
	    RequestErrorCode::ContentTooLarge : RequestErrorCode::Malformed,
	    RequestErrorScope::Connection, Request);
	  ZiLOG(Error, "Zhttp", "invalid content-length");
	} else {
	  m_contentLength = contentLength;
	}
    } else if (deliverHeaders_())
	impl()->template header<Key>(section_(), value);
  }

  static bool headerKey_(ZuBSpan key, ZuCSpan name) {
    return key == name;
  }

  void runtimeHeader_(ZuBSpan key, ZuSpan<uint8_t> value) {
    if constexpr (Fields::HasRuntime<Impl>{}) {
      if (!deliverHeaders_()) return;
	impl()->header(section_(), key, value);
    }
  }

  // process header key/value
  void header_(ZuBSpan key, ZuSpan<uint8_t> value) {
    if (headerKey_(key, "transfer-encoding")) {
	this->template header_<ZuStringT<"transfer-encoding">>(value);
	return;
    }
    if (headerKey_(key, "content-length")) {
      this->template header_<ZuStringT<"content-length">>(value);
      return;
    }
    if (!deliverHeaders_()) return;
    Fields::dispatch<Headers>(
      key, value,
      [this](auto key, ZuSpan<uint8_t> value) {
	impl()->template header<ZuDecay<decltype(key)>>(section_(), value);
      },
      [this](auto key, auto value) {
	impl()->template header<
	  ZuDecay<decltype(key)>, ZuDecay<decltype(value)>>(section_());
      },
	[this](ZuBSpan key, ZuSpan<uint8_t> value) {
	runtimeHeader_(key, value);
      });
  }

  // parse request operation line
  void parseOperation(ZuSpan<uint8_t> line) {
    auto error = [this]() {
	fail_(RequestErrorCode::Malformed,
	  RequestErrorScope::Connection, true);
	ZiLOG(Error, "Zhttp", "invalid HTTP operation");
    };
    unsigned n = line.length();
    int o = 0; // intentionally int
    for (o = 0; o < int(n) && line[o] != ' '; )
	if (ZuUnlikely(++o > 7)) { error(); return; } // unterminated method
    if (ZuUnlikely(!o || o >= int(n))) { error(); return; } // missing method
    Method::T method = Method::lookup({&line[0], unsigned(o)});
    if (ZuUnlikely(method < 0)) {
      fail_(RequestErrorCode::NotImplemented,
	RequestErrorScope::Connection, true);
      return;
    }
    unsigned b = ++o;
    while (o < int(n) && line[o] != ' ') ++o;
    if (ZuUnlikely(b == unsigned(o) || o >= int(n))) { error(); return; }
    ZuSpan<uint8_t> target{&line[b], unsigned(o) - b};
    b = ++o;
    if (ZuUnlikely(b >= n)) { error(); return; } // missing protocol
    ZuCSpan protocol{&line[b], n - b};
    if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") {
      fail_(RequestErrorCode::VersionUnsupported,
	RequestErrorScope::Connection, true);
      return;
    }
    Target parsed;
    if (!Target::parseH1(parsed, method, target).ok()) {
      error();
      return;
    }
    m_http10 = protocol == "HTTP/1.0";
    if (ZuUnlikely(!impl()->operation(method, parsed))) {
      fail_(RequestErrorCode::OperationRejected,
	RequestErrorScope::Connection, true);
      return;
    }
    m_state = State::Headers;
  }

  // parse response status line
  void parseStatus(ZuCSpan line) {
    auto error = [this]() {
	fail_();
	ZiLOG(Error, "Zhttp", "invalid HTTP response status");
    };
    unsigned n = line.length();
    int o = 0; // intentionally int
    for (o = 0; o < int(n) && line[o] != ' '; )
	if (ZuUnlikely(++o > 8)) { error(); return; } // unterminated protocol
    if (ZuUnlikely(!o || o >= int(n))) { error(); return; } // missing protocol
    ZuCSpan protocol{&line[0], unsigned(o)};
    if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") {
      error();
      return;
    }
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
    m_http10 = protocol == "HTTP/1.0";
    m_deliverHeaders = code >= 200 || impl()->enable1xx();
    if (m_deliverHeaders) impl()->status(code);
    m_state = State::Headers;
  }

public:
  // top-level process
  template <typename Stream>
  State::T process(Stream &stream) {
    if (m_state == State::Complete || m_state == State::Error)
      return m_state;
    int64_t consumed = 0;
    m_progressed = false;
    do {
	consumed = 0;
	switch (m_state) {
	  default:
	    break;
	  case State::Initial: { // parse first line
	    consumed = parseLine(
	      m_lineScan, stream, [this](ZuSpan<uint8_t> line) {
	      if constexpr (Request)
		parseOperation(line);
	      else
		parseStatus(line);
	    }, MaxStartLine);
	    if (consumed == -2)
	      fail_(Request ? RequestErrorCode::TargetTooLong :
		RequestErrorCode::Malformed,
		RequestErrorScope::Connection, Request);
	  } break;
	  case State::Headers: { // parse headers
	    uint64_t remaining = m_headerBytes < MaxHeaderSection ?
	      MaxHeaderSection - m_headerBytes : 0;
	    consumed = parseLine(
	      m_lineScan, stream, [this](ZuSpan<uint8_t> line) {
	      if (!line) {
		if constexpr (!Request) {
		  if (interimResponse_()) {
		    m_state = State::Initial;
		    m_chunked = false;
		    m_eofBody = false;
		    m_contentLength = -1;
		    m_chunkLength = -1;
		    m_statusCode = 0;
		    m_headerBytes = 0;
		    return;
		  }
		  if (noResponseBody_()) {
		    if (!bodyInfo_(BodyType::None, 0)) return;
		    m_state = State::Complete;
		    return;
		  }
		}
		if (m_chunked) {
		  if (!bodyInfo_(BodyType::Streamed, 0)) return;
		  m_state = State::ChunkHdr;
		  m_contentLength = 0;
		} else if (m_contentLength != uint64_t(-1)) {
		  if (!bodyInfo_(BodyType::Fixed, m_contentLength)) return;
		  m_state = m_contentLength ? State::Body : State::Complete;
		} else if constexpr (Request) {
		  if (!bodyInfo_(BodyType::None, 0)) return;
		  m_state = State::Complete;
		} else {
		  if (!bodyInfo_(BodyType::Streamed, 0)) return;
		  m_state = State::Body;
		  m_eofBody = true;
		  m_contentLength = 0;
		}
	      } else
		if (!parseKV(line, [this](ZuBSpan key, ZuSpan<uint8_t> value) {
		  this->header_(key, value);
		}))
		  fail_(RequestErrorCode::Malformed,
		    RequestErrorScope::Connection, Request);
	    }, remaining);
	    if (consumed > 0) m_headerBytes += uint64_t(consumed);
	    else if (consumed == -2)
	      fail_(RequestErrorCode::HeadersTooLarge,
		RequestErrorScope::Connection, Request);
	  } break;
	  case State::Body: { // parse body
	    if (m_eofBody) {
	      m_eofStream = &stream;
	      m_eofFn = [](Parser *parser, void *stream_) {
		return parser->eof_(*static_cast<Stream *>(stream_));
	      };
	      if (ZuUnlikely(stream.length() > m_bodyMax)) {
		fail_(RequestErrorCode::ContentTooLarge,
		  RequestErrorScope::Connection, Request);
		ZiLOG(Error, "Zhttp", "oversized body");
	      }
	      break;
	    }
	    uint64_t length = uint64_t(m_contentLength);
	    if (stream.length() < length) break;
	    uint64_t remaining = length;
	    bool accepted = true;
	    consumed = m_bodyRx.splice(
	      stream, length,
	      [&remaining](ZuBSpan span) -> int64_t {
		if (remaining > span.length()) {
		  remaining -= span.length();
		  return 0;
		}
		return remaining;
	      }, allocWireRxBuf, allocRxBuf, 0, 0,
	      [this, &accepted](auto &rx) { accepted = impl()->body(rx); });
	    if (ZuUnlikely(!accepted))
	      fail_(RequestErrorCode::BodyRejected,
		RequestErrorScope::Connection, Request);
	    else if (consumed > 0) {
	      m_contentLength = 0;
	      m_state = State::Complete;
	    } else if (consumed < 0)
	      fail_();
	    } break;
	  case State::ChunkHdr: { // parse chunk header
	    consumed = parseLine(
	      m_lineScan, stream, [this](ZuSpan<uint8_t> span) {
		if (!span) { m_state = State::Complete; return; }
		auto error = [this](RequestErrorCode::T code) {
		  fail_(code, RequestErrorScope::Connection, Request);
		  ZiLOG(Error, "Zhttp", "invalid chunk-length");
		};
		ChunkHdr hdr;
		if (!hdr.parse(span)) {
		  error(RequestErrorCode::Malformed);
		  return;
		}
		m_chunkLength = hdr.length;
		if (!m_chunkLength) {
		  m_state = State::Trailers;
		  return;
		}
		if (m_chunkLength > m_bodyMax) {
		  error(RequestErrorCode::ContentTooLarge);
		  return;
		}
		if ((m_contentLength += m_chunkLength) > m_bodyMax) {
		  error(RequestErrorCode::ContentTooLarge);
		  return;
		}
		m_state = State::Chunk;
	      }, MaxHeaderSection);
	    if (consumed == -2)
	      fail_(RequestErrorCode::Malformed,
		RequestErrorScope::Connection, Request);
	  } break;
	  case State::Chunk: { // parse chunk data + trailing CRLF
	    uint64_t length = uint64_t(m_chunkLength);
	    uint64_t frameLen = length + 2;
	    if (stream.length() < frameLen) break;
	    bool accepted = true;
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
	      }, allocWireRxBuf, allocRxBuf, 0, 2,
	      [this, &accepted](auto &rx) { accepted = impl()->body(rx); });
	    if (ZuUnlikely(!accepted))
	      fail_(RequestErrorCode::BodyRejected,
		RequestErrorScope::Connection, Request);
	    else if (consumed > 0) {
	      m_chunkLength = 0;
	      m_state = State::ChunkHdr;
	    } else if (consumed < 0) {
	      fail_(RequestErrorCode::Malformed,
		RequestErrorScope::Connection, Request);
	      ZiLOG(Error, "Zhttp", "invalid chunk trailer");
	    }
	  } break;
	  case State::Trailers: { // parse trailers
	    uint64_t remaining = m_headerBytes < MaxHeaderSection ?
	      MaxHeaderSection - m_headerBytes : 0;
	    consumed = parseLine(
	      m_lineScan, stream, [this](ZuSpan<uint8_t> line) {
	      if (!line)
		m_state = State::Complete;
	      else
		if (!parseKV(line, [this](ZuBSpan key, ZuSpan<uint8_t> value) {
		  this->header_(key, value);
		}))
		  fail_(RequestErrorCode::Malformed,
		    RequestErrorScope::Connection, Request);
	    }, remaining);
	    if (consumed > 0) m_headerBytes += uint64_t(consumed);
	    else if (consumed == -2)
	      fail_(RequestErrorCode::HeadersTooLarge,
		RequestErrorScope::Connection, Request);
	  } break;
	}
	if (consumed < 0 && m_state != State::Error) fail_();
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
  const RequestError &error() const { return m_error; }
  bool http10() const { return m_http10; }
  bool bodyFramed() const {
    return m_chunked || m_contentLength != uint64_t(-1);
  }
  uint64_t bodyMax() const { return m_bodyMax; }
  void bodyMax(uint64_t value) {
    m_bodyMax = value;
    m_bodyRx.reset(value);
  }

  // complete an EOF-framed response body when the connection closes
  State::T eof() {
    if (m_state == State::Complete || m_state == State::Error)
	return m_state;
    if (m_state == State::Body && m_eofBody && m_eofFn)
      return m_eofFn(this, m_eofStream);
    fail_();
    m_eofStream = nullptr;
    m_eofFn = nullptr;
    m_bodyRx.discard();
    impl()->complete(m_state);
    return m_state;
  }

  // reset for next message
  void reset() {
    m_bodyRx.reset(m_bodyMax);
    m_state = State::Initial;
    m_chunked = false;
    m_eofBody = false;
    m_contentLength = -1;
    m_chunkLength = -1;
    m_statusCode = 0;
    m_headerBytes = 0;
    m_lineScan.reset();
    m_progressed = false;
    m_error = {};
    m_errorLatched = false;
    m_http10 = false;
    m_deliverHeaders = true;
    m_eofStream = nullptr;
    m_eofFn = nullptr;
  }
  void reset(uint64_t bodyMax_) {
    m_bodyMax = bodyMax_;
    reset();
  }

private:
  template <typename Stream>
  State::T eof_(Stream &stream) {
    uint64_t length = stream.length();
    if (ZuUnlikely(length > m_bodyMax)) {
	  fail_(RequestErrorCode::ContentTooLarge);
    } else if (length) {
      uint64_t remaining = length;
      bool accepted = true;
      int64_t n = m_bodyRx.splice(
	stream, length,
	[&remaining](ZuBSpan span) -> int64_t {
	  if (remaining > span.length()) {
	    remaining -= span.length();
	    return 0;
	  }
	  return remaining;
	}, allocWireRxBuf, allocRxBuf, 0, 0,
	[this, &accepted](auto &rx) { accepted = impl()->body(rx); });
      if (ZuUnlikely(!accepted))
	fail_(RequestErrorCode::BodyRejected,
	  RequestErrorScope::Connection, Request);
      else if (n > 0)
	m_state = State::Complete;
      else
	fail_();
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

  Zhttp::FieldSection::T section_() const {
    if (m_state == State::Trailers) return Zhttp::FieldSection::Trailers;
    if constexpr (Request) return Zhttp::FieldSection::Final;
    return interimResponse_() ?
      Zhttp::FieldSection::Informational : Zhttp::FieldSection::Final;
  }

  bool deliverHeaders_() const {
    if constexpr (Request) return true;
    return m_deliverHeaders;
  }

  // Rx thread exclusive
  void		*m_eofStream = nullptr;
  State::T	(*m_eofFn)(Parser *, void *) = nullptr;
  uint64_t	m_bodyMax;
  uint64_t	m_contentLength = uint64_t(-1);
  uint64_t	m_chunkLength = uint64_t(-1);
  uint64_t	m_headerBytes = 0;
  BodyRx	m_bodyRx;
  LineScan	m_lineScan;
  unsigned	m_statusCode = 0;
  State::T	m_state = State::Initial;
  RequestError	m_error;
  bool		m_chunked = false;
  bool		m_eofBody = false;
  bool		m_http10 = false;
  bool		m_progressed = false;
  bool		m_errorLatched = false;
  bool		m_deliverHeaders = true;
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
  bool valid() const { return m_valid && !Base::operator !(); }
  bool complete() const { return valid() && !m_contentLength; }

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
  bool valid() const { return !Base::operator !(); }

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
  bool HasBody_ = false,		// has a body
  bool Chunked_ = false>		// body is chunked
class Builder_ {
public:
  enum { HeaderScratchBuiltin = 1024 };

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  enum { HasBody = HasBody_ };
  enum { Chunked = Chunked_ };

private:
  template <typename Key, typename I = Impl>
  auto headerOffset_(uint64_t offset, unsigned length, int) -> decltype(
      ZuDeclVal<I &>().template headerOffset<Key>(offset, length), void()) {
    impl()->template headerOffset<Key>(offset, length);
  }
  template <typename Key>
  void headerOffset_(uint64_t, unsigned, ...) { }

  template <typename I = Impl>
  auto headerBase_(uint8_t *base, int) -> decltype(
      ZuDeclVal<I &>().headerBase(base), void()) {
    impl()->headerBase(base);
  }
  void headerBase_(uint8_t *, ...) { }

  template <typename I = Impl>
  auto patch_(int) -> decltype(ZuDeclVal<I &>().patch(), void()) {
    impl()->patch();
  }
  void patch_(...) { }

  template <typename KVs = Headers, typename Stream>
  void headers_(Stream &stream) {
    // header key/values
    {
	using List = HeaderList<KVs>;
	ZuUnroll::all<List::N>([this, &stream](auto I) {
	  using Key = typename List::template Key<I>;
	  using Value = typename List::template Value<I>;
	  if constexpr (Value::N) {
	    using Fixed = HeaderValue<Value>;
	    builderFixedHeader<Key, Fixed>(impl(), [&stream]() {
	      stream << Key{}() << ": " << Fixed{}() << "\r\n";
	    }, 0);
	    impl()->template header<Key>([&stream]<typename V>(V &&value) {
	      stream << Key{}() << ": " << ZuFwd<V>(value) << "\r\n";
	    });
	  } else {
	    if constexpr (HasBody && !Chunked)
	      impl()->template header<Key>([this, &stream]<typename V>(V &&value) {
		stream << Key{}() << ": ";
		auto offset = stream.length();
		stream << ZuFwd<V>(value);
		this->template headerOffset_<Key>(
		  offset, unsigned(stream.length() - offset), 0);
		stream << "\r\n";
	      });
	    else
	      impl()->template header<Key>([&stream]<typename V>(V &&value) {
		stream << Key{}() << ": " << ZuFwd<V>(value) << "\r\n";
	      });
	  }
	});
    }
    if constexpr (ZuIsSame<KVs, Headers>{})
      impl()->header([&stream]<typename Key, typename Value>(
	    Key &&key, Value &&value) {
	  stream << ZuFwd<Key>(key) << ": " << ZuFwd<Value>(value) << "\r\n";
      });
  }
  template <typename Stream>
  void headers(Stream &stream) {
    using HeaderBytes = ZtBArray<ZtArrayHeapID<"Zhttp.H1.Headers">>;
    auto block = ZtScratch(HeaderBytes, HeaderScratchBuiltin);
    if constexpr (HasBody && Chunked)
      block << "transfer-encoding: chunked\r\n";
    headers_<Headers>(block);
    if constexpr (HasBody && !Chunked) {
      headerBase_(block.data(), 0);
      patch_(0);
    }
    block << "\r\n";
    stream << block;
  }
protected:
  // request
  template <typename Stream>
  bool beginRequest_(Stream &stream) {
    impl()->operation([&stream](
	  Method::T method, auto &&emit) {
	stream << Method::name(method) << ' ';
	emit([&stream](auto &&write) { write(stream); });
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
  void beginResponse_(Stream &stream) {
    // status
    stream << "HTTP/1.1 " <<
	ZuBox<unsigned>{impl()->status()}.fmt<ZuFmt::Right<3>>() << ' ';
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
	stream << "0\r\n\r\n";
    }
    stream.flush();
  }

  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  // H3::QPackTxTable *qpackTx() { return nullptr; }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  bool HasBody = false,
  bool Chunked = false>
class Request : public Builder_<Impl, Headers, HasBody, Chunked> {
  using Base = Builder_<Impl, Headers, HasBody, Chunked>;

public:
  template <typename Stream>
  bool begin(Stream &stream) { return Base::beginRequest_(stream); }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  bool HasBody = false,
  bool Chunked = false>
class Response : public Builder_<Impl, Headers, HasBody, Chunked> {
  using Base = Builder_<Impl, Headers, HasBody, Chunked>;

public:
  template <typename Stream>
  void begin(Stream &stream) { Base::beginResponse_(stream); }
};

} // namespace H1

} // namespace Zhttp

#endif /* ZhttpH1_HH */
