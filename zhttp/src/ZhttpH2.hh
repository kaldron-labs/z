//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 framing and connection control

#ifndef ZhttpH2_HH
#define ZhttpH2_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuArray.hh>
#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtScratch.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpFields.hh>

namespace Zhttp {

namespace H2 {

constexpr uint32_t DefltFrameSize = 1U<<14;
constexpr uint32_t MaxFrameSize = (1U<<24) - 1;
constexpr uint32_t DefltWindow = (1U<<16) - 1;
constexpr uint32_t MaxWindow = (1U<<31) - 1;
enum {
  FrameHeaderSize = 9,		// RFC 9113 section 4.1
  FixedPayloadSize = 8		// largest incrementally parsed fixed payload
};

ZtEnumNS(ZhttpAPI, FrameType, uint8_t,
  Data, Headers, Priority, RSTStream, Settings, PushPromise, Ping, Goaway,
  WindowUpdate, Continuation);

namespace Flag {
  enum : uint8_t {
    ACK = 0x01,
    EndStream = 0x01,
    EndHeaders = 0x04,
    Padded = 0x08,
    Priority = 0x20
  };
}

ZtEnumNS(ZhttpAPI, Error, uint32_t,
  NoError, ProtocolError, InternalError, FlowControlError, SettingsTimeout,
  StreamClosed, FrameSizeError, RefusedStream, Cancel, CompressionError,
  ConnectError, EnhanceYourCalm, InadequateSecurity, HTTP11Required);

namespace Setting {
  using T = uint16_t;
  enum : T {
    HeaderTableSize = 0x01,
    EnablePush = 0x02,
    MaxConcurrentStreams = 0x03,
    InitialWindowSize = 0x04,
    MaxFrameSize_ = 0x05,
    MaxHeaderListSize = 0x06,
    EnableConnectProtocol = 0x08
  };
}

struct FrameHeader {
  uint32_t	length = 0;
  uint32_t	streamID = 0;
  uint8_t	type = 0;
  uint8_t	flags = 0;
  bool		reserved = false;
};

// decode a contiguous nine-byte frame header
bool decodeHeader(ZuBSpan, FrameHeader &);

// incremental nine-byte frame header parser
class FrameHeaderParser {
public:
  int process(ZuBSpan, unsigned &, FrameHeader &);
  void reset() { m_length = 0; }

private:
  ZuBArray<FrameHeaderSize>	m_bytes;
  unsigned	m_length = 0;
};

// incremental client connection preface parser
class PrefaceParser {
public:
  static ZuBSpan value();

  int process(ZuBSpan, unsigned &);
  void reset() { m_offset = 0; }

private:
  unsigned	m_offset = 0;
};

template <typename Bytes>
int putHeader(Bytes &out, FrameHeader header) {
  if (header.length > MaxFrameSize || header.streamID > MaxWindow)
    return -1;
  out.push(uint8_t(header.length>>16));
  out.push(uint8_t(header.length>>8));
  out.push(uint8_t(header.length));
  out.push(header.type);
  out.push(header.flags);
  out.push(uint8_t(header.streamID>>24));
  out.push(uint8_t(header.streamID>>16));
  out.push(uint8_t(header.streamID>>8));
  out.push(uint8_t(header.streamID));
  return FrameHeaderSize;
}

template <typename Bytes>
int putPreface(Bytes &out) {
  auto preface = PrefaceParser::value();
  unsigned n = preface.length();
  for (unsigned i = 0; i < n; ++i)
    out.push(uint8_t(preface[i]));
  return int(n);
}

template <typename Bytes>
int putUInt32(Bytes &out, uint32_t value) {
  out.push(uint8_t(value>>24));
  out.push(uint8_t(value>>16));
  out.push(uint8_t(value>>8));
  out.push(uint8_t(value));
  return 4;
}

template <typename Bytes>
int putSetting(Bytes &out, uint16_t key, uint32_t value) {
  out.push(uint8_t(key>>8));
  out.push(uint8_t(key));
  return putUInt32(out, value) + 2;
}

struct Settings {
  uint32_t	headerTableSize = 4096;
  uint32_t	maxConcurrentStreams = uint32_t(-1);
  uint32_t	initialWindowSize = DefltWindow;
  uint32_t	maxFrameSize = DefltFrameSize;
  uint32_t	maxHeaderListSize = uint32_t(-1);
  bool		enablePush = true;
  bool		enableConnectProtocol = false;

  Error::T apply(uint16_t key, uint32_t value, bool peerIsServer);
};

Error::T validateFrame(const FrameHeader &, uint32_t maxFrameSize);

template <typename Impl>
class Connection {
public:
  auto impl() { return static_cast<Impl *>(this); }

