//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpH2.hh>

namespace Zhttp { namespace H2 {

ZtEnumImplNS(FrameType);
ZtEnumImplNS(Error);

ZuCSpan PrefaceParser::value()
{
  return "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
}

int PrefaceParser::process(ZuBSpan input, unsigned &offset)
{
  auto preface = value();
  unsigned n = input.length(), end = preface.length();
  while (offset < n && m_offset < end) {
    if (input[offset++] != uint8_t(preface[m_offset++])) return -1;
  }
  return m_offset == end;
}

int FrameHeaderParser::process(
  ZuBSpan input, unsigned &offset, FrameHeader &header)
{
  unsigned n = input.length(), end = m_bytes.size();
  while (offset < n && m_length < end)
    m_bytes[m_length++] = input[offset++];
  if (m_length < end) return 0;
  decodeHeader(ZuCSpan{m_bytes.data(), end}, header);
  m_length = 0;
  return 1;
}

bool decodeHeader(ZuCSpan input, FrameHeader &header)
{
  if (input.length() < FrameHeaderSize) return false;
  auto p = reinterpret_cast<const uint8_t *>(input.data());
  header.length =
    (uint32_t(p[0])<<16) |
    ZuBE(*reinterpret_cast<const uint16_t *>(&p[1]));
  header.type = p[3];
  header.flags = p[4];
  uint32_t streamID = ZuBE(*reinterpret_cast<const uint32_t *>(&p[5]));
  header.reserved = streamID>>31;
  header.streamID = streamID & MaxWindow;
  return true;
}

Error::T Settings::apply(
  uint16_t key, uint32_t value, bool peerIsServer)
{
  switch (key) {
    case Setting::HeaderTableSize:
      headerTableSize = value;
      break;
    case Setting::EnablePush:
      if (peerIsServer || value > 1) return Error::ProtocolError;
      enablePush = value;
      break;
    case Setting::MaxConcurrentStreams:
      maxConcurrentStreams = value;
      break;
    case Setting::InitialWindowSize:
      if (value > MaxWindow) return Error::FlowControlError;
      initialWindowSize = value;
      break;
    case Setting::MaxFrameSize_:
      if (value < DefltFrameSize || value > MaxFrameSize)
	return Error::ProtocolError;
      maxFrameSize = value;
      break;
    case Setting::MaxHeaderListSize:
      maxHeaderListSize = value;
      break;
    case Setting::EnableConnectProtocol:
      if (value > 1) return Error::ProtocolError;
      enableConnectProtocol = value;
      break;
    default:
      break;
  }
  return Error::NoError;
}

Error::T validateFrame(
  const FrameHeader &header, uint32_t maxFrameSize)
{
  if (header.length > maxFrameSize) return Error::FrameSizeError;
  switch (header.type) {
    case FrameType::Data:
    case FrameType::Headers:
    case FrameType::Priority:
    case FrameType::RSTStream:
    case FrameType::PushPromise:
    case FrameType::Continuation:
      if (!header.streamID) return Error::ProtocolError;
      break;
    case FrameType::Settings:
    case FrameType::Ping:
    case FrameType::Goaway:
      if (header.streamID) return Error::ProtocolError;
      break;
    default:
      break;
  }
  switch (header.type) {
    case FrameType::Priority:
      if (header.length != 5) return Error::FrameSizeError;
      break;
    case FrameType::RSTStream:
    case FrameType::WindowUpdate:
      if (header.length != 4) return Error::FrameSizeError;
      break;
    case FrameType::Settings:
      if ((header.flags & Flag::ACK) && header.length)
	return Error::FrameSizeError;
      if (header.length % 6) return Error::FrameSizeError;
      break;
    case FrameType::PushPromise:
      if (header.length < 4) return Error::FrameSizeError;
      break;
    case FrameType::Ping:
      if (header.length != 8) return Error::FrameSizeError;
      break;
    case FrameType::Goaway:
      if (header.length < 8) return Error::FrameSizeError;
      break;
    default:
      break;
  }
  return Error::NoError;
}

}} // namespace Zhttp::H2
