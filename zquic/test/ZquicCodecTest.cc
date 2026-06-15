//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicTransport.hh>

using namespace ZuTestUtil;

static ZuCSpan bytes_(const uint8_t *p, unsigned n)
{
  return ZuCSpan{p, n};
}

static int putParam_(uint8_t *out, unsigned len, uint64_t id, ZuCSpan value)
{
  unsigned o = 0;
  if (Zquic::VarInt::put(out, len, id, o) < 0 ||
      Zquic::VarInt::put(out, len, value.length(), o) < 0 ||
      len < o + value.length())
    return -1;
  memcpy(out + o, value.data(), value.length());
  return int(o + value.length());
}

static int putParamVar_(
  uint8_t *out, unsigned len, uint64_t id, uint64_t value)
{
  uint8_t v[8];
  int n = Zquic::VarInt::encode(v, sizeof(v), value);
  if (n < 0) return -1;
  return putParam_(out, len, id, bytes_(v, unsigned(n)));
}

void testVarIntAndPkt()
{
  ZuTestScope(testVarIntAndPkt);

  uint8_t b[256];
  uint64_t v = 0;
  unsigned n = 0;
  ZuCHECK(Zquic::VarInt::encode(b, sizeof(b), 63) == 1, "varint 1 encode");
  ZuCHECK(!Zquic::VarInt::decode(ZuCSpan{b, 1}, v, n),
    "varint 1 decode failed");
  ZuCHECK(v == 63 && n == 1, "varint 1 decode mismatch");
  ZuCHECK(Zquic::VarInt::encode(b, sizeof(b), 16383) == 2, "varint 2 encode");
  ZuCHECK(!Zquic::VarInt::decode(ZuCSpan{b, 2}, v, n),
    "varint 2 decode failed");
  ZuCHECK(v == 16383 && n == 2, "varint 2 decode mismatch");
  unsigned badOffset = 2;
  ZuCHECK(Zquic::VarInt::put(b, 1, 1, badOffset) < 0 && badOffset == 2,
    "varint put accepted an out-of-range offset");

  Zquic::CxnID dcid{"abcdefgh"};
  Zquic::CxnID scid{"server01"};
  int l = Zquic::Pkt::writeInitial(b, sizeof(b), dcid, scid, 16, 2);
  ZuCHECK(l > 0, "Initial header write failed");
  Zquic::LongHdr h;
  ZuCHECK(Zquic::Pkt::parseLong(
    ZuCSpan{b, unsigned(l + 2)}, h) > 0,
    "Initial header parse failed");
  ZuCHECK(h.type == Zquic::PktType::Initial, "Initial type mismatch");
  ZuCHECK(h.dcid == dcid && h.scid == scid, "CID parse mismatch");

  uint32_t versions[] = { Zquic::Version1 };
  l = Zquic::Pkt::writeVersionNegotiation(
    b, sizeof(b), dcid, scid, versions, 1);
  ZuCHECK(l > 0, "VN write failed");
  ZuCHECK(Zquic::Pkt::isVersionNegotiation(
    ZuCSpan{b, unsigned(l)}),
    "VN recognition failed");
  unsigned nVersions = 0;
  uint32_t parsedVersions[2] = {};
  ZuCHECK(!Zquic::Pkt::parseVersionNegotiation(
    ZuCSpan{b, unsigned(l)},
    parsedVersions, 2, nVersions), "VN parse failed");
  ZuCHECK(nVersions == 1 && parsedVersions[0] == Zquic::Version1,
    "VN version list mismatch");

  l = Zquic::Pkt::writeHandshake(b, sizeof(b), dcid, scid, 12, 1);
  ZuCHECK(l > 0, "Handshake header write failed");
  ZuCHECK(Zquic::Pkt::parseLong(
    ZuCSpan{b, unsigned(l + 1)}, h) > 0 &&
    h.type == Zquic::PktType::Handshake,
    "Handshake header parse failed");

  l = Zquic::Pkt::writeShort(b, sizeof(b), dcid, 0x1234, 2);
  ZuCHECK(l > 0, "short header write failed");
  Zquic::ShortHdr sh;
  ZuCHECK(Zquic::Pkt::parseShort(
    ZuCSpan{b, unsigned(l)}, dcid.length(), sh) > 0 &&
    sh.dcid == dcid && sh.pnLength == 2,
    "short header parse failed");
}

