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
  Zmcp::ReplyBodies<JSONReq>, ZuTypeList<Params>>{}));
ZuAssert((ZuIsSame<
  Zmcp::ReplyBodies<JSONEmptyReq>, ZuTypeList<>>{}));

static void idTest()
{
  ZuTestScope(id);
  Zmcp::ID id;
  ZuCheck(id.is<void>());
  id = Zmcp::Null{};
  ZuCheck(id.is<Zmcp::Null>());
  ZuCheck(id == Zmcp::ID{Zmcp::Null{}});
  ZuCheck(id.hash() == Zmcp::ID{Zmcp::Null{}}.hash());
  id = int64_t{42};
  ZuCheck(id.is<int64_t>());
  ZuCheck(id.p<int64_t>() == 42);
  id = Zmcp::IDString{"call-42"};
  ZuCheck(id.is<Zmcp::IDString>());
  ZuCheck(id.p<Zmcp::IDString>() == "call-42");
}

static void envelopeTest()
{
  ZuTestScope(envelope);
  char request[] =
    "{\"extra\":true,\"jsonrpc\":\"2.0\",\"id\":42,"
    "\"method\":\"tools/list\",\"params\":{}}";
  auto parsed = Zmcp::parse<ZuTypeList<>>(request, sizeof(request));
  ZuCheck(bool(parsed));
  ZuCheck(!parsed.close());
  ZuCheck(parsed.envelope.kind == Zmcp::MessageKind::Request);
  ZuCheck(parsed.envelope.id().is<int64_t>());
  ZuCheck(parsed.envelope.id().p<int64_t>() == 42);
  ZuCheck(parsed.envelope.method() == "tools/list");
  ZuCheck(Zmcp::raw(parsed.envelope.params()));

  char notification[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}";
  auto notified = Zmcp::parse<ZuTypeList<>>(
    notification, sizeof(notification));
  ZuCheck(notified.envelope.kind == Zmcp::MessageKind::Notification);

  char nullID[] =
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"tools/list\"}";
  auto nullRequest = Zmcp::parse<ZuTypeList<>>(nullID, sizeof(nullID));
  ZuCheck(nullRequest.envelope.kind == Zmcp::MessageKind::Request);
  ZuCheck(nullRequest.envelope.id().is<Zmcp::Null>());

  char corrupt[] = "{\"jsonrpc\":";
  auto rejected = Zmcp::parse<ZuTypeList<>>(corrupt, sizeof(corrupt));
  ZuCheck(!rejected);
  ZuCheck(rejected.close());
  ZuCheck(!rejected.root);
}

static void saveTest()
{
  ZuTestScope(save);
  ZtString<> out;
  Zmcp::saveRequest(out, Zmcp::ID{int64_t{7}}, "echo", Params{3});
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"echo\","
    "\"params\":{\"value\":3}}");
  out.length_(0);
  Zmcp::saveError(out, Zmcp::ID{Zmcp::IDString{"x"}},
    Zmcp::ErrorCode::MethodNotFound, "Method not found");
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"error\":{"
    "\"code\":-32601,\"message\":\"Method not found\"}}");
  out.length_(0);
  Zmcp::saveError(out, Zmcp::ID{Zmcp::Null{}},
    Zmcp::ErrorCode::MethodNotFound, "Method not found");
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{"
    "\"code\":-32601,\"message\":\"Method not found\"}}");
}

static void replyTest()
{
  ZuTestScope(reply);
  char input[] =
    "{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":200,\"data\":{\"value\":11}},"
    "\"isError\":false}}";
  auto parsed = Zmcp::parse<ZuTypeList<>>(input, sizeof(input));
  ZuCheck(parsed.envelope.kind == Zmcp::MessageKind::Result);
  auto reply = Zmcp::loadToolReply<JSONReq>(
    Zmcp::raw(parsed.envelope.result()));
  ZuCheck((reply.is<Zmcp::ToolReply<JSONOK>>()));
  ZuCheck(reply.p<Zmcp::ToolReply<JSONOK>>().body.value == 11);

  char arrayInput[] =
    "{\"jsonrpc\":\"2.0\",\"id\":10,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":201,\"data\":[3,5,8]},"
    "\"isError\":false}}";
  auto arrayParsed = Zmcp::parse<ZuTypeList<>>(
    arrayInput, sizeof(arrayInput));
  auto arrayReply = Zmcp::loadToolReply<JSONArrayReq>(
    Zmcp::raw(arrayParsed.envelope.result()));
  ZuCheck((arrayReply.is<Zmcp::ToolReply<JSONArrayOK>>()));
  const auto &body = arrayReply.p<Zmcp::ToolReply<JSONArrayOK>>().body;
  ZuCheck(body.length() == 3);
  ZuCheck(body[0] == 3 && body[1] == 5 && body[2] == 8);

  char emptyInput[] =
    "{\"jsonrpc\":\"2.0\",\"id\":11,\"result\":{\"content\":[],"
    "\"structuredContent\":{\"code\":204},\"isError\":false}}";
  auto emptyParsed = Zmcp::parse<ZuTypeList<>>(
    emptyInput, sizeof(emptyInput));
  auto emptyReply = Zmcp::loadToolReply<JSONEmptyReq>(
    Zmcp::raw(emptyParsed.envelope.result()));
  ZuCheck((emptyReply.is<Zmcp::ToolReply<JSONEmpty>>()));
}

static void loggingDataTest()
{
  ZuTestScope(loggingData);

  char input[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":[1,{\"x\":2}]}}";
  auto parsed = Zmcp::parse<ZuTypeList<>>(input, sizeof(input));
  auto params = Zmcp::raw(parsed.envelope.params());
  auto data = Zmcp::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Array>());

  char objectInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":{\"x\":2}}}";
  auto objectParsed = Zmcp::parse<ZuTypeList<>>(
    objectInput, sizeof(objectInput));
  params = Zmcp::raw(objectParsed.envelope.params());
  data = Zmcp::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Object>());

  char scalarInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":42}}";
  auto scalarParsed = Zmcp::parse<ZuTypeList<>>(
    scalarInput, sizeof(scalarInput));
  params = Zmcp::raw(scalarParsed.envelope.params());
  data = Zmcp::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Number>());

  char nullInput[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\","
    "\"params\":{\"level\":\"info\",\"data\":null}}";
  auto nullParsed = Zmcp::parse<ZuTypeList<>>(
    nullInput, sizeof(nullInput));
  params = Zmcp::raw(nullParsed.envelope.params());
  data = Zmcp::member(params, "data");
  ZuCheck(data && data->has<ZfJSON::AnyNode::Null>());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(idTest);
  ZuTestCall(envelopeTest);
  ZuTestCall(saveTest);
  ZuTestCall(replyTest);
  ZuTestCall(loggingDataTest);
  return 0;
}
