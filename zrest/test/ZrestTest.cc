//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>

using namespace ZuTestUtil;

struct TestObject : public ZmObject { };

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

ZuAssert((ZuIsSame<Zrest::GetAllHdrKeys<OneRequest>, OneHeader>{}));
ZuAssert((ZuIsSame<Zrest::GetAllHdrKeys<TwoRequests>, TwoHeaders>{}));
ZuAssert((ZuIsSame<Zrest::GetAllResponses<OneRequest>,
  ZuTypeList<ReplyA>>{}));
ZuAssert((ZuIsSame<Zrest::GetAllResponses<TwoRequests>,
  ZuTypeList<ReplyA, ReplyB>>{}));
ZuAssert((ZuIsSame<Zrest::GetAllReqRes<OneRequest>,
  ZuTypeList<Zrest::ReqRes<RequestA, ReplyA>>>{}));

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
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(zeroBodyTest);
  return 0;
}
