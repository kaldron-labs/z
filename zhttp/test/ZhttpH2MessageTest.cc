//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/2 normalized message parser/builder test

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

namespace ZhttpH2MessageTest_ {

using TestHeaders = ZhttpHeaders("x-test");

template <typename Parser>
bool data(Parser &parser, ZuBSpan payload, bool endStream = false)
{
  enum { Header = 9 };
  unsigned length = Header + payload.length();
  ZmRef<ZiRxQueue::Node> buf = new Zhttp::BodyRx::WireBufAlloc{};
  if (!buf->alloc(length)) return false;
  buf->length = length;
  auto io = static_cast<ZiIOBuf *>(buf.ptr());
  memset(io->data(), 0, Header);
  if (payload) memcpy(&io->data()[Header], payload.data(), payload.length());
  ZiRxStream<ZiRxQueue> wire;
  wire.push(ZuMv(buf));
  bool transferred = false;
  return parser.data(wire, length, Header, 0, endStream, transferred) &&
    transferred && !wire;
}

struct Parsed :
  public Zhttp::H2::Parser<Parsed, true, TestHeaders> {
  using Base = Zhttp::H2::Parser<Parsed, true, TestHeaders>;

  Parsed() : Base{1024} { }

  void operation(
    Zhttp::Method::T method_, const Zhttp::RequestTarget &target) {
    operationOrder = ++callbackOrder;
    method = method_;
    path = target.raw;
  }
  void status(unsigned value) { status_ = value; ++statusCalls; }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyType = type;
    bodyLength = length;
    ++bodyInfoCalls;
    if (type == Zhttp::BodyType::Fixed) contentLength_ = length;
  }
  template <typename Key>
  void header(Zhttp::HdrSection section, ZuBSpan value) {
    headerSection = section;
    if (!headerOrder) headerOrder = ++callbackOrder;
    if constexpr (Key{}() == "x-test") xTest = value;
  }
  void header(
      Zhttp::HdrSection section, ZuBSpan name, ZuBSpan value) {
    headerSection = section;
    if (!headerOrder) headerOrder = ++callbackOrder;
    if (name == "host") host = value;
  }
  template <typename Rx>
  void body(Rx &rx) {
    if (!bodyOrder) bodyOrder = ++callbackOrder;
    while (rx) {
      const uint8_t *offered = nullptr;
      if (rx.consume(
	  [&offered, this](ZuBSpan value) -> int64_t {
	    offered = value.data();
	    return partial ? 1 : value.length();
	  },
	  [this, &offered](ZuBSpan value) {
	    noCopy &= value.data() == offered;
	    body_ << ZuCSpan{value};
	  }) <= 0)
	break;
      if (partial) break;
    }
  }
  void complete(Zhttp::H2::ParserState::T state) {
    completeState = state;
    ++completeCalls;
  }

  Zhttp::Method::T method = -1;
  unsigned status_ = 0;
  unsigned statusCalls = 0;
  uint64_t contentLength_ = 0;
  uint64_t bodyLength = 0;
  Zhttp::BodyType::T bodyType = Zhttp::BodyType::None;
  Zhttp::HdrSection headerSection = Zhttp::HdrSection::Invalid;
  unsigned bodyInfoCalls = 0;
  unsigned callbackOrder = 0;
  unsigned operationOrder = 0;
  unsigned headerOrder = 0;
  unsigned bodyOrder = 0;
  unsigned completeCalls = 0;
  Zhttp::H2::ParserState::T completeState =
    Zhttp::H2::ParserState::Initial;
  ZtString<> path;
  ZtString<> host;
  ZtString<> xTest;
  ZtString<> body_;
  bool partial = false;
  bool noCopy = true;
};

