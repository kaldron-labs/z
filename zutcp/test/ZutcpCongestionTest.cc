//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

#include <string.h>

using namespace ZuTestUtil;

static Zutcp::SentPkt dataPacket(uint64_t id, uint64_t offset, ZuTime sent,
  unsigned bytes = 1200, bool probe = false)
{
  Zutcp::SentPkt packet;
  packet.pn = id;
  packet.sentTime = sent;
  packet.bytes = bytes;
  packet.ackEliciting = packet.inFlight = true;
  packet.pmtudProbe = probe;
  Zutcp::TxRange range;
  range.dataOffset = offset;
  range.length = 10;
  packet.addFrame(Zutcp::SentFrameRef::data(range, false));
  return packet;
}

void testPersistentCongestion()
{
  ZuTestScope(testPersistentCongestion);

  Zutcp::SentPktTracker tracker;
  ZuCHECK(tracker.add(dataPacket(1, (1 - 1) * 10, Zutcp::timeUS(1000), 1200)) &&
      tracker.add(dataPacket(2, (2 - 1) * 10, Zutcp::timeUS(1800), 1200, true)) &&
      tracker.add(dataPacket(3, (3 - 1) * 10, Zutcp::timeUS(2600), 1200)),
    "persistent congestion setup failed");
  ZuCHECK(tracker.lose(1) && tracker.lose(2) && tracker.lose(3),
    "persistent congestion loss setup failed");
  ZuCHECK(tracker.persistentCongestion(Zutcp::timeUS(1500)),
    "persistent congestion was not detected");
  ZuCHECK(!tracker.persistentCongestion(Zutcp::timeUS(2000)),
    "persistent congestion ignored threshold duration");

  Zutcp::SentPktTracker interrupted;
  ZuCHECK(interrupted.add(dataPacket(1, (1 - 1) * 10, Zutcp::timeUS(1000), 1200)) &&
      interrupted.add(dataPacket(2, (2 - 1) * 10, Zutcp::timeUS(1800), 1200)) &&
      interrupted.add(dataPacket(3, (3 - 1) * 10, Zutcp::timeUS(2600), 1200)),
    "interrupted persistent congestion setup failed");
  Zutcp::PktAckBatch ack;
  ZuCHECK(interrupted.lose(1) && interrupted.ackBytesBatch(20, ack, 8) &&
      interrupted.lose(3) &&
      !interrupted.persistentCongestion(Zutcp::timeUS(1500)),
    "ACKd packet did not interrupt persistent congestion");

  Zutcp::NewReno cc(1200);
  cc.sent(1200);
  cc.ackd(1200, 1200);
  ZuCHECK(cc.cwnd() > 2400, "NewReno setup did not grow cwnd");
  cc.persistentCongestion();
  ZuCHECK(cc.cwnd() == 2400 && cc.ssthresh() == 2400,
    "persistent congestion did not collapse cwnd");

  Zutcp::NewReno ecn(1200);
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

  ZiMultiplex mx;
  auto buf = ZutcpTest::plaintextAllocator<Zutcp::TxBufAlloc<>>(&mx)->allocBuf();
  memcpy(buf->data_(), "data", 4);
  buf->length = 4;

  Zutcp::TxRange range{new Zutcp::TxRanges::Node{buf, 12}, 0, 4, 12};
  Zutcp::SentPkt packet{
    .pn = 9,
    .sentTime = Zutcp::timeUS(3000),
    .bytes = 64,
    .ackEliciting = true,
    .inFlight = true };
  ZuCHECK(packet.addFrame(Zutcp::SentFrameRef::data( range, true)),
    "sent packet frame references were not retained");

  Zutcp::SentPktTracker tracker;
  ZuCHECK(tracker.add(packet) && tracker.lose(9) &&
      tracker.retransmittable() == 1 &&
      tracker.retransmitPending() == 1,
    "lost packet did not enqueue retransmission frames");

  Zutcp::SentFrameRef frame;
  ZuCHECK(tracker.nextRetransmit(frame) &&
      frame.kind == Zutcp::SentFrameKind::Data &&
      frame.offset == 12 &&
      frame.length == 4 &&
      frame.fin &&
      frame.range.storage->pending.ptr() == buf.ptr() &&
      frame.range.dataOffset == 12 &&
      frame.range.offset == 0 && frame.range.length == 4,
    "connection data retransmission frame reference mismatch");
  ZuCHECK(!tracker.nextRetransmit(frame), "one original queues exactly one byte range");

  Zutcp::SentPkt pmtud{
    .pn = 10,
    .sentTime = Zutcp::timeUS(4000),
    .bytes = 1200,
    .ackEliciting = true,
    .inFlight = true,
    .pmtudProbe = true };
  ZuCHECK(pmtud.addFrame(Zutcp::SentFrameRef::data( range, false)) &&
      tracker.add(pmtud) && tracker.lose(10) &&
      !tracker.retransmitPending(),
    "PMTUD probe loss enqueued retransmission work");
}

