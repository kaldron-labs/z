//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC frame codec

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>

namespace Zutcp {

struct AckRange {
  uint64_t	largest = 0;
  uint64_t	first = 0;
};

struct AckECN {
  uint64_t	ect0 = 0;
  uint64_t	ect1 = 0;
  uint64_t	ce = 0;

  bool any() const { return ect0 || ect1 || ce; }
  void reset() { ect0 = ect1 = ce = 0; }
  bool operator ==(const AckECN &o) const {
    return ect0 == o.ect0 && ect1 == o.ect1 && ce == o.ce;
  }
};

struct Frame {
  static constexpr unsigned MaxAckRanges = 64;
  static constexpr unsigned BuiltinAckRanges = 32;
  using AckRanges = ZtBuiltin<
    ZtArray<AckRange,
      ZtArrayHeapMax<MaxAckRanges,
	ZtArrayHeapID<"Zutcp.Frame.AckRanges">>>,
    BuiltinAckRanges>;

  FrameType::T		type = FrameType::Unknown;
  uint64_t		offset = 0;
  uint64_t		length = 0;
  uint64_t		value = 0;
  uint64_t		byteNext = U64Null;
  uint64_t		errorCode = 0;
  bool			fin = false;
  ZuBSpan		payload;
  AckRanges		ackRanges;
  AckECN		ackECN;

  void reset() {
    type = FrameType::Unknown;
    offset = 0;
    length = 0;
    value = 0;
    byteNext = U64Null;
    errorCode = 0;
    fin = false;
    payload = {};
    ackRanges = {};
    ackECN.reset();
  }
};

struct ZutcpAPI FrameCodec {
  static bool ackEliciting(FrameType::T);
  static int parse(ZuBSpan, Frame &, unsigned &);
  static int writePadding(uint8_t *, unsigned, unsigned);
  static int writePing(uint8_t *, unsigned);
  static int writeData(
    uint8_t *, unsigned, uint64_t, ZuBSpan, bool);
  static int writeDataPrefix(
    uint8_t *, unsigned, uint64_t, unsigned, bool);
  static int writeAck(uint8_t *, unsigned, uint64_t, uint64_t, uint64_t);
  static int writeAckRanges(
    uint8_t *, unsigned, const AckRange *, unsigned, uint64_t);
  static int writeAckECN(
    uint8_t *, unsigned, const AckRange *, unsigned, uint64_t, const AckECN &);
  static int writeDataAck(
    uint8_t *, unsigned, uint64_t, const AckRange *, unsigned, const AckECN & = {});
  static int writeReset(uint8_t *, unsigned, uint64_t, uint64_t);
  static int writeStopSending(uint8_t *, unsigned, uint64_t);
  static int writeMaxData(uint8_t *, unsigned, uint64_t);
  static int writeDataBlocked(uint8_t *, unsigned, uint64_t);
  static int writeConnectionClose(uint8_t *, unsigned, uint64_t);
  static int writeApplicationClose(uint8_t *, unsigned, uint64_t, ZuBSpan = {});
};

} // namespace Zutcp