void testVarIntBoundariesAndPktNumbers()
{
  ZuTestScope(testVarIntBoundariesAndPktNumbers);

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
    ZuCHECK(Zquic::VarInt::length(c.value) == c.length,
      "varint boundary length mismatch");
    int n = Zquic::VarInt::encode(b, sizeof(b), c.value);
    ZuCHECK(n == int(c.length), "varint boundary encode length mismatch");
    uint64_t v = 0;
    unsigned used = 0;
    ZuCHECK(!Zquic::VarInt::decode(bytes_(b, c.length), v, used) &&
	v == c.value && used == c.length,
      "varint boundary decode mismatch");
    if (c.length > 1)
      ZuCHECK(Zquic::VarInt::decode(bytes_(b, c.length - 1), v, used) < 0,
	"truncated varint was accepted");
  }
  uint64_t v = 0;
  unsigned used = 0;
  ZuCHECK(Zquic::VarInt::decode({}, v, used) < 0,
    "empty varint was accepted");
  ZuCHECK(!Zquic::VarInt::length(1ULL<<62) &&
      Zquic::VarInt::encode(b, sizeof(b), 1ULL<<62) < 0,
    "oversize varint was accepted");
  unsigned o = 1;
  b[0] = 0xaa;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 16384, o) && o == 5 &&
      b[0] == 0xaa,
    "varint put offset handling mismatch");

  ZuCHECK(Zquic::PktNumber::encodedLength(1, 0) == 1 &&
      Zquic::PktNumber::encodedLength(1ULL<<7, 0) == 2 &&
      Zquic::PktNumber::encodedLength(1ULL<<15, 0) == 3 &&
      Zquic::PktNumber::encodedLength(1ULL<<23, 0) == 4,
    "packet number encoded length boundaries mismatch");
  ZuCHECK(Zquic::PktNumber::encode(b, sizeof(b), 0x123456U, 3) == 3 &&
      b[0] == 0x12 && b[1] == 0x34 && b[2] == 0x56,
    "packet number encode mismatch");
  ZuCHECK(Zquic::PktNumber::encode(b, sizeof(b), 1, 0) < 0 &&
      Zquic::PktNumber::encode(b, sizeof(b), 1, 5) < 0 &&
      Zquic::PktNumber::encode(b, 2, 1, 3) < 0,
    "invalid packet number encode accepted");
  ZuCHECK(Zquic::PktNumber::decode(0xa82f30eaULL, 0x9b32, 16) ==
      0xa82f9b32ULL,
    "packet number decode near expected mismatch");
  ZuCHECK(Zquic::PktNumber::decode(0xff, 0x00, 8) == 0x100,
    "packet number decode forward wrap mismatch");
  ZuCHECK(Zquic::PktNumber::decode(0x100, 0xff, 8) == 0xff,
    "packet number decode backward wrap mismatch");
}

