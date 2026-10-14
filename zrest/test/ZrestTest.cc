//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

#include <zlib/ZmSemaphore.hh>

using namespace ZuTestUtil;

ZuDerive(TestString, ZtString<ZtStringHeapID<"Zrest.Test.String">>);

struct TestObject : public ZmObject { };
struct RawObject : public ZmObject {
  inline static unsigned constructions = 0;
  ZuSpan<uint8_t> data;
  RawObject() { ++constructions; }
  RawObject &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};
struct SignedObject : public ZmObject { TestString value; };
ZfStruct(, (SignedObject, JSON),
  (value, (Required),		String));

struct BodySink {
  TestString data;
  template <typename T> BodySink &operator <<(T &&value) {
    data << ZuFwd<T>(value);
    return *this;
  }
  bool flush() { return true; }
  unsigned produced() const { return data.length(); }
};

template <typename Base>
struct SignedBuilder : public Base {
  enum { Body = Zrest::BodyPolicy::JSON, SignBody = 1 };
  static constexpr unsigned SignBodyBufSize = 256;

  mutable TestString signedBody;

  template <typename S>
  void prefixBody(S &s, const SignedObject *) const {
    s << "{\"body\":";
  }
  template <typename S>
  void signBody(S &s, const SignedObject *, ZuCSpan body) const {
    signedBody = body;
    s << ",\"signed\":true}";
  }
};

struct NoneReq : public Zrest::ReqBuilder<NoneReq, TestObject> { };
struct ZeroReq : public Zrest::ReqBuilder<ZeroReq, TestObject> {
  enum { Body = Zrest::BodyPolicy::Zero };
};
struct NoneRes : public Zrest::ResBuilder<NoneRes, TestObject> { };
struct ZeroRes : public Zrest::ResBuilder<ZeroRes, TestObject> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct ZeroParser : public Zrest::ResParser<ZeroParser, TestObject> {
  enum { Status = 401, Body = Zrest::BodyPolicy::Zero };
};
struct ZeroReqParser : public Zrest::ReqParser<ZeroReqParser, TestObject> {
  enum { Body = Zrest::BodyPolicy::Zero };
};
struct LimitedReqParser : public Zrest::ReqParser<LimitedReqParser, RawObject> {
  enum { Body = Zrest::BodyPolicy::Raw };
  static constexpr uint64_t BodyLimit = 4;
};
struct RawResParser : public Zrest::ResParser<RawResParser, RawObject> {
  enum { Body = Zrest::BodyPolicy::Raw };
};
struct BorrowedReqParser : public Zrest::ReqParser<BorrowedReqParser,
    RawObject> {
  enum { Body = Zrest::BodyPolicy::Raw, Borrowed = 1 };
};
struct BorrowedResParser : public Zrest::ResParser<BorrowedResParser,
    RawObject> {
  enum { Body = Zrest::BodyPolicy::Raw, Borrowed = 1 };
};
struct BorrowedFields : public ZmObject {
  ZuSpan<uint8_t> query;
  ZuSpan<uint8_t> field;
};
struct BorrowedPathReq : public Zrest::ReqParser<BorrowedPathReq,
    BorrowedFields> {
  using Path = ZuStringT<"/borrowed">;
  using Headers = ZhttpHeaders("x-borrow");
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw, Borrowed = 1 };

  auto &queryObject(BorrowedFields *object) { return object->query; }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "x-borrow") this->object->field = value;
  }
};
using BorrowedRequests = ZuTypeList<BorrowedPathReq>;
ZrestCatalogDerive(BorrowedCatalog, BorrowedRequests);
ZrestCatalogImpl(BorrowedCatalog)
struct JSONReqParser : public Zrest::ReqParser<JSONReqParser, SignedObject> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct JSONResParser : public Zrest::ResParser<JSONResParser, SignedObject> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct URIReqParser : public Zrest::ReqParser<URIReqParser, SignedObject> {
  enum { Body = Zrest::BodyPolicy::URI };
  using Body_URI_Facet = ZuFacet::URI;
};
struct URIResParser : public Zrest::ResParser<URIResParser, SignedObject> {
  enum { Body = Zrest::BodyPolicy::URI };
  using Body_URI_Facet = ZuFacet::URI;
};
struct SSEResParser : public Zrest::ResParser<SSEResParser, TestObject> {
  enum { Body = Zrest::BodyPolicy::SSE };
};
struct SSEReqBuilder : public Zrest::ReqBuilder<SSEReqBuilder, TestObject> {
  using Responses = ZuTypeList<SSEResParser>;
};
using SSERequests = ZuTypeList<SSEReqBuilder>;
ZrestCatalogDerive(SSECatalog, SSERequests);
ZrestCatalogImpl(SSECatalog)
struct SSEApp { };
struct SSECall : public ZmObject {
  TestString events;
  TestString id;
  unsigned received = 0;
  unsigned completed = 0;
  unsigned failures = 0;
  bool accept = true;
  bool receiveSSE(Zhttp::SSEEvent event,
      Zrest::SSEMeta<SSEResParser> meta) {
    if (meta.status != 200) return false;
    events << event.data();
    id = event.id;
    ++received;
    return accept;
  }
  void process() { ++completed; }
  void failed() { ++failures; }
};

struct EndpointRes : public Zrest::ResParser<EndpointRes, SignedObject> {
  enum { Body = Zrest::BodyPolicy::JSON };
};
struct EndpointReq : public Zrest::ReqBuilder<EndpointReq, SignedObject> {
  using Responses = ZuTypeList<EndpointRes>;
};
using EndpointRequests = ZuTypeList<EndpointReq>;
ZrestCatalogDerive(EndpointCatalog, EndpointRequests);
ZrestCatalogImpl(EndpointCatalog)
struct EndpointApp;
using EndpointClient = Zrest::Client<EndpointApp, EndpointCatalog>;
struct EndpointApp { };