  bool init(bool server, uint32_t maxFrameSize = DefltFrameSize) {
    if (maxFrameSize < DefltFrameSize || maxFrameSize > MaxFrameSize)
      return false;
    reset();
    m_server = server;
    m_localFrameSize = maxFrameSize;
    m_preface = server;
    return true;
  }

  void reset() {
    m_headerParser.reset();
    m_prefaceParser.reset();
    m_header = {};
    m_peerSettings = {};
    m_error = Error::NoError;
    m_expectedContinuation = 0;
    m_headerStream = 0;
    m_payloadOffset = 0;
    m_settingsOutstanding = 0;
    m_fixedLength = 0;
    m_pad = 0;
    m_prefix = 0;
    m_peerFirst = true;
    m_preface = false;
    m_headersEndStream = false;
    m_inFrame = false;
  }

  int process(ZuSpan<uint8_t> input) {
    if (m_error) return -1;
    unsigned offset = 0, n = input.length();
    if (m_preface) {
      int state = m_prefaceParser.process(input, offset);
      if (state < 0) return fail_(Error::ProtocolError);
      if (!state) return int(offset);
      m_preface = false;
    }
    while (offset < n) {
      if (!m_inFrame) {
	int state = m_headerParser.process(input, offset, m_header);
	if (state < 0) return fail_(Error::ProtocolError);
	if (!state) return int(offset);
	if (!start_()) return -1;
	m_inFrame = true;
	if (!m_header.length) {
	  if (!end_()) return -1;
	  m_inFrame = false;
	  continue;
	}
      }
      unsigned available = n - offset;
      uint32_t remaining = m_header.length - m_payloadOffset;
      if (available > remaining) available = remaining;
      ZuSpan<uint8_t> span{&input[offset], available};
      if (!payload_(span)) return -1;
      offset += available;
      if (m_payloadOffset == m_header.length) {
	if (!end_()) return -1;
	m_inFrame = false;
      }
    }
    return int(offset);
  }

  Error::T error() const { return m_error; }
  uint32_t continuationStream() const { return m_expectedContinuation; }
  const Settings &peerSettings() const { return m_peerSettings; }
  uint32_t settingsOutstanding() const { return m_settingsOutstanding; }

  bool settingsSent() {
    if (m_settingsOutstanding == uint32_t(-1)) return false;
    ++m_settingsOutstanding;
    return true;
  }

  int settingsTimeout() {
    return m_settingsOutstanding ? fail_(Error::SettingsTimeout) : 0;
  }

private:
  int fail_(Error::T error) {
    if (!m_error) {
      m_error = error;
      impl()->h2Error(error);
    }
    return -1;
  }

  bool start_() {
    auto error = validateFrame(m_header, m_localFrameSize);
    if (error) return fail_(error) >= 0;
    if (m_peerFirst) {
      m_peerFirst = false;
      if (m_header.type != FrameType::Settings ||
	  (m_header.flags & Flag::ACK))
	return fail_(Error::ProtocolError) >= 0;
    }
    if (m_expectedContinuation) {
      if (m_header.type != FrameType::Continuation ||
	  m_header.streamID != m_expectedContinuation)
	return fail_(Error::ProtocolError) >= 0;
    } else if (m_header.type == FrameType::Continuation) {
      return fail_(Error::ProtocolError) >= 0;
    }
    m_payloadOffset = 0;
    m_fixedLength = 0;
    m_pad = 0;
    m_prefix = 0;
    switch (m_header.type) {
      case FrameType::Headers:
	m_headerStream = m_header.streamID;
	m_headersEndStream = m_header.flags & Flag::EndStream;
	m_prefix =
	  ((m_header.flags & Flag::Padded) ? 1U : 0U) +
	  ((m_header.flags & Flag::Priority) ? 5U : 0U);
	if (m_prefix > m_header.length)
	  return fail_(Error::FrameSizeError) >= 0;
	break;
      case FrameType::Data:
	m_prefix = (m_header.flags & Flag::Padded) ? 1U : 0U;
	if (m_prefix > m_header.length)
	  return fail_(Error::FrameSizeError) >= 0;
	if (!impl()->h2DataBegin(m_header.streamID, m_header.length))
	  return fail_(Error::FlowControlError) >= 0;
	break;
      case FrameType::PushPromise:
	m_prefix = ((m_header.flags & Flag::Padded) ? 1U : 0U) + 4U;
	if (m_prefix > m_header.length)
	  return fail_(Error::FrameSizeError) >= 0;
	break;
      default:
	break;
    }
    return true;
  }

