//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmHeap.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

using namespace ZuTestUtil;

namespace ZiRxStreamTest_ {

ZmListDerive(RxQueue, Zi::RxBuf, ZmListNode<Zi::RxBuf, ZmListHeapID<"">>);
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 64, 256,
  ZuStringT<"ZiRxStreamTest.Buf">>;

ZmRef<RxQueue::Node> mkBuf(const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{nullptr, 0};
  auto iobuf = static_cast<ZiIOBuf *>(buf.ptr());
  if (n) {
    ::memcpy(iobuf->data(), s, n);
    iobuf->length = n;
  }
  return buf;
}

bool spanEq(ZuBSpan span, const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  return span.length() == n && (!n || !::memcmp(span.data(), s, n));
}

template <typename Stream>
bool consumeExact(Stream &stream, unsigned n, const char *expected)
{
  if (!n)
    return !stream.consume(
      [](ZuSpan<uint8_t>) -> int64_t { return 0; },
      [](ZuSpan<uint8_t>) { });

  unsigned remaining = n;
  bool called = false;
  bool ok = false;
  int64_t consumed = stream.consume(
    [&remaining](ZuSpan<uint8_t> span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&called, &ok, &expected](ZuSpan<uint8_t> span) {
      called = true;
      ok = spanEq(span, expected);
    });
  return consumed == n && called && ok;
}

void testConsumeAcrossQueuedBuffers()
{
  ZuTestScope(testConsumeAcrossQueuedBuffers);

  ZiRxStream<RxQueue> stream;

  ZuCheck(!stream);
  ZuCheck(!consumeExact(stream, 1, ""));

  stream.push(mkBuf("abc"));
  stream.push(mkBuf("de"));

  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 5);
  ZuCheck(consumeExact(stream, 1, "a"));
  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 4);
  ZuCheck(consumeExact(stream, 4, "bcde"));
  ZuCheck(!stream);
  ZuCheck(stream.count_() == 0);
  ZuCheck(!stream.length());

  ZuCheck(consumeExact(stream, 0, ""));
  ZuCheck(!consumeExact(stream, 2, ""));
}

void testPushFiltersZeroLengthNodes()
{
  ZuTestScope(testPushFiltersZeroLengthNodes);

  ZiRxStream<RxQueue> stream;

  stream.push(mkBuf(""));
  stream.push(mkBuf(""));
  stream.push(mkBuf("xy"));
  stream.push(mkBuf(""));

  ZuCheck(stream.count_() == 1);
  ZuCheck(stream.length() == 2);
  ZuCheck(!!stream);
  ZuCheck(stream.advance(1) == 1);
  ZuCheck(stream.length() == 1);
  ZuCheck(consumeExact(stream, 1, "y"));
  ZuCheck(!stream);
  ZuCheck(stream.count_() == 0);
}

void testConsumeStepsThroughQueuedBuffers()
{
  ZuTestScope(testConsumeStepsThroughQueuedBuffers);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abcd"));
  stream.push(mkBuf("ef"));

  ZuCheck(consumeExact(stream, 2, "ab"));
  ZuCheck(consumeExact(stream, 2, "cd"));
  ZuCheck(stream.count_() == 1);

  ZuCheck(consumeExact(stream, 2, "ef"));
  ZuCheck(!stream);
}

void testConsumeGathersFragmentedFrame()
{
  ZuTestScope(testConsumeGathersFragmentedFrame);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc"));
  stream.push(mkBuf("de"));

  bool called = false;
  int64_t consumed = stream.consume(
    [](ZuSpan<uint8_t>) -> int64_t { return 0; },
    [&called](ZuSpan<uint8_t>) { called = true; });

  ZuCheck(!consumed);
  ZuCheck(!called);
  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 5);

  unsigned seen = 0;
  consumed = stream.consume(
    [&seen](ZuSpan<uint8_t> span) -> int64_t {
      seen += span.length();
      return seen >= 5 ? span.length() : 0;
    },
    [&called](ZuSpan<uint8_t> span) {
      called = true;
      ZuCheck(spanEq(span, "abcde"));
    });

  ZuCheck(consumed == 5);
  ZuCheck(called);
  ZuCheck(!stream);
  ZuCheck(!stream.length());
}

