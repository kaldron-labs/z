//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Compilation fixtures: case 0 must compile, cases 1–21 must fail.
#include <zlib/ZtArray.hh>
#include <zlib/Zfix.hh>
#if ZFIX_SCHEMA_CASE >= 20
#include <zlib/ZfixDispatch.hh>
#endif

struct Child {
  int	n;
};

ZfStruct(, (Child, FIX),
  (n, (FIX::ID<100>), Int32));

#if ZFIX_SCHEMA_CASE == 2
struct Bad {
  Child	child;
  int	n;
};

ZfStruct(, (Bad, FIX),
  (child, , UDT),
  (n, (FIX::ID<100>), Int32));
#elif ZFIX_SCHEMA_CASE == 3
struct Bad {
  int	n;
};

ZfStruct(, (Bad, FIX),
  (n, , Int32));
#elif ZFIX_SCHEMA_CASE == 4
struct Bad {
  int	n;
};

ZfStruct(, (Bad, FIX),
  (n, (FIX::ID<100>, FIX::Len<101>), Int32));
#elif ZFIX_SCHEMA_CASE == 11
struct Bad {
  ZuBSpan	data;
};

ZfStruct(, (Bad, FIX),
  (data, (FIX::ID<96>), Bytes));
#elif ZFIX_SCHEMA_CASE == 12
struct Bad {
  using Ints = ZtArray<int, ZtArrayHeapID<"Zfix.SchemaInts">>;
  Ints	values;
};

ZfStruct(, (Bad, FIX),
  (values, (FIX::ID<9001>), Int32Vec));
#elif ZFIX_SCHEMA_CASE == 13
struct Bad {
  ZuCSpan	value;
};

ZfStruct(, (Bad, FIX), (value, (FIX::ID<50>), String));
#elif ZFIX_SCHEMA_CASE == 14
struct Bad {
  ZuCSpan	value;
};

ZfStruct(, (Bad, FIX), (value, (FIX::ID<1128>), String));
#elif ZFIX_SCHEMA_CASE == 5
struct Bad {
  int	n;
};

ZfStruct(, (Bad, FIX),
  (n, (FIX::ID<35>), Int32));
#else
struct Bad {
  int	n;
  int	m;
};

ZfStruct(, (Bad, FIX),
  (n, (FIX::ID<100>), Int32),
#if ZFIX_SCHEMA_CASE == 1
  (m, (FIX::ID<100>), Int32));
#else
  (m, (FIX::ID<101>), Int32));
#endif
#endif

ZuAssert((sizeof(Zfix::Fmt::Schema<Bad, ZuFacet::FIX>) > 0));

struct Request { };

struct Response { };

using RequestMsg = Zfix::MT<Request, "D">;
#if ZFIX_SCHEMA_CASE == 15
using MTList = Zfix::MTList<
  Zfix::MT<Request, "D", ZuFacet::FIX, Zfix::Discriminator<9000, "a">>,
  Zfix::MT<Response, "D", ZuFacet::FIX, Zfix::Discriminator<9000, "a">>>;
#elif ZFIX_SCHEMA_CASE == 16
using MTList = Zfix::MTList<
  Zfix::MT<Request, "D", ZuFacet::FIX, Zfix::Discriminator<9000, "a">>,
  Zfix::MT<Response, "D", ZuFacet::FIX, Zfix::Discriminator<9001, "b">>>;
#elif ZFIX_SCHEMA_CASE == 17
using MTList = Zfix::MTList<RequestMsg,
  Zfix::MT<Response, "D", ZuFacet::FIX, Zfix::Discriminator<9000, "a">>>;
#elif ZFIX_SCHEMA_CASE == 18
using Reply = Zfix::MT<Response, "8">;
using MTList = Zfix::MTList<Zfix::MT<Request, "D", ZuFacet::FIX,
  Zfix::NoDiscriminator, ZuTypeList<Reply, Reply>>>;
#elif ZFIX_SCHEMA_CASE == 19
using MTList = Zfix::MTList<
  Zfix::MT<Request, "D", ZuFacet::FIX, Zfix::Discriminator<35, "D">>>;
