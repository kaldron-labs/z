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

#include <zlib/ZtEnum.hh>

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
bool decodeHeader(ZuCSpan, FrameHeader &);

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
  static ZuCSpan value();

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

  int process(ZuBSpan input) {
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
      ZuBSpan span{&input[offset], available};
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

  bool payload_(ZuBSpan span) {
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
  bool padded_(ZuBSpan span) {
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
      ZuBSpan payload{&span[offset], deliver};
      if constexpr (Header)
	impl()->h2Headers(m_headerStream, payload);
      else
	impl()->h2Data(m_header.streamID, payload);
    }
    m_payloadOffset += n;
    return true;
  }

  bool header_(ZuBSpan span) {
    if (m_header.type == FrameType::Continuation) {
      m_headerStream = m_header.streamID;
      if (span) impl()->h2Headers(m_headerStream, span);
      m_payloadOffset += span.length();
      return true;
    }
    if (m_header.flags & Flag::Priority) return padded_<true, 5>(span);
    return padded_<true>(span);
  }

  bool data_(ZuBSpan span) {
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

} // namespace H2

} // namespace Zhttp

#endif /* ZhttpH2_HH */
