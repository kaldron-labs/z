//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicPQueue.hh>
#include <zlib/ZquicRecovery.hh>

using namespace ZuTestUtil;

void fill_(ZiIOBuf *buf, ZuCSpan data)
{
  memcpy(buf->data_(), data.data(), data.length());
  buf->skip = 0;
  buf->length = data.length();
}

void testRxDataClipping()
{
  ZuTestScope(testRxDataClipping);

  ZmRef<ZiIOBuf> buf = new Zquic::StreamBufAlloc<>{nullptr};
  fill_(buf, "abcdef");

  Zquic::RxData data{buf, 10, 1, 4};
  ZuCHECK(data.key() == 10 && data.length() == 4,
    "RxData initial key/length mismatch");
  ZuCHECK(data.clipHead(2) == 2 &&
      data.key() == 12 &&
      data.bufOffset == 3 &&
      data.length() == 2,
    "RxData head clipping mismatch");
  ZuCHECK(data.clipTail(1) == 1 &&
      data.key() == 12 &&
      data.bufOffset == 3 &&
      data.length() == 1,
    "RxData tail clipping mismatch");
}

void testRxOverlapDrop()
{
  ZuTestScope(testRxOverlapDrop);

  Zquic::StreamRxPQueue q{0};
  ZmRef<ZiIOBuf> first = new Zquic::StreamBufAlloc<>{nullptr};
  ZmRef<ZiIOBuf> dup = new Zquic::StreamBufAlloc<>{nullptr};
  ZmRef<ZiIOBuf> second = new Zquic::StreamBufAlloc<>{nullptr};
  fill_(first, "world");
  fill_(dup, "WORLD");
  fill_(second, "helloworldtails");

  q.add(new Zquic::StreamRxPQueue::Node{
    Zquic::RxData{ZuMv(first), 5, 0, 5}});
  unsigned count = q.count_();
  q.add(new Zquic::StreamRxPQueue::Node{
    Zquic::RxData{ZuMv(dup), 5, 0, 5}});
  ZuCHECK(q.count_() == count,
    "fully duplicate RxData mutated retained queue");

  q.add(new Zquic::StreamRxPQueue::Node{
    Zquic::RxData{second, 0, 0, 5}});
  q.add(new Zquic::StreamRxPQueue::Node{
    Zquic::RxData{ZuMv(second), 10, 10, 5}});

  unsigned spans = 0;
  uint64_t firstKey = 0, firstLength = 0;
  q.spans([&](const auto &span) {
    if (!spans) {
      firstKey = span.key();
      firstLength = span.length();
    }
    ++spans;
    return true;
  });
  ZuCHECK(spans == 1 && firstKey == 0 && firstLength == 15 && q.verify(),
    "overlapping RxData did not normalize to one span");
}

void testOutOfOrderDrain()
{
  ZuTestScope(testOutOfOrderDrain);

  Zquic::StreamRxPQueue q{0};
  for (unsigned i = 1; i < 32; ++i) {
    ZmRef<ZiIOBuf> buf = new Zquic::StreamBufAlloc<>{nullptr};
    fill_(buf, "x");
    q.add(new Zquic::StreamRxPQueue::Node{
      Zquic::RxData{ZuMv(buf), uint64_t(i), 0, 1}});
  }
  ZuCHECK(!q.dequeue(), "out-of-order queue drained before gap was filled");

  ZmRef<ZiIOBuf> head = new Zquic::StreamBufAlloc<>{nullptr};
  fill_(head, "x");
  q.add(new Zquic::StreamRxPQueue::Node{
    Zquic::RxData{ZuMv(head), 0, 0, 1}});

  unsigned drained = 0;
  while (auto node = q.dequeue()) {
    ZuCHECK(node->data().key() == drained,
      "out-of-order queue drained in wrong order");
    ++drained;
  }
  ZuCHECK(drained == 32 && !q.count_() && q.head() == 32 && q.verify(),
    "out-of-order queue did not drain completely");
}

void testAckRanges()
{
  ZuTestScope(testAckRanges);

  Zquic::AckTracker ack;
  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(13) && ack.add(15),
    "packet Rx mark insertion failed");
  ZuCHECK(ack.contains(10) && ack.contains(15) && !ack.contains(12),
    "packet Rx mark duplicate/contains mismatch");
  Zquic::AckRange range;
  ZuCHECK(ack.count() == 3 &&
      ack.range(0, range) &&
      range.first == 10 &&
      range.largest == 11,
    "packet Rx range export mismatch");
}

void testTxPacketLossRequeue()
{
  ZuTestScope(testTxPacketLossRequeue);

  Zquic::TxPacket p;
  p.pn = 7;
  p.sentTime = 100;
  p.bytes = 1200;
  p.space = Zquic::PacketSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  p.addFrame(Zquic::SentFrameRef::crypto(3, 9));
  ZuCHECK(p.key() == 7 && p.length() == 1,
    "TxPacket key/length mismatch");

  Zquic::SentPacketTracker tracker;
  ZuCHECK(tracker.add(p) && tracker.lose(7),
    "sent packet queue loss setup failed");
  Zquic::SentFrameRef frame;
  ZuCHECK(tracker.nextRetransmit(frame) &&
      frame.kind == Zquic::SentFrameKind::Crypto &&
      frame.offset == 3 &&
      frame.length == 9 &&
      !tracker.nextRetransmit(frame) &&
      tracker.lost() == 1 &&
      tracker.retransmittable() == 1,
    "lost TxPacket did not requeue frame references");
}

void testTxPacketAckRanges()
{
  ZuTestScope(testTxPacketAckRanges);

  Zquic::SentPacketTracker tracker;
  uint64_t pns[] = { 10, 11, 13, 15 };
  for (uint64_t pn : pns) {
    Zquic::TxPacket p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PacketSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZuCHECK(tracker.add(p), "sent packet add failed");
  }

  Zquic::AckRange ranges[] = {
    Zquic::AckRange{11, 10},
    Zquic::AckRange{15, 15}
  };
  unsigned lost = 0;
  ZuCHECK(tracker.ack(ranges, 2, &lost) == 3 &&
      !lost &&
      tracker.acked() == 3 &&
      tracker.bytesInFlight() == 100,
    "sent packet ACK range processing mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRxDataClipping);
  ZuTestCall(testRxOverlapDrop);
  ZuTestCall(testOutOfOrderDrain);
  ZuTestCall(testAckRanges);
  ZuTestCall(testTxPacketLossRequeue);
  ZuTestCall(testTxPacketAckRanges);
}
