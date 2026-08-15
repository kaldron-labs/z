//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

void fill_(ZiIOBuf *buf, ZuBSpan data)
{
  memcpy(buf->data_(), data.data(), data.length());
  buf->skip = 0;
  buf->length = data.length();
}

ZmRef<ZiIOBuf> packet_(ZuBSpan data)
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
  buf->skip = 1;
  buf->length = 4;

  Zquic::RxData data{buf, 10};
  ZuCHECK(data.key() == 10 && data.length() == 4,
    "RxData initial key/length mismatch");
  ZuCHECK(data.clipHead(2) == 2 &&
      data.key() == 12 &&
      data.buf->skip == 3 &&
      data.length() == 2,
    "RxData head clipping mismatch");
  ZuCHECK(data.clipTail(1) == 1 &&
      data.key() == 12 &&
      data.buf->skip == 3 &&
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
  q.spans([&spans, &firstKey, &firstLength](const auto &span) {
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

void testAckRangeBound()
{
  ZuTestScope(testAckRangeBound);

  Zquic::AckTracker ack;
  for (unsigned i = 0; i < Zquic::AckTracker::MaxRetained + 10; ++i)
    ZuCHECK(ack.add((uint64_t(i) << 1) + 1),
      "sparse packet Rx mark insertion failed");

  ZuCHECK(ack.count() <= Zquic::AckTracker::MaxRetained,
    "packet Rx sparse ACK range cap was not enforced");
  ZuCHECK(!ack.contains(1),
    "packet Rx sparse ACK range cap did not drop oldest range");
  ZuCHECK(ack.contains(
      ((uint64_t(Zquic::AckTracker::MaxRetained + 9)) << 1) + 1),
    "packet Rx sparse ACK range cap did not retain newest range");
}

void testAckOfAckTrim()
{
  ZuTestScope(testAckOfAckTrim);

  Zquic::AckTracker ack;
  for (unsigned i = 0; i < 6; ++i)
    ZuCHECK(ack.add(i), "contiguous packet Rx mark insertion failed");

  ack.ackdByPeer(3);

  Zquic::AckRange range;
  ZuCHECK(ack.count() == 1 &&
      ack.range(0, range) &&
      range.first == 4 &&
      range.largest == 5,
    "ACK-of-ACK did not trim contiguous ACK range");

  ZuCHECK(ack.add(8) && ack.add(10),
    "sparse packet Rx mark insertion after ACK-of-ACK failed");
  ack.ackdByPeer(8);

  ZuCHECK(ack.count() == 2 &&
      ack.range(0, range) &&
      range.first == 8 &&
      range.largest == 8,
    "ACK-of-ACK incorrectly trimmed sparse ACK ranges");

  uint8_t b[64];
  int n = ack.writeFrame(b, sizeof(b));
  Zquic::Frame frame;
  unsigned used = 0;
  ZuCHECK(n > 0 &&
      !Zquic::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)}, frame, used) &&
      used == unsigned(n) &&
      frame.offset == 10,
    "ACK frame did not retain sparse largest after ACK-of-ACK trim");
}

void testTxPktLossRequeue()
{
  ZuTestScope(testTxPktLossRequeue);

  Zquic::TxPkt p;
  p.pn = 7;
  p.sentTime = Zquic::timeUS(100);
  p.bytes = 1200;
  p.space = Zquic::PktNumSpace::AppData;
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
    p.space = Zquic::PktNumSpace::AppData;
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
      tracker.ackd() == 3 &&
      tracker.bytesInFlight() == 100,
    "sent packet ACK range processing mismatch");
}

static Zquic::TxPkt txPkt_(
  uint64_t pn, ZuTime sentTime = {}, unsigned bytes = 100)
{
  Zquic::TxPkt p;
  p.pn = pn;
  p.sentTime = sentTime ? sentTime : Zquic::timeUS(pn * 100);
  p.bytes = bytes;
  p.space = Zquic::PktNumSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  return p;
}

void testTxPktAckDelete()
{
  ZuTestScope(testTxPktAckDelete);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZuCHECK(tracker.add(p), "sent packet add failed");
  }

  Zquic::AckRange ranges[] = { Zquic::AckRange{11, 10} };
  ZuCHECK(tracker.ack(ranges, 1) == 2 &&
      tracker.count() == 1,
    "sent packet ACK did not remove exact packet nodes");
}

