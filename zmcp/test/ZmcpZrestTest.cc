//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZrestClient.hh>
#include <zlib/ZrestServer.hh>
#include <zlib/ZmcpClient.hh>

using namespace ZuTestUtil;

using Authorization = ZuStringT<"authorization">;
using AppHeaders = ZhttpHeaders("authorization");

struct SharedReq : public ZmObject {
  int lhs = 0;
  int rhs = 0;
};

struct SharedResult : public ZmObject {
  int value = 0;
};

ZfStruct((SharedReq, JSON),
  (((lhs), (Mutable)), (Int32)),
  (((rhs), (Mutable)), (Int32)));
ZfStruct((SharedResult, JSON),
  (((value), (Mutable)), (Int32)));

template <int Status_>
struct SharedResponse : public Zmcp::Response {
  using Body = SharedResult;
  enum { Status = Status_ };
};

using SharedOK = SharedResponse<200>;
using SharedDenied = SharedResponse<403>;

struct SharedOutcome {
  int value = 0;
  int code = 0;
};

struct SharedHandler {
  template <typename Headers>
  SharedOutcome operator ()(const SharedReq &request,
      const Headers &headers) const {
    auto authorization = headers.template get<Authorization>();
    if (authorization.count != 1 ||
	authorization.value != "Bearer test")
      return {0, SharedDenied::Status};
    return {request.lhs + request.rhs, SharedOK::Status};
  }
};

struct AddOperation : public Zmcp::Request {
  using Object = SharedReq;
  using Responses = ZuTypeList<SharedOK, SharedDenied>;
  using OperationID = ZuStringT<"addNumbers">;
  using ToolID = ZuStringT<"add">;
  using Headers = AppHeaders;
};

template <typename Response>
struct RESTResponse :
    public Zrest::ResBuilder<RESTResponse<Response>, typename Response::Body> {
  enum {
    Status = Response::Status,
    Body = Zrest::BodyPolicy::JSON
  };
};

using RESTOK = RESTResponse<SharedOK>;
using RESTDenied = RESTResponse<SharedDenied>;

struct RESTAdd : public Zrest::ReqParser<RESTAdd, SharedReq> {
  using Path = ZuStringT<"/add">;
  using Headers = AppHeaders;
  using Responses = ZuTypeList<RESTOK, RESTDenied>;
  enum {
    Method = Zhttp::Method::POST,
    Body = Zrest::BodyPolicy::JSON
  };

  template <typename Key, typename Value>
  void header(Zhttp::FieldSection::T) {
    if constexpr (ZuIsSame<Key, Authorization>{}) {
      m_authorization = Value{}();
      ++m_authorizationCount;
    }
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (ZuIsSame<Key, Authorization>{}) {
      m_authorization = value;
      ++m_authorizationCount;
    }
  }
  void header(Zhttp::FieldSection::T, ZuBSpan, ZuSpan<uint8_t>) { }

  template <typename Key>
  Zmcp::HTTPHeader get() const {
    if constexpr (ZuIsSame<Key, Authorization>{})
      return {m_authorization, m_authorizationCount};
    else
      return {};
  }

private:
  ZtString<> m_authorization;
  unsigned m_authorizationCount = 0;
};

using Catalog = ZuTypeList<AddOperation>;

struct BodyRx {
  ZuSpan<uint8_t> body;

  template <typename Scan, typename Consume>
  int64_t consume(Scan &&scan, Consume &&consume) {
    int64_t n = scan(body);
    if (n > 0)
      consume(ZuSpan<uint8_t>{body.data(), static_cast<unsigned>(n)});
    return n;
  }
};

static SharedOutcome restCall(ZuCSpan authorization)
{
  RESTAdd request;
  request.init();
  Zhttp::URLString targetText{"/add"};
  Zhttp::Target target;
  auto error = Zhttp::Target::parseH1(
    target, Zhttp::Method::POST, targetText.span());
  if (!error.ok() || !request.operation(Zhttp::Method::POST, target))
    return {};
  ZtString<> header{authorization};
  request.template header<Authorization>(
    Zhttp::FieldSection::Final, header.span());
  uint8_t json[] = "{\"lhs\":20,\"rhs\":22}";
  constexpr unsigned jsonLength = sizeof(json) - 1;
  if (!request.bodyInfo(Zhttp::BodyType::Fixed, jsonLength)) return {};
  BodyRx rx{{json, jsonLength}};
  if (!request.body(rx)) return {};
  return SharedHandler{}(*request.object, request);
}

static SharedOutcome mcpCall(ZuCSpan authorization)
{
  Zmcp::Peer<Catalog> peer;
  ZtString<> ignored;
  char discover[] =
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\"}";
  auto noop = [](auto *, const auto &, auto) { };
  if (!peer.receive(discover, ignored, noop)) return {};

  Zmcp::HTTPHeaders<AppHeaders> headers;
  headers.template put<Authorization>(authorization);
  SharedOutcome outcome;
  auto handler = [&headers, &outcome](auto *, const SharedReq &request,
	auto complete) {
    outcome = SharedHandler{}(request, headers);
    ZmRef<SharedResult> result = new SharedResult();
    result->value = outcome.value;
    switch (outcome.code) {
      case SharedOK::Status:
	complete(Zmcp::ToolReply<SharedOK>{ZuMv(result)});
	break;
      default:
	complete(Zmcp::ToolReply<SharedDenied>{ZuMv(result)});
	break;
    }
  };
  char call[] =
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
    "\"params\":{\"name\":\"add\",\"arguments\":{\"lhs\":20,\"rhs\":22}}}";
  if (!peer.receive(call, ignored, handler)) return {};
  return outcome;
}

static void contractTest()
{
  ZuTestScope(contract);
  ZuCheck((ZuIsSame<typename AddOperation::Object, SharedReq>{}));
  ZuCheck(Catalog::N == 1);
  ZuCheck((ZuTypeIn<Authorization,
    Zrest::GetTypeHdrKeys<RESTAdd>>{}));
  ZuCheck((ZuTypeIn<Authorization, AddOperation::Headers>{}));

  auto rest = restCall("Bearer test");
  auto mcp = mcpCall("Bearer test");
  ZuCheck(rest.code == SharedOK::Status && mcp.code == rest.code);
  ZuCheck(rest.value == 42 && mcp.value == rest.value);

  rest = restCall("Bearer denied");
  mcp = mcpCall("Bearer denied");
  ZuCheck(rest.code == SharedDenied::Status && mcp.code == rest.code);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(contractTest);
  return 0;
}
