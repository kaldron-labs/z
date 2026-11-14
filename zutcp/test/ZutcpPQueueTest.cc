//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zutcp.hh>
#include "ZutcpTestAlloc.hh"

using namespace ZuTestUtil;

static Zutcp::SentFrameRef dataRef(uint64_t offset, uint32_t length)
{
  Zutcp::TxRange range;
  range.dataOffset = offset;
  range.length = length;
  return Zutcp::SentFrameRef::data(range, false);
}


#define ZUTCP_CHECK_RT(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

void fill_(ZiIOBuf *buf, ZuBSpan data)
{
  memcpy(buf->data_(), data.data(), data.length());
  buf->skip = 0;
  buf->length = data.length();
}

ZmRef<ZiIOBuf> packet_(ZuBSpan data)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return ZutcpTest::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  fill_(packet, data);
  return packet;
}



void testRxOverlapDrop()
{
  ZuTestScope(testRxOverlapDrop);

  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue q{0};
  auto first = packet_("world");
  auto dup = packet_("WORLD");
  auto second = packet_("helloworldtails");

  q.add(new Zutcp::DataRxPQueue::Node{
    first, first->data(), first->length, allocator, nullptr, 5});
  unsigned count = q.count_();
  q.add(new Zutcp::DataRxPQueue::Node{
    dup, dup->data(), dup->length, allocator, nullptr, 5});
  ZuCHECK(q.count_() == count,
    "fully duplicate STREAM data mutated retained queue");

  q.add(new Zutcp::DataRxPQueue::Node{
    second, second->data(), 5, allocator, nullptr, 0});
  q.add(new Zutcp::DataRxPQueue::Node{
    second, second->data() + 10, 5, allocator, nullptr, 10});

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
    "overlapping STREAM data did not normalize to one span");
}

void testOutOfOrderDrain()
{
  ZuTestScopeRT(testOutOfOrderDrain);

  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue q{0};
  for (unsigned i = 1; i < 32; ++i) {
    auto packet = packet_("x");
    q.add(new Zutcp::DataRxPQueue::Node{
      packet, packet->data(), packet->length, allocator, nullptr, uint64_t(i)});
  }
  ZUTCP_CHECK_RT(!q.dequeue(), "out-of-order queue drained before gap was filled");

  auto head = packet_("x");
  q.add(new Zutcp::DataRxPQueue::Node{
    head, head->data(), head->length, allocator, nullptr, 0});

  unsigned drained = 0;
  while (auto node = q.dequeue()) {
    ZUTCP_CHECK_RT(node->data().key() == drained,
      "out-of-order queue drained in wrong order");
    ++drained;
  }
  ZUTCP_CHECK_RT(drained == 32 && !q.count_() && q.head() == 32 && q.verify(),
    "out-of-order queue did not drain completely");
}

void testAckRanges()
{
  ZuTestScope(testAckRanges);

  Zutcp::AckTracker ack;
  ZuCHECK(ack.add(10) && ack.add(11) && ack.add(13) && ack.add(15),
    "packet Rx mark insertion failed");
  ZuCHECK(ack.contains(10) && ack.contains(15) && !ack.contains(12),
    "packet Rx mark duplicate/contains mismatch");
  Zutcp::AckRange range;
  ZuCHECK(ack.count() == 3 &&
      ack.range(0, range) &&
      range.first == 10 &&
      range.largest == 11,
    "packet Rx range export mismatch");
}

void testAckRangeBound()
{
  ZuTestScopeRT(testAckRangeBound);

  Zutcp::AckTracker ack;
  for (unsigned i = 0; i < Zutcp::AckTracker::MaxRetained + 10; ++i)
    ZUTCP_CHECK_RT(ack.add((uint64_t(i) << 1) + 1),
      "sparse packet Rx mark insertion failed");

  ZUTCP_CHECK_RT(ack.count() <= Zutcp::AckTracker::MaxRetained,
    "packet Rx sparse ACK range cap was not enforced");
  ZUTCP_CHECK_RT(!ack.contains(1),
    "packet Rx sparse ACK range cap did not drop oldest range");
  ZUTCP_CHECK_RT(ack.contains(
      ((uint64_t(Zutcp::AckTracker::MaxRetained + 9)) << 1) + 1),
    "packet Rx sparse ACK range cap did not retain newest range");
}

void testAckOfAckTrim()
{
  ZuTestScopeRT(testAckOfAckTrim);

  Zutcp::AckTracker ack;
  for (unsigned i = 0; i < 6; ++i)
    ZUTCP_CHECK_RT(ack.add(i), "contiguous packet Rx mark insertion failed");

  ack.ackdByPeer(3);

  Zutcp::AckRange range;
  ZUTCP_CHECK_RT(ack.count() == 1 &&
      ack.range(0, range) &&
      range.first == 4 &&
      range.largest == 5,
    "ACK-of-ACK did not trim contiguous ACK range");

  ZUTCP_CHECK_RT(ack.add(8) && ack.add(10),
    "sparse packet Rx mark insertion after ACK-of-ACK failed");
  ack.ackdByPeer(8);

  ZUTCP_CHECK_RT(ack.count() == 2 &&
      ack.range(0, range) &&
      range.first == 8 &&
      range.largest == 8,
    "ACK-of-ACK incorrectly trimmed sparse ACK ranges");

  uint8_t b[64];
  int n = ack.writeFrame(b, sizeof(b));
  Zutcp::Frame frame;
  unsigned used = 0;
  ZUTCP_CHECK_RT(n > 0 &&
      !Zutcp::FrameCodec::parse(
	ZuBSpan{b, unsigned(n)}, frame, used) &&
      used == unsigned(n) &&
      frame.offset == 10,
    "ACK frame did not retain sparse largest after ACK-of-ACK trim");
}

