//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>
#include <zlib/ZtString.hh>

using namespace ZuTestUtil;

namespace ZhttpParserTest_ {

using RxQueue = ZiRxQueue;
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 2048,
  ZuStringT<"ZhttpParserTest.Buf">>;
using RxStream = ZiRxStream<RxQueue>;
using BodyData = ZtString<ZtStringHeapID<"ZhttpParserTest.BodyData">>;

ZmRef<RxQueue::Node> mkBuf(const char *s)
{
  unsigned n = ::strlen(s);
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  if (n) buf->append(ZuCSpan{s, n});
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
  public Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders> {
  using Base = Zhttp::H1::Parser<ResponseParser, false, ResponseHeaders>;

  ResponseParser() : Base{1024} { }

  bool enable1xx() const { return informational; }
  void status(unsigned v) { statusSeen = v; ++statusCalls; }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyType = type;
    bodyLength = length;
    ++bodyInfoCalls;
    if (type == Zhttp::BodyType::Fixed) {
      contentLengthSeen = length;
      ++contentLengthCalls;
    }
    chunkedSeen = type == Zhttp::BodyType::Streamed;
  }

  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan value) {
    if constexpr (Key{}() == "key") {
      keySection = section;
      ++keyCalls;
      keyValue = spanEq(value, "Value");
    } else if constexpr (Key{}() == "x-empty") {
      ++emptyCalls;
      emptyLen = value.length();
    }
  }

  template <typename Rx>
  bool body(Rx &rx) {
    ++bodyCalls;
    if (recordLen) {
      unsigned remaining = recordLen;
      (void)rx.consume(
	[&remaining](ZuBSpan span) -> int64_t {
	  if (remaining > span.length()) {
	    remaining -= span.length();
	    return 0;
	  }
	  return remaining;
	},
	[this](ZuBSpan span) {
	  bodyBytes += span.length();
	  bodyData << span;
	});
      return true;
    }
    return Zhttp::bodyEach(rx, [this](ZuBSpan span) {
      bodyBytes += span.length();
      bodyData << span;
    });
  }

  void complete(Zhttp::H1::ParserState::T state_) {
    completeState = state_;
    ++completeCalls;
  }

  void header(
      Zhttp::FieldSection::T section, ZuBSpan key, ZuBSpan value) {
    runtimeSection = section;
    ++runtimeCalls;
    runtimeKey.length(0);
    runtimeValue.length(0);
    runtimeKey << ZuCSpan{key};
    runtimeValue << ZuCSpan{value};
  }

  int				statusSeen = -1;
  int64_t			contentLengthSeen = -1;
  uint64_t			bodyLength = 0;
  Zhttp::BodyType::T		bodyType = Zhttp::BodyType::None;
  Zhttp::FieldSection::T	keySection = Zhttp::FieldSection::Invalid;
  Zhttp::FieldSection::T	runtimeSection = Zhttp::FieldSection::Invalid;
  bool				keyValue = false;
  bool				chunkedSeen = false;
  bool				informational = false;
  unsigned			keyCalls = 0;
  unsigned			emptyCalls = 0;
  unsigned			emptyLen = 1;
  unsigned			bodyCalls = 0;
  unsigned			statusCalls = 0;
  uint64_t			bodyBytes = 0;
  unsigned			completeCalls = 0;
  unsigned			contentLengthCalls = 0;
  unsigned			bodyInfoCalls = 0;
  unsigned			runtimeCalls = 0;
  unsigned			recordLen = 0;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
  ZtString<>			runtimeKey;
  ZtString<>			runtimeValue;
};

