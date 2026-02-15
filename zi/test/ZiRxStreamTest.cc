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

void testBasicAdvanceAndNext()
{
  ZuTestScope(testBasicAdvanceAndNext);

  auto stream = Zi::rxStream<RxQueue>();

  ZuCheck(stream.empty());
  ZuCheck(!stream.advance(1));
  ZuCheck(!stream.next());

  stream.pushNode(mkBuf("abc"));
  stream.pushNode(mkBuf("de"));

  ZuCheck(stream.count_() == 2);
  ZuCheck(spanEq(stream.span(), "abc"));

  ZuCheck(stream.advance(1));
  ZuCheck(spanEq(stream.span(), "bc"));
  ZuCheck(stream.count_() == 2);

  ZuCheck(stream.advance(99));
  ZuCheck(spanEq(stream.span(), "de"));
  ZuCheck(stream.count_() == 1);

  ZuCheck(!stream.advance(0));
  ZuCheck(spanEq(stream.span(), "de"));

  ZuCheck(!stream.next());
  ZuCheck(stream.empty());
  ZuCheck(stream.count_() == 0);
}

void testRefreshSkipsZeroLengthNodes()
{
  ZuTestScope(testRefreshSkipsZeroLengthNodes);

  ZiRxStream<RxQueue> stream;

  stream.pushNode(mkBuf(""));
  stream.pushNode(mkBuf(""));
  stream.pushNode(mkBuf("xy"));
  stream.pushNode(mkBuf(""));

  ZuCheck(stream.count_() == 4);
  ZuCheck(!stream.empty());
  ZuCheck(spanEq(stream.span(), "xy"));
  ZuCheck(stream.count_() == 2);

  ZuCheck(stream.advance(2));
  ZuCheck(stream.empty());
  ZuCheck(stream.count_() == 0);
}

void testNextSkipsCurrentRemainder()
{
  ZuTestScope(testNextSkipsCurrentRemainder);

  ZiRxStream<RxQueue> stream;
  stream.pushNode(mkBuf("abcd"));
  stream.pushNode(mkBuf("ef"));

  ZuCheck(stream.advance(2));
  ZuCheck(spanEq(stream.span(), "cd"));

  ZuCheck(stream.next());
  ZuCheck(spanEq(stream.span(), "ef"));
  ZuCheck(stream.count_() == 1);

  ZuCheck(!stream.next());
  ZuCheck(stream.empty());
}

void testCleanResetsState()
{
  ZuTestScope(testCleanResetsState);

  ZiRxStream<RxQueue> stream;
  stream.pushNode(mkBuf("aaa"));
  stream.pushNode(mkBuf("bbb"));

  ZuCheck(stream.count_() == 2);
  stream.clean();
  ZuCheck(stream.count_() == 0);
  ZuCheck(stream.empty());
  ZuCheck(stream.span().length() == 0);

  stream.pushNode(mkBuf("z"));
  ZuCheck(stream.count_() == 1);
  ZuCheck(spanEq(stream.span(), "z"));
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testBasicAdvanceAndNext);
  ZuTestCall(testRefreshSkipsZeroLengthNodes);
  ZuTestCall(testNextSkipsCurrentRemainder);
  ZuTestCall(testCleanResetsState);
  return 0;
}