void testTxPktLossRequeue()
{
  ZuTestScope(testTxPktLossRequeue);

  Zutcp::TxPkt p;
  p.pn = 7;
  p.sentTime = Zutcp::timeUS(100);
  p.bytes = 1200;
  p.space = Zutcp::PktNumSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  p.addFrame(dataRef(3, 9));
  ZuCHECK(p.key() == 7 && p.length() == 1,
    "TxPkt key/length mismatch");

  Zutcp::SentPktTracker tracker;
  ZuCHECK(tracker.add(p) && tracker.lose(7),
    "sent packet queue loss setup failed");
  Zutcp::SentFrameRef frame;
  ZuCHECK(tracker.nextRetransmit(frame) &&
      frame.kind == Zutcp::SentFrameKind::Data &&
      frame.offset == 3 &&
      frame.length == 9 &&
      !tracker.nextRetransmit(frame) &&
      tracker.lost() == 1 &&
      tracker.retransmittable() == 1,
    "lost TxPkt did not requeue frame references");
}

void testTxPktAckRanges()
{
  ZuTestScopeRT(testTxPktAckRanges);

  Zutcp::SentPktTracker tracker;
  uint64_t pns[] = { 10, 11, 13, 15 };
  for (uint64_t pn : pns) {
    Zutcp::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zutcp::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZUTCP_CHECK_RT(tracker.add(p), "sent packet add failed");
  }

  Zutcp::AckRange ranges[] = {
    Zutcp::AckRange{11, 10},
    Zutcp::AckRange{15, 15}
  };
  unsigned lost = 0;
  ZUTCP_CHECK_RT(tracker.ack(ranges, 2, &lost) == 3 &&
      !lost &&
      tracker.ackd() == 3 &&
      tracker.bytesInFlight() == 100,
    "sent packet ACK range processing mismatch");
}

static Zutcp::TxPkt txPkt(
  uint64_t pn, ZuTime sentTime = {}, unsigned bytes = 100)
{
  Zutcp::TxPkt p;
  p.pn = pn;
  p.sentTime = sentTime ? sentTime : Zutcp::timeUS(pn * 100);
  p.bytes = bytes;
  p.space = Zutcp::PktNumSpace::AppData;
  p.ackEliciting = true;
  p.inFlight = true;
  return p;
}

void testTxPktAckDelete()
{
  ZuTestScopeRT(testTxPktAckDelete);

  Zutcp::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zutcp::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zutcp::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZUTCP_CHECK_RT(tracker.add(p), "sent packet add failed");
  }

  Zutcp::AckRange ranges[] = { Zutcp::AckRange{11, 10} };
  ZUTCP_CHECK_RT(tracker.ack(ranges, 1) == 2 &&
      tracker.count() == 1,
    "sent packet ACK did not remove exact packet nodes");
}

void testTxPktLossRetain()
{
  ZuTestScopeRT(testTxPktLossRetain);

  Zutcp::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 15; ++pn)
    ZUTCP_CHECK_RT(tracker.add(txPkt(pn)), "lost packet add failed");
  for (uint64_t pn = 10; pn < 15; ++pn)
    ZUTCP_CHECK_RT(tracker.lose(pn), "lost packet setup failed");

  ZUTCP_CHECK_RT(tracker.count() == 5 &&
      tracker.lost() == 5 &&
      tracker.retainedLost() == 5 &&
      tracker.retransmitPending() == 0,
    "sent packet loss did not retain exact lost packet nodes");
}

void testTxPktRetainedLostAck()
{
  ZuTestScopeRT(testTxPktRetainedLostAck);

  Zutcp::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zutcp::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zutcp::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    ZUTCP_CHECK_RT(tracker.add(p), "sent packet add failed");
  }

  ZUTCP_CHECK_RT(tracker.lose(10) &&
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

  Zutcp::DataTxPQueue q{0};
  ZuCHECK(q.add(new Zutcp::DataTxPQueue::Node{
	Zutcp::TxUnackdRange{10, 10}}) == ZmPQResult::Inserted,
    "stream unackd range insert failed");
  ZuCHECK(q.clear(13, 4),
    "stream unackd range middle clear failed");

  ZmRef<Zutcp::DataTxPQueue::Node> head = q.find(10);
  ZmRef<Zutcp::DataTxPQueue::Node> tail = q.find(17);
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

  Zutcp::DataTxPQueue dataFin{0};
  ZuCHECK(dataFin.add(new Zutcp::DataTxPQueue::Node{
	Zutcp::TxUnackdRange{20, 5, true}}) == ZmPQResult::Inserted &&
      dataFin.length_() == 6,
    "stream unackd data+FIN range insert failed");
  ZuCHECK(dataFin.clear(20, 5) &&
      dataFin.count_() == 1 &&
      dataFin.length_() == 1,
    "stream unackd data ACK did not leave FIN sentinel");
  ZmRef<Zutcp::DataTxPQueue::Node> fin = dataFin.find(25);
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

  Zutcp::DataTxPQueue finOnly{0};
  ZuCHECK(finOnly.add(new Zutcp::DataTxPQueue::Node{
	Zutcp::TxUnackdRange{30, 0, true}}) == ZmPQResult::Inserted &&
      finOnly.clear(30, 1) &&
      !finOnly.count_() &&
      finOnly.verify(),
    "stream unackd FIN-only range clear failed");
}

