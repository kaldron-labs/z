//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfixFmt.hh>

using namespace ZuTestUtil;
using namespace Zfix::Fmt;
using Text = ZtString<ZtStringHeapID<"Zfix.GroupTest">>;

struct Sub {
  Text	id;
  int	type = 0;
};

ZfStruct(, (Sub, FIX),
  (id, (FIX::ID<523>, Required),		String),
  (type, (FIX::ID<803>, Required),		Int32));
ZfStructImpl(Sub);
ZfStructImpl(Sub, FIX);

struct Subs : ZtArray<Sub, ZtArrayHeapID<"Zfix.TestSubs">> {
  ZuDerive_(Subs, (ZtArray<Sub, ZtArrayHeapID<"Zfix.TestSubs">>));
friend AsArray<> Zfix_Fmt(Subs *);
};

struct Party {
  Text	id;
  Text	source;
  int	role = 0;
  Subs	subs;
};

ZfStruct(, (Party, FIX),
  (id, (FIX::ID<448>, Required),		String),
  (source, (FIX::ID<447>, Required),	String),
  (role, (FIX::ID<452>, Required),		Int32),
  (subs, (FIX::ID<802>),			UDT));
ZfStructImpl(Party);
ZfStructImpl(Party, FIX);
struct Parties : ZtArray<Party, ZtArrayHeapID<"Zfix.TestParties">> {
  ZuDerive_(Parties, (ZtArray<Party, ZtArrayHeapID<"Zfix.TestParties">>));
friend AsArray<> Zfix_Fmt(Parties *);
};

struct Body {
  Text		id;
  Parties	parties;
};

ZfStruct(, (Body, FIX),
  (id, (FIX::ID<11>),			String),
  (parties, (FIX::ID<453>, Mutable),	UDT));
ZfStructImpl(Body);
ZfStructImpl(Body, FIX);

struct Dual {
  Parties	first;
  Parties	second;
};

ZfStruct(, (Dual, FIX),
  (first, (FIX::ID<453>),			UDT),
  (second, (FIX::ID<7000>),			UDT));
ZfStructImpl(Dual);
ZfStructImpl(Dual, FIX);

struct Flexible {
  Text	optional;
  int	value = 0;
};

ZfStruct(, (Flexible, FIX),
  (optional, (FIX::ID<448>),		String),
  (value, (FIX::ID<452>, Required),		Int32));
ZfStructImpl(Flexible);
ZfStructImpl(Flexible, FIX);
struct Flexibles : ZtArray<Flexible, ZtArrayHeapID<"Zfix.TestFlexibles">> {
  ZuDerive_(Flexibles, (ZtArray<Flexible, ZtArrayHeapID<"Zfix.TestFlexibles">>));
friend AsArray<> Zfix_Fmt(Flexibles *);
};

struct Inferred {
  Flexibles	entries;
  Text		id;
};

ZfStruct(, (Inferred, FIX),
  (entries, (FIX::ID<7000>),		UDT),
  (id, (FIX::ID<11>),			String));
ZfStructImpl(Inferred);
ZfStructImpl(Inferred, FIX);

struct Skipped {
  Parties	parties;
  Text		id;
};

ZfStruct(, (Skipped, FIX),
  ((parties, Rd), (FIX::ID<453>),		UDT),
  (id, (FIX::ID<11>),			String));
ZfStructImpl(Skipped);
ZfStructImpl(Skipped, FIX);

struct Shared {
  Parties	parties;
  int		role = 0;
};

ZfStruct(, (Shared, FIX),
  (parties, (FIX::ID<453>),			UDT),
  (role, (FIX::ID<452>),			Int32));
ZfStructImpl(Shared);
ZfStructImpl(Shared, FIX);

struct Blob {
  int		id = 0;
  ZuBSpan	data;
};

ZfStruct(, (Blob, FIX),
  (id, (FIX::ID<1001>, Required),		Int32),
  (data, (FIX::ID<96>, FIX::Len<95>),	Bytes));
ZfStructImpl(Blob);
ZfStructImpl(Blob, FIX);
struct Blobs : ZtArray<Blob, ZtArrayHeapID<"Zfix.TestBlobs">> {
  ZuDerive_(Blobs, (ZtArray<Blob, ZtArrayHeapID<"Zfix.TestBlobs">>));
friend AsArray<> Zfix_Fmt(Blobs *);
};

