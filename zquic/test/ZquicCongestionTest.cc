//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

#include <string.h>

using namespace ZuTestUtil;

void testSentPktTracker()
{
  ZuTestScope(testSentPktTracker);

  Zquic::SentPktTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPkt{
	.pn = 1,
	.sentTime = Zquic::timeUS(1000),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "sent packet add failed");
  ZuCHECK(tracker.add(Zquic::SentPkt{
	.pn = 2,
	.sentTime = Zquic::timeUS(1001),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true,
	.pmtudProbe = true }),
    "PMTUD sent packet add failed");
  ZuCHECK(!tracker.add(Zquic::SentPkt{
	.pn = 2,
	.sentTime = Zquic::timeUS(1002),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "duplicate packet number accepted");
  ZuCHECK(tracker.bytesInFlight() == 2400, "bytes-in-flight mismatch");
  ZuCHECK(tracker.ack(1) && tracker.bytesInFlight() == 1200,
    "ACK accounting mismatch");
  ZuCHECK(tracker.lose(2) && tracker.lost() == 1 &&
    tracker.retransmittable() == 0,
    "PMTUD loss accounting mismatch");

  ZuCHECK(tracker.add(Zquic::SentPkt{
	.pn = 3,
	.sentTime = Zquic::timeUS(2000),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "timed sent packet add failed");
  ZuCHECK(!tracker.markTimeThreshLoss(
      4, Zquic::timeUS(2500), Zquic::timeUS(600)),
    "time loss fired too early");
  ZuCHECK(tracker.markTimeThreshLoss(
      4, Zquic::timeUS(2600), Zquic::timeUS(600)) == 1 &&
    tracker.lost() == 2 && tracker.retransmittable() == 1,
    "time threshold loss mismatch");
}

void testAckRangeProcessing()
{
  ZuTestScope(testAckRangeProcessing);

  Zquic::SentPktTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPkt{
	.pn = 1,
	.sentTime = Zquic::timeUS(1000),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      tracker.add(Zquic::SentPkt{
	.pn = 2,
	.sentTime = Zquic::timeUS(1001),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      tracker.add(Zquic::SentPkt{
	.pn = 3,
	.sentTime = Zquic::timeUS(1002),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      tracker.add(Zquic::SentPkt{
	.pn = 6,
	.sentTime = Zquic::timeUS(1003),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "sent packet range setup failed");

  Zquic::AckTracker ranges;
  ZuCHECK(ranges.add(3) && ranges.add(6), "ACK range setup failed");
  unsigned lost = 0;
  ZuCHECK(tracker.ack(ranges, &lost) == 2 &&
      lost == 2 &&
      tracker.ackd() == 2 &&
      tracker.lost() == 2 &&
      tracker.retransmittable() == 2 &&
      !tracker.bytesInFlight(),
    "ACK range processing/loss mismatch");
}

void testPersistentCongestion()
{
  ZuTestScope(testPersistentCongestion);

  Zquic::SentPktTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPkt{
	.pn = 1,
	.sentTime = Zquic::timeUS(1000),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      tracker.add(Zquic::SentPkt{
	.pn = 2,
	.sentTime = Zquic::timeUS(1800),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true,
	.pmtudProbe = true }) &&
      tracker.add(Zquic::SentPkt{
	.pn = 3,
	.sentTime = Zquic::timeUS(2600),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "persistent congestion setup failed");
  ZuCHECK(tracker.lose(1) && tracker.lose(2) && tracker.lose(3),
    "persistent congestion loss setup failed");
  ZuCHECK(tracker.persistentCongestion(Zquic::timeUS(1500)),
    "persistent congestion was not detected");
  ZuCHECK(!tracker.persistentCongestion(Zquic::timeUS(2000)),
    "persistent congestion ignored threshold duration");

  Zquic::SentPktTracker interrupted;
  ZuCHECK(interrupted.add(Zquic::SentPkt{
	.pn = 1,
	.sentTime = Zquic::timeUS(1000),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      interrupted.add(Zquic::SentPkt{
	.pn = 2,
	.sentTime = Zquic::timeUS(1800),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }) &&
      interrupted.add(Zquic::SentPkt{
	.pn = 3,
	.sentTime = Zquic::timeUS(2600),
	.bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true,
	.inFlight = true }),
    "interrupted persistent congestion setup failed");
  ZuCHECK(interrupted.lose(1) && interrupted.ack(2) && interrupted.lose(3) &&
      !interrupted.persistentCongestion(Zquic::timeUS(1500)),
    "ACKd packet did not interrupt persistent congestion");

  Zquic::NewReno cc(1200);
  cc.sent(1200);
  cc.ackd(1200, 1200);
  ZuCHECK(cc.cwnd() > 2400, "NewReno setup did not grow cwnd");
  cc.persistentCongestion();
  ZuCHECK(cc.cwnd() == 2400 && cc.ssthresh() == 2400,
    "persistent congestion did not collapse cwnd");

  Zquic::NewReno ecn(1200);
  ecn.sent(1200);
  ecn.ackd(1200, 1200);
  uint64_t grown = ecn.cwnd();
  ZuCHECK(ecn.congestionEventAt(1000, 2000) &&
      ecn.cwnd() == grown >> 1 &&
      !ecn.congestionEventAt(1000, 2000) &&
      ecn.cwnd() == grown >> 1,
    "NewReno ECN congestion event did not reduce cwnd once per epoch");
}

void testRetransmitQueue()
{
  ZuTestScope(testRetransmitQueue);

  ZmRef<ZiIOBuf> buf = new Zquic::StreamTxBufAlloc<>{nullptr};
  memcpy(buf->data_(), "data", 4);
  buf->length = 4;

  Zquic::TxRange range{buf, 0, 4, 12};
  Zquic::SentPkt packet{
    .pn = 9,
    .sentTime = Zquic::timeUS(3000),
    .bytes = 64,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true };
  ZuCHECK(packet.addFrame(Zquic::SentFrameRef::stream(4, range, true)) &&
      packet.addFrame(Zquic::SentFrameRef::control()),
    "sent packet frame references were not retained");

  Zquic::SentPktTracker tracker;
  ZuCHECK(tracker.add(packet) && tracker.lose(9) &&
      tracker.retransmittable() == 1 &&
      tracker.retransmitPending() == 2,
    "lost packet did not enqueue retransmission frames");

  Zquic::SentFrameRef frame;
  ZuCHECK(tracker.nextRetransmit(frame) &&
      frame.kind == Zquic::SentFrameKind::Stream &&
      frame.streamID == 4 &&
      frame.offset == 12 &&
      frame.length == 4 &&
      frame.fin &&
      frame.range.buf.ptr() == buf.ptr(),
    "stream retransmission frame reference mismatch");
  ZuCHECK(tracker.nextRetransmit(frame) &&
      frame.kind == Zquic::SentFrameKind::Control &&
      !tracker.nextRetransmit(frame),
    "control retransmission queue order mismatch");

  Zquic::SentPkt pmtud{
    .pn = 10,
    .sentTime = Zquic::timeUS(4000),
    .bytes = 1200,
    .space = Zquic::PktNumSpace::AppData,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true };
  ZuCHECK(pmtud.addFrame(Zquic::SentFrameRef::stream(4, range, false)) &&
      tracker.add(pmtud) && tracker.lose(10) &&
      !tracker.retransmitPending(),
    "PMTUD probe loss enqueued retransmission work");
}

void testPTOBackoff()
{
  ZuTestScope(testPTOBackoff);

  Zquic::RttEstimator rtt;
  rtt.sample(Zquic::timeUS(1000), ZuTime{0}, true);
  Zquic::PTOBackoff pto;
  ZuTime t0 = pto.timeout(rtt, Zquic::timeUS(25));
  pto.expired();
  ZuCHECK(pto.timeout(rtt, Zquic::timeUS(25)) == Zquic::timePow2(t0, 1),
    "PTO backoff mismatch");
  pto.reset();
  ZuCHECK(pto.count() == 0, "PTO reset mismatch");
}

void testRecoveryEpochs()
{
  ZuTestScope(testRecoveryEpochs);
  Zquic::NewReno cc{1200};
  cc.sent(8400);
  cc.lostAt(1200, 100, 1000);
  ZuCHECK(cc.cwnd() == 6000 && cc.recoveryStartTime() == 1000,
    "recovery boundary must be the event time");
  cc.lostAt(1200, 90, 1100);
  cc.lostAt(1200, 200, 1200);
  cc.lostAt(1200, 1000, 1300);
  ZuCHECK(cc.cwnd() == 6000 && cc.bytesInFlight() == 3600,
    "older, later pre-recovery, and equal-boundary losses retire without reduction");
  ZuCHECK(!cc.congestionEventAt(900, 1400) && cc.cwnd() == 6000,
    "ECN and loss for the same flight must share recovery");
  cc.lostAt(1200, 1001, 1500);
  ZuCHECK(cc.cwnd() == 3000 && cc.recoveryStartTime() == 1500,
    "a post-boundary transmission starts a new recovery");
  cc.lostAt(1200, 1600, 1700, true);
  ZuCHECK(cc.cwnd() == 3000 && cc.recoveryStartTime() == 1500,
    "PMTUD loss retires without a congestion response");
  cc.lostAt(1200, 1600, 1800);
  ZuCHECK(cc.cwnd() == 2400 && !cc.bytesInFlight(),
    "minimum window and exactly-once loss retirement");
  uint64_t epoch = cc.epoch();
  cc.persistentCongestion();
  ZuCHECK(cc.epoch() != epoch, "persistent congestion invalidates staged ACK growth");
  cc = Zquic::NewReno{1200};
  ZuCHECK(!cc.recoveryStartTime() && !cc.bytesInFlight() && cc.cwnd() == 12000,
    "runtime controller reset clears the recovery boundary");
}

void testAckEpochClassification()
{
  ZuTestScope(testAckEpochClassification);
  // Includes equality, old/new bytes, and a non-flight ACK-only packet.
  for (unsigned mode = 0; mode < 3; ++mode) {
    Zquic::PktTxSpace tx;
    Zquic::NewReno cc{1200};
    cc.congestionEventAt(100, 1000);
    for (unsigned pn = 1; pn <= 3; ++pn) {
      uint64_t sent = mode == 0 ? 999 : mode == 1 ? 1001 : 999 + pn;
      ZuCHECK(tx.add(Zquic::SentPkt{
	.pn = pn, .sentTime = Zquic::timeUS(sent), .bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true, .inFlight = true }), "epoch packet setup");
      cc.sent(1200);
    }
    ZuCHECK(tx.add(Zquic::SentPkt{
      .pn = 4, .sentTime = Zquic::timeUS(1002), .bytes = 40,
      .space = Zquic::PktNumSpace::AppData }), "ACK-only setup");
    Zquic::AckRange range{4, 1};
    Zquic::PktAckBatch batch;
    Zquic::PktTxUpdate update;
    ZuCHECK(tx.ackBatch(&range, 1, batch, 16,
      Zquic::PktNumSpace::AppData, &update, cc.recoveryStartTime()),
      "epoch ACK traversal");
    uint64_t eligible = mode == 0 ? 0 : mode == 1 ? 3600 : 2400;
    ZuCHECK(update.normalAckdBytes == 3600 && update.eligibleAckdBytes == eligible,
      "per-packet eligibility must survive inFlight mutation");
    cc.ackd(update.normalAckdBytes, update.eligibleAckdBytes);
    ZuCHECK(!cc.bytesInFlight() && cc.cwnd() == 6000 + eligible/5,
      "mixed epoch ACK retirement and congestion avoidance growth");
    Zquic::PktAckBatch duplicate;
    Zquic::PktTxUpdate again;
    tx.ackBatch(&range, 1, duplicate, 16, Zquic::PktNumSpace::AppData,
      &again, cc.recoveryStartTime());
    ZuCHECK(!again.normalAckdBytes && !again.eligibleAckdBytes,
      "duplicate ACK cannot retire or grow twice");
  }
  Zquic::NewReno cc{1200};
  cc.congestionEventAt(1, 10);
  cc.ackd(0, 0);
  ZuCHECK(cc.cwnd() == 6000, "zero eligible bytes cannot take minimum increment");
  Zquic::SentPkt probe{
    .sentTime = Zquic::timeUS(20), .bytes = 1200,
    .inFlight = true, .pmtudProbe = true };
  Zquic::PktTxUpdate update;
  update.ackd(probe, true, 10);
  ZuCHECK(update.pmtudAckdBytes == 1200 && !update.eligibleAckdBytes,
    "PMTUD ACK bytes are retirement-only");
  probe.lost = true;
  Zquic::PktTxUpdate late;
  late.ackd(probe, false, 10);
  ZuCHECK(!late.pmtudAckdBytes && !late.normalAckdBytes,
    "late ACK cannot retire lost bytes twice");
}

void testAckResponseOrdering()
{
  ZuTestScope(testAckResponseOrdering);
  for (bool ecn : {false, true}) {
    Zquic::PktTxSpace tx;
    Zquic::NewReno cc{1200};
    for (unsigned pn : {1, 4}) {
      ZuCHECK(tx.add(Zquic::SentPkt{
	.pn = pn, .sentTime = Zquic::timeUS(100 * pn), .bytes = 1200,
	.space = Zquic::PktNumSpace::AppData,
	.ackEliciting = true, .inFlight = true }), "same ACK setup");
      cc.sent(1200);
    }
    Zquic::AckRange range{4, 4};
    Zquic::PktAckBatch ack;
    Zquic::PktTxUpdate update;
    tx.ackBatch(&range, 1, ack, 16, Zquic::PktNumSpace::AppData, &update);
    Zquic::AckGrowth growth;
    uint64_t classifiedEpoch = cc.epoch();
    if (ecn) cc.congestionEventAt(400, 1000);
    growth.stage(cc, classifiedEpoch, update.eligibleAckdBytes);
    cc.release(update.normalAckdBytes);
    Zquic::PktLossBatch loss;
    Zquic::PktTxUpdate losses;
    tx.markPktThreshLossBatch(4, 3, loss, 16, &losses);
    cc.lostAt(losses.normalLostBytes,
      uint64_t(losses.normalLostSentTime.microsecs()), 1001);
    growth.commit(cc);
    ZuCHECK(cc.cwnd() == 6000 && !cc.bytesInFlight(),
      "same ACK loss/ECN must precede growth (never halve 13200 to 6600)");
  }

  // An event in a different packet space between ACK continuations shares
  // this controller and invalidates only credits already collected.
  Zquic::NewReno cc{1200};
  Zquic::AckGrowth growth;
  growth.stage(cc, cc.epoch(), 1200);
  cc.congestionEventAt(100, 1000);
  growth.stage(cc, cc.epoch(), 1200); // a transmission after the new boundary
  growth.commit(cc);
  ZuCHECK(cc.cwnd() == 6240, "later continuation must use the current epoch");
  growth.stage(cc, cc.epoch(), 1200);
  cc.persistentCongestion();
  growth.commit(cc);
  ZuCHECK(cc.cwnd() == 2400, "persistent congestion invalidates deferred credits");
  // Reset/discard abandon the work object; there is no delayed retirement.
  growth.stage(cc, cc.epoch(), 1200);
  growth = Zquic::AckGrowth{};
  growth.commit(cc);
  ZuCHECK(cc.cwnd() == 2400, "abandoned work cannot commit growth");
}

void testAckContinuationEpoch()
{
  ZuTestScope(testAckContinuationEpoch);
  Zquic::PktTxSpace tx;
  Zquic::NewReno cc{1200};
  // PN1 is ACKed before recovery, PN2 is sent before its boundary, and
  // PN3 is transmitted after the intervening event.
  for (unsigned pn = 1; pn <= 3; ++pn) {
    ZuCHECK(tx.add(Zquic::SentPkt{
      .pn = pn, .sentTime = Zquic::timeUS(pn == 3 ? 1100 : 100 * pn),
      .bytes = 1200, .space = Zquic::PktNumSpace::AppData,
      .ackEliciting = true, .inFlight = true }), "continuation setup");
    cc.sent(1200);
  }
  Zquic::AckRange range{3, 1};
  Zquic::PktAckBatch batch;
  Zquic::AckGrowth growth;
  for (unsigned turn = 0; turn < 3; ++turn) {
    if (turn == 1) cc.congestionEventAt(100, 1000);
    Zquic::PktTxUpdate update;
    tx.ackBatch(&range, 1, batch, 1, Zquic::PktNumSpace::AppData,
      &update, cc.recoveryStartTime());
    ZuCHECK(update.normalAckdBytes == 1200 &&
      update.eligibleAckdBytes == (turn == 1 ? 0 : 1200),
      "each continuation must classify with the current boundary");
    growth.stage(cc, cc.epoch(), update.eligibleAckdBytes);
    cc.release(update.normalAckdBytes);
    ZuCHECK(cc.bytesInFlight() == 2400 - turn * 1200,
      "each continuation retires exactly once");
  }
  growth.commit(cc);
  ZuCHECK(cc.cwnd() == 6240, "only the post-boundary continuation can grow");
  Zquic::PktTxUpdate end;
  ZuCHECK(tx.ackBatch(&range, 1, batch, 1, Zquic::PktNumSpace::AppData,
    &end, cc.recoveryStartTime()) && !end.normalAckdBytes,
    "final continuation cannot retire twice");
}

void testSplitLossEpoch()
{
  ZuTestScope(testSplitLossEpoch);
  for (bool timed : {false, true}) {
    Zquic::PktTxSpace spaces[2];
    Zquic::NewReno cc{1200};
    for (unsigned space = 0; space < 2; ++space) {
      for (unsigned pn = 1; pn <= 2; ++pn) {
	ZuCHECK(spaces[space].add(Zquic::SentPkt{
	  .pn = pn, .sentTime = Zquic::timeUS(100 * pn), .bytes = 1200,
	  .space = Zquic::PktNumSpace::T(space),
	  .ackEliciting = true, .inFlight = true }), "split loss setup");
	cc.sent(1200);
      }
      Zquic::PktLossBatch batch;
      bool done = false;
      unsigned turns = 0;
      while (!done && turns < 4) {
	Zquic::PktTxUpdate update;
	done = timed ? spaces[space].markTimeThreshLossBatch(
	  5, Zquic::timeUS(900), Zquic::timeUS(500), batch, 1, &update) :
	  spaces[space].markPktThreshLossBatch(5, 3, batch, 1, &update);
	if (update.normalLostBytes)
	  cc.lostAt(update.normalLostBytes,
	    uint64_t(update.normalLostSentTime.microsecs()), 1000 + turns);
	++turns;
      }
      ZuCHECK(done && turns >= 2 && cc.cwnd() == 6000,
	"split loss scans and packet spaces share one recovery response");
    }
    ZuCHECK(!cc.bytesInFlight(), "split loss retires each byte exactly once");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAckContinuationEpoch);
  ZuTestCall(testSplitLossEpoch);
  ZuTestCall(testAckResponseOrdering);
  ZuTestCall(testRecoveryEpochs);
  ZuTestCall(testAckEpochClassification);
  ZuTestCall(testSentPktTracker);
  ZuTestCall(testAckRangeProcessing);
  ZuTestCall(testPersistentCongestion);
  ZuTestCall(testRetransmitQueue);
  ZuTestCall(testPTOBackoff);
}
