//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif


namespace Zquic {

static constexpr uint16_t TLSExtQUICParamsV1 = 0x39;

struct TransportParams {
  CxnID		origDCID;
  CxnID		initialSCID;
  CxnID		retrySCID;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxUDPPayloadSize = MinUDPPayload;
  uint64_t	initialMaxData = 0;
  uint64_t	initialMaxStreamDataBidiLocal = 0;
  uint64_t	initialMaxStreamDataBidiRemote = 0;
  uint64_t	initialMaxStreamDataUni = 0;
  uint64_t	initialMaxStreamsBidi = 0;
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

ZeroRTTReason::T validateZeroRTTParams(
  const TransportParams &remembered, const TransportParams &current);

} // namespace Zquic
