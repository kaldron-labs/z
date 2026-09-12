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
  ZuSpan<uint8_t> data;
  RawObject &operator =(ZuSpan<uint8_t> data_) {
    data = data_;
    return *this;
  }
};
struct SignedObject : public ZmObject { TestString value; };
ZfStruct(, (SignedObject, JSON),
  (((value), (Required)), (String)));

struct BodySink {
  TestString data;
  template <typename T> BodySink &operator <<(T &&value) {
    data << ZuFwd<T>(value);
    return *this;
  }
  void flush() { }
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
ZuAssert((ZuIsSame<Zrest::GetAllReqRes<OneRequest>,
  ZuTypeList<Zrest::ReqRes<RequestA, ReplyA>>>{}));
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
  ZuCheck(!parser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  ZeroReqParser requestParser;
  ZuCheck(requestParser.bodyInfo(Zhttp::BodyType::Fixed, 0));
  ZuCheck(!requestParser.bodyInfo(Zhttp::BodyType::Fixed, 1));
  ZuCheck(!requestParser.bodyInfo(Zhttp::BodyType::Streamed, 0));
  LimitedReqParser limited;
  ZuCheck(limited.bodyInfo(Zhttp::BodyType::Fixed, 4));
  ZuCheck(!limited.bodyInfo(Zhttp::BodyType::Fixed, 5));
}

static void exactRouteTest()
{
  ZuTestScope(exactRoute);
  using Routes = Zrest::MReqParser<ZuTypeList<ExactReq, ExactNoQuery>>;
  auto match = [](ZuCSpan path) {
    Routes routes;
    Zhttp::Target target;
    target.path = {
      reinterpret_cast<uint8_t *>(const_cast<char *>(path.data())),
      path.length()};
    return routes.operation(Zhttp::Method::GET, target);
  };
  ZuCheck(match("/authorize"));
  ZuCheck(match("/authorize?client_id=x"));
  ZuCheck(!match("/authorize?client_id=long"));
  ZuCheck(!match("/authorize/extra"));
  ZuCheck(match("/.well-known/oauth-authorization-server"));
  ZuCheck(!match("/.well-known/oauth-authorization-server?x=y"));
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
  ZuTestCall(exactRouteTest);
  ZuTestCall(signedBodyTest);
  return 0;
}
