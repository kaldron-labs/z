//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

namespace Zutcp {

struct ZutcpAPI TransportParams {
  CxnID		origDCID;
  CxnID		initialSCID;
  uint64_t	maxIdleTimeout = 0;
  uint64_t	maxPayloadSize = MinPktPayload;
  uint64_t	initialMaxData = 0;
  uint64_t	ackDelayExponent = 3;
  uint64_t	maxAckDelay = 25;

  bool validate() const;
  unsigned encodedLength() const;
  int encode(uint8_t *, unsigned) const;
  int decode(ZuBSpan);
};

// Intermediate Initial wire format: ordinary Initial header and packet number,
// then a VarInt parameter-byte length, encoded parameters, and ordinary frames.
// Zero parameter length denotes an ACK/control-only Initial. There is no TLS
// record, CRYPTO stream, protected header or authentication-tag reservation.
struct ZutcpAPI InitialPkt {
  LongHdr	header;
  uint64_t	number = 0;
  ZuBSpan	params, frames;

  // Reserve the declared frame extent and return its offset. The caller writes
  // frames directly there; neither parameters nor frames need a staging buffer.
  static int write(
    uint8_t *, unsigned capacity, const CxnID &dcid, const CxnID &scid,
    uint64_t number, unsigned pnLength, const TransportParams *,
    unsigned frameLength);
  // Return this packet's consumed length, permitting a coalesced next packet.
  // Parameter decoding/installation follows only after these framing bounds.
  int parse(ZuBSpan, uint64_t largestPN = 0);
};


} // namespace Zutcp
