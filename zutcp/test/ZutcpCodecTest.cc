//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

static ZuBSpan bytes_(const uint8_t *p, unsigned n)
{
  return ZuBSpan{p, n};
}

static ZuBSpan bspan_(const uint8_t *p, unsigned n)
{
  return ZuBSpan{p, n};
}

static int putParam(uint8_t *out, unsigned len, uint64_t id, ZuBSpan value)
{
  unsigned o = 0;
  if (Zutcp::VarInt::put(out, len, id, o) < 0 ||
      Zutcp::VarInt::put(out, len, value.length(), o) < 0 ||
      len < o + value.length())
    return -1;
  memcpy(out + o, value.data(), value.length());
  return int(o + value.length());
}

static int putParamVar(
  uint8_t *out, unsigned len, uint64_t id, uint64_t value)
{
  uint8_t v[8];
  int n = Zutcp::VarInt::encode(v, sizeof(v), value);
  if (n < 0) return -1;
  return putParam(out, len, id, bytes_(v, unsigned(n)));
}

void testVarIntAndPkt()
{
  ZuTestScope(testVarIntAndPkt);

  uint8_t b[256];
  uint64_t v = 0;
  unsigned n = 0;
  ZuCHECK(Zutcp::VarInt::encode(b, sizeof(b), 63) == 1, "varint 1 encode");
  ZuCHECK(!Zutcp::VarInt::decode(ZuBSpan{b, 1}, v, n),
    "varint 1 decode failed");
  ZuCHECK(v == 63 && n == 1, "varint 1 decode mismatch");
  ZuCHECK(Zutcp::VarInt::encode(b, sizeof(b), 16383) == 2, "varint 2 encode");
  ZuCHECK(!Zutcp::VarInt::decode(ZuBSpan{b, 2}, v, n),
    "varint 2 decode failed");
  ZuCHECK(v == 16383 && n == 2, "varint 2 decode mismatch");
  unsigned badOffset = 2;
  ZuCHECK(Zutcp::VarInt::put(b, 1, 1, badOffset) < 0 && badOffset == 2,
    "varint put accepted an out-of-range offset");

  Zutcp::CxnID dcid{"abcdefgh"};
  Zutcp::CxnID scid{"server01"};
  int l = Zutcp::Pkt::writeInitial(b, sizeof(b), dcid, scid, 16, 2);
  ZuCHECK(l > 0, "Initial header write failed");
  Zutcp::LongHdr h;
  ZuCHECK(Zutcp::Pkt::parseLong(
    ZuBSpan{b, unsigned(l + 2)}, h) > 0,
    "Initial header parse failed");
  ZuCHECK(h.type == Zutcp::PktType::Initial, "Initial type mismatch");
  ZuCHECK(h.dcid == dcid && h.scid == scid, "CID parse mismatch");
  uint32_t versions[] = { Zutcp::Version1 };
  l = Zutcp::Pkt::writeVerNeg(
    b, sizeof(b), dcid, scid, versions, 1);
  ZuCHECK(l > 0, "VN write failed");
  ZuCHECK(Zutcp::Pkt::isVerNeg(
    ZuBSpan{b, unsigned(l)}),
    "VN recognition failed");
  unsigned nVersions = 0;
  uint32_t parsedVersions[2] = {};
  ZuCHECK(!Zutcp::Pkt::parseVerNeg(
    ZuBSpan{b, unsigned(l)},
    parsedVersions, 2, nVersions), "VN parse failed");
  ZuCHECK(nVersions == 1 && parsedVersions[0] == Zutcp::Version1,
    "VN version list mismatch");

  l = Zutcp::Pkt::writeShort(b, sizeof(b), dcid, 0x1234, 2);
  ZuCHECK(l > 0, "short header write failed");
  Zutcp::ShortHdr sh;
  ZuCHECK(Zutcp::Pkt::parseShort(
    ZuBSpan{b, unsigned(l)}, dcid.length(), sh) > 0 &&
    sh.dcid == dcid && sh.pnLength == 2,
    "short header parse failed");
}



