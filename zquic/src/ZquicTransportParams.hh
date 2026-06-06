//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport parameters

#ifndef ZquicTransportParams_HH
#define ZquicTransportParams_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZquicPacket.hh>

namespace Zquic {

static constexpr uint16_t TLSExtQUICTransportParamsV1 = 0x39;

struct TransportParams {
  ConnectionID	originalDCID;
  ConnectionID	initialSCID;
  ConnectionID	retrySCID;
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
  uint64_t	activeConnectionIDLimit = 2;
  bool		disableActiveMigration = true;

  bool validate() const;
};

struct TransportParamsCodec {
  static int encode(uint8_t *, unsigned, const TransportParams &);
  static int decode(ZuCSpan, TransportParams &);
};

} // namespace Zquic

#endif /* ZquicTransportParams_HH */
