//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>

using namespace ZuTestUtil;

static Zutcp::SentFrameRef dataRef(uint64_t offset, uint32_t length)
{
  Zutcp::TxRange range;
  range.dataOffset = offset;
  range.length = length;
  return Zutcp::SentFrameRef::data(range, false);
}


#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

static bool getVar(const uint8_t *b, unsigned len, unsigned &o, uint64_t &v)
{
  unsigned n = 0;
  if (Zutcp::VarInt::decode(
      ZuBSpan{b + o, len - o}, v, n) < 0)
    return false;
  o += n;
  return true;
}

static Zutcp::TxPkt txPkt(
  uint64_t pn, unsigned bytes = 100, bool ackEliciting = true,
  Zutcp::PktNumSpace::T space = Zutcp::PktNumSpace::AppData)
{
  Zutcp::TxPkt p;
  p.pn = pn;
  p.sentTime = Zutcp::timeUS(pn * 100);
  p.bytes = bytes;
  p.space = space;
  p.ackEliciting = ackEliciting;
  p.inFlight = bytes;
  p.addFrame(dataRef(pn * 10, pn + 1));
  return p;
}

static Zutcp::TxPkt txRangePkt(
  uint64_t pn, uint64_t offset, uint64_t length,
  unsigned bytes = 100)
{
  Zutcp::TxPkt p = txPkt(pn, bytes);
  p.frames.clear();
  p.addFrame(dataRef(offset, length));
  return p;
}

static Zutcp::TxPkt txDataPkt(
  uint64_t pn, uint64_t offset, uint32_t length,
  bool fin = false, unsigned bytes = 100)
{
  Zutcp::TxPkt p = txPkt(pn, bytes);
  p.frames.clear();
  Zutcp::TxRange range;
  range.offset = uint32_t(offset);
  range.length = length;
  range.dataOffset = offset;
  p.addFrame(Zutcp::SentFrameRef::data(range, fin));
  return p;
}

void testFrameIdentity()
{
  ZuTestScope(testFrameIdentity);
  Zutcp::TxRange range;
  range.offset = 7;
  range.dataOffset = 13;
  range.length = 5;
  auto data = Zutcp::SentFrameRef::data(range, true);
  Zutcp::SentFrameKey original{data};
  data.range.offset = 21; // source-buffer coordinates do not identify wire bytes
  data.controlType = Zutcp::FrameType::MaxData;
  data.value = 91;
  Zutcp::SentFrameKey sameBytes{data};
  ZuCHECK(original == sameBytes && !original.cmp(sameBytes) &&
    original.hash() == sameBytes.hash(),
    "data identity must depend only on wire offset, length and FIN");
  data.fin = false;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "FIN is part of data identity");
  data.fin = true;
  ++data.offset;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "different wire offsets are distinct");
  --data.offset;
  ++data.length;
  ZuCHECK(original != Zutcp::SentFrameKey{data}, "different byte extents are distinct");


}

