//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 normalized message parser/builder test

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpMessage.hh>

using namespace ZuTestUtil;

namespace {

using TestHeaders = ZhttpHeaders("x-test");

struct Parsed :
  public Zhttp::H2::Parser<Parsed, true, TestHeaders, 1024> {
  using Base = Zhttp::H2::Parser<Parsed, true, TestHeaders, 1024>;

  void operation(Zhttp::Method::T method_, ZuBSpan path_) {
    method = method_;
    path = path_;
  }
  void status(unsigned value) { status_ = value; ++statusCalls; }
  void contentLength(uint64_t value) { contentLength_ = value; }
  template <typename Key> void header(ZuBSpan value) {
    if constexpr (Key{}() == "x-test") xTest = value;
  }
  void header(ZuBSpan name, ZuBSpan value) {
    if (name == "host") host = value;
  }
  void body(ZuBSpan value) {
    body_ << ZuCSpan{value};
  }
  void complete(Zhttp::H2::ParserState::T state) {
    completeState = state;
    ++completeCalls;
  }

  Zhttp::Method::T method = -1;
  unsigned status_ = 0;
  unsigned statusCalls = 0;
  uint64_t contentLength_ = 0;
  unsigned completeCalls = 0;
  Zhttp::H2::ParserState::T completeState =
    Zhttp::H2::ParserState::Initial;
  ZtString<> path;
  ZtString<> host;
  ZtString<> xTest;
  ZtString<> body_;
};

struct Response :
  public Zhttp::H2::Parser<Response, false, TestHeaders, 1024> {
  using Base = Zhttp::H2::Parser<Response, false, TestHeaders, 1024>;

  void operation(Zhttp::Method::T, ZuBSpan) { }
  void status(unsigned value) { status_ = value; ++statusCalls; }
  void contentLength(uint64_t value) { contentLength_ = value; }
  template <typename Key> void header(ZuBSpan value) {
    if constexpr (Key{}() == "x-test") xTest = value;
  }
  void body(ZuBSpan value) { body_ << ZuCSpan{value}; }
  void complete(Zhttp::H2::ParserState::T state) {
    completeState = state;
    ++completeCalls;
  }

  unsigned status_ = 0;
  unsigned statusCalls = 0;
  uint64_t contentLength_ = 0;
  unsigned completeCalls = 0;
  Zhttp::H2::ParserState::T completeState =
    Zhttp::H2::ParserState::Initial;
  ZtString<> xTest;
  ZtString<> body_;
};

struct CapturedField {
  ZtString<> name;
  ZtString<> value;
};
using CapturedFields =
  ZtArray<CapturedField,
    ZtArrayHeapID<"Zhttp.H2MessageTest.Fields">>;

struct CaptureStream {
  bool extendedConnect() const { return extended; }
  void beginHeaders(bool end = false) {
    ++beginCalls;
    beginEndStream = end;
  }
  void field(ZuCSpan name, ZuCSpan value) {
    new (fields.push()) CapturedField{
      .name = ZtString<>{name},
      .value = ZtString<>{value}
    };
  }
  void field(
    ZuCSpan name, ZuCSpan value1, char separator, ZuCSpan value2) {
    auto field_ = new (fields.push()) CapturedField{
      .name = ZtString<>{name}
    };
    field_->value << value1 << separator << value2;
  }
  void endHeaders(bool end) {
    ++endHeadersCalls;
    endStream = end;
  }
  CaptureStream &body() { return *this; }
  void end() { endStream = true; ++endCalls; }
  void flush() { ++flushCalls; }

  CapturedFields fields;
  unsigned beginCalls = 0;
  unsigned endHeadersCalls = 0;
  unsigned endCalls = 0;
  unsigned flushCalls = 0;
  bool endStream = false;
  bool beginEndStream = false;
  bool extended = true;
};

struct Build :
  public Zhttp::H2::Builder<
    Build, ZhttpHeaders("x-test"), ZhttpHeaders("x-trailer"), true> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::POST, "/submit", "a=1"); }
  template <typename L> void host(L &&l) { l("example.com"); }
  unsigned status() { return 201; }
  uint64_t contentLength() { return 3; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "x-test")
      l("request");
    else if constexpr (Key{}() == "x-trailer")
      l("done");
  }
};

