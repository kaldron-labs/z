//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtcHeap.hh>

#include <zlib/ZmcpClient.hh>
#include <zlib/ZmcpServer.hh>
#include <zlib/Zmcp.hh>

using namespace ZuTestUtil;

static uint64_t zmcpAllocated()
{
  uint64_t allocated = 0;
  Ztc::HeapMgr::all(Ztc::HeapMgr::AllFn{
    [&allocated](Ztc::Heap *heap) {
      Ztc::HeapTelemetry data;
      heap->telemetry(data);
      ZuCSpan id{data.id};
      if (id.length() >= 5 && !memcmp(id.data(), "Zmcp.", 5))
	allocated += data.allocated();
    }});
  return allocated;
}

struct EchoReq { int value = 0; };
struct EchoResult { int value = 0; };
ZfStruct(, (EchoReq, JSON),
  (value, (Ctor<0>, Required, MCP::Header<"Value">),		Int32));
ZfStruct(, (EchoResult, JSON),
  (value, (Ctor<0>, Required),		Int32));
struct EchoOK : public Zmcp::Response {
  using Body = EchoResult;
};
struct Echo : public Zmcp::Request {
  using Object = EchoReq;
  using OperationID = ZuStringT<"echoValue">;
  using ToolID = ZuStringT<"echo">;
  using Responses = ZuTypeList<EchoOK>;
};
using EchoCatalog = ZuTypeList<Echo>;

struct EchoSSE : public Zmcp::Request {
  using Object = EchoReq;
  using OperationID = ZuStringT<"echoStream">;
  using ToolID = ZuStringT<"echo_stream">;
  using Responses = ZuTypeList<EchoOK>;
  enum { ResponseBody = Zjrpc::BodyPolicy::SSE };
};
using EchoSSECatalog = ZuTypeList<EchoSSE>;

struct HeaderNested { bool enabled = false; };
ZfStruct(, (HeaderNested, JSON),
  (enabled, (Ctor<0>, MCP::Header<"Enabled">),		Bool));
struct HeaderReq {
  ZtString<> text;
  HeaderNested nested;
};
ZfStruct(, (HeaderReq, JSON),
  (text, (Ctor<0>, MCP::Header<"Text">),	String),
  (nested, (Ctor<1>),				UDT));

template <typename Heap = ZuVoid>
struct ThrowContext_ : public Heap, public ZmObject { };
ZuDerive(ThrowContextHeap, (ZmHeap<"ZmcpTest.ThrowContext", ThrowContext_<>>));
ZuDerive(ThrowContext, (ThrowContext_<ThrowContextHeap>));

struct ThrowContextApp {
  void close(Zmcp::TransportTag, ThrowContext *) {
    ++closes;
    throw 1;
  }

  unsigned closes = 0;
};

struct SSEPostHarness {
  Zmcp::HTTPSSEBuilder<EchoCatalog> *builder = nullptr;
  unsigned posts = 0;

  bool postSSE_() { ++posts; return true; }
  bool discardSSE_(Zjrpc::SSEQueue queue) {
    while (queue.shift()) { }
    return true;
  }
  void run() { builder->resume_(); }
};

template <unsigned N>
static ZuSpan<uint8_t> mutableBytes(char (&s)[N]) {
  return ZuSpan<char>{s, N - 1};
}

struct HTTPRx {
  ZuSpan<uint8_t> data;

  template <typename Available, typename Consume>
  void consume(Available &&available, Consume &&consume) {
    int64_t n = available(data);
    if (n > 0) {
      auto span = data;
      span.trunc(n);
      consume(span);
    }
  }
};

struct HTTPBodyOut {
  template <typename V>
  HTTPBodyOut &operator <<(V &&v) {
    data << ZuFwd<V>(v);
    return *this;
  }

  bool failed() const { return failure; }
  bool flush() { if (!flushOK) failure = true; return !failure; }
  uint64_t produced() const { return data.length(); }

  ZtString<> data;
  bool failure = false;
  bool flushOK = true;
};

struct HTTPHeaderPatch {
  HTTPHeaderPatch() { value << Zhttp::contentLengthPad(); }

  template <typename Key, typename Patch>
  void operator ()(Patch &&patch) {
    patch(ZuSpan<uint8_t>{value.span()});
  }

  ZtString<> value;
};

struct HTTPHarness : public Zmcp::HTTPParser<HTTPHarness> {
  ZuCSpan endpoint() const { return "/mcp"; }
  const Zjrpc::Limits &limits() const { return limits_; }
  bool origin(ZuCSpan origin_) const {
    return origin_ == "https://client.example";
  }

  template <typename Link>
  void corruptHTTP(Link *) { ++corrupt; }

  template <typename Link>
  void originHTTP(Link *) { ++originRejected; }

  template <typename Link>
  void deleteHTTP(Link *, const Zmcp::HTTPMeta &meta) {
    ++deletes;
    sessionID = meta.sessionID;
  }

  template <typename Link>
  void receiveHTTP(
      Link *, ZmRef<ZiIOBuf> body, const Zmcp::HTTPMeta &meta) {
    ++received;
    receivedBody = body ? ZuCSpan{*body} : ZuCSpan{};
    sessionID = meta.sessionID;
    method = meta.method;
    name = meta.name;
  }

  Zjrpc::Limits limits_;
  ZtString<> receivedBody;
  ZtString<> sessionID;
  ZtString<> method;
  ZtString<> name;
  unsigned received = 0;
  unsigned deletes = 0;
  unsigned corrupt = 0;
  unsigned originRejected = 0;
};

struct HTTPResponseHarness :
    public Zmcp::HTTPResponseParser<HTTPResponseHarness> {
  const Zjrpc::Limits &limits() const { return limits_; }

  template <typename Link>
  void corruptHTTPResponse(Link *) { ++corrupt; }

  template <typename Link>
  void failedHTTPResponse(Link *) { ++failed; }

  template <typename Link>
  void acceptedHTTPResponse(
      Link *, unsigned status_, const Zmcp::HTTPResponseMeta &) {
    ++accepted;
    responseStatus = status_;
  }

  template <typename Link>
  void receiveHTTPResponse(Link *, unsigned status_, ZmRef<ZiIOBuf> body,
      const Zmcp::HTTPResponseMeta &meta) {
    ++received;
    responseStatus = status_;
    receivedBody = body ? ZuCSpan{*body} : ZuCSpan{};
    sessionID = meta.sessionID;
  }

  bool receiveSSE(
      const Zjrpc::SSEEvent &event, const Zmcp::HTTPResponseMeta &meta) {
    ++events;
    receivedBody.length_(0);
    receivedBody << event.data();
    eventID.length_(0);
    eventID << event.id;
    sessionID = meta.sessionID;
    return acceptSSE;
  }

  template <typename Link>
  void completeHTTPResponse(
      Link *, unsigned status_, const Zmcp::HTTPResponseMeta &) {
    ++completed;
    responseStatus = status_;
  }

  Zjrpc::Limits limits_;
  ZtString<> receivedBody;
  ZtString<> sessionID;
  ZtString<> eventID;
  unsigned responseStatus = 0;
  unsigned received = 0;
  unsigned events = 0;
  unsigned completed = 0;
  unsigned accepted = 0;
  unsigned corrupt = 0;
  unsigned failed = 0;
  bool acceptSSE = true;
};

template <typename Heap = ZuVoid>
struct PendingCall_ : public Heap, public ZmObject {
  void process(const Zjrpc::Reply<EchoOK> &reply) {
    value = reply.body.value;
    ++processed;
  }
  void failed(const Zjrpc::Error &error) {
    errorCode = error.code;
    ++errors;
  }
  void failed() { ++failures; }

  int value = 0;
  int errorCode = 0;
  unsigned processed = 0;
  unsigned errors = 0;
  unsigned failures = 0;
};
ZuDerive(PendingCallHeap, (ZmHeap<"ZmcpTest.PendingCall", PendingCall_<>>));
ZuDerive(PendingCall, (PendingCall_<PendingCallHeap>));

struct ClientNotifyHarness {
  void progress(
      const Zjrpc::ID &token_, double value_, double total_,
      ZuCSpan message_) {
    token = token_;
    value = value_;
    total = total_;
    message = message_;
    ++progresses;
  }