struct BinaryBody {
  Blobs	blobs;
};

ZfStruct(, (BinaryBody, FIX),
  (blobs, (FIX::ID<7001>),			UDT));
ZfStructImpl(BinaryBody);
ZfStructImpl(BinaryBody, FIX);

struct Optional : ZmNHeap<Optional> {
  int	value = 0;
friend inline ZuStringT<"Zfix.TestOptional"> ZmNHeapID(Optional *);
};

ZfStruct(, (Optional, FIX),
  (value, (FIX::ID<448>),	Int32));
ZfStructImpl(Optional);
ZfStructImpl(Optional, FIX);
struct Dynamic {
  ZuPtr<Optional>	optional;
  int			value = 0;
};

ZfStruct(, (Dynamic, FIX),
  (optional, ,			UDT),
  (value, (FIX::ID<452>),	Int32));
ZfStructImpl(Dynamic);
ZfStructImpl(Dynamic, FIX);
struct Dynamics : ZtArray<Dynamic, ZtArrayHeapID<"Zfix.TestDynamics">> {
  ZuDerive_(Dynamics, (ZtArray<Dynamic, ZtArrayHeapID<"Zfix.TestDynamics">>));
friend AsArray<> Zfix_Fmt(Dynamics *);
};

struct DynamicBody {
  Dynamics	entries;
};

ZfStruct(, (DynamicBody, FIX),
  (entries, (FIX::ID<7002>),	UDT));
ZfStructImpl(DynamicBody);
ZfStructImpl(DynamicBody, FIX);

static void delimiter()
{
  ZuTestScope(delimiter);
  DynamicBody body;
  body.entries.push(Dynamic{});
  body.entries.push(Dynamic{});
  constexpr auto wire = "7002=2\1" "452=0\1" "452=0\1"_z;
  char bytes[wire.length() + sizeof("448=0\1") - 1];
  Sink same{{bytes, sizeof(bytes)}};
  ZuCheck(save(same, body) && same.data() == wire);
  body.entries[0].optional = new Optional{};
  Sink different{{bytes, sizeof(bytes)}};
  ZuCheck(!save(different, body) && different.error().code == ErrorCode::Group);
}

static void nested()
{
  ZuTestScope(nested);
  constexpr auto wire =
    "11=order\1" "453=2\1"
    "448=A\1" "447=B\1" "452=1\1" "802=2\1"
      "523=sub1\1" "803=10\1" "523=sub2\1" "803=11\1"
    "448=C\1" "447=D\1" "452=2\1" "802=0\1"_z;
  Body body;
  auto e = load(body, wire);
  ZuCheck(!e);
  ZuCheck(body.id == "order" && body.parties.length() == 2);
  ZuCheck(body.parties[0].id == "A" && body.parties[1].id == "C");
  ZuCheck(body.parties[0].subs.length() == 2 && !body.parties[1].subs.length());
  ZuCheck(body.parties[0].subs[1].id == "sub2");
  char bytes[wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, body));
  ZuCheck(out.data() == wire && out.flush());
  e = update(body, "453=0\1"_z);
  ZuCheck(!e && !body.parties.length());
  ZuCheck(body.id == "order");
  Sink zero{{bytes, sizeof(bytes)}};
  ZuCheck(save(zero, body));
  ZuCheck(zero.data() == "11=order\1" "453=0\1"_z);
  ZuCheck(!load(body, wire));
  ZuCheck(!load(body, "11=missing-group\1"_z));
  ZuCheck(!body.parties.length());
}

static void scopes()
{
  ZuTestScope(scopes);
  Dual d;
  auto e = load(d,
    "453=1\1" "448=A\1" "447=B\1" "452=1\1"
    "7000=1\1" "448=C\1" "447=D\1" "452=2\1"_z);
  ZuCheck(!e);
  ZuCheck(d.first.length() == 1 && d.second.length() == 1);
  ZuCheck(d.first[0].id == "A" && d.second[0].id == "C");
  Inferred inferred;
  e = load(inferred, "7000=2\1" "999=unknown\1" "452=1\1" "452=2\1" "11=end\1"_z);
  ZuCheck(!e);
  ZuCheck(inferred.entries.length() == 2 && inferred.entries[1].value == 2);
  ZuCheck(!inferred.entries[0].optional && inferred.id == "end");
  Skipped skipped;
  e = load(skipped, "453=1\1" "448=A\1" "447=B\1" "452=1\1" "11=end\1"_z);
  ZuCheck(!e && !skipped.parties.length() && skipped.id == "end");
  e = load(skipped, "453=1\1" "448=A\1" "447=B\1" "452=bad\1"_z);
  ZuCheck(e.code == ErrorCode::Value && e.tag == 452);
  Shared shared;
  e = load(shared, "453=1\1" "448=A\1" "447=B\1" "452=1\1" "452=9\1"_z);
  ZuCheck(!e && shared.role == 9 && shared.parties[0].role == 1);
}