void testVarIntAndPktNumBounds()
{
  ZuTestScopeRT(testVarIntAndPktNumBounds);

  struct Case {
    uint64_t	value;
    unsigned	length;
  } cases[] = {
    {0, 1}, {63, 1}, {64, 2}, {16383, 2},
    {16384, 4}, {(1ULL<<30) - 1, 4},
    {(1ULL<<30), 8}, {(1ULL<<62) - 1, 8}
  };

  uint8_t b[16];
  for (auto c : cases) {
    memset(b, 0, sizeof(b));
    ZUTCP_CHECK_RT(Zutcp::VarInt::length(c.value) == c.length,
      "varint boundary length mismatch");
    int n = Zutcp::VarInt::encode(b, sizeof(b), c.value);
    ZUTCP_CHECK_RT(n == int(c.length), "varint boundary encode length mismatch");
    uint64_t v = 0;
    unsigned used = 0;
    ZUTCP_CHECK_RT(!Zutcp::VarInt::decode(bytes_(b, c.length), v, used) &&
	v == c.value && used == c.length,
      "varint boundary decode mismatch");
    if (c.length > 1)
      ZUTCP_CHECK_RT(Zutcp::VarInt::decode(bytes_(b, c.length - 1), v, used) < 0,
	"truncated varint was accepted");
  }
  uint64_t v = 0;
  unsigned used = 0;
  ZUTCP_CHECK_RT(Zutcp::VarInt::decode({}, v, used) < 0,
    "empty varint was accepted");
  ZUTCP_CHECK_RT(!Zutcp::VarInt::length(1ULL<<62) &&
      Zutcp::VarInt::encode(b, sizeof(b), 1ULL<<62) < 0,
    "oversize varint was accepted");
  unsigned o = 1;
  b[0] = 0xaa;
  ZUTCP_CHECK_RT(!Zutcp::VarInt::put(b, sizeof(b), 16384, o) && o == 5 &&
      b[0] == 0xaa,
    "varint put offset handling mismatch");

  ZUTCP_CHECK_RT(Zutcp::PktNumber::encodedLength(1, 0) == 1 &&
      Zutcp::PktNumber::encodedLength(1ULL<<7, 0) == 2 &&
      Zutcp::PktNumber::encodedLength(1ULL<<15, 0) == 3 &&
      Zutcp::PktNumber::encodedLength(1ULL<<23, 0) == 4,
    "packet number encoded length boundaries mismatch");
  ZUTCP_CHECK_RT(Zutcp::PktNumber::encode(b, sizeof(b), 0x123456U, 3) == 3 &&
      b[0] == 0x12 && b[1] == 0x34 && b[2] == 0x56,
    "packet number encode mismatch");
  ZUTCP_CHECK_RT(Zutcp::PktNumber::encode(b, sizeof(b), 1, 0) < 0 &&
      Zutcp::PktNumber::encode(b, sizeof(b), 1, 5) < 0 &&
      Zutcp::PktNumber::encode(b, 2, 1, 3) < 0,
    "invalid packet number encode accepted");
  ZUTCP_CHECK_RT(Zutcp::PktNumber::decode(0xa82f30eaULL, 0x9b32, 16) ==
      0xa82f9b32ULL,
    "packet number decode near expected mismatch");
  ZUTCP_CHECK_RT(Zutcp::PktNumber::decode(0xff, 0x00, 8) == 0x100,
    "packet number decode forward wrap mismatch");
  ZUTCP_CHECK_RT(Zutcp::PktNumber::decode(0x100, 0xff, 8) == 0xff,
    "packet number decode backward wrap mismatch");
}

