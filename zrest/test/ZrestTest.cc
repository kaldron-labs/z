//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

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
  (((value), (Required)),	String));

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
ZuAssert((ZuIsSame<Zrest::GetKValues<ContentType, MultiHeaders>,
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
  ZuCheck(limited.bodyInfo(Zhttp::BodyType::Fixed, 4));
  ZuCheck(!limited.bodyInfo(Zhttp::BodyType::Fixed, 5));
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
  (((appID), (URI::PathIndex<0>, Required)),	UInt64));

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

static void rootPathTest()
{
  ZuTestScope(rootPath);
  RootServer server;
  RootParser parser;
  parser.init(server);

  auto app = target("/oauth2/42/v1/token?grant_type=refresh_token");
  ZuCheck(parser.operation(Zhttp::Method::GET, app));
  ZuCheck(app.path ==
    "/oauth2/42/v1/token?grant_type=refresh_token");
  auto &appParser = parser.u.template p<AppParser>();
  ZuCheck(appParser.appID == 42);
  auto &request = appParser.u.template p<PrefixedReq>();
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
  ZuTestCall(dispatchBoundaryTest);
  ZuTestCall(exactPathTest);
  ZuTestCall(skipPathTest);
  ZuTestCall(rootPathTest);
  ZuTestCall(signedBodyTest);
  return 0;
}
