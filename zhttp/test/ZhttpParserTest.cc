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
#include <zlib/ZtString.hh>

using namespace ZuTestUtil;

namespace {

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf, ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 2048,
  ZuStringT<"ZhttpParserTest.Buf">>;
using RxStream = ZiRxStream<RxQueue>;
using BodyData = ZtString<ZtStringHeapID<"ZhttpParserTest.BodyData">>;

ZmRef<RxQueue::Node> mkBuf(const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  auto iobuf = static_cast<ZiIOBuf *>(buf.ptr());
  if (n) iobuf->append(reinterpret_cast<const uint8_t *>(s), n);
  return buf;
}

bool spanEq(ZuBSpan span, const char *s)
{
  unsigned n = static_cast<unsigned>(::strlen(s));
  return span.length() == n && (!n || !::memcmp(span.data(), s, n));
}

using ResponseHeaders = ZuTypeList<
  ZuStringT<"key">, void,
  ZuStringT<"x-empty">, void>;
struct ResponseParser :
  public Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders, 1024> {
  using Base = Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders, 1024>;

  void status(unsigned v) { statusSeen = v; }
  void contentLength(uint64_t v) { contentLengthSeen = v; }
  void chunked() { chunkedSeen = true; }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"key">>{}) {
      ++keyCalls;
      keyValue = spanEq(value, "Value");
    } else if constexpr (ZuIsSame<Key, ZuStringT<"x-empty">>{}) {
      ++emptyCalls;
      emptyLen = value.length();
    }
  }

  void body(ZuBSpan span) {
    ++bodyCalls;
    bodyBytes += span.length();
    bodyData << span;
  }

  void complete(Zhttp::H1::ParserState::T state_) {
    completeState = state_;
    ++completeCalls;
  }

  void header(ZuCSpan key, ZuCSpan value) {
    ++runtimeCalls;
    runtimeKey.length(0);
    runtimeValue.length(0);
    runtimeKey << key;
    runtimeValue << value;
  }

  int				statusSeen = -1;
  int64_t			contentLengthSeen = -1;
  bool				keyValue = false;
  bool				chunkedSeen = false;
  unsigned			keyCalls = 0;
  unsigned			emptyCalls = 0;
  unsigned			emptyLen = 1;
  unsigned			bodyCalls = 0;
  uint64_t			bodyBytes = 0;
  unsigned			completeCalls = 0;
  unsigned			runtimeCalls = 0;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
  ZtString<>			runtimeKey;
  ZtString<>			runtimeValue;
};

using RequestHeaders = ZuTypeList<ZuStringT<"host">, void>;
struct RequestParser :
  public Zhttp::H1::Parser<RequestParser, true, RequestHeaders, 1024> {
  using Base = Zhttp::H1::Parser<RequestParser, true, RequestHeaders, 1024>;

  void operation(Zhttp::Method::T method_, ZuCSpan path_) {
    method = method_;
    path.length(0);
    path << path_;
  }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (ZuIsSame<Key, ZuStringT<"host">>{}) {
      ++hostCalls;
      host.length(0);
      host << ZuCSpan{
	reinterpret_cast<const char *>(value.data()), value.length()};
    }
  }

  void body(ZuBSpan span) {
    ++bodyCalls;
    bodyData << span;
  }

  void complete(Zhttp::H1::ParserState::T state_) {
    completeState = state_;
    ++completeCalls;
  }

  Zhttp::Method::T		method = -1;
  ZtString<>			path;
  ZtString<>			host;
  unsigned			hostCalls = 0;
  unsigned			bodyCalls = 0;
  unsigned			completeCalls = 0;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
};

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

  ResponseParser parser;
  RxStream stream;

  stream.push(mkBuf(frag0));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Headers,
    "parser did not stop at incomplete header");
  ZuCHECK(parser.statusSeen == 200, "response status not parsed");
  ZuCHECK(parser.contentLengthSeen == 5, "content-length was not parsed");
  ZuCHECK(parser.keyCalls == 0, "selected key callback was premature");
  ZuCHECK(parser.bodyCalls == 0, "body callback was premature");
  ZuCHECK(stream.count_() == 1, "partial header buffer was dequeued");

  stream.push(mkBuf(frag1));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "response did not complete after second fragment");
  ZuCHECK(parser.keyCalls == 1, "selected key callback count mismatch");
  ZuCHECK(parser.keyValue, "split selected header value mismatch");
  ZuCHECK(parser.bodyCalls == 1, "body callback count mismatch");
  ZuCHECK(parser.bodyData == "hello", "body data mismatch");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Complete,
    "complete callback mismatch");
  ZuCHECK(!stream, "stream still has data after full response");
}

void testCanonicalContentLengthHeader()
{
  ZuTestScope(testCanonicalContentLengthHeader);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "\r\n"
    "hello"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "canonical content-length response completed");
  ZuCHECK(parser.contentLengthSeen == 5,
    "canonical content-length was parsed");
  ZuCHECK(parser.bodyData == "hello", "canonical content-length body parsed");
}