#elif ZFIX_SCHEMA_CASE == 6
using MTList = Zfix::MTList<RequestMsg, Zfix::MT<Response, "D">>;
#elif ZFIX_SCHEMA_CASE == 7
using MTList = Zfix::MTList<RequestMsg, RequestMsg>;
#else
using MTList = Zfix::MTList<RequestMsg, Zfix::MT<Response, "8">>;
#endif
ZuAssert(sizeof(MTList) > 0);
#if ZFIX_SCHEMA_CASE == 0
using Fill = Zfix::MT<Response, "8", ZuFacet::FIX, Zfix::Discriminator<150, "F">>;
using Cancel = Zfix::MT<Response, "8", ZuFacet::FIX, Zfix::Discriminator<150, "4">>;
using RequestWithReplies = Zfix::MT<Request, "D", ZuFacet::FIX,
  Zfix::NoDiscriminator, ZuTypeList<Fill, Cancel>>;
using Selected = Zfix::MTList<RequestWithReplies, Fill, Cancel>;
ZuAssert(sizeof(Selected) > 0);
#endif


#if ZFIX_SCHEMA_CASE == 0 || (ZFIX_SCHEMA_CASE >= 8 && ZFIX_SCHEMA_CASE <= 10)
struct Entry {
  int	a;
  int	b;
};

ZfStruct(, (Entry, FIX),
  (a, (FIX::ID<448>), Int32),
#if ZFIX_SCHEMA_CASE == 8
  (b, (FIX::ID<448>), Int32));
#else
  (b, (FIX::ID<452>), Int32));
#endif
struct Entries : ZtArray<Entry, ZtArrayHeapID<"Zfix.Schema">> {
  ZuDerive_(Entries, (ZtArray<Entry, ZtArrayHeapID<"Zfix.Schema">>));
#if ZFIX_SCHEMA_CASE == 9
  friend Zfix::Fmt::AsArray<ZfFieldTC::Int32> Zfix_Fmt(Entries *);
#else
  friend Zfix::Fmt::AsArray<> Zfix_Fmt(Entries *);
#endif
};

#if ZFIX_SCHEMA_CASE == 10
#include <zlib/ZuUnion.hh>
struct Choice : ZuUnion<Entry> {
  ZuDerive_(Choice, (ZuUnion<Entry>));
friend Zfix::Fmt::AsUnion Zfix_Fmt(Choice *);
};

struct Group {
  Choice	entries;
};

ZfStruct(, (Group, FIX), (entries, , UDT));
#else
struct Group {
  Entries	entries;
};

ZfStruct(, (Group, FIX), (entries, (FIX::ID<453>), UDT));
#endif
void instantiate() { Zfix::Fmt::Handler<Group> h{""}; }
#endif

#if ZFIX_SCHEMA_CASE == 0
struct Vec {
  using Ints = ZtArray<int, ZtArrayHeapID<"Zfix.SchemaInts">>;
  Ints	values;
};

ZfStruct(, (Vec, FIX),
  (values, (FIX::ID<9001>, FIX::Multiple), Int32Vec));
ZuAssert((sizeof(Zfix::Fmt::Schema<Vec, ZuFacet::FIX>) > 0));
#endif

#if ZFIX_SCHEMA_CASE >= 20
struct LeftWire {
  ZuBSpan	data;
};

ZfStruct(, (LeftWire, FIX), (data, (FIX::ID<96>, FIX::Len<95>), Bytes));
struct RightWire {
#if ZFIX_SCHEMA_CASE == 20
  ZuCSpan data;
#else
  ZuBSpan data;
#endif
};

ZfStruct(, (RightWire, FIX),
#if ZFIX_SCHEMA_CASE == 20
  (data, (FIX::ID<96>), String));
#else
  (data, (FIX::ID<96>, FIX::Len<94>), Bytes));
#endif
using LeftMsg = Zfix::MT<LeftWire, "X", ZuFacet::FIX, Zfix::Discriminator<9000, "L">>;
using RightMsg = Zfix::MT<RightWire, "X", ZuFacet::FIX, Zfix::Discriminator<9000, "R">>;
using Choices = Zfix::MTList<LeftMsg, RightMsg>;
using Scan = typename Zfix::Dispatch_::Wire<Choices::Messages, 9000>::T;
ZuAssert((sizeof(Zfix::Fmt::Schema<Scan, ZuFacet::FIX>) > 0));
#endif