void testTxPktLossRetain()
{
  ZuTestScope(testTxPktLossRetain);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 15; ++pn)
    ZuCHECK(tracker.add(txPkt_(pn)), "lost packet add failed");
  for (uint64_t pn = 10; pn < 15; ++pn)
    ZuCHECK(tracker.lose(pn), "lost packet setup failed");

  ZuCHECK(tracker.count() == 5 &&
      tracker.lost() == 5 &&
      tracker.retainedLost() == 5 &&
      tracker.retransmitPending() == 0,
    "sent packet loss did not retain exact lost packet nodes");
}

void testTxPktRetainedLostAck()
{
  ZuTestScope(testTxPktRetainedLostAck);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZuCHECK(tracker.add(p), "sent packet add failed");
  }

  ZuCHECK(tracker.lose(10) &&
      tracker.ack(10) &&
      tracker.ack(11) &&
      tracker.lost() == 1 &&
      tracker.ackd() == 1 &&
      !tracker.retainedLost() &&
      tracker.count() == 1,
    "ACK of retained lost packet changed fresh ACK accounting");
}

void testTxUnackdRangeQueue()
{
  ZuTestScope(testTxUnackdRangeQueue);

  Zquic::StreamTxPQueue q{0};
  ZuCHECK(q.add(new Zquic::StreamTxPQueue::Node{
	Zquic::TxUnackdRange{10, 10}}) == ZmPQResult::Inserted,
    "stream unackd range insert failed");
  ZuCHECK(q.clear(13, 4),
    "stream unackd range middle clear failed");

  ZmRef<Zquic::StreamTxPQueue::Node> head = q.find(10);
  ZmRef<Zquic::StreamTxPQueue::Node> tail = q.find(17);
  ZuCHECK(head && tail &&
      head->data().key() == 10 &&
      head->data().length() == 3 &&
      tail->data().key() == 17 &&
      tail->data().length() == 3 &&
      q.count_() == 2 &&
      q.length_() == 6 &&
      q.verify(),
    "stream unackd range clear did not split retained ranges");

  ZuCHECK(q.clear(10, 3) &&
      !q.find(10) &&
      q.count_() == 1 &&
      q.length_() == 3 &&
      q.verify(),
    "stream unackd range exact clear failed");
}

void testTxUnackdFinSentinel()
{
  ZuTestScope(testTxUnackdFinSentinel);

  Zquic::StreamTxPQueue dataFin{0};
  ZuCHECK(dataFin.add(new Zquic::StreamTxPQueue::Node{
	Zquic::TxUnackdRange{20, 5, true}}) == ZmPQResult::Inserted &&
      dataFin.length_() == 6,
    "stream unackd data+FIN range insert failed");
  ZuCHECK(dataFin.clear(20, 5) &&
      dataFin.count_() == 1 &&
      dataFin.length_() == 1,
    "stream unackd data ACK did not leave FIN sentinel");
  ZmRef<Zquic::StreamTxPQueue::Node> fin = dataFin.find(25);
  ZuCHECK(fin &&
      fin->data().key() == 25 &&
      fin->data().bytes == 0 &&
      fin->data().fin,
    "stream unackd FIN sentinel has wrong offset/state");
  ZuCHECK(dataFin.clear(25, 1) &&
      !dataFin.count_() &&
      !dataFin.length_() &&
      dataFin.verify(),
    "stream unackd FIN sentinel clear failed");

  Zquic::StreamTxPQueue finOnly{0};
  ZuCHECK(finOnly.add(new Zquic::StreamTxPQueue::Node{
	Zquic::TxUnackdRange{30, 0, true}}) == ZmPQResult::Inserted &&
      finOnly.clear(30, 1) &&
      !finOnly.count_() &&
      finOnly.verify(),
    "stream unackd FIN-only range clear failed");
}