void testPktParserRejections()
{
  ZuTestScope(testPktParserRejections);

  uint8_t b[128] = {};
  Zquic::LongHdr h;
  ZuCHECK(Zquic::Pkt::parseLong({}, h) < 0,
    "empty long header was accepted");
  b[0] = 0x40;
  ZuCHECK(Zquic::Pkt::parseLong(bytes_(b, 8), h) < 0,
    "short-header packet parsed as long");

  b[0] = 0xc0;
  b[4] = 1;
  b[5] = Zquic::CxnIDMax + 1;
  ZuCHECK(Zquic::Pkt::parseLong(bytes_(b, 7), h) < 0,
    "overlong DCID length was accepted");

  Zquic::CxnID dcid{"abcdefgh"};
  Zquic::CxnID scid{"server01"};
  ZuCHECK(Zquic::Pkt::writeInitial(b, sizeof(b), dcid, scid, 0, 0) < 0 &&
      Zquic::Pkt::writeShort(b, sizeof(b), dcid, 1, 5) < 0,
    "invalid packet number length accepted");
  int l = Zquic::Pkt::writeInitial(b, sizeof(b), dcid, scid, 4, 1);
  ZuCHECK(l > 0, "Initial token overrun setup failed");
  unsigned tokenOff = 1 + 4 + 1 + dcid.length() + 1 + scid.length();
  unsigned o = tokenOff;
  ZuCHECK(Zquic::VarInt::put(b, sizeof(b), 16, o) == 0,
    "Initial token overrun token length setup failed");
  ZuCHECK(Zquic::Pkt::parseLong(bytes_(b, o + 1), h) < 0,
    "Initial token length beyond packet was accepted");
  o = tokenOff;
  ZuCHECK(Zquic::VarInt::put(b, sizeof(b), (1ULL<<62) - 1, o) == 0,
    "Initial token max length setup failed");
  ZuCHECK(Zquic::Pkt::parseLong(bytes_(b, o), h) < 0,
    "Initial max token length beyond packet was accepted");

  uint32_t versions[] = { Zquic::Version1 };
  int n = Zquic::Pkt::writeVersionNegotiation(
    b, sizeof(b), dcid, scid, versions, 1);
  ZuCHECK(n > 0, "VN setup failed");
  unsigned nVersions = 0;
  uint32_t parsed[1];
  ZuCHECK(Zquic::Pkt::parseVersionNegotiation(
      bytes_(b, unsigned(n - 1)), parsed, 1, nVersions) < 0,
    "malformed VN version list was accepted");
  ZuCHECK(Zquic::Pkt::parseVersionNegotiation(
      bytes_(b, unsigned(n)), parsed, 0, nVersions) < 0,
    "VN parse ignored version capacity");

  Zquic::ShortHdr sh;
  ZuCHECK(Zquic::Pkt::parseShort(bytes_(b, 4), dcid.length(), sh) < 0,
    "too-short short header was accepted");
  b[0] = 0xc0;
  ZuCHECK(Zquic::Pkt::parseShort(bytes_(b, sizeof(b)), dcid.length(), sh) < 0,
    "long-header packet parsed as short");
  b[0] = 0x40;
  ZuCHECK(Zquic::Pkt::parseShort(
      bytes_(b, sizeof(b)), Zquic::CxnIDMax + 1, sh) < 0,
    "overlong short-header CID length was accepted");
}