void testRecovery()
{
  ZuTestScope(testRecovery);

  Zutcp::AckTracker ack;
  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(9), "ACK range add failed");
  ZuCHECK(ack.count() == 1 && ack.first(0) == 9 && ack.last(0) == 11,
    "ACK range coalesce mismatch");
  ZuCHECK(ack.add(13) && ack.add(15) && ack.count() == 3,
    "ACK disjoint range add failed");
  ZuCHECK(ack.add(14) && ack.add(12) && ack.count() == 1 &&
    ack.first(0) == 9 && ack.last(0) == 15 && ack.largest() == 15 &&
    ack.contains(12), "ACK transitive coalesce mismatch");
  Zutcp::AckRange range;
  ZuCHECK(ack.range(0, range) && range.first == 9 && range.largest == 15,
    "ACK range export mismatch");
  uint8_t b[64];
  int n = ack.writeFrame(b, sizeof(b), 7);
  Zutcp::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)},
	frame, used) &&
      used == unsigned(n) &&
      frame.type == Zutcp::FrameType::Ack &&
      frame.offset == 15 &&
      frame.value == 7 &&
      frame.length == 6,
    "contiguous ACK frame encode mismatch");
  ack.clear();
  ZuCHECK(!ack.count() && ack.writeFrame(b, sizeof(b)) < 0,
    "empty ACK tracker emitted a frame");

  Zutcp::AckTracker many;
  bool manyOK = true;
  for (unsigned i = 0; i < Zutcp::Frame::MaxAckRanges + 8; ++i)
    manyOK = many.add(i << 1) && manyOK;
  ZuCHECK(manyOK, "many-range ACK setup failed");
  Zutcp::AckRange snapshot[Zutcp::Frame::MaxAckRanges];
  ZuCHECK(many.multipleRanges() &&
      many.count() == Zutcp::AckTracker::MaxRetained &&
      many.retiredRanges() == 7 &&
      many.snapshot(snapshot, Zutcp::Frame::MaxAckRanges) ==
	Zutcp::Frame::MaxAckRanges,
    "many-range ACK retention policy mismatch");
  Zutcp::Frame::AckRanges typed =
    many.snapshot(Zutcp::Frame::MaxAckRanges);
  ZuCHECK(typed.length() == Zutcp::Frame::MaxAckRanges &&
      typed[0].first == snapshot[0].first &&
      typed[typed.length() - 1].largest ==
	snapshot[Zutcp::Frame::MaxAckRanges - 1].largest,
    "typed ACK snapshot ordering mismatch");
  uint8_t manyBuf[1024];
  n = many.writeFrame(manyBuf, sizeof(manyBuf));
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(
	ZuBSpan{manyBuf, unsigned(n)},
	frame, used) &&
      used == unsigned(n) &&
      frame.ackRanges.length() == Zutcp::Frame::MaxAckRanges &&
      frame.offset == uint64_t((Zutcp::Frame::MaxAckRanges + 7) << 1),
    "many-range ACK frame did not retain newest capped ranges");

  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(13) && ack.add(15),
    "disjoint ACK range setup failed");
  n = ack.writeFrame(b, sizeof(b), 3);
  ZuCHECK(n > 0 && b[0] == 0x02, "disjoint ACK frame write failed");
  unsigned o = 1;
  uint64_t largest = 0, delay = 0, rangeCount = 0, firstRange = 0;
  uint64_t gap0 = 0, range0 = 0, gap1 = 0, range1 = 0;
  ZuCHECK(getVar(b, unsigned(n), o, largest) &&
      getVar(b, unsigned(n), o, delay) &&
      getVar(b, unsigned(n), o, rangeCount) &&
      getVar(b, unsigned(n), o, firstRange) &&
      getVar(b, unsigned(n), o, gap0) &&
      getVar(b, unsigned(n), o, range0) &&
      getVar(b, unsigned(n), o, gap1) &&
      getVar(b, unsigned(n), o, range1) &&
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

  Zutcp::AckTracker reordered;
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
      !Zutcp::FrameCodec::parse(
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

  Zutcp::RttEstimator rtt;
  ZuCHECK(rtt.pto(ZuTime{0}) == Zutcp::timeUS(999000),
    "initial PTO did not use RFC/ngtcp2 initial RTT");
  rtt.sample(Zutcp::timeUS(1000), ZuTime{0}, true);
  rtt.sample(Zutcp::timeUS(1200), Zutcp::timeUS(100), true);
  ZuCHECK(rtt.smoothed() && rtt.pto(Zutcp::timeUS(25)) > rtt.smoothed(),
    "RTT/PTO estimate mismatch");

  Zutcp::NewReno cc(1200);
  uint64_t cwnd = cc.cwnd();
  cc.sent(1200);
  cc.ackd(1200, 1200);
  ZuCHECK(cc.cwnd() > cwnd, "NewReno did not grow in slow start");
  cwnd = cc.cwnd();
  cc.sent(1200);
  cc.lostAt(1200, 1, 10, true);
  ZuCHECK(cc.cwnd() == cwnd, "PMTUD probe loss changed cwnd");
  cc.sent(1200);
  cc.lostAt(1200, 1, 10);
  ZuCHECK(cc.cwnd() < cwnd, "data loss did not reduce cwnd");
  cwnd = cc.cwnd();
  cc.lostAt(0, 100, 200);
  ZuCHECK(cc.cwnd() < cwnd && cc.recoveryStartTime() == 200,
    "timed loss did not enter recovery");
  cwnd = cc.cwnd();
  cc.lostAt(0, 90, 210);
  ZuCHECK(cc.cwnd() == cwnd, "old packet loss reduced cwnd during recovery");

  Zutcp::PktTxSpace tx;
  ZuCHECK(!tx.retransmitDropped(),
    "unbounded retransmit queue reported dropped frames");

  Zutcp::PktTxSpace rttTx;
  ZuCHECK(rttTx.add(txPkt(1)) && rttTx.add(txPkt(2)) &&
      rttTx.add(txPkt(3)), "RTT sent-packet setup failed");
  Zutcp::AckRange rttRanges[] = {
    Zutcp::AckRange{1, 1}, Zutcp::AckRange{3, 3} };
  ZuTime sentTime;
  ZuCHECK(rttTx.ack(rttRanges, 2, nullptr, 3, &sentTime) == 2 &&
      sentTime == Zutcp::timeUS(300),
    "ACK processing did not report largest newly acknowledged sent time");
}

void testPktReorderDuplicateLoss()
{
  ZuTestScopeRT(testPktReorderDuplicateLoss);

  Zutcp::AckTracker ack;
  ZUTCP_CHECK_RT(ack.add(0) && ack.add(2),
    "out-of-order packet ACK setup failed");
  ZUTCP_CHECK_RT(ack.count() == 2 &&
      ack.first(0) == 0 &&
      ack.last(0) == 0 &&
      ack.first(1) == 2 &&
      ack.last(1) == 2,
    "out-of-order packet ACK gap mismatch");
  ZUTCP_CHECK_RT(ack.add(2) &&
      ack.count() == 2 &&
      ack.first(1) == 2 &&
      ack.last(1) == 2,
    "duplicate out-of-order packet changed ACK ranges");
  ZUTCP_CHECK_RT(ack.add(1) &&
      ack.count() == 1 &&
      ack.first(0) == 0 &&
      ack.last(0) == 2,
    "reordered packet gap fill did not coalesce ACK ranges");

  Zutcp::PktTxSpace tx;
  for (uint64_t pn = 0; pn < 6; ++pn)
    ZUTCP_CHECK_RT(tx.add(txPkt(pn)), "sent packet setup failed");
  ZUTCP_CHECK_RT(tx.bytesInFlight() == 600,
    "sent packet bytes-in-flight setup mismatch");

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{4, 4} };
  unsigned lost = 0;
  ZUTCP_CHECK_RT(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 2 &&
      tx.ackd() == 1 &&
      tx.lost() == 2 &&
      tx.retransmittable() == 2 &&
      tx.bytesInFlight() == 300,
    "reordered ACK did not mark packet-threshold loss");

  Zutcp::SentFrameRef ref;
  ZUTCP_CHECK_RT(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 0 &&
      ref.length == 1,
    "first threshold-lost packet was not queued for retransmit");
  ZUTCP_CHECK_RT(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 10 &&
      ref.length == 2,
    "second threshold-lost packet was not queued for retransmit");
  ZUTCP_CHECK_RT(!tx.nextRetransmit(ref),
    "threshold loss queued duplicate retransmits");

  ZUTCP_CHECK_RT(!tx.ack(ranges, 1, &lost) &&
      !lost &&
      tx.ackd() == 1 &&
      tx.lost() == 2 &&
      !tx.nextRetransmit(ref),
    "duplicate ACK range mutated sent-packet state");
}

