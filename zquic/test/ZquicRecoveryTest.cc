//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

static bool getVar_(const uint8_t *b, unsigned len, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zquic::VarInt::decode(
      ZuBSpan{b + o, len - o}, v, n) < 0)
    return false;
  o += n;
  return true;
}

static Zquic::TxPkt txPkt_(
  uint64_t pn, unsigned bytes = 100, bool ackEliciting = true,
  Zquic::PktNumSpace::T space = Zquic::PktNumSpace::AppData)
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

static Zquic::TxPkt txCryptoPkt_(
  uint64_t pn, uint64_t offset, uint64_t length,
  unsigned bytes = 100)
{
  Zquic::TxPkt p = txPkt_(pn, bytes);
  p.frameCount = 0;
  p.addFrame(Zquic::SentFrameRef::crypto(offset, length));
  return p;
}

static Zquic::TxPkt txStreamPkt_(
  uint64_t pn, uint64_t streamID, uint64_t offset, uint32_t length,
  bool fin = false, unsigned bytes = 100)
{
  Zquic::TxPkt p = txPkt_(pn, bytes);
  p.frameCount = 0;
  Zquic::TxRange range;
  range.offset = uint32_t(offset);
  range.length = length;
  range.streamOffset = offset;
  p.addFrame(Zquic::SentFrameRef::stream(streamID, range, fin));
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
	ZuBSpan{b, unsigned(n)},
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

  Zquic::AckTracker many;
  bool manyOK = true;
  for (unsigned i = 0; i < Zquic::Frame::MaxAckRanges + 8; ++i)
    manyOK = many.add(i << 1) && manyOK;
  ZuCHECK(manyOK, "many-range ACK setup failed");
  Zquic::AckRange snapshot[Zquic::Frame::MaxAckRanges];
  ZuCHECK(many.multipleRanges() &&
      many.snapshot(snapshot, Zquic::Frame::MaxAckRanges) ==
	Zquic::Frame::MaxAckRanges,
    "many-range ACK snapshot did not cap ranges");
  uint8_t manyBuf[1024];
  n = many.writeFrame(manyBuf, sizeof(manyBuf));
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuBSpan{manyBuf, unsigned(n)},
	frame, used) &&
      used == unsigned(n) &&
      frame.ackRanges.length() == Zquic::Frame::MaxAckRanges &&
      frame.offset == uint64_t((Zquic::Frame::MaxAckRanges + 7) << 1),
    "many-range ACK frame did not retain newest capped ranges");

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
	ZuBSpan{b, unsigned(n)},
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
  cc.ackd(1200);
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
      tx.ackd() == 1 &&
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
      tx.ackd() == 1 &&
      tx.lost() == 2 &&
      !tx.nextRetransmit(ref),
    "duplicate ACK range mutated sent-packet state");
}