void testTxUnackdFragmentation()
{
  ZuTestScope(testTxUnackdFragmentation);

  Zquic::StreamTxPQueue q{0};
  constexpr unsigned Ranges = Zquic::StreamTxPQueue::Max << 1;
  for (unsigned i = 0; i < Ranges; ++i)
    ZuCHECK(q.add(Zquic::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "fragmented stream range insert failed");
  ZuCHECK(q.count_() == Ranges &&
      q.length_() == Ranges &&
      q.find(0) &&
      q.find((uint64_t(Ranges - 1) << 1)) &&
      !q.find(1) &&
      q.verify(),
    "fragmented stream ranges did not grow beyond former capacity");

  ZuCHECK(q.add(Zquic::TxUnackdRange{1, 1}) == ZmPQResult::Inserted &&
      q.count_() == Ranges - 1 &&
      q.length_() == Ranges + 1 &&
      q.find(0)->data().length() == 3 &&
      q.verify(),
    "fragmented stream ranges did not coalesce around an inserted bridge");

  unsigned spans = 0;
  ZuCHECK(q.spans(0, 8, [&spans](const auto &) {
	++spans;
	return true;
      }) &&
      spans == 3,
    "fragmented stream range span traversal mismatch");
}

void testTxUnackdBoundaries()
{
  ZuTestScope(testTxUnackdBoundaries);

  Zquic::StreamTxPQueue q{0};
  ZuCHECK(q.add(Zquic::TxUnackdRange{uint64_t(-1), 1}) ==
	ZmPQResult::Invalid &&
      q.add(Zquic::TxUnackdRange{0, uint64_t(-1), true}) ==
	ZmPQResult::Invalid &&
      !q.clear(uint64_t(-1), 2) &&
      !q.spans(uint64_t(-1), 2, [](const auto &) { return true; }) &&
      !q.count_() &&
      !q.length_() &&
      q.verify(),
    "stream range endpoint overflow was not rejected");

  for (unsigned i = 0; i <= Zquic::StreamTxPQueue::BuiltinRanges; ++i)
    ZuCHECK(q.add(Zquic::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "stream range inline/heap transition insert failed");
  ZuCHECK(q.count_() == Zquic::StreamTxPQueue::BuiltinRanges + 1 &&
      q.verify(),
    "stream range inline/heap transition lost retained ranges");
#ifdef ZDEBUG
  ZuCHECK(q.promotions() == 1 && q.indexed(),
    "fragmented stream ranges did not promote exactly once");
  ZuCHECK(q.clear(0, 1) && q.indexed() && q.promotions() == 1,
    "indexed stream ranges demoted before full clear");
  q.clear();
  ZuCHECK(!q.indexed(),
    "full stream-range clear retained indexed representation");
#endif
}

void testTxUnackdRandomized()
{
  ZuTestScope(testTxUnackdRandomized);

  Zquic::StreamTxPQueue q{0};
  bool expected[512] = {};
  uint32_t random = 0x243f6a88U;
  for (unsigned operation = 0; operation < 4096; ++operation) {
    random = random * 1664525U + 1013904223U;
    unsigned offset = (random >> 8) % 480;
    unsigned length = ((random >> 24) & 31) + 1;
    if (random & 1) {
      ZuCHECK(q.add(Zquic::TxUnackdRange{offset, length}) ==
	  ZmPQResult::Inserted,
	"randomized stream range insert failed");
      for (unsigned i = offset; i < offset + length; ++i)
	expected[i] = true;
    } else {
      bool changed = false;
      for (unsigned i = offset; i < offset + length; ++i) {
	changed = changed || expected[i];
	expected[i] = false;
      }
      ZuCHECK(q.clear(offset, length) == changed,
	"randomized stream range clear result mismatch");
    }
    if (operation & 15) continue;
    bool observed[512] = {};
    unsigned expectedLength = 0, expectedCount = 0;
    bool inRange = false;
    for (unsigned i = 0; i < 512; ++i) {
      if (expected[i]) {
	++expectedLength;
	if (!inRange) ++expectedCount;
      }
      inRange = expected[i];
    }
    ZuCHECK(q.spans(0, 512, [&observed](const auto &range) {
	for (uint64_t i = range.key(), n = i + range.length(); i < n; ++i)
	  observed[i] = true;
	return true;
      }), "randomized stream range traversal failed");
    bool equal = true;
    for (unsigned i = 0; i < 512; ++i)
      equal = equal && observed[i] == expected[i];
    ZuCHECK(equal && q.length_() == expectedLength &&
	q.count_() == expectedCount && q.verify(),
      "randomized stream range model mismatch");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRxDataClipping);
  ZuTestCall(testRxOverlapDrop);
  ZuTestCall(testOutOfOrderDrain);
  ZuTestCall(testAckRanges);
  ZuTestCall(testAckRangeBound);
  ZuTestCall(testAckOfAckTrim);
  ZuTestCall(testTxPktLossRequeue);
  ZuTestCall(testTxPktAckRanges);
  ZuTestCall(testTxPktAckDelete);
  ZuTestCall(testTxPktLossRetain);
  ZuTestCall(testTxPktRetainedLostAck);
  ZuTestCall(testTxUnackdRangeQueue);
  ZuTestCall(testTxUnackdFinSentinel);
  ZuTestCall(testTxUnackdFragmentation);
  ZuTestCall(testTxUnackdBoundaries);
  ZuTestCall(testTxUnackdRandomized);
}
