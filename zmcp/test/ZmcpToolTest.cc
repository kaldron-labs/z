//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuPtr.hh>

#include <zlib/ZmcpClient.hh>
#include <zlib/ZmcpServer.hh>

using namespace ZuTestUtil;

struct AddReq { int lhs = 0; int rhs = 0; };
ZfStruct(, (AddReq, JSON),
  (((lhs), (Ctor<0>, Required)),	Int32),
  (((rhs), (Ctor<1>, Required)),	Int32));
struct AddResult { int value = 0; };
ZfStruct(, (AddResult, JSON),
  (((value), (Ctor<0>, Required)),	Int32));
struct AddOK : public Zmcp::Response {
  using Body = AddResult;
};
struct AddInvalid : public Zmcp::Response { enum { Status = 400 }; };
struct Add : public Zmcp::Request {
  using Object = AddReq;
  using OperationID = ZuStringT<"addNumbers">;
  using ToolID = ZuStringT<"add">;
  using Annotations = Zmcp::ToolAnnotations<false, true, true>;
  using Responses = ZuTypeList<AddOK, AddInvalid>;

  static Annotations annotations() { return {}; }
};

struct MulReq { int lhs = 0; int rhs = 0; };
ZfStruct(, (MulReq, JSON),
  (((lhs), (Ctor<0>, Required)),	Int32),
  (((rhs), (Ctor<1>, Required)),	Int32));
struct Mul : public Zmcp::Request {
  using Object = MulReq;
  using OperationID = ZuStringT<"multiplyNumbers">;
  using ToolID = ZuStringT<"multiply">;
  using Responses = ZuTypeList<AddOK, AddInvalid>;
};

struct AddAgain : public Zmcp::Request {
  using Object = AddReq;
  using OperationID = ZuStringT<"addAgain">;
  using ToolID = ZuStringT<"add_again">;
  using Responses = ZuTypeList<AddOK, AddInvalid>;
};

using Catalog = ZuTypeList<Add, Mul>;
using EmptyCatalog = ZuTypeList<>;
using SingleCatalog = ZuTypeList<Add>;
using ReusedCatalog = ZuTypeList<Add, AddAgain>;

static void catalogTest()
{
  ZuTestScope(catalog);
  ZuCheck(Catalog::N == 2);
  ZuCheck(Zmcp::validToolID<Add::ToolID>());
  ZuCheck(Zmcp::validToolID<ZuStringT<"tool name">>());
  ZuCheck(Add::ToolID{}() == "add");
  ZuCheck(Zmcp::ToolIdempotent<Add>{});
  ZuCheck(!Zmcp::ToolIdempotent<Mul>{});
}

static void idempotentTest()
{
  ZuTestScope(idempotent);
  Zmcp::ToolCallRequestMessage<Catalog> message;
  message.params.arguments() = Zmcp::ToolArg<Add>{AddReq{2, 3}};
  ZuCheck(message.idempotent());
  message.params.arguments() = Zmcp::ToolArg<Mul>{MulReq{2, 3}};
  ZuCheck(!message.idempotent());
}

static void callParamsTest()
{
  ZuTestScope(callParams);
  char json[] =
    "{\"name\":\"add\",\"arguments\":{\"lhs\":2,\"rhs\":3}}";
  auto scanned = ZfJSON::scan(json);
  ZuCheck(scanned.p<0>() > 0);
  auto &nodes = *scanned.p<1>();
  using Params = Zmcp::ToolsCallParams<Catalog>;
  ZuCheck((ZuIsSame<ZfJSON::As<Params>, ZfJSON::AsDeflt>{}));
  auto handler = ZfJSON::handler<Params>(nodes[0]);
  ZuCheck(handler.valid);
  ZuPtr<Params> allocated = handler.alloc();
  ZuCheck(allocated->name() == "add");
  ZuCheck((allocated->arguments().is<const ZfJSON::AnyNode *>()));
  auto params = handler.ctor();
  ZuCheck(params.name() == "add");
  ZuCheck(params.name().data() >= json &&
      params.name().data() < json + sizeof(json));
  ZuCheck((params.arguments().is<const ZfJSON::AnyNode *>()));
  ZuCheck(params.narrow() == 0);
  ZuCheck(params.active() == 0);
  const auto &add = params.arguments().p<Zmcp::ToolArg<Add>>().value;
  ZuCheck(add.lhs == 2);
  ZuCheck(add.rhs == 3);
  ZtString<> out;
  ZfJSON::save(out, params);
  ZuCheck(out ==
    "{\"name\":\"add\",\"arguments\":{\"lhs\":2,\"rhs\":3}}");

  params.name("not-the-transmit-name");
  out.length_(0);
  ZfJSON::save(out, params);
  ZuCheck(out ==
    "{\"name\":\"add\",\"arguments\":{\"lhs\":2,\"rhs\":3}}");

  Params multiply;
  multiply.arguments() = Zmcp::ToolArg<Mul>{MulReq{4, 5}};
  out.length_(0);
  ZfJSON::save(out, multiply);
  ZuCheck(out ==
    "{\"name\":\"multiply\",\"arguments\":{\"lhs\":4,\"rhs\":5}}");
}