  bool payload_(ZuSpan<uint8_t> span) {
    switch (m_header.type) {
      case FrameType::Headers:
      case FrameType::Continuation:
	return header_(span);
      case FrameType::Data:
	return data_(span);
      case FrameType::Settings:
	return settings_(span);
      case FrameType::PushPromise:
	return push_(span);
      case FrameType::Priority:
	return fixed_<5>(span);
      case FrameType::Ping:
	return fixed_<8>(span);
      case FrameType::RSTStream:
      case FrameType::WindowUpdate:
	return fixed_<4>(span);
      case FrameType::Goaway:
	return fixed_<8>(span);
      default:
	m_payloadOffset += span.length();
	return true;
    }
  }

  // process a DATA or HEADERS payload without copying its content
  template <bool Header, unsigned Priority = 0>
  bool padded_(ZuSpan<uint8_t> span) {
    unsigned offset = 0, n = span.length();
    uint32_t frameOffset = m_payloadOffset;
    if ((m_header.flags & Flag::Padded) && !frameOffset && span) {
      m_pad = span[0];
      ++offset;
      ++frameOffset;
      if (uint64_t(m_pad) + m_prefix > m_header.length)
	return fail_(Error::ProtocolError) >= 0;
    }
    unsigned skip = 0;
    unsigned prefixEnd =
      ((m_header.flags & Flag::Padded) ? 1U : 0U) + Priority;
    if (frameOffset < prefixEnd) {
      skip = prefixEnd - frameOffset;
      if (skip > n - offset) skip = n - offset;
      if constexpr (Header && Priority) {
	unsigned priorityStart =
	  (m_header.flags & Flag::Padded) ? 1U : 0U;
	for (unsigned i = 0; i < skip; ++i) {
	  uint32_t position = frameOffset + i;
	  if (position >= priorityStart &&
	      position < priorityStart + Priority)
	    m_fixed[position - priorityStart] = span[offset + i];
	}
      }
      offset += skip;
      frameOffset += skip;
    }
    uint32_t dataEnd = m_header.length - m_pad;
    unsigned deliver = 0;
    if (frameOffset < dataEnd) {
      deliver = dataEnd - frameOffset;
      if (deliver > n - offset) deliver = n - offset;
    }
    if (deliver) {
      ZuSpan<uint8_t> payload{&span[offset], deliver};
      if constexpr (Header)
	impl()->h2Headers(m_headerStream, payload);
      else
	impl()->h2Data(m_header.streamID, payload);
    }
    m_payloadOffset += n;
    return true;
  }

  bool header_(ZuSpan<uint8_t> span) {
    if (m_header.type == FrameType::Continuation) {
      m_headerStream = m_header.streamID;
      if (span) impl()->h2Headers(m_headerStream, span);
      m_payloadOffset += span.length();
      return true;
    }
    if (m_header.flags & Flag::Priority) return padded_<true, 5>(span);
    return padded_<true>(span);
  }

  bool data_(ZuSpan<uint8_t> span) {
    return padded_<false>(span);
  }

  // process SETTINGS entries incrementally
  bool settings_(ZuBSpan span) {
    for (unsigned i = 0, n = span.length(); i < n; ++i) {
      m_fixed[m_fixedLength++] = span[i];
      ++m_payloadOffset;
      if (m_fixedLength == 6) {
	uint16_t key = ZuBE(*reinterpret_cast<const uint16_t *>(m_fixed.data()));
	uint32_t value = ZuBE(*reinterpret_cast<const uint32_t *>(&m_fixed[2]));
	auto error = m_peerSettings.apply(key, value, !m_server);
	if (error) return fail_(error) >= 0;
	impl()->h2Setting(key, value);
	m_fixedLength = 0;
      }
    }
    return true;
  }

  bool push_(ZuBSpan span) {
    unsigned offset = 0, n = span.length();
    if ((m_header.flags & Flag::Padded) && !m_payloadOffset && span) {
      m_pad = span[0];
      ++offset;
      if (uint64_t(m_pad) + m_prefix > m_header.length)
	return fail_(Error::ProtocolError) >= 0;
    }
    while (offset < n && m_fixedLength < m_prefix)
      m_fixed[m_fixedLength++] = span[offset++];
    m_payloadOffset += n;
    return true;
  }

  // retain the fixed prefix needed when a frame ends
  template <unsigned Retain>
  bool fixed_(ZuBSpan span) {
    unsigned offset = 0;
    unsigned n = span.length();
    while (offset < n && m_fixedLength < Retain)
      m_fixed[m_fixedLength++] = span[offset++];
    m_payloadOffset += n;
    return true;
  }

