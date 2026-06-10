//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC frame codec

#ifndef ZquicFrame_HH
#define ZquicFrame_HH

#ifndef ZquicLib_HH
#include <zlib/ZquicLib.hh>
#endif

#include <zlib/ZquicPacket.hh>

namespace Zquic {

struct AckRange {
  uint64_t	largest = 0;
  uint64_t	first = 0;
};

struct Frame {
  static constexpr unsigned MaxAckRanges = 64;

  FrameType::T	type = FrameType::Unknown;
  uint64_t	streamID = 0;
  uint64_t	offset = 0;
  uint64_t	length = 0;
  uint64_t	value = 0;
  uint64_t	errorCode = 0;
  StreamType::T	streamType = StreamType::Bidi;
  bool		fin = false;
  StatelessResetToken resetToken;
  ZuCSpan	payload;
  AckRange	ackRanges[MaxAckRanges];
  unsigned	ackRangeCount = 0;
};

struct FrameCodec {
  static bool ackEliciting(FrameType::T);
  static int parse(ZuCSpan, Frame &, unsigned &);
  static int writePadding(uint8_t *, unsigned, unsigned);
  static int writePing(uint8_t *, unsigned);
  static int writeCrypto(uint8_t *, unsigned, uint64_t, ZuCSpan);
  static int writeCryptoPrefix(uint8_t *, unsigned, uint64_t, unsigned);
  static int writeStream(
    uint8_t *, unsigned, uint64_t, uint64_t, ZuCSpan, bool);
  static int writeStreamPrefix(
    uint8_t *, unsigned, uint64_t, uint64_t, unsigned, bool);
  static int writeAck(uint8_t *, unsigned, uint64_t, uint64_t, uint64_t);
  static int writeAckRanges(
    uint8_t *, unsigned, const AckRange *, unsigned, uint64_t);
  static int writeResetStream(uint8_t *, unsigned, uint64_t, uint64_t, uint64_t);
  static int writeStopSending(uint8_t *, unsigned, uint64_t, uint64_t);
  static int writeMaxData(uint8_t *, unsigned, uint64_t);
  static int writeMaxStreamData(uint8_t *, unsigned, uint64_t, uint64_t);
  static int writeMaxStreams(uint8_t *, unsigned, StreamType::T, uint64_t);
  static int writeDataBlocked(uint8_t *, unsigned, uint64_t);
  static int writeStreamDataBlocked(uint8_t *, unsigned, uint64_t, uint64_t);
  static int writeStreamsBlocked(uint8_t *, unsigned, StreamType::T, uint64_t);
  static int writeNewConnectionID(
    uint8_t *, unsigned, uint64_t sequence, uint64_t retirePriorTo,
    const CxnID &, const StatelessResetToken &);
  static int writeRetireConnectionID(uint8_t *, unsigned, uint64_t sequence);
  static int writePathChallenge(uint8_t *, unsigned, ZuCSpan);
  static int writePathResponse(uint8_t *, unsigned, ZuCSpan);
  static int writeConnectionClose(uint8_t *, unsigned, uint64_t);
  static int writeHandshakeDone(uint8_t *, unsigned);
};

} // namespace Zquic

#endif /* ZquicFrame_HH */
