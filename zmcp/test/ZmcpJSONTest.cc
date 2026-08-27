//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zmcp.hh>

using namespace ZuTestUtil;

struct Params { int value = 0; };
ZfStruct((Params, JSON), (((value), (Ctor<0>)), (Int32)));
struct JSONOK : public Zmcp::Response {
  using Body = Params;
};
struct JSONEmpty : public Zmcp::Response { enum { Status = 204 }; };
struct JSONReq {
  using Responses = ZuTypeList<JSONOK, JSONEmpty>;
};

static void idTest()
{
  ZuTestScope(id);
  Zmcp::ID id;
  ZuCheck(id.absent());
  id = int64_t{42};
  ZuCheck(id.integer());
  ZuCheck(id.p<int64_t>() == 42);
  id = Zmcp::IDString{"call-42"};
  ZuCheck(id.string());
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
  ZuCheck(parsed.envelope.id().integer());
  ZuCheck(parsed.envelope.id().p<int64_t>() == 42);
  ZuCheck(parsed.envelope.method() == "tools/list");
  ZuCheck(Zmcp::raw(parsed.envelope.params()));

  char notification[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}";
  auto notified = Zmcp::parse<ZuTypeList<>>(
    notification, sizeof(notification));
  ZuCheck(notified.envelope.kind == Zmcp::MessageKind::Notification);

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
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(idTest);
  ZuTestCall(envelopeTest);
  ZuTestCall(saveTest);
  ZuTestCall(replyTest);
  return 0;
}