struct Response :
  public Zhttp::H2::Parser<Response, false, TestHeaders> {
  using Base = Zhttp::H2::Parser<Response, false, TestHeaders>;

  Response() : Base{1024} { }

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
  void status(unsigned value) {
    statusOrder = ++callbackOrder;
    headerOrder = bodyOrder = 0;
    status_ = value;
    ++statusCalls;
  }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    bodyType = type;
    bodyLength = length;
    ++bodyInfoCalls;
    if (type == Zhttp::BodyType::Fixed) contentLength_ = length;
  }
  template <typename Key>
  void header(Zhttp::HdrSection section, ZuBSpan value) {
    headerSection = section;
    if (!headerOrder) headerOrder = ++callbackOrder;
    if constexpr (Key{}() == "x-test") xTest = value;
  }
  template <typename Rx>
  void body(Rx &rx) {
    if (!bodyOrder) bodyOrder = ++callbackOrder;
    Zhttp::bodyEach(rx,
      [this](ZuBSpan value) { body_ << ZuCSpan{value}; });
  }
  void complete(Zhttp::H2::ParserState::T state) {
    completeState = state;
    ++completeCalls;
  }

  unsigned status_ = 0;
  unsigned statusCalls = 0;
  uint64_t contentLength_ = 0;
  uint64_t bodyLength = 0;
  Zhttp::BodyType::T bodyType = Zhttp::BodyType::None;
  Zhttp::HdrSection headerSection = Zhttp::HdrSection::Invalid;
  unsigned bodyInfoCalls = 0;
  unsigned callbackOrder = 0;
  unsigned statusOrder = 0;
  unsigned headerOrder = 0;
  unsigned bodyOrder = 0;
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

struct CustomTarget {
  template <typename S>
  void print(S &s) const { s << "/printable?"; }

  friend ZuPrintFn ZuPrintType(CustomTarget *);
};

struct CaptureStream {
  bool extendedConnect() const { return extended; }
  void beginHeaders(bool end = false) {
    ++beginCalls;
    beginEndStream = end;
  }
  template <typename V>
  void field(ZuCSpan name, V &&value) {
    auto field_ = new (fields.push()) CapturedField{
      .name = ZtString<>{name}
    };
    field_->value << ZuFwd<V>(value);
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

struct BuildOps {
  template <typename L>
  void operation(L &&l) {
    if (customTarget)
      l(Zhttp::Method::POST, CustomTarget{});
    else
      l(Zhttp::Method::POST, Zhttp::PathQuery{"/submit", "a=1", true});
  }
  template <typename L> void host(L &&l) { l("example.com"); }
  unsigned status() { return 201; }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length")
      l("3");
    else if constexpr (Key{}() == "x-test")
      l("request");
    else if constexpr (Key{}() == "x-trailer")
      l("done");
  }

  bool customTarget = false;
};

struct RequestBuild :
  public Zhttp::H2::Request<
    RequestBuild, ZhttpHeaders("content-length", "x-test"),
    ZhttpHeaders("x-trailer"), true>,
  public BuildOps {
  using BuildOps::header;
  using BuildOps::host;
  using BuildOps::operation;
};

struct ResponseBuild :
  public Zhttp::H2::Response<
    ResponseBuild, ZhttpHeaders("content-length", "x-test"),
    ZhttpHeaders("x-trailer"), true>,
  public BuildOps {
  using BuildOps::header;
  using BuildOps::status;
};

struct ConnectBuild :
  public Zhttp::H2::Request<ConnectBuild> {
  template <typename L>
  void operation(L &&l) {
    l(Zhttp::Method::CONNECT, Zhttp::PathQuery{"/chat", "v=1", true});
  }
  template <typename L> void host(L &&l) { l("example.com"); }
  template <typename L> void protocol(L &&l) { l("opaque"); }
};

struct StreamResponse;

struct StreamLink {
  struct Tx { void flush() { } };

  bool streamLocalCap() const { return true; }
  bool streamPeerCap() const { return true; }
  template <typename L>
  void streamTx(L &&l) {
    Tx tx;
    ZuFwd<L>(l)(tx);
  }
  void streamTxEnd() { }
  void streamTxReset() { }
};

struct StreamConsumer {
  template <typename Stream, typename Rx>
  int process(Stream stream, Rx &rx);