  void logging(
      ZuCSpan level_, const ZfJSON::AnyNode *data_, ZuCSpan logger_) {
    level = level_;
    logger = logger_;
    if (data_ && data_->template has<ZfJSON::AnyNode::String>())
      data = data_->template data<ZfJSON::AnyNode::String>();
    ++logs;
  }

  Zjrpc::ID token;
  ZtString<> message;
  ZtString<> level;
  ZtString<> logger;
  ZtString<> data;
  double value = 0;
  double total = 0;
  unsigned progresses = 0;
  unsigned logs = 0;
};

struct CompletionHarness : public Zmcp::CompletionSet<CompletionHarness> {
  using Base = Zmcp::CompletionSet<CompletionHarness>;
  using Base::Base;

  template <typename Req, typename Token>
  void made(Req *, Token *) { }

  template <typename Token, typename Res>
  bool completion(Token *token, Zjrpc::Reply<Res> reply) {
    return this->complete_(token, ZuMv(reply));
  }

  template <typename Token>
  bool progression(
      Token *token, double value, double total, ZuCSpan message) {
    return this->progress_(token, value, total, message);
  }

  template <typename Token>
  bool logging(
      Token *token, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    return this->log_(token, level, data, logger);
  }

  template <typename Res>
  void complete(const Zjrpc::ID &id, Zjrpc::Reply<Res> reply) {
    Zmcp::ToolReplyMessage<Res>{id, ZuMv(reply), era}.write(out);
  }

  template <typename Req, typename Token>
  void cancelled(Req *, Token *token, ZuCSpan reason_) {
    if (token->cancelled()) ++cancellations;
    reason = reason_;
  }

  template <typename Req, typename Token>
  bool progress(
      Req *, Token *token, double value, double total, ZuCSpan message) {
    Zmcp::ProgressMessage{
      token->progressToken(), message, value, total}.write(out);
    return true;
  }

  template <typename Req, typename Token>
  bool log(
      Req *, Token *, ZuCSpan level, ZuCSpan data, ZuCSpan logger) {
    Zmcp::LogMessage{level, data, logger}.write(out);
    return true;
  }

  ZtString<> out;
  ZtString<> reason;
  int era = Zmcp::Era::Modern;
  unsigned cancellations = 0;
};

template <typename Catalog>
struct HTTPResponderHarness : public Zjrpc::Dispatcher<HTTPResponderHarness<Catalog>,
    HTTPResponderHarness<Catalog>, Catalog,
    Zmcp::Server_::HTTPDispatchContext<Zjrpc::HTTPHeaders<ZuTypeList<>>, Catalog>,
    Zmcp::Server_::HTTPDispatchPolicy<Catalog>> {
  using Headers = Zjrpc::HTTPHeaders<ZuTypeList<>>;
  using Context = Zmcp::Server_::HTTPDispatchContext<Headers, Catalog>;
  using Dispatch = Zjrpc::Dispatcher<HTTPResponderHarness, HTTPResponderHarness,
    Catalog, Context, Zmcp::Server_::HTTPDispatchPolicy<Catalog>>;
  using Responder = Zmcp::HTTPResponder<Catalog, HTTPResponderHarness>;
  using Builder = typename Responder::Builder;

  Zjrpc::Limits limitsValue;
  Headers headers;
  Zmcp::Peer<Catalog> rpcPeer;
  Zjrpc::InboundCalls inbound;
  Context context;
  Responder responder{this, {}};
  ZmRef<Builder> builder;
  ZmList<ZmFn<void()>, ZmListHeapID<"ZmcpTest.HTTP.Turns">> turns;
  unsigned sends = 0;
  unsigned failures = 0;
  bool attached = true;
  bool defer = false;
  ZmFn<bool()> deferred;

  HTTPResponderHarness() : context{{}, &headers, &rpcPeer, this,
      [](void *route, int policy) {
	static_cast<HTTPResponderHarness *>(route)->responder.prepare(policy);
      }, [](void *route) { return static_cast<HTTPResponderHarness *>(route)->attached; }} { }
  ~HTTPResponderHarness() {
    while (!Dispatch::close()) turn();
    while (turn()) { }
    responder.close();
    if (builder) builder->data().close();
  }
  HTTPResponderHarness *impl() { return this; }
  bool up() const { return true; }
  Zjrpc::CompletionRoute completionRoute() const { return {}; }
  bool invoked() const { return true; }
  const Zjrpc::Limits &limits() const { return limitsValue; }
  bool response(const Zjrpc::Envelope &) { return true; }
  template <typename L> bool ownerRun(L &&l) { l(); return true; }
  template <typename L> void continue_(L &&l) { turns.push(ZuFwd<L>(l)); }
  bool turn() {
    auto fn = turns.shift();
    if (!fn) return false;
    fn->data()();
    return true;
  }
  bool receive(ZuCSpan input) {
    ZmRef<ZiIOBuf> body = new Zjrpc::BatchBuf{};
    *body << input;
    return Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == Zjrpc::RoutePolicy::Abort) { failed(); return false; }
      if (!attached) return true;
      responder.emit(ZuMv(message));
      return true;
    }, &inbound, &context);
  }
  void send(ZmRef<Builder> value) { builder = ZuMv(value); ++sends; }
  void responseClosed_() { }
  void fail_() { failed(); }
  void failed() { ++failures; }
  template <typename Req, typename Token>
  void tool(Req *, const EchoReq &value, const Headers &, const Zmcp::Context &, Token token) {
    if (defer) {
      deferred = [value, token = ZuMv(token)]() mutable {
	return token->complete(Zjrpc::Reply<EchoOK>{EchoResult{value.value}});
      };
      return;
    }
    (void)token->progress(.25, 1, "quarter");
    token->complete(Zjrpc::Reply<EchoOK>{EchoResult{value.value}});
  }
  void cancelled(auto *, auto *, ZuCSpan) { }
};

static void httpDetachedTest()
{
  ZuTestScope(httpDetached);
  HTTPResponderHarness<EchoCatalog> harness;
  harness.defer = true;
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":39,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":39}}}"));
  ZuCheck(harness.inbound.count() == 1);
  harness.attached = false;
  ZuCheck(!harness.deferred());
  ZuCheck(!harness.inbound.count());
  ZuCheck(!harness.sends && !harness.failures);
}

static void batchDeclarationTest()
{
  ZuTestScope(batchDeclaration);
  Zmcp::Batch<EchoCatalog> batch;
  ZuCheck(batch.request<Echo>(int64_t{51}, EchoReq{51}));
  batch.notify<Echo>(EchoReq{52});
  ZuCheck(batch.count() == 2 && batch.calls() == 1);
  auto frame = batch.frame(Zjrpc::Default::MaxJSONBytes);
  ZuCheck(frame, (return));
  auto parsed = Zjrpc::parse(ZuSpan<char>{frame->span()}, frame->length);
  ZuCheck(parsed.batch(), (return));
  const auto &members = parsed.value()->data<ZfJSON::AnyNode::Array>();
  ZuCheck(members.length() == 2, (return));
  auto request = Zjrpc::decode(members[0].ptr());
  auto notification = Zjrpc::decode(members[1].ptr());
  ZuCheck(request.kind == Zjrpc::MessageKind::Request &&
    request.id() == Zjrpc::ID{int64_t{51}});
  ZuCheck(request.method() == "tools/call");
  ZuCheck(notification.kind == Zjrpc::MessageKind::Notification);
  auto params = Zjrpc::loadObject<Zmcp::ToolsCallParams<EchoCatalog>>(
    Zjrpc::raw(request.params()));
  ZuCheck(params.narrow() == 0, (return));
  ZuCheck(params.arguments().p<Zmcp::ToolArg<Echo>>().object().value == 51);
}

