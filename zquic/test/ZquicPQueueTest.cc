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

ZmRef<ZiIOBuf> packet_(ZuCSpan data)
{
  ZmRef<ZiIOBuf> packet = new Zquic::PktRxBufAlloc<>{nullptr};
  fill_(packet, data);
  return packet;
}

void testRxDataClipping()
{
  ZuTestScope(testRxDataClipping);

  ZmRef<ZiIOBuf> buf = new Zquic::CryptoRxBufAlloc<>{nullptr};
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
  auto first = packet_("world");
  auto dup = packet_("WORLD");
  auto second = packet_("helloworldtails");

  q.add(new Zquic::StreamRxPQueue::Node{
    first, first->data(), first->length, nullptr, 5});
  unsigned count = q.count_();
  q.add(new Zquic::StreamRxPQueue::Node{
    dup, dup->data(), dup->length, nullptr, 5});
  ZuCHECK(q.count_() == count,
    "fully duplicate RxData mutated retained queue");

  q.add(new Zquic::StreamRxPQueue::Node{
    second, second->data(), 5, nullptr, 0});
  q.add(new Zquic::StreamRxPQueue::Node{
    second, second->data() + 10, 5, nullptr, 10});

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
    auto packet = packet_("x");
    q.add(new Zquic::StreamRxPQueue::Node{
      packet, packet->data(), packet->length, nullptr, uint64_t(i)});
  }
  ZuCHECK(!q.dequeue(), "out-of-order queue drained before gap was filled");

  auto head = packet_("x");
  q.add(new Zquic::StreamRxPQueue::Node{
    head, head->data(), head->length, nullptr, 0});

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

void testTxPktLossRequeue()
{
  ZuTestScope(testTxPktLossRequeue);

  Zquic::TxPkt p;
  p.pn = 7;
  p.sentTime = Zquic::timeUS(100);
  p.bytes = 1200;
  p.space = Zquic::PktSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  p.addFrame(Zquic::SentFrameRef::crypto(3, 9));
  ZuCHECK(p.key() == 7 && p.length() == 1,
    "TxPkt key/length mismatch");

  Zquic::SentPktTracker tracker;
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
    "lost TxPkt did not requeue frame references");
}

void testTxPktAckRanges()
{
  ZuTestScope(testTxPktAckRanges);

  Zquic::SentPktTracker tracker;
  uint64_t pns[] = { 10, 11, 13, 15 };
  for (uint64_t pn : pns) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktSpace::AppData;
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
  ZuTestCall(testTxPktLossRequeue);
  ZuTestCall(testTxPktAckRanges);
}
