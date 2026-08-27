//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmcpClient.hh>

#include <zlib/ZtEnum.hh>

using namespace ZuTestUtil;

struct SchemaBody { int value = 0; };
struct SchemaOK : public Zmcp::Response { using Body = SchemaBody; };
struct SchemaEmpty : public Zmcp::Response { enum { Status = 204 }; };

struct Nested {
  bool enabled = false;
};
ZfStruct((Nested, JSON),
  (((enabled), (Ctor<0>, Required)), (Bool)));

struct SchemaReq {
  int count = 2;
  ZtString<> label;
  Nested nested;
};
ZfStruct((SchemaReq, JSON),
  (((count), (Ctor<0>, (Range<1, 8>))), (Int32, 2)),
  (((label), (Ctor<1>, Required, MCP::Header<"Label">)), (String)),
  (((nested), (Ctor<2>, Required)), (UDT)));

struct SchemaCreated : public Zmcp::Response { using Body = Nested; };
struct SchemaNoBody : public Zmcp::Response { enum { Status = 204 }; };
struct SchemaTool : public Zmcp::Request {
  using Object = SchemaReq;
  using OperationID = ZuStringT<"schemaOperation">;
  using ToolID = ZuStringT<"schema_tool">;
  using Title = ZuStringT<"Schema tool">;
  using Description = ZuStringT<"Exercises reflected schemas">;
  using Annotations = Zmcp::ToolAnnotations<false, true, true, false>;
  using Responses = ZuTypeList<SchemaCreated, SchemaNoBody>;

  static Annotations annotations() { return {}; }
};
using SchemaCatalog = ZuTypeList<SchemaTool>;
using EmptyCatalog = ZuTypeList<>;

struct NoArgResponse : public Zmcp::Response { };
struct NoArgTool : public Zmcp::Request {
  using Object = Zmcp::EmptyObject;
  using OperationID = ZuStringT<"noArguments">;
  using ToolID = ZuStringT<"no_arguments">;
  using Responses = ZuTypeList<NoArgResponse>;
};
using NoArgCatalog = ZuTypeList<NoArgTool>;

struct SchemaMap {
  using Key = ZuCSpan;
  using Val = int;
};
inline ZfJSON::AsMap<ZfFieldTC::Int32> ZfJSON_Fmt(SchemaMap *);

struct SchemaArray : public ZtArray<int> {
  using ZtArray<int>::ZtArray;
};
inline ZfJSON::AsArray<ZfFieldTC::Int32> ZfJSON_Fmt(SchemaArray *);

ZtEnumNS(, SchemaMode, int8_t, Fast, Safe);
ZtEnumImplNS(SchemaMode);

struct NestedArray : public ZtArray<Nested> {
  using ZtArray<Nested>::ZtArray;
};
inline ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(NestedArray *);

struct SchemaAdvanced {
  ZtString<> name;
  int mode = SchemaMode::Safe;
  NestedArray objects;
  SchemaMap values;
};
ZfStruct((SchemaAdvanced, JSON),
  (((name), (Ctor<0>)), (String, "fallback")),
  (((mode), (Ctor<1>, (Enum<SchemaMode::Map>))),
    (Int32, SchemaMode::Safe)),
  (((objects), (Ctor<2>, Required)), (UDT)),
  (((values), (Ctor<3>, Required)), (UDT)));

static void responseTest()
{
  ZuTestScope(response);
  ZuCheck(SchemaOK::Status == 200);
  ZuCheck(SchemaEmpty::Status == 204);
  ZuCheck((ZuIsSame<typename SchemaOK::Body, SchemaBody>{}));
  ZuCheck((ZuIsSame<void, typename SchemaEmpty::Body>{}));
}

