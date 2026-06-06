//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC packet plaintext vector builder

#ifndef ZquicPacketBuilder_HH
#define ZquicPacketBuilder_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zpicotls.h>

#include <limits.h>

#include <zlib/ZuSpan.hh>

#include <zlib/ZquicTypes.hh>

namespace Zquic {

inline constexpr uint8_t PacketBuildZeroPad[BufSize] = {};

inline ZuCSpan packetBuildSpan(const uint8_t *data, unsigned len)
{
  return ZuCSpan{reinterpret_cast<const char *>(data), len};
}

class PlainVec {
public:
  static constexpr unsigned Max = 8;

  const ptls_iovec_t *data() const { return m_vec; }
  unsigned count() const { return m_count; }
  unsigned bytes() const { return m_bytes; }

  void reset() {
    m_count = 0;
    m_bytes = 0;
  }

  bool add(ZuCSpan span) {
    if (!span.length()) return true;
    if (m_count >= Max || span.length() > UINT_MAX - m_bytes) return false;
    m_vec[m_count++] = ptls_iovec_init(span.data(), span.length());
    m_bytes += span.length();
    return true;
  }

private:
  ptls_iovec_t	m_vec[Max];
  unsigned	m_count = 0;
  unsigned	m_bytes = 0;
};

class PacketBuild {
public:
  const ptls_iovec_t *data() const { return m_plain.data(); }
  unsigned count() const { return m_plain.count(); }
  unsigned bytes() const { return m_plain.bytes(); }
  uint8_t *scratch() { return m_scratch + m_scratchLen; }
  unsigned scratchAvail() const { return BufSize - m_scratchLen; }

  void reset() {
    m_plain.reset();
    m_scratchLen = 0;
  }

  bool commitScratch(unsigned len) {
    if (len > scratchAvail()) return false;
    unsigned off = m_scratchLen;
    m_scratchLen += len;
    return m_plain.add(packetBuildSpan(m_scratch + off, len));
  }

  bool add(ZuCSpan span) { return m_plain.add(span); }

  bool pad(unsigned len) {
    if (!len) return true;
    if (len > BufSize) return false;
    return m_plain.add(packetBuildSpan(PacketBuildZeroPad, len));
  }

  bool padTo(unsigned bytes) {
    unsigned n = m_plain.bytes();
    return bytes <= n || pad(bytes - n);
  }

  bool padForProtectionSample(
    unsigned pnOffset, unsigned pnLength, unsigned tagLen)
  {
    unsigned headerLen = pnOffset + pnLength;
    unsigned minPacketLen = pnOffset + 4 + 16;
    unsigned minPayloadLen = minPacketLen > headerLen + tagLen ?
      minPacketLen - headerLen - tagLen : 0;
    return padTo(minPayloadLen);
  }

private:
  PlainVec	m_plain;
  uint8_t	m_scratch[BufSize];
  unsigned	m_scratchLen = 0;
};

} // namespace Zquic

#endif /* ZquicPacketBuilder_HH */
