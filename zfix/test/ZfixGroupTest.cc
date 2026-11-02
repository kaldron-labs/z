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
  constexpr auto Wire = "7002=2\1" "452=0\1" "452=0\1"_z;
  char bytes[Wire.length() + sizeof("448=0\1") - 1];
  Sink same{{bytes, sizeof(bytes)}};
  ZuCheck(save(same, body) && same.data() == Wire);
  body.entries[0].optional = new Optional{};
  Sink different{{bytes, sizeof(bytes)}};
  ZuCheck(!save(different, body) && different.error().code == ErrorCode::Group);
}

static void nested()
{
  ZuTestScope(nested);
  constexpr auto Wire =
    "11=order\1" "453=2\1"
    "448=A\1" "447=B\1" "452=1\1" "802=2\1"
      "523=sub1\1" "803=10\1" "523=sub2\1" "803=11\1"
    "448=C\1" "447=D\1" "452=2\1" "802=0\1"_z;
  Body body;
  auto e = load(body, Wire);
  ZuCheck(!e);
  ZuCheck(body.id == "order" && body.parties.length() == 2);
  ZuCheck(body.parties[0].id == "A" && body.parties[1].id == "C");
  ZuCheck(body.parties[0].subs.length() == 2 && !body.parties[1].subs.length());
  ZuCheck(body.parties[0].subs[1].id == "sub2");
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, body));
  ZuCheck(out.data() == Wire && out.flush());
  e = update(body, "453=0\1"_z);
  ZuCheck(!e && !body.parties.length());
  ZuCheck(body.id == "order");
  Sink zero{{bytes, sizeof(bytes)}};
  ZuCheck(save(zero, body));
  ZuCheck(zero.data() == "11=order\1" "453=0\1"_z);
  ZuCheck(!load(body, Wire));
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
  ZuCheck(!e && !skipped.parties.length());
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
  ZuCheck(e.code == ErrorCode::Size);
  e = load(b, "453=1\1" "448=A\1" "447=B\1"_z);
  ZuCheck(e.code == ErrorCode::Required && e.tag == 452);
  e = load(b, "453=2\1" "448=A\1" "447=B\1" "452=1\1" "447=D\1" "452=2\1"_z);
  ZuCheck(bool(e));
}