void testPktParserRejections()
{
  ZuTestScope(testPktParserRejections);

  uint8_t b[128] = {};
  Zutcp::LongHdr h;
  ZuCHECK(Zutcp::Pkt::parseLong({}, h) < 0,
    "empty long header was accepted");
  b[0] = 0x40;
  ZuCHECK(Zutcp::Pkt::parseLong(bytes_(b, 8), h) < 0,
    "short-header packet parsed as long");

  b[0] = 0xc0;
  b[4] = 1;
  b[5] = Zutcp::CxnIDMax + 1;
  ZuCHECK(Zutcp::Pkt::parseLong(bytes_(b, 7), h) < 0,
    "overlong DCID length was accepted");

  Zutcp::CxnID dcid{"abcdefgh"};
  Zutcp::CxnID scid{"server01"};
  ZuCHECK(Zutcp::Pkt::writeInitial(b, sizeof(b), dcid, scid, 0, 0) < 0 &&
      Zutcp::Pkt::writeShort(b, sizeof(b), dcid, 1, 5) < 0,
    "invalid packet number length accepted");
  int l = Zutcp::Pkt::writeInitial(b, sizeof(b), dcid, scid, 4, 1);
  ZuCHECK(l > 0 && Zutcp::Pkt::parseLong(bytes_(b, unsigned(l)), h) < 0,
    "Initial without its packet number was accepted");
  unsigned lengthOffset = 7 + dcid.length() + scid.length();
  b[lengthOffset] = 0x40;
  ZuCHECK(Zutcp::Pkt::parseLong(bytes_(b, lengthOffset + 1), h) < 0,
    "truncated Initial payload-length VarInt was accepted");

  uint32_t versions[] = { Zutcp::Version1 };
  int n = Zutcp::Pkt::writeVerNeg(
    b, sizeof(b), dcid, scid, versions, 1);
  ZuCHECK(n > 0, "VN setup failed");
  unsigned nVersions = 0;
  uint32_t parsed[1];
  ZuCHECK(Zutcp::Pkt::parseVerNeg(
      bytes_(b, unsigned(n - 1)), parsed, 1, nVersions) < 0,
    "malformed VN version list was accepted");
  ZuCHECK(Zutcp::Pkt::parseVerNeg(
      bytes_(b, unsigned(n)), parsed, 0, nVersions) < 0,
    "VN parse ignored version capacity");

  Zutcp::ShortHdr sh;
  ZuCHECK(Zutcp::Pkt::parseShort(bytes_(b, 4), dcid.length(), sh) < 0,
    "too-short short header was accepted");
  b[0] = 0xc0;
  ZuCHECK(Zutcp::Pkt::parseShort(bytes_(b, sizeof(b)), dcid.length(), sh) < 0,
    "long-header packet parsed as short");
  b[0] = 0x40;
  ZuCHECK(Zutcp::Pkt::parseShort(
      bytes_(b, sizeof(b)), Zutcp::CxnIDMax + 1, sh) < 0,
    "overlong short-header CID length was accepted");
}

void testConnectionWire()
{
  ZuTestScope(testConnectionWire);
  uint8_t out[64];
  const uint8_t data[] = {0x0f, 0x03, 0x05, 'h', 'e', 'l', 'l', 'o'};
  int n = Zutcp::FrameCodec::writeData(out, sizeof(out), 3, "hello", true);
  ZuCHECK(n == sizeof(data) && !memcmp(out, data, sizeof(data)),
    "data wire must contain offset/length/payload without a stream identifier");
  const uint8_t reset[] = {0x04, 0x07, 0x40, 0x63};
  n = Zutcp::FrameCodec::writeReset(out, sizeof(out), 7, 99);
  ZuCHECK(n == sizeof(reset) && !memcmp(out, reset, sizeof(reset)),
    "reset wire must contain only error and final byte offset");
  const uint8_t stop[] = {0x05, 0x40, 0x63};
  n = Zutcp::FrameCodec::writeStopSending(out, sizeof(out), 99);
  ZuCHECK(n == sizeof(stop) && !memcmp(out, stop, sizeof(stop)),
    "stop wire must contain only the error code");
  Zutcp::Frame frame;
  unsigned used = 0;
  bool truncated = true;
  for (unsigned i = 0; i < sizeof(data); ++i)
    truncated &= Zutcp::FrameCodec::parse({data, i}, frame, used) < 0;
  ZuCHECK(truncated && !Zutcp::FrameCodec::parse(data, frame, used) &&
    used == sizeof(data) && frame.type == Zutcp::FrameType::Data &&
    frame.offset == 3 && frame.length == 5 && frame.payload == "hello" && frame.fin,
    "independent data vector must parse only within its complete extent");
}