void testRecoveryEpochs()
{
  ZuTestScope(testRecoveryEpochs);
  Zutcp::NewReno cc{1200};
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
  cc = Zutcp::NewReno{1200};
  ZuCHECK(!cc.recoveryStartTime() && !cc.bytesInFlight() && cc.cwnd() == 12000,
    "runtime controller reset clears the recovery boundary");
}

void testAckEpochClassification()
{
  ZuTestScope(testAckEpochClassification);
  // Includes equality and mixed old/new accepted byte transmissions.
  for (unsigned mode = 0; mode < 3; ++mode) {
    Zutcp::PktTxSpace tx;
    Zutcp::NewReno cc{1200};
    cc.congestionEventAt(100, 1000);
    for (unsigned pn = 1; pn <= 3; ++pn) {
      uint64_t sent = mode == 0 ? 999 : mode == 1 ? 1001 : 999 + pn;
      ZuCHECK(tx.add(dataPacket(pn, (pn - 1) * 10, Zutcp::timeUS(sent))),
	"epoch original setup");
      cc.sent(1200);
    }
    Zutcp::PktAckBatch batch;
    Zutcp::PktTxUpdate update;
    ZuCHECK(tx.ackBytesBatch(30, batch, 16, &update, cc.recoveryStartTime()),
      "epoch ACK traversal");
    uint64_t eligible = mode == 0 ? 0 : mode == 1 ? 3600 : 2400;
    ZuCHECK(update.normalAckdBytes == 3600 && update.eligibleAckdBytes == eligible,
      "per-packet eligibility must survive inFlight mutation");
    cc.ackd(update.normalAckdBytes, update.eligibleAckdBytes);
    ZuCHECK(!cc.bytesInFlight() && cc.cwnd() == 6000 + eligible/5,
      "mixed epoch ACK retirement and congestion avoidance growth");
    Zutcp::PktAckBatch duplicate;
    Zutcp::PktTxUpdate again;
    tx.ackBytesBatch(30, duplicate, 16, &again, cc.recoveryStartTime());
    ZuCHECK(!again.normalAckdBytes && !again.eligibleAckdBytes,
      "duplicate ACK cannot retire or grow twice");
  }
  Zutcp::NewReno cc{1200};
  cc.congestionEventAt(1, 10);
  cc.ackd(0, 0);
  ZuCHECK(cc.cwnd() == 6000, "zero eligible bytes cannot take minimum increment");
  Zutcp::SentPkt probe{
    .sentTime = Zutcp::timeUS(20), .bytes = 1200,
    .inFlight = true, .pmtudProbe = true };
  Zutcp::PktTxUpdate update;
  update.ackd(probe, true, 10);
  ZuCHECK(update.pmtudAckdBytes == 1200 && !update.eligibleAckdBytes,
    "PMTUD ACK bytes are retirement-only");
  probe.lost = true;
  Zutcp::PktTxUpdate late;
  late.ackd(probe, false, 10);
  ZuCHECK(!late.pmtudAckdBytes && !late.normalAckdBytes,
    "late ACK cannot retire lost bytes twice");
}

void testAckResponseOrdering()
{
  ZuTestScope(testAckResponseOrdering);
  for (bool ecn : {false, true}) {
    Zutcp::PktTxSpace tx;
    Zutcp::NewReno cc{1200};
    ZuCHECK(tx.add(dataPacket(1, 0, Zutcp::timeUS(100))) &&
	tx.add(dataPacket(4, 10, Zutcp::timeUS(400))), "same ACK originals");
    cc.sent(2400);
    Zutcp::PktAckBatch ack;
    Zutcp::PktTxUpdate update;
    tx.ackBytesBatch(10, ack, 16, &update);
    Zutcp::AckGrowth growth;
    uint64_t classifiedEpoch = cc.epoch();
    if (ecn) cc.congestionEventAt(100, 1000);
    growth.stage(cc, classifiedEpoch, update.eligibleAckdBytes);
    cc.release(update.normalAckdBytes);
    Zutcp::PktSACKLossBatch loss;
    Zutcp::PktTxUpdate losses;
    tx.loseSACKHolesBatch(20, loss, 16, losses);
    cc.lostAt(losses.normalLostBytes,
      uint64_t(losses.normalLostSentTime.microsecs()), 1001);
    growth.commit(cc);
    ZuCHECK(cc.cwnd() == 6000 && !cc.bytesInFlight(),
      "same ACK loss/ECE precedes growth and shares the accepted-flight epoch");
  }

  // An intervening congestion event invalidates only credits already collected.
  Zutcp::NewReno cc{1200};
  Zutcp::AckGrowth growth;
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
  growth = Zutcp::AckGrowth{};
  growth.commit(cc);
  ZuCHECK(cc.cwnd() == 2400, "abandoned work cannot commit growth");
}