  StreamResponse	*owner = nullptr;
};

struct StreamResponse :
  public Zhttp::H2::Parser<StreamResponse, false, ZuTypeList<>> {
  using Base = Zhttp::H2::Parser<StreamResponse, false, ZuTypeList<>>;

  StreamResponse() : Base{1}, consumer{this} {
    dispatch.init(link, consumer);
  }
  ~StreamResponse() {
    dispatch.disable_();
    dispatch.final_();
  }

  void operation(Zhttp::Method::T, const Zhttp::RequestTarget &) { }
  void status(unsigned value) { status_ = value; }
  template <typename Key>
  void header(Zhttp::HdrSection, ZuBSpan) { }
  template <typename Rx>
  void body(Rx &rx) { Zhttp::bodyDrain(rx); }
  void bodyInfo(Zhttp::BodyType::T type, uint64_t) {
    if (status_ >= 200 && status_ < 300 &&
	type != Zhttp::BodyType::None) {
      stream();
      ++established;
      ++starts;
    }
  }
  template <typename Rx>
  void streamRx_(Rx &rx) { dispatch.process(rx); }
  void streamPeerEnd_() { ++remoteEnds; dispatch.peerEnd(); }
  void streamError_() { ++resets; dispatch.error(); }
  template <typename Rx>
  void processStream(Rx &rx) {
    while (rx) {
      const uint8_t *offered = nullptr;
      int64_t n = rx.consume(
	  [&offered](ZuBSpan span) -> int64_t {
	    offered = span.data();
	    return 1;
	  },
	  [this, &offered](ZuBSpan span) {
	    noCopy &= span.data() == offered;
	    data_ << ZuCSpan{span};
	  });
      if (n <= 0) break;
      if (partial) break;
    }
  }
  void complete(Zhttp::H2::ParserState::T) { ++completions; }

  ZtString<>	data_;
  Zhttp::StreamDispatch<StreamLink, StreamConsumer> dispatch;
  StreamConsumer consumer;
  unsigned	status_ = 0;
  unsigned	established = 0;
  unsigned	remoteEnds = 0;
  unsigned	resets = 0;
  unsigned	completions = 0;
  unsigned	starts = 0;
  bool		noCopy = true;
  bool		partial = false;
  StreamLink	link;
};

template <typename Stream, typename Rx>
int StreamConsumer::process(Stream, Rx &rx)
{
  owner->processStream(rx);
  return 1;
}

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
      parser.xTest == "request" && parser.contentLength_ == 3 &&
      parser.headerSection == Zhttp::HdrSection::Final &&
      parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && parser.bodyLength == 3 &&
      parser.operationOrder &&
      parser.operationOrder < parser.headerOrder,
    "shared request callbacks");
  ZuCHECK(data(parser, "abc", true) &&
      parser.body_ == "abc" && parser.completeCalls == 1 &&
      parser.completeState == Zhttp::H2::ParserState::Complete &&
      parser.noCopy && parser.headerOrder < parser.bodyOrder,
    "streaming request body completion");

  Parsed invalid;
  invalid.extendedConnect(true);
  ZuCHECK(invalid.beginHeaders() &&
      invalid.field(":method", "CONNECT") &&
      invalid.field(":scheme", "https") &&
      invalid.field(":authority", "example.com") &&
      invalid.field(":path", "/chat") &&
      invalid.field(":protocol", "opaque") &&
      invalid.field("x-test", "request") &&
      invalid.field("content-length", "0") &&
      !invalid.endHeaders(false) &&
      invalid.operationOrder < invalid.headerOrder &&
      invalid.completeCalls == 1 &&
      invalid.completeState == Zhttp::H2::ParserState::Error,
    "post-operation H2 validation failure completes with an error");

  Parsed partial;
  partial.partial = true;
  ZuCHECK(partial.beginHeaders() &&
      partial.field(":method", "POST") &&
      partial.field(":scheme", "https") &&
      partial.field(":authority", "example.com") &&
      partial.field(":path", "/submit") &&
      partial.field("content-length", "3") &&
      partial.endHeaders(false) &&
      data(partial, "abc", true) && partial.body_ == "a" &&
      partial.completeCalls == 1 &&
      partial.completeState == Zhttp::H2::ParserState::Complete,
    "unconsumed H2 message DATA was not discarded at completion");

  Parsed oversized;
  ZuCHECK(oversized.beginHeaders() &&
      oversized.field(":method", "POST") &&
      oversized.field(":scheme", "https") &&
      oversized.field(":authority", "example.com") &&
      oversized.field(":path", "/submit") &&
      oversized.field("content-length", "3") &&
      oversized.endHeaders(false) &&
      !oversized.dataLength(4, false) &&
      oversized.completeCalls == 1 &&
      oversized.completeState == Zhttp::H2::ParserState::Error,
    "prospective H2 DATA length was not rejected before transfer");
}