struct ServerRes : public Zrest::ResBuilder<ServerRes, TestObject> {
  enum { Status = 200 };
};
struct ServerReq : public Zrest::ReqParser<ServerReq, TestObject> {
  using Path = ZuStringT<"/server">;
  using Responses = ZuTypeList<ServerRes>;
  enum { Exact = 1 };
};
using ServerRequests = ZuTypeList<ServerReq>;
ZrestCatalogDerive(ServerCatalog, ServerRequests);
ZrestCatalogImpl(ServerCatalog)
struct ServerApp;
using ServerDecoder = Zrest::MReqParser<ServerCatalog>;
using ServerEndpoint = Zrest::Server<ServerApp, ServerCatalog, ServerDecoder>;
struct ServerApp {
  void listening(int8_t, uint16_t) { }
  void listenFailed(int8_t, bool) { }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
  void request(ServerReq *, const ServerReq &,
      const Zrest::RequestContext &,
      ZmRef<Zrest::Completion<ServerReq, ServerEndpoint>> token) {
    token->complete(Zrest::Reply<ServerRes>{
      ZmRef<TestObject>{new TestObject{}}});
  }
};
static void serverEndpointCompile(ServerEndpoint &server,
    const Zhttp::HubConfig &hub, ServerApp *app) {
  Zrest::ServerConfig config;
  (void)server.init(hub, ZuMv(config), app);
  (void)server.start();
  (void)server.stop();
  server.final();
}
struct EndpointCall : public ZmObject {
  unsigned processed = 0;
  unsigned failures = 0;
  TestString value;
  void process(Zrest::Reply<EndpointRes> reply) {
    ++processed;
    value = reply.bodyObject().value;
  }
  void failed() { ++failures; }
};
static bool endpointCallCompile(EndpointClient &client,
    ZmRef<SignedObject> object, ZmRef<EndpointCall> call)
{
  return client.call<EndpointReq>(0, ZuMv(object), ZuMv(call));
}
static void endpointLifecycleCompile(EndpointClient &client,
    const Zhttp::HubConfig &hub, Zrest::ClientConfig config,
    EndpointApp *app)
{
  (void)client.init(hub, 1, ZuMv(config), app);
  (void)client.start();
  client.start([](bool) { });
  client.stop([](bool) { });
  (void)client.stop();
  client.final();
}
ZuAssert(sizeof(EndpointClient) > 0);

static void endpointHandoffTest()
{
  ZuTestScope(endpointHandoff);
  ZmRef<EndpointCall> call = new EndpointCall{};
  ZmRef<SignedObject> object = new SignedObject{};
  Zrest::ClientReq<EndpointApp, EndpointCatalog> request;
  request.init<EndpointReq>(object, call);
  EndpointRes response;
  response.init();
  response.object->value = "accepted";
  request.process(static_cast<void *>(nullptr), ZuMv(response));
  request.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  ZuCheck(!call->processed && !call->failures);
  request.deliver();
  ZuCheck(call->processed == 1 && !call->failures);
  ZuCheck(call->value == "accepted");

  Zrest::ClientReq<EndpointApp, EndpointCatalog> refused;
  refused.init<EndpointReq>(object, call);
  refused.failure.code = Zrest::Failure::Limit;
  refused.deliver();
  ZuCheck(call->processed == 1 && call->failures == 1);
}

struct FragmentRx {
  ZuSpan<uint8_t> data;

  template <typename Check, typename Consume>
  int64_t consume(Check &&check, Consume &&consume) {
    auto n = check(data);
    if (!n) return 0;
    auto part = data;
    part.trunc(n);
    consume(part);
    data.offset(n);
    return n;
  }
};

static FragmentRx fragment(TestString &data)
{
  return {ZuSpan<uint8_t>{data.data(), data.length()}};
}
struct ExactReq : public Zrest::ReqParser<ExactReq, RawObject> {
  using Path = ZuStringT<"/authorize">;
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  static constexpr uint64_t QueryLimit = 12;
};
struct ExactNoQuery : public Zrest::ReqParser<ExactNoQuery, TestObject> {
  using Path = ZuStringT<"/.well-known/oauth-authorization-server">;
  enum { Exact = 1 };
};
struct PrefixedReq : public Zrest::ReqParser<PrefixedReq, RawObject> {
  using Base = Zrest::ReqParser<PrefixedReq, RawObject>;
  using Path = ZuStringT<"/v1/token">;
  enum { Exact = 1, Query = Zrest::QueryPolicy::Raw };
  using Headers = ZhttpHeaders("x-route");

  ZuBSpan projectedPath;
  TestString routeHeader;
  inline static unsigned completions = 0;

  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    projectedPath = target.path;
    return Base::operation(method, target);
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "x-route") routeHeader = value;
  }
  template <typename Link> void complete(Link *, bool ok) {
    if (ok) ++completions;
  }
};
struct SignedReq : public SignedBuilder<
    Zrest::ReqBuilder<SignedReq, SignedObject>> { };
struct SignedRes : public SignedBuilder<
    Zrest::ResBuilder<SignedRes, SignedObject>> { };

struct ReplyA { };
struct ReplyB { };
struct RequestA : public Zrest::Request {
  using Headers = ZhttpHeaders("x-a");
  using Responses = ZuTypeList<ReplyA>;
};
struct RequestB : public Zrest::Request {
  using Headers = ZhttpHeaders("x-b");
  using Responses = ZuTypeList<ReplyB>;
};
struct RequestC : public Zrest::Request {
  using Responses = ZuTypeList<ReplyA>;
};