void testRuntimeHeaderCallback()
{
  ZuTestScope(testRuntimeHeaderCallback);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "Key: Value\r\n"
    "X-Runtime: varied\r\n"
    "Content-Length: 0\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "runtime header response failed");
  ZuCHECK(parser.keyCalls == 1,
    "selected header was not delivered through typed callback");
  ZuCHECK(parser.runtimeCalls == 1,
    "unselected header callback count mismatch");
  ZuCHECK(parser.runtimeKey == "x-runtime",
    "unselected header key mismatch");
  ZuCHECK(parser.runtimeValue == "varied",
    "unselected header value mismatch");
}

void testResponseStartLineFragmentedAcrossManyRxBuffers()
{
  ZuTestScope(testResponseStartLineFragmentedAcrossManyRxBuffers);

  const char *frags[] = {
    "HT", "TP", "/1", ".1", " ", "2", "0", "0", " ", "O", "K", "\r"
  };
  static const char lf[] = "\n";
  static const char suffix[] =
    "content-length: 0\r\n"
    "\r\n";

  ResponseParser parser;
  RxStream stream;

  bool incomplete = true;
  for (auto frag : frags) {
    stream.push(mkBuf(frag));
    incomplete &= parser.process(stream) == Zhttp::H1::ParserState::Initial;
    incomplete &= parser.statusSeen == -1;
    incomplete &= parser.bodyCalls == 0;
  }
  ZuCHECK(incomplete, "response start line was parsed before final LF");

  stream.push(mkBuf(lf));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Headers,
    "response did not enter headers after start line");
  ZuCHECK(parser.statusSeen == 200, "response status not parsed");
  ZuCHECK(!stream, "fragmented response start-line buffers not consumed");

  stream.push(mkBuf(suffix));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "response suffix did not complete");
  ZuCHECK(parser.completeCalls == 1, "response complete callback mismatch");
  ZuCHECK(parser.bodyCalls == 0, "zero-length response delivered body");
  ZuCHECK(!stream, "stream still has data after response");
}

void testRequestStartLineFragmentedAcrossManyRxBuffers()
{
  ZuTestScope(testRequestStartLineFragmentedAcrossManyRxBuffers);

  const char *frags[] = {
    "G", "E", "T", " ", "/", "v", "1", "/x", "?", "q=1",
    " ", "HT", "TP", "/1", ".1", "\r"
  };
  static const char lf[] = "\n";
  static const char suffix[] =
    "host: example.com\r\n"
    "\r\n";

  RequestParser parser;
  RxStream stream;

  bool incomplete = true;
  for (auto frag : frags) {
    stream.push(mkBuf(frag));
    incomplete &= parser.process(stream) == Zhttp::H1::ParserState::Initial;
    incomplete &= parser.method < 0;
    incomplete &= parser.hostCalls == 0;
    incomplete &= parser.bodyCalls == 0;
  }
  ZuCHECK(incomplete, "request start line was parsed before final LF");

  stream.push(mkBuf(lf));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Headers,
    "request did not enter headers after start line");
  ZuCHECK(parser.method == Zhttp::Method::GET, "request method not parsed");
  ZuCHECK(parser.path == "/v1/x?q=1", "request path not parsed");
  ZuCHECK(!stream, "fragmented request start-line buffers not consumed");

  stream.push(mkBuf(suffix));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "request suffix did not complete");
  ZuCHECK(parser.hostCalls == 1, "host callback count mismatch");
  ZuCHECK(parser.host == "example.com", "request host not parsed");
  ZuCHECK(parser.completeCalls == 1, "request complete callback mismatch");
  ZuCHECK(!stream, "stream still has data after request");
}

void testInvalidRequestMethod()
{
  ZuTestScope(testInvalidRequestMethod);

  RequestParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "WHAT /bad HTTP/1.1\r\n"
    "host: example.com\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error,
    "invalid request method was not rejected");
  ZuCHECK(parser.method < 0 && !parser.hostCalls && !parser.bodyCalls,
    "invalid request method delivered callbacks");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Error,
    "invalid request method completion mismatch");
}

void testEmptySelectedHeaderValues()
{
  ZuTestScope(testEmptySelectedHeaderValues);

  {
    ResponseParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "HTTP/1.1 200 OK\r\n"
      "X-Empty:\r\n"
      "Content-Length: 0\r\n"
      "\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
      "empty selected header response failed");
    ZuCHECK(parser.emptyCalls == 1 && !parser.emptyLen,
      "empty selected header value was not delivered as empty");
  }
  {
    ResponseParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "HTTP/1.1 200 OK\r\n"
      "X-Empty:   \r\n"
      "Content-Length: 0\r\n"
      "\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
      "OWS-only selected header response failed");
    ZuCHECK(parser.emptyCalls == 1 && !parser.emptyLen,
      "OWS-only selected header value was not delivered as empty");
  }
}