void testConsumePaddingAcrossQueuedBuffers()
{
  ZuTestScope(testConsumePaddingAcrossQueuedBuffers);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc\r"));
  stream.push(mkBuf("\nrest"));

  bool called = false;
  int64_t consumed = stream.consume<2>(
    [prevCR = false](ZuSpan<uint8_t> span) mutable -> int64_t {
      if (prevCR && span[0] == '\n') return 1;
      for (unsigned i = 1, n = span.length(); i < n; ++i)
	if (span[i - 1] == '\r' && span[i] == '\n')
	  return i + 1;
      prevCR = span[span.length() - 1] == '\r';
      return 0;
    },
    [&called](ZuSpan<uint8_t> span) {
      called = true;
      ZuCheck(spanEq(span, "abc"));
    });

  ZuCheck(consumed == 5);
  ZuCheck(called);
  ZuCheck(stream.count_() == 1);
  ZuCheck(stream.length() == 4);
  ZuCheck(consumeExact(stream, 4, "rest"));
  ZuCheck(!stream);
}

void testCleanResetsState()
{
  ZuTestScope(testCleanResetsState);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("aaa"));
  stream.push(mkBuf("bbb"));

  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 6);
  stream.clean();
  ZuCheck(stream.count_() == 0);
  ZuCheck(!stream.length());
  ZuCheck(!stream);
  ZuCheck(!consumeExact(stream, 1, ""));

  stream.push(mkBuf("z"));
  ZuCheck(stream.count_() == 1);
  ZuCheck(consumeExact(stream, 1, "z"));
}

int64_t extractN(
    ZiRxStream<RxQueue> &stream, unsigned n,
    Zi::RxRange &out, unsigned &allocs)
{
  unsigned remaining = n;
  return stream.extract(
    [&remaining](ZuBSpan span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&allocs]() -> ZmRef<RxQueue::Node> {
      ++allocs;
      return new RxBufAlloc{nullptr, 0};
    },
    out);
}

void testExtractCompleteFrames()
{
  ZuTestScope(testExtractCompleteFrames);

  {
    ZiRxStream<RxQueue> stream;
    auto input = mkBuf("frame");
    auto inputPtr = input.ptr();
    stream.push(ZuMv(input));
    Zi::RxRange out;
    unsigned allocs = 0;
    ZuCheck(extractN(stream, 5, out, allocs) == 5);
    ZuCheck(out.buf.ptr() == inputPtr);
    ZuCheck(spanEq(out.span(), "frame"));
    ZuCheck(!allocs);
    ZuCheck(!stream);
    ZuCheck(!stream.length());
  }
  {
    ZiRxStream<RxQueue> stream;
    auto input = mkBuf("frameNEXT");
    auto inputPtr = input.ptr();
    stream.push(ZuMv(input));
    Zi::RxRange out;
    unsigned allocs = 0;
    ZuCheck(extractN(stream, 5, out, allocs) == 5);
    ZuCheck(out.buf.ptr() == inputPtr);
    ZuCheck(spanEq(out.span(), "frame"));
    ZuCheck(!allocs);
    ZuCheck(spanEq(stream.span(), "NEXT"));
    ZuCheck(stream.length() == 4);
  }
  {
    ZiRxStream<RxQueue> stream;
    stream.push(mkBuf("abc"));
    stream.push(mkBuf("deNEXT"));
    Zi::RxRange out;
    unsigned allocs = 0;
    ZuCheck(extractN(stream, 5, out, allocs) == 5);
    ZuCheck(spanEq(out.span(), "abcde"));
    ZuCheck(!allocs);
    ZuCheck(spanEq(stream.span(), "NEXT"));
    ZuCheck(stream.length() == 4);
  }
}

void testExtractWaitAndError()
{
  ZuTestScope(testExtractWaitAndError);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc"));
  stream.push(mkBuf("de"));
  Zi::RxRange out;
  unsigned allocs = 0;
  ZuCheck(!extractN(stream, 6, out, allocs));
  ZuCheck(!out);
  ZuCheck(!allocs);
  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 5);

  int64_t n = stream.extract(
    [](ZuBSpan) -> int64_t { return -1; },
    [&allocs]() -> ZmRef<RxQueue::Node> {
      ++allocs;
      return new RxBufAlloc{nullptr, 0};
    },
    out);
  ZuCheck(n < 0);
  ZuCheck(!out);
  ZuCheck(!allocs);
  ZuCheck(stream.count_() == 2);
  ZuCheck(stream.length() == 5);
}