void testTxUnackdFragmentation()
{
  ZuTestScopeRT(testTxUnackdFragmentation);

  Zutcp::DataTxPQueue q{0};
  constexpr unsigned Ranges = Zutcp::DataTxPQueue::Max << 1;
  for (unsigned i = 0; i < Ranges; ++i)
    ZUTCP_CHECK_RT(q.add(Zutcp::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "fragmented stream range insert failed");
  ZUTCP_CHECK_RT(q.count_() == Ranges &&
      q.length_() == Ranges &&
      q.find(0) &&
      q.find((uint64_t(Ranges - 1) << 1)) &&
      !q.find(1) &&
      q.verify(),
    "fragmented stream ranges did not grow beyond former capacity");

  ZUTCP_CHECK_RT(q.add(Zutcp::TxUnackdRange{1, 1}) == ZmPQResult::Inserted &&
      q.count_() == Ranges - 1 &&
      q.length_() == Ranges + 1 &&
      q.find(0)->data().length() == 3 &&
      q.verify(),
    "fragmented stream ranges did not coalesce around an inserted bridge");

  unsigned spans = 0;
  ZUTCP_CHECK_RT(q.spans(0, 8, [&spans](const auto &) {
	++spans;
	return true;
      }) &&
      spans == 3,
    "fragmented stream range span traversal mismatch");
}

void testTxUnackdBoundaries()
{
  ZuTestScopeRT(testTxUnackdBoundaries);

  Zutcp::DataTxPQueue q{0};
  ZUTCP_CHECK_RT(q.add(Zutcp::TxUnackdRange{uint64_t(-1), 1}) ==
	ZmPQResult::Invalid &&
      q.add(Zutcp::TxUnackdRange{0, uint64_t(-1), true}) ==
	ZmPQResult::Invalid &&
      !q.clear(uint64_t(-1), 2) &&
      !q.spans(uint64_t(-1), 2, [](const auto &) { return true; }) &&
      !q.count_() &&
      !q.length_() &&
      q.verify(),
    "stream range endpoint overflow was not rejected");

  for (unsigned i = 0; i <= Zutcp::DataTxPQueue::BuiltinRanges; ++i)
    ZUTCP_CHECK_RT(q.add(Zutcp::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "stream range inline/heap transition insert failed");
  ZUTCP_CHECK_RT(q.count_() == Zutcp::DataTxPQueue::BuiltinRanges + 1 &&
      q.verify(),
    "stream range inline/heap transition lost retained ranges");
#ifdef ZDEBUG
  ZUTCP_CHECK_RT(q.promotions() == 1 && q.indexed(),
    "fragmented stream ranges did not promote exactly once");
  ZUTCP_CHECK_RT(q.clear(0, 1) && q.indexed() && q.promotions() == 1,
    "indexed stream ranges demoted before full clear");
  q.clear();
  ZUTCP_CHECK_RT(!q.indexed(),
    "full stream-range clear retained indexed representation");
#endif
}

void testTxUnackdRandomized()
{
  ZuTestScopeRT(testTxUnackdRandomized);

  Zutcp::DataTxPQueue q{0};
  bool expected[512] = {};
  uint32_t random = 0x243f6a88U;
  for (unsigned operation = 0; operation < 4096; ++operation) {
    random = random * 1664525U + 1013904223U;
    unsigned offset = (random >> 8) % 480;
    unsigned length = ((random >> 24) & 31) + 1;
    if (random & 1) {
      ZUTCP_CHECK_RT(q.add(Zutcp::TxUnackdRange{offset, length}) ==
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
      ZUTCP_CHECK_RT(q.clear(offset, length) == changed,
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
    ZUTCP_CHECK_RT(q.spans(0, 512, [&observed](const auto &range) {
	for (uint64_t i = range.key(), n = i + range.length(); i < n; ++i)
	  observed[i] = true;
	return true;
      }), "randomized stream range traversal failed");
    bool equal = true;
    for (unsigned i = 0; i < 512; ++i)
      equal = equal && observed[i] == expected[i];
    ZUTCP_CHECK_RT(equal && q.length_() == expectedLength &&
	q.count_() == expectedCount && q.verify(),
      "randomized stream range model mismatch");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
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
