//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicRecovery.hh>

using namespace ZuTestUtil;

static bool getVar_(const uint8_t *b, unsigned len, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zquic::VarInt::decode(
      ZuCSpan{b + o, len - o}, v, n) < 0)
    return false;
  o += n;
  return true;
}

static Zquic::TxPkt txPkt_(
  uint64_t pn, unsigned bytes = 100, bool ackEliciting = true,
  Zquic::PktSpace::T space = Zquic::PktSpace::AppData)
{
  Zquic::TxPkt p;
  p.pn = pn;
  p.sentTime = Zquic::timeUS(pn * 100);
  p.bytes = bytes;
  p.space = space;
  p.ackEliciting = ackEliciting;
  p.inFlight = bytes;
  p.addFrame(Zquic::SentFrameRef::crypto(pn * 10, pn + 1));
  return p;
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
	ZuCSpan{b, unsigned(n)},
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

  Zquic::AckTracker reordered;
  ZuCHECK(reordered.add(0) && reordered.add(2),
    "reordered ACK range setup failed");
  ZuCHECK(reordered.count() == 2 &&
      reordered.first(0) == 0 &&
      reordered.last(0) == 0 &&
      reordered.first(1) == 2 &&
      reordered.last(1) == 2,
    "reordered ACK gap was not retained");
  n = reordered.writeFrame(b, sizeof(b));
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{b, unsigned(n)},
	frame, used) &&
      frame.ackRanges.length() == 2 &&
      frame.ackRanges[0].first == 0 &&
      frame.ackRanges[0].largest == 0 &&
      frame.ackRanges[1].first == 2 &&
      frame.ackRanges[1].largest == 2,
    "reordered ACK frame did not encode stashed gap");
  ZuCHECK(reordered.add(1) &&
      reordered.count() == 1 &&
      reordered.first(0) == 0 &&
      reordered.last(0) == 2,
    "filled ACK gap did not coalesce ranges");
  ZuCHECK(reordered.add(1) &&
      reordered.add(2) &&
      reordered.count() == 1 &&
      reordered.first(0) == 0 &&
      reordered.last(0) == 2,
    "duplicate packet numbers mutated ACK ranges");

  Zquic::RttEstimator rtt;
  ZuCHECK(rtt.pto(ZuTime{0}) == Zquic::timeUS(999000),
    "initial PTO did not use RFC/ngtcp2 initial RTT");
  rtt.sample(Zquic::timeUS(1000), ZuTime{0}, true);
  rtt.sample(Zquic::timeUS(1200), Zquic::timeUS(100), true);
  ZuCHECK(rtt.smoothed() && rtt.pto(Zquic::timeUS(25)) > rtt.smoothed(),
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

  Zquic::PktTxSpace tx;
  ZuCHECK(!tx.retransmitDropped(),
    "unbounded retransmit queue reported dropped frames");

  Zquic::PktTxSpace rttTx;
  ZuCHECK(rttTx.add(txPkt_(1)) && rttTx.add(txPkt_(3)) &&
      rttTx.add(txPkt_(2)), "RTT sent-packet setup failed");
  Zquic::AckRange rttRanges[] = {
    Zquic::AckRange{1, 1}, Zquic::AckRange{3, 3} };
  ZuTime sentTime;
  ZuCHECK(rttTx.ack(rttRanges, 2, nullptr, 3, &sentTime) == 2 &&
      sentTime == Zquic::timeUS(300),
    "ACK processing did not report largest newly acknowledged sent time");
}