void ackdFrameStopsLossReclaim()
{
  ZuTestScope(ackdFrameStopsLossReclaim);

  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txRangePkt(0, 0, 46)) &&
      tx.add(txRangePkt(1, 100, 10)) &&
      tx.add(txRangePkt(2, 200, 10)) &&
      tx.add(txRangePkt(4, 0, 46)),
    "duplicate-frame sent packet setup failed");

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 2 &&
      tx.ackd() == 1 &&
      tx.lost() == 2,
    "ACK/loss setup did not mark duplicate-frame packet lost");

  Zutcp::SentFrameRef ref;
  uint64_t offsets[2] = {};
  uint64_t lengths[2] = {};
  bool refsOK = true;
  for (unsigned i = 0; i < 2; ++i) {
    refsOK = refsOK &&
      tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data;
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

  Zutcp::PktTxSpace late;
  ZuCHECK(late.add(txRangePkt(0, 0, 46)) &&
      late.add(txRangePkt(1, 100, 10)) &&
      late.add(txRangePkt(2, 200, 10)) &&
      late.add(txRangePkt(4, 400, 10)) &&
      late.add(txRangePkt(5, 0, 46)),
    "late-ACK duplicate-frame sent packet setup failed");
  Zutcp::AckRange lossRanges[] = { Zutcp::AckRange{4, 4} };
  ZuCHECK(late.ack(lossRanges, 1, &lost) == 1 &&
      lost == 2 &&
      late.retransmitPending() == 1,
    "late-ACK setup did not queue lost duplicate frame");
  Zutcp::AckRange ackRanges[] = { Zutcp::AckRange{5, 5} };
  ZuCHECK(late.ack(ackRanges, 1, nullptr, 99) == 1,
    "late duplicate frame ACK failed");
  ZuCHECK(late.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 100 &&
      ref.length == 10 &&
      !late.nextRetransmit(ref),
    "retransmit pop did not skip a late-ACKd frame");

  Zutcp::PktTxSpace dup;
  ZuCHECK(dup.add(txRangePkt(0, 0, 46)) &&
      dup.add(txRangePkt(1, 0, 46)) &&
      dup.add(txRangePkt(3, 300, 10)),
    "duplicate outstanding frame setup failed");
  Zutcp::AckRange dupLoss[] = { Zutcp::AckRange{3, 3} };
  lost = 0;
  ZuCHECK(dup.ack(dupLoss, 1, &lost) == 1 &&
      lost == 1 &&
      !dup.retransmitPending(),
    "duplicate outstanding frame was reclaimed from older lost packet");
  ZuCHECK(!dup.nextRetransmit(ref),
    "duplicate outstanding frame suppression mismatch");

  Zutcp::PktTxSpace lateLost;
  ZuCHECK(lateLost.add(txRangePkt(0, 0, 46)) &&
      lateLost.add(txRangePkt(1, 100, 10)) &&
      lateLost.add(txRangePkt(4, 400, 10)),
    "late-ACK lost-packet setup failed");
  ZuCHECK(lateLost.ack(lossRanges, 1, &lost) == 1 &&
      lost == 2 &&
      lateLost.retransmitPending() == 2,
    "late-ACK lost-packet setup did not queue lost frames");
  Zutcp::AckRange lateAckRanges[] = { Zutcp::AckRange{0, 0} };
  Zutcp::PktTxUpdate update;
  uint64_t ackdBytes = 999;
  ZuCHECK(lateLost.ack(
	lateAckRanges, 1, nullptr, 99, nullptr, &ackdBytes, nullptr,
	nullptr, &update) == 0 &&
      !ackdBytes &&
      !update.ackdBytes &&
      update.ackdFrames.length() == 1 &&
      update.ackdFrames[0].ref.kind == Zutcp::SentFrameKind::Data &&
      update.ackdFrames[0].ref.offset == 0 &&
      update.ackdFrames[0].ref.length == 46 &&
      lateLost.count() == 1 &&
      lateLost.retainedLost() == 1 &&
      lateLost.ackd() == 1,
    "retained lost ACK did not clear exact owner state without fresh ACK accounting");
}

void testLossThresholds()
{
  ZuTestScope(testLossThresholds);

  Zutcp::RttEstimator rtt;
  ZuCHECK(rtt.timeThreshold() == Zutcp::timeUS(374625),
    "initial RTT time-threshold mismatch");
  rtt.sample(Zutcp::timeUS(1000), ZuTime{0}, true);
  ZuCHECK(rtt.timeThreshold() == Zutcp::timeUS(1125),
    "sampled RTT time-threshold mismatch");

  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txPkt(1)) && tx.add(txPkt(2)) &&
      tx.add(txPkt(3)), "sent packet setup failed");
  ZuTime threshold = Zutcp::timeUS(150);
  ZuCHECK(!tx.nextLossTime(ZuCmp<uint64_t>::null(), threshold),
    "loss deadline was scheduled before any largest ACKed packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zutcp::timeUS(250),
    "next loss-deadline computation mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zutcp::timeUS(270), threshold) == 1,
    "time-threshold loss did not mark only oldest packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zutcp::timeUS(350),
    "next loss-deadline after a single oldest loss mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zutcp::timeUS(350), threshold) == 1,
    "time-threshold loss missed oldest remaining packet");
  ZuCHECK(tx.nextLossTime(4, threshold) == Zutcp::timeUS(450),
    "next loss-deadline after two oldest losses mismatch");
  ZuCHECK(tx.markTimeThreshLoss(4, Zutcp::timeUS(450), threshold) == 1,
    "time-threshold loss missed final eligible packet");
  ZuCHECK(!tx.nextLossTime(4, threshold),
    "loss deadline remained after all eligible packets were lost");

  Zutcp::PktTxSpace ackOnly;
  Zutcp::SentPkt pkt = txPkt(5, 100, false);
  ZuCHECK(ackOnly.add(pkt) && !ackOnly.nextLossTime(6, threshold),
    "non-ack-eliciting packet scheduled a loss deadline");

  Zutcp::PktTxSpace sameLargest;
  ZuCHECK(sameLargest.add(txPkt(4)) &&
      !sameLargest.nextLossTime(4, threshold),
    "largest ACKed packet scheduled a loss deadline");
  ZuCHECK(!sameLargest.markTimeThreshLoss(
      4, Zutcp::timeUS(1000), threshold),
    "largest ACKed packet was marked time-threshold lost");

  Zutcp::PktTxSpace ackOnlyLargest;
  ZuCHECK(ackOnlyLargest.add(txPkt(1)) &&
      ackOnlyLargest.add(txPkt(2)) &&
      ackOnlyLargest.add(txPkt(3)),
    "ACK-only largest setup failed");
  Zutcp::AckRange ranges[] = { Zutcp::AckRange{100, 3} };
  unsigned lost = 0;
  ZuCHECK(ackOnlyLargest.ack(ranges, 1, &lost) == 1 &&
      !lost &&
      ackOnlyLargest.ackd() == 1 &&
      ackOnlyLargest.lost() == 0,
    "ACK-only largest packet number caused spurious threshold loss");
}

void testRetainedLossFrontier()
{
  ZuTestScopeRT(testRetainedLossFrontier);

  Zutcp::PktTxSpace tx;
  for (uint64_t pn = 1; pn <= 1024; ++pn)
    ZUTCP_CHECK_RT(tx.add(txPkt(pn)) && tx.lose(pn),
      "retained-loss frontier setup failed");
  ZUTCP_CHECK_RT(tx.add(txPkt(1200)) && tx.retainedLost() == 1024 &&
      tx.recoveryCount() == 1,
    "active recovery frontier retained final packet history");
#ifdef ZDEBUG
  tx.resetRecoveryVisits();
#endif
  ZUTCP_CHECK_RT(tx.nextLossTime(1201, Zutcp::timeUS(150), 256) ==
      Zutcp::timeUS(120150),
    "retained loss history hid the active loss deadline");
#ifdef ZDEBUG
  ZUTCP_CHECK_RT(tx.recoveryVisits() == 1,
    "loss timer examined retained packet history");
  tx.resetOutstandingVisits();
  ZUTCP_CHECK_RT(!tx.requeueRetransmit(dataRef(12000, 1201)) &&
      tx.outstandingVisits() == 1,
    "retransmission suppression examined retained packet history");
  ZUTCP_CHECK_RT(tx.verifyOutstanding(),
    "retained-loss outstanding index mismatch");
#endif
}

