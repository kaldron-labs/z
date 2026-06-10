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
  CxnID	dcid;
  CxnID	scid;
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

} // namespace Zquic

#endif /* ZquicPacket_HH */