void testAckdFrameStopsLossReclaim()
{
  ZuTestScope(testAckdFrameStopsLossReclaim);

  Zquic::PktTxSpace tx;
  ZuCHECK(tx.add(txCryptoPkt_(0, 0, 46)) &&
      tx.add(txCryptoPkt_(1, 100, 10)) &&
      tx.add(txCryptoPkt_(2, 200, 10)) &&
      tx.add(txCryptoPkt_(4, 0, 46)),
    "duplicate-frame sent packet setup failed");

  Zquic::AckRange ranges[] = { Zquic::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 2 &&
      tx.ackd() == 1 &&
      tx.lost() == 2,
    "ACK/loss setup did not mark duplicate-frame packet lost");

  Zquic::SentFrameRef ref;
  uint64_t offsets[2] = {};
  uint64_t lengths[2] = {};
  bool refsOK = true;
  for (unsigned i = 0; i < 2; ++i) {
    refsOK = refsOK &&
      tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto;
    offsets[i] = ref.offset;
    lengths[i] = ref.length;
  }
  ZuCHECK(refsOK &&
      ((offsets[0] == 0 && lengths[0] == 46 &&
	offsets[1] == 100 && lengths[1] == 10) ||
	(offsets[0] == 100 && lengths[0] == 10 &&
	  offsets[1] == 0 && lengths[1] == 46)) &&
      !tx.nextRetransmit(ref),
    "loss reclaim did not leave range-frame ACK filtering to owner");

  Zquic::PktTxSpace late;
  ZuCHECK(late.add(txCryptoPkt_(0, 0, 46)) &&
      late.add(txCryptoPkt_(1, 100, 10)) &&
      late.add(txCryptoPkt_(2, 200, 10)) &&
      late.add(txCryptoPkt_(4, 400, 10)) &&
      late.add(txCryptoPkt_(5, 0, 46)),
    "late-ACK duplicate-frame sent packet setup failed");
  Zquic::AckRange lossRanges[] = { Zquic::AckRange{4, 4} };
  ZuCHECK(late.ack(lossRanges, 1, &lost) == 1 &&
      lost == 2 &&
      late.retransmitPending() == 1,
    "late-ACK setup did not queue lost duplicate frame");
  Zquic::AckRange ackRanges[] = { Zquic::AckRange{5, 5} };
  ZuCHECK(late.ack(ackRanges, 1, nullptr, 99) == 1,
    "late duplicate frame ACK failed");
  ZuCHECK(late.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 100 &&
      ref.length == 10 &&
      !late.nextRetransmit(ref),
    "retransmit pop did not skip a late-ACKd frame");

  Zquic::PktTxSpace dup;
  ZuCHECK(dup.add(txCryptoPkt_(0, 0, 46)) &&
      dup.add(txCryptoPkt_(1, 0, 46)) &&
      dup.add(txCryptoPkt_(3, 300, 10)),
    "duplicate outstanding frame setup failed");
  Zquic::AckRange dupLoss[] = { Zquic::AckRange{3, 3} };
  lost = 0;
  ZuCHECK(dup.ack(dupLoss, 1, &lost) == 1 &&
      lost == 1 &&
      !dup.retransmitPending(),
    "duplicate outstanding frame was reclaimed from older lost packet");
  ZuCHECK(!dup.nextRetransmit(ref),
    "duplicate outstanding frame suppression mismatch");

  Zquic::PktTxSpace lateLost;
  ZuCHECK(lateLost.add(txCryptoPkt_(0, 0, 46)) &&
      lateLost.add(txCryptoPkt_(1, 100, 10)) &&
      lateLost.add(txCryptoPkt_(4, 400, 10)),
    "late-ACK lost-packet setup failed");
  ZuCHECK(lateLost.ack(lossRanges, 1, &lost) == 1 &&
      lost == 2 &&
      lateLost.retransmitPending() == 2,
    "late-ACK lost-packet setup did not queue lost frames");
  Zquic::AckRange lateAckRanges[] = { Zquic::AckRange{0, 0} };
  Zquic::PktTxUpdate update;
  uint64_t ackdBytes = 999;
  ZuCHECK(lateLost.ack(
	lateAckRanges, 1, nullptr, 99, nullptr, &ackdBytes, nullptr,
	nullptr, &update) == 0 &&
      !ackdBytes &&
      !update.ackdBytes &&
      update.nAckdFrames == 1 &&
      update.ackdFrames[0].kind == Zquic::SentFrameKind::Crypto &&
      update.ackdFrames[0].offset == 0 &&
      update.ackdFrames[0].length == 46 &&
      lateLost.count() == 1 &&
      lateLost.retainedLost() == 1 &&
      lateLost.ackd() == 1,
    "retained lost ACK did not clear exact owner state without fresh ACK accounting");
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
  ZuCHECK(!tx.nextLossTime(ZuCmp<uint64_t>::null(), threshold),
    "loss deadline was scheduled before any largest ACKed packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zquic::timeUS(250),
    "next loss-deadline computation mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zquic::timeUS(270), threshold) == 1,
    "time-threshold loss did not mark only oldest packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zquic::timeUS(350),
    "next loss-deadline after a single oldest loss mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zquic::timeUS(350), threshold) == 1,
    "time-threshold loss missed oldest remaining packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zquic::timeUS(450),
    "next loss-deadline after two oldest losses mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zquic::timeUS(450), threshold) == 1,
    "time-threshold loss missed final eligible packet");
  ZuCHECK(!tx.nextLossTime(4, threshold),
    "loss deadline remained after all eligible packets were lost");

  Zquic::PktTxSpace ackOnly;
  Zquic::SentPkt pkt = txPkt_(5, 100, false);
  ZuCHECK(ackOnly.add(pkt) && !ackOnly.nextLossTime(6, threshold),
    "non-ack-eliciting packet scheduled a loss deadline");

  Zquic::PktTxSpace sameLargest;
  ZuCHECK(sameLargest.add(txPkt_(4)) &&
      !sameLargest.nextLossTime(4, threshold),
    "largest ACKed packet scheduled a loss deadline");
  ZuCHECK(!sameLargest.markTimeThreshLoss(
      4, Zquic::timeUS(1000), threshold),
    "largest ACKed packet was marked time-threshold lost");

  Zquic::PktTxSpace ackOnlyLargest;
  ZuCHECK(ackOnlyLargest.add(txPkt_(1)) &&
      ackOnlyLargest.add(txPkt_(2)) &&
      ackOnlyLargest.add(txPkt_(3)),
    "ACK-only largest setup failed");
  Zquic::AckRange ranges[] = { Zquic::AckRange{100, 3} };
  unsigned lost = 0;
  ZuCHECK(ackOnlyLargest.ack(ranges, 1, &lost) == 1 &&
      !lost &&
      ackOnlyLargest.ackd() == 1 &&
      ackOnlyLargest.lost() == 0,
    "ACK-only largest packet number caused spurious threshold loss");
}