void testFramesAndParams()
{
  ZuTestScope(testFramesAndParams);

  uint8_t b[256];
  int l = Zquic::FrameCodec::writeStream(
    b, sizeof(b), 0, 3, "hello", true);
  ZuCHECK(l > 0, "STREAM write failed");
  Zquic::Frame f;
  unsigned used = 0;
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used),
    "STREAM parse failed");
  ZuCHECK(f.type == Zquic::FrameType::Stream, "STREAM type mismatch");
  ZuCHECK(f.streamID == 0 && f.offset == 3 && f.length == 5 && f.fin,
    "STREAM fields mismatch");

  Zquic::TransportParams p;
  p.maxUDPPayloadSize = 1400;
  p.initialMaxData = 1000;
  p.initialSCID = "server01";
  p.statelessResetToken = Zquic::ResetToken{"0123456789abcdef"};
  p.statelessResetTokenPresent = true;
  l = p.encode(b, sizeof(b));
  ZuCHECK(l > 0, "transport parameter encode failed");
  Zquic::TransportParams q;
  ZuCHECK(!q.decode(ZuCSpan{b, unsigned(l)}),
    "transport parameter decode failed");
  ZuCHECK(q.maxUDPPayloadSize == 1400 && q.initialMaxData == 1000,
    "transport parameter values mismatch");
  ZuCHECK(q.initialSCID == p.initialSCID, "transport parameter CID mismatch");
  ZuCHECK(q.statelessResetTokenPresent &&
      q.statelessResetToken == p.statelessResetToken,
    "transport parameter stateless reset token mismatch");

  l = Zquic::FrameCodec::writeAck(b, sizeof(b), 10, 0, 3);
  ZuCHECK(l > 0, "ACK write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::Ack && f.offset == 10 && f.length == 3 &&
    f.ackRanges.length() == 1 &&
    f.ackRanges[0].first == 7 &&
    f.ackRanges[0].largest == 10,
    "ACK parse mismatch");

  l = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 4, 7, 99);
  ZuCHECK(l > 0, "RESET_STREAM write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::ResetStream &&
    f.streamID == 4 && f.errorCode == 7 && f.length == 99,
    "RESET_STREAM parse mismatch");

  l = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 4, 4096);
  ZuCHECK(l > 0, "MAX_STREAM_DATA write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::MaxStreamData &&
    f.streamID == 4 && f.value == 4096,
    "MAX_STREAM_DATA parse mismatch");

  l = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zi::StreamType::Simplex, 11);
  ZuCHECK(l > 0, "STREAMS_BLOCKED write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::StreamsBlocked &&
    f.streamType == Zi::StreamType::Simplex && f.value == 11,
    "STREAMS_BLOCKED parse mismatch");

  l = Zquic::FrameCodec::writePathChallenge(b, sizeof(b), "12345678");
  ZuCHECK(l > 0, "PATH_CHALLENGE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::PathChallenge && f.payload == "12345678",
    "PATH_CHALLENGE parse mismatch");

  l = Zquic::FrameCodec::writeConnectionClose(
    b, sizeof(b), Zquic::TransportError::FrameEncoding);
  ZuCHECK(l > 0, "CONNECTION_CLOSE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::ConnectionClose &&
    f.errorCode == Zquic::TransportError::FrameEncoding &&
    !f.value && !f.length && used == unsigned(l),
    "CONNECTION_CLOSE parse mismatch");

  unsigned o = 0;
  b[o++] = 0x03;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 10) == 1,
    "ACK ECN largest encode failed"); o += 1;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 0) == 1,
    "ACK ECN delay encode failed"); o += 1;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN range count encode failed"); o += 1;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 2) == 1,
    "ACK ECN first range encode failed"); o += 1;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN gap encode failed"); o += 1;
  ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 1) == 1,
    "ACK ECN range encode failed"); o += 1;
  for (unsigned i = 0; i < 3; ++i) {
    ZuCHECK(Zquic::VarInt::encode(b + o, sizeof(b) - o, 0) == 1,
      "ACK ECN count encode failed");
    ++o;
  }
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, o}, f, used) &&
    f.type == Zquic::FrameType::Ack && f.offset == 10 &&
    f.length == 2 && used == o &&
    f.ackRanges.length() == 2 &&
    f.ackRanges[0].first == 4 &&
    f.ackRanges[0].largest == 5 &&
    f.ackRanges[1].first == 8 &&
    f.ackRanges[1].largest == 10,
    "ACK ECN/range parse mismatch");

  l = Zquic::FrameCodec::writeHandshakeDone(b, sizeof(b));
  ZuCHECK(l == 1, "HANDSHAKE_DONE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{b, unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::HandshakeDone && used == unsigned(l),
    "HANDSHAKE_DONE parse mismatch");
  ZuCHECK(Zquic::FrameCodec::ackEliciting(Zquic::FrameType::HandshakeDone),
    "HANDSHAKE_DONE ack-eliciting classification mismatch");
}

