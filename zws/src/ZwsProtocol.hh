//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// RFC 6455 protocol vocabulary and wire utilities

#ifndef ZwsProtocol_HH
#define ZwsProtocol_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZtEnum.hh>

namespace Zws {

template <typename App, typename = void>
struct AppLinkState { };
template <typename App>
struct AppLinkState<App, decltype(
  (typename App::LinkState *)nullptr, void())> : public App::LinkState { };

ZtEnumStruct(Opcode, uint8_t,
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xa);

ZtEnumStruct(CloseCode, uint16_t,
  Normal = 1000,
  GoingAway = 1001,
  Protocol = 1002,
  Unsupported = 1003,
  NoStatus = 1005,
  InvalidData = 1007,
  Policy = 1008,
  TooLarge = 1009,
  Extension = 1010,
  Internal = 1011,
  Restart = 1012,
  TryAgain = 1013,
  BadGateway = 1014);

ZtEnumStruct(Failure, uint8_t,
  None,
  InvalidHeader,
  ReservedBits,
  InvalidOpcode,
  MaskDirection,
  NonCanonicalLength,
  ControlFragment,
  ControlTooLarge,
  UnexpectedContinuation,
  MissingContinuation,
  FrameTooLarge,
  MessageTooLarge,
  InputPressure,
  InvalidUTF8,
  InvalidClose,
  Transmit,
  Handshake,
  Capability,
  Timeout,
  AbnormalClose);

struct Config {
  uint64_t	maxFrame = uint64_t(1)<<30;
  uint64_t	maxMessage = uint64_t(1)<<30;
  uint64_t	maxQueuedInput = uint64_t(1)<<30;
  unsigned	handshakeTimeout = 10;
  unsigned	closeTimeout = 5;
  unsigned	pingInterval = 0;
  unsigned	pongTimeout = 5;
};

struct Frame {
  uint64_t	length = 0;
  uint32_t	key = 0;
  Opcode::T	opcode = Opcode::Continuation;
  bool		final = false;
  bool		masked = false;
};

enum {
  MaxHeader = 14,
  MaxControl = 125
};

inline bool control(Opcode::T opcode)
{
  return opcode >= Opcode::Close;
}

inline bool data(Opcode::T opcode)
{
  return opcode == Opcode::Text || opcode == Opcode::Binary;
}

inline bool opcode(unsigned opcode)
{
  return opcode == Opcode::Continuation || opcode == Opcode::Text ||
    opcode == Opcode::Binary || opcode == Opcode::Close ||
    opcode == Opcode::Ping || opcode == Opcode::Pong;
}

inline bool closeCode(uint16_t code)
{
  if (code >= 3000 && code <= 4999) return true;
  switch (code) {
    case CloseCode::Normal:
    case CloseCode::GoingAway:
    case CloseCode::Protocol:
    case CloseCode::Unsupported:
    case CloseCode::InvalidData:
    case CloseCode::Policy:
    case CloseCode::TooLarge:
    case CloseCode::Extension:
    case CloseCode::Internal:
    case CloseCode::Restart:
    case CloseCode::TryAgain:
    case CloseCode::BadGateway:
      return true;
  }
  return false;
}

inline void mask(ZuSpan<uint8_t> data, uint32_t key, uint64_t offset = 0)
{
  uint8_t bytes[] = {
    uint8_t(key>>24), uint8_t(key>>16), uint8_t(key>>8), uint8_t(key)
  };
  unsigned n = data.length();
  for (unsigned i = 0; i < n; ++i)
    data[i] ^= bytes[(offset + i) & 3];
}

class HeaderParser {
public:
  struct Result {
    enum { Wait, Complete, Error };
  };

  void reset() {
    m_have = 0;
    m_need = 2;
  }

  int process(ZuBSpan &input, Frame &frame, Failure::T &failure) {
    while (m_have < m_need && input) {
      unsigned n = m_need - m_have;
      if (n > input.length()) n = input.length();
      for (unsigned i = 0; i < n; ++i) m_data[m_have + i] = input[i];
      m_have += n;
      input.offset(n);
      if (m_have == 2) {
	unsigned length = m_data[1] & 0x7f;
	m_need = 2 + (length == 126 ? 2 : length == 127 ? 8 : 0) +
	  ((m_data[1] & 0x80) ? 4 : 0);
      }
    }
    if (m_have < m_need) return Result::Wait;

    unsigned opcode_ = m_data[0] & 0x0f;
    if (m_data[0] & 0x70) {
      failure = Failure::ReservedBits;
      return Result::Error;
    }
    if (!Zws::opcode(opcode_)) {
      failure = Failure::InvalidOpcode;
      return Result::Error;
    }
    frame.final = m_data[0] & 0x80;
    frame.opcode = Opcode::T(opcode_);
    frame.masked = m_data[1] & 0x80;
    unsigned encoded = m_data[1] & 0x7f;
    unsigned offset = 2;
    if (encoded < 126)
      frame.length = encoded;
    else if (encoded == 126) {
      frame.length = (uint64_t(m_data[2])<<8) | m_data[3];
      offset += 2;
      if (frame.length < 126) {
	failure = Failure::NonCanonicalLength;
	return Result::Error;
      }
    } else {
      if (m_data[2] & 0x80) {
	failure = Failure::InvalidHeader;
	return Result::Error;
      }
      frame.length = 0;
      for (unsigned i = 0; i < 8; ++i)
	frame.length = (frame.length<<8) | m_data[2 + i];
      offset += 8;
      if (frame.length <= 0xffff) {
	failure = Failure::NonCanonicalLength;
	return Result::Error;
      }
    }
    frame.key = 0;
    if (frame.masked)
      for (unsigned i = 0; i < 4; ++i)
	frame.key = (frame.key<<8) | m_data[offset + i];
    reset();
    return Result::Complete;
  }

private:
  uint8_t	m_data[MaxHeader];
  uint8_t	m_have = 0;
  uint8_t	m_need = 2;
};

class UTF8 {
public:
  void reset() {
    m_code = 0;
    m_min = 0;
    m_need = 0;
    m_valid = true;
  }

  bool update(ZuBSpan span) {
    if (!m_valid) return false;
    for (uint8_t c : span) {
      if (!m_need) {
	if (c < 0x80) continue;
	if (c >= 0xc2 && c <= 0xdf) {
	  m_code = c & 0x1f;
	  m_min = 0x80;
	  m_need = 1;
	} else if (c >= 0xe0 && c <= 0xef) {
	  m_code = c & 0x0f;
	  m_min = 0x800;
	  m_need = 2;
	} else if (c >= 0xf0 && c <= 0xf4) {
	  m_code = c & 0x07;
	  m_min = 0x10000;
	  m_need = 3;
	} else
	  return m_valid = false;
	continue;
      }
      if ((c & 0xc0) != 0x80) return m_valid = false;
      m_code = (m_code<<6) | (c & 0x3f);
      --m_need;
      if (!m_need &&
	  (m_code < m_min || m_code > 0x10ffff ||
	   (m_code >= 0xd800 && m_code <= 0xdfff)))
	return m_valid = false;
    }
    return true;
  }

  bool complete() const { return m_valid && !m_need; }
  bool valid() const { return m_valid; }

private:
  uint32_t	m_code = 0;
  uint32_t	m_min = 0;
  uint8_t	m_need = 0;
  bool		m_valid = true;
};

} // namespace Zws

#endif /* ZwsProtocol_HH */