using RequestHeaders = ZuTypeList<ZuStringT<"host">, void>;
struct RequestParser :
  public Zhttp::H1::Parser<RequestParser, true, RequestHeaders> {
  using Base = Zhttp::H1::Parser<RequestParser, true, RequestHeaders>;

  RequestParser() : Base{1024} { }

  void operation(
    Zhttp::Method::T method_, const Zhttp::RequestTarget &target) {
    method = method_;
    path.length(0);
    path << ZuCSpan{target.raw};
  }

  void bodyInfo(Zhttp::BodyType::T type_, uint64_t length_) {
    bodyType = type_;
    bodyLength = length_;
    ++bodyInfoCalls;
  }

  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan value) {
    if constexpr (Key{}() == "host") {
      ++hostCalls;
      host.length(0);
      host << value;
    }
  }

  template <typename Rx>
  bool body(Rx &rx) {
    ++bodyCalls;
    return Zhttp::bodyEach(
      rx, [this](ZuBSpan span) { bodyData << span; });
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
  unsigned			bodyInfoCalls = 0;
  uint64_t			bodyLength = 0;
  Zhttp::BodyType::T		bodyType = Zhttp::BodyType::Fixed;
  Zhttp::H1::ParserState::T		completeState = Zhttp::H1::ParserState::Initial;
  BodyData			bodyData;
};

struct LimitedRequestParser :
  public Zhttp::H1::Parser<
    LimitedRequestParser, true, ZuTypeList<>, 16, 32> {
  void complete(Zhttp::H1::ParserState::T) { ++completeCalls; }
  unsigned completeCalls = 0;
};

template <bool Request>
struct RejectingParser :
  public Zhttp::H1::Parser<RejectingParser<Request>, Request> {
  using Base = Zhttp::H1::Parser<RejectingParser<Request>, Request>;

  RejectingParser() : Base{1024} { }

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) {
    ++operations;
  }
  void status(unsigned) { ++statuses; }
  template <typename Rx>
  bool body(Rx &rx) {
    ++bodies;
    if (consume)
      (void)rx.consume(
	[](ZuBSpan) -> int64_t { return 1; }, [](ZuBSpan) { });
    return false;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T section, ZuBSpan) {
    if (section == Zhttp::FieldSection::Trailers) ++trailers;
  }
  void complete(Zhttp::H1::ParserState::T state) {
    completeState = state;
    ++completions;
  }

  unsigned operations = 0;
  unsigned statuses = 0;
  unsigned bodies = 0;
  unsigned trailers = 0;
  unsigned completions = 0;
  bool consume = false;
  Zhttp::H1::ParserState::T completeState =
    Zhttp::H1::ParserState::Initial;
};

void testBodyRejection()
{
  ZuTestScope(testBodyRejection);

  {
    Zhttp::Parser parser;
    RxStream body;
    ZuCHECK(!parser.body(body), "base Parser accepted an unhandled body");
  }

  {
    RejectingParser<true> parser;
    parser.consume = true;
    RxStream stream;
    stream.push(mkBuf(
      "POST /one HTTP/1.1\r\n"
      "content-length: 5\r\n\r\n"
      "hello"
      "GET /two HTTP/1.1\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::BodyRejected &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible && parser.operations == 1 &&
	parser.bodies == 1 && parser.completions == 1 &&
	parser.completeState == Zhttp::H1::ParserState::Error && stream,
      "fixed request body rejection did not stop before pipelined input");
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.operations == 1 && parser.bodies == 1 && parser.completions == 1,
      "rejected fixed body produced duplicate callbacks");
  }

  {
    RejectingParser<true> parser;
    RxStream stream;
    stream.push(mkBuf(
      "POST / HTTP/1.1\r\n"
      "transfer-encoding: chunked\r\n\r\n"
      "3\r\nabc\r\n0\r\nx-test: ignored\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::BodyRejected &&
	parser.bodies == 1 && !parser.trailers && parser.completions == 1,
      "chunked body rejection delivered later callbacks");
  }

  {
    RejectingParser<false> parser;
    RxStream stream;
    stream.push(mkBuf(
      "HTTP/1.1 200 OK\r\ncontent-length: 3\r\n\r\nabc"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::BodyRejected &&
	!parser.error().responsePossible && parser.statuses == 1 &&
	parser.bodies == 1 && parser.completions == 1,
      "response body rejection classification mismatch");
  }

  ZuCHECK(Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::BodyRejected) == 400,
    "body rejection status mapping mismatch");
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

  ResponseParser parser;
  RxStream stream;

  stream.push(mkBuf(frag0));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Headers,
    "parser did not stop at incomplete header");
  ZuCHECK(parser.statusSeen == 200, "response status not parsed");
  ZuCHECK(parser.contentLengthSeen < 0,
    "body metadata callback was premature");
  ZuCHECK(parser.keyCalls == 0, "selected key callback was premature");
  ZuCHECK(parser.bodyCalls == 0, "body callback was premature");
  ZuCHECK(stream.count_() == 1, "partial header buffer was dequeued");

  stream.push(mkBuf(frag1));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "response did not complete after second fragment");
  ZuCHECK(parser.keyCalls == 1, "selected key callback count mismatch");
  ZuCHECK(parser.keyValue, "split selected header value mismatch");
  ZuCHECK(parser.keySection == Zhttp::FieldSection::Final,
    "selected header section mismatch");
  ZuCHECK(parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && parser.bodyLength == 5,
    "fixed positive body metadata mismatch");
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
    "Key-Junk: suffix\r\n"
    "Content-Length-Junk: 7\r\n"
    "X-Runtime: varied\r\n"
    "Content-Length: 0\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "runtime header response failed");
  ZuCHECK(parser.keyCalls == 1,
    "selected header was not delivered through typed callback");
  ZuCHECK(parser.runtimeCalls == 3,
    "unselected header callback count mismatch");
  ZuCHECK(parser.contentLengthCalls == 1,
    "suffixed content-length was treated as canonical");
  ZuCHECK(parser.runtimeKey == "x-runtime",
    "unselected header key mismatch");
  ZuCHECK(parser.runtimeValue == "varied",
    "unselected header value mismatch");
  ZuCHECK(parser.runtimeSection == Zhttp::FieldSection::Final,
    "unselected header section mismatch");
  ZuCHECK(parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && !parser.bodyLength,
    "fixed empty body metadata mismatch");
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
  ZuCHECK(parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::None && !parser.bodyLength,
    "request without framing did not report no body");
  ZuCHECK(parser.completeCalls == 1, "request complete callback mismatch");
  ZuCHECK(!stream, "stream still has data after request");
}

