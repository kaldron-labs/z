//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicFrame.hh>
#include <zlib/ZquicTransportParams.hh>

using namespace ZuTestUtil;

void testVarIntAndPacket()
{
  ZuTestScope(testVarIntAndPacket);

  uint8_t b[256];
  uint64_t v = 0;
  unsigned n = 0;
  ZuCHECK(Zquic::VarInt::encode(b, sizeof(b), 63) == 1, "varint 1 encode");
  ZuCHECK(!Zquic::VarInt::decode(ZuCSpan{reinterpret_cast<char *>(b), 1}, v, n),
    "varint 1 decode failed");
  ZuCHECK(v == 63 && n == 1, "varint 1 decode mismatch");
  ZuCHECK(Zquic::VarInt::encode(b, sizeof(b), 16383) == 2, "varint 2 encode");
  ZuCHECK(!Zquic::VarInt::decode(ZuCSpan{reinterpret_cast<char *>(b), 2}, v, n),
    "varint 2 decode failed");
  ZuCHECK(v == 16383 && n == 2, "varint 2 decode mismatch");

  Zquic::ConnectionID dcid{"abcdefgh"};
  Zquic::ConnectionID scid{"server01"};
  int l = Zquic::Packet::writeInitial(b, sizeof(b), dcid, scid, 16, 2);
  ZuCHECK(l > 0, "Initial header write failed");
  Zquic::LongHeader h;
  ZuCHECK(Zquic::Packet::parseLong(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l + 2)}, h) > 0,
    "Initial header parse failed");
  ZuCHECK(h.type == Zquic::PacketType::Initial, "Initial type mismatch");
  ZuCHECK(h.dcid == dcid && h.scid == scid, "CID parse mismatch");

  uint32_t versions[] = { Zquic::Version1 };
  l = Zquic::Packet::writeVersionNegotiation(
    b, sizeof(b), dcid, scid, versions, 1);
  ZuCHECK(l > 0, "VN write failed");
  ZuCHECK(Zquic::Packet::isVersionNegotiation(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}),
    "VN recognition failed");
  unsigned nVersions = 0;
  uint32_t parsedVersions[2] = {};
  ZuCHECK(!Zquic::Packet::parseVersionNegotiation(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)},
    parsedVersions, 2, nVersions), "VN parse failed");
  ZuCHECK(nVersions == 1 && parsedVersions[0] == Zquic::Version1,
    "VN version list mismatch");

  l = Zquic::Packet::writeHandshake(b, sizeof(b), dcid, scid, 12, 1);
  ZuCHECK(l > 0, "Handshake header write failed");
  ZuCHECK(Zquic::Packet::parseLong(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l + 1)}, h) > 0 &&
    h.type == Zquic::PacketType::Handshake,
    "Handshake header parse failed");

  l = Zquic::Packet::writeShort(b, sizeof(b), dcid, 0x1234, 2);
  ZuCHECK(l > 0, "short header write failed");
  Zquic::ShortHeader sh;
  ZuCHECK(Zquic::Packet::parseShort(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, dcid.length(), sh) > 0 &&
    sh.dcid == dcid && sh.pnLength == 2,
    "short header parse failed");
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
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used),
    "STREAM parse failed");
  ZuCHECK(f.type == Zquic::FrameType::Stream, "STREAM type mismatch");
  ZuCHECK(f.streamID == 0 && f.offset == 3 && f.length == 5 && f.fin,
    "STREAM fields mismatch");

  Zquic::TransportParams p;
  p.maxUDPPayloadSize = 1400;
  p.initialMaxData = 1000;
  p.initialSCID.set("server01");
  l = Zquic::TransportParamsCodec::encode(b, sizeof(b), p);
  ZuCHECK(l > 0, "transport parameter encode failed");
  Zquic::TransportParams q;
  ZuCHECK(!Zquic::TransportParamsCodec::decode(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, q),
    "transport parameter decode failed");
  ZuCHECK(q.maxUDPPayloadSize == 1400 && q.initialMaxData == 1000,
    "transport parameter values mismatch");
  ZuCHECK(q.initialSCID == p.initialSCID, "transport parameter CID mismatch");

  l = Zquic::FrameCodec::writeAck(b, sizeof(b), 10, 0, 3);
  ZuCHECK(l > 0, "ACK write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::Ack && f.offset == 10 && f.length == 3 &&
    f.ackRangeCount == 1 &&
    f.ackRanges[0].first == 7 &&
    f.ackRanges[0].largest == 10,
    "ACK parse mismatch");

  l = Zquic::FrameCodec::writeResetStream(b, sizeof(b), 4, 7, 99);
  ZuCHECK(l > 0, "RESET_STREAM write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::ResetStream &&
    f.streamID == 4 && f.errorCode == 7 && f.length == 99,
    "RESET_STREAM parse mismatch");

  l = Zquic::FrameCodec::writeMaxStreamData(b, sizeof(b), 4, 4096);
  ZuCHECK(l > 0, "MAX_STREAM_DATA write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::MaxStreamData &&
    f.streamID == 4 && f.value == 4096,
    "MAX_STREAM_DATA parse mismatch");

  l = Zquic::FrameCodec::writeStreamsBlocked(
    b, sizeof(b), Zquic::StreamType::Uni, 11);
  ZuCHECK(l > 0, "STREAMS_BLOCKED write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::StreamsBlocked &&
    f.streamType == Zquic::StreamType::Uni && f.value == 11,
    "STREAMS_BLOCKED parse mismatch");

  l = Zquic::FrameCodec::writePathChallenge(b, sizeof(b), "12345678");
  ZuCHECK(l > 0, "PATH_CHALLENGE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::PathChallenge && f.payload == "12345678",
    "PATH_CHALLENGE parse mismatch");

  l = Zquic::FrameCodec::writeConnectionClose(
    b, sizeof(b), Zquic::TransportError::FrameEncoding);
  ZuCHECK(l > 0, "CONNECTION_CLOSE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
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
    ZuCSpan{reinterpret_cast<char *>(b), o}, f, used) &&
    f.type == Zquic::FrameType::Ack && f.offset == 10 &&
    f.length == 2 && used == o &&
    f.ackRangeCount == 2 &&
    f.ackRanges[0].first == 4 &&
    f.ackRanges[0].largest == 5 &&
    f.ackRanges[1].first == 8 &&
    f.ackRanges[1].largest == 10,
    "ACK ECN/range parse mismatch");

  l = Zquic::FrameCodec::writeHandshakeDone(b, sizeof(b));
  ZuCHECK(l == 1, "HANDSHAKE_DONE write failed");
  ZuCHECK(!Zquic::FrameCodec::parse(
    ZuCSpan{reinterpret_cast<char *>(b), unsigned(l)}, f, used) &&
    f.type == Zquic::FrameType::HandshakeDone && used == unsigned(l),
    "HANDSHAKE_DONE parse mismatch");
  ZuCHECK(Zquic::FrameCodec::ackEliciting(Zquic::FrameType::HandshakeDone),
    "HANDSHAKE_DONE ack-eliciting classification mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testVarIntAndPacket);
  ZuTestCall(testFramesAndParams);
}
