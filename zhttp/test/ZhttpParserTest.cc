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

struct RequestCtx {
  bool		method = false;
  bool		path = false;
  bool		host = false;
  unsigned	hostCalls = 0;
  unsigned	bodyCalls = 0;
};

using Parser = Zhttp::Parser<
  Zhttp::Response<ZuStringTL<"key">>,
  Zhttp::Body<1024>,
  ResponseCtx,
  RxStream>;

using RequestParser = Zhttp::Parser<
  Zhttp::Request<ZuStringTL<"host">>,
  Zhttp::Body<1024>,
  RequestCtx,
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
  ZuCHECK(consumed == sizeof(frag0) - sizeof("Key: Va"),
    "complete prefix lines were not consumed");
  ZuCHECK(!parser.header.complete, "header completed without final line");
  ZuCHECK(parser.header.status == 200, "response status not parsed");
  ZuCHECK(parser.context.status == 200, "status callback not invoked");
  ZuCHECK(parser.header.contentLength == 5, "content-length was not parsed");
  ZuCHECK(parser.context.keyCalls == 0, "selected key callback was premature");
  ZuCHECK(parser.context.bodyCalls == 0, "body callback was premature");
  ZuCHECK(parser.stream.count_() == 1, "partial header buffer was dequeued");
  ZuCHECK(spanEq(parser.stream.span(), "Key: Va"),
    "stream was not left at the incomplete header line");

  parser.stream.push(mkBuf(frag1));

  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof("Key: Va") + sizeof(frag1) - 2,
    "complete response byte count mismatch");
  ZuCHECK(parser.header.complete, "header did not complete after next buffer");
  ZuCHECK(parser.context.keyCalls == 1, "selected key callback count mismatch");
  ZuCHECK(parser.context.keyValue, "split selected header value mismatch");
  ZuCHECK(parser.body.complete, "body did not complete");
  ZuCHECK(parser.context.bodyCalls == 1, "body callback count mismatch");
  ZuCHECK(bodyData == "hello", "body data mismatch");
  ZuCHECK(!parser.stream, "stream still has data after full response");
}

void testSelectedHeaderLineFragmentedAcrossManyRxBuffers()
{
  ZuTestScope(testSelectedHeaderLineFragmentedAcrossManyRxBuffers);

  static const char prefix[] =
    "HTTP/1.1 200 OK\r\n"
    "content-length: 5\r\n";
  static const char frag0[] = "Ke";
  static const char frag1[] = "y:";
  static const char frag2[] = " ";
  static const char frag3[] = "V";
  static const char frag4[] = "alu";
  static const char frag5[] = "e";
  static const char frag6[] = "\r";
  static const char frag7[] = "\n";
  static const char suffix[] =
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

  parser.stream.push(mkBuf(prefix));
  int consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof(prefix) - 1,
    "complete prefix lines were not consumed");
  ZuCHECK(!parser.header.complete, "header completed before selected key");
  ZuCHECK(parser.header.status == 200, "response status not parsed");
  ZuCHECK(parser.context.status == 200, "status callback not invoked");
  ZuCHECK(parser.header.contentLength == 5, "content-length was not parsed");
  ZuCHECK(parser.context.keyCalls == 0, "selected key callback was premature");

  const char *frags[] = { frag0, frag1, frag2, frag3, frag4, frag5, frag6 };
  bool incomplete = true;
  for (unsigned i = 0; i < sizeof(frags) / sizeof(frags[0]); ++i) {
    parser.stream.push(mkBuf(frags[i]));
    consumed = parser.process(status, key, kv, rcvd);
    incomplete &= consumed == 0;
    incomplete &= !parser.header.complete;
    incomplete &= parser.context.keyCalls == 0;
    incomplete &= parser.context.bodyCalls == 0;
  }
  ZuCHECK(incomplete, "fragmented selected header was parsed before final LF");

  parser.stream.push(mkBuf(frag7));
  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof("Key: Value\r\n") - 1,
    "fragmented selected header byte count mismatch");
  ZuCHECK(!parser.header.complete, "header completed before empty line");
  ZuCHECK(parser.context.keyCalls == 1, "selected key callback count mismatch");
  ZuCHECK(parser.context.keyValue, "fragmented selected header value mismatch");
  ZuCHECK(parser.context.bodyCalls == 0, "body callback was premature");
  ZuCHECK(!parser.stream, "fragmented selected header buffers not consumed");

  parser.stream.push(mkBuf(suffix));
  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof(suffix) - 1, "suffix byte count mismatch");
  ZuCHECK(parser.header.complete, "header did not complete");
  ZuCHECK(parser.body.complete, "body did not complete");
  ZuCHECK(parser.context.bodyCalls == 1, "body callback count mismatch");
  ZuCHECK(bodyData == "hello", "body data mismatch");
  ZuCHECK(!parser.stream, "stream still has data after full response");
}