template <bool Streaming>
static void httpBatchTest_()
{
  ZuTestScopeRT(httpBatchBinding);
  using Catalog = ZuIf<Streaming, EchoSSECatalog, EchoCatalog>;
  HTTPResponderHarness<Catalog> harness;
  const char *request;
  if constexpr (Streaming) request =
    "[{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo_stream\",\"arguments\":{\"value\":40},"
    "\"_meta\":{\"progressToken\":\"batch-40\"}}},"
    "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"ping\"}]";
  else request =
    "[{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":40}}},"
    "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"ping\"}]";
  ZuCheckRT(harness.receive(request));
  ZuCheckRT(harness.sends == 1 && harness.responder.terminal());
  ZuCheckRT(harness.responder.streaming() == Streaming);
  HTTPBodyOut output;
  if constexpr (Streaming) harness.builder->data().body([&harness, &output](auto write) {
    auto result = write(output);
    if (result == Zhttp::WriteOutcome::End) harness.builder->data().close();
    return true;
  });
  else harness.builder->data().body([&output](auto write) { (void)write(output); });
  unsigned replies = 0, notifications = 0;
  auto process = [&replies, &notifications](ZuSpan<char> json) {
    auto parsed = Zjrpc::parse(json, json.length());
    if (parsed.batch()) {
      ++replies;
      const auto &members = parsed.value()->template data<ZfJSON::AnyNode::Array>();
      ZuCheckRT(members.length() == 2);
      auto first = Zjrpc::decode(members[0].ptr());
      auto second = Zjrpc::decode(members[1].ptr());
      ZuCheckRT(first.kind == Zjrpc::MessageKind::Result && first.id() == Zjrpc::ID{int64_t{40}});
      ZuCheckRT(second.kind == Zjrpc::MessageKind::Result &&
    second.id() == Zjrpc::ID{int64_t{41}});
    } else {
      ++notifications;
      ZuCheckRT(parsed.envelope.kind == Zjrpc::MessageKind::Notification);
      ZuCheckRT(parsed.envelope.method() == "notifications/progress");
    }
  };
  if constexpr (Streaming) {
    Zjrpc::SSEDecoder decoder{harness.limits().maxSSELineBytes,
      harness.limits().maxSSEEventBytes};
    ZuCheckRT(decoder.feed(output.data, [&process](const auto &event) {
      process(event.body->span());
    }));
  } else process(output.data.span());
  ZuCheckRT(replies == 1 && notifications == unsigned(Streaming));
  ZuCheckRT(!harness.failures);
}

static void httpBatchTest()
{
  ZuTestScope(httpBatch);
  ZuTestCall(httpBatchTest_<false>);
  ZuTestCall(httpBatchTest_<true>);
}

struct HTTPServerImpl {
  bool origin(ZuCSpan) const { return true; }
  void listening(int, unsigned) { }
  void listenFailed(int, bool) { }
  void connected(int) { }
  void disconnected(int) { }

  template <typename Req, typename Completion>
  void tool(
      Req *, const EchoReq &request, const auto &,
      const Zmcp::Context &, Completion completion) {
    completion->complete(
      Zjrpc::Reply<EchoOK>{EchoResult{request.value}});
  }

  template <typename Req, typename Completion>
  void cancelled(Req *, Completion *, ZuCSpan) { }
};
using HTTPServerContract = Zmcp::HTTPServer<HTTPServerImpl, EchoCatalog>;
ZuAssert(sizeof(HTTPServerContract) > 0);

struct HTTPServerLink : public ZmObject {
  Zhttp::Session session() const { return {1, Zhttp::Transport::TCP}; }
  template <typename Builder>
  void send(ZmRef<Builder>) { }
  void responseCancel(Zhttp::StreamCancelFn) { }
  void disconnect() { }
};

static bool compileHTTPServer(
    HTTPServerContract &server, ZiMultiplex &mx, HTTPServerImpl &impl,
    HTTPServerLink *link, ZmRef<ZiIOBuf> body, const Zmcp::HTTPMeta &meta) {
  Zmcp::ServerConfig config;
  config.port(1).tcp();
  bool ok = server.init(Zhttp::HubConfig{&mx, "1", "2"},
    ZuMv(config), &impl);
  server.receiveHTTP(link, ZuMv(body), meta);
  return ok;
}

class ChannelHarness : public Zjrpc::Dispatcher<ChannelHarness, ChannelHarness,
    EchoCatalog, Zmcp::Context, Zmcp::Server_::DispatchPolicy<EchoCatalog>> {
  using Dispatch = Zjrpc::Dispatcher<ChannelHarness, ChannelHarness,
    EchoCatalog, Zmcp::Context, Zmcp::Server_::DispatchPolicy<EchoCatalog>>;
public:
  using Headers = ZuTypeList<>;
  ChannelHarness() { m_limits.workBatch = 2; }
  ~ChannelHarness() { close(); }
  ChannelHarness *impl() { return this; }
  bool up() const { return m_up; }
  Zjrpc::CompletionRoute completionRoute() const { return {}; }
  bool invoked() const { return true; }
  const Zjrpc::Limits &limits() const { return m_limits; }
  Zmcp::Peer<EchoCatalog> &peer() { return m_peer; }
  bool response(const Zjrpc::Envelope &) { return true; }
  void fail_() { m_failed = true; }
  bool failed() const { return m_failed; }
  template <typename L> bool ownerRun(L &&l) { l(); return true; }
  template <typename L> void continue_(L &&l) { m_turns.push(ZuFwd<L>(l)); }

  bool receive(ZuCSpan input) {
    ZmRef<ZiIOBuf> body = new Zjrpc::BatchBuf{};
    *body << input;
    return Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == Zjrpc::RoutePolicy::Abort) { fail_(); return false; }
      if (message.empty()) return true;
      ZtString<> text;
      message.write(text);
      m_output.push(ZuMv(text));
      return true;
    }, &m_pending, &m_context);
  }
  void turns() { while (auto turn = m_turns.shift()) turn(); }
  void complete() { auto fn = ZuMv(m_complete); fn(); }
  const auto &output() const { return m_output; }
  unsigned cancellations() const { return m_cancellations; }
  unsigned calls() const { return m_calls; }

  template <typename Token>
  void tool(Echo *, const EchoReq &value, const auto &, const Zmcp::Context &, Token token) {
    ++m_calls;
    m_complete = [token = ZuMv(token), value]() mutable {
	token->complete(Zjrpc::Reply<EchoOK>{EchoResult{value.value}});
    };
  }
  void cancelled(auto *, auto *, ZuCSpan) { ++m_cancellations; }
  void close() {
    m_up = false;
    while (!Dispatch::close()) { }
    turns();
    (void)m_pending.close(0);
    m_complete = {};
  }
private:
  Zjrpc::Limits m_limits;
  Zjrpc::InboundCalls m_pending;
  Zmcp::Peer<EchoCatalog> m_peer;
  Zmcp::Context m_context;
  ZmFn<void()> m_complete;
  ZtArray<ZmFn<void()>, ZtArrayHeapID<"ZmcpTest.Turns">> m_turns;
  ZtArray<ZtString<>, ZtArrayHeapID<"ZmcpTest.Output">> m_output;
  unsigned m_calls = 0;
  unsigned m_cancellations = 0;
  bool m_up = true;
  bool m_failed = false;
};

static void channelBatchTest()
{
  ZuTestScope(channelBatch);
  ChannelHarness harness;
  ZuCheck(harness.receive(
    "[{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
      "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":42}}},"
    "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"ping\"},"
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\","
      "\"params\":{\"requestId\":31,\"reason\":\"stop\"}}]"));
  ZuCheck(harness.calls() == 1 && !harness.output().length());
  harness.turns();
  ZuCheck(harness.cancellations() == 1 && !harness.output().length());
  harness.complete();
  ZuCheck(harness.output().length() == 1 && !harness.failed());
  if (harness.output().length() == 1) {
    ZtString<> wire = harness.output()[0];
    auto parsed = Zjrpc::parse(wire.span(), Zjrpc::Default::MaxJSONBytes);
    ZuCheck(parsed.batch());
    if (parsed.batch()) {
      const auto &nodes = parsed.value()->template data<ZfJSON::AnyNode::Array>();
      ZuCheck(nodes.length() == 2);
      if (nodes.length() == 2) {
	ZuCheck(Zjrpc::decode(nodes[0].ptr()).id() == Zjrpc::ID{int64_t{32}});
	ZuCheck(Zjrpc::decode(nodes[1].ptr()).id() == Zjrpc::ID{int64_t{31}});
      }
    }
  }
  // History rejection uses the ordinary encoder without re-executing tools.
  ZuCheck(harness.receive(
    "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":9}}}"));
  ZuCheck(harness.calls() == 1 && harness.output().length() == 2);
  harness.close();
}

static void defaultsTest()
{
  ZuTestScope(defaults);
  Zjrpc::Limits limits;
  ZuCheck(Zmcp::ModernVersion{}() == "2026-07-28");
  ZuCheck(Zmcp::LegacyVersion{}() == "2025-11-25");
  ZuCheck(limits.maxJSONBytes == Zjrpc::Default::MaxJSONBytes);
  ZuCheck(limits.maxLineBytes == limits.maxJSONBytes);
  ZuCheck(limits.workBatch > 0);

  ThrowContextApp app;
  Zmcp::Server_::ContextRef context{
    &app, Zmcp::TransportTag{}, ZuRef<ThrowContext>{new ThrowContext{}}};
  context.close();
  ZuCheck(app.closes == 1);
}

