//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

#define Zquic_CheckRT_1(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

void fillBuffer(ZiIOBuf *buf, ZuBSpan data)
{
  memcpy(buf->data_(), data.data(), data.length());
  buf->skip = 0;
  buf->length = data.length();
}

ZmRef<ZiIOBuf> packet_(ZuBSpan data)
{
  ZmRef<ZiIOBuf> packet = ([](void *owner) {
      static Zi::IOBufAllocator *allocator =
	([] { static ZiMultiplex mx; return Zquic::pktRxAllocator(&mx); })();
      return allocator->allocBuf(owner, 0);
    })(nullptr);
  if (ZuUnlikely(!packet)) return nullptr;
  fillBuffer(packet, data);
  return packet;
}

void testCryptoRxClipping()
{
  ZuTestScope(testCryptoRxClipping);

  ZiMultiplex mx;
  ZmRef<Zquic::CryptoRxPQueue::Node> buf =
    Zquic::plaintextAllocator<Zquic::CryptoRxBufAlloc<>>(&mx)->allocBuf();
  fillBuffer(buf, "abcdef");
  auto &data = buf->data();
  data.skip = 1;
  data.length = 4;
  data.offset = 10;

  ZuCHECK(data.key() == 10 && data.length == 4,
    "CRYPTO initial key/length mismatch");
  ZuCHECK(data.clipHead(2) == 2 &&
      data.key() == 12 && data.skip == 3 && data.length == 2,
    "CRYPTO head clipping mismatch");
  ZuCHECK(data.clipTail(1) == 1 &&
      data.key() == 12 && data.skip == 3 && data.length == 1,
    "CRYPTO tail clipping mismatch");

  ZtArray<uint8_t, ZtArrayHeapID<"Zquic.Test.CryptoPayload">> payload;
  payload.length(Zquic::BufSize * 2);
  memset(payload.data(), 'x', payload.length());
  data.clear();
  ZuCHECK(data.append(payload) && data.length == payload.length() &&
      data.cspan() == ZuBSpan{payload} && data.offset == 12,
    "CRYPTO buffer growth lost payload or stream metadata");
}