void testControlFrameCoverage()
{
  ZuTestScope(testControlFrameCoverage);

  // QUIC v1 base frame matrix covered here:
  // 0x00 PADDING, 0x01 PING, 0x02/0x03 ACK, 0x04 RESET_STREAM,
  // 0x05 STOP_SENDING, 0x06 CRYPTO, 0x07 NEW_TOKEN, 0x08..0x0f STREAM,
  // 0x10 MAX_DATA, 0x11 MAX_STREAM_DATA, 0x12/0x13 MAX_STREAMS,
  // 0x14 DATA_BLOCKED, 0x15 STREAM_DATA_BLOCKED,
  // 0x16/0x17 STREAMS_BLOCKED, 0x18 NEW_CONNECTION_ID,
  // 0x19 RETIRE_CONNECTION_ID, 0x1a PATH_CHALLENGE,
  // 0x1b PATH_RESPONSE, 0x1c CONNECTION_CLOSE,
  // 0x1d CONNECTION_CLOSE_APP, 0x1e HANDSHAKE_DONE.
  // DATAGRAM is intentionally unsupported unless negotiated by extension.

  uint8_t b[256];
  Zquic::Frame f;
  unsigned used = 0;

  int n = Zquic::FrameCodec::writePadding(b, sizeof(b), 3);
  ZuCHECK(n == 3 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::Padding && used == 3 &&
      !Zquic::FrameCodec::ackEliciting(f.type),
    "PADDING coverage mismatch");

  n = Zquic::FrameCodec::writePing(b, sizeof(b));
  ZuCHECK(n == 1 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::Ping &&
      Zquic::FrameCodec::ackEliciting(f.type),
    "PING coverage mismatch");

  n = Zquic::FrameCodec::writeCrypto(b, sizeof(b), 7, "crypto-data");
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::Crypto &&
      f.offset == 7 && f.length == 11 && f.payload == "crypto-data",
    "CRYPTO coverage mismatch");

  unsigned o = 0;
  b[o++] = 0x07;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 5, o), "NEW_TOKEN length setup");
  memcpy(b + o, "token", 5); o += 5;
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, o), f, used) &&
      f.type == Zquic::FrameType::NewToken &&
      f.length == 5 && f.payload == "token",
    "NEW_TOKEN parse mismatch");

  o = 0;
  b[o++] = 0x08;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 3, o),
    "STREAM no-length ID setup");
  memcpy(b + o, "abc", 3); o += 3;
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, o), f, used) &&
      f.type == Zquic::FrameType::Stream &&
      f.streamID == 3 && f.length == 3 && f.payload == "abc" && !f.fin,
    "STREAM no-length parse mismatch");

  n = Zquic::FrameCodec::writeStopSending(b, sizeof(b), 8, 99);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::StopSending &&
      f.streamID == 8 && f.errorCode == 99,
    "STOP_SENDING coverage mismatch");

  n = Zquic::FrameCodec::writeMaxData(b, sizeof(b), 1ULL<<30);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::MaxData && f.value == (1ULL<<30),
    "MAX_DATA coverage mismatch");

  n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zi::StreamType::Duplex, 17);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::MaxStreams &&
      f.streamType == Zi::StreamType::Duplex && f.value == 17,
    "MAX_STREAMS bidi coverage mismatch");
  n = Zquic::FrameCodec::writeMaxStreams(
    b, sizeof(b), Zi::StreamType::Simplex, 19);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::MaxStreams &&
      f.streamType == Zi::StreamType::Simplex && f.value == 19,
    "MAX_STREAMS uni coverage mismatch");

  n = Zquic::FrameCodec::writeDataBlocked(b, sizeof(b), 4096);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::DataBlocked && f.value == 4096,
    "DATA_BLOCKED coverage mismatch");
  n = Zquic::FrameCodec::writeStreamDataBlocked(b, sizeof(b), 8, 2048);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::StreamDataBlocked &&
      f.streamID == 8 && f.value == 2048,
    "STREAM_DATA_BLOCKED coverage mismatch");

  Zquic::AckRange ranges[] = {
    {7, 5}, {11, 10}, {15, 15}
  };
  n = Zquic::FrameCodec::writeAckRanges(b, sizeof(b), ranges, 3, 4);
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::Ack && f.value == 4 &&
      f.ackRanges.length() == 3 &&
      f.ackRanges[0].first == 5 && f.ackRanges[0].largest == 7 &&
      f.ackRanges[1].first == 10 && f.ackRanges[1].largest == 11 &&
      f.ackRanges[2].first == 15 && f.ackRanges[2].largest == 15,
    "ACK range write/parse coverage mismatch");

  n = Zquic::FrameCodec::writePathResponse(b, sizeof(b), "87654321");
  ZuCHECK(n == 9 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::PathResponse && f.payload == "87654321",
    "PATH_RESPONSE coverage mismatch");

  Zquic::CxnID ncid{"server01"};
  Zquic::ResetToken ncidToken{"0123456789abcdef"};
  n = Zquic::FrameCodec::writeNewConnectionID(
    b, sizeof(b), 7, 2, ncid, ncidToken);
  ZuCHECK(n > 0, "NEW_CONNECTION_ID setup failed");
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::NewConnectionID &&
      f.value == 7 && f.offset == 2 &&
      f.length == 8 && f.payload == "server01" &&
      f.resetToken == ncidToken && used == unsigned(n),
    "NEW_CONNECTION_ID parse mismatch");

  n = Zquic::FrameCodec::writeRetireConnectionID(b, sizeof(b), 7);
  ZuCHECK(n > 0,
    "RETIRE_CONNECTION_ID setup failed");
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::RetireConnectionID && f.value == 7,
    "RETIRE_CONNECTION_ID parse mismatch");

  n = Zquic::FrameCodec::writeApplicationClose(b, sizeof(b), 42, "fail");
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(bytes_(b, unsigned(n)), f, used) &&
      f.type == Zquic::FrameType::ApplicationClose &&
      f.errorCode == 42 && f.length == 4 && f.payload == "fail",
    "application CONNECTION_CLOSE parse mismatch");

  b[0] = 0x30;
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, 1), f, used) &&
      f.type == Zquic::FrameType::Unknown && used == 1,
    "unsupported DATAGRAM frame parse mismatch");

  b[0] = 0x40;
  ZuCHECK(!Zquic::FrameCodec::parse(bytes_(b, 1), f, used) &&
      f.type == Zquic::FrameType::Unknown && used == 1,
    "unknown frame parse mismatch");
}