void outstandingLifecycleIndex()
{
  ZuTestScope(outstandingLifecycleIndex);

#ifdef ZDEBUG
  Zutcp::PktTxSpace tx;
  uint32_t random = 0x9e3779b9U;
  for (uint64_t pn = 1; pn <= 512; ++pn) {
    random = random * 1664525U + 1013904223U;
    bool stream = random & 1;
    Zutcp::TxPkt packet = stream ?
      txDataPkt(pn, (random >> 3) & 255, 32,
	!(pn & 31)) :
      txRangePkt(pn, (random >> 3) & 255, 32);
    ZuCHECK(tx.add(packet), "outstanding-index packet add failed");
    switch ((random >> 16) & 3) {
      case 0: (void)tx.ack(pn); break;
      case 1: (void)tx.lose(pn); break;
      case 2:
	if (!(pn & 15)) (void)tx.reclaimOnPTO(1);
	break;
      default: break;
    }
    ZuCHECK(tx.verifyOutstanding(),
      "outstanding index diverged from packet history");
  }
  tx.clear();
  ZuCHECK(tx.verifyOutstanding(),
    "packet-space clear retained outstanding index entries");
#endif
}

void testPktNumSpaceAckLoss()
{
  ZuTestScopeRT(testPktNumSpaceAckLoss);

  static const Zutcp::PktNumSpace::T spaces[] = {
    Zutcp::PktNumSpace::Initial,
    Zutcp::PktNumSpace::AppData
  };
  for (auto space : spaces) {
    Zutcp::PktTxSpace tx;
    for (uint64_t pn = 0; pn < 6; ++pn)
      ZUTCP_CHECK_RT(tx.add(txPkt(pn, 100, true, space)),
	"packet-space sent packet setup failed");
    Zutcp::AckRange ranges[] = { Zutcp::AckRange{5, 3} };
    unsigned lost = 0;
    ZuTime sentTime;
    ZUTCP_CHECK_RT(tx.ack(ranges, 1, &lost, 3, &sentTime) == 3 &&
	lost == 3 &&
	tx.ackd() == 3 &&
	tx.lost() == 3 &&
	tx.bytesInFlight() == 0 &&
	sentTime == Zutcp::timeUS(500),
      "packet-space ACK/loss processing mismatch");

    Zutcp::SentFrameRef ref;
    for (uint64_t pn = 0; pn < 3; ++pn) {
      ZUTCP_CHECK_RT(tx.nextRetransmit(ref) &&
	  ref.kind == Zutcp::SentFrameKind::Data &&
	  ref.offset == pn * 10 &&
	  ref.length == pn + 1,
	"packet-space retransmit ref mismatch");
    }
    ZUTCP_CHECK_RT(!tx.nextRetransmit(ref),
      "packet-space retransmit queue retained extra refs");
  }
}

void testPTOReclaimUsesRetxQueue()
{
  ZuTestScope(testPTOReclaimUsesRetxQueue);

  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txPkt(7, 1200)) &&
      tx.add(txPkt(8, 1200)) &&
      tx.bytesInFlight() == 2400,
    "PTO packet setup failed");
  ZuCHECK(tx.reclaimOnPTO(1) == 1 &&
      !tx.lost() &&
      !tx.ackd() &&
      tx.bytesInFlight() == 2400 &&
      tx.retransmitPending() == 1,
    "PTO reclaim did not enqueue through retransmit queue");

  Zutcp::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 70 &&
      ref.length == 8 &&
      !tx.nextRetransmit(ref),
    "PTO retransmit fallback did not choose oldest outstanding packet");
  ZuCHECK(tx.reclaimOnPTO(1) == 1 &&
      tx.retransmitPending() == 1,
    "PTO reclaim did not skip already reclaimed packet");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 80 &&
      ref.length == 9 &&
      !tx.nextRetransmit(ref),
    "PTO retransmit fallback did not move to next outstanding packet");
}

void testAckLeavesOnlyRetxPending()
{
  ZuTestScopeRT(testAckLeavesOnlyRetxPending);

  Zutcp::PktTxSpace tx;
  for (uint64_t pn = 0; pn < 6; ++pn)
    ZUTCP_CHECK_RT(tx.add(txPkt(pn)), "sent packet setup failed");

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{5, 2} };
  unsigned lost = 0;
  ZUTCP_CHECK_RT(tx.ack(ranges, 1, &lost) == 4 &&
      lost == 2 &&
      tx.ackd() == 4 &&
      tx.lost() == 2 &&
      tx.retransmittable() == 2 &&
      !tx.bytesInFlight() &&
      tx.retransmitPending() == 2,
    "ACK/loss did not leave queued retransmits without bytes in flight");

  Zutcp::SentFrameRef ref;
  ZUTCP_CHECK_RT(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 0 &&
      ref.length == 1,
    "first zero-flight retransmit ref mismatch");
  ZUTCP_CHECK_RT(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 10 &&
      ref.length == 2 &&
      !tx.nextRetransmit(ref),
    "second zero-flight retransmit ref mismatch");
}

void retxSkipsUnackdData()
{
  ZuTestScope(retxSkipsUnackdData);

  Zutcp::PktTxSpace tx;
  ZuCHECK(tx.add(txDataPkt(1, 0, 100)) &&
      tx.add(txDataPkt(2, 0, 50)) &&
      tx.add(txPkt(4)),
    "overlapping stream sent-packet setup failed");
  Zutcp::AckRange ranges[] = { Zutcp::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 &&
      lost == 1 &&
      tx.retransmitPending() == 1,
    "overlapping stream loss setup failed");
  Zutcp::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data &&
      ref.offset == 50 &&
      ref.length == 50 &&
      ref.range.dataOffset == 50 &&
      ref.range.offset == 50 &&
      ref.range.length == 50 &&
      !ref.fin &&
      !tx.nextRetransmit(ref),
    "stream retransmit did not skip outstanding prefix");
}

void testTypedControlRefs()
{
  ZuTestScope(testTypedControlRefs);

  Zutcp::FlowUpdate update{
    Zutcp::FrameType::MaxData, 4096};
  Zutcp::SentFrameRef ref = Zutcp::SentFrameRef::flowUpdate(update);
  ZuCHECK(ref.kind == Zutcp::SentFrameKind::Control &&
      ref.controlType == Zutcp::FrameType::MaxData &&
      ref.value == 4096,
    "typed flow-update control ref mismatch");

  ref = Zutcp::SentFrameRef::blocked(
    17);
  ZuCHECK(ref.kind == Zutcp::SentFrameKind::Control &&
      ref.controlType == Zutcp::FrameType::DataBlocked &&
      ref.value == 17,
    "typed blocked control ref mismatch");



}

static Zutcp::TxPkt txControlPkt(
  uint64_t pn, const Zutcp::SentFrameRef &ref, unsigned bytes = 100)
{
  Zutcp::TxPkt p = txPkt(pn, bytes);
  p.frames.clear();
  p.addFrame(ref);
  return p;
}