void testFramesAndParams()
{
  ZuTestScopeRT(testFramesAndParams);

  uint8_t b[256];
  int l = Zutcp::FrameCodec::writeData(
    b, sizeof(b), 3, "hello", true);
  ZUTCP_CHECK_RT(l > 0, "DATA write failed");
  Zutcp::Frame f;
  unsigned used = 0;
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(l)}, f, used),
    "DATA parse failed");
  ZUTCP_CHECK_RT(f.type == Zutcp::FrameType::Data, "DATA type mismatch");
  ZUTCP_CHECK_RT(f.offset == 3 && f.length == 5 && f.fin,
    "DATA fields mismatch");

  Zutcp::TransportParams p;
  p.maxPayloadSize = 1400;
  p.initialMaxData = 1000;
  p.initialSCID = "server01";
  l = p.encode(b, sizeof(b));
  ZUTCP_CHECK_RT(l > 0, "transport parameter encode failed");
  Zutcp::TransportParams q;
  ZUTCP_CHECK_RT(!q.decode(ZuBSpan{b, unsigned(l)}),
    "transport parameter decode failed");
  ZUTCP_CHECK_RT(q.maxPayloadSize == 1400 && q.initialMaxData == 1000,
    "transport parameter values mismatch");
  ZUTCP_CHECK_RT(q.initialSCID == p.initialSCID, "transport parameter CID mismatch");

  l = Zutcp::FrameCodec::writeAck(b, sizeof(b), 10, 0, 3);
  ZUTCP_CHECK_RT(l > 0, "ACK write failed");
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(l)}, f, used) &&
    f.type == Zutcp::FrameType::Ack && f.offset == 10 && f.length == 3 &&
    f.ackRanges.length() == 1 &&
    f.ackRanges[0].first == 7 &&
    f.ackRanges[0].largest == 10,
    "ACK parse mismatch");

  l = Zutcp::FrameCodec::writeReset(b, sizeof(b), 7, 99);
  ZUTCP_CHECK_RT(l > 0, "RESET_DATA write failed");
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(l)}, f, used) &&
    f.type == Zutcp::FrameType::Reset &&
    f.errorCode == 7 && f.length == 99,
    "RESET_DATA parse mismatch");





  l = Zutcp::FrameCodec::writeConnectionClose(
    b, sizeof(b), Zutcp::TransportError::FrameEncoding);
  ZUTCP_CHECK_RT(l > 0, "CONNECTION_CLOSE write failed");
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
    ZuBSpan{b, unsigned(l)}, f, used) &&
    f.type == Zutcp::FrameType::ConnectionClose &&
    f.errorCode == Zutcp::TransportError::FrameEncoding &&
    !f.value && !f.length && used == unsigned(l),
    "CONNECTION_CLOSE parse mismatch");

  unsigned o = 0;
  b[o++] = 0x03;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 10) == 1,
    "ACK ECN largest encode failed"); o += 1;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 0) == 1,
    "ACK ECN delay encode failed"); o += 1;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN range count encode failed"); o += 1;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 2) == 1,
    "ACK ECN first range encode failed"); o += 1;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN gap encode failed"); o += 1;
  ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN range encode failed"); o += 1;
  for (unsigned i = 0; i < 3; ++i) {
    ZUTCP_CHECK_RT(Zutcp::VarInt::encode(b + o, sizeof(b) - o, 0) == 1,
      "ACK ECN count encode failed");
    ++o;
  }
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
    ZuBSpan{b, o}, f, used) &&
    f.type == Zutcp::FrameType::Ack && f.offset == 10 &&
    f.length == 2 && used == o &&
    f.ackRanges.length() == 2 &&
    f.ackRanges[0].first == 4 &&
    f.ackRanges[0].largest == 5 &&
    f.ackRanges[1].first == 8 &&
    f.ackRanges[1].largest == 10,
    "ACK ECN/range parse mismatch");

  Zutcp::AckRange ecnRanges[] = {
    {7, 5}, {12, 10}
  };
  Zutcp::AckECN ecn;
  ecn.ect0 = 5;
  ecn.ect1 = 3;
  ecn.ce = 2;
  l = Zutcp::FrameCodec::writeAckECN(
    b, sizeof(b), ecnRanges, 2, 9, ecn);
  ZUTCP_CHECK_RT(l > 0 && b[0] == 0x03, "ACK_ECN write failed");
  f.reset();
  ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse(
      ZuBSpan{b, unsigned(l)}, f, used) &&
      used == unsigned(l) &&
      f.type == Zutcp::FrameType::Ack &&
      f.value == 9 &&
      f.ackECN == ecn &&
      f.ackRanges.length() == 2 &&
      f.ackRanges[0].first == 5 &&
      f.ackRanges[0].largest == 7 &&
      f.ackRanges[1].first == 10 &&
      f.ackRanges[1].largest == 12,
    "ACK_ECN write/parse counters mismatch");
  f.reset();
  ZUTCP_CHECK_RT(!f.ackECN.any(), "Frame reset retained ACK_ECN counters");

  for (uint8_t removed : {uint8_t(0x06), uint8_t(0x07), uint8_t(0x18), uint8_t(0x19),
      uint8_t(0x1a), uint8_t(0x1b), uint8_t(0x1e)}) {
    b[0] = removed;
    ZUTCP_CHECK_RT(!Zutcp::FrameCodec::parse({b, 1}, f, used) &&
        f.type == Zutcp::FrameType::Unknown && used == 1,
      "deleted setup, CID and path controls must be unknown");
  }

}

