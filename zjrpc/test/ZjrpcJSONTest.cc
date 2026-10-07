//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zjrpc.hh>

using namespace ZuTestUtil;

struct Params { int value = 0; };
ZfStruct(, (Params, JSON), (value, (Ctor<0>), Int32));

static void idTest()
{
  ZuTestScope(id);
  Zjrpc::ID id;
  ZuCheck(id.is<void>());
  id = Zjrpc::Null{};
  ZuCheck(id.is<Zjrpc::Null>());
  ZuCheck(id == Zjrpc::ID{Zjrpc::Null{}});
  ZuCheck(id.hash() == Zjrpc::ID{Zjrpc::Null{}}.hash());
  id = int64_t{42};
  ZuCheck(id.is<int64_t>());
  ZuCheck(id.p<int64_t>() == 42);
  id = Zjrpc::IDString{"call-42"};
  ZuCheck(id.is<Zjrpc::IDString>());
  ZuCheck(id.p<Zjrpc::IDString>() == "call-42");
}

static void envelopeTest()
{
  ZuTestScope(envelope);
  char request[] =
    "{\"extra\":true,\"jsonrpc\":\"2.0\",\"id\":42,"
    "\"method\":\"tools/list\",\"params\":{}}";
  auto parsed = Zjrpc::parse(request, sizeof(request));
  ZuCheck(bool(parsed));
  ZuCheck(!parsed.close());
  ZuCheck(parsed.envelope.kind == Zjrpc::MessageKind::Request);
  ZuCheck(parsed.envelope.id().is<int64_t>());
  ZuCheck(parsed.envelope.id().p<int64_t>() == 42);
  ZuCheck(parsed.envelope.method() == "tools/list");
  ZuCheck(Zjrpc::raw(parsed.envelope.params()));

  char notification[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}";
  auto notified = Zjrpc::parse(
    notification, sizeof(notification));
  ZuCheck(notified.envelope.kind == Zjrpc::MessageKind::Notification);

  char nullID[] =
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"tools/list\"}";
  auto nullRequest = Zjrpc::parse(nullID, sizeof(nullID));
  ZuCheck(nullRequest.envelope.kind == Zjrpc::MessageKind::Request);
  ZuCheck(nullRequest.envelope.id().is<Zjrpc::Null>());

  char corrupt[] = "{\"jsonrpc\":";
  auto rejected = Zjrpc::parse(corrupt, sizeof(corrupt));
  ZuCheck(!rejected);
  ZuCheck(rejected.close());
  ZuCheck(!rejected.root);
}

static void saveTest()
{
  ZuTestScope(save);
  ZtString<> out;
  Zjrpc::saveRequest(out, Zjrpc::ID{int64_t{7}}, "echo", Params{3});
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"echo\","
    "\"params\":{\"value\":3}}");
  out.length_(0);
  Zjrpc::saveError(out, Zjrpc::ID{Zjrpc::IDString{"x"}},
    Zjrpc::ErrorCode::MethodNotFound, "Method not found");
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"error\":{"
    "\"code\":-32601,\"message\":\"Method not found\"}}");
  out.length_(0);
  Zjrpc::saveError(out, Zjrpc::ID{Zjrpc::Null{}},
    Zjrpc::ErrorCode::MethodNotFound, "Method not found");
  ZuCheck(out ==
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{"
    "\"code\":-32601,\"message\":\"Method not found\"}}");
}

static void numericTest()
{
  ZuTestScopeRT(numeric);
  for (auto value : {"-9223372036854775808", "9223372036854775807", "0"}) {
    ZtString<> input;
    input << "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"id\":" << value << '}';
    auto parsed = Zjrpc::parse(input, input.length());
    ZuCheckRT(bool(parsed));
    ZtString<> output;
    ZfJSON::save(output, parsed.envelope.id());
    ZuCheckRT(output == value);
  }
  for (auto value : {"9223372036854775808", "-9223372036854775809",
      "1.5", "1.0", "1e2", "1E+2", "true", "[]", "{}"}) {
    ZtString<> input;
    input << "{\"jsonrpc\":\"2.0\",\"method\":\"echo\",\"id\":" << value << '}';
    ZuCheckRT(!Zjrpc::parse(input, input.length()));
  }
}

static void arbitraryTest()
{
  ZuTestScope(arbitrary);
  char result[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[1,null,\"x\"]}";
  auto parsed = Zjrpc::parse(result, sizeof(result));
  ZuCheck(parsed.envelope.kind == Zjrpc::MessageKind::Result);
  ZtString<> out;
  Zjrpc::saveResult(out, parsed.envelope.id(), parsed.envelope.result());
  ZuCheck(out == "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[1,null,\"x\"]}");
  char error[] = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32001,"
    "\"message\":\"failed\",\"data\":[1,2]}}";
  auto failed = Zjrpc::parse(error, sizeof(error));
  ZuCheck(failed.envelope.kind == Zjrpc::MessageKind::Error);
  auto value = Zjrpc::loadError(Zjrpc::raw(failed.envelope.error()));
  ZuCheck(value.code == -32001);
  ZuCheck(Zjrpc::raw(value.data)->has<ZfJSON::AnyNode::Array>());
}

struct Pair {
  int lhs;
  int rhs;
};
ZfStruct(, (Pair, JSON),
  (lhs, (Ctor<0>), Int32),
  (rhs, (Ctor<1>), Int32));

static void paramsTest()
{
  ZuTestScope(params);
  char named[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"add\","
    "\"params\":{\"rhs\":5,\"lhs\":3}}";
  auto request = Zjrpc::parse(named, sizeof(named));
  int sum = 0;
  auto consume = [&sum](const Pair &pair) { sum = pair.lhs + pair.rhs; };
  ZuCheck(Zjrpc::loadParams<Pair>(Zjrpc::raw(request.envelope.params()), consume));
  ZuCheck(sum == 8);
  char positional[] = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"add\",\"params\":[7,11]}";
  auto ordered = Zjrpc::parse(positional, sizeof(positional));
  ZuCheck(Zjrpc::loadParams<Pair>(Zjrpc::raw(ordered.envelope.params()), consume));
  ZuCheck(sum == 18);
  unsigned calls = 0;
  ZuCheck(Zjrpc::loadParams<Zjrpc::EmptyObject>(nullptr,
    [&calls](const auto &) { ++calls; }));
  ZuCheck(calls == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(idTest);
  ZuTestCall(envelopeTest);
  ZuTestCall(saveTest);
  ZuTestCall(numericTest);
  ZuTestCall(arbitraryTest);
  ZuTestCall(paramsTest);
  return 0;
}