static void peerTest()
{
  ZuTestScope(peer);
  Zmcp::Peer<EchoCatalog> peer;
  ZtString<> out;
  auto tool = [](auto *, const auto &request, auto complete) {
    complete(Zjrpc::Reply<EchoOK>{EchoResult{request.value}});
  };

  char discover[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\","
    "\"unrecognized\":true}";
  ZuCheck(peer.receive(discover, out, tool));
  ZuCheck(peer.era() == Zmcp::Era::Modern);
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
    "\"supportedVersions\":[\"2026-07-28\",\"2025-11-25\"],"
    "\"capabilities\":{\"tools\":{}},"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\","
    "\"cacheScope\":\"public\",\"ttlMs\":86400000}}");

  out.length_(0);
  char call[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":7}}}";
  ZuCheck(peer.receive(call, out, tool));
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":7}},"
    "\"isError\":false}}");

  out.length_(0);
  char unknownTool[] =
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"missing\",\"arguments\":{}}}";
  ZuCheck(peer.receive(unknownTool, out, tool));
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":404},\"isError\":true}}");

  out.length_(0);
  char unknownMethod[] =
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"other/method\"}";
  ZuCheck(peer.receive(unknownMethod, out, tool));
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"error\":{\"code\":-32601,"
    "\"message\":\"Method not found\"}}");

  out.length_(0);
  char appError[] =
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":8}}}";
  auto errorTool = [](auto *, const auto &, auto) {
    throw Zjrpc::Error{"rejected", 409};
  };
  ZuCheck(peer.receive(appError, out, errorTool));
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":409},\"isError\":true}}");

  out.length_(0);
  char unknownAppError[] =
    "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":9}}}";
  auto unknownError = [](auto *, const auto &, auto) { throw 1; };
  ZuCheck(peer.receive(unknownAppError, out, unknownError));
  ZuCheck(out.find("\"structuredContent\":{\"code\":-32000}") >= 0);
}

