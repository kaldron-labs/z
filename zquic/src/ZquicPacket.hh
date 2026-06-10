//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC packet codec

#ifndef ZquicPacket_HH
#define ZquicPacket_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <string.h>

#include <limits.h>

#include <zpicotls.h>

#include <zlib/ZuHash.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuArray.hh>

#include <zlib/ZquicBuf.hh>

namespace Zquic {

namespace VarInt {
  unsigned length(uint64_t);
  int encode(uint8_t *, unsigned, uint64_t);
  int put(uint8_t *, unsigned, uint64_t, unsigned &);
  int decode(ZuCSpan, uint64_t &, unsigned &);
}

using CxnID = ZuBArray<20>;
inline constexpr unsigned CxnIDMax = 20;

struct PacketNumber {
  static unsigned encodedLength(uint64_t pn, uint64_t largestAcked);
  static int encode(uint8_t *, unsigned, uint64_t pn, unsigned length);
  static uint64_t decode(uint64_t largestPN, uint64_t truncated, unsigned bits);
};

struct LongHeader {
  PacketType::T	type = PacketType::Initial;
  uint32_t	version = Version1;
  CxnID		dcid;
  CxnID		scid;
  uint64_t	tokenLength = 0;
  uint64_t	length = 0;
  unsigned	pnLength = 0;
  unsigned	pnOffset = 0;
  unsigned	payloadOffset = 0;
};

struct RetryPacket {
  LongHeader	header;
  ZuCSpan	token;
  ZuCSpan	integrityTag;
};

struct ShortHeader {
  CxnID	dcid;
  unsigned	pnLength = 0;
  unsigned	pnOffset = 0;
};

struct Packet {
  static bool isLong(ZuCSpan);
  static bool isVersionNegotiation(ZuCSpan);
  static int parseLong(ZuCSpan, LongHeader &);
  static int parseRetry(ZuCSpan, RetryPacket &);
  static int retryIntegrityTag(
    uint8_t *, unsigned, ZuCSpan retryWithoutTag,
    const CxnID &originalDCID);
  static bool validateRetryIntegrity(ZuCSpan, const CxnID &originalDCID);
  static int parseShort(ZuCSpan, unsigned cidLen, ShortHeader &);
  static int parseVersionNegotiation(
    ZuCSpan, uint32_t *, unsigned capacity, unsigned &nVersions);
  static int writeLong(
    uint8_t *, unsigned, PacketType::T,
    const CxnID &, const CxnID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeInitial(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeHandshake(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeRetry(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    ZuCSpan token, ZuCSpan retryIntegrityTag = {});
  static int writeRetryAuthenticated(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    ZuCSpan token, const CxnID &originalDCID);
  static int writeShort(
    uint8_t *, unsigned, const CxnID &, uint64_t pn, unsigned pnLength);
  static int writeVersionNegotiation(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    const uint32_t *, unsigned);
};

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

#endif /* ZquicPacket_HH */