void testPktReorderDuplicateLoss()
{
  ZuTestScope(testPktReorderDuplicateLoss);

  Zquic::AckTracker ack;
  ZuCHECK(ack.add(0) && ack.add(2),
    "out-of-order packet ACK setup failed");
  ZuCHECK(ack.count() == 2 &&
      ack.first(0) == 0 &&
      ack.last(0) == 0 &&
      ack.first(1) == 2 &&
      ack.last(1) == 2,
    "out-of-order packet ACK gap mismatch");
  ZuCHECK(ack.add(2) &&
      ack.count() == 2 &&
      ack.first(1) == 2 &&
      ack.last(1) == 2,
    "duplicate out-of-order packet changed ACK ranges");
  ZuCHECK(ack.add(1) &&
      ack.count() == 1 &&
      ack.first(0) == 0 &&
      ack.last(0) == 2,
    "reordered packet gap fill did not coalesce ACK ranges");

  Zquic::PktTxSpace tx;
  for (uint64_t pn = 0; pn < 6; ++pn)
    ZuCHECK(tx.add(txPkt_(pn)), "sent packet setup failed");
  ZuCHECK(tx.bytesInFlight() == 600,
    "sent packet bytes-in-flight setup mismatch");

  Zquic::AckRange ranges[] = { Zquic::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 2 &&
      tx.acked() == 1 &&
      tx.lost() == 2 &&
      tx.retransmittable() == 2 &&
      tx.bytesInFlight() == 300,
    "reordered ACK did not mark packet-threshold loss");

  Zquic::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 0 &&
      ref.length == 1,
    "first threshold-lost packet was not queued for retransmit");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 10 &&
      ref.length == 2,
    "second threshold-lost packet was not queued for retransmit");
  ZuCHECK(!tx.nextRetransmit(ref),
    "threshold loss queued duplicate retransmits");

  ZuCHECK(!tx.ack(ranges, 1, &lost) &&
      !lost &&
      tx.acked() == 1 &&
      tx.lost() == 2 &&
      !tx.nextRetransmit(ref),
    "duplicate ACK range mutated sent-packet state");
}

void testLossThresholds()
{
  ZuTestScope(testLossThresholds);

  Zquic::RttEstimator rtt;
  ZuCHECK(rtt.timeThreshold() == Zquic::timeUS(374625),
    "initial RTT time-threshold mismatch");
  rtt.sample(Zquic::timeUS(1000), ZuTime{0}, true);
  ZuCHECK(rtt.timeThreshold() == Zquic::timeUS(1125),
    "sampled RTT time-threshold mismatch");

  Zquic::PktTxSpace tx;
  ZuCHECK(tx.add(txPkt_(1)) && tx.add(txPkt_(2)) &&
      tx.add(txPkt_(3)), "sent packet setup failed");
  ZuTime threshold = Zquic::timeUS(150);
  ZuCHECK(tx.nextLossTime(threshold) == Zquic::timeUS(250),
    "next loss-deadline computation mismatch");
  ZuCHECK(tx.markTimeThresholdLoss(Zquic::timeUS(270), threshold) == 1,
    "time-threshold loss did not mark only oldest packet");
  ZuCHECK(tx.nextLossTime(threshold) == Zquic::timeUS(350),
    "next loss-deadline after a single oldest loss mismatch");
  ZuCHECK(tx.markTimeThresholdLoss(Zquic::timeUS(350), threshold) == 1,
    "time-threshold loss missed oldest remaining packet");
  ZuCHECK(tx.nextLossTime(threshold) == Zquic::timeUS(450),
    "next loss-deadline after two oldest losses mismatch");
  ZuCHECK(tx.markTimeThresholdLoss(Zquic::timeUS(450), threshold) == 1,
    "time-threshold loss missed final eligible packet");
  ZuCHECK(!tx.nextLossTime(threshold),
    "loss deadline remained after all eligible packets were lost");

  Zquic::PktTxSpace ackOnly;
  Zquic::SentPkt pkt = txPkt_(5, 100, false);
  ZuCHECK(ackOnly.add(pkt) && !ackOnly.nextLossTime(threshold),
    "non-ack-eliciting packet scheduled a loss deadline");
}

void testPktSpaceAckLoss()
{
  ZuTestScope(testPktSpaceAckLoss);

  static const Zquic::PktSpace::T spaces[] = {
    Zquic::PktSpace::Initial,
    Zquic::PktSpace::Handshake,
    Zquic::PktSpace::AppData
  };
  for (auto space : spaces) {
    Zquic::PktTxSpace tx;
    for (uint64_t pn = 0; pn < 6; ++pn)
      ZuCHECK(tx.add(txPkt_(pn, 100, true, space)),
	"packet-space sent packet setup failed");
    Zquic::AckRange ranges[] = { Zquic::AckRange{5, 3} };
    unsigned lost = 0;
    ZuTime sentTime;
    ZuCHECK(tx.ack(ranges, 1, &lost, 3, &sentTime) == 3 &&
	lost == 3 &&
	tx.acked() == 3 &&
	tx.lost() == 3 &&
	tx.bytesInFlight() == 0 &&
	sentTime == Zquic::timeUS(500),
      "packet-space ACK/loss processing mismatch");

    Zquic::SentFrameRef ref;
    for (uint64_t pn = 0; pn < 3; ++pn) {
      ZuCHECK(tx.nextRetransmit(ref) &&
	  ref.kind == Zquic::SentFrameKind::Crypto &&
	  ref.offset == pn * 10 &&
	  ref.length == pn + 1,
	"packet-space retransmit ref mismatch");
    }
    ZuCHECK(!tx.nextRetransmit(ref),
      "packet-space retransmit queue retained extra refs");
  }
}