void testInvalidRequestMethod()
{
  ZuTestScope(testInvalidRequestMethod);

  auto test = [](const char *request) {
    RequestParser parser;
    RxStream stream;
    stream.push(mkBuf(request));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error,
      "invalid request method was not rejected");
    ZuCHECK(parser.method < 0 && !parser.hostCalls && !parser.bodyCalls,
      "invalid request method delivered callbacks");
    ZuCHECK(parser.completeCalls == 1 &&
	parser.completeState == Zhttp::H1::ParserState::Error,
      "invalid request method completion mismatch");
    ZuCHECK(parser.error().code == Zhttp::RequestErrorCode::NotImplemented &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible,
      "invalid request method classification mismatch");
  };

  test(
    "WHAT /bad HTTP/1.1\r\n"
    "host: example.com\r\n"
    "\r\n");
  test(
    "GETX /bad HTTP/1.1\r\n"
    "host: example.com\r\n"
    "\r\n");
}

void testRequestErrorClassification()
{
  ZuTestScope(testRequestErrorClassification);

  {
    RequestParser parser;
    RxStream stream;
    stream.push(mkBuf("GET / HTTP/9.9\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::VersionUnsupported &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible,
      "unsupported request version classification mismatch");
    ZuCHECK(Zhttp::requestErrorStatus(parser.error().code) == 505,
      "unsupported request version status mismatch");
  }

  {
    RequestParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "POST / HTTP/1.1\r\n"
      "content-length: 1025\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::ContentTooLarge &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible,
      "oversized request body classification mismatch");
    ZuCHECK(Zhttp::requestErrorStatus(parser.error().code) == 413,
      "oversized request body status mismatch");
  }

  {
    RequestParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "POST / HTTP/1.1\r\n"
      "transfer-encoding: unknown\r\n"
      "content-length: 1025\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::NotImplemented,
      "first request error was overwritten");
    parser.reset();
    ZuCHECK(parser.error().code == Zhttp::RequestErrorCode::Malformed &&
	!parser.error().responsePossible,
      "request error latch was not reset");
  }

  {
    LimitedRequestParser parser;
    RxStream stream;
    stream.push(mkBuf("GET /0123456789 HTTP/1.1\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::TargetTooLong &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible,
      "oversized H1 request target classification mismatch");
  }

  {
    LimitedRequestParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "GET / HTTP/1.1\r\n"
      "x-one: 1234567890\r\n"
      "x-two: 1234567890\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	parser.error().code == Zhttp::RequestErrorCode::HeadersTooLarge &&
	parser.error().scope == Zhttp::RequestErrorScope::Connection &&
	parser.error().responsePossible,
      "oversized H1 header section classification mismatch");
  }

  ZuCHECK(Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::Malformed) == 400 &&
      Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::ContentTooLarge) == 413 &&
      Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::TargetTooLong) == 414 &&
      Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::HeadersTooLarge) == 431 &&
      Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::NotImplemented) == 501 &&
      Zhttp::requestErrorStatus(Zhttp::RequestErrorCode::VersionUnsupported) == 505,
    "standard request error status mapping mismatch");
}