static void malformed()
{
  ZuTestScope(malformed);
  Body b;
  auto e = load(b, "453=2\1" "448=A\1" "447=B\1" "452=1\1"_z);
  ZuCheck(e.code == ErrorCode::Group);
  e = load(b, "453=0\1" "448=A\1"_z);
  ZuCheck(e.code == ErrorCode::Group);
  e = load(b, "453=1\1" "448=A\1" "447=B\1" "452=1\1" "448=C\1"_z);
  ZuCheck(e.code == ErrorCode::Group);
  e = load(b, "453=1\1" "448=A\1" "447=B\1" "447=C\1" "452=1\1"_z);
  ZuCheck(e.code == ErrorCode::Duplicate && e.tag == 447);
  e = load(b, "453=1\1" "448=A\1" "452=1\1" "447=B\1"_z);
  ZuCheck(e.code == ErrorCode::Group);
  e = load(b, "453=-1\1"_z);
  ZuCheck(e.code == ErrorCode::Value);
  e = load(b, "453=1\1" "448=A\1" "447=B\1"_z);
  ZuCheck(e.code == ErrorCode::Required && e.tag == 452);
  e = load(b, "453=2\1" "448=A\1" "447=B\1" "452=1\1" "447=D\1" "452=2\1"_z);
  ZuCheck(bool(e));
}

static void limits()
{
  ZuTestScope(limits);
  constexpr auto wire = "453=1\1" "448=A\1" "447=B\1" "452=1\1" "802=1\1"
    "523=sub\1" "803=10\1"_z;
  Body body;
  Limits cfg;
  cfg.groupEntries = 1;
  ZuCheck(load(body, wire, cfg).code == ErrorCode::Size);
  cfg.groupEntries = 2;
  cfg.groupDepth = 1;
  ZuCheck(load(body, wire, cfg).code == ErrorCode::Depth);
  cfg.groupDepth = 2;
  ZuCheck(!load(body, wire, cfg));
  cfg.parseBytes = sizeof(Position) * 2;
  ZuCheck(load(body, wire, cfg).code == ErrorCode::Size);
  cfg.parseBytes = Limits::ParseBytes;
  ZuCheck(!load(body, wire, cfg));
  char bytes[wire.length() + sizeof("11=\1") - 1];
  cfg.groupEntries = 1;
  Sink countOut{{bytes, sizeof(bytes)}, cfg};
  ZuCheck(!save(countOut, body) && countOut.error().code == ErrorCode::Size);
  cfg.groupEntries = 2;
  cfg.groupDepth = 1;
  Sink depthOut{{bytes, sizeof(bytes)}, cfg};
  ZuCheck(!save(depthOut, body) && depthOut.error().code == ErrorCode::Depth);
  cfg.groupDepth = 2;
  Sink good{{bytes, sizeof(bytes)}, cfg};
  ZuCheck(save(good, body) && good.flush());
}

static void binary()
{
  ZuTestScope(binary);
  constexpr auto wire = "7001=2\1" "1001=1\1" "95=9\1" "96=A\1" "1001=9\1\1"
    "1001=2\1" "95=0\1" "96=\1"_z;
  BinaryBody b;
  auto e = load(b, wire);
  ZuCheck(!e && b.blobs.length() == 2);
  ZuCheck(b.blobs[0].id == 1 && b.blobs[1].id == 2);
  ZuCheck(b.blobs[0].data == ZuBSpan{"A\1" "1001=9\1"_z});
  ZuCheck(!b.blobs[1].data);
  char bytes[wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, b) && out.flush());
  ZuCheck(out.data() == wire);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(delimiter);
  ZuTestCall(nested);
  ZuTestCall(scopes);
  ZuTestCall(malformed);
  ZuTestCall(limits);
  ZuTestCall(binary);
  return 0;
}
