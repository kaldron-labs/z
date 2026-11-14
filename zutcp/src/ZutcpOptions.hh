//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Bounded TCP option values and standard option-byte codec.

#ifndef ZutcpOptions_HH
#define ZutcpOptions_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuSpan.hh>

namespace Zutcp {

struct SACKBlock {
  uint32_t	left = 0, right = 0;

  bool valid() const {
    uint32_t length = right - left;
    return length && length < uint32_t(0x80000000);
  }
};

struct TCPOptionState {
  enum { SACKSend = 16, SACKReceive = 32 };

  unsigned	flags = 0;
  uint16_t	peerMSS = 0;
  uint8_t	localScale = 0, peerScale = 0;
};

struct ZutcpAPI TCPOptions {
  enum { MaxBytes = 40, MaxSACKBlocks = 4, MaxScale = 14, MaxTFOCookie = 16 };
  enum { MSS = 1, WS = 2, SACKPermitted = 4, TS = 8, TFO = 64 };

  unsigned	flags = 0;
  uint16_t	mss = 0;
  uint8_t	scale = 0, sackCount = 0;
  uint32_t	tsval = 0, tsecr = 0;
  SACKBlock	sack[MaxSACKBlocks] = {};
  uint8_t	tfoLength = 0, tfoCookie[MaxTFOCookie] = {};

  // SYN-only offers are explicit; no MSS/default option policy is selected here.
  // TFO presence with length zero requests a cookie; otherwise it is opaque.
  // The result includes EOL/zero padding to a four-byte TCP header boundary.
  int encodedLength(bool syn) const;
  int encode(uint8_t *, unsigned capacity, bool syn) const;
  // Unknown well-formed options are skipped; EOL ends parsing. Failure leaves
  // this value unchanged. Non-SYN MSS/WS/SACK-permitted offers are ignored.
  // Invalid TFO cookie lengths and off-SYN TFO are ignored after framing.
  // SACK blocks on SYN are ignored. Block edges remain wire values; valid()
  // checks their bounded serial span, leaving receive policy to the caller.
  int decode(ZuBSpan, bool syn);
  // WS/TS need both SYN offers. SACKSend follows peer permission; SACKReceive
  // follows our offer. MSS presence remains directional even for advertised zero;
  // callers choose fallback and per-packet timestamp echo policy.
  TCPOptionState negotiate(const TCPOptions &peer) const;
  // Wire block order is significant: DSACK is the first block, never a new kind.
  bool dsack(uint32_t packetACK) const;
};

} // namespace Zutcp

#endif /* ZutcpOptions_HH */