void testControlFrameCoverage()
{
  ZuTestScope(testControlFrameCoverage);

  // QUIC v1 base frame matrix covered here:
  // 0x00 PADDING, 0x01 PING, 0x02/0x03 ACK, 0x04 RESET_DATA,
  // 0x05 STOP_SENDING, 0x08..0x0f DATA,
  // 0x10 MAX_DATA, 0x11 MAX_DATA_DATA, 0x12/0x13 MAX_DATAS,
  // 0x14 DATA_BLOCKED, 0x15 DATA_DATA_BLOCKED,
  // 0x16/0x17 DATAS_BLOCKED, 0x18 NEW_CONNECTION_ID,
  // 0x19 RETIRE_CONNECTION_ID, 0x1a PATH_CHALLENGE,
  // 0x1b PATH_RESPONSE, 0x1c CONNECTION_CLOSE,
  // 0x1d CONNECTION_CLOSE_APP, 0x1e HANDSHAKE_DONE.
  // DATAGRAM is intentionally unsupported unless negotiated by extension.

  uint8_t b[256];
  Zutcp::Frame f;
  unsigned used = 0;

  int n = Zutcp::FrameCodec::writePadding(b, sizeof(b), 3);
  ZuCHECK(n == 3 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::Padding && used == 3 &&
      !Zutcp::FrameCodec::ackEliciting(f.type),
    "PADDING coverage mismatch");

  n = Zutcp::FrameCodec::writePing(b, sizeof(b));
  ZuCHECK(n == 1 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::Ping &&
      Zutcp::FrameCodec::ackEliciting(f.type),
    "PING coverage mismatch");

  unsigned o = 0;
  b[o++] = 0x08;
  memcpy(b + o, "abc", 3); o += 3;
  ZuCHECK(!Zutcp::FrameCodec::parse(bytes_(b, o), f, used) &&
      f.type == Zutcp::FrameType::Data &&
      f.length == 3 && f.payload == "abc" && !f.fin,
    "DATA no-length parse mismatch");

  n = Zutcp::FrameCodec::writeStopSending(b, sizeof(b), 99);
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::StopSending &&
      f.errorCode == 99,
    "STOP_SENDING coverage mismatch");

  n = Zutcp::FrameCodec::writeMaxData(b, sizeof(b), 1ULL<<30);
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::MaxData && f.value == (1ULL<<30),
    "MAX_DATA coverage mismatch");



  n = Zutcp::FrameCodec::writeDataBlocked(b, sizeof(b), 4096);
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::DataBlocked && f.value == 4096,
    "DATA_BLOCKED coverage mismatch");


  Zutcp::AckRange ranges[] = {
    {7, 5}, {11, 10}, {15, 15}
  };
  n = Zutcp::FrameCodec::writeAckRanges(b, sizeof(b), ranges, 3, 4);
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::Ack && f.value == 4 &&
      f.ackRanges.length() == 3 &&
      f.ackRanges[0].first == 5 && f.ackRanges[0].largest == 7 &&
      f.ackRanges[1].first == 10 && f.ackRanges[1].largest == 11 &&
      f.ackRanges[2].first == 15 && f.ackRanges[2].largest == 15,
    "ACK range write/parse coverage mismatch");

  n = Zutcp::FrameCodec::writeApplicationClose(b, sizeof(b), 42, "fail");
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zutcp::FrameType::ApplicationClose &&
      f.errorCode == 42 && f.length == 4 && f.payload == "fail",
    "application CONNECTION_CLOSE parse mismatch");

  b[0] = 0x30;
  ZuCHECK(!Zutcp::FrameCodec::parse(bytes_(b, 1), f, used) &&
      f.type == Zutcp::FrameType::Unknown && used == 1,
    "unsupported DATAGRAM frame parse mismatch");

  b[0] = 0x40;
  ZuCHECK(!Zutcp::FrameCodec::parse(bytes_(b, 1), f, used) &&
      f.type == Zutcp::FrameType::Unknown && used == 1,
    "unknown frame parse mismatch");
}



