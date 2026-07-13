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
  cc.ackd(1200);
  ZuCHECK(cc.cwnd() > 2400, "NewReno setup did not grow cwnd");
  cc.persistentCongestion();
  ZuCHECK(cc.cwnd() == 2400 && cc.ssthresh() == 2400,
    "persistent congestion did not collapse cwnd");

  Zquic::NewReno ecn(1200);
  ecn.sent(1200);
  ecn.ackd(1200);
  uint64_t grown = ecn.cwnd();
  ZuCHECK(ecn.congestionEventAt(1000) &&
      ecn.cwnd() == grown >> 1 &&
      !ecn.congestionEventAt(1000) &&
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

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSentPktTracker);
  ZuTestCall(testAckRangeProcessing);
  ZuTestCall(testPersistentCongestion);
  ZuTestCall(testRetransmitQueue);
  ZuTestCall(testPTOBackoff);
}
