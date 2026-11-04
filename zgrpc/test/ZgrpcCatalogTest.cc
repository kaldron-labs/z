//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zgrpc.hh>

using namespace ZuTestUtil;

struct Item { unsigned id; };
ZfStruct(, (Item, PB), (id, (Ctor<0>, PB::FieldID<1>), UInt32));
Zpb::AsMessage<"test.Item"> Zpb_Fmt(Item *);

struct Lookup : Zgrpc::Method {
  using Service = ZuStringT<"example.Store">;
  using Name = ZuStringT<"Lookup">;
  using Request = Item;
  using Response = Item;
  using ReqHeaders = ZuTypeList<ZuStringT<"x-tag">, ZuTypeList<ZuStringT<"one">>>;
};
struct Watch : Lookup {
  using Name = ZuStringT<"Watch">;
  enum { Kind = Zgrpc::Kind::ServerStream };
  using ReqHeaders = ZuTypeList<ZuStringT<"x-tag">, ZuTypeList<ZuStringT<"two">>>;
};
using Methods = ZuTypeList<Lookup, Watch>;
ZgrpcCatalogDerive(Store, Methods);
ZgrpcCatalogImpl(Store);

struct BadName : Lookup { using Name = ZuStringT<"bad/name">; };
struct BadService : Lookup { using Service = ZuStringT<"a..b">; };
struct BadKind : Lookup { enum { Kind = Zgrpc::Kind::N }; };
struct NoPB : Lookup { using Request = int; };
struct Dup : Lookup { };
ZuStructFacet(FramedPB);
ZpbConfig(FramedPB, Zpb_SizePrefixed<>);
struct Prefixed : Lookup { using ReqFacet = ZuFacet::FramedPB; };

struct ReservedHeader : Lookup { using ReqHeaders = ZhttpHeaders("grpc-status"); };
struct UpperHeader : Lookup { using ResHeaders = ZhttpHeaders("X-Upper"); };
struct BodyLength : Lookup { using Trailers = ZhttpHeaders("content-length"); };
struct ManyDefaults : Lookup {
  using ReqHeaders = ZuTypeList<ZuStringT<"x-tag">,
    ZuTypeList<ZuStringT<"one">, ZuStringT<"two">>>;
};
ZuAssert((!Zgrpc::MethodValid<ReservedHeader>{}));
ZuAssert((!Zgrpc::MethodValid<UpperHeader>{}));
ZuAssert((!Zgrpc::MethodValid<BodyLength>{}));
ZuAssert((!Zgrpc::MethodValid<ManyDefaults>{}));

ZuAssert((Zgrpc::CatalogValid<Methods>{}));
ZuAssert((!Zgrpc::CatalogValid<ZuTypeList<Lookup, Dup>>{}));
ZuAssert((!Zgrpc::MethodValid<BadName>{}));
ZuAssert((!Zgrpc::MethodValid<BadService>{}));
ZuAssert((!Zgrpc::MethodValid<BadKind>{}));
ZuAssert((!Zgrpc::MethodValid<NoPB>{}));
ZuAssert((!Zgrpc::MethodValid<Prefixed>{}));
ZuAssert((!Zgrpc::MethodValid<int>{}));

static void catalog()
{
  ZuTestScopeRT(catalog);
  ZuCheckRT(Store::methodMatch("/example.Store/Lookup") == 0);
  ZuCheckRT(Store::methodMatch("/example.Store/Watch") == 1);
  for (auto path : {"/example.Store/lookup", "/example.Store/Lookup?x=1",
    "/example.Store/Lookup/", "/example.Store/%4c ookup", "example.Store/Lookup"})
    ZuCheckRT(Store::methodMatch(path) == -1);
  ZuCheckRT(Store::ReqHeaders::nameMatch("x-tag") == 0);
  ZuCheckRT(Store::ReqHeaders::valueMatch(0, "one") >= 0);
  ZuCheckRT(Store::ReqHeaders::valueMatch(0, "two") >= 0);
  ZuCheckRT(Store::ReqHeaders::valueMatch(0, "three") == -1);
  ZuCheckRT(Zgrpc::valid(Zgrpc::Limits{}));
  Zgrpc::Limits limits;
  limits.maxMessageBytes = 0;
  ZuCheckRT(!Zgrpc::valid(limits));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(catalog);
}