void testRuntimeBodyLimit()
{
  ZuTestScope(testRuntimeBodyLimit);

  static const char request[] =
    "POST / HTTP/1.1\r\n"
    "Content-Length: 5\r\n\r\n"
    "abcde";
  RequestParser parser;
  parser.reset(4);
  RxStream rejected;
  rejected.push(mkBuf(request));
  ZuCHECK(parser.bodyMax() == 4 &&
      parser.process(rejected) == Zhttp::H1::ParserState::Error &&
      parser.error().code == Zhttp::RequestErrorCode::ContentTooLarge,
    "runtime body limit did not reject an oversized request");

  parser.reset(5);
  RxStream accepted;
  accepted.push(mkBuf(request));
  ZuCHECK(parser.bodyMax() == 5 &&
      parser.process(accepted) == Zhttp::H1::ParserState::Complete &&
      parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && parser.bodyLength == 5 &&
      parser.bodyData == "abcde",
    "runtime body limit did not admit an exact-size request");
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
    "0\r\nKey: Value\r\n\r\n"
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
  ZuCHECK(parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Streamed && !parser.bodyLength,
    "chunked body metadata mismatch");
  ZuCHECK(parser.keySection == Zhttp::FieldSection::Trailers,
    "trailer header section mismatch");
  ZuCHECK(parser.bodyBytes == 18, "chunked body total mismatch");
  ZuCHECK(parser.bodyData == "{\"x\": 42, \"y\": 42}",
    "chunked body data mismatch");
  ZuCHECK(!stream, "stream still has data after chunked response");
}

void testChunkLengthSyntax()
{
  ZuTestScope(testChunkLengthSyntax);

  {
    ResponseParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "HTTP/1.1 200 OK\r\n"
      "transfer-encoding: chunked\r\n"
      "\r\n"
      "3;name=value\r\nabc\r\n0\r\n\r\n"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete &&
	parser.bodyData == "abc",
      "chunk extension was not ignored");
  }
  static const char *lengths[] = {
    "10000000000000000", "0x3", "3x"
  };
  for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
    ResponseParser parser;
    RxStream stream;
    ZtString<> msg;
    msg << "HTTP/1.1 200 OK\r\n"
	"transfer-encoding: chunked\r\n\r\n" << lengths[i] << "\r\n";
    stream.push(mkBuf(msg));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Error &&
	!parser.bodyCalls,
      "invalid chunk length was accepted");
  }
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
  ZuCHECK(!parser.bodyData && stream.length() == 7,
    "close-delimited body was published before EOF");
  ZuCHECK(parser.completeCalls == 0,
    "close-delimited response completed before EOF");
  ZuCHECK(parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Streamed && !parser.bodyLength,
    "close-delimited body metadata mismatch");

  stream.push(mkBuf(frags[1]));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Body,
    "close-delimited response left body state before EOF");
  ZuCHECK(!parser.bodyData && stream.length() == 12,
    "close-delimited body was published before final EOF");
  ZuCHECK(parser.completeCalls == 0,
    "close-delimited response completed before final EOF");
  ZuCHECK(parser.eof() == Zhttp::H1::ParserState::Complete,
    "close-delimited response did not complete on EOF");
  ZuCHECK(parser.bodyData == "hello, world" && !stream,
    "close-delimited body was not atomically published at EOF");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Complete,
    "close-delimited completion callback mismatch");
}