void testRxOverlapDrop()
{
  ZuTestScope(testRxOverlapDrop);

  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zquic::StreamRxPQueue q{0};
  auto first = packet_("world");
  auto dup = packet_("WORLD");
  auto second = packet_("helloworldtails");

  q.add(new Zquic::StreamRxPQueue::Node{
    first, first->data(), first->length, allocator, nullptr, 5});
  unsigned count = q.count_();
  q.add(new Zquic::StreamRxPQueue::Node{
    dup, dup->data(), dup->length, allocator, nullptr, 5});
  ZuCHECK(q.count_() == count,
    "fully duplicate STREAM data mutated retained queue");

  q.add(new Zquic::StreamRxPQueue::Node{
    second, second->data(), 5, allocator, nullptr, 0});
  q.add(new Zquic::StreamRxPQueue::Node{
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
  Zquic::StreamRxPQueue q{0};
  for (unsigned i = 1; i < 32; ++i) {
    auto packet = packet_("x");
    q.add(new Zquic::StreamRxPQueue::Node{
      packet, packet->data(), packet->length, allocator, nullptr, uint64_t(i)});
  }
  Zquic_CheckRT_1(!q.dequeue(), "out-of-order queue drained before gap was filled");

  auto head = packet_("x");
  q.add(new Zquic::StreamRxPQueue::Node{
    head, head->data(), head->length, allocator, nullptr, 0});

  unsigned drained = 0;
  while (auto node = q.dequeue()) {
    Zquic_CheckRT_1(node->data().key() == drained,
      "out-of-order queue drained in wrong order");
    ++drained;
  }
  Zquic_CheckRT_1(drained == 32 && !q.count_() && q.head() == 32 && q.verify(),
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
  ZuTestScopeRT(testAckRangeBound);

  Zquic::AckTracker ack;
  for (unsigned i = 0; i < Zquic::AckTracker::MaxRetained + 10; ++i)
    Zquic_CheckRT_1(ack.add((uint64_t(i) << 1) + 1),
      "sparse packet Rx mark insertion failed");

  Zquic_CheckRT_1(ack.count() <= Zquic::AckTracker::MaxRetained,
    "packet Rx sparse ACK range cap was not enforced");
  Zquic_CheckRT_1(!ack.contains(1),
    "packet Rx sparse ACK range cap did not drop oldest range");
  Zquic_CheckRT_1(ack.contains(
      ((uint64_t(Zquic::AckTracker::MaxRetained + 9)) << 1) + 1),
    "packet Rx sparse ACK range cap did not retain newest range");
}

void testAckOfAckTrim()
{
  ZuTestScopeRT(testAckOfAckTrim);

  Zquic::AckTracker ack;
  for (unsigned i = 0; i < 6; ++i)
    Zquic_CheckRT_1(ack.add(i), "contiguous packet Rx mark insertion failed");

  ack.ackdByPeer(3);

  Zquic::AckRange range;
  Zquic_CheckRT_1(ack.count() == 1 &&
      ack.range(0, range) &&
      range.first == 4 &&
      range.largest == 5,
    "ACK-of-ACK did not trim contiguous ACK range");

  Zquic_CheckRT_1(ack.add(8) && ack.add(10),
    "sparse packet Rx mark insertion after ACK-of-ACK failed");
  ack.ackdByPeer(8);

  Zquic_CheckRT_1(ack.count() == 2 &&
      ack.range(0, range) &&
      range.first == 8 &&
      range.largest == 8,
    "ACK-of-ACK incorrectly trimmed sparse ACK ranges");

  uint8_t b[64];
  int n = ack.writeFrame(b, sizeof(b));
  Zquic::Frame frame;
  unsigned used = 0;
  Zquic_CheckRT_1(n > 0 &&
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
  ZuTestScopeRT(testTxPktAckRanges);

  Zquic::SentPktTracker tracker;
  uint64_t pns[] = { 10, 11, 13, 15 };
  for (uint64_t pn : pns) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    Zquic_CheckRT_1(tracker.add(p), "sent packet add failed");
  }

  Zquic::AckRange ranges[] = {
    Zquic::AckRange{11, 10},
    Zquic::AckRange{15, 15}
  };
  unsigned lost = 0;
  Zquic_CheckRT_1(tracker.ack(ranges, 2, &lost) == 3 &&
      !lost &&
      tracker.ackd() == 3 &&
      tracker.bytesInFlight() == 100,
    "sent packet ACK range processing mismatch");
}

static Zquic::TxPkt txPkt(
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
  ZuTestScopeRT(testTxPktAckDelete);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    Zquic_CheckRT_1(tracker.add(p), "sent packet add failed");
  }

  Zquic::AckRange ranges[] = { Zquic::AckRange{11, 10} };
  Zquic_CheckRT_1(tracker.ack(ranges, 1) == 2 &&
      tracker.count() == 1,
    "sent packet ACK did not remove exact packet nodes");
}

void testTxPktLossRetain()
{
  ZuTestScopeRT(testTxPktLossRetain);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 15; ++pn)
    Zquic_CheckRT_1(tracker.add(txPkt(pn)), "lost packet add failed");
  for (uint64_t pn = 10; pn < 15; ++pn)
    Zquic_CheckRT_1(tracker.lose(pn), "lost packet setup failed");

  Zquic_CheckRT_1(tracker.count() == 5 &&
      tracker.lost() == 5 &&
      tracker.retainedLost() == 5 &&
      tracker.retransmitPending() == 0,
    "sent packet loss did not retain exact lost packet nodes");
}

void testTxPktRetainedLostAck()
{
  ZuTestScopeRT(testTxPktRetainedLostAck);

  Zquic::SentPktTracker tracker;
  for (uint64_t pn = 10; pn < 13; ++pn) {
    Zquic::TxPkt p;
    p.pn = pn;
    p.bytes = 100;
    p.space = Zquic::PktNumSpace::AppData;
    p.ackEliciting = true;
    p.inFlight = true;
    Zquic_CheckRT_1(tracker.add(p), "sent packet add failed");
  }

  Zquic_CheckRT_1(tracker.lose(10) &&
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
  ZuCHECK(q.add(Zquic::TxUnackdRange{10, 10}) == ZmPQResult::Inserted,
    "stream unackd range insert failed");
  ZuCHECK(q.clear(13, 4),
    "stream unackd range middle clear failed");

  Zquic::TxUnackdRange head, tail;
  ZuCHECK(q.find(10, head) && q.find(17, tail) &&
      head.key() == 10 &&
      head.length() == 3 &&
      tail.key() == 17 &&
      tail.length() == 3 &&
      q.count_() == 2 &&
      q.length_() == 6 &&
      q.verify(),
    "stream unackd range clear did not split retained ranges");

  ZuCHECK(q.clear(10, 3) &&
      !q.find(10, head) &&
      q.count_() == 1 &&
      q.length_() == 3 &&
      q.verify(),
    "stream unackd range exact clear failed");
}

void testTxUnackdFinSentinel()
{
  ZuTestScope(testTxUnackdFinSentinel);

  Zquic::StreamTxPQueue dataFin{0};
  ZuCHECK(dataFin.add(Zquic::TxUnackdRange{20, 5, true}) == ZmPQResult::Inserted &&
      dataFin.length_() == 6,
    "stream unackd data+FIN range insert failed");
  ZuCHECK(dataFin.clear(20, 5) &&
      dataFin.count_() == 1 &&
      dataFin.length_() == 1,
    "stream unackd data ACK did not leave FIN sentinel");
  Zquic::TxUnackdRange fin;
  ZuCHECK(dataFin.find(25, fin) &&
      fin.key() == 25 &&
      fin.bytes == 0 &&
      fin.fin,
    "stream unackd FIN sentinel has wrong offset/state");
  ZuCHECK(dataFin.clear(25, 1) &&
      !dataFin.count_() &&
      !dataFin.length_() &&
      dataFin.verify(),
    "stream unackd FIN sentinel clear failed");

  Zquic::StreamTxPQueue finOnly{0};
  ZuCHECK(finOnly.add(Zquic::TxUnackdRange{30, 0, true}) == ZmPQResult::Inserted &&
      finOnly.clear(30, 1) &&
      !finOnly.count_() &&
      finOnly.verify(),
    "stream unackd FIN-only range clear failed");
}

void testTxUnackdFragmentation()
{
  ZuTestScopeRT(testTxUnackdFragmentation);
  Zquic::TxUnackdRange found;

  Zquic::StreamTxPQueue q{0};
  enum { Ranges = Zquic::StreamTxPQueue::Max << 1 };
  for (unsigned i = 0; i < Ranges; ++i)
    Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "fragmented stream range insert failed");
  Zquic_CheckRT_1(q.count_() == Ranges &&
      q.length_() == Ranges &&
      q.find(0, found) &&
      q.find(uint64_t(Ranges - 1) << 1, found) &&
      !q.find(1, found) &&
      q.verify(),
    "fragmented stream ranges did not grow beyond former capacity");

  Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{1, 1}) == ZmPQResult::Inserted &&
      q.count_() == Ranges - 1 &&
      q.length_() == Ranges + 1 &&
      q.find(0, found) && found.length() == 3 &&
      q.verify(),
    "fragmented stream ranges did not coalesce around an inserted bridge");

  unsigned spans = 0;
  Zquic_CheckRT_1(q.spans(0, 8, [&spans](const auto &) {
	++spans;
	return true;
      }) &&
      spans == 3,
    "fragmented stream range span traversal mismatch");
}