int64_t spliceN(
  ZiRxStream<RxQueue> &src, ZiRxStream<RxQueue> &dst,
  unsigned total, unsigned head, unsigned tail)
{
  unsigned remaining = total;
  return src.splice(dst,
    [&remaining](ZuBSpan span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    }, head, tail);
}

void testSpliceWaitAndFullNode()
{
  ZuTestScope(testSpliceWaitAndFullNode);

  ZiRxStream<RxQueue> src;
  ZiRxStream<RxQueue> dst;
  src.push(mkBuf("abc"));
  dst.push(mkBuf("old"));

  ZuCheck(!spliceN(src, dst, 4, 0, 0));
  ZuCheck(src.length() == 3);
  ZuCheck(dst.length() == 3);

  auto tail = mkBuf("d");
  auto tailPtr = tail.ptr();
  src.push(ZuMv(tail));
  ZuCheck(spliceN(src, dst, 4, 0, 0) == 4);
  ZuCheck(!src);
  ZuCheck(dst.length() == 7);
  ZuCheck(consumeExact(dst, 7, "oldabcd"));
  ZuCheck(tailPtr);
}

void testSpliceBoundaryRanges()
{
  ZuTestScope(testSpliceBoundaryRanges);

  {
    ZiRxStream<RxQueue> src;
    ZiRxStream<RxQueue> dst;
    src.push(mkBuf("[abc]NEXT"));
    ZuCheck(spliceN(src, dst, 5, 1, 1) == 5);
    ZuCheck(dst.length() == 3);
    ZuCheck(consumeExact(dst, 3, "abc"));
    ZuCheck(src.length() == 4);
    ZuCheck(consumeExact(src, 4, "NEXT"));
  }
  {
    ZiRxStream<RxQueue> src;
    ZiRxStream<RxQueue> dst;
    auto input = mkBuf("[abcdefgh]N");
    auto inputPtr = input.ptr();
    src.push(ZuMv(input));
    ZuCheck(spliceN(src, dst, 10, 1, 1) == 10);
    ZuCheck(dst.span().data() == static_cast<ZiIOBuf *>(inputPtr)->data() + 1);
    ZuCheck(consumeExact(dst, 8, "abcdefgh"));
    ZuCheck(consumeExact(src, 1, "N"));
  }
}

void testSpliceFragmentedAndEmpty()
{
  ZuTestScope(testSpliceFragmentedAndEmpty);

  ZiRxStream<RxQueue> src;
  ZiRxStream<RxQueue> dst;
  dst.push(mkBuf("partial"));
  src.push(mkBuf("[a"));
  src.push(mkBuf("bc"));
  src.push(mkBuf("d]NEXT"));

  ZuCheck(spliceN(src, dst, 6, 1, 1) == 6);
  ZuCheck(dst.length() == 11);
  ZuCheck(consumeExact(dst, 11, "partialabcd"));
  ZuCheck(consumeExact(src, 4, "NEXT"));

  src.push(mkBuf("[]rest"));
  ZuCheck(spliceN(src, dst, 2, 1, 1) == 2);
  ZuCheck(!dst);
  ZuCheck(consumeExact(src, 4, "rest"));
}

void testSpliceFramingRejection()
{
  ZuTestScope(testSpliceFramingRejection);

  ZiRxStream<RxQueue> src;
  ZiRxStream<RxQueue> dst;
  src.push(mkBuf("[abc]NEXT"));
  dst.push(mkBuf("old"));

  ZuCheck(spliceN(src, dst, 5, 4, 2) < 0);
  ZuCheck(src.length() == 9);
  ZuCheck(dst.length() == 3);
  ZuCheck(consumeExact(src, 9, "[abc]NEXT"));
  ZuCheck(consumeExact(dst, 3, "old"));
}