  bool end_() {
    switch (m_header.type) {
      case FrameType::Settings:
	if (m_fixedLength) return fail_(Error::FrameSizeError) >= 0;
	if (m_header.flags & Flag::ACK) {
	  if (m_settingsOutstanding) --m_settingsOutstanding;
	  impl()->h2SettingsAck();
	} else
	  impl()->h2Settings();
	break;
      case FrameType::Headers:
	if ((m_header.flags & Flag::Priority) &&
	    (ZuBE(*reinterpret_cast<const uint32_t *>(m_fixed.data())) &
	      MaxWindow) == m_header.streamID)
	  return fail_(Error::ProtocolError) >= 0;
	if (m_header.flags & Flag::EndHeaders)
	  impl()->h2HeadersEnd(m_headerStream, m_headersEndStream);
	else
	  m_expectedContinuation = m_header.streamID;
	break;
      case FrameType::Continuation:
	if (m_header.flags & Flag::EndHeaders) {
	  m_expectedContinuation = 0;
	  impl()->h2HeadersEnd(m_headerStream, m_headersEndStream);
	}
	break;
      case FrameType::Data:
	impl()->h2DataEnd(
	  m_header.streamID, bool(m_header.flags & Flag::EndStream),
	  m_header.length, m_prefix, m_pad);
	break;
      case FrameType::Ping:
	if (m_header.flags & Flag::ACK)
	  impl()->h2PingAck(ZuBSpan{m_fixed.data(), FixedPayloadSize});
	else
	  impl()->h2Ping(ZuBSpan{m_fixed.data(), FixedPayloadSize});
	break;
      case FrameType::RSTStream:
	impl()->h2Reset(
	  m_header.streamID,
	  Error::T(ZuBE(*reinterpret_cast<const uint32_t *>(m_fixed.data()))));
	break;
      case FrameType::Priority:
	if ((ZuBE(*reinterpret_cast<const uint32_t *>(m_fixed.data())) &
	    MaxWindow) == m_header.streamID)
	  return fail_(Error::ProtocolError) >= 0;
	break;
      case FrameType::WindowUpdate: {
	uint32_t value =
	  ZuBE(*reinterpret_cast<const uint32_t *>(m_fixed.data())) & MaxWindow;
	if (!value) return fail_(Error::ProtocolError) >= 0;
	impl()->h2WindowUpdate(m_header.streamID, value);
	break;
      }
      case FrameType::Goaway:
	impl()->h2Goaway(
	  ZuBE(*reinterpret_cast<const uint32_t *>(m_fixed.data())) & MaxWindow,
	  Error::T(ZuBE(*reinterpret_cast<const uint32_t *>(&m_fixed[4]))));
	break;
      case FrameType::PushPromise:
	return fail_(Error::ProtocolError) >= 0;
      default:
	break;
    }
    return true;
  }

  FrameHeaderParser	m_headerParser;
  PrefaceParser		m_prefaceParser;
  FrameHeader		m_header;
  Settings		m_peerSettings;
  ZuBArray<FixedPayloadSize>	m_fixed;
  uint32_t		m_expectedContinuation = 0;
  uint32_t		m_headerStream = 0;
  uint32_t		m_payloadOffset = 0;
  uint32_t		m_localFrameSize = DefltFrameSize;
  uint32_t		m_settingsOutstanding = 0;
  unsigned		m_fixedLength = 0;
  unsigned		m_prefix = 0;
  unsigned		m_pad = 0;
  Error::T		m_error = Error::NoError;
  bool			m_server = false;
  bool			m_preface = false;
  bool			m_peerFirst = true;
  bool			m_inFrame = false;
  bool			m_headersEndStream = false;
};

template <typename Bytes>
int putSettingsHeader(Bytes &out, unsigned count = 0, bool ack = false) {
  return putHeader(out, {
    .length = count * 6U,
    .streamID = 0,
    .type = FrameType::Settings,
    .flags = uint8_t(ack ? Flag::ACK : 0)
  });
}

template <typename Bytes>
int putGoaway(
  Bytes &out, uint32_t lastStreamID, Error::T error) {
  if (putHeader(out, {
      .length = 8,
      .streamID = 0,
      .type = FrameType::Goaway
    }) < 0)
    return -1;
  putUInt32(out, lastStreamID & MaxWindow);
  putUInt32(out, uint32_t(error));
  return 17;
}

enum {
  StatusSize = 3,		// HTTP status is exactly three decimal digits
  UInt64BufSize = 20		// maximum decimal width of uint64_t
};

ZtEnumStruct(ZhttpAPI, ParserState, int8_t,
  Initial, Body, Stream, RemoteClosed, Trailers, Complete, Error);

template <
  typename Impl,
  bool Request_ = false,
  typename Headers_ = ZuTypeList<>>
class Parser {
public:
  Parser(uint64_t bodyMax = DefltMaxBody) :
    m_bodyMax{bodyMax}, m_bodyRx{bodyMax} { }