void testTxUnackdBoundaries()
{
  ZuTestScopeRT(testTxUnackdBoundaries);

  Zquic::StreamTxPQueue q{0};
  Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{uint64_t(-1), 1}) ==
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
    Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{uint64_t(i) << 1, 1}) ==
	ZmPQResult::Inserted,
      "stream range inline/heap transition insert failed");
  Zquic_CheckRT_1(q.count_() == Zquic::StreamTxPQueue::BuiltinRanges + 1 &&
      q.verify(),
    "stream range inline/heap transition lost retained ranges");
#ifdef ZDEBUG
  Zquic_CheckRT_1(q.promotions() == 1 && q.indexed(),
    "fragmented stream ranges did not promote exactly once");
  Zquic_CheckRT_1(q.clear(0, 1) && q.indexed() && q.promotions() == 1,
    "indexed stream ranges demoted before full clear");
  q.clear();
  Zquic_CheckRT_1(!q.indexed(),
    "full stream-range clear retained indexed representation");
#endif
}

void testTxUnackdDrain()
{
  ZuTestScopeRT(testTxUnackdDrain);
  Zquic::TxUnackdRange found;

  unsigned counts[] = {4, Zquic::StreamTxPQueue::BuiltinRanges + 3};
  for (unsigned count : counts) {
    Zquic::StreamTxPQueue q{0};
    uint64_t length = 0;
    for (unsigned i = 0; i < count; ++i) {
      unsigned bytes = i % 3 + 1;
      bool fin = i == count - 1;
      length += bytes + unsigned(fin);
      Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{uint64_t(i) << 3, bytes, fin}) ==
	  ZmPQResult::Inserted, "bounded drain setup failed");
    }
    unsigned budget = 0;
    Zquic_CheckRT_1(!q.drain(budget) && !budget && q.count_() == count &&
	q.length_() == length && q.verify(),
      "zero-budget drain changed retained ranges");
    budget = 2;
    Zquic_CheckRT_1(!q.drain(budget) && !budget && q.count_() == count - 2 &&
	q.length_() == length - 3 && !q.find(0, found) && !q.find(8, found) && q.find(16, found) &&
	q.verify(), "partial drain exceeded its budget or lost remaining ranges");