static void emptyTest()
{
  ZuTestScope(empty);
  using EmptyCatalog = ZuTypeList<>;
  Zmcp::Peer<EmptyCatalog> peer;
  ZtString<> out;
  auto tool = [](auto *, const auto &, auto) { };
  char discover[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\"}";
  ZuCheck(peer.receive(discover, out, tool));
  out.length_(0);
  char list[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}";
  ZuCheck(peer.receive(list, out, tool));
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{"
    "\"tools\":[],"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\","
    "\"cacheScope\":\"public\",\"ttlMs\":86400000}}");

  Zmcp::ClientPeer<EmptyCatalog> client;
  auto emit = [&out](const auto &message) {
    out.length_(0);
    message.write(out);
  };
  ZuCheck(client.probe(emit));
  char discovered[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
    "\"supportedVersions\":[\"2026-07-28\"]}}";
  ZuCheck(client.receive(discovered, emit));
  ZuCheck(client.tools(emit).is<int64_t>());
  ZmRef<ZiIOBuf> listed = new Zjrpc::StdioBuf{};
  *listed << "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{"
    "\"tools\":[]}}";
  ZuCheck(client.receive(ZuMv(listed), emit));
  ZuCheck(Zjrpc::member(client.toolCatalog(), "tools"));
}

static void legacyTest()
{
  ZuTestScope(legacy);
  Zmcp::Peer<EchoCatalog> peer;
  ZtString<> out;
  auto tool = [](auto *, const auto &request, auto complete) {
    complete(Zjrpc::Reply<EchoOK>{EchoResult{request.value}});
  };
  char initialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
    "\"params\":{\"protocolVersion\":\"2025-11-25\"}}";
  ZuCheck(peer.receive(initialize, out, tool));
  ZuCheck(peer.era() == Zmcp::Era::Legacy);
  ZuCheck(peer.legacyState() == Zmcp::LegacyState::Initializing);
  out.length_(0);
  char initialized[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
  ZuCheck(peer.receive(initialized, out, tool));
  ZuCheck(peer.legacyState() == Zmcp::LegacyState::Ready);
  ZuCheck(!out);

  Zmcp::Peer<EchoCatalog> stateless;
  stateless.statelessLegacy();
  char call[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":7}}}";
  ZuCheck(stateless.receive(call, out, tool));
  ZuCheck(stateless.era() == Zmcp::Era::Legacy);
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":7}},"
    "\"isError\":false}}");
}

static void clientTest()
{
  ZuTestScope(client);
  Zmcp::ClientPeer<EchoCatalog> client;
  ZtString<> out;
  ZtString<> method;
  ZtString<> name;
  unsigned emits = 0;
  auto emit = [&out, &method, &name, &emits](const auto &message) {
    ++emits;
    out.length_(0);
    method = message.method();
    name = message.name();
    message.write(out);
  };
  ZuCheck(client.probe(emit));
  ZuCheck(client.state() == Zmcp::ClientState::Discovering);
  ZuCheck(method == "server/discover");
  char discover[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
    "\"supportedVersions\":[\"2026-07-28\"],\"extra\":true}}";
  ZuCheck(client.receive(discover, emit));
  ZuCheck(client.ready());
  ZuCheck(client.era() == Zmcp::Era::Modern);
  auto id = client.template callLog<Echo>(
    EchoReq{9}, Zmcp::LogLevel::Notice, emit);
  ZuCheck(id.is<int64_t>() && id.template p<int64_t>() == 2);
  ZuCheck(method == "tools/call");
  ZuCheck(name == "echo");
  ZtString<> expected;
  expected <<
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":9},"
    "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":"
    "\"2026-07-28\","
    "\"io.modelcontextprotocol/clientCapabilities\":{},"
    "\"io.modelcontextprotocol/clientInfo\":{\"name\":\"zmcp\","
    "\"version\":\"" << Z_VMAJOR << '.' << Z_VMINOR << '.' << Z_VPATCH
    << "\"},\"io.modelcontextprotocol/logLevel\":\"notice\"}}}";
  ZuCheck(out == expected);
  auto toolsID = client.tools(emit);
  ZuCheck(toolsID.is<int64_t>() && toolsID.template p<int64_t>() == 3);
  ZmRef<ZiIOBuf> catalog = new Zjrpc::StdioBuf{};
  *catalog << "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{"
    "\"tools\":[{\"name\":\"echo\"}]}}";
  ZuCheck(client.receive(ZuMv(catalog), emit));
  ZuCheck(Zjrpc::member(client.toolCatalog(), "tools"));
  unsigned cachedEmits = emits;
  ZuCheck(client.tools(emit).is<void>());
  ZuCheck(emits == cachedEmits);
  client.discardCatalog();
  ZuCheck(client.tools(emit).is<int64_t>());
  ZuCheck(emits == cachedEmits + 1);

  Zmcp::ClientPeer<EchoCatalog> legacy;
  ZuCheck(legacy.probe(emit));
  char unsupported[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{"
    "\"code\":-32601,\"message\":\"Method not found\"}}";
  ZuCheck(legacy.receive(unsupported, emit));
  ZuCheck(legacy.state() == Zmcp::ClientState::Initializing);
  ZuCheck(method == "initialize");
  char initialized[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}";
  ZuCheck(legacy.receive(initialized, emit));
  ZuCheck(legacy.ready());
  ZuCheck(legacy.era() == Zmcp::Era::Legacy);
  ZuCheck(method == "notifications/initialized");

  Zmcp::ClientPeer<EchoCatalog> transportFallback;
  ZuCheck(transportFallback.probe(emit));
  ZuCheck(transportFallback.fallback(emit));
  ZuCheck(transportFallback.state() == Zmcp::ClientState::Initializing);
  ZuCheck(method == "initialize");
}

static void pendingTest()
{
  ZuTestScope(pending);
  Zjrpc::PendingCalls pending{1};
  ZmRef<PendingCall> call = new PendingCall{};
  Zjrpc::ID id = int64_t{7};
  ZuCheck((pending.template add<Echo, Zmcp::ToolReplyDecode>(id, call)));
  ZuCheck(pending.count() == 1);
  ZuCheck((!pending.template add<Echo, Zmcp::ToolReplyDecode>(id, call)));
  char response[] =
    "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":12}},"
    "\"isError\":false}}";
  auto parsed = Zjrpc::parse(response, sizeof(response));
  ZuCheck(parsed && pending.receive(parsed.envelope));
  ZuCheck(call->processed == 1 && call->value == 12);
  ZuCheck(!pending.count());

  Zjrpc::ID errorID = Zjrpc::IDString{"request-x"};
  ZuCheck((pending.template add<Echo, Zmcp::ToolReplyDecode>(errorID, call)));
  char error[] =
    "{\"jsonrpc\":\"2.0\",\"id\":\"request-x\",\"error\":{"
    "\"code\":-32007,\"message\":\"failed\"}}";
  parsed = Zjrpc::parse(error, sizeof(error));
  ZuCheck(parsed && pending.receive(parsed.envelope));
  ZuCheck(call->errors == 1 && call->errorCode == -32007);

  char unrelated[] =
    "{\"jsonrpc\":\"2.0\",\"id\":999,\"result\":{}}";
  parsed = Zjrpc::parse(unrelated, sizeof(unrelated));
  ZuCheck(parsed && pending.receive(parsed.envelope));
  ZuCheck(!pending.count());

  ClientNotifyHarness notifications;
  char progress[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
    "\"params\":{\"progressToken\":\"progress-x\",\"progress\":0.5,"
    "\"total\":1,\"message\":\"half\"}}";
  parsed = Zjrpc::parse(progress, sizeof(progress));
  ZuCheck(parsed && Zmcp::Client_::receive(
    &notifications, pending, parsed.envelope));
  ZuCheck(notifications.progresses == 1 &&
    notifications.token == Zjrpc::ID{Zjrpc::IDString{"progress-x"}} &&
    notifications.value == .5 && notifications.total == 1 &&
    notifications.message == "half");

  char logging[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"logger\":\"echo\","
    "\"data\":\"working\"}}";
  parsed = Zjrpc::parse(logging, sizeof(logging));
  ZuCheck(parsed && Zmcp::Client_::receive(
    &notifications, pending, parsed.envelope));
  ZuCheck(notifications.logs == 1 && notifications.level == "info" &&
    notifications.logger == "echo" && notifications.data == "working");

  Zjrpc::ID failedID = int64_t{8};
  ZuCheck((pending.template add<Echo, Zmcp::ToolReplyDecode>(failedID, call)));
  ZuCheck(pending.close(4));
  ZuCheck(call->failures == 1);
  ZuCheck(!pending.count());
  ZuCheck(pending.state() == Zjrpc::PendingCalls::Closed);
  ZuCheck((!pending.template add<Echo, Zmcp::ToolReplyDecode>(Zjrpc::ID{int64_t{14}}, call)));
}

static void residueTest()
{
  ZuTestScope(residue);
  uint64_t before = zmcpAllocated();
  {
    Zmcp::Peer<EchoCatalog> peer;
    ZtString<> out;
    auto tool = [](auto *, const auto &request, auto complete) {
      complete(Zjrpc::Reply<EchoOK>{EchoResult{request.value}});
    };
    char discover[] =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\"}";
    ZuCheck(peer.receive(discover, out, tool));
    for (unsigned i = 0; i < 4096; ++i) {
      char request[] =
	"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
	"\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":7}}}";
      out.length_(0);
      if (!peer.receive(request, out, tool)) {
	ZuCheck(false);
	break;
      }
    }
  }
  ZuCheck(zmcpAllocated() == before);
}

static void completionTest()
{
  ZuTestScope(completion);
  using Owner = Zmcp::CompletionSet<CompletionHarness>;
  using Token = Zmcp::Completion<Echo, Owner>;
  Zmcp::Peer<EchoCatalog> peer;
  CompletionHarness completions;
  ZmRef<Token> retained;
  auto emit = [&completions](const auto &message) {
    message.write(completions.out);
  };
  auto tool = [&retained](auto *, const auto &, auto completion) {
    retained = ZuMv(completion);
  };
  char call[] =
    "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":4},"
    "\"_meta\":{\"progressToken\":\"progress-17\","
    "\"io.modelcontextprotocol/logLevel\":\"info\"}}}";
  ZuCheck(peer.dispatchAsync(call, completions, emit, tool));
  ZuCheck(retained && retained->live());
  ZuCheck(completions.count() == 1);
  ZuCheck(!completions.out);
  ZuCheck(retained->progress(.5, 1, "half"));
  ZuCheck(!retained->log("debug", "too detailed", "echo"));
  ZuCheck(retained->log("info", "working", "echo"));
  ZuCheck(completions.out ==
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\","
    "\"params\":{\"progressToken\":\"progress-17\",\"progress\":0.5,"
    "\"total\":1,\"message\":\"half\"}}"
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"logger\":\"echo\","
    "\"data\":\"working\"}}");
  completions.out.length_(0);
  char cancel[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\","
    "\"params\":{\"requestId\":17,\"reason\":\"superseded\"}}";
  ZuCheck(peer.dispatchAsync(cancel, completions, emit, tool));
  ZuCheck(retained->cancelled());
  ZuCheck(completions.cancellations == 1);
  ZuCheck(completions.reason == "superseded");
  ZuCheck(retained->complete(Zjrpc::Reply<EchoOK>{EchoResult{4}}));
  ZuCheck(!retained->live());
  ZuCheck(!completions.count());
  ZuCheck(completions.out ==
    "{\"jsonrpc\":\"2.0\",\"id\":17,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":4}},"
    "\"isError\":false}}");
  ZuCheck(!retained->complete(Zjrpc::Reply<EchoOK>{EchoResult{5}}));

  Zmcp::Peer<EchoCatalog> legacyPeer;
  CompletionHarness legacyCompletions;
  legacyCompletions.era = Zmcp::Era::Legacy;
  ZmRef<Token> legacyRetained;
  auto legacyEmit = [&legacyCompletions](const auto &message) {
    message.write(legacyCompletions.out);
  };
  auto legacyTool = [&legacyRetained](auto *, const auto &, auto completion) {
    legacyRetained = ZuMv(completion);
  };
  char initialize[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
    "\"params\":{\"protocolVersion\":\"2025-11-25\"}}";
  char initialized[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
  char setLevel[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,"
    "\"method\":\"logging/setLevel\","
    "\"params\":{\"level\":\"warning\"}}";
  char legacyCall[] =
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":6}}}";
  ZuCheck(legacyPeer.dispatchAsync(
    initialize, legacyCompletions, legacyEmit, legacyTool));
  ZuCheck(legacyPeer.dispatchAsync(
    initialized, legacyCompletions, legacyEmit, legacyTool));
  ZuCheck(legacyPeer.dispatchAsync(
    setLevel, legacyCompletions, legacyEmit, legacyTool));
  legacyCompletions.out.length_(0);
  ZuCheck(legacyPeer.dispatchAsync(
    legacyCall, legacyCompletions, legacyEmit, legacyTool));
  ZuCheck(legacyRetained && !legacyRetained->log("info", "below level"));
  ZuCheck(legacyRetained->log("error", "visible"));
  ZuCheck(legacyCompletions.out ==
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"error\",\"data\":\"visible\"}}");
  legacyCompletions.out.length_(0);
  ZuCheck(legacyRetained->complete(
    Zjrpc::Reply<EchoOK>{EchoResult{6}}));
  ZuCheck(legacyCompletions.out ==
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":6}},"
    "\"isError\":false}}");

  Zmcp::Peer<EchoCatalog> errorPeer;
  CompletionHarness errorCompletions;
  ZtString<> errorOut;
  auto errorEmit = [&errorOut](const auto &message) {
    message.write(errorOut);
  };
  auto errorTool = [](auto *, const auto &, auto) {
    throw Zjrpc::Error{"conflict", 409};
  };
  char asyncError[] =
    "{\"jsonrpc\":\"2.0\",\"id\":19,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":9}}}";
  ZuCheck(errorPeer.dispatchAsync(
    asyncError, errorCompletions, errorEmit, errorTool));
  ZuCheck(!errorCompletions.count());
  ZuCheck(errorOut.find(
    "\"structuredContent\":{\"code\":409}") >= 0);

  completions.out.length_(0);
  retained = nullptr;
  char late[] =
    "{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":5}}}";
  ZuCheck(peer.dispatchAsync(late, completions, emit, tool));
  ZuCheck(completions.count() == 1);
  ZuCheck(!retained->log("emergency", "not requested"));
  completions.drain();
  ZuCheck(!retained->live());
  ZuCheck(completions.state() == Zjrpc::CompletionState::Closed);
  ZuCheck(!retained->complete(Zjrpc::Reply<EchoOK>{EchoResult{5}}));
  ZuCheck(!completions.out);
}

