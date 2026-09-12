//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmDemangle.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLHash.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>

#include "ZfMapTest.hh"

using namespace ZuTestUtil;

struct Empty { };
ZfStruct(, Empty);
ZfStructImpl(Empty);

ZtEnumNS(, Values, int8_t, High, Low, Normal);

namespace Flags {
  ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2);
}

ZtEnumImplNS(Values);
ZtEnumImplNS(Flags);

ZuStructFacet(Bah);

struct Nested {
  int i1 = 0, i2 = 0;

  friend ZfStructPrint ZuPrintType(Nested *);
};

#define Nested_Fields(macro, ...) macro( \
  (((i1), (Ctor<0>)), (Int32)), \
  (((i2), (Ctor<1>)), (Int32)) __VA_OPT__(, __VA_ARGS__))

#define Nested_Struct(...) ZfStruct(, (Nested, JSON, Bah) __VA_OPT__(, __VA_ARGS__))

Nested_Fields(Nested_Struct);

struct Foo {
  const char *string = nullptr;
  ZtArray<uint8_t> bytes;
  ZuID id = "goodbye";
  int int_ = 0;
  int int_ranged = 42;
  unsigned hex = 0xdeadbeef;
  int enum_ = Values::Normal;
  uint128_t daFlags = Flags::Bit1();
  double float_ = ZuCmp<double>::null();
  double float_ranged = 0.42;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time_;
  Nested nested;
  ZtArray<ZtArray<uint8_t>> bytesVec;

  friend ZfStructPrint ZuPrintType(Foo *);
};

#define FooFloatFields \
  (((float_),		(Ctor<8>)),	(Float)), \
  (((float_ranged),	(Ctor<9>, (Range<0.0, 1>))),	(Float, 0.42))

ZfStruct(, (Foo, JSON),
  (((string, Rd),	(Ctor<0>)),	(CString, "hello \"world\"")),
  (((bytes),		(Ctor<1>)),	(Bytes, ZuBSpan{"bytes"})),
  (((id),		(Ctor<2>, Mutable)),	(String, "goodbye")),
  (((int_),		(Ctor<3>)),	(Int32)),
  (((int_ranged),	(Ctor<4>, (Range<0, 100>))),	(Int32, 42)),
  (((hex),		(Ctor<5>, Hex)),
    					(UInt32, 0xdeadbeef)),
  (((enum_),		(Ctor<6>, Enum<Values::Map>)),
    					(Int32, Values::Normal)),
  (((daFlags),		(Ctor<7>, Flags<Flags::Map>)),
    					(UInt128, Flags::Bit1())),
  FooFloatFields,
  (((fixed),		(Ctor<10>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))),		(Fixed)),
  (((decimal),		(Ctor<11>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))),	(Decimal)),
  (((time_),		(Ctor<12>)),	(Time)),
  (((nested),		(Ctor<13>)),	(UDT)),
  (((bytesVec),		(Ctor<14>)),	(BytesVec)));

ZfStructRender(, Foo, Bah,
  string,
  (bytes,	JSON::ID<"bytes-BAH">, JSON::Raw),
  id,
  (int_,	JSON::ID<"int-BAH">, JSON::Number<ZuFmt::Right<9>>),
  int_ranged, hex,
  (enum_,	JSON::ID<"enum-BAH">),
  daFlags,
  (float_,	JSON::ID<"float-BAH">, JSON::Number<ZuFmt::FP<4>>),
  float_ranged, fixed, decimal, time_, nested, bytesVec);

ZfStructImpl(Nested);
ZfStructImpl(Nested, JSON);
ZfStructImpl(Nested, Bah);
ZfStructImpl(Foo);
ZfStructImpl(Foo, JSON);
ZfStructImpl(Foo, Bah);

using IntField = ZfField(Foo, int_);
using IntRangeField = ZfField(Foo, int_ranged);
using FloatRangeField = ZfField(Foo, float_ranged);
using IntRange = ZuFieldProp::GetRange<typename IntRangeField::Props>;
using FloatRange = ZuFieldProp::GetRange<typename FloatRangeField::Props>;

ZuAssert(!ZuFieldProp::HasRange<typename IntField::Props>{});
ZuAssert(ZuFieldProp::HasRange<typename IntRangeField::Props>{});
ZuAssert(IntRange::minimum() == 0);
ZuAssert(IntRange::maximum() == 100);
ZuAssert(FloatRange::minimum() == 0.0);
ZuAssert(FloatRange::maximum() == 1);
ZuAssert(IntField::minimum() == ZuCmp<int>::minimum());
ZuAssert(IntField::maximum() == ZuCmp<int>::maximum());
ZuAssert(IntRangeField::minimum() == 0);
ZuAssert(IntRangeField::maximum() == 100);
ZuAssert(FloatRangeField::minimum() == 0.0);
ZuAssert(FloatRangeField::maximum() == 1);

using BoxInt = ZuNBox0(int);

struct BoxFoo {
  BoxInt value;
  ZtArray<BoxInt> values;
};

ZfStruct(, (BoxFoo, JSON),
  (((value),	(Ctor<0>, JSON::String<>)),	(Int32)),
  (((values),	(Ctor<1>, JSON::String<>)),	(Int32Vec)));

template <typename T, typename = void>
struct MinMax {
  template <typename S>
  friend inline S &operator <<(S &s, const MinMax &) { return s; }
};
template <typename T>
struct MinMax<T, decltype(T::minimum(), void())> {
  template <typename S>
  friend inline S &operator <<(S &s, const MinMax &m) {
    using Print = typename T::Type::template Print<>;
    s << " minimum=" << Print{T::minimum()}
      << " maximum=" << Print{T::maximum()};
    return s;
  }
};