struct ConnectBuild :
  public Zhttp::H2::Builder<ConnectBuild> {
  template <typename L>
  void operation(L &&l) { l(Zhttp::Method::CONNECT, "/chat", "v=1"); }
  template <typename L> void host(L &&l) { l("example.com"); }
  template <typename L> void protocol(L &&l) { l("opaque"); }
};

struct TunnelResponse :
  public Zhttp::H2::Parser<TunnelResponse, false, ZuTypeList<>, 1> {
  void operation(Zhttp::Method::T, ZuBSpan) { }
  void status(unsigned value) { status_ = value; }
  void contentLength(uint64_t) { }
  template <typename Key> void header(ZuBSpan) { }
  void body(ZuBSpan) { }
  void headers(Zhttp::Fields::Section section, bool endStream) {
    if (section == Zhttp::Fields::Final &&
	status_ >= 200 && status_ < 300 && !endStream) {
      tunnel();
      ++established;
    }
  }
  void tunnelData(ZuBSpan value) { data_ << ZuCSpan{value}; }
  void tunnelEnd() { ++remoteEnds; }
  void tunnelReset() { ++resets; }
  void complete(Zhttp::H2::ParserState::T) { ++completions; }

  ZtString<>	data_;
  unsigned	status_ = 0;
  unsigned	established = 0;
  unsigned	remoteEnds = 0;
  unsigned	resets = 0;
  unsigned	completions = 0;
};

bool find(
  const CapturedFields &fields, ZuCSpan name, ZuCSpan value)
{
  for (unsigned i = 0; i < fields.length(); ++i)
    if (fields[i].name == name && fields[i].value == value) return true;
  return false;
}

void testRequest()
{
  ZuTestScope(testRequest);

  Parsed parser;
  parser.reset();
  ZuCHECK(parser.beginHeaders() &&
      parser.field(":method", "POST") &&
      parser.field(":scheme", "https") &&
      parser.field(":authority", "example.com") &&
      parser.field(":path", "/submit") &&
      parser.field("content-length", "3") &&
      parser.field("x-test", "request") &&
      parser.endHeaders(false),
    "request headers");
  ZuCHECK(parser.method == Zhttp::Method::POST &&
      parser.path == "/submit" && parser.host == "example.com" &&
      parser.xTest == "request" && parser.contentLength_ == 3,
    "shared request callbacks");
  ZuCHECK(parser.data("abc", true) &&
      parser.body_ == "abc" && parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H2::ParserState::Complete,
    "streaming request body completion");
}

void testResponses()
{
  ZuTestScope(testResponses);

  Response parser;
  parser.reset();
  ZuCHECK(parser.beginHeaders() && parser.field(":status", "103") &&
      parser.field("x-test", "early") && parser.endHeaders(false) &&
      parser.state() == Zhttp::H2::ParserState::Initial &&
      parser.statusCalls == 1 && !parser.completeCalls,
    "informational response does not complete the stream");
  ZuCHECK(parser.beginHeaders() && parser.field(":status", "200") &&
      parser.field("content-length", "3") &&
      parser.endHeaders(false) && parser.data("abc") &&
      parser.beginHeaders(true) && parser.field("x-test", "trailer") &&
      parser.endHeaders(true) && parser.statusCalls == 2 &&
      parser.body_ == "abc" && parser.xTest == "trailer" &&
      parser.completeCalls == 1,
    "final response, body, and trailers");

  Response head;
  head.reset();
  head.requestMethod(Zhttp::Method::HEAD);
  ZuCHECK(head.beginHeaders() && head.field(":status", "200") &&
      head.field("content-length", "99") && head.endHeaders(true) &&
      head.completeCalls == 1 && !head.data("x"),
    "HEAD response completes without DATA");

  Response cancelled;
  cancelled.reset();
  ZuCHECK(!cancelled.cancel() && cancelled.completeCalls == 1 &&
      cancelled.completeState == Zhttp::H2::ParserState::Error &&
      !cancelled.cancel() && cancelled.completeCalls == 1,
    "cancellation completes exactly once");
}

void testConnect()
{
  ZuTestScope(testConnect);

  Parsed parser;
  parser.reset();
  ZuCHECK(parser.beginHeaders() &&
      parser.field(":method", "CONNECT") &&
      parser.field(":authority", "example.com:443") &&
      parser.endHeaders(true) &&
      parser.method == Zhttp::Method::CONNECT &&
      parser.completeCalls == 1,
    "ordinary CONNECT syntax");
}