  auto impl() { return static_cast<Impl *>(this); }

  enum { Request = Request_ };
  using Headers = typename Headers_::template Unshift<
    ZuStringT<"content-length">, ZuTypeList<>>;
  using State = ParserState;

  void reset() {
    m_bodyRx.reset(m_bodyMax);
    m_fields = {};
    m_state = State::Initial;
    m_contentLength = -1;
    m_bodyLength = 0;
    m_complete = false;
    m_headers = false;
    m_bodyAllowed = true;
    m_extendedConnect = false;
    m_error = {};
    m_errorLatched = false;
  }
  void reset(uint64_t bodyMax_) {
    m_bodyMax = bodyMax_;
    reset();
  }
  uint64_t bodyMax() const { return m_bodyMax; }
  void bodyMax(uint64_t value) {
    m_bodyMax = value;
    m_bodyRx.reset(value);
  }

  void requestMethod(Method::T method) { m_requestMethod = method; }
  void extendedConnect(bool value) { m_extendedConnect = value; }
  void stream() {
    m_state = State::Stream;
    m_bodyRx.reset(uint64_t(-1));
  }

  bool beginHeaders(bool trailers = false) {
    if (m_headers || m_complete) return fail_();
    if (trailers && m_state != State::Body) return fail_();
    if (!trailers && m_state != State::Initial) return fail_();
    if (!trailers) m_contentLength = -1;
    m_deliverHeaders = true;
    m_fields = {};
    m_fields.trailers(trailers);
    m_fields.extendedConnect(m_extendedConnect);
    if constexpr (!Request) m_fields.requestMethod(m_requestMethod);
    m_headers = true;
    return true;
  }

  bool field(ZuBSpan name, ZuSpan<uint8_t> value) {
    if (!m_headers) return fail_();
    if (name && name[0] != ':' && !start_()) return fail_();
    if (!m_fields.field(name, value,
      [this](ZuBSpan key, ZuSpan<uint8_t> value_) {
	this->header_(key, value_);
      }) || m_state == State::Error)
      return fail_();
    return true;
  }

  bool endHeaders(bool endStream) {
    if (!m_headers) return fail_();
    m_headers = false;
    auto section = m_fields.finish(
      [this](Method::T method, Target &target) {
	(void)operation_(method, target);
      },
      [this](unsigned status) { status_(status); },
      [this](ZuBSpan key, ZuSpan<uint8_t> value) {
	if (m_state != State::Error) header_(key, value);
      });
    if (m_state == State::Error) return false;
    if (section == Zhttp::FieldSection::Invalid) return fail_();
    if (section == Zhttp::FieldSection::Informational) {
      if (endStream) return fail_();
      m_state = State::Initial;
      return true;
    }
    if (section == Zhttp::FieldSection::Trailers) {
      if (!endStream || !bodyComplete_()) return fail_();
      m_state = State::Trailers;
      complete_();
      return true;
    }
    m_bodyAllowed = m_fields.bodyAllowed();
    if (endStream) {
      if (m_bodyAllowed && !bodyComplete_()) return fail_();
    } else if (!m_bodyAllowed)
      return fail_();
    if (ZuUnlikely(!impl()->bodyInfo(
	!m_bodyAllowed ? BodyType::None :
	m_contentLength >= 0 ? BodyType::Fixed :
	endStream ? BodyType::None : BodyType::Streamed,
	m_bodyAllowed && m_contentLength >= 0 ?
	  uint64_t(m_contentLength) : 0)))
      return fail_(RequestErrorCode::BodyRejected,
	RequestErrorScope::Request, Request);
    if (m_state == State::Stream) {
      if (endStream) return fail_();
      return true;
    }
    if (endStream) {
      complete_();
    } else {
      m_state = State::Body;
    }
    return true;
  }