template <typename Fields, typename O>
void print(const O &o) {
  if (verbose) std::cerr << o;
}

struct Baz {
  Baz() = default;
  Baz(const Baz &) = default;
  Baz(Baz &&) = default;
  Baz &operator =(const Baz &) = default;
  Baz &operator =(Baz &&) = default;
  Baz(ZuCSpan s) { }
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const Baz &baz) {
    return s << "baz!";
  }
  friend ZfJSON::AsString ZfJSON_Fmt(Baz *);
};
struct BazArray : public ZtArray<Baz> {
  ZuDerive_(BazArray, ZtArray<Baz>);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(BazArray *);
};
struct Bazz {
  Baz baz;
  BazArray bazArray;
};
ZfStruct(, Bazz,
  (((baz), (Ctor<0>)), (UDT)),
  (((bazArray), (Ctor<1>)), (UDT)));

struct OptFoo {
  const char *head = nullptr;
  const char *req = "";
  const char *mid = nullptr;
  const char *tail = "tail";
};
ZfStruct(, (OptFoo, JSON),
  (((head),	(Ctor<0>, JSON::Opt)),	(CString)),
  (((req),	(Ctor<1>)),		(CString)),
  (((mid),	(Ctor<2>, JSON::Opt)),	(CString)),
  (((tail),	(Ctor<3>)),		(CString)));

struct JSONUpdate {
  int required;
  int kept;
  int reset;
};
ZfStruct(, (JSONUpdate, JSON),
  (((required), (Ctor<0>, Mutable, Required)), (Int32)),
  (((kept),     (Ctor<1>, Mutable)),           (Int32, 2)),
  (((reset),    (Ctor<2>, Mutable, Reset)),    (Int32, 3)));

struct FmtInt {
  uint128_t value = 0;
};
using FmtIntFormat = ZuFmt::Hex<false, ZuFmt::Right<32>>;
ZfStruct(, (FmtInt, JSON),
  (((value), (Ctor<0>, JSON::String<FmtIntFormat>)), (UInt128)));

using MapKey = ZtString<>;

using IntHash =
  ZfMapTest<"ZfTest.JSON.IntHash", ZmHashKV<MapKey, int>>;
using IntHashRef = ZmRef<IntHash>;
inline ZfJSON::AsMap<ZfFieldTC::Int32> ZfJSON_Fmt(IntHash *);

using IntLHash =
  ZfMapTest<"ZfTest.JSON.IntLHash", ZmLHashKV<MapKey, int>>;
using IntLHashRef = ZmRef<IntLHash>;
inline ZfJSON::AsMap<ZfFieldTC::Int32> ZfJSON_Fmt(IntLHash *);

using IntTree_ =
  ZmRBTreeKV<MapKey, int, ZmRBTreeUnique<true>>;
using IntTree = ZfMapTest<
  "ZfTest.JSON.IntTree", ZfRefMapTest<IntTree_>>;
using IntTreeRef = ZmRef<IntTree>;
inline ZfJSON::AsMap<ZfFieldTC::Int32> ZfJSON_Fmt(IntTree *);

struct MapObj {
  int fixed = 0;
  int mutable_ = 0;
};
ZfStruct(, (MapObj, JSON, Bah),
  (((fixed),	(Ctor<0>)),		(Int32)),
  (((mutable_),	(Ctor<1>, Mutable)),	(Int32)));

using ObjHash =
  ZfMapTest<"ZfTest.JSON.ObjHash", ZmHashKV<MapKey, MapObj>>;
using ObjHashRef = ZmRef<ObjHash>;
inline ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(ObjHash *);

struct IntArray : public ZtArray<int> {
  ZuDerive_(IntArray, ZtArray<int>);
  friend ZfJSON::AsArray<ZfFieldTC::Int32> ZfJSON_Fmt(IntArray *);
};
using ArrayHash =
  ZfMapTest<"ZfTest.JSON.ArrayHash", ZmHashKV<MapKey, IntArray>>;
using ArrayHashRef = ZmRef<ArrayHash>;
inline ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(ArrayHash *);

struct MapText {
  ZtString<> value;

  MapText() = default;
  MapText(ZuCSpan value_) : value{value_} { }

  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const MapText &v) {
    return s << v.value;
  }
  friend ZfJSON::AsString ZfJSON_Fmt(MapText *);
};
using TextHash =
  ZfMapTest<"ZfTest.JSON.TextHash", ZmHashKV<MapKey, MapText>>;
using TextHashRef = ZmRef<TextHash>;
inline ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(TextHash *);

using MapHash =
  ZfMapTest<"ZfTest.JSON.MapHash", ZmHashKV<MapKey, IntTreeRef>>;
using MapHashRef = ZmRef<MapHash>;
inline ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(MapHash *);

using HexMapProps = ZuTypeList<ZuFieldProp::Hex>;
using HexHash =
  ZfMapTest<"ZfTest.JSON.HexHash", ZmHashKV<MapKey, unsigned>>;
using HexHashRef = ZmRef<HexHash>;
inline ZfJSON::AsMap<ZfFieldTC::UInt32, HexMapProps>
  ZfJSON_Fmt(HexHash *);

using BoolHash =
  ZfMapTest<"ZfTest.JSON.BoolHash", ZmHashKV<MapKey, bool>>;
using BoolHashRef = ZmRef<BoolHash>;
inline ZfJSON::AsMap<ZfFieldTC::Bool> ZfJSON_Fmt(BoolHash *);

using StringHash = ZfMapTest<
  "ZfTest.JSON.StringHash", ZmHashKV<MapKey, ZtString<>>>;
