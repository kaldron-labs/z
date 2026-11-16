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

void testTxPktLossRequeue()
{
  ZuTestScope(testTxPktLossRequeue);

  Zutcp::TxPkt p;
  p.pn = 7;
  p.sentTime = Zutcp::timeUS(100);
  p.bytes = 1200;
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

static Zutcp::TxPkt txPkt(
  uint64_t pn, ZuTime sentTime = {}, unsigned bytes = 100)
{
  Zutcp::TxPkt p;
  p.pn = pn;
  p.sentTime = sentTime ? sentTime : Zutcp::timeUS(pn * 100);
  p.bytes = bytes;
  p.ackEliciting = true;
  p.inFlight = true;
  p.addFrame(dataRef(pn * 10, 10));
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
    p.ackEliciting = true;
    p.inFlight = true;
    p.addFrame(dataRef((pn - 10) * 10, 10));
    ZUTCP_CHECK_RT(tracker.add(p), "sent packet add failed");
  }

  Zutcp::PktAckBatch ack;
  ZUTCP_CHECK_RT(tracker.ackBytesBatch(20, ack, 8) && ack.ackd == 2 &&
      tracker.count() == 1 && tracker.bytesInFlight() == 100,
    "cumulative byte ACK removes only the two fully covered transmission nodes");
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
      tracker.retransmitPending() == 5,
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
    p.ackEliciting = true;
    p.inFlight = true;
    p.addFrame(dataRef((pn - 10) * 10, 10));
    ZUTCP_CHECK_RT(tracker.add(p), "sent packet add failed");
  }

  Zutcp::PktAckBatch ack;
  ZUTCP_CHECK_RT(tracker.lose(10) &&
      tracker.ackBytesBatch(20, ack, 8) && ack.ackd == 1 &&
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

static bool addSACKRange(
  Zutcp::DataRxPQueue &queue, Zi::IOBufAllocator *allocator,
  uint64_t first, unsigned length)
{
  auto packet = packet_("0123456789abcdef");
  if (!packet || length > packet->length) return false;
  queue.add(new Zutcp::DataRxPQueue::Node{
    packet, packet->data(), length, allocator, nullptr, first});
  return true;
}

void testRxSACKOrder()
{
  ZuTestScope(testRxSACKOrder);
  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  bool setup = true;
  for (unsigned first : {10,40,70,100,130}) {
    setup &= addSACKRange(queue, allocator, first, 10);
    state.accepted(first);
  }
  auto iter = queue.citer(130);
  auto node = iter();
  auto original = node ? node->data().data_() : nullptr;
  Zutcp::TCPOptions options;
  bool duplicate = state.fill(queue, 0, options);
  ZuCHECK(setup && !duplicate && options.sackCount == 4 &&
      options.sack[0].left == 130 && options.sack[1].left == 100 &&
      options.sack[2].left == 70 && options.sack[3].left == 40 && queue.count_() == 5,
    "four bounded report priorities must follow the latest accepted out-of-order segment");
  state.accepted(40);
  options = {.flags = Zutcp::TCPOptions::TS, .tsval = 0, .tsecr = 99};
  state.fill(queue, 0, options);
  ZuCHECK(options.sackCount == 3 && options.sack[0].left == 40 &&
      options.sack[1].left == 130 && options.sack[2].left == 100 &&
      options.flags == Zutcp::TCPOptions::TS && !options.tsval && options.tsecr == 99 &&
      options.encodedLength(false) == 36 && node && node->data().data_() == original,
    "TS limits reporting to three blocks without changing echo fields or accepted backing");
  state.duplicate(2, 5, 1);
  bool reported = state.fill(queue, 6, options);
  state.sent(1, true);
  ZuCHECK(reported && !state.fill(queue, 6, options) && options.sackCount == 3 &&
      options.sack[0].left == 40 && options.sack[1].left == 130 && options.sack[2].left == 100,
    "a temporary DSACK slot must not erase the next most-recent normal report priority");
  bool joined = addSACKRange(queue, allocator, 20, 10);
  state.accepted(20);
  options = {};
  state.fill(queue, 0, options);
  ZuCHECK(joined && options.sack[0].left == 10 && options.sack[0].right == 30 &&
      options.sack[1].left == 40,
    "the first block must expand to the actual merged queue span containing the latest segment");
  joined &= addSACKRange(queue, allocator, 30, 10);
  state.accepted(30);
  state.fill(queue, 0, options);
  bool unique = true;
  for (unsigned i = 1; i < options.sackCount; ++i)
    unique &= options.sack[i].left != 10;
  ZuCHECK(joined && options.sackCount == 4 && options.sack[0].left == 10 &&
      options.sack[0].right == 50 && unique && queue.verify(),
    "multiple recent markers in one merged block must not duplicate that SACK block");
}

void testRxDSACKVersion()
{
  ZuTestScope(testRxDSACKVersion);
  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  bool setup = addSACKRange(queue, allocator, 20, 10) &&
    addSACKRange(queue, allocator, 50, 10);
  state.accepted(20);
  state.accepted(50);
  Zutcp::TCPOptions options{.flags = Zutcp::TCPOptions::TS, .tsval = 123, .tsecr = 456};
  bool recorded = state.duplicate(2, 5, 7);
  ZuCHECK(setup && recorded && state.fill(queue, 10, options) && options.sackCount == 3 &&
      options.sack[0].left == 2 && options.sack[0].right == 5 &&
      options.sack[1].left == 50 && options.sack[2].left == 20 && options.dsack(10) &&
      options.tsval == 123 && options.tsecr == 456,
    "a below-ACK duplicate goes first while normal blocks and timestamp values remain bounded");
  state.sent(6, true);
  bool old = state.fill(queue, 10, options);
  state.sent(100, false);
  bool normal = state.fill(queue, 10, options);
  recorded &= state.duplicate(3, 6, 8);
  state.sent(7, true);
  ZuCHECK(old && normal && recorded && state.fill(queue, 10, options) &&
      options.sack[0].left == 3 && options.sack[0].right == 6,
    "older accepted output and output without DSACK cannot clear a replacement report");
  state.sent(8, true);
  ZuCHECK(!state.fill(queue, 10, options) && options.sackCount == 2 &&
      options.sack[0].left == 50,
    "only actual DSACK submission at the report version retires the pending duplicate");
  recorded = state.duplicate(22, 25, 9);
  ZuCHECK(recorded && state.fill(queue, 10, options) && options.sackCount == 3 &&
      options.sack[0].left == 22 && options.sack[0].right == 25 &&
      options.sack[1].left == 20 && options.sack[1].right == 30 &&
      options.sack[2].left == 50 && options.dsack(10),
    "above-ACK DSACK must be immediately followed by its actual containing queue block");
  state.sent(9, true);
  ZuCHECK(!state.fill(queue, 10, options) && options.sack[0].left == 50,
    "DSACK wire ordering must not displace the latest accepted normal marker priority");
  state.reset();
  bool zero = state.duplicate(0, 1, 0) && state.fill(queue, 10, options);
  state.sent(0, true);
  ZuCHECK(zero && !state.fill(queue, 10, options) &&
      !state.duplicate(2, 2, 10) && !state.duplicate(0, uint64_t(1)<<31, 10),
    "duplicate position/version zero are valid; empty or ambiguous spans create no report");
}

void testRxSACKWrap()
{
  ZuTestScope(testRxSACKWrap);
  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  uint64_t base = (uint64_t(1)<<32) - 16;
  bool setup = addSACKRange(queue, allocator, base - 32, 16) &&
    addSACKRange(queue, allocator, base + 8, 16);
  state.accepted(base + 8);
  Zutcp::TCPOptions options;
  ZuCHECK(setup && !state.fill(queue, base, options) && options.sackCount == 1 &&
      options.sack[0].left == 0xfffffff8 && options.sack[0].right == 8,
    "wide accepted ranges report exact relative wrap and skip cumulative queue history");
  bool recorded = state.duplicate(base + 10, base + 20, 11);
  ZuCHECK(recorded && state.fill(queue, base, options) && options.sackCount == 2 &&
      options.sack[0].left == 0xfffffffa && options.sack[0].right == 4 &&
      options.sack[1].left == 0xfffffff8 && options.sack[1].right == 8 &&
      options.dsack(uint32_t(base)),
    "above-ACK duplicate and containing block must preserve relative modulo wrap");
  ZuCHECK(state.fill(queue, base + 24, options) && options.sackCount == 1 &&
      options.sack[0].left == 0xfffffffa && options.dsack(8),
    "after cumulative progress the pending duplicate is below ACK and needs no normal block");
  state.sent(11, true);
  ZuCHECK(!state.fill(queue, base + 24, options) && !options.sackCount && queue.count_() == 2,
    "delivered queue backing must not remain advertised beyond receive-next");
}

void testRxSACKQueueAuthority()
{
  ZuTestScope(testRxSACKQueueAuthority);
  ZiMultiplex mx;
  auto allocator = mx.allocator<Zi::IOBufType::App>();
  Zutcp::DataRxPQueue queue{0};
  Zutcp::RxSACK state;
  bool setup = addSACKRange(queue, allocator, 20, 10);
  state.accepted(20);
  state.duplicate(22, 25, 15);
  Zutcp::TCPOptions options;
  bool first = state.fill(queue, 10, options);
  queue.reset(0);
  ZuCHECK(setup && first && !state.fill(queue, 10, options) && !options.sackCount,
    "discarded receive ranges must disappear from SACK rather than survive in report metadata");
  state.sent(15, false);
  bool restored = addSACKRange(queue, allocator, 20, 10);
  state.accepted(20);
  ZuCHECK(restored && state.fill(queue, 10, options) && options.sackCount == 2 &&
      options.sack[0].left == 22 && options.sack[1].left == 20,
    "a held above-ACK duplicate needs its containing range before it can be reported again");
  state.reset();
  queue.reset(0);
  restored = addSACKRange(queue, allocator, 60, 10);
  ZuCHECK(restored && !state.fill(queue, 10, options) && options.sackCount == 1 &&
      options.sack[0].left == 60 && !options.sack[1].left && !options.sack[1].right,
    "incarnation reset removes duplicate priorities and clears unused option block slots");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRxOverlapDrop);
  ZuTestCall(testRxSACKOrder);
  ZuTestCall(testRxDSACKVersion);
  ZuTestCall(testRxSACKWrap);
  ZuTestCall(testRxSACKQueueAuthority);
  ZuTestCall(testOutOfOrderDrain);
  ZuTestCall(testTxPktLossRequeue);
  ZuTestCall(testTxPktAckDelete);
  ZuTestCall(testTxPktLossRetain);
  ZuTestCall(testTxPktRetainedLostAck);
  ZuTestCall(testTxUnackdRangeQueue);
  ZuTestCall(testTxUnackdFinSentinel);
  ZuTestCall(testTxUnackdFragmentation);
  ZuTestCall(testTxUnackdBoundaries);
  ZuTestCall(testTxUnackdRandomized);
}