void testMalformedFrameCoverage()
{
  ZuTestScope(testMalformedFrameCoverage);

  uint8_t b[256];
  Zutcp::Frame f;
  unsigned used = 0;

  ZuCHECK(Zutcp::FrameCodec::writePing(b, 0) < 0 &&
      Zutcp::FrameCodec::writePadding(b, 2, 3) < 0 &&
      Zutcp::FrameCodec::writeData(b, 3, 0, "abcd", false) < 0,
    "malformed frame write accepted");

  ZuCHECK(Zutcp::FrameCodec::writeAck(b, sizeof(b), 5, 0, 6) < 0,
    "invalid ACK first range accepted");
  Zutcp::AckRange badRanges[] = {
    {10, 8}, {11, 11}
  };
  ZuCHECK(Zutcp::FrameCodec::writeAckRanges(
      b, sizeof(b), badRanges, 2, 0) < 0,
    "overlapping ACK ranges accepted");

  unsigned o = 0;
  b[o++] = 0x02;
  ZuCHECK(!Zutcp::VarInt::put(b, sizeof(b), 3, o) &&
      !Zutcp::VarInt::put(b, sizeof(b), 0, o) &&
      !Zutcp::VarInt::put(b, sizeof(b), 0, o) &&
      !Zutcp::VarInt::put(b, sizeof(b), 4, o),
    "invalid ACK setup failed");
  ZuCHECK(Zutcp::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "ACK with first range larger than largest accepted");

  o = 0;
  b[o++] = 0x06;
  ZuCHECK(!Zutcp::VarInt::put(b, sizeof(b), 0, o) &&
      !Zutcp::VarInt::put(b, sizeof(b), 3, o),
    "excluded frame setup failed");
  b[o++] = 'x';
  ZuCHECK(!Zutcp::FrameCodec::parse(bytes_(b, o), f, used) &&
      f.type == Zutcp::FrameType::Unknown,
    "removed crypto frame must be classified as unknown");

  o = 0;
  b[o++] = 0x0a;
  ZuCHECK(!Zutcp::VarInt::put(b, sizeof(b), 5, o),
    "truncated DATA setup failed");
  memcpy(b + o, "abc", 3); o += 3;
  ZuCHECK(Zutcp::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "truncated DATA payload accepted");


}

void testTransportParamCoverage()
{
  ZuTestScope(testTransportParamCoverage);

  Zutcp::TransportParams connection;
  connection.initialMaxData = 16384;
  uint8_t wire[64];
  static constexpr uint8_t expected[] = {
    1,1,0, 3,2,0x44,0xb0, 4,4,0x80,0,0x40,0,
    0x0a,1,3, 0x0b,1,25
  };
  int wireLen = connection.encode(wire, sizeof(wire));
  ZuCHECK(wireLen == sizeof(expected) && connection.encodedLength() == sizeof(expected) &&
      !memcmp(wire, expected, sizeof(expected)),
    "independent parameter vector contains one connection byte limit and no stream limits");

  uint8_t b[512];
  Zutcp::TransportParams p;
  p.origDCID = "orig-dcid";
  p.initialSCID = "init-scid";
  p.maxIdleTimeout = 16363;
  p.maxPayloadSize = Zutcp::BufSize;
  p.initialMaxData = 1000000009;

  p.ackDelayExponent = 20;
  p.maxAckDelay = 63;
  int n = p.encode(b, sizeof(b));
  ZuCHECK(n > 0, "full transport parameter encode failed");

  Zutcp::TransportParams q;
  ZuCHECK(!q.decode(bytes_(b, unsigned(n))),
    "full transport parameter decode failed");
  ZuCHECK(q.origDCID == p.origDCID &&
      q.initialSCID == p.initialSCID &&
      q.maxIdleTimeout == p.maxIdleTimeout &&
      q.maxPayloadSize == p.maxPayloadSize &&
      q.initialMaxData == p.initialMaxData &&

      q.ackDelayExponent == p.ackDelayExponent &&
      q.maxAckDelay == p.maxAckDelay,
    "full transport parameter round-trip mismatch");

  n = putParam(b, sizeof(b), 0x3f, "abc");
  ZuCHECK(n > 0 &&
      !q.decode(bytes_(b, unsigned(n))),
    "unknown transport parameter was not ignored");

  n = putParamVar(b, sizeof(b), 0x03, Zutcp::MinPktPayload - 1);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "undersized max_payload_size accepted");
  n = putParamVar(b, sizeof(b), 0x0a, 21);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "oversized ack_delay_exponent accepted");
  n = putParamVar(b, sizeof(b), 0x0b, 1ULL<<14);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "oversized max_ack_delay accepted");
  n = putParam(b, sizeof(b), 0x02, "removed-parameter");
  ZuCHECK(n > 0 && !q.decode(bytes_(b, unsigned(n))),
    "removed reset-token parameter must follow unknown-parameter handling");

  uint8_t value[32];
  memset(value, 'a', sizeof(value));
  n = putParam(b, sizeof(b), 0x0f,
    bytes_(value, Zutcp::MinCIDLength - 1));
  ZuCHECK(n > 0 &&
      !q.decode(bytes_(b, unsigned(n))) &&
      q.initialSCID.length() == Zutcp::MinCIDLength - 1,
    "short peer initial_source_connection_id was rejected");
  n = putParam(b, sizeof(b), 0x0f,
    bytes_(value, Zutcp::CxnIDMax + 1));
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "overlong initial_source_connection_id accepted");

  unsigned o = 0;
  ZuCHECK(!Zutcp::VarInt::put(b, sizeof(b), 0x03, o) &&
      !Zutcp::VarInt::put(b, sizeof(b), 2, o),
    "malformed transport varint setup failed");
  b[o++] = Zutcp::MinPktPayload & 0x3fU;
  b[o++] = 0;
  ZuCHECK(q.decode(bytes_(b, o)) < 0,
    "transport parameter varint with trailing byte accepted");

  n = p.encode(b, sizeof(b));
  ZuCHECK(n > 1 &&
      q.decode(bytes_(b, unsigned(n - 1))) < 0,
    "truncated transport parameter accepted");
}





