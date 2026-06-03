//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicRecovery.hh>

#include <string.h>

using namespace ZuTestUtil;

void testSentPacketTracker()
{
  ZuTestScope(testSentPacketTracker);

  Zquic::SentPacketTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPacket{
    1, 1000, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "sent packet add failed");
  ZuCHECK(tracker.add(Zquic::SentPacket{
    2, 1001, 1200, Zquic::PacketSpace::AppData, true, true, true }),
    "PMTUD sent packet add failed");
  ZuCHECK(!tracker.add(Zquic::SentPacket{
    2, 1002, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "duplicate packet number accepted");
  ZuCHECK(tracker.bytesInFlight() == 2400, "bytes-in-flight mismatch");
  ZuCHECK(tracker.ack(1) && tracker.bytesInFlight() == 1200,
    "ACK accounting mismatch");
  ZuCHECK(tracker.lose(2) && tracker.lost() == 1 &&
    tracker.retransmittable() == 0,
    "PMTUD loss accounting mismatch");

  ZuCHECK(tracker.add(Zquic::SentPacket{
    3, 2000, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "timed sent packet add failed");
  ZuCHECK(!tracker.markTimeThresholdLoss(2500, 600),
    "time loss fired too early");
  ZuCHECK(tracker.markTimeThresholdLoss(2600, 600) == 1 &&
    tracker.lost() == 2 && tracker.retransmittable() == 1,
    "time threshold loss mismatch");
}

void testAckRangeProcessing()
{
  ZuTestScope(testAckRangeProcessing);

  Zquic::SentPacketTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPacket{
      1, 1000, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      tracker.add(Zquic::SentPacket{
      2, 1001, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      tracker.add(Zquic::SentPacket{
      3, 1002, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      tracker.add(Zquic::SentPacket{
      6, 1003, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "sent packet range setup failed");

  Zquic::AckTracker ranges;
  ZuCHECK(ranges.add(3) && ranges.add(6), "ACK range setup failed");
  unsigned lost = 0;
  ZuCHECK(tracker.ack(ranges, &lost) == 2 &&
      lost == 2 &&
      tracker.acked() == 2 &&
      tracker.lost() == 2 &&
      tracker.retransmittable() == 2 &&
      !tracker.bytesInFlight(),
    "ACK range processing/loss mismatch");
}

void testPersistentCongestion()
{
  ZuTestScope(testPersistentCongestion);

  Zquic::SentPacketTracker tracker;
  ZuCHECK(tracker.add(Zquic::SentPacket{
      1, 1000, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      tracker.add(Zquic::SentPacket{
      2, 1800, 1200, Zquic::PacketSpace::AppData, true, true, true }) &&
      tracker.add(Zquic::SentPacket{
      3, 2600, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "persistent congestion setup failed");
  ZuCHECK(tracker.lose(1) && tracker.lose(2) && tracker.lose(3),
    "persistent congestion loss setup failed");
  ZuCHECK(tracker.persistentCongestion(1500),
    "persistent congestion was not detected");
  ZuCHECK(!tracker.persistentCongestion(2000),
    "persistent congestion ignored threshold duration");

  Zquic::SentPacketTracker interrupted;
  ZuCHECK(interrupted.add(Zquic::SentPacket{
      1, 1000, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      interrupted.add(Zquic::SentPacket{
      2, 1800, 1200, Zquic::PacketSpace::AppData, true, true, false }) &&
      interrupted.add(Zquic::SentPacket{
      3, 2600, 1200, Zquic::PacketSpace::AppData, true, true, false }),
    "interrupted persistent congestion setup failed");
  ZuCHECK(interrupted.lose(1) && interrupted.ack(2) && interrupted.lose(3) &&
      !interrupted.persistentCongestion(1500),
    "ACKed packet did not interrupt persistent congestion");

  Zquic::NewReno cc(1200);
  cc.sent(1200);
  cc.acked(1200);
  ZuCHECK(cc.cwnd() > 2400, "NewReno setup did not grow cwnd");
  cc.persistentCongestion();
  ZuCHECK(cc.cwnd() == 2400 && cc.ssthresh() == 2400,
    "persistent congestion did not collapse cwnd");
}

void testRetransmitQueue()
{
  ZuTestScope(testRetransmitQueue);

  ZmRef<ZiIOBuf> buf = new Zquic::StreamBufAlloc<>{nullptr};
  memcpy(buf->data_(), "data", 4);
  buf->length = 4;

  Zquic::TxRange range{buf, 0, 4, 12};
  Zquic::SentPacket packet{
    9, 3000, 64, Zquic::PacketSpace::AppData, true, true, false };
  ZuCHECK(packet.addFrame(Zquic::SentFrameRef::stream(4, range, true)) &&
      packet.addFrame(Zquic::SentFrameRef::control()),
    "sent packet frame references were not retained");

  Zquic::SentPacketTracker tracker;
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

  Zquic::SentPacket pmtud{
    10, 4000, 1200, Zquic::PacketSpace::AppData, true, true, true };
  ZuCHECK(pmtud.addFrame(Zquic::SentFrameRef::stream(4, range, false)) &&
      tracker.add(pmtud) && tracker.lose(10) &&
      !tracker.retransmitPending(),
    "PMTUD probe loss enqueued retransmission work");
}

void testPTOBackoff()
{
  ZuTestScope(testPTOBackoff);

  Zquic::RttEstimator rtt;
  rtt.sample(1000, 0, true);
  Zquic::PTOBackoff pto;
  uint64_t t0 = pto.timeout(rtt, 25);
  pto.expired();
  ZuCHECK(pto.timeout(rtt, 25) == (t0 << 1), "PTO backoff mismatch");
  pto.reset();
  ZuCHECK(pto.count() == 0, "PTO reset mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSentPacketTracker);
  ZuTestCall(testAckRangeProcessing);
  ZuTestCall(testPersistentCongestion);
  ZuTestCall(testRetransmitQueue);
  ZuTestCall(testPTOBackoff);
}