void testContentLengthBodyTransactional()
{
  ZuTestScope(testContentLengthBodyTransactional);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "content-length: 5\r\n"
    "\r\n"
    "ab"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Body &&
      !parser.bodyCalls && stream.length() == 2,
    "partial content-length body was consumed or published");

  stream.push(mkBuf("cdeNEXT"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete &&
      parser.bodyCalls == 1 && parser.bodyData == "abcde",
    "complete content-length body was not atomically published");
  ZuCHECK(stream.length() == 4 && ZuCSpan{stream.span()} == "NEXT",
    "content-length body consumed pipelined bytes");
}

void testChunkBodyTransactional()
{
  ZuTestScope(testChunkBodyTransactional);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "transfer-encoding: chunked\r\n"
    "\r\n"
    "4\r\nab"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Chunk &&
      !parser.bodyCalls && stream.length() == 2,
    "partial chunk data was consumed or published");

  stream.push(mkBuf("cd\r\n0\r\n\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete &&
      parser.bodyCalls == 1 && parser.bodyData == "abcd" && !stream,
    "complete chunk frame was not atomically published");
}

void testAppFrameAcrossChunks()
{
  ZuTestScope(testAppFrameAcrossChunks);

  ResponseParser parser;
  parser.recordLen = 6;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "transfer-encoding: chunked\r\n"
    "\r\n"
    "3\r\nabc\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::ChunkHdr &&
      parser.bodyCalls == 1 && !parser.bodyData,
    "partial application frame was not retained after first chunk");

  stream.push(mkBuf("3\r\ndef\r\n0\r\n\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete &&
      parser.bodyCalls == 2 && parser.bodyData == "abcdef",
    "application frame did not span complete HTTP chunks");
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
    "Content-Length: 99\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "no-body status did not complete at headers");
  ZuCHECK(parser.keyCalls == 1 && parser.keyValue,
    "no-body status header mismatch");
  ZuCHECK(parser.keySection == Zhttp::FieldSection::Final &&
      parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::None && !parser.bodyLength,
    "no-body status metadata mismatch");
  ZuCHECK(parser.bodyCalls == 0, "no-body status delivered body");
  ZuCHECK(parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H1::ParserState::Complete,
    "no-body status completion mismatch");
  ZuCHECK(!stream, "stream still has data after no-body status response");
}

void testInformationalThenFinalResponse()
{
  ZuTestScope(testInformationalThenFinalResponse);

  ResponseParser parser;
  parser.informational = true;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 103 Early Hints\r\n"
    "Key: Value\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Initial,
    "informational response restores the initial parser state");
  ZuCHECK(parser.progressed(),
    "informational response reports progress without completion");
  ZuCHECK(parser.statusSeen == 103 && parser.statusCalls == 1 &&
      parser.completeCalls == 0 && !stream,
    "informational response preserves the final-response parse");
  ZuCHECK(parser.keySection == Zhttp::FieldSection::Informational &&
      !parser.bodyInfoCalls,
    "informational header section/body metadata mismatch");

  stream.push(mkBuf(
    "HTTP/1.1 200 OK\r\n"
    "Key: Value\r\n"
    "Content-Length: 4\r\n"
    "\r\n"
    "pong"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
    "final response completes after informational response");
  ZuCHECK(parser.statusSeen == 200 && parser.statusCalls == 2 &&
      parser.bodyData == "pong" && parser.completeCalls == 1,
    "final response callbacks follow informational callbacks exactly once");
  ZuCHECK(parser.keySection == Zhttp::FieldSection::Final &&
      parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && parser.bodyLength == 4,
    "final header section/body metadata mismatch");
}

void testInformationalSuppressed()
{
  ZuTestScope(testInformationalSuppressed);

  ResponseParser parser;
  RxStream stream;
  stream.push(mkBuf(
    "HTTP/1.1 103 Early Hints\r\n"
    "Key: Value\r\n"
    "\r\n"
    "HTTP/1.1 204 No Content\r\n"
    "\r\n"));
  ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete &&
      parser.statusSeen == 204 && parser.statusCalls == 1 &&
      !parser.keyCalls && parser.completeCalls == 1 && !stream,
    "informational response callbacks were not suppressed by default");
}

