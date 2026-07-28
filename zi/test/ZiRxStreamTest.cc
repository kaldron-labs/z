//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

using namespace ZuTestUtil;

namespace {

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 64, 256,
  ZuStringT<"ZiRxStreamTest.Buf">>;

ZmRef<RxQueue::Node> mkBuf(const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
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

struct ViewOwner {
  struct Part {
    uint32_t	length = 0;
    bool	payload = false;
    bool	final = false;
  };
  using Parts = ZtArray<Part, ZtArrayHeapID<"ZiRxStreamTest.Parts">>;

  ZiRxStream<RxQueue>	stream;
  Parts			parts;
  unsigned		part = 0;
  uint32_t		remaining = 0;
  uint32_t		offered = 0;
  unsigned		cancelled = 0;

  void add(uint32_t length, bool payload, bool final = false) {
    parts.push(Part{length, payload, final});
  }
  Zi::RxRefill rxRefill_() {
    if (offered) return {0, Zi::RxRefillState::Error};
    for (;;) {
      if (!remaining) {
	if (part >= parts.length()) return {0, Zi::RxRefillState::Wait};
	remaining = parts[part].length;
	if (!remaining) {
	  bool final = parts[part++].final;
	  if (final) return {0, Zi::RxRefillState::Final};
	  continue;
	}
      }
      auto span = stream.span();
      if (!span.length()) return {0, Zi::RxRefillState::Wait};
      auto &current = parts[part];
      unsigned n = span.length();
      if (n > remaining) n = remaining;
      if (!current.payload) {
	stream.advance(n);
	remaining -= n;
	if (!remaining) ++part;
	continue;
      }
      offered = n;
      return {
	n,
	current.final && n == remaining ?
	  Zi::RxRefillState::Final : Zi::RxRefillState::Input
      };
    }
  }
  auto rxSpan_() { return stream.span(); }
  unsigned rxAdvance_(unsigned n) {
    if (n > offered) return 0;
    unsigned advanced = stream.advance(n);
    offered -= advanced;
    remaining -= advanced;
    if (!remaining) ++part;
    return advanced;
  }
  void rxCancel_() {
    ++cancelled;
    stream.clean();
    part = parts.length();
    remaining = offered = 0;
  }
};

template <typename Layer>
ZtString<> drain(Layer &layer)
{
  ZtString<> out;
  while (layer.input()) {
    int64_t n = layer.consume(
      [](ZuBSpan span) -> int64_t { return span.length(); },
      [&out](ZuSpan<uint8_t> span) { out << span; });
    if (n <= 0) break;
  }
  return out;
}

template <typename Stream>
bool consumeExact(Stream &stream, unsigned n, const char *expected)
{
  if (!n)
    return !stream.consume(
      [](ZuBSpan) -> int64_t { return 0; },
      [](ZuBSpan) { });

  unsigned remaining = n;
  bool called = false;
  bool ok = false;
  int64_t consumed = stream.consume(
    [&remaining](ZuBSpan span) -> int64_t {
      if (remaining > span.length()) {
	remaining -= span.length();
	return 0;
      }
      return remaining;
    },
    [&called, &ok, &expected](ZuBSpan span) {
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
  ZuCheck(consumeExact(stream, 1, "a"));
  ZuCheck(stream.count_() == 2);
  ZuCheck(consumeExact(stream, 4, "bcde"));
  ZuCheck(!stream);
  ZuCheck(stream.count_() == 0);

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
  ZuCheck(!!stream);
  ZuCheck(consumeExact(stream, 2, "xy"));
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
    [](ZuBSpan) -> int64_t { return 0; },
    [&called](ZuBSpan) { called = true; });

  ZuCheck(!consumed);
  ZuCheck(!called);
  ZuCheck(stream.count_() == 2);

  unsigned seen = 0;
  consumed = stream.consume(
    [&seen](ZuBSpan span) -> int64_t {
      seen += span.length();
      return seen >= 5 ? span.length() : 0;
    },
    [&called](ZuBSpan span) {
      called = true;
      ZuCheck(spanEq(span, "abcde"));
    });

  ZuCheck(consumed == 5);
  ZuCheck(called);
  ZuCheck(!stream);
}

void testConsumePaddingAcrossQueuedBuffers()
{
  ZuTestScope(testConsumePaddingAcrossQueuedBuffers);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc\r"));
  stream.push(mkBuf("\nrest"));

  bool called = false;
  int64_t consumed = stream.consume<2>(
    [prevCR = false](ZuBSpan span) mutable -> int64_t {
      if (prevCR && span[0] == '\n') return 1;
      for (unsigned i = 1; i < span.length(); ++i)
	if (span[i - 1] == '\r' && span[i] == '\n')
	  return i + 1;
      prevCR = span[span.length() - 1] == '\r';
      return 0;
    },
    [&called](ZuBSpan span) {
      called = true;
      ZuCheck(spanEq(span, "abc"));
    });

  ZuCheck(consumed == 5);
  ZuCheck(called);
  ZuCheck(stream.count_() == 1);
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
  stream.clean();
  ZuCheck(stream.count_() == 0);
  ZuCheck(!stream);
  ZuCheck(!consumeExact(stream, 1, ""));

  stream.push(mkBuf("z"));
  ZuCheck(stream.count_() == 1);
  ZuCheck(consumeExact(stream, 1, "z"));
}

void testBoundedView()
{
  ZuTestScope(testBoundedView);

  ViewOwner owner;
  owner.add(3, true, true);
  owner.add(4, true, true);
  owner.stream.push(mkBuf("abcNEXT"));
  ZiRxLayer<ViewOwner> view{owner};

  ZuCheck(view.events() == Zi::RxEvent::Start);
  ZuCheck(view.input());
  ZuCheck(view.events() == Zi::RxEvent::Input);
  ZuCheck(drain(view) == "abc");
  ZuCheck(view.complete());
  ZuCheck(view.events() == Zi::RxEvent::Final);
  ZuCheck(spanEq(owner.stream.span(), "NEXT"));
}

void testBoundedViewHiddenAndFragmented()
{
  ZuTestScope(testBoundedViewHiddenAndFragmented);

  ViewOwner owner;
  owner.add(2, true);
  owner.add(2, false);
  owner.add(2, true, true);
  owner.stream.push(mkBuf("ab"));
  owner.stream.push(mkBuf("XX"));
  owner.stream.push(mkBuf("cd"));
  ZiRxLayer<ViewOwner> view{owner};

  ZuCheck(drain(view) == "abcd");
  ZuCheck(view.complete());
  ZuCheck(!owner.stream);
}

void testBoundedViewIncremental()
{
  ZuTestScope(testBoundedViewIncremental);

  ViewOwner owner;
  owner.add(4, true, true);
  owner.stream.push(mkBuf("a"));
  ZiRxLayer<ViewOwner> view{owner};

  ZuCheck(drain(view) == "a");
  ZuCheck(!view.complete());
  ZuCheck(view.empty());
  owner.stream.push(mkBuf("bcd"));
  ZuCheck(drain(view) == "bcd");
  ZuCheck(view.complete());
}

void testBoundedViewBoundsAndCleanup()
{
  ZuTestScope(testBoundedViewBoundsAndCleanup);

  ViewOwner owner;
  owner.add(3, true, true);
  owner.stream.push(mkBuf("abc"));
  ZiRxLayer<ViewOwner> view{owner};

  int64_t n = view.consume(
    [](ZuBSpan span) -> int64_t { return span.length() + 1; },
    [](ZuSpan<uint8_t>) { });
  ZuCheck(n < 0);
  ZuCheck(view.failed());
  ZuCheck(spanEq(owner.stream.span(), "abc"));
  view.clean();

  ViewOwner partial;
  partial.add(3, true, true);
  partial.stream.push(mkBuf("abc"));
  ZiRxLayer<ViewOwner> partialView{partial};
  n = partialView.consume(
    [](ZuBSpan) -> int64_t { return 1; },
    [](ZuSpan<uint8_t> span) { span[0] = 'A'; });
  ZuCheck(n == 1);
  ZuCheck(spanEq(partial.stream.span(), "bc"));
  partialView.clean();
  ZuCheck(partial.cancelled == 1);
  ZuCheck(!partial.stream);

  ViewOwner scoped;
  scoped.add(3, true, true);
  scoped.stream.push(mkBuf("abc"));
  {
    ZiRxLayer<ViewOwner> layer{scoped};
    n = layer.consume(
      [](ZuBSpan) -> int64_t { return 1; },
      [](ZuSpan<uint8_t>) { });
    ZuCheck(n == 1);
  }
  ZuCheck(scoped.cancelled == 1);
  ZuCheck(!scoped.stream);
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConsumeAcrossQueuedBuffers);
  ZuTestCall(testPushFiltersZeroLengthNodes);
  ZuTestCall(testConsumeStepsThroughQueuedBuffers);
  ZuTestCall(testConsumeGathersFragmentedFrame);
  ZuTestCall(testConsumePaddingAcrossQueuedBuffers);
  ZuTestCall(testCleanResetsState);
  ZuTestCall(testBoundedView);
  ZuTestCall(testBoundedViewHiddenAndFragmented);
  ZuTestCall(testBoundedViewIncremental);
  ZuTestCall(testBoundedViewBoundsAndCleanup);
  return 0;
}
