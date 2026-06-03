//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicRecovery.hh>

using namespace ZuTestUtil;

static bool getVar_(const uint8_t *b, unsigned len, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zquic::VarInt::decode(
      ZuCSpan{reinterpret_cast<const char *>(b + o), len - o}, v, n) < 0)
    return false;
  o += n;
  return true;
}

void testRecovery()
{
  ZuTestScope(testRecovery);

  Zquic::AckTracker ack;
  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(9), "ACK range add failed");
  ZuCHECK(ack.count() == 1 && ack.first(0) == 9 && ack.last(0) == 11,
    "ACK range coalesce mismatch");
  ZuCHECK(ack.add(13) && ack.add(15) && ack.count() == 3,
    "ACK disjoint range add failed");
  ZuCHECK(ack.add(14) && ack.add(12) && ack.count() == 1 &&
    ack.first(0) == 9 && ack.last(0) == 15 && ack.largest() == 15 &&
    ack.contains(12), "ACK transitive coalesce mismatch");
  Zquic::AckRange range;
  ZuCHECK(ack.range(0, range) && range.first == 9 && range.largest == 15,
    "ACK range export mismatch");
  uint8_t b[64];
  int n = ack.writeFrame(b, sizeof(b), 7);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
	frame, used) &&
      used == unsigned(n) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.offset == 15 &&
      frame.value == 7 &&
      frame.length == 6,
    "contiguous ACK frame encode mismatch");
  ack.clear();
  ZuCHECK(!ack.count() && ack.writeFrame(b, sizeof(b)) < 0,
    "empty ACK tracker emitted a frame");

  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(13) && ack.add(15),
    "disjoint ACK range setup failed");
  n = ack.writeFrame(b, sizeof(b), 3);
  ZuCHECK(n > 0 && b[0] == 0x02, "disjoint ACK frame write failed");
  unsigned o = 1;
  uint64_t largest = 0, delay = 0, rangeCount = 0, firstRange = 0;
  uint64_t gap0 = 0, range0 = 0, gap1 = 0, range1 = 0;
  ZuCHECK(getVar_(b, unsigned(n), o, largest) &&
      getVar_(b, unsigned(n), o, delay) &&
      getVar_(b, unsigned(n), o, rangeCount) &&
      getVar_(b, unsigned(n), o, firstRange) &&
      getVar_(b, unsigned(n), o, gap0) &&
      getVar_(b, unsigned(n), o, range0) &&
      getVar_(b, unsigned(n), o, gap1) &&
      getVar_(b, unsigned(n), o, range1) &&
      o == unsigned(n) &&
      largest == 15 &&
      delay == 3 &&
      rangeCount == 2 &&
      firstRange == 0 &&
      gap0 == 0 &&
      range0 == 0 &&
      gap1 == 0 &&
      range1 == 1,
    "disjoint ACK frame range encoding mismatch");

  Zquic::RttEstimator rtt;
  rtt.sample(1000, 0, true);
  rtt.sample(1200, 100, true);
  ZuCHECK(rtt.smoothed() > 0 && rtt.pto(25) > rtt.smoothed(),
    "RTT/PTO estimate mismatch");

  Zquic::NewReno cc(1200);
  uint64_t cwnd = cc.cwnd();
  cc.sent(1200);
  cc.acked(1200);
  ZuCHECK(cc.cwnd() > cwnd, "NewReno did not grow in slow start");
  cwnd = cc.cwnd();
  cc.sent(1200);
  cc.lost(1200, true);
  ZuCHECK(cc.cwnd() == cwnd, "PMTUD probe loss changed cwnd");
  cc.sent(1200);
  cc.lost(1200);
  ZuCHECK(cc.cwnd() < cwnd, "data loss did not reduce cwnd");
  cwnd = cc.cwnd();
  cc.lostAt(0, 100);
  ZuCHECK(cc.cwnd() < cwnd && cc.recoveryStartTime() == 100,
    "timed loss did not enter recovery");
  cwnd = cc.cwnd();
  cc.lostAt(0, 90);
  ZuCHECK(cc.cwnd() == cwnd, "old packet loss reduced cwnd during recovery");
}

void testAckManager()
{
  ZuTestScope(testAckManager);

  Zquic::AckManager acks;
  ZuCHECK(acks.received(Zquic::PacketSpace::Initial, 1, 1000, 25) &&
      acks.received(Zquic::PacketSpace::AppData, 5, 1000, 25, false),
    "ACK manager receive failed");
  ZuCHECK(acks.pending(Zquic::PacketSpace::Initial) &&
      acks.deadlineSet(Zquic::PacketSpace::Initial) &&
      acks.deadline(Zquic::PacketSpace::Initial) == 1025 &&
      !acks.due(Zquic::PacketSpace::Initial, 1024) &&
      acks.due(Zquic::PacketSpace::Initial, 1025),
    "ACK manager Initial deadline mismatch");
  ZuCHECK(acks.pending(Zquic::PacketSpace::AppData) &&
      !acks.deadlineSet(Zquic::PacketSpace::AppData) &&
      !acks.due(Zquic::PacketSpace::AppData, 2000),
    "ACK-only AppData packet set an ACK deadline");

  uint8_t b[64];
  int n = acks.writeFrame(Zquic::PacketSpace::Initial, b, sizeof(b), 4);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
	frame, used) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.offset == 1 &&
      frame.value == 4 &&
      !frame.length,
    "ACK manager Initial frame mismatch");
  acks.sent(Zquic::PacketSpace::Initial);
  ZuCHECK(!acks.pending(Zquic::PacketSpace::Initial) &&
      acks.pending(Zquic::PacketSpace::AppData),
    "ACK manager sent cleared wrong packet space");

  ZuCHECK(acks.received(Zquic::PacketSpace::AppData, 6, 2000, 25) &&
      acks.deadlineSet(Zquic::PacketSpace::AppData) &&
      acks.deadline(Zquic::PacketSpace::AppData) == 2025,
    "ACK manager AppData deadline mismatch");
  n = acks.writeFrame(Zquic::PacketSpace::AppData, b, sizeof(b));
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{reinterpret_cast<const char *>(b), unsigned(n)},
	frame, used) &&
      frame.offset == 6 &&
      frame.length == 1,
    "ACK manager AppData coalesced frame mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecovery);
  ZuTestCall(testAckManager);
}