void testPTOReclaimUsesRetransmitQueue()
{
  ZuTestScope(testPTOReclaimUsesRetransmitQueue);

  Zquic::PktTxSpace tx;
  ZuCHECK(tx.add(txPkt_(7, 1200)) &&
      tx.add(txPkt_(8, 1200)) &&
      tx.bytesInFlight() == 2400,
    "PTO packet setup failed");
  ZuCHECK(tx.reclaimOnPTO(1) == 1 &&
      !tx.lost() &&
      !tx.acked() &&
      tx.bytesInFlight() == 2400 &&
      tx.retransmitPending() == 1,
    "PTO reclaim did not enqueue through retransmit queue");

  Zquic::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 80 &&
      ref.length == 9 &&
      !tx.nextRetransmit(ref),
    "PTO retransmit fallback did not choose newest outstanding packet");
}

void testAckCanLeaveOnlyRetransmitsPending()
{
  ZuTestScope(testAckCanLeaveOnlyRetransmitsPending);

  Zquic::PktTxSpace tx;
  for (uint64_t pn = 0; pn < 6; ++pn)
    ZuCHECK(tx.add(txPkt_(pn)), "sent packet setup failed");

  Zquic::AckRange ranges[] = { Zquic::AckRange{5, 2} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 4 &&
      lost == 2 &&
      tx.acked() == 4 &&
      tx.lost() == 2 &&
      tx.retransmittable() == 2 &&
      !tx.bytesInFlight() &&
      tx.retransmitPending() == 2,
    "ACK/loss did not leave queued retransmits without bytes in flight");

  Zquic::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 0 &&
      ref.length == 1,
    "first zero-flight retransmit ref mismatch");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 10 &&
      ref.length == 2 &&
      !tx.nextRetransmit(ref),
    "second zero-flight retransmit ref mismatch");
}

void testTypedControlRefs()
{
  ZuTestScope(testTypedControlRefs);

  Zquic::FlowUpdate update{
    Zquic::FrameType::MaxStreamData, 8, 4096, Zi::StreamType::Duplex};
  Zquic::SentFrameRef ref = Zquic::SentFrameRef::flowUpdate(update);
  ZuCHECK(ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::MaxStreamData &&
      ref.streamID == 8 &&
      ref.value == 4096,
    "typed flow-update control ref mismatch");

  ref = Zquic::SentFrameRef::blocked(
    Zquic::FrameType::StreamsBlocked, 0, 17, Zi::StreamType::Simplex);
  ZuCHECK(ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::StreamsBlocked &&
      ref.streamType == Zi::StreamType::Simplex &&
      ref.value == 17,
    "typed blocked control ref mismatch");

  ref = Zquic::SentFrameRef::pathResponse("12345678");
  ZuCHECK(ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::PathResponse &&
      !memcmp(ref.payload, "12345678", 8),
    "typed PATH_RESPONSE control ref mismatch");

  ref = Zquic::SentFrameRef::handshakeDone();
  ZuCHECK(ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::HandshakeDone,
    "typed HANDSHAKE_DONE control ref mismatch");
}