void testMalformedFrameCoverage()
{
  ZuTestScope(testMalformedFrameCoverage);

  uint8_t b[256];
  Zquic::Frame f;
  unsigned used = 0;

  ZuCHECK(Zquic::FrameCodec::writePing(b, 0) < 0 &&
      Zquic::FrameCodec::writePadding(b, 2, 3) < 0 &&
      Zquic::FrameCodec::writeCrypto(b, 3, 0, "abcd") < 0 &&
      Zquic::FrameCodec::writeStream(b, 3, 0, 0, "abcd", false) < 0 &&
      Zquic::FrameCodec::writePathChallenge(b, sizeof(b), "short") < 0 &&
      Zquic::FrameCodec::writePathResponse(b, 8, "12345678") < 0,
    "malformed frame write accepted");

  ZuCHECK(Zquic::FrameCodec::writeAck(b, sizeof(b), 5, 0, 6) < 0,
    "invalid ACK first range accepted");
  Zquic::AckRange badRanges[] = {
    {10, 8}, {11, 11}
  };
  ZuCHECK(Zquic::FrameCodec::writeAckRanges(
      b, sizeof(b), badRanges, 2, 0) < 0,
    "overlapping ACK ranges accepted");

  unsigned o = 0;
  b[o++] = 0x02;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 3, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 0, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 0, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 4, o),
    "invalid ACK setup failed");
  ZuCHECK(Zquic::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "ACK with first range larger than largest accepted");

  o = 0;
  b[o++] = 0x06;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 0, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 3, o),
    "truncated CRYPTO setup failed");
  b[o++] = 'x';
  ZuCHECK(Zquic::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "truncated CRYPTO payload accepted");

  o = 0;
  b[o++] = 0x0a;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 1, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 5, o),
    "truncated STREAM setup failed");
  memcpy(b + o, "abc", 3); o += 3;
  ZuCHECK(Zquic::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "truncated STREAM payload accepted");

  b[0] = 0x1a;
  memcpy(b + 1, "short", 5);
  ZuCHECK(Zquic::FrameCodec::parse(bytes_(b, 6), f, used) < 0,
    "truncated PATH_CHALLENGE accepted");

  o = 0;
  b[o++] = 0x18;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 1, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 0, o),
    "overlong NEW_CONNECTION_ID setup failed");
  b[o++] = Zquic::CxnIDMax + 1;
  memset(b + o, 0, Zquic::CxnIDMax + 1 + 16);
  o += Zquic::CxnIDMax + 1 + 16;
  ZuCHECK(Zquic::FrameCodec::parse(bytes_(b, o), f, used) < 0,
    "overlong NEW_CONNECTION_ID CID accepted");
}

