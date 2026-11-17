//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic {

enum { TLSExtQUICParamsV1 = 0x39 };

struct ZquicAPI PreferredAddress {
  ZiSockAddr ipv4;
  ZiSockAddr ipv6;
  CxnID cid;
  ResetToken resetToken;

  bool operator !() const { return !cid; }
  ZuOpBool
  bool validate() const;
  // RFC 9000 section 18.2: two addresses and ports, CID length, reset token.
  enum { FixedLength = 41 };
  unsigned encodedLength() const { return FixedLength + cid.length(); }
  int encode(uint8_t *, unsigned) const;
  int decode(ZuBSpan);
  ZiSockAddr select(const ZiIP &) const;
};

struct ZquicAPI TransportParams {
  CxnID		origDCID;
  CxnID		initialSCID;
  CxnID		retrySCID;
  PreferredAddress preferredAddress;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxUDPPayloadSize = MinUDPPayload;
  uint64_t	initialMaxData = 0;
  uint64_t	initMaxStreamDataDuplexLcl = 0;
  uint64_t	initMaxStreamDataDuplexRmt = 0;
  uint64_t	initialMaxStreamDataUni = 0;
  uint64_t	initMaxStreamsDuplex = 0;
  uint64_t	initialMaxStreamsUni = 0;
  uint64_t	ackDelayExponent = 3;
  uint64_t	maxAckDelay = 25;
  uint64_t	activeCxnIDLimit = 2;
  ResetToken	statelessResetToken;
  bool		statelessResetTokenPresent = false;
  bool		disableActiveMigration = false;

  bool validate() const;
  unsigned encodedLength() const;
  int encode(uint8_t *, unsigned) const;
  int decode(ZuBSpan);
};

ZquicAPI ZeroRTTReason::T validateZeroRTTParams(
  const TransportParams &remembered, const TransportParams &current);

} // namespace Zquic