void ptoProgressWithNewPackets()
{
  ZuTestScopeRT(ptoProgressWithNewPackets);

  Zutcp::PktTxSpace tx;
  ZUTCP_CHECK_RT(tx.add(txControlPkt(
      0, Zutcp::SentFrameRef::flowUpdate({Zutcp::FrameType::MaxData, 8192}))),
    "MAX_DATA packet setup failed");
  for (uint64_t pn = 1; pn < 4; ++pn)
    ZUTCP_CHECK_RT(tx.add(txDataPkt(pn, (pn - 1) << 5, 32)),
      "STREAM packet setup failed");

  unsigned served = 0;
  Zutcp::SentFrameRef ref;
  for (unsigned round = 0; round < 4; ++round) {
    ZUTCP_CHECK_RT(tx.reclaimOnPTO(1) == 1 &&
	!tx.lost() && !tx.ackd() &&
	tx.bytesInFlight() == uint64_t(4 + round) * 100 &&
	tx.nextRetransmit(ref) && !tx.retransmitPending(),
      "PTO changed loss/accounting or failed to produce its bounded probe");
    if (ref.kind == Zutcp::SentFrameKind::Control) {
      ZUTCP_CHECK_RT(ref.controlType == Zutcp::FrameType::MaxData && ref.value == 8192,
	"PTO produced an unrelated control frame");
      served |= 1;
    } else {
      ZUTCP_CHECK_RT(ref.kind == Zutcp::SentFrameKind::Data &&
	ref.length == 32 && ref.offset < 96,
	"PTO produced an unrelated STREAM range");
      served |= 1U << (1 + (ref.offset >> 5));
    }
    auto packet = txPkt(4 + round);
    packet.frames.clear();
    packet.addFrame(ref);
    ZUTCP_CHECK_RT(tx.add(packet), "PTO retransmission packet add failed");
#ifdef ZDEBUG
    ZUTCP_CHECK_RT(tx.verifyOutstanding(),
      "PTO retransmission changed the outstanding-frame index");
#endif
  }
  ZUTCP_CHECK_RT(served == 15 && tx.bytesInFlight() == 800,
    "new retransmission packets starved older MAX_DATA/connection data work");

  ZUTCP_CHECK_RT(tx.ack(0) && tx.ack(4) &&
      tx.ackd() == 2 && tx.bytesInFlight() == 600 &&
      tx.lose(7) && tx.lost() == 1 && tx.bytesInFlight() == 500 &&
      tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Data && ref.offset == 64 &&
      !tx.nextRetransmit(ref),
    "ACK/loss after PTO did not retire control packets or retain connection data work");
  auto packet = txPkt(8);
  packet.frames.clear();
  packet.addFrame(ref);
  ZUTCP_CHECK_RT(tx.add(packet), "lost STREAM retransmission add failed");
  for (unsigned range = 0; range < 3; ++range)
    ZUTCP_CHECK_RT(tx.reclaimOnPTO(1) == 1 &&
	tx.nextRetransmit(ref) &&
	ref.kind == Zutcp::SentFrameKind::Data &&
	ref.offset == uint64_t(range << 5) &&
	!tx.nextRetransmit(ref) && tx.bytesInFlight() == 600,
      "PTO revisited retired control work or changed flight accounting");
#ifdef ZDEBUG
  ZUTCP_CHECK_RT(tx.verifyOutstanding(),
    "ACK/loss after PTO changed the outstanding-frame index");
#endif
}

void testFlowControlRetransmit()
{
  ZuTestScope(testFlowControlRetransmit);

  Zutcp::PktTxSpace tx;
  Zutcp::FlowUpdate update{
    Zutcp::FrameType::MaxData, 129};
  ZuCHECK(tx.add(txControlPkt(0, Zutcp::SentFrameRef::flowUpdate(update))) &&
      tx.add(txControlPkt(1, Zutcp::SentFrameRef::blocked(
	128))) &&
      tx.add(txPkt(4)),
    "flow-control retransmit packet setup failed");

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{4, 4} };
  unsigned lost = 0;
  ZuCHECK(tx.ack(ranges, 1, &lost) == 1 && lost == 2,
    "flow-control retransmit setup did not mark packets lost");

  Zutcp::SentFrameRef ref;
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Control &&
      ref.controlType == Zutcp::FrameType::MaxData &&
      ref.value == 129,
    "lost MAX_DATA was not queued for retransmit");
  ZuCHECK(tx.nextRetransmit(ref) &&
      ref.kind == Zutcp::SentFrameKind::Control &&
      ref.controlType == Zutcp::FrameType::DataBlocked &&
      ref.value == 128 &&
      !tx.nextRetransmit(ref),
	    "lost DATA_BLOCKED was not queued for retransmit");
}

void testPktNumSpaceBatchCursors()
{
  ZuTestScopeRT(testPktNumSpaceBatchCursors);

  Zutcp::PktTxSpace tx;
  for (uint64_t pn = 0; pn <= 5; ++pn)
    ZUTCP_CHECK_RT(tx.add(txPkt(pn)), "batch cursor sent-packet add failed");

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{5, 5} };
  Zutcp::PktAckBatch ackBatch;
  Zutcp::PktTxUpdate ackUpdate0;
  bool done = tx.ackBatch(
    ranges, 1, ackBatch, 1, Zutcp::PktNumSpace::AppData, &ackUpdate0);
  Zutcp::PktTxUpdate ackUpdate1;
  done = done || tx.ackBatch(
    ranges, 1, ackBatch, 1, Zutcp::PktNumSpace::AppData, &ackUpdate1);
  ZUTCP_CHECK_RT(done && ackBatch.ackd == 1 && ackUpdate0.ackdBytes == 100 &&
      !ackUpdate1.ackdBytes &&
      ackBatch.latestSentTime == Zutcp::timeUS(500),
    "batch ACK cursor failed");

  Zutcp::PktLossBatch lossBatch;
  Zutcp::PktTxUpdate lossUpdate0;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate0);
  ZUTCP_CHECK_RT(!done && lossBatch.lost == 2 && lossUpdate0.lostBytes == 200,
    "first batch loss cursor failed");

  Zutcp::PktTxUpdate lossUpdate1;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate1);
  ZUTCP_CHECK_RT(done && lossBatch.lost == 3 && lossUpdate1.lostBytes == 100,
    "second batch loss cursor failed");

  Zutcp::PktTxUpdate lossUpdate2;
  done = tx.markPktThreshLossBatch(5, 3, lossBatch, 2, &lossUpdate2);
  ZUTCP_CHECK_RT(done && lossBatch.lost == 3 && !lossUpdate2.lostBytes &&
      tx.lost() == 3 && tx.ackd() == 1 && tx.retransmitPending() == 3,
    "final batch loss cursor failed");
}