static void limits()
{
  ZuTestScope(limits);
  constexpr auto Wire = "453=1\1" "448=A\1" "447=B\1" "452=1\1" "802=1\1"
    "523=sub\1" "803=10\1"_z;
  Body body;
  Limits cfg;
  cfg.groupEntries = 1;
  ZuCheck(load(body, Wire, cfg).code == ErrorCode::Size);
  cfg.groupEntries = 2;
  cfg.groupDepth = 1;
  ZuCheck(load(body, Wire, cfg).code == ErrorCode::Depth);
  cfg.groupDepth = 2;
  ZuCheck(!load(body, Wire, cfg));
  cfg.parseBytes = sizeof(ZuBitmap<Schema<Body, ZuFacet::FIX>::Leaves::N>);
  ZuCheck(load(body, Wire, cfg).code == ErrorCode::Size);
  cfg.parseBytes = Limits::ParseBytes;
  ZuCheck(!load(body, Wire, cfg));
  char bytes[Wire.length() + sizeof("11=\1") - 1];
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

struct SignedRow { int value = 0; };
ZfStruct(, (SignedRow, FIX), (value, (FIX::ID<-11>, Required), Int32));
struct SignedRows : ZtArray<SignedRow, ZtArrayHeapID<"Zfix.TestSigned">> {
  ZuDerive_(SignedRows, (ZtArray<SignedRow, ZtArrayHeapID<"Zfix.TestSigned">>));
friend AsArray<> Zfix_Fmt(SignedRows *);
};
struct SignedBody { SignedRows rows; };
ZfStruct(, (SignedBody, FIX), (rows, (FIX::ID<-453>), UDT));

static void signedGroups()
{
  ZuTestScope(signedGroups);
  constexpr auto Wire = "-453=2\1" "-11=7\1" "-11=8\1"_z;
  SignedBody body;
  auto error = load(body, Wire);
  ZuCheck(!error && body.rows.length() == 2);
  if (body.rows.length() == 2)
    ZuCheck(body.rows[0].value == 7 && body.rows[1].value == 8);
  char bytes[Wire.length()];
  Sink out{{bytes, sizeof(bytes)}};
  ZuCheck(save(out, body) && out.data() == Wire);
}

struct BinaryDetail {
  ZuBSpan data;
};
ZfStruct(, (BinaryDetail, FIX),
  (data, (FIX::ID<212>, FIX::Binary<213>, Required), Bytes));
struct BinaryDetails : ZtArray<BinaryDetail, ZtArrayHeapID<"Zfix.BinaryDetails">> {
  ZuDerive_(BinaryDetails, (ZtArray<BinaryDetail, ZtArrayHeapID<"Zfix.BinaryDetails">>));
friend AsArray<> Zfix_Fmt(BinaryDetails *);
};
struct BinaryEntry {
  ZuBSpan key;
  BinaryDetails details;
  ZuCSpan extra;
  ZuCSpan tail;
};
ZfStruct(, (BinaryEntry, FIX),
  (key, (FIX::ID<95>, FIX::Binary<96>, Required), Bytes),
  (details, (FIX::ID<7000>), UDT),
  (extra, (FIX::ID<4200>, FIX::Binary<4201>), String),
  (tail, (FIX::ID<5000>, Required), String));
struct BinaryEntries : ZtArray<BinaryEntry, ZtArrayHeapID<"Zfix.BinaryEntries">> {
  ZuDerive_(BinaryEntries, (ZtArray<BinaryEntry, ZtArrayHeapID<"Zfix.BinaryEntries">>));
friend AsArray<> Zfix_Fmt(BinaryEntries *);
};
struct BinaryOuter {
  BinaryEntries entries;
  ZuBSpan parent;
  int end = 0;
};
ZfStruct(, (BinaryOuter, FIX),
  (entries, (FIX::ID<6000>), UDT),
  (parent, (FIX::ID<95>, FIX::Binary<96>), Bytes),
  (end, (FIX::ID<9100>), Int32));
struct BinarySkipped {
  BinaryEntries entries;
  ZuBSpan parent;
  int end = 0;
};
ZfStruct(, (BinarySkipped, FIX),
  ((entries, Rd), (FIX::ID<6000>), UDT),
  (parent, (FIX::ID<95>, FIX::Binary<96>), Bytes),
  (end, (FIX::ID<9100>), Int32));
struct BinaryOnly { BinaryEntries entries; };
ZfStruct(, (BinaryOnly, FIX), (entries, (FIX::ID<6000>), UDT));

static void binaryGroups()
{
  ZuTestScope(binaryGroups);
  constexpr auto Wire = "6000=2\1" "95=3\1" "96=A\1B\1" "7000=2\1"
    "212=6\1" "213=95=9\1X\1" "212=0\1" "213=\1"
    "4200=5\1" "4201=95=9\1\1" "5000=first\1"
    "95=1\1" "96=Z\1" "7000=0\1" "4200=0\1" "4201=\1" "5000=end\1"
    "95=3\1" "96=P\0Q\1" "9100=9\1"_z;
  BinaryOuter value, copy;
  ZuCheck(!load(value, Wire) && value.entries.length() == 2 && value.end == 9);
  if (value.entries.length() != 2) return;
  const auto &first = value.entries[0]; const auto &second = value.entries[1];
  ZuCheck(first.details.length() == 2 && !second.details.length());
  if (first.details.length() != 2) return;
  ZuCheck(first.key.data() == reinterpret_cast<const uint8_t *>(Wire.data() + 15) &&
    first.key.length() == 3 && first.tail == "first");
  ZuCheck(first.details[0].data.data() == reinterpret_cast<const uint8_t *>(
    Wire.data() + unsigned(ZuCSpan{Wire}.find("213=95")) + 4));
  ZuCheck(first.details[0].data == ZuBSpan{"95=9\1X"_z});
  ZuCheck(first.details[1].data.data() == reinterpret_cast<const uint8_t *>(
    Wire.data() + unsigned(ZuCSpan{Wire}.find("213=\1")) + 4) && !first.details[1].data.length());
  ZuCheck(first.extra.data() == Wire.data() + unsigned(ZuCSpan{Wire}.find("4201=95")) + 5);
  ZuCheck(first.extra.length() == 5 && second.tail == "end");
  ZuCheck(value.parent.data() == reinterpret_cast<const uint8_t *>(
    Wire.data() + unsigned(ZuCSpan{Wire}.find("96=P")) + 3) && value.parent.length() == 3);
  Indexed<BinaryOuter, ZuFacet::FIX> indexed{Wire};
  ZuCheck(indexed.load(copy) && copy.entries.length() == 2);
  if (copy.entries.length() != 2 || copy.entries[0].details.length() != 2) return;
  ZuCheck(copy.entries[0].key.data() == first.key.data() &&
    copy.entries[0].details[0].data.data() == first.details[0].data.data() &&
    copy.entries[0].details[1].data.data() == first.details[1].data.data() &&
    copy.entries[0].extra.data() == first.extra.data() &&
    copy.parent.data() == value.parent.data());
  auto bytes = ZtScratch(PrintBuf, Wire.length());
  Sink out{{bytes.data(), unsigned(Wire.length())}};
  ZuCheck(save(out, value) && out.data() == Wire);
  BinarySkipped skipped;
  ZuCheck(!load(skipped, Wire) && !skipped.entries.length() && skipped.end == 9);
  ZuCheck(skipped.parent.data() == value.parent.data());
  Indexed<BinarySkipped, ZuFacet::FIX> skippedIndex{Wire};
  BinarySkipped indexedSkip;
  ZuCheck(skippedIndex.load(indexedSkip) && !indexedSkip.entries.length() &&
    indexedSkip.parent.data() == value.parent.data());
}
static void binaryGroupErrors()
{
  ZuTestScope(binaryGroupErrors);
  auto malformed = [](ZuCSpan wire, unsigned code, int32_t tag, unsigned offset) {
    BinaryOuter value, copy;
    auto error = load(value, wire);
    Indexed<BinaryOuter, ZuFacet::FIX> indexed{wire}; indexed.load(copy);
    BinarySkipped skipped;
    auto skippedError = load(skipped, wire);
    Indexed<BinarySkipped, ZuFacet::FIX> skipIndex{wire}; skipIndex.load(skipped);
    return error.code == code && error.tag == tag && error.offset == offset &&
      indexed.error().code == code && indexed.error().tag == tag &&
      indexed.error().offset == offset &&
      skippedError.code == code && skippedError.tag == tag && skippedError.offset == offset &&
      skipIndex.error().code == code && skipIndex.error().tag == tag &&
      skipIndex.error().offset == offset;
  };
  constexpr auto Bad = "6000=1\1" "95=1\1" "96=A\1" "7000=1\1"
    "212=1\1" "213=XX\1" "5000=tail\1"_z;
  ZuCheck(malformed(Bad, ErrorCode::Length, 213, unsigned(ZuCSpan{Bad}.find("213=XX")) + 5));
  constexpr auto Count = "6000=1\1" "95=1\1" "96=A\1" "7000=1\1"
    "212=x\1" "213=A\1" "5000=tail\1"_z;
  ZuCheck(malformed(Count, ErrorCode::Value, 212, unsigned(ZuCSpan{Count}.find("212=x")) + 4));
  constexpr auto Duplicate = "6000=1\1" "95=1\1" "96=A\1" "7000=0\1"
    "4200=1\1" "4201=X\1" "4200=1\1" "4201=Y\1" "5000=tail\1"_z;
  auto duplicateOffset = unsigned(ZuCSpan{Duplicate}.find("4200=1\1" "4201=Y"));
  ZuCheck(malformed(Duplicate, ErrorCode::Duplicate, 4200, duplicateOffset));
  BinaryOnly only, copy;
  constexpr auto Zero = "6000=0\1" "95=1\1" "96=A\1"_z;
  auto error = load(only, Zero);
  Indexed<BinaryOnly, ZuFacet::FIX> zero{Zero}; zero.load(copy);
  ZuCheck(error.code == ErrorCode::Group && error.tag == 95 && error.offset == 7);
  ZuCheck(zero.error().code == error.code && zero.error().tag == error.tag &&
    zero.error().offset == error.offset);
  constexpr auto Excess = "6000=1\1" "95=1\1" "96=A\1" "5000=x\1" "95=1\1" "96=B\1"_z;
  error = load(only, Excess);
  Indexed<BinaryOnly, ZuFacet::FIX> excess{Excess}; excess.load(copy);
  ZuCheck(error.code == ErrorCode::Group && error.tag == 95 && error.offset == 24);
  ZuCheck(excess.error().code == error.code && excess.error().tag == error.tag &&
    excess.error().offset == error.offset);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(delimiter);
  ZuTestCall(binaryGroups);
  ZuTestCall(binaryGroupErrors);
  ZuTestCall(signedGroups);
  ZuTestCall(nested);
  ZuTestCall(scopes);
  ZuTestCall(malformed);
  ZuTestCall(limits);
  return 0;
}