struct DuplicateExactA : public Zrest::Request {
  using Path = ZuStringT<"/duplicate">;
  enum { Exact = 1 };
};
struct DuplicateExactB : public Zrest::Request {
  using Path = ZuStringT<"/duplicate">;
  enum { Exact = 1 };
};
struct DuplicatePrefixA : public Zrest::Request {
  using Path = ZuStringT<"/prefix">;
};
struct DuplicatePrefixB : public Zrest::Request {
  using Path = ZuStringT<"/prefix">;
};
struct PostDuplicate : public DuplicateExactA {
  enum { Method = Zhttp::Method::POST };
};
struct ExactPrefixOverlap : public DuplicateExactA {
  enum { Exact = 0 };
};
struct OverlapExact : public Zrest::Request {
  using Path = ZuStringT<"/overlap">;
  enum { Exact = 1 };
};
struct OverlapPrefix : public Zrest::Request {
  using Path = ZuStringT<"/overlap">;
};

ZuAssert((!Zrest::ReqCatalogValid<ZuTypeList<
  DuplicateExactA, DuplicateExactB>>{}));
ZuAssert((!Zrest::ReqCatalogValid<ZuTypeList<
  DuplicatePrefixA, DuplicatePrefixB>>{}));
ZuAssert((Zrest::ReqCatalogValid<ZuTypeList<
  DuplicateExactA, PostDuplicate>>{}));
ZuAssert((Zrest::ReqCatalogValid<ZuTypeList<
  DuplicateExactA, ExactPrefixOverlap>>{}));
using OneRequest = ZuTypeList<RequestA>;
using TwoRequests = ZuTypeList<RequestA, RequestB>;
using OneHeader = ZuTypeList<ZuStringT<"x-a">>;
using TwoHeaders = ZuTypeList<ZuStringT<"x-a">, ZuStringT<"x-b">>;
using MultiHeaders = ZhttpHeaders(("content-type", "a"),
  ("content-type", "b"));
using ContentType = ZuStringT<"content-type">;
using ContentTypes = ZuTypeList<ZuStringT<"a">, ZuStringT<"b">>;

ZuAssert((ZuIsSame<Zrest::GetAllHdrKeys<OneRequest>, OneHeader>{}));
ZuAssert((ZuIsSame<Zrest::GetAllHdrKeys<TwoRequests>, TwoHeaders>{}));
ZuAssert((ZuIsSame<Zrest::GetAllResponses<OneRequest>,
  ZuTypeList<ReplyA>>{}));
ZuAssert((ZuIsSame<Zrest::GetAllResponses<TwoRequests>,
  ZuTypeList<ReplyA, ReplyB>>{}));
ZuAssert((ZuIsSame<Zrest::GetAllResponses<
  ZuTypeList<RequestA, RequestB, RequestC>>,
  ZuTypeList<ReplyA, ReplyB>>{}));
ZuAssert((ZuIsSame<Zhttp::HeaderValues<ContentType, MultiHeaders>,
  ContentTypes>{}));

static void zeroBodyTest()
{
  ZuTestScope(zeroBody);
  using ContentLength = ZuStringT<"content-length">;
  ZuCheck((!ZuTypeIn<ContentLength,
    Zrest::GetTypeHdrKeys<NoneReq>>{}));
  ZuCheck((ZuTypeIn<ContentLength,
    Zrest::GetTypeHdrKeys<ZeroReq>>{}));
  ZuCheck((!ZuTypeIn<ContentLength,
    Zrest::GetTypeHdrKeys<NoneRes>>{}));
  ZuCheck((ZuTypeIn<ContentLength,
    Zrest::GetTypeHdrKeys<ZeroRes>>{}));

  ZeroReq request;
  ZeroRes response;
  ZtString<> requestLength, responseLength;
  request.template header<ContentLength>(
    [&requestLength](ZuCSpan value) { requestLength = value; });
  response.template header<ContentLength>(
    [&responseLength](ZuCSpan value) { responseLength = value; });
  ZuCheck(requestLength == "0");
  ZuCheck(responseLength == "0");
  ZuCheck(request.bodyPolicy() == Zhttp::BodyPolicy::None);
  ZuCheck(response.bodyPolicy() == Zhttp::BodyPolicy::None);
  ZuCheck(response.status() == 401);

  ZeroParser parser;
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Fixed, 0));
  ZuCheck(!parser.bodyInfo(Zhttp::BodyType::Fixed, 1));
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  ZeroReqParser requestParser;
  ZuCheck(requestParser.bodyInfo(Zhttp::BodyType::Fixed, 0));
  ZuCheck(!requestParser.bodyInfo(Zhttp::BodyType::Fixed, 1));
  ZuCheck(requestParser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  LimitedReqParser limited;
  ZuCheck(!limited.bodyInfo(Zhttp::BodyType::Fixed, 4));
  limited.bodyMax(8);
  ZuCheck(limited.bodyInfo(Zhttp::BodyType::Fixed, 4));
  ZuCheck(!limited.bodyInfo(Zhttp::BodyType::Fixed, 5));
}