void testPktNumSpaceAckLoss()
{
  ZuTestScope(testPktNumSpaceAckLoss);

  static const Zquic::PktNumSpace::T spaces[] = {
    Zquic::PktNumSpace::Initial,
    Zquic::PktNumSpace::Handshake,
    Zquic::PktNumSpace::AppData
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
	tx.ackd() == 3 &&
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

void testPTOReclaimUsesRetxQueue()
{
  ZuTestScope(testPTOReclaimUsesRetxQueue);

  Zquic::PktTxSpace tx;
  ZuCHECK(tx.add(txPkt_(7, 1200)) &&
      tx.add(txPkt_(8, 1200)) &&
      tx.bytesInFlight() == 2400,
    "PTO packet setup failed");
  ZuCHECK(tx.reclaimOnPTO(1) == 1 &&
      !tx.lost() &&
      !tx.ackd() &&
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
  ZuCHECK(tx.reclaimOnPTO(1) == 1 &&
      tx.retransmitPending() == 1,
    "PTO reclaim did not skip already reclaimed packet");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Crypto &&
      ref.offset == 70 &&
      ref.length == 8 &&
      !tx.nextRetransmit(ref),
    "PTO retransmit fallback did not move to next outstanding packet");
}

void testAckLeavesOnlyRetxPending()
{
  ZuTestScope(testAckLeavesOnlyRetxPending);

  Zquic::PktTxSpace tx;
  for (uint64_t pn = 0; pn < 6; ++pn)
    ZuCHECK(tx.add(txPkt_(pn)), "sent packet setup failed");

  Zquic::AckRange ranges[] = { Zquic::AckRange{5, 2} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 4 &&
      lost == 2 &&
      tx.ackd() == 4 &&
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

void testRetxSkipsUnackdStreamBytes()
{
  ZuTestScope(testRetxSkipsUnackdStreamBytes);

  Zquic::PktTxSpace tx;
  ZuCHECK(tx.add(txStreamPkt_(1, 7, 0, 100)) &&
      tx.add(txStreamPkt_(2, 7, 0, 50)) &&
      tx.add(txPkt_(4)),
    "overlapping stream sent-packet setup failed");
  Zquic::AckRange ranges[] = { Zquic::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 1 &&
      tx.retransmitPending() == 1,
    "overlapping stream loss setup failed");
  Zquic::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Stream &&
      ref.streamID == 7 &&
      ref.offset == 50 &&
      ref.length == 50 &&
      ref.range.streamOffset == 50 &&
      ref.range.offset == 50 &&
      ref.range.length == 50 &&
      !ref.fin &&
      !tx.nextRetransmit(ref),
    "stream retransmit did not skip outstanding prefix");
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

static Zquic::TxPkt txControlPkt_(
  uint64_t pn, const Zquic::SentFrameRef &ref, unsigned bytes = 100)
{
  Zquic::TxPkt p = txPkt_(pn, bytes);
  p.frameCount = 0;
  p.addFrame(ref);
  return p;
}

void testFlowControlRetransmit()
{
  ZuTestScope(testFlowControlRetransmit);

  Zquic::PktTxSpace tx;
  Zquic::FlowUpdate update{
    Zquic::FrameType::MaxStreams, 0, 129, Zi::StreamType::Duplex};
  ZuCHECK(tx.add(txControlPkt_(0, Zquic::SentFrameRef::flowUpdate(update))) &&
      tx.add(txControlPkt_(1, Zquic::SentFrameRef::blocked(
	Zquic::FrameType::StreamsBlocked, 0, 128,
	Zi::StreamType::Duplex))) &&
      tx.add(txPkt_(4)),
    "flow-control retransmit packet setup failed");

  Zquic::AckRange ranges[] = { Zquic::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 && lost == 2,
    "flow-control retransmit setup did not mark packets lost");

  Zquic::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::MaxStreams &&
      ref.value == 129,
    "lost MAX_STREAMS was not queued for retransmit");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zquic::SentFrameKind::Control &&
      ref.controlType == Zquic::FrameType::StreamsBlocked &&
      ref.value == 128 &&
      !tx.nextRetransmit(ref),
	    "lost STREAMS_BLOCKED was not queued for retransmit");
}

void testPktNumSpaceBatchCursors()
{
  ZuTestScope(testPktNumSpaceBatchCursors);

  Zquic::PktTxSpace tx;
  for (uint64_t pn = 0; pn <= 5; ++pn)
    ZuCHECK(tx.add(txPkt_(pn)), "batch cursor sent-packet add failed");

  Zquic::AckRange ranges[] = { Zquic::AckRange{5, 5} };
  Zquic::PktAckBatch ackBatch;
  Zquic::PktTxUpdate ackUpdate0;
  bool done = tx.ackBatch(
    ranges, 1, ackBatch, 1, Zquic::PktNumSpace::AppData, &ackUpdate0);
  Zquic::PktTxUpdate ackUpdate1;
  done = done || tx.ackBatch(
    ranges, 1, ackBatch, 1, Zquic::PktNumSpace::AppData, &ackUpdate1);
  ZuCHECK(done && ackBatch.ackd == 1 && ackUpdate0.ackdBytes == 100 &&
      !ackUpdate1.ackdBytes &&
      ackBatch.latestSentTime == Zquic::timeUS(500),
    "batch ACK cursor failed");

  Zquic::PktLossBatch lossBatch;
  Zquic::PktTxUpdate lossUpdate0;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate0);
  ZuCHECK(!done && lossBatch.lost == 2 && lossUpdate0.lostBytes == 200,
    "first batch loss cursor failed");

  Zquic::PktTxUpdate lossUpdate1;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate1);
  ZuCHECK(!done && lossBatch.lost == 3 && lossUpdate1.lostBytes == 100,
    "second batch loss cursor failed");

  Zquic::PktTxUpdate lossUpdate2;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate2);
  Zquic::PktTxUpdate lossUpdate3;
  done = done || tx.markPktThreshLossBatch(
    5, 3, lossBatch, 2, &lossUpdate3);
  ZuCHECK(done && lossBatch.lost == 3 && !lossUpdate2.lostBytes &&
      !lossUpdate3.lostBytes &&
      tx.lost() == 3 && tx.ackd() == 1 && tx.retransmitPending() == 3,
    "final batch loss cursor failed");
}