void testResponses()
{
  ZuTestScope(testResponses);

  Response parser;
  parser.reset();
  ZuCHECK(parser.beginHeaders() && parser.field(":status", "103") &&
      parser.field("x-test", "early") && parser.endHeaders(false) &&
      parser.state() == Zhttp::H2::ParserState::Initial &&
      parser.statusCalls == 1 && !parser.completeCalls &&
      parser.statusOrder < parser.headerOrder &&
      parser.headerSection == Zhttp::HdrSection::Informational &&
      !parser.bodyInfoCalls,
    "informational response does not complete the stream");
  ZuCHECK(parser.beginHeaders() && parser.field(":status", "200") &&
      parser.field("content-length", "3") &&
      parser.endHeaders(false) && data(parser, "abc") &&
      parser.beginHeaders(true) && parser.field("x-test", "trailer") &&
      parser.endHeaders(true) && parser.statusCalls == 2 &&
      parser.body_ == "abc" && parser.xTest == "trailer" &&
      parser.headerSection == Zhttp::HdrSection::Trailers &&
      parser.bodyInfoCalls == 1 &&
      parser.bodyType == Zhttp::BodyType::Fixed && parser.bodyLength == 3 &&
      parser.completeCalls == 1 &&
      parser.statusOrder < parser.bodyOrder &&
      parser.statusOrder < parser.headerOrder,
    "final response, body, and trailers");

  Response invalid;
  ZuCHECK(invalid.beginHeaders() &&
      invalid.field(":status", "200") &&
      invalid.field("x-test", "response") &&
      !invalid.field("connection", "close") &&
      invalid.statusOrder < invalid.headerOrder &&
      invalid.completeCalls == 1 &&
      invalid.completeState == Zhttp::H2::ParserState::Error,
    "post-status H2 validation failure completes with an error");

  Response head;
  head.reset();
  head.requestMethod(Zhttp::Method::HEAD);
  ZuCHECK(head.beginHeaders() && head.field(":status", "200") &&
      head.field("content-length", "99") && head.endHeaders(true) &&
      head.completeCalls == 1 && head.bodyInfoCalls == 1 &&
      head.bodyType == Zhttp::BodyType::None && !head.bodyLength &&
      !data(head, "x"),
    "HEAD response completes without DATA");

  Response cancelled;
  cancelled.reset();
  ZuCHECK(!cancelled.cancel() && cancelled.completeCalls == 1 &&
      cancelled.completeState == Zhttp::H2::ParserState::Error &&
      !cancelled.cancel() && cancelled.completeCalls == 1,
    "cancellation completes exactly once");
}

