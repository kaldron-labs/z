//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zmcp.hh>

using namespace ZuTestUtil;

struct Params { int value = 0; };
ZfStruct(, (Params, JSON), (value, (Ctor<0>), Int32));
struct JSONOK : public Zmcp::Response {
  using Body = Params;
};
struct JSONEmpty : public Zmcp::Response { enum { Status = 204 }; };
struct JSONReq {
  using Responses = ZuTypeList<JSONOK, JSONEmpty>;
};
struct JSONArrayBody : public ZtArray<int> {
  ZuDerive_(JSONArrayBody, ZtArray<int>)
  friend ZfJSON::AsArray<ZfFieldTC::Int32> ZfJSON_Fmt(JSONArrayBody *);
};
struct JSONArrayOK : public Zmcp::Response {
  using Body = JSONArrayBody;
  enum { Status = 201 };
};
struct JSONArrayReq {
  using Responses = ZuTypeList<JSONArrayOK>;
};
struct JSONEmptyReq {
  using Responses = ZuTypeList<JSONEmpty>;
};

ZuAssert((ZuIsSame<
  Zjrpc::ReplyBodies<JSONReq>, ZuTypeList<Params>>{}));
ZuAssert((ZuIsSame<
  Zjrpc::ReplyBodies<JSONEmptyReq>, ZuTypeList<>>{}));

static void replyTest()
{
  ZuTestScope(reply);
  char input[] =
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":11}},"
    "\"isError\":false}}";
  auto parsed = Zjrpc::parse(input, sizeof(input));
  ZuCheck(parsed.envelope.kind == Zjrpc::MessageKind::Result);
  auto reply = Zmcp::loadToolReply<JSONReq>(
    Zjrpc::raw(parsed.envelope.result()));
  ZuCheck((reply.is<Zjrpc::Reply<JSONOK>>()));
  ZuCheck(reply.p<Zjrpc::Reply<JSONOK>>().body.value == 11);

  char arrayInput[] =
    "{\"jsonrpc\":\"2.0\",\"id\":10,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":201,\"data\":[3,5,8]},"
    "\"isError\":false}}";
  auto arrayParsed = Zjrpc::parse(
    arrayInput, sizeof(arrayInput));
  auto arrayReply = Zmcp::loadToolReply<JSONArrayReq>(
    Zjrpc::raw(arrayParsed.envelope.result()));
  ZuCheck((arrayReply.is<Zjrpc::Reply<JSONArrayOK>>()));
  const auto &body = arrayReply.p<Zjrpc::Reply<JSONArrayOK>>().body;
  ZuCheck(body.length() == 3);
  ZuCheck(body[0] == 3 && body[1] == 5 && body[2] == 8);

  char emptyInput[] =
    "{\"jsonrpc\":\"2.0\",\"id\":11,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":204},\"isError\":false}}";
  auto emptyParsed = Zjrpc::parse(
    emptyInput, sizeof(emptyInput));
  auto emptyReply = Zmcp::loadToolReply<JSONEmptyReq>(
    Zjrpc::raw(emptyParsed.envelope.result()));
  ZuCheck((emptyReply.is<Zjrpc::Reply<JSONEmpty>>()));
}

static void loggingDataTest()
{
  ZuTestScope(loggingData);

  char input[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":[1,{\"x\":2}]}}";
  auto parsed = Zjrpc::parse(input, sizeof(input));
  auto params = Zjrpc::raw(parsed.envelope.params());
  auto data = Zjrpc::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Array>());

  char objectInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":{\"x\":2}}}";
  auto objectParsed = Zjrpc::parse(
    objectInput, sizeof(objectInput));
  params = Zjrpc::raw(objectParsed.envelope.params());
  data = Zjrpc::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Object>());

  char scalarInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":42}}";
  auto scalarParsed = Zjrpc::parse(
    scalarInput, sizeof(scalarInput));
  params = Zjrpc::raw(scalarParsed.envelope.params());
  data = Zjrpc::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Number>());

  char nullInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":null}}";
  auto nullParsed = Zjrpc::parse(
    nullInput, sizeof(nullInput));
  params = Zjrpc::raw(nullParsed.envelope.params());
  data = Zjrpc::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Null>());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(replyTest);
  ZuTestCall(loggingDataTest);
  return 0;
}