void testAckManager()
{
  ZuTestScope(testAckManager);

  Zquic::AckManager acks;
  ZuCHECK(acks.received(Zquic::PktNumSpace::Initial, 1, 1000, 25) &&
      acks.received(Zquic::PktNumSpace::AppData, 5, 1000, 25, false),
    "ACK manager receive failed");
  ZuCHECK(acks.pending(Zquic::PktNumSpace::Initial) &&
      acks.deadlineSet(Zquic::PktNumSpace::Initial) &&
      acks.deadline(Zquic::PktNumSpace::Initial) == 1025 &&
      !acks.due(Zquic::PktNumSpace::Initial, 1024) &&
      acks.due(Zquic::PktNumSpace::Initial, 1025),
    "ACK manager Initial deadline mismatch");
  ZuCHECK(!acks.pending(Zquic::PktNumSpace::AppData) &&
      !acks.deadlineSet(Zquic::PktNumSpace::AppData) &&
      !acks.due(Zquic::PktNumSpace::AppData, 2000),
    "ACK-only AppData packet created pending ACK work");
  ZuCHECK(!acks.post(Zquic::PktNumSpace::AppData),
    "ACK manager posted ACK-only packet");

  Zquic::AckManager postAcks;
  ZuCHECK(postAcks.received(Zquic::PktNumSpace::AppData, 5, 1000, 25, false) &&
      postAcks.ackEliciting(
      Zquic::PktNumSpace::AppData, 5, 1500, 25) &&
      postAcks.pending(Zquic::PktNumSpace::AppData) &&
      postAcks.post(Zquic::PktNumSpace::AppData) &&
      !postAcks.post(Zquic::PktNumSpace::AppData),
    "ACK manager did not post/suppress ack-eliciting packet");

  uint8_t b[64];
  int n = acks.writeFrame(Zquic::PktNumSpace::Initial, b, sizeof(b), 4);
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)},
	frame, used) &&
      frame.type == Zquic::FrameType::Ack &&
      frame.offset == 1 &&
      frame.value == 4 &&
      !frame.length,
    "ACK manager Initial frame mismatch");
  acks.sent(Zquic::PktNumSpace::Initial);
  ZuCHECK(!acks.pending(Zquic::PktNumSpace::Initial) &&
      !acks.pending(Zquic::PktNumSpace::AppData),
    "ACK manager sent cleared wrong packet space");

  ZuCHECK(acks.received(Zquic::PktNumSpace::AppData, 6, 2000, 25) &&
      acks.post(Zquic::PktNumSpace::AppData) &&
      !acks.post(Zquic::PktNumSpace::AppData) &&
      acks.deadlineSet(Zquic::PktNumSpace::AppData) &&
      acks.deadline(Zquic::PktNumSpace::AppData) == 2025,
    "ACK manager AppData post/deadline mismatch");
  ZuCHECK(!acks.received(Zquic::PktNumSpace::AppData, 6, 2010, 50) &&
      acks.pending(Zquic::PktNumSpace::AppData) &&
      acks.deadlineSet(Zquic::PktNumSpace::AppData) &&
      acks.deadline(Zquic::PktNumSpace::AppData) == 2025 &&
      acks.tracker(Zquic::PktNumSpace::AppData).count() == 1,
    "ACK manager duplicate packet changed deadline or ranges");
  n = acks.writeFrame(Zquic::PktNumSpace::AppData, b, sizeof(b));
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)},
	frame, used) &&
      frame.offset == 6 &&
      frame.length == 1,
    "ACK manager AppData coalesced frame mismatch");

  Zquic::AckManager ecnAcks;
  ZuCHECK(ecnAcks.received(
      Zquic::PktNumSpace::AppData, 1, 1000, 25, true, false,
      Zquic::EcnMark::ECT0) &&
      ecnAcks.received(
	Zquic::PktNumSpace::AppData, 2, 1001, 25, true, false,
	Zquic::EcnMark::ECT1) &&
      ecnAcks.received(
	Zquic::PktNumSpace::AppData, 3, 1002, 25, true, false,
	Zquic::EcnMark::CE) &&
      !ecnAcks.received(
	Zquic::PktNumSpace::AppData, 3, 1003, 25, true, false,
	Zquic::EcnMark::CE),
    "ACK manager ECN receive accounting failed");
  n = ecnAcks.writeFrame(Zquic::PktNumSpace::AppData, b, sizeof(b), 0, true);
  ZuCHECK(n > 0 && b[0] == 0x03 &&
      !Zquic::FrameCodec::parse(ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.ackECN.ect0 == 1 &&
      frame.ackECN.ect1 == 1 &&
      frame.ackECN.ce == 1,
    "ACK manager ACK_ECN frame mismatch");

  Zquic::AckManager runtime;
  ZuCHECK(runtime.received(Zquic::PktNumSpace::Initial, 1, 1000, 25, true, true) &&
      runtime.immediate(Zquic::PktNumSpace::Initial) &&
      runtime.due(Zquic::PktNumSpace::Initial, 1000) &&
      runtime.largestRxTime(Zquic::PktNumSpace::Initial) == 1000,
    "ACK manager Initial immediate state mismatch");
  uint64_t initialGen = runtime.gen(Zquic::PktNumSpace::Initial);
  ZuCHECK(runtime.received(Zquic::PktNumSpace::Handshake, 7, 1100, 25, true, true) &&
      runtime.immediate(Zquic::PktNumSpace::Handshake) &&
      runtime.due(Zquic::PktNumSpace::Handshake, 1100),
    "ACK manager Handshake immediate state mismatch");
  runtime.sent(Zquic::PktNumSpace::Initial, initialGen - 1);
  ZuCHECK(runtime.pending(Zquic::PktNumSpace::Initial),
    "ACK manager stale generation cleared pending ACK");
  runtime.sent(Zquic::PktNumSpace::Initial, initialGen);
  ZuCHECK(!runtime.pending(Zquic::PktNumSpace::Initial) &&
      runtime.pending(Zquic::PktNumSpace::Handshake),
    "ACK manager generation commit mismatch");

  ZuCHECK(runtime.received(Zquic::PktNumSpace::AppData, 10, 2000, 50) &&
      runtime.deadlineSet(Zquic::PktNumSpace::AppData) &&
      runtime.deadline(Zquic::PktNumSpace::AppData) == 2050 &&
      !runtime.immediate(Zquic::PktNumSpace::AppData),
    "ACK manager AppData delayed state mismatch");
  uint64_t appGen = runtime.gen(Zquic::PktNumSpace::AppData);
  ZuCHECK(runtime.post(Zquic::PktNumSpace::AppData),
    "ACK manager did not post initial pending ACK work");
  ZuCHECK(runtime.received(
	Zquic::PktNumSpace::AppData, 11, 2005, 50, false, false) &&
      runtime.gen(Zquic::PktNumSpace::AppData) == appGen &&
      !runtime.post(Zquic::PktNumSpace::AppData),
    "ACK manager ACK-only packet posted pending ACK work");
  ZuCHECK(runtime.received(Zquic::PktNumSpace::AppData, 12, 2010, 50, true, true) &&
      runtime.gen(Zquic::PktNumSpace::AppData) == appGen + 1 &&
      runtime.immediate(Zquic::PktNumSpace::AppData) &&
      runtime.due(Zquic::PktNumSpace::AppData, 2010) &&
      runtime.largestRxTime(Zquic::PktNumSpace::AppData) == 2010,
    "ACK manager AppData immediate/reordered state mismatch");
  ZuCHECK(!runtime.received(Zquic::PktNumSpace::AppData, 12, 2020, 50) &&
      runtime.gen(Zquic::PktNumSpace::AppData) == appGen + 1,
    "ACK manager duplicate changed generation");

  Zquic::AckManager active;
  ZuCHECK(active.received(Zquic::PktNumSpace::AppData, 1, 1000, 50) &&
      active.deadlineSet(Zquic::PktNumSpace::AppData) &&
      !active.immediate(Zquic::PktNumSpace::AppData),
    "ACK manager active ACK first packet state mismatch");
  ZuCHECK(active.received(Zquic::PktNumSpace::AppData, 2, 1001, 50) &&
      active.immediate(Zquic::PktNumSpace::AppData) &&
      active.due(Zquic::PktNumSpace::AppData, 1001) &&
      !active.deadlineSet(Zquic::PktNumSpace::AppData),
    "ACK manager active ACK threshold did not make ACK immediate");
  uint64_t activeGen = active.gen(Zquic::PktNumSpace::AppData);
  active.sent(Zquic::PktNumSpace::AppData, activeGen);
  ZuCHECK(active.received(Zquic::PktNumSpace::AppData, 3, 1025, 50) &&
      active.deadlineSet(Zquic::PktNumSpace::AppData) &&
      !active.immediate(Zquic::PktNumSpace::AppData),
    "ACK manager active ACK threshold did not reset after send");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecovery);
  ZuTestCall(testPktReorderDuplicateLoss);
  ZuTestCall(testAckdFrameStopsLossReclaim);
  ZuTestCall(testLossThresholds);
  ZuTestCall(testPktNumSpaceAckLoss);
  ZuTestCall(testPTOReclaimUsesRetxQueue);
  ZuTestCall(testAckLeavesOnlyRetxPending);
  ZuTestCall(testRetxSkipsUnackdStreamBytes);
  ZuTestCall(testTypedControlRefs);
  ZuTestCall(testFlowControlRetransmit);
  ZuTestCall(testPktNumSpaceBatchCursors);
  ZuTestCall(testAckManager);
}
