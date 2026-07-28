//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpH2.hh>

namespace Zhttp { namespace H2 {

ZuCSpan PrefaceParser::value()
{
  return "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
}

int PrefaceParser::process(ZuBSpan input, unsigned &offset)
{
  auto preface = value();
  while (offset < input.length() && m_offset < preface.length()) {
    if (input[offset++] != uint8_t(preface[m_offset++])) return -1;
  }
  return m_offset == preface.length();
}

int FrameHeaderParser::process(
  ZuBSpan input, unsigned &offset, FrameHeader &header)
{
  while (offset < input.length() && m_length < sizeof(m_bytes))
    m_bytes[m_length++] = input[offset++];
  if (m_length < sizeof(m_bytes)) return 0;
  header.length =
    (uint32_t(m_bytes[0])<<16) |
    (uint32_t(m_bytes[1])<<8) |
    uint32_t(m_bytes[2]);
  header.type = m_bytes[3];
  header.flags = m_bytes[4];
  header.reserved = m_bytes[5] & 0x80;
  header.streamID =
    (uint32_t(m_bytes[5] & 0x7f)<<24) |
    (uint32_t(m_bytes[6])<<16) |
    (uint32_t(m_bytes[7])<<8) |
    uint32_t(m_bytes[8]);
  m_length = 0;
  return 1;
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