using StringHashRef = ZmRef<StringHash>;
inline ZfJSON::AsMap<ZfFieldTC::String> ZfJSON_Fmt(StringHash *);

using BytesHash = ZfMapTest<
  "ZfTest.JSON.BytesHash", ZmHashKV<MapKey, ZtArray<uint8_t>>>;
using BytesHashRef = ZmRef<BytesHash>;
inline ZfJSON::AsMap<ZfFieldTC::Bytes> ZfJSON_Fmt(BytesHash *);

struct MapHolder {
  IntTreeRef map;
};
ZfStruct(, (MapHolder, JSON),
  (((map), (Ctor<0>, Mutable)), (UDT)));

ZuAssert((ZuIsSame<IntHash::Key, MapKey>{}));
ZuAssert((ZuIsSame<IntHash::Val, int>{}));
ZuAssert(ZuTraits<typename IntHash::Key>::IsString);
ZuAssert((ZuIsSame<
  ZmHeapID<IntHash>, ZuStringT<"ZfTest.JSON.IntHash">>{}));

struct UnionA { int foo; };
struct UnionB { int bar; };
ZfStruct(, (UnionA, JSON), (((foo), (Ctor<0>, Mutable)), (Int32)));
ZfStruct(, (UnionB, JSON), (((bar), (Ctor<0>, Mutable)), (Int32)));
struct UnionArray : public ZtArray<int> {
  ZuDerive_(UnionArray, ZtArray<int>)
  friend ZfJSON::AsArray<ZfFieldTC::Int32> ZfJSON_Fmt(UnionArray *);
};
struct UnionHolder {
  ZfJSON::Union<UnionA, UnionB, UnionArray, MapText>	u;
};
ZfStruct(, (UnionHolder, JSON), (((u), (Ctor<0>, Mutable)), (UDT)));