void testSpliceTransform()
{
  ZuTestScope(testSpliceTransform);

  ZiRxStream<RxQueue> src;
  ZiRxStream<RxQueue> dst;
  src.push(mkBuf("abcNEXT"));
  unsigned remaining = 3;
  int64_t n = src.splice(
    dst,
    [&remaining](ZuBSpan span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    0, 0,
    [](ZuSpan<uint8_t> span) {
      for (auto &c : span) c -= 'a' - 'A';
    });

  ZuCheck(n == 3);
  ZuCheck(consumeExact(dst, 3, "ABC"));
  ZuCheck(consumeExact(src, 4, "NEXT"));
}

void testIncompleteAppend()
{
  ZuTestScope(testIncompleteAppend);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc"));

  ZuCheck(!consumeExact(stream, 5, ""));
  ZuCheck(stream.length() == 3);
  ZuCheck(spanEq(stream.span(), "abc"));

  stream.push(mkBuf("deNEXT"));
  ZuCheck(stream.length() == 9);
  ZuCheck(consumeExact(stream, 5, "abcde"));
  ZuCheck(stream.length() == 4);
  ZuCheck(spanEq(stream.span(), "NEXT"));
}

void testCopyAndEach()
{
  ZuTestScope(testCopyAndEach);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("ab"));
  stream.push(mkBuf("cde"));
  stream.push(mkBuf("fg"));
  ZuBArray<4> copy;
  ZuCheck(stream.copy(1, copy.span()) == 4);
  ZuCheck(!memcmp(copy.data(), "bcde", 4));

  ZtString<> visited;
  int64_t n = stream.each(2, 4, [&visited](ZuSpan<uint8_t> span) -> int64_t {
    visited << ZuCSpan{span};
    return span.length();
  });
  ZuCheck(n == 4);
  ZuCheck(visited == "cdef");
  ZuCheck(stream.length() == 7);
  ZuCheck(stream.advance(5) == 5);
  ZuCheck(stream.length() == 2);
  ZuCheck(consumeExact(stream, 2, "fg"));
}

void testMoveAndClean()
{
  ZuTestScope(testMoveAndClean);

  ZiRxStream<RxQueue> src;
  src.push(mkBuf("abc"));
  src.push(mkBuf("de"));
  ZiRxStream<RxQueue> dst{ZuMv(src)};

  ZuCheck(!src.length());
  ZuCheck(!src);
  ZuCheck(dst.length() == 5);
  ZuCheck(dst.count_() == 2);

  ZiRxStream<RxQueue> assigned;
  assigned.push(mkBuf("discard"));
  assigned = ZuMv(dst);
  ZuCheck(!dst.length());
  ZuCheck(!dst);
  ZuCheck(assigned.length() == 5);
  ZuCheck(assigned.count_() == 2);
  assigned.clean();
  ZuCheck(!assigned.length());
  ZuCheck(!assigned);
}

void testEmptyIsPassive()
{
  ZuTestScope(testEmptyIsPassive);

  ZiRxStream<RxQueue> stream;
  unsigned calls = 0;

  ZuCheck(stream.empty());
  ZuCheck(!stream);
  ZuCheck(!stream.length());
  ZuCheck(!stream.span().length());
  ZuCheck(!stream.consume(
    [&calls](ZuSpan<uint8_t>) -> int64_t { ++calls; return 1; },
    [&calls](ZuSpan<uint8_t>) { ++calls; }));
  ZuCheck(!calls);
}

struct RxBase {
  unsigned maxSize_;
  unsigned headRoom_;
  unsigned tailRoom_;
  unsigned allocHead = 0;
  unsigned allocTail = 0;

  unsigned maxSize() const { return maxSize_; }
  unsigned headRoom() const { return headRoom_; }
  unsigned tailRoom() const { return tailRoom_; }
  void alloc(unsigned head, unsigned tail) {
    allocHead = head;
    allocTail = tail;
  }
};

void testGeometry()
{
  ZuTestScope(testGeometry);

  RxBase base{64, 1, 2};
  ZiRxLayer<RxBase> one{base, 3, 4};
  ZiRxLayer two{one, 5, 6};
  ZiRxLayer three{two, 7, 8};

  ZuCheck(three.maxSize() == 64);
  ZuCheck(three.headRoom() == 16);
  ZuCheck(three.tailRoom() == 20);
  base.alloc(three.headRoom(), three.tailRoom());
  ZuCheck(base.allocHead == 16);
  ZuCheck(base.allocTail == 20);
}

struct TrackedRx : public RxQueue::Node {
  using Node = RxQueue::Node;
  using Node::Node;
  ~TrackedRx() { ++*static_cast<unsigned *>(owner); }
};
using TrackedAlloc = Zi::IOBufAlloc<TrackedRx, 64, 256,
  ZuStringT<"ZiRxStreamTest.Lifetime">>;