static void streamedBodyTest()
{
  ZuTestScope(streamedBody);
  URIReqParser uriRequest;
  uriRequest.init();
  uriRequest.bodyMax(64);
  ZuCheck(!uriRequest.bodyInfo(Zhttp::BodyType::Streamed, 0));
  URIResParser uriResponse;
  uriResponse.init();
  uriResponse.bodyMax(64);
  ZuCheck(!uriResponse.bodyInfo(Zhttp::BodyType::Streamed, 0));

  LimitedReqParser request;
  request.init();
  request.bodyMax(8);
  ZuCheck(request.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString ab{"ab"}, cd{"cd"};
  auto a = fragment(ab), b = fragment(cd);
  ZuCheck(request.body(a));
  ZuCheck(request.body(b));
  ZuCheck(request.finishBody());
  ZuCheck(request.object->data == "abcd");
  request.reset();

  RawResParser response;
  response.init();
  response.bodyMax(4);
  ZuCheck(response.bodyInfo(Zhttp::BodyType::Streamed, 0));
  auto c = fragment(ab), d = fragment(cd);
  ZuCheck(response.body(c));
  ZuCheck(response.body(d));
  ZuCheck(response.finishBody());
  ZuCheck(response.object->data == "abcd");
  Zrest::Reply<RawResParser> reply{ZuMv(response)};
  ZuCheck(reply.bodyObject().data == "abcd");
  response.reset();
  ZuCheck(reply.bodyObject().data == "abcd");

  response.init();
  response.bodyMax(3);
  ZuCheck(response.bodyInfo(Zhttp::BodyType::Streamed, 0));
  auto e = fragment(ab), f = fragment(cd);
  ZuCheck(response.body(e));
  ZuCheck(!response.body(f));
  ZuCheck(!response.finishBody());
  response.reset();

  JSONReqParser jsonRequest;
  jsonRequest.init();
  jsonRequest.bodyMax(64);
  ZuCheck(jsonRequest.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString valid{"{\"value\":\"yes\"}"};
  auto g = fragment(valid);
  ZuCheck(jsonRequest.body(g));
  ZuCheck(jsonRequest.finishBody());
  ZuCheck(jsonRequest.object->value == "yes");
  jsonRequest.reset();

  jsonRequest.init();
  jsonRequest.bodyMax(64);
  ZuCheck(jsonRequest.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString multiple{"{\"value\":\"one\"}\n{\"value\":\"two\"}"};
  auto extra = fragment(multiple);
  ZuCheck(jsonRequest.body(extra));
  ZuCheck(!jsonRequest.finishBody());
  jsonRequest.reset();

  JSONResParser jsonResponse;
  jsonResponse.init();
  jsonResponse.bodyMax(64);
  ZuCheck(jsonResponse.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString padded{"{\"value\":\"ok\"} \r\n"};
  auto paddedRx = fragment(padded);
  ZuCheck(jsonResponse.body(paddedRx));
  ZuCheck(jsonResponse.finishBody());
  jsonResponse.reset();

  jsonResponse.init();
  jsonResponse.bodyMax(64);
  ZuCheck(jsonResponse.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString invalid{"{\"value\":"};
  auto h = fragment(invalid);
  ZuCheck(jsonResponse.body(h));
  ZuCheck(!jsonResponse.finishBody());
  jsonResponse.reset();
}

static void fixedBodyTest()
{
  ZuTestScope(fixedBody);
  RawResParser raw;
  raw.init();
  raw.bodyMax(4);
  ZuCheck(!raw.bodyInfo(Zhttp::BodyType::Fixed, uint64_t(-1)));
  ZuCheck(raw.bodyInfo(Zhttp::BodyType::Fixed, 4));
  TestString partial{"ab"};
  auto first = fragment(partial);
  ZuCheck(raw.body(first) && first.data.length() == 2);
  TestString full{"abcd"};
  auto second = fragment(full);
  ZuCheck(raw.body(second) && !second.data);
  ZuCheck(raw.finishBody() && raw.object->data == "abcd");
  raw.reset();

  JSONReqParser json;
  json.init();
  json.bodyMax(64);
  TestString valid;
  valid << "{\"value\":\"ok\"}";
  ZuCheck(json.bodyInfo(Zhttp::BodyType::Fixed, valid.length()));
  TestString shortBody{"{\"value\""};
  auto third = fragment(shortBody);
  ZuCheck(json.body(third) && third.data.length() == shortBody.length());
  auto fourth = fragment(valid);
  ZuCheck(json.body(fourth) && !fourth.data);
  ZuCheck(json.finishBody() && json.object->value == "ok");
  json.reset();

  json.init();
  json.bodyMax(64);
  TestString malformed;
  malformed << "{\"value\":";
  ZuCheck(json.bodyInfo(Zhttp::BodyType::Fixed, malformed.length()));
  auto fifth = fragment(malformed);
  ZuCheck(!json.body(fifth));
  ZuCheck(!json.finishBody());
  json.reset();
  json.init();
  json.bodyMax(64);
  TestString validAgain;
  validAgain << "{\"value\":\"ok\"}";
  ZuCheck(json.bodyInfo(Zhttp::BodyType::Fixed, validAgain.length()));
  auto sixth = fragment(validAgain);
  ZuCheck(json.body(sixth));
  ZuCheck(json.finishBody() && json.object->value == "ok");
}

static void borrowedBodyTest()
{
  ZuTestScope(borrowedBody);
  TestString source;
  source << "owned";
  BorrowedReqParser request;
  request.init();
  request.bodyMax(16);
  ZuCheck(request.bodyInfo(Zhttp::BodyType::Fixed, source.length()));
  auto rx = fragment(source);
  ZuCheck(request.body(rx));
  ZuCheck(request.finishBody());
  source[0] = 'x';
  ZuCheck(request.object->data == "owned");
  BorrowedReqParser retained{ZuMv(request)};
  request.reset();
  ZuCheck(retained.object->data == "owned");

  source.length_(0);
  source << "reply";
  BorrowedResParser response;
  response.init();
  response.bodyMax(16);
  ZuCheck(response.bodyInfo(Zhttp::BodyType::Fixed, source.length()));
  auto rr = fragment(source);
  ZuCheck(response.body(rr));
  ZuCheck(response.finishBody());
  source[0] = 'x';
  Zrest::Reply<BorrowedResParser> reply{ZuMv(response)};
  response.reset();
  ZuCheck(reply.bodyObject().data == "reply");
}

static void borrowedFieldsTest()
{
  ZuTestScope(borrowedFields);
  Zrest::MReqParser<BorrowedCatalog> parser;
  TestString path;
  path << "/borrowed?q=1";
  Zhttp::Target target;
  target.path = path;
  ZuCheck(parser.operation(Zhttp::Method::GET, target));
  TestString field;
  field << "retained";
  parser.template header<ZuStringT<"x-borrow">>(
    Zhttp::FieldSection::Final, field);
  auto &selected = parser.u.template p<BorrowedPathReq>();
  path[10] = 'x';
  field[0] = 'x';
  ZuCheck(selected.object->query == "?q=1");
  ZuCheck(selected.object->field == "retained");
  BorrowedPathReq retained{ZuMv(selected)};
  parser.reset();
  ZuCheck(retained.object->query == "?q=1");
  ZuCheck(retained.object->field == "retained");
}

static void sseReceiveTest()
{
  ZuTestScope(sseReceive);
  using Request = Zrest::ClientReq<SSEApp, SSECatalog>;
  ZmRef<SSECall> call = new SSECall{};
  Request request;
  request.init<SSEReqBuilder>(ZmRef<TestObject>{new TestObject{}}, call);
  Zrest::MResParser<SSECatalog, Request> parser;
  parser.init(request);
  parser.bodyMax(128);
  ZuCheck(parser.status(200));
  parser.template header<ZuStringT<"content-type">,
    ZuStringT<"text/event-stream">>(Zhttp::FieldSection::Final);
  ZuCheck(parser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString first;
  first << "data: hel";
  auto a = fragment(first);
  ZuCheck(parser.body(a));
  ZuCheck(!call->received);
  TestString second;
  second << "lo\nid: one\n\n";
  auto b = fragment(second);
  ZuCheck(parser.body(b));
  ZuCheck(call->received == 1 && call->events == "hello" &&
    call->id == "one");
  TestString grouped;
  grouped << "data: a\n\ndata: b\n\n";
  auto groupedRx = fragment(grouped);
  ZuCheck(parser.body(groupedRx));
  ZuCheck(call->received == 3 && call->events == "helloab" &&
    !call->id);
  ZuCheck(!call->completed && !call->failures);
  parser.complete(static_cast<void *>(nullptr), true);
  request.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  ZuCheck(!call->completed);
  request.deliver();
  ZuCheck(call->completed == 1 && !call->failures);

  ZmRef<SSECall> fixedCall = new SSECall{};
  Request fixed;
  fixed.init<SSEReqBuilder>(ZmRef<TestObject>{new TestObject{}}, fixedCall);
  Zrest::MResParser<SSECatalog, Request> fixedParser;
  fixedParser.init(fixed);
  fixedParser.bodyMax(128);
  ZuCheck(fixedParser.status(200));
  fixedParser.template header<ZuStringT<"content-type">,
    ZuStringT<"text/event-stream">>(Zhttp::FieldSection::Final);
  TestString fixedBody;
  fixedBody << "data: fixed\n\n";
  ZuCheck(fixedParser.bodyInfo(Zhttp::BodyType::Fixed,
    fixedBody.length()));
  auto fixedRx = fragment(fixedBody);
  ZuCheck(fixedParser.body(fixedRx));
  ZuCheck(fixedCall->received == 1 && fixedCall->events == "fixed" &&
    !fixedCall->completed);
  fixedParser.complete(static_cast<void *>(nullptr), true);
  fixed.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  fixed.deliver();
  ZuCheck(fixedCall->completed == 1 && !fixedCall->failures);

  ZmRef<SSECall> partialCall = new SSECall{};
  Request partial;
  partial.init<SSEReqBuilder>(ZmRef<TestObject>{new TestObject{}},
    partialCall);
  Zrest::MResParser<SSECatalog, Request> partialParser;
  partialParser.init(partial);
  partialParser.bodyMax(128);
  ZuCheck(partialParser.status(200));
  partialParser.template header<ZuStringT<"content-type">,
    ZuStringT<"text/event-stream">>(Zhttp::FieldSection::Final);
  ZuCheck(partialParser.bodyInfo(Zhttp::BodyType::Fixed, 5));
  TestString incomplete;
  incomplete << "data:";
  auto c = fragment(incomplete);
  ZuCheck(partialParser.body(c));
  partialParser.complete(static_cast<void *>(nullptr), true);
  partial.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  partial.deliver();
  ZuCheck(!partialCall->completed && partialCall->failures == 1);

  ZmRef<SSECall> rejectedCall = new SSECall{};
  rejectedCall->accept = false;
  Request rejected;
  rejected.init<SSEReqBuilder>(ZmRef<TestObject>{new TestObject{}},
    rejectedCall);
  Zrest::MResParser<SSECatalog, Request> rejectedParser;
  rejectedParser.init(rejected);
  rejectedParser.bodyMax(128);
  ZuCheck(rejectedParser.status(200));
  rejectedParser.template header<ZuStringT<"content-type">,
    ZuStringT<"text/event-stream">>(Zhttp::FieldSection::Final);
  ZuCheck(rejectedParser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString event;
  event << "data: no\n\n";
  auto d = fragment(event);
  ZuCheck(!rejectedParser.body(d));
  rejectedParser.complete(static_cast<void *>(nullptr), false);
  rejected.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  rejected.deliver();
  ZuCheck(rejectedCall->received == 1 &&
    !rejectedCall->completed && rejectedCall->failures == 1);

  Zhttp::SSEDecoder lineLimited{7, 32};
  ZuCheck(!lineLimited.feed("data: ab\n\n",
    [](Zhttp::SSEEvent) { }));
  Zhttp::SSEDecoder eventLimited{32, 1};
  ZuCheck(!eventLimited.feed("data: ab\n\n",
    [](Zhttp::SSEEvent) { }));

  unsigned atLimit = 0;
  Zhttp::SSEDecoder exactLine{8, 2};
  ZuCheck(exactLine.feed("data: ", [&atLimit](Zhttp::SSEEvent) {
    ++atLimit;
  }));
  ZuCheck(exactLine.feed("xy\n\n", [&atLimit](Zhttp::SSEEvent event_) {
    if (event_.data() == "xy") ++atLimit;
  }));
  ZuCheck(atLimit == 1 && exactLine.complete());
  Zhttp::SSEDecoder splitLine{8, 4};
  ZuCheck(splitLine.feed("data: ", [](Zhttp::SSEEvent) { }));
  ZuCheck(!splitLine.feed("xyz", [](Zhttp::SSEEvent) { }));

  Zhttp::SSEDecoder splitEvent{32, 4};
  ZuCheck(splitEvent.feed("data: ab\n", [](Zhttp::SSEEvent) { }));
  ZuCheck(!splitEvent.feed("data: cd\n\n",
    [](Zhttp::SSEEvent) { }));

  ZmRef<SSECall> totalCall = new SSECall{};
  Request total;
  total.init<SSEReqBuilder>(ZmRef<TestObject>{new TestObject{}},
    totalCall);
  Zrest::MResParser<SSECatalog, Request> totalParser;
  totalParser.init(total);
  totalParser.bodyMax(8);
  ZuCheck(totalParser.status(200));
  totalParser.template header<ZuStringT<"content-type">,
    ZuStringT<"text/event-stream">>(Zhttp::FieldSection::Final);
  ZuCheck(totalParser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  TestString totalPrefix{"data: x\n"}, totalEnd{"\n"};
  auto totalA = fragment(totalPrefix), totalB = fragment(totalEnd);
  ZuCheck(totalParser.body(totalA));
  ZuCheck(!totalParser.body(totalB));
  totalParser.complete(static_cast<void *>(nullptr), false);
  total.completed(Zhttp::Result{.code = Zhttp::ResultCode::OK});
  total.deliver();
  ZuCheck(!totalCall->completed && totalCall->failures == 1);
}

using ExactRequests = ZuTypeList<ExactReq, ExactNoQuery>;
ZrestCatalogDerive(ExactCatalog, ExactRequests);
ZrestCatalogImpl(ExactCatalog)

static void exactPathTest()
{
  ZuTestScope(exactPath);
  using Parser = Zrest::MReqParser<ExactCatalog>;
  auto match = [](ZuCSpan path) {
    unsigned constructions = RawObject::constructions;
    Parser parser;
    TestString data;
    data << path;
    Zhttp::Target target;
    target.path = data;
    bool matched = parser.operation(Zhttp::Method::GET, target);
    if (matched && path.find<"/authorize">() == 0)
      ZuCheck(RawObject::constructions == constructions + 1);
    return matched;
  };
  ZuCheck(match("/authorize"));
  ZuCheck(match("/authorize?client_id=x"));
  ZuCheck(!match("/authorize?client_id=long"));
  ZuCheck(!match("/authorize/extra"));
  ZuCheck(match("/.well-known/oauth-authorization-server"));
  ZuCheck(!match("/.well-known/oauth-authorization-server?x=y"));
}

struct ResponseA : public Zrest::ResBuilder<ResponseA, TestObject> {
  enum { Status = 201 };
};
struct ResponseReq : public Zrest::Request {
  using Responses = ZuTypeList<ResponseA>;
};
using ResponseRequests = ZuTypeList<ResponseReq>;
ZrestCatalogDerive(ResponseCatalog, ResponseRequests);
ZrestCatalogImpl(ResponseCatalog)

struct CompletionOwner {
  ZmAtomic<unsigned> count = 0;
  ZmAtomic<unsigned> reentrant = 0;
  ZmSemaphore done;
  template <typename Req, typename Res, typename Token>
  bool complete(Token *token, Zrest::Reply<Res>) {
    ++count;
    if (!token->complete(Zrest::Reply<Res>{})) ++reentrant;
    token->invalidate();
    done.post();
    return true;
  }
};

static void completionClaimTest()
{
  ZuTestScope(completionClaim);
  ZmScheduler scheduler{ZmSchedParams().id("zrest-completion").nThreads(1)};
  bool started = scheduler.start();
  ZuCheck(started);
  if (!started) return;
  CompletionOwner owner;
  using Token = Zrest::Completion<ResponseReq, CompletionOwner>;
  ZmRef<Token> token = new Token{&owner, nullptr, &scheduler, 1};
  ZuCheck(token->live());
  ZuCheck(token->complete(Zrest::Reply<ResponseA>{
    ZmRef<TestObject>{new TestObject{}}}));
  ZuCheck(!token->complete(Zrest::Reply<ResponseA>{
    ZmRef<TestObject>{new TestObject{}}}));
  ZuCheck(owner.done.timedwait(Zm::now(10)) == 0);
  ZuCheck(owner.count.load_() == 1);
  ZuCheck(owner.reentrant.load_() == 1);
  ZuCheck(!token->live() && !token->cancelled());
  ZmAtomic<int> late = -1;
  ZmSemaphore lateDone;
  scheduler.run([token, &late, &lateDone]() {
    late = token->complete(Zrest::Reply<ResponseA>{
      ZmRef<TestObject>{new TestObject{}}});
    lateDone.post();
  }, 1);
  ZuCheck(lateDone.timedwait(Zm::now(10)) == 0 &&
    late.load_() == 0 && owner.count.load_() == 1);
  ZuCheck(scheduler.stop());
}

struct ResponseWork : public ZmObject {
  unsigned *retirements = nullptr;
  void retired(const Zhttp::ResponseResult &result) {
    if (result.ok()) ++*retirements;
  }
};

using OverlapRequests = ZuTypeList<OverlapExact, OverlapPrefix>;
ZrestCatalogDerive(OverlapCatalog, OverlapRequests);
ZrestCatalogImpl(OverlapCatalog)

static void dispatchBoundaryTest()
{
  ZuTestScope(dispatchBoundary);
  ZuBSpan path;
  path = "/duplicate";
  ZuCheck(ResponseCatalog::reqMatch(Zhttp::Method::T(-1), path) == -1);
  ZuCheck(Zrest::resMatch<ResponseRequests>(ResponseRequests::N, 201) == -1);
  ZuCheck(OverlapCatalog::reqMatch(Zhttp::Method::GET,
    ZuBSpan{"/overlap"}) == 0);
  ZuCheck(OverlapCatalog::reqMatch(Zhttp::Method::GET,
    ZuBSpan{"/overlap/child"}) == 1);
  Zrest::MReqParser<ResponseCatalog> parser;
  Zrest::MResBuilder<ResponseCatalog> builder;
  TestObject object;
  builder.init<ResponseA>(parser, &object);
  ZuCheck(builder.status() == 500);

  using Q = Zrest::Server_::ResQ<ResponseCatalog>;
  unsigned retirements = 0;
  ZmRef<ResponseWork> work = new ResponseWork{};
  work->retirements = &retirements;
  ZmRef<TestObject> body = new TestObject{};
  ZmRef<Q::Node> response = new Q::Node{};
  response->init<ResponseReq>(Zrest::Reply<ResponseA>{ZuMv(body)},
    Zhttp::Method::HEAD, work.ptr());
  ZuCheck(response->method() == Zhttp::Method::HEAD);
  ZuCheck(!response->disconnect());
  response->completed({Zhttp::ResponseCode::Submitted});
  response = nullptr;
  ZuCheck(retirements == 1);
}

using PrefixRequests = ZuTypeList<PrefixedReq>;
ZrestCatalogDerive(PrefixCatalog, PrefixRequests);
ZrestCatalogImpl(PrefixCatalog)
using PrefixParser = Zrest::MReqParser<PrefixCatalog,
  Zrest::MReqPolicy<PrefixCatalog, 2>>;

struct TestTarget : public Zhttp::Target {
  TestString data;

  TestTarget(ZuCSpan path_) {
    data << path_;
    path = data;
  }
};

static TestTarget target(ZuCSpan path)
{
  return TestTarget{path};
}

static void skipPathTest()
{
  ZuTestScope(skipPath);
  PrefixParser parser;
  auto requestTarget = target(
    "/oauth2/ping/v1/token?grant_type=refresh_token");
  ZuCheck(parser.operation(Zhttp::Method::GET, requestTarget));
  ZuCheck(requestTarget.path ==
    "/oauth2/ping/v1/token?grant_type=refresh_token");
  parser.u.dispatch([](auto, auto &request) {
    ZuCheck(request.projectedPath ==
      "/v1/token?grant_type=refresh_token");
    ZuCheck(request.object->data == "?grant_type=refresh_token");
  });

  PrefixParser malformed;
  auto relative = target("oauth2/ping/v1/token");
  ZuCheck(!malformed.operation(Zhttp::Method::GET, relative));
  auto shortPath = target("/oauth2/ping");
  ZuCheck(!malformed.operation(Zhttp::Method::GET, shortPath));
}

struct RootServer { };

struct AppPath { uint64_t appID = 0; };
ZfStruct(, (AppPath, URI),
  (appID, (URI::PathIndex<0>, Required),	UInt64));

struct AppParser : public Zrest::MReqParser<PrefixCatalog> {
  using Base = Zrest::MReqParser<PrefixCatalog>;
  uint64_t appID = 0;

  void init(RootServer &) { appID = 0; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    AppPath app;
    auto suffix = ZfURI::loadPathPrefix(app, target.path);
    if (!suffix || !app.appID) return false;
    auto projected = target;
    projected.path = suffix;
    if (!Base::operation(method, projected)) return false;
    appID = app.appID;
    return true;
  }
};

struct HealthReq : public Zrest::ReqParser<HealthReq, TestObject> {
  using Path = ZuStringT<"/live">;
  enum { Exact = 1 };
};
using HealthRequests = ZuTypeList<HealthReq>;
ZrestCatalogDerive(HealthCatalog, HealthRequests);
ZrestCatalogImpl(HealthCatalog)
using HealthParser = Zrest::MReqParser<HealthCatalog>;

struct EnrollReq : public Zrest::ReqParser<EnrollReq, TestObject> {
  using Path = ZuStringT<"">;
  enum { Exact = 1 };
};
using EnrollRequests = ZuTypeList<EnrollReq>;
ZrestCatalogDerive(EnrollCatalog, EnrollRequests);
ZrestCatalogImpl(EnrollCatalog)
using EnrollParser = Zrest::MReqParser<EnrollCatalog>;

using TestRoots = ZuTypeList<
  Zrest::ReqRoot<ZuStringT<"oauth2">, AppParser>,
  Zrest::ReqRoot<ZuStringT<"health">, HealthParser>,
  Zrest::ReqRoot<ZuStringT<"enroll">, EnrollParser>>;
ZrestRootCatalogDerive(RootCatalog, TestRoots);
ZrestRootCatalogImpl(RootCatalog)
using RootParser = Zrest::MReqParser<RootCatalog,
  Zrest::MReqRootPolicy<RootCatalog, RootServer>>;

using ResponseRoots = ZuTypeList<
  Zrest::ReqRoot<ZuStringT<"api">, ServerDecoder>>;
ZrestRootCatalogDerive(ResponseRootCatalog, ResponseRoots);
ZrestRootCatalogImpl(ResponseRootCatalog)
using ResponseRootParser = Zrest::MReqParser<ResponseRootCatalog,
  Zrest::MReqRootPolicy<ResponseRootCatalog>>;
struct ResponseRootApp;
using ResponseRootServer = Zrest::Server<ResponseRootApp, ServerCatalog,
  ResponseRootParser>;
struct ResponseRootApp {
  void listening(int8_t, uint16_t) { }
  void listenFailed(int8_t, bool) { }
  void connected(int8_t) { }
  void disconnected(int8_t) { }
  void request(ServerReq *, const ServerReq &,
      const Zrest::RequestContext &,
      ZmRef<Zrest::Completion<ServerReq, ResponseRootServer>> token) {
    token->complete(Zrest::Reply<ServerRes>{
      ZmRef<TestObject>{new TestObject{}}});
  }
};
ZuAssert(sizeof(ResponseRootServer) > 0);
static void responseRootEndpointCompile(ResponseRootServer &server,
    const Zhttp::HubConfig &hub, ResponseRootApp *app) {
  Zrest::ServerConfig config;
  (void)server.init(hub, ZuMv(config), app);
  (void)server.start();
  server.admit(ServerReq{}, Zrest::RequestContext{}, {});
  (void)server.stop();
  server.final();
}

static void rootPathTest()
{
  ZuTestScope(rootPath);
  ResponseRootParser responseRoot;
  auto responseTarget = target("/api/server");
  ZuCheck(responseRoot.operation(Zhttp::Method::GET, responseTarget));
  Zrest::MResBuilder<ServerCatalog> responseBuilder;
  ZmRef<TestObject> responseObject = new TestObject{};
  responseBuilder.init<ServerRes>(
    responseRoot.u.template p<ServerDecoder>(), responseObject.ptr());
  ZuCheck(responseBuilder.status() == 200);
  responseRoot.reset();

  RootServer server;
  RootParser parser;
  parser.init(server);
  parser.bodyMax(4096);

  auto app = target("/oauth2/42/v1/token?grant_type=refresh_token");
  ZuCheck(parser.operation(Zhttp::Method::GET, app));
  ZuCheck(app.path ==
    "/oauth2/42/v1/token?grant_type=refresh_token");
  auto &appParser = parser.u.template p<AppParser>();
  ZuCheck(appParser.appID == 42);
  ZuCheck(appParser.inputMax == 4096);
  auto &request = appParser.u.template p<PrefixedReq>();
  ZuCheck(request.inputMax == 4096);
  ZuCheck(request.projectedPath ==
    "/v1/token?grant_type=refresh_token");
  ZuCheck(request.object->data == "?grant_type=refresh_token");
  TestString header{"selected-child"};
  parser.template header<ZuStringT<"x-route">>(
    Zhttp::FieldSection::Final, header);
  ZuCheck(request.routeHeader == "selected-child");
  unsigned completions = PrefixedReq::completions;
  struct TestLink { };
  TestLink *link = nullptr;
  parser.complete(link, true);
  ZuCheck(PrefixedReq::completions == completions + 1);
  parser.reset();

  auto health = target("/health/live");
  ZuCheck(parser.operation(Zhttp::Method::GET, health));
  ZuCheck(parser.u.template is<HealthParser>());
  parser.reset();

  auto enroll = target("/enroll");
  ZuCheck(parser.operation(Zhttp::Method::GET, enroll));
  ZuCheck(parser.u.template is<EnrollParser>());
  parser.reset();

  auto enrollQuery = target("/enroll?invalid=true");
  ZuCheck(!parser.operation(Zhttp::Method::GET, enrollQuery));
  auto unknown = target("/unknown/live");
  ZuCheck(!parser.operation(Zhttp::Method::GET, unknown));
  auto boundary = target("/oauth2x/42/v1/token");
  ZuCheck(!parser.operation(Zhttp::Method::GET, boundary));
  auto relative = target("oauth2/42/v1/token");
  ZuCheck(!parser.operation(Zhttp::Method::GET, relative));
}

template <typename Builder>
static BodySink signedBody(Builder &builder)
{
  ZmRef<SignedObject> object = new SignedObject{};
  object->value = "value";
  builder.init(object.ptr());
  BodySink sink;
  builder.body([&sink](auto emit) { emit(sink); });
  return sink;
}

static void signedBodyTest()
{
  ZuTestScope(signedBody);
  SignedReq request;
  auto requestBody = signedBody(request);
  ZuCheck(request.signedBody == "{\"value\":\"value\"}");
  ZuCheck(requestBody.data ==
    "{\"body\":{\"value\":\"value\"},\"signed\":true}");
  SignedRes response;
  auto responseBody = signedBody(response);
  ZuCheck(response.signedBody == "{\"value\":\"value\"}");
  ZuCheck(responseBody.data ==
    "{\"body\":{\"value\":\"value\"},\"signed\":true}");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(zeroBodyTest);
  ZuTestCall(fixedBodyTest);
  ZuTestCall(streamedBodyTest);
  ZuTestCall(borrowedBodyTest);
  ZuTestCall(borrowedFieldsTest);
  ZuTestCall(sseReceiveTest);
  ZuTestCall(endpointHandoffTest);
  ZuTestCall(dispatchBoundaryTest);
  ZuTestCall(completionClaimTest);
  ZuTestCall(exactPathTest);
  ZuTestCall(skipPathTest);
  ZuTestCall(rootPathTest);
  ZuTestCall(signedBodyTest);
  return 0;
}