void testTransportParamCoverage()
{
  ZuTestScope(testTransportParamCoverage);

  uint8_t b[512];
  Zquic::TransportParams p;
  p.originalDCID = "orig-dcid";
  p.initialSCID = "init-scid";
  p.retrySCID = "retrycid";
  p.maxIdleTimeout = 16363;
  p.maxUDPPayloadSize = Zquic::BufSize;
  p.initialMaxData = 1000000009;
  p.initialMaxStreamDataBidiLocal = 1000000007;
  p.initialMaxStreamDataBidiRemote = 961748941;
  p.initialMaxStreamDataUni = 982451653;
  p.initialMaxStreamsBidi = 908;
  p.initialMaxStreamsUni = 16383;
  p.ackDelayExponent = 20;
  p.maxAckDelay = 63;
  p.activeConnectionIDLimit = 1073741824;
  p.statelessResetToken = Zquic::ResetToken{"0123456789abcdef"};
  p.statelessResetTokenPresent = true;
  p.disableActiveMigration = true;
  int n = p.encode(b, sizeof(b));
  ZuCHECK(n > 0, "full transport parameter encode failed");

  Zquic::TransportParams q;
  ZuCHECK(!q.decode(bytes_(b, unsigned(n))),
    "full transport parameter decode failed");
  ZuCHECK(q.originalDCID == p.originalDCID &&
      q.initialSCID == p.initialSCID &&
      q.retrySCID == p.retrySCID &&
      q.maxIdleTimeout == p.maxIdleTimeout &&
      q.maxUDPPayloadSize == p.maxUDPPayloadSize &&
      q.initialMaxData == p.initialMaxData &&
      q.initialMaxStreamDataBidiLocal ==
	p.initialMaxStreamDataBidiLocal &&
      q.initialMaxStreamDataBidiRemote ==
	p.initialMaxStreamDataBidiRemote &&
      q.initialMaxStreamDataUni == p.initialMaxStreamDataUni &&
      q.initialMaxStreamsBidi == p.initialMaxStreamsBidi &&
      q.initialMaxStreamsUni == p.initialMaxStreamsUni &&
      q.ackDelayExponent == p.ackDelayExponent &&
      q.maxAckDelay == p.maxAckDelay &&
      q.activeConnectionIDLimit == p.activeConnectionIDLimit &&
      q.statelessResetTokenPresent == p.statelessResetTokenPresent &&
      q.statelessResetToken == p.statelessResetToken &&
      q.disableActiveMigration == p.disableActiveMigration,
    "full transport parameter round-trip mismatch");

  n = putParam_(b, sizeof(b), 0x3f, "abc");
  ZuCHECK(n > 0 &&
      !q.decode(bytes_(b, unsigned(n))),
    "unknown transport parameter was not ignored");

  n = putParam_(b, sizeof(b), 0x0c, "x");
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "non-empty disable_active_migration accepted");
  n = putParamVar_(b, sizeof(b), 0x03, Zquic::MinUDPPayload - 1);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "undersized max_udp_payload_size accepted");
  n = putParamVar_(b, sizeof(b), 0x0a, 21);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "oversized ack_delay_exponent accepted");
  n = putParamVar_(b, sizeof(b), 0x0b, 1ULL<<14);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "oversized max_ack_delay accepted");
  n = putParamVar_(b, sizeof(b), 0x0e, 1);
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "undersized active_connection_id_limit accepted");
  n = putParam_(b, sizeof(b), 0x02, "short-token");
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "short stateless_reset_token accepted");

  uint8_t value[32];
  memset(value, 'a', sizeof(value));
  n = putParam_(b, sizeof(b), 0x0f,
    bytes_(value, Zquic::MinCIDLength - 1));
  ZuCHECK(n > 0 &&
      !q.decode(bytes_(b, unsigned(n))) &&
      q.initialSCID.length() == Zquic::MinCIDLength - 1,
    "short peer initial_source_connection_id was rejected");
  n = putParam_(b, sizeof(b), 0x0f,
    bytes_(value, Zquic::CxnIDMax + 1));
  ZuCHECK(n > 0 &&
      q.decode(bytes_(b, unsigned(n))) < 0,
    "overlong initial_source_connection_id accepted");

  unsigned o = 0;
  ZuCHECK(!Zquic::VarInt::put(b, sizeof(b), 0x03, o) &&
      !Zquic::VarInt::put(b, sizeof(b), 2, o),
    "malformed transport varint setup failed");
  b[o++] = Zquic::MinUDPPayload & 0x3fU;
  b[o++] = 0;
  ZuCHECK(q.decode(bytes_(b, o)) < 0,
    "transport parameter varint with trailing byte accepted");

  n = p.encode(b, sizeof(b));
  ZuCHECK(n > 1 &&
      q.decode(bytes_(b, unsigned(n - 1))) < 0,
    "truncated transport parameter accepted");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVarIntAndPkt);
  ZuTestCall(testVarIntBoundariesAndPktNumbers);
  ZuTestCall(testPktParserRejections);
  ZuTestCall(testFramesAndParams);
  ZuTestCall(testControlFrameCoverage);
  ZuTestCall(testMalformedFrameCoverage);
  ZuTestCall(testTransportParamCoverage);
}
