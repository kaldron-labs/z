//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC packet codec

#ifndef ZquicPkt_HH
#define ZquicPkt_HH

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

inline constexpr unsigned CxnIDMax = 20;
using CxnID = ZuBArray<CxnIDMax>;

struct PktNumber {
  static unsigned encodedLength(uint64_t pn, uint64_t largestAckd);
  static int encode(uint8_t *, unsigned, uint64_t pn, unsigned length);
  static uint64_t decode(uint64_t largestPN, uint64_t truncated, unsigned bits);
};

struct LongHdr {
  PktType::T	type = PktType::Initial;
  uint32_t	version = Version1;
  CxnID		dcid;
  CxnID		scid;
  uint64_t	tokenLength = 0;
  uint64_t	length = 0;
  unsigned	pnLength = 0;
  unsigned	pnOffset = 0;
  unsigned	payloadOffset = 0;
};

struct RetryPkt {
  LongHdr	header;
  ZuCSpan	token;
  ZuCSpan	integrityTag;
};

struct ShortHdr {
  CxnID	dcid;
  unsigned	pnLength = 0;
  unsigned	pnOffset = 0;
  bool		keyPhase = false;
};

struct Pkt {
  static bool isLong(ZuCSpan);
  static bool isVersionNegotiation(ZuCSpan);
  static int parseLong(ZuCSpan, LongHdr &);
  static int parseRetry(ZuCSpan, RetryPkt &);
  static int retryIntegrityTag(
    uint8_t *, unsigned, ZuCSpan retryWithoutTag,
    const CxnID &originalDCID);
  static bool validateRetryIntegrity(ZuCSpan, const CxnID &originalDCID);
  static int parseShort(ZuCSpan, unsigned cidLen, ShortHdr &);
  static int parseVersionNegotiation(
    ZuCSpan, uint32_t *, unsigned capacity, unsigned &nVersions);
  static int writeLong(
    uint8_t *, unsigned, PktType::T,
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
    uint8_t *, unsigned, const CxnID &, uint64_t pn, unsigned pnLength,
    bool keyPhase = false);
  static int writeVersionNegotiation(
    uint8_t *, unsigned, const CxnID &, const CxnID &,
    const uint32_t *, unsigned);
};

inline constexpr uint8_t PktBuildZeroPad[BufSize] = {};
// Scratch holds locally encoded ACK/control and frame prefixes only. It is
// sized for the current ACK_ECN encoder cap of 64 ranges plus one STREAM prefix.
inline constexpr unsigned PktBuildMaxAckRanges = 64;
inline constexpr unsigned PktBuildScratchSize =
  1 + (7 * 8) + ((PktBuildMaxAckRanges - 1) * 2 * 8) +
  1 + (3 * 8);

inline ZuCSpan pktBuildSpan(const uint8_t *data, unsigned len)
{
  return ZuCSpan{reinterpret_cast<const char *>(data), len};
}

class PlainVec {
public:
  // Current packet assembly uses at most: generated control, frame prefix,
  // frame payload, and padding for up to SentPkt::MaxFrames runtime frames.
  enum { Max = 18 };

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

class PktBuild {
public:
  PktBuild() = default;

  PktBuild(const PktBuild &) = delete;
  PktBuild &operator =(const PktBuild &) = delete;

  const ptls_iovec_t *data() const { return m_plain.data(); }
  unsigned count() const { return m_plain.count(); }
  unsigned bytes() const { return m_plain.bytes(); }
  uint8_t *scratch() { return m_scratch + m_scratchLen; }
  unsigned scratchAvail() const { return sizeof(m_scratch) - m_scratchLen; }
  bool ack(unsigned level) const {
    return m_ackLevel == level && m_ackLevel < 3;
  }
  void markAck(unsigned level) { m_ackLevel = level; }

  void reset() {
    m_plain.reset();
    m_scratchLen = 0;
    m_ackLevel = 3;
  }

  bool commitScratch(unsigned len) {
    if (len > scratchAvail()) return false;
    unsigned off = m_scratchLen;
    m_scratchLen += len;
    return m_plain.add(pktBuildSpan(m_scratch + off, len));
  }

  bool add(ZuCSpan span) { return m_plain.add(span); }
  bool add(const TxRange &range) {
    if (!range.length) return true;
    if (!range.buf || range.offset + range.length > range.buf->size)
      return false;
    return m_plain.add(
      pktBuildSpan(range.buf->data_() + range.offset, range.length));
  }

  bool pad(unsigned len) {
    if (!len) return true;
    if (len > BufSize) return false;
    return m_plain.add(pktBuildSpan(PktBuildZeroPad, len));
  }

  bool padTo(unsigned bytes) {
    unsigned n = m_plain.bytes();
    return bytes <= n || pad(bytes - n);
  }

  bool padForProtSample(
    unsigned pnOffset, unsigned pnLength, unsigned tagLen)
  {
    unsigned headerLen = pnOffset + pnLength;
    unsigned minPktLen = pnOffset + 4 + 16;
    unsigned minPayloadLen = minPktLen > headerLen + tagLen ?
      minPktLen - headerLen - tagLen : 0;
    return padTo(minPayloadLen);
  }

private:
  PlainVec		m_plain;
  uint8_t		m_scratch[PktBuildScratchSize];
  unsigned		m_scratchLen = 0;
  unsigned		m_ackLevel = 3;
};

} // namespace Zquic

#endif /* ZquicPkt_HH */