void testAckManager()
{
  ZuTestScope(testAckManager);

  Zquic::AckManager acks;
  ZuCHECK(acks.received(Zquic::PktSpace::Initial, 1, 1000, 25) &&
      acks.received(Zquic::PktSpace::AppData, 5, 1000, 25, false),
    "ACK manager receive failed");
  ZuCHECK(acks.pending(Zquic::PktSpace::Initial) &&
      acks.deadlineSet(Zquic::PktSpace::Initial) &&
      acks.deadline(Zquic::PktSpace::Initial) == 1025 &&
      !acks.due(Zquic::PktSpace::Initial, 1024) &&
      acks.due(Zquic::PktSpace::Initial, 1025),
    "ACK manager Initial deadline mismatch");
  ZuCHECK(acks.pending(Zquic::PktSpace::AppData) &&
      !acks.deadlineSet(Zquic::PktSpace::AppData) &&
      !acks.due(Zquic::PktSpace::AppData, 2000),
    "ACK-only AppData packet set an ACK deadline");

  uint8_t b[64];
  int n = acks.writeFrame(Zquic::PktSpace::Initial, b, sizeof(b), 4);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{b, unsigned(n)},
	frame, used) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.offset == 1 &&
      frame.value == 4 &&
      !frame.length,
    "ACK manager Initial frame mismatch");
  acks.sent(Zquic::PktSpace::Initial);
  ZuCHECK(!acks.pending(Zquic::PktSpace::Initial) &&
      acks.pending(Zquic::PktSpace::AppData),
    "ACK manager sent cleared wrong packet space");

  ZuCHECK(acks.received(Zquic::PktSpace::AppData, 6, 2000, 25) &&
      acks.deadlineSet(Zquic::PktSpace::AppData) &&
      acks.deadline(Zquic::PktSpace::AppData) == 2025,
    "ACK manager AppData deadline mismatch");
  ZuCHECK(!acks.received(Zquic::PktSpace::AppData, 6, 2010, 50) &&
      acks.pending(Zquic::PktSpace::AppData) &&
      acks.deadlineSet(Zquic::PktSpace::AppData) &&
      acks.deadline(Zquic::PktSpace::AppData) == 2025 &&
      acks.tracker(Zquic::PktSpace::AppData).count() == 1,
    "ACK manager duplicate packet changed deadline or ranges");
  n = acks.writeFrame(Zquic::PktSpace::AppData, b, sizeof(b));
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuCSpan{b, unsigned(n)},
	frame, used) &&
      frame.offset == 6 &&
      frame.length == 1,
    "ACK manager AppData coalesced frame mismatch");

  Zquic::AckManager runtime;
  ZuCHECK(runtime.received(Zquic::PktSpace::Initial, 1, 1000, 25, true, true) &&
      runtime.immediate(Zquic::PktSpace::Initial) &&
      runtime.due(Zquic::PktSpace::Initial, 1000) &&
      runtime.largestRxTime(Zquic::PktSpace::Initial) == 1000,
    "ACK manager Initial immediate state mismatch");
  uint64_t initialGen = runtime.gen(Zquic::PktSpace::Initial);
  ZuCHECK(runtime.received(Zquic::PktSpace::Handshake, 7, 1100, 25, true, true) &&
      runtime.immediate(Zquic::PktSpace::Handshake) &&
      runtime.due(Zquic::PktSpace::Handshake, 1100),
    "ACK manager Handshake immediate state mismatch");
  runtime.sent(Zquic::PktSpace::Initial, initialGen - 1);
  ZuCHECK(runtime.pending(Zquic::PktSpace::Initial),
    "ACK manager stale generation cleared pending ACK");
  runtime.sent(Zquic::PktSpace::Initial, initialGen);
  ZuCHECK(!runtime.pending(Zquic::PktSpace::Initial) &&
      runtime.pending(Zquic::PktSpace::Handshake),
    "ACK manager generation commit mismatch");

  ZuCHECK(runtime.received(Zquic::PktSpace::AppData, 10, 2000, 50) &&
      runtime.deadlineSet(Zquic::PktSpace::AppData) &&
      runtime.deadline(Zquic::PktSpace::AppData) == 2050 &&
      !runtime.immediate(Zquic::PktSpace::AppData),
    "ACK manager AppData delayed state mismatch");
  uint64_t appGen = runtime.gen(Zquic::PktSpace::AppData);
  ZuCHECK(runtime.received(Zquic::PktSpace::AppData, 12, 2010, 50, true, true) &&
      runtime.gen(Zquic::PktSpace::AppData) == appGen + 1 &&
      runtime.immediate(Zquic::PktSpace::AppData) &&
      runtime.due(Zquic::PktSpace::AppData, 2010) &&
      runtime.largestRxTime(Zquic::PktSpace::AppData) == 2010,
    "ACK manager AppData immediate/reordered state mismatch");
  ZuCHECK(!runtime.received(Zquic::PktSpace::AppData, 12, 2020, 50) &&
      runtime.gen(Zquic::PktSpace::AppData) == appGen + 1,
    "ACK manager duplicate changed generation");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecovery);
  ZuTestCall(testPktReorderDuplicateLoss);
  ZuTestCall(testLossThresholds);
  ZuTestCall(testPktSpaceAckLoss);
  ZuTestCall(testPTOReclaimUsesRetransmitQueue);
  ZuTestCall(testAckCanLeaveOnlyRetransmitsPending);
  ZuTestCall(testTypedControlRefs);
  ZuTestCall(testAckManager);
}