#ifdef ZDEBUG
    Zquic_CheckRT_1(q.indexed() == (count > Zquic::StreamTxPQueue::BuiltinRanges),
      "partial drain changed the retained representation");
#endif
    budget = count + 5;
    Zquic_CheckRT_1(q.drain(budget) && budget == 7 && !q.count_() &&
	!q.length_() && q.verify(),
      "completed drain lost FIN accounting or consumed excess budget");
#ifdef ZDEBUG
    Zquic_CheckRT_1(!q.indexed(), "completed drain retained the empty index");
#endif
    Zquic_CheckRT_1(q.drain(budget) && budget == 7 &&
	q.add(Zquic::TxUnackdRange{0, 1}) == ZmPQResult::Inserted &&
	q.find(0, found) && q.count_() == 1 && q.length_() == 1 && q.verify(),
      "drained queue did not accept a fresh runtime at offset zero");
  }
}

void testTxUnackdIndexedReuse()
{
  ZuTestScopeRT(testTxUnackdIndexedReuse);

  using Node = Zquic::TxUnackdRanges::Indexed::Node;
  using Cache = ZmHeapCacheT<
    ZmNHeapID_<Node>, ZmHeapAllocSize<sizeof(Node)>{}, alignof(Node),
    ZmNHeapSharded_<Node>{}>;
  auto cache = Cache::instance()->cache();
  auto allocs = [cache] {
    const auto &stats = cache->stats();
    return stats.allocs.load_() + stats.heapAllocs.load_();
  };
  uint64_t baseline = allocs();
  Zquic::TxUnackdRanges q;
  Zquic_CheckRT_1(q.add({10, 10}) == ZmPQResult::Inserted &&
      q.add({40, 10}) == ZmPQResult::Inserted && allocs() == baseline,
    "inline interval insert allocated index storage");
  for (unsigned i = 0; i < 8; ++i)
    Zquic_CheckRT_1(q.add({1000 + uint64_t(i) * 100, 10}) ==
	ZmPQResult::Inserted, "indexed reuse setup failed");
  baseline = allocs();
  auto check = [&q](ZuSpan<const Zquic::TxUnackdRange> expected) {
    unsigned i = 0;
    uint64_t length = 80;
    for (const auto &range : expected) length += range.length();
    bool equal = q.spans(0, 1000, [&expected, &i](const auto &range) {
      if (i == expected.length()) return false;
      const auto &want = expected[i++];
      return range.offset == want.offset && range.bytes == want.bytes &&
	range.fin == want.fin;
    });
    return equal && i == expected.length() &&
      q.count_() == 8 + expected.length() && q.length_() == length && q.verify();
  };
  Zquic::TxUnackdRange extended[] = {{10, 15}, {40, 10}};
  Zquic_CheckRT_1(q.add({20, 5}) == ZmPQResult::Inserted &&
      q.add({12, 1}) == ZmPQResult::Inserted && check(extended) &&
      allocs() == baseline, "indexed append/contained insert replaced a node");
  Zquic::TxUnackdRange bridged[] = {{10, 40}};
  Zquic_CheckRT_1(q.add({25, 15}) == ZmPQResult::Inserted && check(bridged) &&
      allocs() == baseline, "indexed bridge merge allocated a replacement");
  Zquic::TxUnackdRange clipped[] = {{15, 30}};
  Zquic_CheckRT_1(q.clear(10, 5) && q.clear(45, 5) && check(clipped) &&
      allocs() == baseline, "indexed head/tail clip allocated a replacement");
  Zquic::TxUnackdRange split[] = {{15, 10}, {30, 15}};
  Zquic_CheckRT_1(q.clear(25, 5) && check(split) && allocs() == baseline + 1,
    "indexed middle split did not allocate exactly one additional node");
  ++baseline;
  Zquic::TxUnackdRange withFin[] = {{15, 10}, {30, 15, true}};
  Zquic_CheckRT_1(q.add({45, 0, true}) == ZmPQResult::Inserted && check(withFin) &&
      q.add({45, 0, true}) == ZmPQResult::Inserted && check(withFin) &&
      allocs() == baseline, "indexed FIN extension/duplicate replaced a node");
  Zquic::TxUnackdRange finOnly[] = {{15, 10}, {45, 0, true}};
  Zquic_CheckRT_1(q.clear(30, 15) && check(finOnly) && allocs() == baseline,
    "indexed data ACK did not reuse the FIN sentinel node");
  Zquic::TxUnackdRange headOnly[] = {{15, 10}};
  Zquic_CheckRT_1(q.clear(45, 1) && check(headOnly) && allocs() == baseline,
    "indexed FIN ACK changed allocation accounting");
  unsigned budget = q.count_();
  Zquic_CheckRT_1(q.drain(budget) && !budget && !q.count_() && !q.length_() &&
      q.verify() && allocs() == baseline, "indexed empty drain allocated storage");
}

