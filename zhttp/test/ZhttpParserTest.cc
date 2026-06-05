//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

namespace {

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 2048,
  ZuStringT<"ZhttpParserTest.Buf">>;
using RxStream = ZiRxStream<RxQueue>;

struct ResponseCtx {
  int		status = -1;
  bool		keyValue = false;
  unsigned	keyCalls = 0;
  unsigned	bodyCalls = 0;
};

using Parser = Zhttp::Parser<
  Zhttp::Response<ZuStringTL<"key">>,
  Zhttp::Body<1024>,
  ResponseCtx,
  RxStream>;

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

void testSelectedHeaderValueSplitAcrossRxBuffers()
{
  ZuTestScope(testSelectedHeaderValueSplitAcrossRxBuffers);

  static const char frag0[] =
    "HTTP/1.1 200 OK\r\n"
    "content-length: 5\r\n"
    "Key: Va";
  static const char frag1[] =
    "lue\r\n"
    "\r\n"
    "hello";

  Parser parser{RxStream{}};
  Zhttp::BodyData bodyData;

  auto status = [](auto &parser, int status) {
    parser.context.status = status;
  };
  auto key = [](auto &parser, int i, ZuCSpan value) {
    if (!i) {
      ++parser.context.keyCalls;
      parser.context.keyValue = value == "Value";
    }
  };
  auto kv = [](auto &, int) { };
  auto rcvd = [&bodyData](auto &parser) {
    ++parser.context.bodyCalls;
    bodyData << parser.body.span;
    return true;
  };

  parser.stream.push(mkBuf(frag0));

  int consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == 0, "incomplete trailing header line was consumed");
  ZuCHECK(!parser.header.complete, "header completed without final line");
  ZuCHECK(parser.context.keyCalls == 0, "selected key callback was premature");
  ZuCHECK(parser.context.bodyCalls == 0, "body callback was premature");
  ZuCHECK(parser.stream.count_() == 1, "partial header buffer was dequeued");
  ZuCHECK(spanEq(parser.stream.span(), frag0),
    "partial header buffer was advanced");

  parser.stream.push(mkBuf(frag1));

  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof(frag0) + sizeof(frag1) - 2,
    "complete response byte count mismatch");
  ZuCHECK(parser.header.complete, "header did not complete after next buffer");
  ZuCHECK(parser.header.status == 200, "response status not parsed");
  ZuCHECK(parser.context.status == 200, "status callback not invoked");
  ZuCHECK(parser.context.keyCalls == 1, "selected key callback count mismatch");
  ZuCHECK(parser.context.keyValue, "split selected header value mismatch");
  ZuCHECK(parser.body.complete, "body did not complete");
  ZuCHECK(parser.context.bodyCalls == 1, "body callback count mismatch");
  ZuCHECK(bodyData == "hello", "body data mismatch");
  ZuCHECK(!parser.stream, "stream still has data after full response");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSelectedHeaderValueSplitAcrossRxBuffers);
  return 0;
}