static void httpTest()
{
  ZuTestScope(http);
  HTTPHarness parser;
  Zhttp::Target target;
  char path[] = "/mcp";
  char origin[] = "https://client.example";
  char session[] = "session-7";
  char method[] = "tools/call";
  char name[] = "advisory-name";
  char body[] = "{\"x\":1}";
  target.path = mutableBytes(path);
  ZuCheck(parser.operation(Zhttp::Method::POST, target));
  parser.template header<HTTPHarness::Origin>(
    Zhttp::FieldSection::Final, mutableBytes(origin));
  parser.template header<HTTPHarness::Session>(
    Zhttp::FieldSection::Final, mutableBytes(session));
  parser.template header<HTTPHarness::Method>(
    Zhttp::FieldSection::Final, mutableBytes(method));
  parser.template header<HTTPHarness::Name>(
    Zhttp::FieldSection::Final, mutableBytes(name));
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Fixed, 7));
  HTTPRx rx{mutableBytes(body)};
  ZuCheck(parser.body(rx));
  parser.complete(static_cast<void *>(nullptr), true);
  ZuCheck(parser.received == 1);
  ZuCheck(parser.receivedBody == "{\"x\":1}");
  ZuCheck(parser.sessionID == "session-7");
  ZuCheck(parser.method == "tools/call");
  ZuCheck(parser.name == "advisory-name");

  parser.reset();
  ZuCheck(parser.operation(Zhttp::Method::DELETE, target));
  parser.template header<HTTPHarness::Origin>(
    Zhttp::FieldSection::Final, mutableBytes(origin));
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Fixed, 0));
  parser.complete(static_cast<void *>(nullptr), true);
  ZuCheck(parser.deletes == 1);

  parser.reset();
  parser.limits_.maxJSONBytes = 4;
  ZuCheck(parser.operation(Zhttp::Method::POST, target));
  parser.template header<HTTPHarness::Origin>(
    Zhttp::FieldSection::Final, mutableBytes(origin));
  ZuCheck(!parser.bodyInfo(Zhttp::BodyType::Fixed, 5));
  parser.complete(static_cast<void *>(nullptr), false);
  ZuCheck(parser.corrupt == 1);

  parser.reset();
  char rejectedOrigin[] = "https://blocked.example";
  ZuCheck(parser.operation(Zhttp::Method::POST, target));
  parser.template header<HTTPHarness::Origin>(
    Zhttp::FieldSection::Final, mutableBytes(rejectedOrigin));
  ZuCheck(!parser.bodyInfo(Zhttp::BodyType::Fixed, 0));
  parser.complete(static_cast<void *>(nullptr), false);
  ZuCheck(parser.originRejected == 1);

  Zjrpc::HTTPFixedBuilder<Zmcp::DiscoverMessage> response;
  response.message.id = int64_t{11};
  HTTPBodyOut out;
  int outcome = -1;
  response.body([&out, &outcome](auto write) {
    outcome = write(out);
  });
  ZuCheck(outcome == Zhttp::WriteOutcome::End);
  HTTPBodyOut rejected;
  rejected.flushOK = false;
  response.body([&rejected, &outcome](auto write) { outcome = write(rejected); });
  ZuCheck(rejected.failed() && outcome == Zhttp::WriteOutcome::Abort);
  auto prefix = ZuCSpan{out.data};
  prefix.trunc(sizeof("{\"jsonrpc\":\"2.0\",\"id\":11,") - 1);
  ZuCheck(prefix == "{\"jsonrpc\":\"2.0\",\"id\":11,");
  HTTPHeaderPatch patch;
  response.bodyHdrs(patch);
  ZuCheck(ZuBox<unsigned>{patch.value} == out.data.length());

  Zjrpc::HTTPFixedBuilder<Zmcp::DiscoverMessage> boundedResponse;
  boundedResponse.message.id = int64_t{12};
  boundedResponse.maxBodyBytes = 16;
  HTTPBodyOut boundedOut;
  outcome = -1;
  boundedResponse.body([&boundedOut, &outcome](auto write) {
    outcome = write(boundedOut);
  });
  ZuCheck(outcome == Zhttp::WriteOutcome::Abort);
  ZuCheck(boundedOut.data.length() <= boundedResponse.maxBodyBytes);

  Zmcp::ToolsCallParams<EchoCatalog> params;
  params.arguments() = Zmcp::ToolArg<Echo>{EchoReq{21}};
  params.progressToken = Zjrpc::IDString{"progress-21"};
  params.logLevel = Zmcp::LogLevel::Warning;
  using CallMessage = Zmcp::ToolCallRequestMessage<EchoCatalog>;
  Zmcp::HTTPRequestBuilder<CallMessage> request;
  request.message = CallMessage{Zjrpc::ID{int64_t{21}}, ZuMv(params)};
  request.endpoint = "/mcp";
  request.sequence = 9;
  ZtString<> requestPath;
  int requestMethod = -1;
  request.operation([&requestPath, &requestMethod](auto method_, auto emit) {
    requestMethod = method_;
    emit([&requestPath](auto write) { write(requestPath); });
  });
  ZuCheck(requestMethod == Zhttp::Method::POST);
  ZuCheck(requestPath == "/mcp");
  ZuCheck(request.key() == 9);
  ZtString<> version, methodHeader, nameHeader;
  request.template header<decltype(request)::Version>(
    [&version](auto value) { version = value; });
  request.template header<decltype(request)::Method>(
    [&methodHeader](auto value) { methodHeader = value; });
  request.template header<decltype(request)::Name>(
    [&nameHeader](auto value) { nameHeader = value; });
  ZuCheck(version == "2026-07-28");
  ZuCheck(methodHeader == "tools/call");
  ZuCheck(nameHeader == "echo");
  ZtString<> parameterName, parameterValue;
  request.header([&parameterName, &parameterValue](auto name, auto value) {
    parameterName = name;
    parameterValue = value;
  });
  ZuCheck(parameterName == "Mcp-Param-Value");
  ZuCheck(parameterValue == "21");
  ZtString<> encoded;
  Zmcp::headerValue(" padded ", [&encoded](auto value) {
    encoded = value;
  });
  ZuCheck(encoded == "=?base64?IHBhZGRlZCA=?=");
  ZtString<> nestedHeaders;
  Zmcp::parameterHeaders(
    HeaderReq{ZtString<>{"line1\nline2"}, HeaderNested{true}},
    [&nestedHeaders](auto name, auto value) {
      nestedHeaders << name << ':' << value << '\n';
    });
  ZuCheck(nestedHeaders ==
    "Mcp-Param-Text:=?base64?bGluZTEKbGluZTI=?=\n"
    "Mcp-Param-Enabled:true\n");

  request.era = Zmcp::Era::Legacy;
  methodHeader.length_(0);
  nameHeader.length_(0);
  parameterName.length_(0);
  request.template header<decltype(request)::Method>(
    [&methodHeader](auto value) { methodHeader = value; });
  request.template header<decltype(request)::Name>(
    [&nameHeader](auto value) { nameHeader = value; });
  request.header([&parameterName](auto name, auto) {
    parameterName = name;
  });
  ZuCheck(!methodHeader && !nameHeader && !parameterName);
  request.era = Zmcp::Era::Modern;
  HTTPBodyOut requestOut;
  request.body([&requestOut](auto write) { (void)write(requestOut); });
  ZuCheck(requestOut.data ==
    "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":21},"
    "\"_meta\":{\"progressToken\":\"progress-21\","
    "\"io.modelcontextprotocol/logLevel\":\"warning\"}}}");

  Zmcp::HTTPRequestBuilder<Zmcp::DeleteRequestMessage> deleteRequest;
  deleteRequest.endpoint = "/mcp";
  deleteRequest.sessionID << "session-9";
  deleteRequest.era = Zmcp::Era::Legacy;
  requestMethod = -1;
  requestPath.length_(0);
  deleteRequest.operation(
    [&requestPath, &requestMethod](auto method_, auto emit) {
      requestMethod = method_;
      emit([&requestPath](auto write) { write(requestPath); });
    });
  ZuCheck(requestMethod == Zhttp::Method::DELETE);
  ZuCheck(deleteRequest.bodyPolicy() == Zhttp::BodyPolicy::None);
  ZtString<> deleteLength, deleteSession;
  deleteRequest.template header<decltype(deleteRequest)::ContentLength>(
    [&deleteLength](auto value) { deleteLength = value; });
  deleteRequest.template header<decltype(deleteRequest)::Session>(
    [&deleteSession](auto value) { deleteSession = value; });
  ZuCheck(deleteLength == "0" && deleteSession == "session-9");

  Zmcp::HTTPResponseBuilder<EchoCatalog> generic;
  generic.init(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{22}}, Zjrpc::Reply<EchoOK>{EchoResult{22}}});
  ZuCheck(generic.bodyPolicy() == Zhttp::BodyPolicy::Fixed);
  HTTPBodyOut genericOut;
  generic.body([&genericOut](auto write) { (void)write(genericOut); });
  ZuCheck(genericOut.data ==
    "{\"jsonrpc\":\"2.0\",\"id\":22,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":22}},"
    "\"isError\":false}}");
  generic.accepted();
  ZuCheck(generic.status() == 202);
  ZuCheck(generic.bodyPolicy() == Zhttp::BodyPolicy::None);

  // HTTP output must retain the dispatcher's borrowed input until it closes.
  ZmRef<Zjrpc::BatchBuf> input = new Zjrpc::BatchBuf{};
  using WireMessage = Zmcp::Server_::DispatchPolicy<EchoCatalog>::Message;
  WireMessage pinned{Zjrpc::ErrorMessage{
    Zjrpc::ID{int64_t{23}}, Zjrpc::ErrorCode::InvalidRequest, "Duplicate request ID"}};
  pinned.input(input);
  generic.init(ZuMv(pinned));
  ZuCheck(input->refCount() == 2);
  HTTPBodyOut errorOut;
  generic.body([&errorOut](auto write) { (void)write(errorOut); });
  ZuCheck(errorOut.data ==
    "{\"jsonrpc\":\"2.0\",\"id\":23,\"error\":{"
    "\"code\":-32600,\"message\":\"Duplicate request ID\"}}");
  generic.close();
  ZuCheck(input->refCount() == 1);

  using HTTPBuilderQ = Zmcp::HTTPBuilderQ<EchoCatalog>;
  ZmRef<HTTPBuilderQ::Node> fixedNode = new HTTPBuilderQ::Node{};
  fixedNode->data().fixed(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{24}}, Zjrpc::Reply<EchoOK>{EchoResult{24}}});
  ZuCheck(fixedNode->data().fixedPtr());
  ZuCheck(!fixedNode->data().streamPtr());
  ZuCheck(fixedNode->data().bodyPolicy() == Zhttp::BodyPolicy::Fixed);
  HTTPBodyOut nodeOut;
  fixedNode->data().body(
    [&nodeOut](auto write) { (void)write(nodeOut); });
  ZuCheck(nodeOut.data ==
    "{\"jsonrpc\":\"2.0\",\"id\":24,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":24}},"
    "\"isError\":false}}");

  ZmRef<HTTPBuilderQ::Node> streamNode = new HTTPBuilderQ::Node{};
  auto nodeSSE = streamNode->data().stream();
  ZuCheck(!streamNode->data().fixedPtr());
  ZuCheck(streamNode->data().streamPtr());
  ZuCheck(streamNode->data().bodyPolicy() == Zhttp::BodyPolicy::Stream);
  ZuCheck(nodeSSE->emit(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{25}}, Zjrpc::Reply<EchoOK>{EchoResult{25}}}));
  HTTPBodyOut nodeSSEOut;
  streamNode->data().body(
    [&streamNode, &nodeSSEOut](auto write) {
      auto outcome = write(nodeSSEOut);
      if (outcome == Zhttp::WriteOutcome::End)
	streamNode->data().close();
      return true;
    });
  ZuCheck(nodeSSEOut.data ==
    "id: 1\ndata: {\"jsonrpc\":\"2.0\",\"id\":25,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,"
    "\"data\":{\"value\":25}},\"isError\":false}}\n\n");

  HTTPResponderHarness<EchoCatalog> fixedHarness;
  auto &fixedResponder = fixedHarness.responder;
  char fixedCall[] =
    "{\"jsonrpc\":\"2.0\",\"id\":26,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":26}}}";
  ZuCheck(fixedHarness.receive(fixedCall));
  ZuCheck(fixedHarness.sends == 1);
  ZuCheck(fixedResponder.terminal());
  HTTPBodyOut fixedResponderOut;
  fixedHarness.builder->data().body(
    [&fixedResponderOut](auto write) {
      (void)write(fixedResponderOut);
    });
  ZuCheck(fixedResponderOut.data ==
    "{\"jsonrpc\":\"2.0\",\"id\":26,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":26}},"
    "\"isError\":false}}");

  HTTPResponderHarness<EchoSSECatalog> responderHarness;
  auto &responder = responderHarness.responder;
  char streamCall[] =
    "{\"jsonrpc\":\"2.0\",\"id\":27,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"echo_stream\","
    "\"arguments\":{\"value\":27},"
    "\"_meta\":{\"progressToken\":\"progress-27\"}}}";
  ZuCheck(responderHarness.receive(streamCall));
  ZuCheck(responderHarness.sends == 1);
  ZuCheck(responder.streaming());
  ZuCheck(responder.terminal());
  HTTPBodyOut responderOut;
  responderHarness.builder->data().body(
    [&responderHarness, &responderOut](auto write) {
      auto outcome = write(responderOut);
      if (outcome == Zhttp::WriteOutcome::End)
	responderHarness.builder->data().close();
      return true;
    });
  ZuCheck(responderOut.data ==
    "id: 1\ndata: {\"jsonrpc\":\"2.0\","
    "\"method\":\"notifications/progress\",\"params\":{"
    "\"progressToken\":\"progress-27\",\"progress\":0.25,"
    "\"total\":1,\"message\":\"quarter\"}}\n\n"
    "id: 2\ndata: {\"jsonrpc\":\"2.0\",\"id\":27,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,"
    "\"data\":{\"value\":27}},\"isError\":false}}\n\n");

  Zmcp::HTTPSSEBuilder<EchoCatalog> sseBuilder;
  sseBuilder.sessionID("legacy-22");
  ZuCheck(sseBuilder.emit(Zmcp::ProgressMessage{
    Zjrpc::IDString{"progress-22"}, "half", .5, 1}));
  ZuCheck(sseBuilder.emit(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{22}}, Zjrpc::Reply<EchoOK>{EchoResult{22}}}));
  HTTPBodyOut sseOut;
  unsigned sseWrites = 0;
  sseBuilder.body([&sseBuilder, &sseOut, &sseWrites](auto write) {
    ++sseWrites;
    auto outcome = write(sseOut);
    if (outcome == Zhttp::WriteOutcome::End) sseBuilder.close();
    return true;
  });
  ZuCheck(sseWrites == 2);
  ZuCheck(sseBuilder.state() == Zjrpc::SSEState::Closed);
  ZuCheck(sseOut.data ==
    "id: 1\ndata: {\"jsonrpc\":\"2.0\","
    "\"method\":\"notifications/progress\",\"params\":{"
    "\"progressToken\":\"progress-22\",\"progress\":0.5,"
    "\"total\":1,\"message\":\"half\"}}\n\n"
    "id: 2\ndata: {\"jsonrpc\":\"2.0\",\"id\":22,\"result\":{"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\",\"content\":[],"
    "\"structuredContent\":{\"code\":200,"
    "\"data\":{\"value\":22}},\"isError\":false}}\n\n");
  ZuCheck(!sseBuilder.emit(Zmcp::LogMessage{"info", "late", {}}));

  Zjrpc::Limits batchLimits;
  batchLimits.workBatch = 2;
  Zmcp::HTTPSSEBuilder<EchoCatalog> batchSSE{batchLimits};
  SSEPostHarness batchPost{&batchSSE};
  batchSSE.owner(&batchPost, 0);
  for (unsigned i = 0; i < 4; ++i)
    ZuCheck(batchSSE.emit(Zmcp::LogMessage{"info", "queued", {}}));
  ZuCheck(batchSSE.emit(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{24}}, Zjrpc::Reply<EchoOK>{EchoResult{24}}}));
  HTTPBodyOut batchOut;
  unsigned batchWrites = 0;
  batchSSE.body([&batchSSE, &batchOut, &batchWrites](auto write) {
    ++batchWrites;
    auto outcome = write(batchOut);
    if (outcome == Zhttp::WriteOutcome::End) batchSSE.close();
    return true;
  });
  ZuCheck(batchWrites == 1 && batchSSE.queued() == 4 &&
    batchPost.posts == 1);
  batchPost.run();
  ZuCheck(batchWrites == 3 && batchSSE.queued() == 2 &&
    batchPost.posts == 2);
  batchPost.run();
  ZuCheck(batchWrites == 5 &&
    batchSSE.state() == Zjrpc::SSEState::Closed);

  Zmcp::HTTPSSEBuilder<EchoCatalog> liveSSE;
  HTTPBodyOut liveOut;
  unsigned liveWrites = 0;
  liveSSE.body([&liveSSE, &liveOut, &liveWrites](auto write) {
    ++liveWrites;
    auto outcome = write(liveOut);
    if (outcome == Zhttp::WriteOutcome::End) liveSSE.close();
    return true;
  });
  ZuCheck(liveWrites == 1);
  ZuCheck(liveSSE.emit(Zmcp::LogMessage{"info", "working", "echo"}));
  ZuCheck(liveWrites == 2);
  ZuCheck(liveSSE.emit(Zmcp::ToolReplyMessage<EchoOK>{
    Zjrpc::ID{int64_t{23}}, Zjrpc::Reply<EchoOK>{EchoResult{23}}}));
  ZuCheck(liveWrites == 3);
  ZuCheck(liveSSE.state() == Zjrpc::SSEState::Closed);

  Zjrpc::Limits sseLimits;
  sseLimits.maxQueue = 1;
  Zmcp::HTTPSSEBuilder<EchoCatalog> saturatedSSE{sseLimits};
  ZuCheck(saturatedSSE.emit(Zmcp::LogMessage{"info", "one", {}}));
  ZuCheck(!saturatedSSE.emit(Zmcp::LogMessage{"info", "two", {}}));
  ZuCheck(saturatedSSE.state() == Zjrpc::SSEState::Failed);
  HTTPBodyOut saturatedOut;
  int saturatedOutcome = -1;
  saturatedSSE.body(
    [&saturatedSSE, &saturatedOut, &saturatedOutcome](auto write) {
      saturatedOutcome = write(saturatedOut);
      if (saturatedOutcome == Zhttp::WriteOutcome::Abort)
	saturatedSSE.close();
      return true;
    });
  ZuCheck(saturatedOutcome == Zhttp::WriteOutcome::Abort);
  ZuCheck(!saturatedOut.data);
  ZuCheck(saturatedSSE.state() == Zjrpc::SSEState::Closed);

  sseLimits.maxQueue = Zjrpc::Default::MaxQueue;
  sseLimits.maxSSEEventBytes = 4;
  Zmcp::HTTPSSEBuilder<EchoCatalog> oversizedEvent{sseLimits};
  ZuCheck(!oversizedEvent.emit(Zmcp::LogMessage{"info", "large", {}}));
  ZuCheck(oversizedEvent.state() == Zjrpc::SSEState::Failed);
  oversizedEvent.close();

  sseLimits.maxSSEEventBytes = Zjrpc::Default::MaxSSEEventBytes;
  sseLimits.maxJSONBytes = 4;
  Zmcp::HTTPSSEBuilder<EchoCatalog> oversizedJSON{sseLimits};
  ZuCheck(!oversizedJSON.emit(Zmcp::LogMessage{"info", "large", {}}));
  ZuCheck(oversizedJSON.state() == Zjrpc::SSEState::Failed);
  oversizedJSON.close();

  HTTPResponseHarness fixed;
  char responseSession[] = "response-session";
  char responseBody[] = "{\"jsonrpc\":\"2.0\",\"id\":21,\"result\":{}}";
  fixed.status(200);
  fixed.template header<Zmcp::SessionID>(
    Zhttp::FieldSection::Final, mutableBytes(responseSession));
  ZuCheck(fixed.bodyInfo(
    Zhttp::BodyType::Fixed, sizeof(responseBody) - 1));
  HTTPRx fixedRx{mutableBytes(responseBody)};
  ZuCheck(fixed.body(fixedRx));
  fixed.complete(static_cast<void *>(nullptr), true);
  ZuCheck(fixed.received == 1);
  ZuCheck(fixed.responseStatus == 200);
  ZuCheck(fixed.receivedBody == responseBody);
  ZuCheck(fixed.sessionID == "response-session");

  HTTPResponseHarness accepted;
  accepted.status(202);
  accepted.complete(static_cast<void *>(nullptr), true);
  ZuCheck(accepted.accepted == 1 && accepted.responseStatus == 202);

  HTTPResponseHarness streamed;
  streamed.status(200);
  streamed.template header<HTTPResponseHarness::ContentType,
    HTTPResponseHarness::SSEContent>(Zhttp::FieldSection::Final);
  ZuCheck(streamed.bodyInfo(Zhttp::BodyType::Streamed, 0));
  char sse1[] = "id: 1\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"not";
  char sse2[] = "ifications/progress\"}\n\n"
    "id: 2\ndata: {\"jsonrpc\":\"2.0\",\"id\":21,\"result\":{}}\n\n";
  HTTPRx sseRx1{mutableBytes(sse1)};
  HTTPRx sseRx2{mutableBytes(sse2)};
  ZuCheck(streamed.body(sseRx1));
  ZuCheck(streamed.body(sseRx2));
  streamed.complete(static_cast<void *>(nullptr), true);
  ZuCheck(streamed.events == 2);
  ZuCheck(streamed.eventID == "2");
  ZuCheck(streamed.receivedBody ==
    "{\"jsonrpc\":\"2.0\",\"id\":21,\"result\":{}}");
  ZuCheck(streamed.completed == 1);

  HTTPResponseHarness rejectedEvent;
  rejectedEvent.acceptSSE = false;
  rejectedEvent.status(200);
  rejectedEvent.template header<HTTPResponseHarness::ContentType,
    HTTPResponseHarness::SSEContent>(Zhttp::FieldSection::Final);
  ZuCheck(rejectedEvent.bodyInfo(Zhttp::BodyType::Streamed, 0));
  char rejectedSSE[] = "data: {}\n\n";
  HTTPRx rejectedRx{mutableBytes(rejectedSSE)};
  ZuCheck(!rejectedEvent.body(rejectedRx));
  rejectedEvent.complete(static_cast<void *>(nullptr), false);
  ZuCheck(rejectedEvent.corrupt == 1);

  HTTPResponseHarness jsonOverride;
  jsonOverride.streaming(true);
  jsonOverride.status(200);
  jsonOverride.template header<HTTPResponseHarness::ContentType,
    HTTPResponseHarness::JSONContent>(Zhttp::FieldSection::Final);
  ZuCheck(jsonOverride.bodyInfo(
    Zhttp::BodyType::Fixed, sizeof(responseBody) - 1));
  HTTPRx jsonOverrideRx{mutableBytes(responseBody)};
  ZuCheck(jsonOverride.body(jsonOverrideRx));
  jsonOverride.complete(static_cast<void *>(nullptr), true);
  ZuCheck(jsonOverride.received == 1 && !jsonOverride.events);

  HTTPResponseHarness bounded;
  bounded.streaming(true);
  bounded.limits_.maxSSELineBytes = 4;
  ZuCheck(bounded.bodyInfo(Zhttp::BodyType::Streamed, 0));
  char oversizedSSE[] = "data: {}\n\n";
  HTTPRx boundedRx{mutableBytes(oversizedSSE)};
  ZuCheck(!bounded.body(boundedRx));
  bounded.complete(static_cast<void *>(nullptr), false);
  ZuCheck(bounded.corrupt == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(defaultsTest);
  ZuTestCall(peerTest);
  ZuTestCall(emptyTest);
  ZuTestCall(legacyTest);
  ZuTestCall(clientTest);
  ZuTestCall(pendingTest);
  ZuTestCall(residueTest);
  ZuTestCall(completionTest);
  ZuTestCall(channelBatchTest);
  ZuTestCall(httpTest);
  ZuTestCall(httpBatchTest);
  ZuTestCall(httpDetachedTest);
  ZuTestCall(batchDeclarationTest);
  return 0;
}