void testAckManager()
{
  ZuTestScope(testAckManager);

  Zutcp::AckManager acks;
  ZuCHECK(acks.received(Zutcp::PktNumSpace::Initial, 1, 1000, 25) &&
      acks.received(Zutcp::PktNumSpace::AppData, 5, 1000, 25, false),
    "ACK manager receive failed");
  ZuCHECK(acks.pending(Zutcp::PktNumSpace::Initial) &&
      acks.deadlineSet(Zutcp::PktNumSpace::Initial) &&
      acks.deadline(Zutcp::PktNumSpace::Initial) == 1025 &&
      !acks.due(Zutcp::PktNumSpace::Initial, 1024) &&
      acks.due(Zutcp::PktNumSpace::Initial, 1025),
    "ACK manager Initial deadline mismatch");
  ZuCHECK(!acks.pending(Zutcp::PktNumSpace::AppData) &&
      !acks.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      !acks.due(Zutcp::PktNumSpace::AppData, 2000),
    "ACK-only AppData packet created pending ACK work");
  ZuCHECK(!acks.post(Zutcp::PktNumSpace::AppData),
    "ACK manager posted ACK-only packet");

  Zutcp::AckManager postAcks;
  ZuCHECK(postAcks.received(Zutcp::PktNumSpace::AppData, 5, 1000, 25, false) &&
      postAcks.ackEliciting(
      Zutcp::PktNumSpace::AppData, 5, 1500, 25) &&
      postAcks.pending(Zutcp::PktNumSpace::AppData) &&
      postAcks.post(Zutcp::PktNumSpace::AppData) &&
      !postAcks.post(Zutcp::PktNumSpace::AppData),
    "ACK manager did not post/suppress ack-eliciting packet");

  uint8_t b[64];
  int n = acks.writeFrame(Zutcp::PktNumSpace::Initial, b, sizeof(b), 4);
  Zutcp::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)},
	frame, used) &&
      frame.type == Zutcp::FrameType::Ack &&
      frame.offset == 1 &&
      frame.value == 4 &&
      !frame.length,
    "ACK manager Initial frame mismatch");
  acks.sent(Zutcp::PktNumSpace::Initial);
  ZuCHECK(!acks.pending(Zutcp::PktNumSpace::Initial) &&
      !acks.pending(Zutcp::PktNumSpace::AppData),
    "ACK manager sent cleared wrong packet space");

  ZuCHECK(acks.received(Zutcp::PktNumSpace::AppData, 6, 2000, 25) &&
      acks.post(Zutcp::PktNumSpace::AppData) &&
      !acks.post(Zutcp::PktNumSpace::AppData) &&
      acks.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      acks.deadline(Zutcp::PktNumSpace::AppData) == 2025,
    "ACK manager AppData post/deadline mismatch");
  ZuCHECK(!acks.received(Zutcp::PktNumSpace::AppData, 6, 2010, 50) &&
      acks.pending(Zutcp::PktNumSpace::AppData) &&
      acks.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      acks.deadline(Zutcp::PktNumSpace::AppData) == 2025 &&
      acks.tracker(Zutcp::PktNumSpace::AppData).count() == 1,
    "ACK manager duplicate packet changed deadline or ranges");
  n = acks.writeFrame(Zutcp::PktNumSpace::AppData, b, sizeof(b));
  ZuCHECK(n > 0 &&
      !Zutcp::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)},
	frame, used) &&
      frame.offset == 6 &&
      frame.length == 1,
    "ACK manager AppData coalesced frame mismatch");

  Zutcp::AckManager ecnAcks;
  ZuCHECK(ecnAcks.received(
      Zutcp::PktNumSpace::AppData, 1, 1000, 25, true, false,
      Zutcp::EcnMark::ECT0) &&
      ecnAcks.received(
	Zutcp::PktNumSpace::AppData, 2, 1001, 25, true, false,
	Zutcp::EcnMark::ECT1) &&
      ecnAcks.received(
	Zutcp::PktNumSpace::AppData, 3, 1002, 25, true, false,
	Zutcp::EcnMark::CE) &&
      !ecnAcks.received(
	Zutcp::PktNumSpace::AppData, 3, 1003, 25, true, false,
	Zutcp::EcnMark::CE),
    "ACK manager ECN receive accounting failed");
  n = ecnAcks.writeFrame(Zutcp::PktNumSpace::AppData, b, sizeof(b), 0, true);
  ZuCHECK(n > 0 && b[0] == 0x03 &&
      !Zutcp::FrameCodec::parse(ZuBSpan{b, unsigned(n)}, frame, used) &&
      frame.ackECN.ect0 == 1 &&
      frame.ackECN.ect1 == 1 &&
      frame.ackECN.ce == 1,
    "ACK manager ACK_ECN frame mismatch");

  Zutcp::AckManager runtime;
  ZuCHECK(runtime.received(Zutcp::PktNumSpace::Initial, 1, 1000, 25, true, true) &&
      runtime.immediate(Zutcp::PktNumSpace::Initial) &&
      runtime.due(Zutcp::PktNumSpace::Initial, 1000) &&
      runtime.largestRxTime(Zutcp::PktNumSpace::Initial) == 1000,
    "ACK manager Initial immediate state mismatch");
  uint64_t initialGen = runtime.gen(Zutcp::PktNumSpace::Initial);
  ZuCHECK(runtime.received(Zutcp::PktNumSpace::AppData, 7, 1100, 25, true, true) &&
      runtime.immediate(Zutcp::PktNumSpace::AppData) &&
      runtime.due(Zutcp::PktNumSpace::AppData, 1100),
    "ACK manager independent AppData immediate state mismatch");
  runtime.sent(Zutcp::PktNumSpace::Initial, initialGen - 1);
  ZuCHECK(runtime.pending(Zutcp::PktNumSpace::Initial),
    "ACK manager stale generation cleared pending ACK");
  runtime.sent(Zutcp::PktNumSpace::Initial, initialGen);
  ZuCHECK(!runtime.pending(Zutcp::PktNumSpace::Initial) &&
      runtime.pending(Zutcp::PktNumSpace::AppData),
    "ACK manager generation commit mismatch");

  runtime.sent(Zutcp::PktNumSpace::AppData);
  ZuCHECK(runtime.received(Zutcp::PktNumSpace::AppData, 10, 2000, 50) &&
      runtime.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      runtime.deadline(Zutcp::PktNumSpace::AppData) == 2050 &&
      !runtime.immediate(Zutcp::PktNumSpace::AppData),
    "ACK manager AppData delayed state mismatch");
  uint64_t appGen = runtime.gen(Zutcp::PktNumSpace::AppData);
  ZuCHECK(runtime.post(Zutcp::PktNumSpace::AppData),
    "ACK manager did not post initial pending ACK work");
  ZuCHECK(runtime.received(
	Zutcp::PktNumSpace::AppData, 11, 2005, 50, false, false) &&
      runtime.gen(Zutcp::PktNumSpace::AppData) == appGen &&
      !runtime.post(Zutcp::PktNumSpace::AppData),
    "ACK manager ACK-only packet posted pending ACK work");
  ZuCHECK(runtime.received(Zutcp::PktNumSpace::AppData, 12, 2010, 50, true, true) &&
      runtime.gen(Zutcp::PktNumSpace::AppData) == appGen + 1 &&
      runtime.immediate(Zutcp::PktNumSpace::AppData) &&
      runtime.due(Zutcp::PktNumSpace::AppData, 2010) &&
      runtime.largestRxTime(Zutcp::PktNumSpace::AppData) == 2010,
    "ACK manager AppData immediate/reordered state mismatch");
  ZuCHECK(!runtime.received(Zutcp::PktNumSpace::AppData, 12, 2020, 50) &&
      runtime.gen(Zutcp::PktNumSpace::AppData) == appGen + 1,
    "ACK manager duplicate changed generation");

  Zutcp::AckManager active;
  ZuCHECK(active.received(Zutcp::PktNumSpace::AppData, 1, 1000, 50) &&
      active.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      !active.immediate(Zutcp::PktNumSpace::AppData),
    "ACK manager active ACK first packet state mismatch");
  ZuCHECK(active.received(Zutcp::PktNumSpace::AppData, 2, 1001, 50) &&
      active.immediate(Zutcp::PktNumSpace::AppData) &&
      active.due(Zutcp::PktNumSpace::AppData, 1001) &&
      !active.deadlineSet(Zutcp::PktNumSpace::AppData),
    "ACK manager active ACK threshold did not make ACK immediate");
  uint64_t activeGen = active.gen(Zutcp::PktNumSpace::AppData);
  active.sent(Zutcp::PktNumSpace::AppData, activeGen);
  ZuCHECK(active.received(Zutcp::PktNumSpace::AppData, 3, 1025, 50) &&
      active.deadlineSet(Zutcp::PktNumSpace::AppData) &&
      !active.immediate(Zutcp::PktNumSpace::AppData),
    "ACK manager active ACK threshold did not reset after send");
}