  template <typename Rx>
  bool data(
    Rx &wire, uint64_t frameLength, unsigned headLen, unsigned tailLen,
    bool endStream, bool &transferred)
  {
    transferred = false;
    if (ZuUnlikely(
	headLen > frameLength || tailLen > frameLength - headLen))
      return fail_();
    uint64_t length = frameLength - headLen - tailLen;
    if (!dataLength(length, endStream)) return false;
    uint64_t remaining = frameLength;
    bool accepted = true;
    int64_t n = m_bodyRx.splice(
      wire, length,
      [&remaining](ZuBSpan span) -> int64_t {
	if (remaining > span.length()) {
	  remaining -= span.length();
	  return 0;
	}
	return remaining;
      }, wireAlloc_, alloc_, headLen, tailLen,
      [this, &accepted](auto &rx) {
	if (m_state == State::Stream)
	  impl()->streamRx_(rx);
	else
	  accepted = impl()->body(rx);
      });
    if (ZuUnlikely(!accepted))
      return fail_(RequestErrorCode::BodyRejected,
	RequestErrorScope::Request, Request);
    if (ZuUnlikely(n <= 0)) return fail_();
    transferred = true;
    if (m_state == State::Stream) {
      if (endStream) {
	impl()->streamPeerEnd_();
	m_bodyRx.discard();
	m_state = State::RemoteClosed;
      }
      return true;
    }
    m_bodyLength += length;
    if (endStream) complete_();
    return true;
  }

  bool dataLength(uint64_t length, bool endStream) {
    if (m_state == State::Stream) return true;
    if (m_state != State::Body || !m_bodyAllowed)
      return fail_();
    if (m_bodyLength > m_bodyMax || length > m_bodyMax - m_bodyLength)
      return fail_(RequestErrorCode::ContentTooLarge,
	RequestErrorScope::Stream, false);
    if (m_contentLength >= 0 &&
	(m_bodyLength > uint64_t(m_contentLength) ||
	 length > uint64_t(m_contentLength) - m_bodyLength ||
	 (endStream && m_bodyLength + length != uint64_t(m_contentLength))))
      return fail_();
    return true;
  }

  template <typename Rx>
  State::T process(Rx &rx) {
    rx.process(*this);
    return m_state;
  }

  State::T state() const { return m_state; }
  const RequestError &error() const { return m_error; }
  uint64_t consumed() const { return m_bodyRx.consumed(); }
  bool cancel() {
    if (m_state == State::Stream) impl()->streamError_();
    return fail_();
  }

  template <typename Rx>
  void streamRx_(Rx &rx) { bodyDrain(rx); }
  void streamPeerEnd_() { }
  void streamError_() { }

private:
  bool operation_(Method::T method, Target &target) {
    if constexpr (Request)
      if (ZuUnlikely(!impl()->operation(method, target)))
	return fail_(RequestErrorCode::OperationRejected,
	  RequestErrorScope::Request, true);
    return true;
  }

  bool start_() {
    bool ok = m_fields.start(
      [this](Method::T method, Target &target) {
	(void)operation_(method, target);
      },
      [this](unsigned status) { status_(status); },
      [this](ZuBSpan key, ZuSpan<uint8_t> value) {
	if (m_state != State::Error) header_(key, value);
      });
    return ok && m_state != State::Error;
  }

  void header_(ZuBSpan key, ZuSpan<uint8_t> value) {
    if (key == "content-length") {
      uint64_t length = 0;
      if (!Fields::uint64(value, length)) {
	fail_();
	return;
      }
      if (length > m_bodyMax) {
	fail_(RequestErrorCode::ContentTooLarge,
	  RequestErrorScope::Request, true);
	return;
      }
      m_contentLength = length;
      return;
    }
    if (!m_deliverHeaders) return;
    Fields::dispatch<Headers>(
      key, value,
      [this](auto key_, ZuSpan<uint8_t> value_) {
	impl()->template header<ZuDecay<decltype(key_)>>(
	  m_fields.section(), value_);
      },
      [this](auto key_, auto value_) {
	impl()->template header<
	  ZuDecay<decltype(key_)>, ZuDecay<decltype(value_)>>(
	    m_fields.section());
      },
      [this](ZuBSpan key_, ZuSpan<uint8_t> value_) {
	runtimeHeader_(key_, value_);
      });
  }

  void runtimeHeader_(ZuBSpan key, ZuSpan<uint8_t> value) {
    if constexpr (Fields::HasRuntime<Impl>{}) {
      if (!m_deliverHeaders) return;
      impl()->header(m_fields.section(), key, value);
    }
  }

  void status_(unsigned value) {
    if constexpr (!Request)
      m_deliverHeaders = value >= 200 || impl()->enable1xx();
    if (m_deliverHeaders) impl()->status(value);
  }

  bool bodyComplete_() const {
    return m_contentLength < 0 ||
      m_bodyLength == uint64_t(m_contentLength);
  }
  bool fail_(
    RequestErrorCode::T code = RequestErrorCode::Malformed,
    RequestErrorScope::T scope = RequestErrorScope::Stream,
    bool responsePossible = false) {
    if (!m_errorLatched) {
      m_error = {code, scope, responsePossible};
      m_errorLatched = true;
    }
    m_state = State::Error;
    complete_();
    return false;
  }
  void complete_() {
    if (m_complete) return;
    m_complete = true;
    if (m_state != State::Error) m_state = State::Complete;
    impl()->complete(m_state);
    m_bodyRx.discard();
  }