void testResponseStartLineFragmentedAcrossManyRxBuffers()
{
  ZuTestScope(testResponseStartLineFragmentedAcrossManyRxBuffers);

  static const char frag0[] = "HT";
  static const char frag1[] = "TP";
  static const char frag2[] = "/1";
  static const char frag3[] = ".1";
  static const char frag4[] = " ";
  static const char frag5[] = "2";
  static const char frag6[] = "0";
  static const char frag7[] = "0";
  static const char frag8[] = " ";
  static const char frag9[] = "O";
  static const char frag10[] = "K";
  static const char frag11[] = "\r";
  static const char frag12[] = "\n";
  static const char suffix[] =
    "content-length: 0\r\n"
    "\r\n";

  Parser parser{RxStream{}};

  auto status = [](auto &parser, int status) {
    parser.context.status = status;
  };
  auto key = [](auto &, int, ZuCSpan) { };
  auto kv = [](auto &, int) { };
  auto rcvd = [](auto &parser) {
    ++parser.context.bodyCalls;
    return true;
  };

  const char *frags[] = {
    frag0, frag1, frag2, frag3, frag4, frag5,
    frag6, frag7, frag8, frag9, frag10, frag11
  };
  bool incomplete = true;
  int consumed = 0;
  for (unsigned i = 0; i < sizeof(frags) / sizeof(frags[0]); ++i) {
    parser.stream.push(mkBuf(frags[i]));
    consumed = parser.process(status, key, kv, rcvd);
    incomplete &= consumed == 0;
    incomplete &= parser.header.status == -1;
    incomplete &= parser.context.status == -1;
    incomplete &= parser.context.bodyCalls == 0;
  }
  ZuCHECK(incomplete, "response start line was parsed before final LF");

  parser.stream.push(mkBuf(frag12));
  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof("HTTP/1.1 200 OK\r\n") - 1,
    "fragmented response start-line byte count mismatch");
  ZuCHECK(parser.header.status == 200, "response status not parsed");
  ZuCHECK(parser.context.status == 200, "status callback not invoked");
  ZuCHECK(!parser.header.complete, "header completed before field section");
  ZuCHECK(parser.context.bodyCalls == 0, "body callback was premature");
  ZuCHECK(!parser.stream, "fragmented response start-line buffers not consumed");

  parser.stream.push(mkBuf(suffix));
  consumed = parser.process(status, key, kv, rcvd);
  ZuCHECK(consumed == sizeof(suffix) - 1, "response suffix byte count mismatch");
  ZuCHECK(parser.header.complete, "response header did not complete");
  ZuCHECK(parser.body.complete, "response body did not complete");
  ZuCHECK(parser.context.bodyCalls == 1, "response body callback count mismatch");
  ZuCHECK(!parser.stream, "stream still has data after response");
}

void testRequestStartLineFragmentedAcrossManyRxBuffers()
{
  ZuTestScope(testRequestStartLineFragmentedAcrossManyRxBuffers);

  static const char frag0[] = "G";
  static const char frag1[] = "E";
  static const char frag2[] = "T";
  static const char frag3[] = " ";
  static const char frag4[] = "/";
  static const char frag5[] = "v";
  static const char frag6[] = "1";
  static const char frag7[] = "/x";
  static const char frag8[] = "?";
  static const char frag9[] = "q=1";
  static const char frag10[] = " ";
  static const char frag11[] = "HT";
  static const char frag12[] = "TP";
  static const char frag13[] = "/1";
  static const char frag14[] = ".1";
  static const char frag15[] = "\r";
  static const char frag16[] = "\n";
  static const char suffix[] =
    "host: example.com\r\n"
    "\r\n";

  RequestParser parser{RxStream{}};

  auto operation = [](auto &parser, Zhttp::Method::T method, ZuCSpan path) {
    parser.context.method = method == Zhttp::Method::GET;
    parser.context.path = path == "/v1/x?q=1";
  };
  auto key = [](auto &parser, int i, ZuCSpan value) {
    if (!i) {
      ++parser.context.hostCalls;
      parser.context.host = value == "example.com";
    }
  };
  auto kv = [](auto &, int) { };
  auto rcvd = [](auto &parser) {
    ++parser.context.bodyCalls;
    return true;
  };

  const char *frags[] = {
    frag0, frag1, frag2, frag3, frag4, frag5, frag6, frag7, frag8,
    frag9, frag10, frag11, frag12, frag13, frag14, frag15
  };
  bool incomplete = true;
  int consumed = 0;
  for (unsigned i = 0; i < sizeof(frags) / sizeof(frags[0]); ++i) {
    parser.stream.push(mkBuf(frags[i]));
    consumed = parser.process(operation, key, kv, rcvd);
    incomplete &= consumed == 0;
    incomplete &= !parser.context.method;
    incomplete &= !parser.context.path;
    incomplete &= parser.context.hostCalls == 0;
    incomplete &= parser.context.bodyCalls == 0;
  }
  ZuCHECK(incomplete, "request start line was parsed before final LF");

  parser.stream.push(mkBuf(frag16));
  consumed = parser.process(operation, key, kv, rcvd);
  ZuCHECK(consumed == sizeof("GET /v1/x?q=1 HTTP/1.1\r\n") - 1,
    "fragmented request start-line byte count mismatch");
  ZuCHECK(parser.context.method, "request method not parsed");
  ZuCHECK(parser.context.path, "request path not parsed");
  ZuCHECK(!parser.header.complete, "request header completed before fields");
  ZuCHECK(parser.context.hostCalls == 0, "host callback was premature");
  ZuCHECK(parser.context.bodyCalls == 0, "body callback was premature");
  ZuCHECK(!parser.stream, "fragmented request start-line buffers not consumed");

  parser.stream.push(mkBuf(suffix));
  consumed = parser.process(operation, key, kv, rcvd);
  ZuCHECK(consumed == sizeof(suffix) - 1, "request suffix byte count mismatch");
  ZuCHECK(parser.header.complete, "request header did not complete");
  ZuCHECK(parser.context.hostCalls == 1, "host callback count mismatch");
  ZuCHECK(parser.context.host, "request host not parsed");
  ZuCHECK(parser.body.complete, "request body did not complete");
  ZuCHECK(parser.context.bodyCalls == 1, "request body callback count mismatch");
  ZuCHECK(!parser.stream, "stream still has data after request");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSelectedHeaderValueSplitAcrossRxBuffers);
  ZuTestCall(testSelectedHeaderLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testResponseStartLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testRequestStartLineFragmentedAcrossManyRxBuffers);
  return 0;
}