void dataAckEnvelope()
{
  ZuTestScope(dataAckEnvelope);
  uint8_t bytes[128];
  Zutcp::AckRange ranges[] = {{4, 0}, {19, 17}};
  Zutcp::AckECN ecn{1, 2, 3};
  uint64_t next = (uint64_t(1)<<32) + 7;
  int n = Zutcp::FrameCodec::writeDataAck(bytes, sizeof(bytes), next, ranges, 2, ecn);
  Zutcp::Frame frame;
  unsigned used;
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse({bytes, unsigned(n)}, frame, used) &&
      used == unsigned(n) && frame.type == Zutcp::FrameType::Ack && frame.byteNext == next &&
      !frame.value && frame.ackRanges.length() == 2 && frame.ackRanges[0].first == 0 &&
      frame.ackRanges[1].largest == 19 && frame.ackECN == ecn &&
      !Zutcp::FrameCodec::ackEliciting(frame.type),
    "DATA ACK carries cumulative byte evidence plus retained control packet ACK/ECN fields");
  bool truncated = true;
  for (unsigned i = 0; i < unsigned(n); ++i)
    truncated &= Zutcp::FrameCodec::parse({bytes, i}, frame, used) < 0;
  ZuCHECK(truncated, "a truncated DATA ACK cannot manufacture byte progress");
  uint8_t nested[] = {0x1f, 0, 0x1f, 0, 0x01};
  ZuCHECK(Zutcp::FrameCodec::parse({nested, sizeof(nested)}, frame, used) < 0,
    "DATA ACK envelope wraps a packet ACK, without recursive or ACK-eliciting content");
  n = Zutcp::FrameCodec::writeAckRanges(bytes, sizeof(bytes), ranges, 2, 9);
  ZuCHECK(n > 0 && !Zutcp::FrameCodec::parse({bytes, unsigned(n)}, frame, used) &&
      ZuNull(frame.byteNext) && frame.value == 9,
    "bootstrap packet ACK has no DATA frontier and resets reused frame metadata");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVarIntAndPkt);
  ZuTestCall(testVarIntAndPktNumBounds);
  ZuTestCall(testPktParserRejections);
  ZuTestCall(testConnectionWire);
  ZuTestCall(testFramesAndParams);
  ZuTestCall(testControlFrameCoverage);
  ZuTestCall(testMalformedFrameCoverage);
  ZuTestCall(testTransportParamCoverage);
  ZuTestCall(dataAckEnvelope);
}