using JSONValue = ZfJSON::Union<>;
struct JSONValueVec : public ZtArray<JSONValue> {
  ZuDerive_(JSONValueVec, ZtArray<JSONValue>);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(JSONValueVec *);
};
struct JSONValueReply { JSONValueVec items; };
ZfStruct(, (JSONValueReply, JSON), (((items), (Required)), (UDT)));

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  {
    ZuCSpan value{"éX中Y🎵Z\n\1\"\\/"};
    ZtString escaped;
    ZfJSON::quote(escaped, value);
    ZuCheck(escaped == "\"\\u00e9X\\u4e2dY\\ud83c\\udfb5Z\\n\\u0001\\\"\\\\/\"");
    escaped.null();
    ZfJSON::quote(escaped, ZuCSpan{"\0\x1f", 2});
    ZuCheck(escaped == "\"\\u0000\\u001f\"");
  }

  {
    char source[] = "{\"items\":[{\"id\":\"1\",\"name\":\"zum\"}]}";
    auto parsed = ZfJSON::scan(source);
    ZuCheck(parsed.p<0>() == int(sizeof(source) - 1) && parsed.p<1>() &&
      parsed.p<1>()->has<ZfJSON::AnyNode::Array>());
    auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
    ZuCheck(roots.length() == 1 && ZfJSON::unique(roots[0]));
    JSONValueVec values;
    values.push(JSONValue{static_cast<const ZfJSON::AnyNode *>(roots[0].ptr())});
    ZtString encoded;
    ZfJSON::save(encoded, JSONValueReply{ZuMv(values)});
    ZuCheck(encoded ==
      "{\"items\":[{\"items\":[{\"id\":\"1\",\"name\":\"zum\"}]}]}");

    char first[] = "{\"kty\":\"EC\",\"kid\":\"one\"}";
    char second[] = "{\"kty\":\"EC\",\"kid\":\"two\"}";
    auto firstParsed = ZfJSON::scan(first);
    auto secondParsed = ZfJSON::scan(second);
    JSONValueVec jwks;
    jwks.push(JSONValue{static_cast<const ZfJSON::AnyNode *>(
      (*firstParsed.p<1>())[0].ptr())});
    jwks.push(JSONValue{static_cast<const ZfJSON::AnyNode *>(
      (*secondParsed.p<1>())[0].ptr())});
    encoded.length_(0);
    ZfJSON::save(encoded, JSONValueReply{ZuMv(jwks)});
    ZuCheck(encoded == "{\"items\":[{\"kty\":\"EC\",\"kid\":\"one\"},"
      "{\"kty\":\"EC\",\"kid\":\"two\"}]}");

    char duplicate[] = "{\"id\":1,\"id\":2}";
    auto duplicateParsed = ZfJSON::scan(duplicate);
    ZuCheck(duplicateParsed.p<1>() &&
      !ZfJSON::unique((*duplicateParsed.p<1>())[0]));
  }

  using Fields = ZuFields<Foo>;

  ZuCheck(Fields::N > 0);

  ZtVFmt fmt;
  ZfVFieldArray fields{ZfVFields<Foo>()};
  ZuCheck(fields.length() > 0);
  ZuCheck(ZfVFieldMatcher<Foo>()("int_") == ZfFieldIndex(Foo, int_));
  ZuCheck(ZfVFieldMatcher<Foo>()("int_ranged.x") ==
      ZfFieldIndex(Foo, int_ranged));
  ZuCheck(ZfVFieldMatcher<Foo>()("unknown") < 0);
  ZuCheck((ZfVFieldMatcher<Foo, ZuFacet::Bah>()("int_") ==
      ZuTypeIndex<ZfField(Foo_Bah, int_), ZuFields<Foo, ZuFacet::Bah>>{}));
  ZuCheck(ZfVFieldMatcher<Empty>()("unknown") < 0);

  {
    auto intField = fields[ZfFieldIndex(Foo, int_)];
    auto intRangeField = fields[ZfFieldIndex(Foo, int_ranged)];
    auto floatRangeField = fields[ZfFieldIndex(Foo, float_ranged)];
    ZuCheck(!(intField->props & ZfVFieldProp::Range()));
    ZuCheck(intRangeField->props & ZfVFieldProp::Range());
    ZuCheck(floatRangeField->props & ZfVFieldProp::Range());
    ZuCheck(!(intRangeField->type->props & ZfVFieldProp::Range()));
    ZuCheck(intRangeField->constant.get<ZfFieldTC::Int32>(
	ZfVField::cget(ZfVFieldConstant::Minimum)) == 0);
    ZuCheck(intRangeField->constant.get<ZfFieldTC::Int32>(
	ZfVField::cget(ZfVFieldConstant::Maximum)) == 100);
    ZuCheck(floatRangeField->constant.get<ZfFieldTC::Float>(
	ZfVField::cget(ZfVFieldConstant::Minimum)) == 0.0);
    ZuCheck(floatRangeField->constant.get<ZfFieldTC::Float>(
	ZfVField::cget(ZfVFieldConstant::Maximum)) == 1.0);
  }

  auto vprint = [&fmt](auto &s, const ZfVField &field, int constant) {
    using namespace ZfFieldTC;
    ZuSwitch::dispatch<ZfFieldTC::N>(field.type->code,
	[&s, constant, &field, &fmt](auto Code) {
      field.constant.print<Code>(s, ZfVField::cget(constant), &field, fmt);
    });
  };

  if (verbose) {
    std::cerr << "Foo schema (compile-time)\n";
    ZuUnroll::all<Fields>([]<typename Field>() {
      std::cerr << "  " << Field::id()
	<< ' ' << ZfFieldTC::name(Field::Type::Code)
	<< " deflt=" << typename Field::Type::template Print<>{Field::deflt()}
	<< MinMax<Field>{}
	<< (Field::Type::Code == ZfFieldTC::Bytes ? "" : "\n");
    });
    std::cerr << '\n';

    std::cerr << "Foo schema (run-time)\n";
    for (unsigned i = 0, n = fields.length(); i < n; i++) {
      std::cerr << "  " << fields[i]->id;
      auto type = fields[i]->type;
      std::cerr << ' ' << ZfFieldTC::name(type->code);
      if (type->code == ZfFieldTC::UDT) {
	std::cerr << " udt=" << ZmDemangle_{type->info.udt()->info->name()};
      } else if (type->props & ZfVFieldProp::Enum()) {
	std::cerr << " enum=" << type->info.enum_()->id();
      } else if (type->props & ZfVFieldProp::Flags()) {
	std::cerr << " flags=" << type->info.flags()->id();
      }
      std::cerr << " deflt=";
      vprint(std::cerr, *fields[i], ZfVFieldConstant::Deflt);
      using namespace ZfFieldTC;
      switch (type->code) {
	case Int32:
	case UInt32:
	case Float:
	case Fixed:
	case Decimal:
	  std::cerr << " minimum=";
	  vprint(std::cerr, *fields[i], ZfVFieldConstant::Minimum);
	  std::cerr << " maximum=";
	  vprint(std::cerr, *fields[i], ZfVFieldConstant::Maximum);
	  break;
	case Bytes:
	  continue;
      }
      std::cerr << '\n';
    }
    std::cerr << '\n';
  }

  {
    Foo foo;

    char cow[] = "{ id : \"\\ud83d\\uDC04\" }";
    auto scan = ZfJSON::scan(cow);
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() < 0) return 1;
    using Handler = ZfJSON::As<Foo>::template Handler<Foo, ZuFacet::JSON>;
    ZuCheck((Handler::UpdFields::N));
    ZfJSON::handler<Foo, ZuFacet::JSON>((*scan.p<1>())[0]).update(foo);

    foo.bytesVec = { "xxx", "yyyy", "zzzzz" };

    print<ZuFields_JSON<Foo>>(foo);
    if (verbose) std::cerr << '\n';

    ZtString<> out;
    out << foo;
    ZuCheck(out == "{string:\"\",bytes:,id:\"\xf0\x9f\x90\x84\",int_:0,int_ranged:42,hex:deadbeef,enum_:Normal,daFlags:Bit1,float_:nan,float_ranged:0.42,fixed:nan,decimal:nan,time_:,nested:{i1:0,i2:0},bytesVec:[eHh4,eXl5eQ==,enp6eno=]}");
  }

  {
    Foo foo;

    foo.bytesVec = { "x\xffxx", "y\x80yyy", "z\x00zzzz" };
    foo.time_ = Zm::now();

    ZtString<> json, json2, json3;

    ZfJSON::save<ZuFacet::Bah>(json, foo);

    json2 = json;

    auto scan = ZfJSON::scan(json); // mutates json (unquotes strings, etc.)
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() < 0) return 1;

    auto bar = ZfJSON::handler<Foo, ZuFacet::Bah>((*scan.p<1>())[0]).ctor();

    ZfJSON::save<ZuFacet::Bah>(json3, bar);

    log("JSON: ", json2);

    ZuCheck(json2 == json3);
  }

  {
    // malformed input should be rejected
    char bad[] = "{ id : \"x\" ";
    auto scan = ZfJSON::scan(bad);
    ZuCheck(scan.p<0>() < 0);
  }

  {
    char first[] = "{\"id\":\"one\"}";
    char second[] = "{\"id\":\"two\"}";
    auto scan = ZfJSON::scan(first);
    ZuCheck(scan.p<0>() == int(sizeof(first) - 1));
    auto next = ZfJSON::scan(ZuMv(scan.p<1>()), second);
    ZuCheck(next.p<0>() == int(sizeof(second) - 1));
    ZuCheck(next.p<1>()->data<ZfJSON::NodeArray>().length() == 2);
    auto one = ZfJSON::handler<Foo>((*next.p<1>())[0]).ctor();
    auto two = ZfJSON::handler<Foo>((*next.p<1>())[1]).ctor();
    ZuCheck(one.id == "one");
    ZuCheck(two.id == "two");
  }

  {
    char wrong_[] = "{\"nested\":42}";
    auto wrongScan = ZfJSON::scan(wrong_);
    ZuCheck(wrongScan.p<0>() >= 0);
    auto wrong = ZfJSON::handler<Foo>((*wrongScan.p<1>())[0]).ctor();

    char null_[] = "{\"nested\":null}";
    auto nullScan = ZfJSON::scan(null_);
    ZuCheck(nullScan.p<0>() >= 0);
    auto null = ZfJSON::handler<Foo>((*nullScan.p<1>())[0]).ctor();

    ZuCheck(wrong.nested.i1 == null.nested.i1);
    ZuCheck(wrong.nested.i2 == null.nested.i2);

    char object_[] = "{\"nested\":{}}";
    auto objectScan = ZfJSON::scan(object_);
    ZuCheck(objectScan.p<0>() >= 0);
    auto object = ZfJSON::handler<Foo>((*objectScan.p<1>())[0]).ctor();
    ZuCheck(object.nested.i1 == ZuCmp<int>::null());
    ZuCheck(object.nested.i2 == ZuCmp<int>::null());
  }

  {
    char unknown_[] = "{\"int_ranged-junk\":99}";
    auto unknownScan = ZfJSON::scan(unknown_);
    ZuCheck(unknownScan.p<0>() >= 0);
    auto unknown = ZfJSON::handler<Foo>((*unknownScan.p<1>())[0]).ctor();
    ZuCheck(unknown.int_ranged == 42);

    char minimum_[] = "{\"int_ranged\":0}";
    auto minimumScan = ZfJSON::scan(minimum_);
    ZuCheck(minimumScan.p<0>() >= 0);
    auto minimum = ZfJSON::handler<Foo>((*minimumScan.p<1>())[0]).ctor();
    ZuCheck(minimum.int_ranged == 0);

    char maximum_[] = "{\"int_ranged\":\"100tail\"}";
    auto maximumScan = ZfJSON::scan(maximum_);
    ZuCheck(maximumScan.p<0>() >= 0);
    auto maximum = ZfJSON::handler<Foo>((*maximumScan.p<1>())[0]).ctor();
    ZuCheck(maximum.int_ranged == 100);

    char below_[] = "{\"int_ranged\":-1}";
    auto belowScan = ZfJSON::scan(below_);
    ZuCheck(belowScan.p<0>() >= 0);
    auto below = ZfJSON::handler<Foo>((*belowScan.p<1>())[0]).ctor();
    ZuCheck(below.int_ranged == ZuCmp<int>::null());

    char above_[] = "{\"int_ranged\":\"101\"}";
    auto aboveScan = ZfJSON::scan(above_);
    ZuCheck(aboveScan.p<0>() >= 0);
    auto above = ZfJSON::handler<Foo>((*aboveScan.p<1>())[0]).ctor();
    ZuCheck(above.int_ranged == ZuCmp<int>::null());
  }

  {
    char outside_[] =
      "{\"float_ranged\":1.1,\"fixed\":\"-0.1\",\"decimal\":1.1}";
    auto scan = ZfJSON::scan(outside_);
    ZuCheck(scan.p<0>() >= 0);
    auto outside = ZfJSON::handler<Foo>((*scan.p<1>())[0]).ctor();
    ZuCheck(ZuCmp<double>::null(outside.float_ranged));
    ZuCheck(ZuCmp<ZuFixed>::null(outside.fixed));
    ZuCheck(ZuCmp<ZuDecimal>::null(outside.decimal));
  }

  {
    // missing fields should preserve defaults on update
    Foo foo;
    char partial[] = "{\"int_\":7}";
    auto scan = ZfJSON::scan(partial);
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() >= 0) {
      ZfJSON::handler<Foo, ZuFacet::JSON>((*scan.p<1>())[0]).update(foo);
      ZuCheck(foo.int_ == 0);
      ZuCheck(foo.int_ranged == 42);
      ZuCheck(foo.enum_ == Values::Normal);
    }
  }

  {
    char missing_[] = "{\"kept\":5}";
    auto missingScan = ZfJSON::scan(missing_);
    const auto &missing = (*missingScan.p<1>())[0];
    auto missingValue = ZfJSON::handler<JSONUpdate>(missing).ctor();
    ZuCheck(ZuNull(missingValue.required));
    ZuCheck(missingValue.kept == 5);
    ZuCheck(missingValue.reset == 3);

    JSONUpdate loaded{1, 2, 8};
    ZfJSON::handler<JSONUpdate>(missing).load(loaded);
    ZuCheck(ZuNull(loaded.required));
    ZuCheck(loaded.kept == 5);
    ZuCheck(loaded.reset == 3);

    loaded.reset = 8;
    char update_[] = "{\"required\":4}";
    auto updateScan = ZfJSON::scan(update_);
    ZfJSON::handler<JSONUpdate>((*updateScan.p<1>())[0]).update(loaded);
    ZuCheck(loaded.required == 4);
    ZuCheck(loaded.kept == 5);
    ZuCheck(loaded.reset == 3);

    char reset_[] = "{\"reset\":6}";
    auto resetScan = ZfJSON::scan(reset_);
    ZfJSON::handler<JSONUpdate>((*resetScan.p<1>())[0]).update(loaded);
    ZuCheck(loaded.required == 4);
    ZuCheck(loaded.kept == 5);
    ZuCheck(loaded.reset == 6);
  }

  {
    OptFoo opt;
    ZtString<> json;

    ZfJSON::save(json, opt);
    ZuCheck(json == "{\"req\":\"\",\"tail\":\"tail\"}");
    {
      auto scan = ZfJSON::scan(json);
      ZuCheck(scan.p<0>() >= 0);
      if (scan.p<0>() >= 0) {
	auto in = ZfJSON::handler<OptFoo>((*scan.p<1>())[0]).ctor();
	ZuCheck(!in.head);
	ZuCheck(!in.req[0]);
	ZuCheck(!in.mid);
	ZuCheck(!strcmp(in.tail, "tail"));
      }
    }

    json.length(0);
    opt.head = "head";
    opt.mid = "mid";
    ZfJSON::save(json, opt);
    ZuCheck(json ==
	"{\"head\":\"head\",\"req\":\"\",\"mid\":\"mid\",\"tail\":\"tail\"}");

    json.length(0);
    opt.head = nullptr;
    ZfJSON::save(json, opt);
    ZuCheck(json == "{\"req\":\"\",\"mid\":\"mid\",\"tail\":\"tail\"}");
  }

  {
    BoxFoo box;
    box.values = {BoxInt{0}, BoxInt{7}};

    using Field = ZfField(BoxFoo, value);
    ZtString<> printed;
    printed << typename Field::Type::template Print<>{box.value};
    ZuCheck(!printed);

    ZtString<> json;
    ZfJSON::save(json, box);
    ZuCheck(json == "{\"value\":\"\",\"values\":[\"\",\"7\"]}");

    auto scan = ZfJSON::scan(json);
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() >= 0) {
      auto copy = ZfJSON::handler<BoxFoo>((*scan.p<1>())[0]).ctor();
      ZuCheck(!*copy.value);
      ZuCheck(copy.value.val() == 0);
      ZuCheck(copy.values.length() == 2);
      ZuCheck(!*copy.values[0]);
      ZuCheck(copy.values[0].val() == 0);
      ZuCheck(copy.values[1].val() == 7);
    }
  }

  {
    FmtInt value{uint128_t{0xabcdef}};
    ZtString<> json;
    ZfJSON::save(json, value);
    ZuCheck(json ==
      "{\"value\":\"00000000000000000000000000abcdef\"}");
    auto scan = ZfJSON::scan(json);
    FmtInt loaded;
    ZfJSON::handler<FmtInt>((*scan.p<1>())[0]).load(loaded);
    ZuCheck(loaded.value == value.value);
  }

  {
    char json_[] = "{\"q\\\"\\\\\xf0\x9f\x90\x84\":7}";
    ZuCSpan expected{"{\"q\\\"\\\\\\ud83d\\udc04\":7}"};
    auto scan = ZfJSON::scan(json_);
    ZuCheck(scan.p<0>() >= 0);
    auto map = ZmRef(ZfJSON::handler<IntTree>((*scan.p<1>())[0]).alloc());
    ZuCheck(map);
    ZuCheck(map->count_() == 1);
    ZuCheck(map->findVal("q\"\\\xf0\x9f\x90\x84") == 7);

    ZtString<> json;
    ZfJSON::save(json, map);
    ZuCheck(json == expected);

    alignas(IntTree) uint8_t storage[sizeof(IntTree)];
    ZfJSON::handler<IntTree>((*scan.p<1>())[0]).new_(storage);
    auto placed = reinterpret_cast<IntTree *>(storage);
    ZuCheck(placed);
    ZuCheck(placed->findVal("q\"\\\xf0\x9f\x90\x84") == 7);
    placed->~IntTree();
  }

  {
    IntTreeRef map = new IntTree{};
    map->add("old", 1);
    auto ptr = map.ptr();

    char load_[] = "{\"loaded\":2}";
    auto loadScan = ZfJSON::scan(load_);
    ZfJSON::handler<IntTree>((*loadScan.p<1>())[0]).load(*map);
    ZuCheck(map.ptr() == ptr);
    ZuCheck(map->count_() == 1);
    ZuCheck(map->findVal("loaded") == 2);
    ZuCheck(ZuNull(map->findVal("old")));

    map->add("kept", 3);
    char update_[] = "{\"loaded\":4,\"added\":5}";
    auto updateScan = ZfJSON::scan(update_);
    ZfJSON::handler<IntTree>((*updateScan.p<1>())[0]).update(*map);
    ZuCheck(map.ptr() == ptr);
    ZuCheck(map->count_() == 3);
    ZuCheck(map->findVal("loaded") == 4);
    ZuCheck(map->findVal("kept") == 3);
    ZuCheck(map->findVal("added") == 5);

    auto loadedNull = ZmRef(new IntTree());
    ZfJSON::handler<IntTree>((*loadScan.p<1>())[0]).load(*loadedNull);
    ZuCheck(loadedNull);
    ZuCheck(loadedNull->findVal("loaded") == 2);
    auto updatedNull = ZmRef(new IntTree());
    ZfJSON::handler<IntTree>((*updateScan.p<1>())[0]).update(*updatedNull);
    ZuCheck(updatedNull);
    ZuCheck(updatedNull->findVal("loaded") == 4);
    ZuCheck(updatedNull->findVal("added") == 5);

    char wrong_[] = "[1]";
    auto wrongScan = ZfJSON::scan(wrong_);
    ZfJSON::handler<IntTree>((*wrongScan.p<1>())[0]).load(*map);
    ZuCheck(!map->count_());

    ZtString<> json;
    ZfJSON::save(json, map);
    ZuCheck(json == "{}");
  }

  {
    char hash_[] = "{\"a\":1,\"b\":2}";
    auto hashScan = ZfJSON::scan(hash_);
    auto hash = ZmRef(ZfJSON::handler<IntHash>((*hashScan.p<1>())[0]).alloc());
    ZuCheck(hash);
    ZuCheck(hash->count_() == 2);
    ZuCheck(hash->findVal("a") == 1);
    ZuCheck(hash->findVal("b") == 2);

    auto hashPtr = hash.ptr();
    char hashUpdate_[] = "{\"a\":3,\"c\":4}";
    auto hashUpdateScan = ZfJSON::scan(hashUpdate_);
    ZfJSON::handler<IntHash>((*hashUpdateScan.p<1>())[0]).update(*hash);
    ZuCheck(hash.ptr() == hashPtr);
    ZuCheck(hash->findVal("a") == 3);
    ZuCheck(hash->findVal("b") == 2);
    ZuCheck(hash->findVal("c") == 4);

    char lhash_[] = "{\"a\":1,\"b\":2}";
    auto lhashScan = ZfJSON::scan(lhash_);
    auto lhash = ZmRef(ZfJSON::handler<IntLHash>((*lhashScan.p<1>())[0]).alloc());
    ZuCheck(lhash);
    ZuCheck(lhash->count_() == 2);
    ZuCheck(lhash->findVal("a") == 1);
    ZuCheck(lhash->findVal("b") == 2);

    auto lhashPtr = lhash.ptr();
    char lhashLoad_[] = "{\"c\":4}";
    auto lhashLoadScan = ZfJSON::scan(lhashLoad_);
    ZfJSON::handler<IntLHash>((*lhashLoadScan.p<1>())[0]).load(*lhash);
    ZuCheck(lhash.ptr() == lhashPtr);
    ZuCheck(lhash->count_() == 1);
    ZuCheck(lhash->findVal("c") == 4);

    char duplicate_[] = "{\"a\":1,\"a\":2}";
    auto duplicateScan = ZfJSON::scan(duplicate_);
    auto duplicate = ZmRef(ZfJSON::handler<IntHash>((*duplicateScan.p<1>())[0]).alloc());
    ZuCheck(duplicate->count_() == 2);
  }

  {
    BoolHashRef bools = new BoolHash{};
    bools->add("yes", true);
    StringHashRef strings = new StringHash{};
    strings->add("text", "hello");
    BytesHashRef bytes = new BytesHash{};
    bytes->add("bytes", ZuBSpan{"x"});
    HexHashRef hex = new HexHash{};
    hex->add("hex", 0x2a);

    ZtString<> json;
    ZfJSON::save(json, bools);
    ZuCheck(json == "{\"yes\":true}");
    json.length(0);
    ZfJSON::save(json, strings);
    ZuCheck(json == "{\"text\":\"hello\"}");
    json.length(0);
    ZfJSON::save(json, bytes);
    ZuCheck(json == "{\"bytes\":\"eA==\"}");
    json.length(0);
    ZfJSON::save(json, hex);
    ZuCheck(json == "{\"hex\":\"2a\"}");

    char wrong_[] = "{\"bad\":\"x\"}";
    auto wrongScan = ZfJSON::scan(wrong_);
    auto wrong = ZmRef(ZfJSON::handler<IntHash>((*wrongScan.p<1>())[0]).alloc());
    ZuCheck(ZuNull(wrong->findVal("bad")));
  }

  {
    ObjHashRef objects = new ObjHash{};
    objects->add("object", MapObj{1, 2});
    ArrayHashRef arrays = new ArrayHash{};
    arrays->add("array", IntArray{1, 2});
    TextHashRef strings = new TextHash{};
    strings->add("string", MapText{"hello"});
    IntTreeRef inner = new IntTree{};
    inner->add("value", 7);
    MapHashRef maps = new MapHash{};
    maps->add("map", inner);

    ZtString<> json;
    ZfJSON::save(json, objects);
    ZuCheck(json ==
	"{\"object\":{\"fixed\":1,\"mutable_\":2}}");
    json.length(0);
    ZfJSON::saveUpd(json, objects);
    ZuCheck(json == "{\"object\":{\"mutable_\":2}}");
    json.length(0);
    ZfJSON::save<ZuFacet::Bah>(json, objects);
    ZuCheck(json ==
	"{\"object\":{\"fixed\":1,\"mutable_\":2}}");
    json.length(0);
    ZfJSON::save(json, arrays);
    ZuCheck(json == "{\"array\":[1,2]}");
    json.length(0);
    ZfJSON::save(json, strings);
    ZuCheck(json == "{\"string\":\"hello\"}");
    json.length(0);
    ZfJSON::save(json, maps);
    ZuCheck(json == "{\"map\":{\"value\":7}}");

    char object_[] =
      "{\"object\":{\"fixed\":3,\"mutable_\":4}}";
    auto objectScan = ZfJSON::scan(object_);
    auto loadedObjects = ZmRef(ZfJSON::handler<ObjHash>((*objectScan.p<1>())[0]).alloc());
    auto object = loadedObjects->findVal("object");
    ZuCheck(object.fixed == 3);
    ZuCheck(object.mutable_ == 4);

    char array_[] = "{\"array\":[3,4]}";
    auto arrayScan = ZfJSON::scan(array_);
    auto loadedArrays = ZmRef(ZfJSON::handler<ArrayHash>((*arrayScan.p<1>())[0]).alloc());
    auto array = loadedArrays->findVal("array");
    ZuCheck(array.length() == 2);
    ZuCheck(array[0] == 3);
    ZuCheck(array[1] == 4);

    char string_[] = "{\"string\":\"world\"}";
    auto stringScan = ZfJSON::scan(string_);
    auto loadedStrings = ZmRef(ZfJSON::handler<TextHash>((*stringScan.p<1>())[0]).alloc());
    ZuCheck(loadedStrings->findVal("string").value == "world");

    char map_[] = "{\"map\":{\"value\":9}}";
    auto mapScan = ZfJSON::scan(map_);
    auto loadedMaps = ZmRef(ZfJSON::handler<MapHash>((*mapScan.p<1>())[0]).alloc());
    ZuCheck(loadedMaps->findVal("map")->findVal("value") == 9);
  }

  {
    IntTreeRef map = new IntTree{};
    map->add("value", 7);
    MapHolder holder{map};
    ZtString<> json;
    ZfJSON::save(json, holder);
    ZuCheck(json == "{\"map\":{\"value\":7}}");

    char holder_[] = "{\"map\":{\"value\":8}}";
    auto holderScan = ZfJSON::scan(holder_);
    auto loaded = ZfJSON::handler<MapHolder>((*holderScan.p<1>())[0]).ctor();
    ZuCheck(loaded.map);
    ZuCheck(loaded.map->findVal("value") == 8);

    char null_[] = "{\"map\":null}";
    auto nullScan = ZfJSON::scan(null_);
    auto null = ZfJSON::handler<MapHolder>((*nullScan.p<1>())[0]).ctor();
    ZuCheck(!null.map);
  }

  {
    ZtString<> json, json2, json3;
    UnionHolder holder;
    holder.u = UnionA{ .foo = 42 };
    ZfJSON::save(json, holder);
    json3 = json;
    auto scan = ZuMv((*(ZfJSON::scan(json3).p<1>()))[0]);
    auto loaded = ZfJSON::handler<UnionHolder>(scan).ctor();
    auto node = loaded.u.p<const ZfJSON::AnyNode *>();
    loaded.u = ZfJSON::handler<UnionA>(node).ctor();
    ZfJSON::save(json2, loaded);
    ZuCheck(json == json2);

    char arrayJSON[] = "{\"u\":[1,2,3]}";
    auto arrayScan = ZfJSON::scan(arrayJSON);
    auto arrayHolder =
      ZfJSON::handler<UnionHolder>((*arrayScan.p<1>())[0]).ctor();
    ZuCheck((arrayHolder.u.is<const ZfJSON::AnyNode *>()));
    auto arrayNode = arrayHolder.u.p<const ZfJSON::AnyNode *>();
    ZuCheck(arrayNode && arrayNode->has<ZfJSON::AnyNode::Array>());
    arrayHolder.u = ZfJSON::handler<UnionArray>(arrayNode).ctor();
    ZuCheck(arrayHolder.u.p<UnionArray>().length() == 3);
    ZuCheck(arrayHolder.u.p<UnionArray>()[1] == 2);
    json.length_(0);
    ZfJSON::save(json, arrayHolder);
    ZuCheck(json == "{\"u\":[1,2,3]}");

    char scalarJSON[] = "{\"u\":\"text\"}";
    auto scalarScan = ZfJSON::scan(scalarJSON);
    auto scalarHolder =
      ZfJSON::handler<UnionHolder>((*scalarScan.p<1>())[0]).ctor();
    ZuCheck((scalarHolder.u.is<const ZfJSON::AnyNode *>()));
    auto scalarNode = scalarHolder.u.p<const ZfJSON::AnyNode *>();
    ZuCheck(scalarNode && scalarNode->has<ZfJSON::AnyNode::String>());
    scalarHolder.u = ZfJSON::handler<MapText>(scalarNode).ctor();
    ZuCheck(scalarHolder.u.p<MapText>().value == "text");
    json.length_(0);
    ZfJSON::save(json, scalarHolder);
    ZuCheck(json == "{\"u\":\"text\"}");

    char nullJSON[] = "{\"u\":null}";
    auto nullScan = ZfJSON::scan(nullJSON);
    auto nullHolder =
      ZfJSON::handler<UnionHolder>((*nullScan.p<1>())[0]).ctor();
    ZuCheck((nullHolder.u.is<const ZfJSON::AnyNode *>()));
    auto nullNode = nullHolder.u.p<const ZfJSON::AnyNode *>();
    ZuCheck(nullNode && nullNode->has<ZfJSON::AnyNode::Null>());
    using UnionHandler = ZfJSON::As<decltype(nullHolder.u)>::
      Handler<decltype(nullHolder.u), ZuFacet::JSON>;
    ZuCheck(!UnionHandler::valid(nullptr));

    char numberJSON[] = "{\"u\":42}";
    auto numberScan = ZfJSON::scan(numberJSON);
    auto numberHolder =
      ZfJSON::handler<UnionHolder>((*numberScan.p<1>())[0]).ctor();
    auto numberNode = numberHolder.u.p<const ZfJSON::AnyNode *>();
    ZuCheck(numberNode && numberNode->has<ZfJSON::AnyNode::Number>());
    char boolJSON[] = "{\"u\":true}";
    auto boolScan = ZfJSON::scan(boolJSON);
    auto boolHolder =
      ZfJSON::handler<UnionHolder>((*boolScan.p<1>())[0]).ctor();
    auto boolNode = boolHolder.u.p<const ZfJSON::AnyNode *>();
    ZuCheck(boolNode && boolNode->has<ZfJSON::AnyNode::True>());

    UnionHolder empty;
    json.length_(0);
    ZfJSON::save(json, empty);
    ZuCheck(json == "{\"u\":null}");
  }

  return 0;
}