void testAckContinuationEpoch()
{
  ZuTestScope(testAckContinuationEpoch);
  Zutcp::PktTxSpace tx;
  Zutcp::NewReno cc{1200};
  // Original1 retires before recovery, original2 precedes its boundary,
  // and original3 is transmitted after the intervening event.
  for (unsigned pn = 1; pn <= 3; ++pn) {
    ZuCHECK(tx.add(dataPacket(pn, (pn - 1) * 10,
      Zutcp::timeUS(pn == 3 ? 1100 : 100 * pn))), "continuation originals");
    cc.sent(1200);
  }
  Zutcp::PktAckBatch batch;
  Zutcp::AckGrowth growth;
  for (unsigned turn = 0; turn < 3; ++turn) {
    if (turn == 1) cc.congestionEventAt(100, 1000);
    Zutcp::PktTxUpdate update;
    tx.ackBytesBatch(30, batch, 1, &update, cc.recoveryStartTime());
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
  Zutcp::PktTxUpdate end;
  ZuCHECK(tx.ackBytesBatch(30, batch, 1, &end, cc.recoveryStartTime()) && !end.normalAckdBytes,
    "final continuation cannot retire twice");
}

void testSplitLossEpoch()
{
  ZuTestScope(testSplitLossEpoch);
  Zutcp::PktTxSpace tx;
  Zutcp::NewReno cc{1200};
  for (unsigned id = 1; id <= 4; ++id) {
    ZuCHECK(tx.add(dataPacket(id, (id - 1) * 10, Zutcp::timeUS(100 * id))),
      "split byte loss setup");
    cc.sent(1200);
  }
  Zutcp::PktSACKLossBatch batch;
  unsigned turns = 0;
  bool done = false;
  while (!done && turns < 6) {
    Zutcp::PktTxUpdate update;
    done = tx.loseSACKHolesBatch(40, batch, 1, update);
    if (update.normalLostBytes)
      cc.lostAt(update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()), 1000 + turns);
    ++turns;
  }
  ZuCHECK(done && turns == 5 && cc.cwnd() == 6000 && !cc.bytesInFlight(),
    "bounded loss continuations retire once and share one congestion response");
}

void testClassicECNCongestion() {
  ZuTestScope(testClassicECNCongestion);
  Zutcp::NewReno cc{1200};
  cc.sent(1200);
  bool reduced = cc.ecnEventAt(100,200);
  auto window = cc.cwnd(), epoch = cc.epoch();
  cc.lostAt(1200,100,201);
  ZuCHECK(reduced && window == 6000 && cc.cwnd() == window && cc.epoch() == epoch,
    "ECN and loss from one accepted flight must share the existing congestion epoch");
  Zutcp::NewReno small{1200};
  small.persistentCongestion();
  ZuCHECK(small.ecnEventAt(100,200) && small.cwnd() == 1200 && small.ssthresh() == 2400,
    "ECN halves two-MSS cwnd to one while preserving the independent ssthresh floor");
  cc.timeout(300);
  epoch = cc.epoch();
  ZuCHECK(cc.cwnd() == 1200 && !cc.ecnEventAt(200,301) && cc.epoch() == epoch,
    "old ECE after RTO cannot reduce a second time or raise the one-MSS window");
  ZuCHECK(cc.ecnEventAt(400,500) && cc.cwnd() == 1200 && cc.ssthresh() == 2400,
    "a later flight's ECE records its new epoch without increasing one-MSS cwnd");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testClassicECNCongestion);
  ZuTestCall(testSplitLossEpoch);
  ZuTestCall(testAckContinuationEpoch);
  ZuTestCall(testAckResponseOrdering);
  ZuTestCall(testRecoveryEpochs);
  ZuTestCall(testAckEpochClassification);
  ZuTestCall(testPersistentCongestion);
  ZuTestCall(testRetransmitQueue);
}