void testLosslessFrameUpdates()
{
  ZuTestScope(testLosslessFrameUpdates);

  Zutcp::PktTxUpdate lost;
  bool lostSetup = true;
  for (unsigned i = 0; i < 65; ++i) {
    Zutcp::SentPkt packet;
    packet.pn = i;
    packet.bytes = 1;
    lostSetup &= packet.addFrame(
	dataRef(i, 1));
    lost.lost(packet);
  }
  ZuCHECK(lostSetup && lost.lostFrames.length() == 65 &&
      lost.lostFrames[64].ref.offset == 64,
    "lost updates stopped at built-in capacity");

  Zutcp::PktTxUpdate ackd;
  bool ackdSetup = true;
  for (unsigned packetNo = 0; packetNo < 256; ++packetNo) {
    Zutcp::SentPkt packet;
    packet.pn = packetNo;
    packet.bytes = 8;
    for (unsigned frameNo = 0; frameNo < Zutcp::SentPkt::MaxFrames; ++frameNo)
      ackdSetup &= packet.addFrame(
	  dataRef(packetNo * 8 + frameNo, 1));
    ackd.ackd(packet, packet.inFlight);
  }
  ZuCHECK(ackdSetup &&
      ackd.ackdFrames.length() == 256 * Zutcp::SentPkt::MaxFrames &&
      ackd.ackdFrames[2047].ref.offset == 2047 &&
      ackd.ackedPNsTruncated,
    "ACK updates or qlog truncation policy mismatch");
}

void cumulativeByteHistory()
{
  ZuTestScope(cumulativeByteHistory);
  Zutcp::PktTxSpace tx;
  auto packet = txRangePkt(17, 0, 100, 140);
  packet.frames[0].ref.range.offset = 7;
  packet.addFrame(Zutcp::SentFrameRef::control());
  auto control = txPkt(18, 40);
  control.frames.clear();
  control.addFrame(Zutcp::SentFrameRef::control());
  ZuCHECK(tx.add(packet) && tx.add(control), "byte ACK history setup");
  Zutcp::PktAckBatch first;
  Zutcp::PktTxUpdate update;
  ZuCHECK(tx.ackBytesBatch(40, first, 32, &update) && tx.count() == 2 &&
      tx.bytesInFlight() == 180 && !update.ackdBytes && update.ackdFrames.length() == 1,
    "partial byte ACK retains the covering packet and its flight accounting");
  const auto &part = update.ackdFrames[0].ref;
  ZuCHECK(part.offset == 0 && part.length == 40 && part.range.offset == 7 &&
      part.range.dataOffset == 0 && part.range.length == 40 && !part.fin,
    "ACK slice keeps payload coordinates independent of byte sequence coordinates");
  Zutcp::PktAckBatch duplicate, old;
  Zutcp::PktTxUpdate repeated;
  ZuCHECK(tx.ackBytesBatch(40, duplicate, 32, &repeated) &&
      tx.ackBytesBatch(10, old, 32, &repeated) && !repeated.ackdFrames.length(),
    "duplicate and older cumulative ACKs do not report byte progress again");
  Zutcp::PktAckBatch middle;
  Zutcp::PktTxUpdate progress;
  ZuCHECK(tx.ackBytesBatch(75, middle, 32, &progress) &&
      progress.ackdFrames.length() == 1 && progress.ackdFrames[0].ref.offset == 40 &&
      progress.ackdFrames[0].ref.length == 35 &&
      progress.ackdFrames[0].ref.range.offset == 47,
    "later partial ACK reports only the newly acknowledged suffix");
  ZuCHECK(tx.lose(17), "partially ACKed packet can still be lost");
  Zutcp::SentFrameRef retry;
  ZuCHECK(tx.nextRetransmit(retry) && retry.kind == Zutcp::SentFrameKind::Data &&
      retry.offset == 0 && retry.length == 100 && retry.range.offset == 7,
    "partial ACK does not trim the retained covering packet coordinates");
  ZuCHECK(tx.add(txRangePkt(99, 0, 100, 140)), "same bytes have a later send record");
  Zutcp::PktAckBatch complete;
  Zutcp::PktTxUpdate finished;
  ZuCHECK(tx.ackBytesBatch(100, complete, 32, &finished) && tx.count() == 1 &&
      tx.bytesInFlight() == 40 && finished.ackdBytes == 140 &&
      finished.ackdFrames.length() == 3 &&
      finished.ackdFrames[0].ref.offset == 75 && finished.ackdFrames[0].ref.length == 25 &&
      finished.ackdFrames[1].ref.kind == Zutcp::SentFrameKind::Control &&
      finished.ackdFrames[2].ref.offset == 0 && finished.ackdFrames[2].ref.length == 100,
    "one byte ACK retires original and repeated send records while preserving pure controls");
#ifdef ZDEBUG
  ZuCHECK(tx.verifyOutstanding(), "byte retirement preserves the outstanding-frame index");
#endif
}