void testRetainedCoalescedLifetime()
{
  ZuTestScope(testRetainedCoalescedLifetime);

  unsigned destroyed = 0, allocs = 0;
  ZiRxStream<RxQueue> stream;
  ZmRef<RxQueue::Node> input = new TrackedAlloc{&destroyed, 0};
  *static_cast<ZiIOBuf *>(input.ptr()) << "abcNEXT";
  auto original = static_cast<ZiIOBuf *>(input.ptr());
  auto data = original->data();
  stream.push(ZuMv(input));
  Zi::RxRange first;
  ZuCheck(extractN(stream, 3, first, allocs) == 3);
  auto tail = stream.range();
  ZuCheck(!allocs);
  ZuCheck(first.buf.ptr() == original && first.offset == 0);
  ZuCheck(tail.buf.ptr() == original && tail.offset == 3);
  ZuCheck(first.span().data() == data);
  ZuCheck(tail.span().data() == data + 3);
  ZuCheck(stream.advance(2) == 2);
  ZuCheck(original->skip == 0 && original->length == 7);
  stream.clean();
  ZuCheck(!destroyed);
  ZuCheck(spanEq(first.span(), "abc"));
  ZuCheck(spanEq(tail.span(), "NEXT"));
  first = {};
  ZuCheck(!destroyed);
  tail = {};
  ZuCheck(destroyed == 1);
}

void testSharedFragmentsUseFinalOwner()
{
  ZuTestScope(testSharedFragmentsUseFinalOwner);

  ZiRxStream<RxQueue> stream;
  auto first = mkBuf("abc");
  auto second = mkBuf("deNEXT");
  auto firstData = static_cast<ZiIOBuf *>(first.ptr())->data();
  auto secondData = static_cast<ZiIOBuf *>(second.ptr())->data();
  stream.push(first);
  stream.push(second);
  unsigned allocs = 0;
  Zi::RxRange out;
  ZuCheck(extractN(stream, 5, out, allocs) == 5);
  ZuCheck(allocs == 1);
  ZuCheck(out.buf.ptr() != first.ptr() && out.buf.ptr() != second.ptr());
  ZuCheck(spanEq(out.span(), "abcde"));
  ZuCheck(first->skip == 0 && first->length == 3);
  ZuCheck(second->skip == 0 && second->length == 6);
  ZuCheck(static_cast<ZiIOBuf *>(first.ptr())->data() == firstData);
  ZuCheck(stream.span().data() == secondData + 2);
  stream.clean();
  ZuCheck(spanEq(first->span(), "abc"));
  ZuCheck(spanEq(second->span(), "deNEXT"));
  ZuCheck(spanEq(out.span(), "abcde"));
}

void testRejectedConsolidationKeepsInput()
{
  ZuTestScope(testRejectedConsolidationKeepsInput);

  ZiRxStream<RxQueue> stream;
  auto first = mkBuf("abc");
  stream.push(first);
  stream.push(mkBuf("def"));
  unsigned remaining = 5, allocs = 0;
  Zi::RxRange out;
  auto n = stream.extract([&remaining](ZuBSpan span) -> int64_t {
    if (remaining > span.length()) { remaining -= span.length(); return 0; }
    return remaining;
  }, [&allocs]() -> ZmRef<RxQueue::Node> { ++allocs; return {}; }, out);
  ZuCheck(n == -1);
  ZuCheck(!out && allocs == 1);
  ZuCheck(stream.length() == 6 && stream.count_() == 2);
  ZuCheck(spanEq(stream.span(), "abc"));
  ZuCheck(first->skip == 0 && first->length == 3);
}

void testSplicedRangesRetainBacking()
{
  ZuTestScope(testSplicedRangesRetainBacking);

  unsigned destroyed = 0;
  ZiRxStream<RxQueue> src, payload, inner;
  ZmRef<RxQueue::Node> input = new TrackedAlloc{&destroyed, 0};
  *static_cast<ZiIOBuf *>(input.ptr()) << "[abc]NEXT";
  auto original = static_cast<ZiIOBuf *>(input.ptr());
  auto data = original->data();
  src.push(ZuMv(input));
  ZuCheck(spliceN(src, payload, 5, 1, 1) == 5);
  ZuCheck(payload.span().data() == data + 1);
  ZuCheck(src.span().data() == data + 5);
  ZuCheck(spliceN(payload, inner, 3, 1, 1) == 3);
  auto retained = inner.range();
  ZuCheck(retained.buf.ptr() == original && retained.offset == 2);
  ZuCheck(retained.span().data() == data + 2);
  ZuCheck(original->skip == 0 && original->length == 9);
  src.clean(); payload.clean(); inner.clean();
  ZuCheck(!destroyed);
  ZuCheck(spanEq(retained.span(), "b"));
  retained = {};
  ZuCheck(destroyed == 1);
}