void testInvalidContentLengthValues()
{
  ZuTestScope(testInvalidContentLengthValues);

  static const char *values[] = {
    "", "+5", "-5", "5x", "18446744073709551616"
  };
  for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    ResponseParser parser;
    RxStream stream;
    ZtString<> msg;
    msg << "HTTP/1.1 200 OK\r\ncontent-length: " << values[i] <<
      "\r\n\r\nhello";
    stream.push(mkBuf(msg));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error,
      "invalid content-length was not rejected");
    ZuCHECK(parser.contentLengthSeen < 0 && !parser.bodyCalls,
      "invalid content-length delivered callbacks");
    ZuCHECK(parser.completeCalls == 1 &&
	parser.completeState == Zhttp::H1::ParserState::Error,
      "invalid content-length completion mismatch");
  }
}

void testChunkedBodyAcrossRxBuffers()
{
  ZuTestScope(testChunkedBodyAcrossRxBuffers);

  const char *frags[] = {
    "HTTP/1.1 200 OK\r\n"
    "transfer-encoding: chunked\r\n"
    "\r\n",
    "1\r\n{\r\n",
    "9\r\n\"x\": 42, \r\n",
    "7\r\n\"y\": 42\r\n",
    "1\r\n}\r\n",
    "0\r\n\r\n"
  };

  ResponseParser parser;
  RxStream stream;

  for (unsigned i = 0; i < sizeof(frags) / sizeof(frags[0]); ++i) {
    stream.push(mkBuf(frags[i]));
    auto state = parser.process(stream);
    if (i + 1 < sizeof(frags) / sizeof(frags[0]))
      ZuCHECK(state != Zhttp::H1::ParserState::Complete,
	"chunked response completed too early");
  }

  ZuCHECK(parser.completeState == Zhttp::H1::ParserState::Complete,
    "chunked response did not complete");
  ZuCHECK(parser.chunkedSeen, "chunked transfer-encoding was not detected");
  ZuCHECK(parser.bodyBytes == 18, "chunked body total mismatch");
  ZuCHECK(parser.bodyData == "{\"x\": 42, \"y\": 42}",
    "chunked body data mismatch");
  ZuCHECK(!stream, "stream still has data after chunked response");
}

void testCloseDelimitedResponseBody()
{
  ZuTestScope(testCloseDelimitedResponseBody);

  const char *frags[] = {
    "HTTP/1.1 200 OK\r\n"
    "Key: Value\r\n"
    "\r\n"
    "hello, ",
    "world"
  };

  ResponseParser parser;
  RxStream stream;

  stream.push(mkBuf(frags[0]));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Body,
    "close-delimited response did not enter body state");
  ZuCHECK(parser.bodyData == "hello, ",
    "close-delimited first body fragment mismatch");
  ZuCHECK(parser.completeCalls == 0,
    "close-delimited response completed before EOF");
  ZuCHECK(!stream, "stream still has data after first body fragment");

  stream.push(mkBuf(frags[1]));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Body,
    "close-delimited response left body state before EOF");
  ZuCHECK(parser.bodyData == "hello, world",
    "close-delimited body mismatch before EOF");
  ZuCHECK(parser.completeCalls == 0,
    "close-delimited response completed before final EOF");
  ZuCHECK(!stream, "stream still has data after second body fragment");

  ZuCHECK(parser.eof() == Zhttp::H1::ParserState::Complete,
    "close-delimited response did not complete on EOF");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Complete,
    "close-delimited completion callback mismatch");
}

void testCloseDelimitedResponseTooLarge()
{
  ZuTestScope(testCloseDelimitedResponseTooLarge);

  ZtString<> msg;
  msg << "HTTP/1.1 200 OK\r\n\r\n";
  for (unsigned i = 0; i <= 1024; ++i) msg << 'x';

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(msg));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error,
    "oversized close-delimited response was not rejected");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Error,
    "oversized close-delimited completion mismatch");
}

void testNoBodyStatusWithoutLengthCompletesAtHeaders()
{
  ZuTestScope(testNoBodyStatusWithoutLengthCompletesAtHeaders);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 204 No Content\r\n"
    "Key: Value\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "no-body status did not complete at headers");
  ZuCHECK(parser.keyCalls == 1 && parser.keyValue,
    "no-body status header mismatch");
  ZuCHECK(parser.bodyCalls == 0, "no-body status delivered body");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Complete,
    "no-body status completion mismatch");
  ZuCHECK(!stream, "stream still has data after no-body status response");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSelectedHeaderValueSplitAcrossRxBuffers);
  ZuTestCall(testCanonicalContentLengthHeader);
  ZuTestCall(testRuntimeHeaderCallback);
  ZuTestCall(testInvalidRequestMethod);
  ZuTestCall(testEmptySelectedHeaderValues);
  ZuTestCall(testInvalidContentLengthValues);
  ZuTestCall(testResponseStartLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testRequestStartLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testChunkedBodyAcrossRxBuffers);
  ZuTestCall(testCloseDelimitedResponseBody);
  ZuTestCall(testCloseDelimitedResponseTooLarge);
  ZuTestCall(testNoBodyStatusWithoutLengthCompletesAtHeaders);
  return 0;
}