  static ZmRef<ZiRxQueue::Node> alloc_() {
    return new BodyRx::BufAlloc{};
  }
  static ZmRef<ZiRxQueue::Node> wireAlloc_() {
    return new BodyRx::WireBufAlloc{};
  }

  Fields::Semantics<Request> m_fields;
  uint64_t		m_bodyMax;
  BodyRx		m_bodyRx;
  State::T		m_state = State::Initial;
  Method::T		m_requestMethod = -1;
  int64_t		m_contentLength = -1;
  uint64_t		m_bodyLength = 0;
  bool			m_complete = false;
  bool			m_headers = false;
  bool			m_bodyAllowed = true;
  bool			m_extendedConnect = false;
  RequestError		m_error;
  bool			m_errorLatched = false;
  bool			m_deliverHeaders = true;
};

template <
  typename Impl,
  typename Headers_ = ZuTypeList<>,
  bool HasBody_ = false,
  bool Streaming_ = false>
class Builder_ {
public:
  auto impl() { return static_cast<Impl *>(this); }

  using Headers = Headers_;
  enum { HasBody = HasBody_ };
  enum { Streaming = Streaming_ };

protected:
  template <typename Stream>
  bool beginRequest_(Stream &stream) {
    using HeaderBytes = typename Stream::HeaderBytes;
    auto block = ZtScratch(HeaderBytes, Impl::HdrBufSize);
    typename Stream::HeaderSection section{block};
    bool sent = false;
    bool endStream = false;
    bool streamMode = false;
    impl()->operation(
      [this, &stream, &section, &sent, &endStream, &streamMode]
      (Method::T method, auto &&emit) {
      ZuBSpan protocol;
      if (method == Method::CONNECT)
	impl()->protocol([&protocol]<typename P>(P &&value) {
	  protocol = ZuFwd<P>(value);
	});
      if (protocol && !stream.extendedConnect()) return;
      streamMode = bool(protocol);
      endStream = !HasBody && !protocol;
      stream.beginHeaders(section, endStream);
      Builder_::field_(stream, ":method", Method::name(method));
      if (method != Method::CONNECT || protocol) {
	Builder_::field_(stream, ":scheme", "https");
	emit([&stream](auto &&write) {
	  auto writer = Zhttp::Builder_::writer(write);
	  Builder_::field_(stream, ":path", writer);
	});
      }
      if (protocol) Builder_::field_(stream, ":protocol", protocol);
      sent = true;
    });
    if (!sent) return false;
    impl()->host([&stream]<typename Host>(Host &&host) {
      Builder_::field_(stream, ":authority", ZuFwd<Host>(host));
    });
    if (streamMode)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    patch_(stream);
    stream.endHeaders(endStream);
    return true;
  }

  template <typename Stream>
  void beginResponse_(Stream &stream) {
    using HeaderBytes = typename Stream::HeaderBytes;
    auto block = ZtScratch(HeaderBytes, Impl::HdrBufSize);
    typename Stream::HeaderSection section{block};
    unsigned value = impl()->status();
    bool informational = value >= 100 && value < 200;
    bool streamMode = impl()->streamResponse();
    bool endStream =
      !informational && !streamMode && !HasBody;
    stream.beginHeaders(section, endStream);
    ZuBArray<StatusSize> status;
    status[0] = uint8_t('0' + ((value / 100) % 10));
    status[1] = uint8_t('0' + ((value / 10) % 10));
    status[2] = uint8_t('0' + (value % 10));
    field_(stream, ":status", status.span());
    if (streamMode)
      headers_<Headers, false>(stream);
    else
      headers_(stream);
    patch_(stream);
    stream.endHeaders(endStream);
  }

public:
  template <typename Stream>
  auto body(Stream &stream) {
    return stream.body();
  }
  template <typename Stream>
  auto body(Stream &stream, uint64_t remaining) {
    if constexpr (Streaming)
      return stream.body();
    else
      return stream.body(remaining);
  }

  template <typename Stream>
  void finish(Stream &stream) {
    if constexpr (HasBody)
      stream.end();
    stream.flush();
  }

  template <typename L> void host(L &&l) { l("127.0.0.1"); }
  template <typename L> void protocol(L &&) { }
  bool streamResponse() { return false; }

private:
  template <typename Key, typename I = Impl>
  static auto headerOffset_(I *impl, uint64_t offset, unsigned length, int) ->
      decltype(impl->template headerOffset<Key>(offset, length), void()) {
    impl->template headerOffset<Key>(offset, length);
  }
  template <typename Key>
  static void headerOffset_(Impl *, uint64_t, unsigned, ...) { }