void testExclusivePartialHeadReuse()
{
  ZuTestScope(testExclusivePartialHeadReuse);

  ZiRxStream<RxQueue> stream;
  auto first = mkBuf("abcd");
  auto original = static_cast<ZiIOBuf *>(first.ptr());
  stream.push(ZuMv(first));
  ZuCheck(stream.advance(2) == 2);
  stream.push(mkBuf("eNEXT"));
  unsigned allocs = 0;
  Zi::RxRange out;
  ZuCheck(extractN(stream, 3, out, allocs) == 3);
  ZuCheck(out.buf.ptr() == original);
  ZuCheck(!allocs);
  ZuCheck(spanEq(out.span(), "cde"));
  ZuCheck(spanEq(stream.span(), "NEXT"));
}

uint64_t sliceAllocs()
{
  using Slice = Zi::RxSlice<RxQueue::Node>;
  using Cache = ZmHeapCacheT<ZmNHeapID_<Slice>,
    ZmHeapAllocSize<sizeof(Slice)>{}, alignof(Slice), ZmNHeapSharded_<Slice>{}>;
  const auto &stats = Cache::instance()->cache()->stats();
  return stats.allocs.load_() + stats.heapAllocs.load_();
}

void testSlicesOnlyAtSharedBoundaries()
{
  ZuTestScope(testSlicesOnlyAtSharedBoundaries);

  auto before = sliceAllocs();
  ZiRxStream<RxQueue> src, dst;
  auto first = mkBuf("abc");
  auto firstData = static_cast<ZiIOBuf *>(first.ptr())->data();
  src.push(ZuMv(first));
  src.push(mkBuf("def"));
  ZuCheck(sliceAllocs() == before);
  ZuCheck(spliceN(src, dst, 3, 0, 0) == 3);
  ZuCheck(dst.span().data() == firstData);
  ZuCheck(sliceAllocs() == before);
  ZuCheck(spliceN(src, dst, 2, 0, 0) == 2);
  ZuCheck(sliceAllocs() == before + 1);
  ZuCheck(dst.length() == 5 && src.length() == 1);
  unsigned allocs = 0;
  Zi::RxRange out;
  ZuCheck(extractN(dst, 2, out, allocs) == 2);
  ZuCheck(!allocs && sliceAllocs() == before + 1);
  ZuCheck(out.span().data() == firstData);
  src.clean(); dst.clean();
  ZuCheck(spanEq(out.span(), "ab"));
}

} // namespace ZiRxStreamTest_

int main(int argc, char **argv)
{
  using namespace ZiRxStreamTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConsumeAcrossQueuedBuffers);
  ZuTestCall(testPushFiltersZeroLengthNodes);
  ZuTestCall(testConsumeStepsThroughQueuedBuffers);
  ZuTestCall(testConsumeGathersFragmentedFrame);
  ZuTestCall(testConsumePaddingAcrossQueuedBuffers);
  ZuTestCall(testCleanResetsState);
  ZuTestCall(testExtractCompleteFrames);
  ZuTestCall(testExtractWaitAndError);
  ZuTestCall(testSpliceWaitAndFullNode);
  ZuTestCall(testSpliceBoundaryRanges);
  ZuTestCall(testSpliceFragmentedAndEmpty);
  ZuTestCall(testSpliceFramingRejection);
  ZuTestCall(testSpliceTransform);
  ZuTestCall(testIncompleteAppend);
  ZuTestCall(testCopyAndEach);
  ZuTestCall(testMoveAndClean);
  ZuTestCall(testEmptyIsPassive);
  ZuTestCall(testGeometry);
  ZuTestCall(testRetainedCoalescedLifetime);
  ZuTestCall(testSharedFragmentsUseFinalOwner);
  ZuTestCall(testRejectedConsolidationKeepsInput);
  ZuTestCall(testSplicedRangesRetainBacking);
  ZuTestCall(testExclusivePartialHeadReuse);
  ZuTestCall(testSlicesOnlyAtSharedBoundaries);
  return 0;
}