void testTxUnackdRandomized()
{
  ZuTestScopeRT(testTxUnackdRandomized);

  Zquic::StreamTxPQueue q{0};
  bool expected[512] = {};
  uint32_t random = 0x243f6a88U;
  for (unsigned operation = 0; operation < 4096; ++operation) {
    random = random * 1664525U + 1013904223U;
    unsigned offset = (random >> 8) % 480;
    unsigned length = ((random >> 24) & 31) + 1;
    if (random & 1) {
      Zquic_CheckRT_1(q.add(Zquic::TxUnackdRange{offset, length}) ==
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
      Zquic_CheckRT_1(q.clear(offset, length) == changed,
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
    Zquic_CheckRT_1(q.spans(0, 512, [&observed](const auto &range) {
	for (uint64_t i = range.key(), n = i + range.length(); i < n; ++i)
	  observed[i] = true;
	return true;
      }), "randomized stream range traversal failed");
    bool equal = true;
    for (unsigned i = 0; i < 512; ++i)
      equal = equal && observed[i] == expected[i];
    Zquic_CheckRT_1(equal && q.length_() == expectedLength &&
	q.count_() == expectedCount && q.verify(),
      "randomized stream range model mismatch");
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCryptoRxClipping);
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
  ZuTestCall(testTxUnackdDrain);
  ZuTestCall(testTxUnackdIndexedReuse);
  ZuTestCall(testTxUnackdRandomized);
}
