//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZmList.hh>

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

bool spanEq(ZuSpan<uint8_t> span, const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  return span.length() == n && (!n || !::memcmp(span.data(), s, n));
}

void testAdvanceConsumesAcrossQueuedBuffers()
{
  ZuTestScope(testAdvanceConsumesAcrossQueuedBuffers);

  auto stream = Zi::rxStream<RxQueue>();

  ZuCheck(!stream);
  ZuCheck(!stream.advance(1));

  stream.push(mkBuf("abc"));
  stream.push(mkBuf("de"));

  ZuCheck(stream.count_() == 2);
  ZuCheck(spanEq(stream.span(), "abc"));

  ZuCheck(stream.advance(1));
  ZuCheck(spanEq(stream.span(), "bc"));
  ZuCheck(stream.count_() == 2);

  ZuCheck(stream.advance(99));
  ZuCheck(!stream);
  ZuCheck(stream.count_() == 0);

  ZuCheck(!stream.advance(0));
  ZuCheck(!stream.advance(2));
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
  ZuCheck(spanEq(stream.span(), "xy"));
  ZuCheck(stream.count_() == 1);

  ZuCheck(stream.advance(2));
  ZuCheck(!stream);
  ZuCheck(stream.count_() == 0);
}

void testNextSkipsCurrentRemainder()
{
  ZuTestScope(testNextSkipsCurrentRemainder);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abcd"));
  stream.push(mkBuf("ef"));

  ZuCheck(stream.advance(2));
  ZuCheck(spanEq(stream.span(), "cd"));

  ZuCheck(stream.advance(2));
  ZuCheck(spanEq(stream.span(), "ef"));
  ZuCheck(stream.count_() == 1);

  ZuCheck(stream.advance(2));
  ZuCheck(!stream);
}

void testSpansIteratesWithoutConsuming()
{
  ZuTestScope(testSpansIteratesWithoutConsuming);

  ZiRxStream<RxQueue> stream;
  stream.push(mkBuf("abc"));
  stream.push(mkBuf("de"));

  char seen[6] = {};
  unsigned len = 0;
  unsigned calls = 0;
  bool completed = stream.spans([&](ZuSpan<uint8_t> span) {
    ::memcpy(seen + len, span.data(), span.length());
    len += span.length();
    ++calls;
    return true;
  });

  ZuCheck(completed);
  ZuCheck(calls == 2);
  ZuCheck(len == 5 && !::memcmp(seen, "abcde", 5));
  ZuCheck(stream.count_() == 2);
  ZuCheck(spanEq(stream.span(), "abc"));

  calls = 0;
  completed = stream.spans([&](ZuSpan<uint8_t>) {
    ++calls;
    return false;
  });

  ZuCheck(!completed);
  ZuCheck(calls == 1);
  ZuCheck(stream.count_() == 2);
  ZuCheck(spanEq(stream.span(), "abc"));
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
  ZuCheck(stream.span().length() == 0);

  stream.push(mkBuf("z"));
  ZuCheck(stream.count_() == 1);
  ZuCheck(spanEq(stream.span(), "z"));
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testAdvanceConsumesAcrossQueuedBuffers);
  ZuTestCall(testPushFiltersZeroLengthNodes);
  ZuTestCall(testNextSkipsCurrentRemainder);
  ZuTestCall(testSpansIteratesWithoutConsuming);
  ZuTestCall(testCleanResetsState);
  return 0;
}