static void customAllocTest()
{
  ZuTestScope(customAlloc);

  {
    char json[] = "request-id";
    auto node = ZfJSON::newNode<ZfJSON::AnyNode::String>(json);
    auto handler = ZfJSON::handler<Zmcp::ID>(node.ptr());
    ZuCheck(handler.valid);
    ZuPtr<Zmcp::ID> id = handler.alloc();
    ZuCheck(id->string());
    ZuCheck(id->p<Zmcp::IDString>() == "request-id");
  }
  {
    char json[] = "{\"lhs\":10,\"rhs\":11}";
    auto scanned = ZfJSON::scan(json);
    ZuCheck(scanned.p<0>() > 0);
    auto handler = ZfJSON::handler<Zmcp::ToolArg<Add>>(
      (*scanned.p<1>())[0]);
    ZuCheck(handler.valid);
    ZuPtr<Zmcp::ToolArg<Add>> arg = handler.alloc();
    ZuCheck(arg->value.lhs == 10);
    ZuCheck(arg->value.rhs == 11);
  }
}

static void discriminatorTest()
{
  ZuTestScope(discriminator);
  char emptyJSON[] =
    "{\"name\":\"anything\",\"arguments\":{}}";
  auto scanned = ZfJSON::scan(emptyJSON);
  ZuCheck(scanned.p<0>() > 0);
  auto emptyHandler = ZfJSON::handler<Zmcp::ToolsCallParams<EmptyCatalog>>(
    (*scanned.p<1>())[0]);
  ZuCheck(emptyHandler.valid);
  auto empty = emptyHandler.ctor();
  ZuCheck(empty.match() == -1);
  ZuCheck(empty.narrow() == -1);
  ZuCheck(empty.active() == -1);

  char singleJSON[] =
    "{\"name\":\"add\",\"arguments\":{\"lhs\":6,\"rhs\":7}}";
  scanned = ZfJSON::scan(singleJSON);
  ZuCheck(scanned.p<0>() > 0);
  auto singleHandler = ZfJSON::handler<Zmcp::ToolsCallParams<SingleCatalog>>(
    (*scanned.p<1>())[0]);
  ZuCheck(singleHandler.valid);
  auto single = singleHandler.ctor();
  ZuCheck(single.match() == 0);
  ZuCheck(single.narrow() == 0);
  ZuCheck(single.active() == 0);

  char reusedJSON[] =
    "{\"name\":\"add_again\","
    "\"arguments\":{\"lhs\":8,\"rhs\":9}}";
  scanned = ZfJSON::scan(reusedJSON);
  ZuCheck(scanned.p<0>() > 0);
  auto reusedHandler = ZfJSON::handler<Zmcp::ToolsCallParams<ReusedCatalog>>(
    (*scanned.p<1>())[0]);
  ZuCheck(reusedHandler.valid);
  auto reused = reusedHandler.ctor();
  ZuCheck(reused.match() == 1);
  ZuCheck(reused.narrow() == 1);
  ZuCheck(reused.active() == 1);
  const auto &again =
    reused.arguments().p<Zmcp::ToolArg<AddAgain>>().value;
  ZuCheck(again.lhs == 8 && again.rhs == 9);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(catalogTest);
  ZuTestCall(idempotentTest);
  ZuTestCall(callParamsTest);
  ZuTestCall(customAllocTest);
  ZuTestCall(discriminatorTest);
  return 0;
}
