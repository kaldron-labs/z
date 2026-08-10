//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmDemangle.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfJSON.hh>

using namespace ZuTestUtil;

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

#define Nested_Struct(...) ZfStruct((Nested, JSON, Bah) __VA_OPT__(, __VA_ARGS__))

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

ZfStruct((Foo, JSON),
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

ZfStructRender(Foo, Bah,
  string,
  (bytes,	JSON::ID<"bytes-BAH">, JSON::Raw),
  id,
  (int_,	JSON::ID<"int-BAH">, JSON::Number<ZuFmt::Right<9>>),
  int_ranged, hex,
  (enum_,	JSON::ID<"enum-BAH">),
  daFlags,
  (float_,	JSON::ID<"float-BAH">, JSON::Number<ZuFmt::FP<4>>),
  float_ranged, fixed, decimal, time_, nested, bytesVec);

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

ZfStruct((BoxFoo, JSON),
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
ZfStruct(Bazz,
  (((baz), (Ctor<0>)), (UDT)),
  (((bazArray), (Ctor<1>)), (UDT)));

struct OptFoo {
  const char *head = nullptr;
  const char *req = "";
  const char *mid = nullptr;
  const char *tail = "tail";
};
ZfStruct((OptFoo, JSON),
  (((head),	(Ctor<0>, JSON::Opt)),	(CString)),
  (((req),	(Ctor<1>)),		(CString)),
  (((mid),	(Ctor<2>, JSON::Opt)),	(CString)),
  (((tail),	(Ctor<3>)),		(CString)));

struct JSONUpdate {
  int required;
  int kept;
  int reset;
};
ZfStruct((JSONUpdate, JSON),
  (((required), (Ctor<0>, Mutable, Required)), (Int32)),
  (((kept),     (Ctor<1>, Mutable)),           (Int32, 2)),
  (((reset),    (Ctor<2>, Mutable, Reset)),    (Int32, 3)));

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  using Fields = ZuFields<Foo>;

  ZuCheck(Fields::N > 0);

  ZtVFmt fmt;
  ZfVFieldArray fields{ZfVFields<Foo>()};
  ZuCheck(fields.length() > 0);

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

  return 0;
}