  template <typename Stream, typename V>
  static void field_(Stream &stream, ZuBSpan name, V &&value) {
    stream.field(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static auto nameField_(Stream &stream, ZuBSpan name, V &&value, int) ->
      decltype(stream.fieldName(name, ZuFwd<V>(value)), void()) {
    stream.fieldName(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static void nameField_(Stream &stream, ZuBSpan name, V &&value, ...) {
    stream.field(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static auto literalField_(Stream &stream, ZuBSpan name, V &&value, int) ->
      decltype(stream.fieldLiteral(name, ZuFwd<V>(value)), void()) {
    stream.fieldLiteral(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static void literalField_(Stream &stream, ZuBSpan name, V &&value, ...) {
    stream.field(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static auto fixedField_(Stream &stream, ZuBSpan name, V &&value, int) ->
      decltype(stream.fieldFixed(name, ZuFwd<V>(value)), void()) {
    stream.fieldFixed(name, ZuFwd<V>(value));
  }
  template <typename Stream, typename V>
  static void fixedField_(Stream &stream, ZuBSpan name, V &&value, ...) {
    stream.field(name, ZuFwd<V>(value));
  }

  template <typename Key, typename Stream, typename V>
  static auto mutableField_(Impl *impl, Stream &stream, V &&value, int) -> decltype(
      stream.fieldMutable(Key{}(), ZuFwd<V>(value)), void()) {
    auto span = stream.fieldMutable(Key{}(), ZuFwd<V>(value));
    headerOffset_<Key>(impl, span.offset, span.length, 0);
  }
  template <typename Key, typename Stream, typename V>
  static void mutableField_(Impl *, Stream &stream, V &&value, ...) {
    stream.field(Key{}(), ZuFwd<V>(value));
  }

  template <typename Stream, typename I = Impl>
  auto patchBase_(Stream &stream, int) -> decltype(
      stream.headerBase(),
      ZuDeclVal<I &>().headerBase(stream.headerBase()), void()) {
    impl()->headerBase(stream.headerBase());
  }
  template <typename Stream>
  void patchBase_(Stream &, ...) { }
  template <typename I = Impl>
  auto patchImpl_(int) -> decltype(ZuDeclVal<I &>().patch(), void()) {
    impl()->patch();
  }
  void patchImpl_(...) { }
  template <typename Stream>
  void patch_(Stream &stream) {
    if constexpr (HasBody && !Streaming) {
      patchBase_(stream, 0);
      patchImpl_(0);
    }
  }

  template <
    typename KVs = Headers, bool IncludeContentLength = true,
    typename Stream>
  void headers_(Stream &stream) {
    using List = HeaderList<KVs>;
    ZuUnroll::all<List::N>([this, &stream](auto I) {
      using Key = typename List::template Key<I>;
      using Value = typename List::template Value<I>;
      if constexpr (Value::N) {
	using Fixed = HeaderValue<Value>;
	builderFixedHeader<Key, Fixed>(impl(), [this, &stream]() {
	  fixedField_(stream, Key{}(), Fixed{}(), 0);
	}, 0);
	impl()->template header<Key>([&stream]<typename V>(V &&v) {
	  nameField_(stream, Key{}(), ZuFwd<V>(v), 0);
	});
      } else {
	auto impl = this->impl();
	impl->template header<Key>([impl, &stream]<typename V>(V &&v) {
	  mutableField_<Key>(impl, stream, ZuFwd<V>(v), 0);
	});
      }
    });
    runtimeHeaders_(stream);
  }

  template <typename Stream>
  void runtimeHeaders_(Stream &stream) {
    auto fn = [&stream]<typename K, typename V>(K &&k, V &&v) {
      ZtBArray<ZtArrayHeapID<"Zhttp.H2.HeaderName">> name;
      name << ZuFwd<K>(k);
      if (name) literalField_(stream, name, ZuFwd<V>(v), 0);
    };
    impl()->header(ZuMv(fn));
  }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class Request : public Builder_<Impl, Headers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, HasBody, Streaming>;

public:
  template <typename Stream>
  bool begin(Stream &stream) { return Base::beginRequest_(stream); }
};

template <
  typename Impl,
  typename Headers = ZuTypeList<>,
  bool HasBody = false,
  bool Streaming = false>
class Response : public Builder_<Impl, Headers, HasBody, Streaming> {
  using Base = Builder_<Impl, Headers, HasBody, Streaming>;

public:
  template <typename Stream>
  void begin(Stream &stream) { Base::beginResponse_(stream); }
};


} // namespace H2

} // namespace Zhttp

#endif /* ZhttpH2_HH */