void byteHistoryBatchAndFin()
{
  ZuTestScope(byteHistoryBatchAndFin);
  Zutcp::PktTxSpace tx;
  auto control = txPkt(7, 40);
  control.frames.clear();
  control.addFrame(Zutcp::SentFrameRef::control());
  ZuCHECK(tx.add(txDataPkt(1, 50, 50)) && tx.add(control) &&
      tx.add(txDataPkt(9, 0, 50)), "packet IDs need not follow byte sequence order");
  Zutcp::PktAckBatch batch;
  Zutcp::PktTxUpdate first;
  unsigned turns = 0;
  while (!tx.ackBytesBatch(50, batch, 1, &first)) ++turns;
  ZuCHECK(turns == 1 && tx.count() == 2 && batch.ackd == 1 &&
      first.ackdFrames.length() == 1 && first.ackdFrames[0].ref.offset == 0 &&
      first.ackdFrames[0].ref.length == 50,
    "bounded continuation retires bytes independently of packet-number ordering");
#ifdef ZDEBUG
  tx.resetByteAckVisits();
  Zutcp::PktAckBatch before;
  ZuCHECK(tx.ackBytesBatch(49, before, 32) && !tx.byteAckVisits(),
    "byte ACK selection must not walk future packet history");
#endif

  Zutcp::PktTxSpace fin;
  ZuCHECK(fin.add(txDataPkt(3, 100, 10, true)), "FIN history setup");
  Zutcp::PktAckBatch payload;
  Zutcp::PktTxUpdate data;
  ZuCHECK(fin.ackBytesBatch(110, payload, 32, &data) && fin.count() == 1 &&
      data.ackdFrames.length() == 1 && data.ackdFrames[0].ref.length == 10 &&
      !data.ackdFrames[0].ref.fin,
    "payload acknowledgment retains the final sequence position");
  Zutcp::PktAckBatch tail;
  Zutcp::PktTxUpdate terminal;
  ZuCHECK(fin.ackBytesBatch(111, tail, 32, &terminal) && !fin.count() &&
      !fin.bytesInFlight() && terminal.ackdFrames.length() == 1 &&
      terminal.ackdFrames[0].ref.offset == 110 && !terminal.ackdFrames[0].ref.length &&
      terminal.ackdFrames[0].ref.fin,
    "the final position is acknowledged exactly once without inventing payload bytes");
  Zutcp::PktTxSpace other;
  auto setup = txDataPkt(21, 0, 10);
  setup.space = Zutcp::PktNumSpace::Initial;
  ZuCHECK(other.add(txDataPkt(20, 0, 0)) && other.add(setup),
    "empty DATA and bootstrap history setup");
  Zutcp::PktAckBatch unrelated;
  Zutcp::PktTxUpdate untouched;
  ZuCHECK(other.ackBytesBatch(10, unrelated, 32, &untouched) && other.count() == 2 &&
      !untouched.ackdFrames.length() && !untouched.ackdBytes,
    "byte ACKs cannot retire bootstrap records or packets with no byte sequence space");
}

void byteRtoRecovery()
{
  ZuTestScope(byteRtoRecovery);
  Zutcp::PktTxSpace tx;
  tx.byteRecovery(true);
  ZuCHECK(tx.add(txDataPkt(1, 0, 10)) && tx.add(txDataPkt(2, 0, 10)) &&
      tx.add(txDataPkt(3, 10, 10)), "DATA history setup failed");
  auto control = txPkt(4);
  control.frames.clear();
  control.addFrame(Zutcp::SentFrameRef::control());
  ZuCHECK(tx.add(control) && tx.recoveryCount() == 1 &&
      tx.markPktThreshLoss(7) == 1 && tx.bytesInFlight() == 300,
    "packet threshold recovery must select controls without losing DATA");
  Zutcp::PktAckBatch batch;
  Zutcp::PktTxUpdate first, second, done;
  ZuCHECK(!tx.reclaimBytesBatch(0, batch, 1, first) && first.lostBytes == 100,
    "oldest-byte recovery must yield after its bounded first batch");
  ZuCHECK(!tx.reclaimBytesBatch(0, batch, 1, second) && second.lostBytes == 100 &&
      tx.reclaimBytesBatch(0, batch, 1, done) && !done.lostBytes &&
      tx.bytesInFlight() == 100,
    "RTO must reclaim copies of the oldest range without losing later DATA");
  Zutcp::SentFrameRef frame;
  while (tx.nextRetransmit(frame) && frame.kind != Zutcp::SentFrameKind::Data) { }
  ZuCHECK(frame.kind == Zutcp::SentFrameKind::Data && frame.offset == 0 &&
      frame.length == 10, "RTO must queue the oldest retained DATA range");
  Zutcp::PktAckBatch ack;
  Zutcp::PktTxUpdate prefix;
  ZuCHECK(tx.ackBytesBatch(4, ack, 8, &prefix) && prefix.ackdFrames.length() == 2 &&
      tx.add(txDataPkt(5, 4, 6)), "partial ACK/retransmission setup failed");
  batch = {};
  Zutcp::PktTxUpdate retry;
  ZuCHECK(tx.reclaimBytesBatch(4, batch, 8, retry) && retry.lostBytes == 100 &&
      tx.nextRetransmit(frame) && frame.offset == 4 && frame.length == 6,
    "repeated RTO after a partial ACK must select the surviving suffix");
  Zutcp::NewReno cc(1200);
  cc.sent(12000);
  cc.timeout(100);
  ZuCHECK(cc.cwnd() == 1200 && cc.ssthresh() == 6000 &&
      cc.bytesInFlight() == 12000,
    "RTO must enter one-segment slow start without freeing live flight");
  cc.release(10000);
  cc.timeout(200, true);
  ZuCHECK(cc.cwnd() == 1200 && cc.ssthresh() == 6000 && cc.bytesInFlight() == 2000,
    "repeated timeout of the same covering range must preserve ssthresh");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRecovery);
  ZuTestCall(testFrameIdentity);
  ZuTestCall(testPktReorderDuplicateLoss);
  ZuTestCall(ackdFrameStopsLossReclaim);
  ZuTestCall(testLossThresholds);
  ZuTestCall(testRetainedLossFrontier);
  ZuTestCall(outstandingLifecycleIndex);
  ZuTestCall(testPktNumSpaceAckLoss);
  ZuTestCall(testPTOReclaimUsesRetxQueue);
  ZuTestCall(testAckLeavesOnlyRetxPending);
  ZuTestCall(retxSkipsUnackdData);
  ZuTestCall(testTypedControlRefs);
  ZuTestCall(ptoProgressWithNewPackets);
  ZuTestCall(testFlowControlRetransmit);
  ZuTestCall(testPktNumSpaceBatchCursors);
  ZuTestCall(testAckManager);
  ZuTestCall(testLosslessFrameUpdates);
  ZuTestCall(cumulativeByteHistory);
  ZuTestCall(byteHistoryBatchAndFin);
  ZuTestCall(byteRtoRecovery);
}