void testBuilder()
{
  ZuTestScope(testBuilder);

  Build builder;
  CaptureStream request;
  builder.request(request);
  builder.finish(request);
  ZuCHECK(request.beginCalls == 2 && request.endHeadersCalls == 2 &&
      request.endStream && request.flushCalls == 1 &&
      find(request.fields, ":method", "POST") &&
      find(request.fields, ":scheme", "https") &&
      find(request.fields, ":authority", "example.com") &&
      find(request.fields, ":path", "/submit?a=1") &&
      find(request.fields, "content-length", "3") &&
      find(request.fields, "x-test", "request") &&
      find(request.fields, "x-trailer", "done"),
    "request builder emits normalized fields and trailing HEADERS");

  CaptureStream response;
  builder.response(response);
  ZuCHECK(find(response.fields, ":status", "201"),
    "response builder emits status");
  ZuCHECK(find(response.fields, "content-length", "3"),
    "response builder emits content length");
}

void testExtendedConnect()
{
  ZuTestScope(testExtendedConnect);

  ConnectBuild builder;
  CaptureStream supported;
  ZuCHECK(builder.request(supported) &&
      find(supported.fields, ":method", "CONNECT") &&
      find(supported.fields, ":scheme", "https") &&
      find(supported.fields, ":authority", "example.com") &&
      find(supported.fields, ":path", "/chat?v=1") &&
      find(supported.fields, ":protocol", "opaque") &&
      !supported.beginEndStream && !supported.endStream,
    "Extended CONNECT is emitted for an advertised peer");

  CaptureStream unsupported;
  unsupported.extended = false;
  ZuCHECK(!builder.request(unsupported) && !unsupported.beginCalls &&
      !unsupported.fields,
    "Extended CONNECT is rejected before emission without capability");

  Parsed request;
  request.reset();
  request.extendedConnect(true);
  ZuCHECK(request.beginHeaders() &&
      request.field(":method", "CONNECT") &&
      request.field(":scheme", "https") &&
      request.field(":authority", "example.com") &&
      request.field(":path", "/chat") &&
      request.field(":protocol", "opaque") &&
      request.endHeaders(false),
    "enabled Extended CONNECT request semantics");

  Parsed disabled;
  disabled.reset();
  ZuCHECK(disabled.beginHeaders() &&
      disabled.field(":method", "CONNECT") &&
      disabled.field(":scheme", "https") &&
      disabled.field(":authority", "example.com") &&
      disabled.field(":path", "/chat") &&
      disabled.field(":protocol", "opaque") &&
      !disabled.endHeaders(false),
    "disabled Extended CONNECT request is rejected");

  TunnelResponse response;
  response.reset();
  ZuCHECK(response.beginHeaders() &&
      response.field(":status", "200") &&
      response.endHeaders(false) &&
      response.state() == Zhttp::H2::ParserState::Tunnel &&
      response.established == 1 && response.data("abc") &&
      response.data_ == "abc" && response.data({}, true) &&
      response.state() == Zhttp::H2::ParserState::RemoteClosed &&
      response.remoteEnds == 1 && !response.completions,
    "successful response transitions to an unbounded ordered tunnel");
  ZuCHECK(!response.cancel() && response.resets == 1 &&
      response.completions == 1 &&
      response.state() == Zhttp::H2::ParserState::Error,
    "tunnel reset completes exactly once");

  TunnelResponse rejected;
  rejected.reset();
  ZuCHECK(rejected.beginHeaders() &&
      rejected.field(":status", "403") &&
      rejected.endHeaders(true) &&
      rejected.state() == Zhttp::H2::ParserState::Complete &&
      !rejected.established && rejected.completions == 1,
    "non-2xx response completes without entering tunnel mode");
}

void testMessageTrait()
{
  ZuTestScope(testMessageTrait);
  using Message = Zhttp::MessageTraits<Zhttp::H2TLS>;
  ZuCHECK(Message::ID == Zhttp::Version::H2 &&
      Message::Multiplexed && Message::OneMessagePerLink &&
      !Message::CloseDelimited,
    "H2 profile uses the common message trait contract");
}

} // namespace

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRequest);
  ZuTestCall(testResponses);
  ZuTestCall(testConnect);
  ZuTestCall(testBuilder);
  ZuTestCall(testExtendedConnect);
  ZuTestCall(testMessageTrait);
}