static void toolTest()
{
  ZuTestScope(tool);
  ZtString<> out;
  Zmcp::emitToolsList<SchemaCatalog>(out, Zmcp::Era::Modern);
  ZuCheck(Zmcp::ToolIdempotent<SchemaTool>{});
  ZuCheck(out ==
    "{\"tools\":[{\"name\":\"schema_tool\",\"title\":\"Schema tool\","
    "\"description\":\"Exercises reflected schemas\",\"annotations\":"
      "{\"readOnlyHint\":false,\"destructiveHint\":true,"
      "\"idempotentHint\":true,\"openWorldHint\":false},"
    "\"inputSchema\":"
    "{\"type\":\"object\",\"properties\":{"
      "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8,"
        "\"default\":2},"
      "\"label\":{\"type\":\"string\",\"x-mcp-header\":\"Label\"},"
      "\"nested\":{\"type\":\"object\",\"properties\":{"
        "\"enabled\":{\"type\":\"boolean\"}},"
        "\"required\":[\"enabled\"],\"additionalProperties\":false}},"
      "\"required\":[\"label\",\"nested\"],"
      "\"additionalProperties\":false},"
    "\"outputSchema\":{\"oneOf\":["
      "{\"type\":\"object\",\"properties\":{\"code\":{\"const\":200},"
        "\"data\":{\"type\":\"object\",\"properties\":{"
          "\"enabled\":{\"type\":\"boolean\"}},"
          "\"required\":[\"enabled\"],\"additionalProperties\":false}},"
        "\"required\":[\"code\",\"data\"],\"additionalProperties\":false},"
      "{\"type\":\"object\",\"properties\":{\"code\":{\"const\":204}},"
        "\"required\":[\"code\"],\"additionalProperties\":false}]},"
    "\"_meta\":{\"operationId\":\"schemaOperation\"}}],"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\","
    "\"cacheScope\":\"public\",\"ttlMs\":86400000}");

  out.length_(0);
  Zmcp::emitToolsList<EmptyCatalog>(out, Zmcp::Era::Modern);
  ZuCheck(out ==
    "{\"tools\":[],"
    "\"_meta\":{\"io.modelcontextprotocol/serverInfo\":{"
      "\"name\":\"zmcp\",\"version\":\"" Z_VERNAME "\"}},"
    "\"resultType\":\"complete\","
    "\"cacheScope\":\"public\",\"ttlMs\":86400000}");

  out.length_(0);
  Zmcp::emitToolsList<NoArgCatalog>(out, Zmcp::Era::Modern);
  ZuCheck(out.find(
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{},"
    "\"additionalProperties\":false}") >= 0);
}

static void containerTest()
{
  ZuTestScope(container);
  ZuCheck(Zmcp::schema<SchemaMap>() ==
    "{\"type\":\"object\",\"additionalProperties\":{"
    "\"type\":\"integer\"}}");
  ZuCheck(Zmcp::schema<SchemaArray>() ==
    "{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}");
}

static void advancedTest()
{
  ZuTestScope(advanced);
  ZuCheck(Zmcp::schema<SchemaAdvanced>() ==
    "{\"type\":\"object\",\"properties\":{"
    "\"name\":{\"type\":\"string\",\"default\":\"fallback\"},"
    "\"mode\":{\"type\":\"string\",\"enum\":[\"Fast\",\"Safe\"],"
      "\"default\":\"Safe\"},"
    "\"objects\":{\"type\":\"array\",\"items\":{"
      "\"type\":\"object\",\"properties\":{"
        "\"enabled\":{\"type\":\"boolean\"}},"
      "\"required\":[\"enabled\"],\"additionalProperties\":false}},"
    "\"values\":{\"type\":\"object\",\"additionalProperties\":{"
      "\"type\":\"integer\"}}},"
    "\"required\":[\"objects\",\"values\"],"
      "\"additionalProperties\":false}");
}

static void schemaTest()
{
  ZuTestScope(schema);
  auto out = Zmcp::schema<SchemaReq>();
  ZuCheck(out ==
    "{\"type\":\"object\",\"properties\":{"
      "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8,"
        "\"default\":2},"
      "\"label\":{\"type\":\"string\",\"x-mcp-header\":\"Label\"},"
      "\"nested\":{\"type\":\"object\",\"properties\":{"
        "\"enabled\":{\"type\":\"boolean\"}},"
        "\"required\":[\"enabled\"],\"additionalProperties\":false}},"
      "\"required\":[\"label\",\"nested\"],"
      "\"additionalProperties\":false}");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(responseTest);
  ZuTestCall(schemaTest);
  ZuTestCall(toolTest);
  ZuTestCall(containerTest);
  ZuTestCall(advancedTest);
  return 0;
}
