//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC packet codec

#ifndef ZquicPacket_HH
#define ZquicPacket_HH

#ifndef ZquicBuf_HH
#include <zlib/ZquicBuf.hh>
#endif

#include <string.h>

#include <zlib/ZuHash.hh>
#include <zlib/ZuSpan.hh>

namespace Zquic {

struct VarInt {
  static unsigned length(uint64_t);
  static int encode(uint8_t *, unsigned, uint64_t);
  static int put(uint8_t *, unsigned, uint64_t, unsigned &);
  static int decode(ZuCSpan, uint64_t &, unsigned &);
};

class ConnectionID {
public:
  static constexpr unsigned Max = 20;

  ConnectionID() = default;
  explicit ConnectionID(ZuCSpan s) { set(s); }

  bool set(ZuCSpan);
  unsigned length() const { return m_length; }
  const uint8_t *data() const { return m_data; }
  uint8_t *data() { return m_data; }
  ZuCSpan cspan() const {
    return ZuCSpan{reinterpret_cast<const char *>(m_data), m_length};
  }

  bool equals(const ConnectionID &) const;
  int cmp(const ConnectionID &) const;
  uint32_t hash() const { return ZuHash<ZuCSpan>::hash(cspan()); }

  friend inline bool operator ==(const ConnectionID &l, const ConnectionID &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ConnectionID &l, const ConnectionID &r) {
    return l.cmp(r);
  }

private:
  uint8_t	m_data[Max] = {};
  uint8_t	m_length = 0;
};

struct PacketNumber {
  static unsigned encodedLength(uint64_t pn, uint64_t largestAcked);
  static int encode(uint8_t *, unsigned, uint64_t pn, unsigned length);
  static uint64_t decode(uint64_t largestPN, uint64_t truncated, unsigned bits);
};

struct LongHeader {
  PacketType::T	type = PacketType::Initial;
  uint32_t	version = Version1;
  ConnectionID	dcid;
  ConnectionID	scid;
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
  ConnectionID	dcid;
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
    const ConnectionID &originalDCID);
  static bool validateRetryIntegrity(ZuCSpan, const ConnectionID &originalDCID);
  static int parseShort(ZuCSpan, unsigned cidLen, ShortHeader &);
  static int parseVersionNegotiation(
    ZuCSpan, uint32_t *, unsigned capacity, unsigned &nVersions);
  static int writeLong(
    uint8_t *, unsigned, PacketType::T,
    const ConnectionID &, const ConnectionID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeInitial(
    uint8_t *, unsigned, const ConnectionID &, const ConnectionID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeHandshake(
    uint8_t *, unsigned, const ConnectionID &, const ConnectionID &,
    uint64_t payloadLength, unsigned pnLength);
  static int writeRetry(
    uint8_t *, unsigned, const ConnectionID &, const ConnectionID &,
    ZuCSpan token, ZuCSpan retryIntegrityTag = {});
  static int writeRetryAuthenticated(
    uint8_t *, unsigned, const ConnectionID &, const ConnectionID &,
    ZuCSpan token, const ConnectionID &originalDCID);
  static int writeShort(
    uint8_t *, unsigned, const ConnectionID &, uint64_t pn, unsigned pnLength);
  static int writeVersionNegotiation(
    uint8_t *, unsigned, const ConnectionID &, const ConnectionID &,
    const uint32_t *, unsigned);
};

} // namespace Zquic

#endif /* ZquicPacket_HH */