void testUpgradeLeavesInput()
{
  ZuTestScope(testUpgradeLeavesInput);

  {
    ResponseParser parser;
    parser.informational = true;
    RxStream stream;
    stream.push(mkBuf(
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: opaque\r\n"
      "Connection: Upgrade\r\n"
      "\r\n"
      "first"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
      "101 response did not complete at the header boundary");
    ZuCHECK(parser.statusSeen == 101 && parser.completeCalls == 1,
      "101 response completion callbacks mismatch");
    ZuCHECK(ZuCSpan{stream.span()} == "first",
      "101 response consumed coalesced upgraded-stream input");
  }

  {
    RequestParser parser;
    RxStream stream;
    stream.push(mkBuf(
      "GET /chat HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "Upgrade: opaque\r\n"
      "Connection: Upgrade\r\n"
      "\r\n"
      "first"));
    ZuCHECK(parser.process(stream) == Zhttp::H1::ParserState::Complete,
      "Upgrade request did not complete at the header boundary");
    ZuCHECK(parser.method == Zhttp::Method::GET &&
	parser.path == "/chat" && parser.completeCalls == 1,
      "Upgrade request completion callbacks mismatch");
    ZuCHECK(ZuCSpan{stream.span()} == "first",
      "Upgrade request consumed coalesced upgraded-stream input");
  }
}

void testEarlyDataSafeRequestPolicy()
{
  ZuTestScope(testEarlyDataSafeRequestPolicy);

  ZuCHECK(Zhttp::earlyDataSafeRequest(Zhttp::Method::GET, false),
    "GET without body should be 0-RTT eligible");
  ZuCHECK(Zhttp::earlyDataSafeRequest(Zhttp::Method::HEAD, false),
    "HEAD without body should be 0-RTT eligible");
  ZuCHECK(Zhttp::earlyDataSafeRequest(Zhttp::Method::OPTIONS, false),
    "OPTIONS without body should be 0-RTT eligible");
  ZuCHECK(!Zhttp::earlyDataSafeRequest(Zhttp::Method::GET, true),
    "body-bearing request should not be 0-RTT eligible by default");
  ZuCHECK(!Zhttp::earlyDataSafeRequest(Zhttp::Method::POST, false),
    "POST should not be 0-RTT eligible by default");
  ZuCHECK(!Zhttp::earlyDataSafeRequest(Zhttp::Method::PUT, false),
    "PUT should not be 0-RTT eligible by default");
  ZuCHECK(!Zhttp::earlyDataSafeRequest(Zhttp::Method::DELETE, false),
    "DELETE should not be 0-RTT eligible by default");
}

} // namespace ZhttpParserTest_

using namespace ZhttpParserTest_;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSelectedHeaderValueSplitAcrossRxBuffers);
  ZuTestCall(testCanonicalContentLengthHeader);
  ZuTestCall(testRuntimeHeaderCallback);
  ZuTestCall(testInvalidRequestMethod);
  ZuTestCall(testRequestErrorClassification);
  ZuTestCall(testBodyRejection);
  ZuTestCall(testRuntimeBodyLimit);
  ZuTestCall(testEmptySelectedHeaderValues);
  ZuTestCall(testInvalidContentLengthValues);
  ZuTestCall(testResponseStartLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testRequestStartLineFragmentedAcrossManyRxBuffers);
  ZuTestCall(testChunkedBodyAcrossRxBuffers);
  ZuTestCall(testChunkLengthSyntax);
  ZuTestCall(testContentLengthBodyTransactional);
  ZuTestCall(testChunkBodyTransactional);
  ZuTestCall(testAppFrameAcrossChunks);
  ZuTestCall(testCloseDelimitedResponseBody);
  ZuTestCall(testCloseDelimitedResponseTooLarge);
  ZuTestCall(testNoBodyStatusWithoutLengthCompletesAtHeaders);
  ZuTestCall(testInformationalSuppressed);
  ZuTestCall(testInformationalThenFinalResponse);
  ZuTestCall(testUpgradeLeavesInput);
  ZuTestCall(testEarlyDataSafeRequestPolicy);
  ZiLog::stop();
  return 0;
}