void testRequestErrors()
{
  ZuTestScope(testRequestErrors);

  Parsed oversized;
  ZuCHECK(oversized.beginHeaders() &&
      oversized.field(":method", "POST") &&
      oversized.field(":scheme", "https") &&
      oversized.field(":authority", "example.com") &&
      oversized.field(":path", "/submit") &&
      !oversized.field("content-length", "1025") &&
      oversized.error().code == Zhttp::RequestErrorCode::ContentTooLarge &&
      oversized.error().scope == Zhttp::RequestErrorScope::Request &&
      oversized.error().responsePossible &&
      Zhttp::requestErrorStatus(oversized.error().code) == 413,
    "oversized H2 request classification mismatch");
  ZuCHECK(!oversized.beginHeaders() &&
      oversized.error().code == Zhttp::RequestErrorCode::ContentTooLarge,
    "first H2 request error was overwritten");

  Parsed malformed;
  ZuCHECK(!malformed.endHeaders(false) &&
      malformed.error().code == Zhttp::RequestErrorCode::Malformed &&
      malformed.error().scope == Zhttp::RequestErrorScope::Stream &&
      !malformed.error().responsePossible,
    "malformed H2 request classification mismatch");
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

  RequestBuild builder;
  CaptureStream request;
  builder.begin(request);
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

  RequestBuild customBuilder;
  customBuilder.customTarget = true;
  CaptureStream custom;
  ZuCHECK(customBuilder.begin(custom) &&
      find(custom.fields, ":path", "/printable?"),
    "custom printable target survives H2 Builder dispatch");

  CaptureStream response;
  ResponseBuild responseBuilder;
  responseBuilder.begin(response);
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
  ZuCHECK(builder.begin(supported) &&
      find(supported.fields, ":method", "CONNECT") &&
      find(supported.fields, ":scheme", "https") &&
      find(supported.fields, ":authority", "example.com") &&
      find(supported.fields, ":path", "/chat?v=1") &&
      find(supported.fields, ":protocol", "opaque") &&
      !supported.beginEndStream && !supported.endStream,
    "Extended CONNECT is emitted for an advertised peer");

  CaptureStream unsupported;
  unsupported.extended = false;
  ZuCHECK(!builder.begin(unsupported) && !unsupported.beginCalls &&
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

  StreamResponse response;
  response.reset();
  ZuCHECK(response.beginHeaders() &&
      response.field(":status", "200") &&
      response.endHeaders(false) &&
      response.state() == Zhttp::H2::ParserState::Stream &&
      response.established == 1 && data(response, "a") &&
      data(response, "bc") && response.data_ == "abc" &&
      response.starts == 1 && response.noCopy &&
      data(response, {}, true) &&
      response.state() == Zhttp::H2::ParserState::RemoteClosed &&
      response.remoteEnds == 1 && !response.completions,
    "successful response transitions to an unbounded ordered stream");
  ZuCHECK(!response.cancel() && response.resets == 0 &&
      response.completions == 1 &&
      response.state() == Zhttp::H2::ParserState::Error,
    "post-Final stream cleanup does not duplicate a terminal event");

  StreamResponse partial;
  partial.reset();
  partial.partial = true;
  ZuCHECK(partial.beginHeaders() &&
      partial.field(":status", "200") &&
      partial.endHeaders(false) &&
      data(partial, "abc", true) &&
      partial.data_ == "a" &&
      partial.remoteEnds == 1 && !partial.resets &&
      !partial.completions &&
      partial.state() == Zhttp::H2::ParserState::RemoteClosed &&
      !partial.cancel() && !partial.resets &&
      partial.completions == 1,
    "unconsumed stream input is discarded after peer end");

  StreamResponse rejected;
  rejected.reset();
  ZuCHECK(rejected.beginHeaders() &&
      rejected.field(":status", "403") &&
      rejected.endHeaders(true) &&
      rejected.state() == Zhttp::H2::ParserState::Complete &&
      !rejected.established && rejected.completions == 1,
    "non-2xx response completes without entering stream mode");
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

} // namespace ZhttpH2MessageTest_

int main(int argc, char **argv)
{
  using namespace ZhttpH2MessageTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRequest);
  ZuTestCall(testResponses);
  ZuTestCall(testRequestErrors);
  ZuTestCall(testConnect);
  ZuTestCall(testBuilder);
  ZuTestCall(testExtendedConnect);
  ZuTestCall(testMessageTrait);
}
