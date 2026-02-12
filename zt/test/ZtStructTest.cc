//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmDemangle.hh>

#include <zlib/ZtStruct.hh>
#include <zlib/ZtJSON.hh>

using namespace ZuTestUtil;

namespace Values {
  ZtEnum(Values, int8_t, High, Low, Normal);
}

namespace Flags {
  ZtFlags(Flags, uint8_t, Bit0, Bit1, Bit2);
}

ZuStructFacet(Bah);

struct Nested {
  int i1 = 0, i2 = 0;

  friend ZtStructPrint ZuPrintType(Nested *);
};

#define Nested_Fields(macro, ...) macro( \
  (((i1), (Ctor<0>)), (Int32)), \
  (((i2), (Ctor<1>)), (Int32)), __VA_OPT__(, __VA_ARGS__))

#define Nested_Struct(...) ZtStruct((Nested, JSON, Bah), __VA_ARGS__)

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

  friend ZtStructPrint ZuPrintType(Foo *);
};

ZtStruct((Foo, JSON),
  (((string, Rd),	(Ctor<0>)),	(CString, "hello \"world\"")),
  (((bytes),		(Ctor<1>)),	(Bytes, ZuBSpan{"bytes"})),
  (((id),		(Ctor<2>, Mutable)),	(String, "goodbye")),
  (((int_),		(Ctor<3>)),	(Int32)),
  (((int_ranged),	(Ctor<4>)),	(Int32, 42, 0, 100)),
  (((hex),		(Ctor<5>, Hex)),
    					(UInt32, 0xdeadbeef)),
  (((enum_),		(Ctor<6>, Enum<Values::Map>)),
    					(Int32, Values::Normal)),
  (((daFlags),		(Ctor<7>, Flags<Flags::Map>)),
    					(UInt128, Flags::Bit1())),
  (((float_),		(Ctor<8>)),	(Float)),
  (((float_ranged),	(Ctor<9>)),	(Float, 0.42, 0.0, 1)),
  (((fixed),		(Ctor<10>)),	(Fixed)),
  (((decimal),		(Ctor<11>)),	(Decimal)),
  (((time_),		(Ctor<12>)),	(Time)),
  (((nested),		(Ctor<13>)),	(UDT)),
  (((bytesVec),		(Ctor<14>)),	(BytesVec)));

ZtStructRender(Foo, Bah,
  string,
  (bytes,	JSON::ID<"bytes-BAH">, JSON::Raw),
  id,
  (int_,	JSON::ID<"int-BAH">, JSON::Number<ZuFmt::Right<9>>),
  int_ranged, hex,
  (enum_,	JSON::ID<"enum-BAH">),
  daFlags,
  (float_,	JSON::ID<"float-BAH">, JSON::Number<ZuFmt::FP<4>>),
  float_ranged, fixed, decimal, time_, nested, bytesVec);

template <typename T, typename = void>
struct MinMax {
  template <typename S>
  friend inline S &operator <<(S &s, const MinMax &) { return s; }
};
template <typename T>
struct MinMax<T, decltype(T::minimum(), void())> {
  template <typename S>
  friend inline S &operator <<(S &s, const MinMax &m) {
    s << " minimum=" << typename T::template Print_<>{T::minimum()}
      << " maximum=" << typename T::template Print_<>{T::maximum()};
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
  friend ZtJSON::AsString ZtJSON_Fmt(Baz *);
};
struct BazArray : public ZtArray<Baz> {
  ZuDerive_(BazArray, ZtArray<Baz>);
  friend ZtJSON::AsArray<ZtFieldTC::UDT> ZtJSON_Fmt(BazArray *);
};
struct Bazz {
  Baz baz;
  BazArray bazArray;
};
ZtStruct(Bazz,
  (((baz), (Ctor<0>)), (UDT)),
  (((bazArray), (Ctor<1>)), (UDT)));

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  using Fields = ZuFields<Foo>;

  ZuCheck(Fields::N > 0);

  ZtVFmt fmt;
  ZtVFieldArray fields{ZtVFields<Foo>()};
  ZuCheck(fields.length() > 0);

  auto vprint = [&fmt](auto &s, const ZtVField &field, int constant) {
    using namespace ZtFieldTC;
    ZuSwitch::dispatch<ZtFieldTC::N>(field.type->code,
	[&s, constant, &field, &fmt](auto Code) {
      field.constant.print<Code>(s, ZtVField::cget(constant), &field, fmt);
    });
  };

  if (verbose) {
    std::cerr << "Foo schema (compile-time)\n";
    ZuUnroll::all<Fields>([]<typename Field>() {
      std::cerr << "  " << Field::id()
	<< ' ' << ZtFieldTC::name(Field::Type::Code)
	<< " deflt=" << typename Field::Type::template Print<>{Field::deflt()}
	<< MinMax<Field>{}
	<< (Field::Type::Code == ZtFieldTC::Bytes ? "" : "\n");
    });
    std::cerr << '\n';

    std::cerr << "Foo schema (run-time)\n";
    for (unsigned i = 0, n = fields.length(); i < n; i++) {
      std::cerr << "  " << fields[i]->id;
      auto type = fields[i]->type;
      std::cerr << ' ' << ZtFieldTC::name(type->code);
      if (type->code == ZtFieldTC::UDT) {
	std::cerr << " udt=" << ZmDemangle_{type->info.udt()->info->name()};
      } else if (type->props & ZtVFieldProp::Enum()) {
	std::cerr << " enum=" << type->info.enum_()->id();
      } else if (type->props & ZtVFieldProp::Flags()) {
	std::cerr << " flags=" << type->info.flags()->id();
      }
      std::cerr << " deflt=";
      vprint(std::cerr, *fields[i], ZtVFieldConstant::Deflt);
      using namespace ZtFieldTC;
      switch (type->code) {
	case Int32:
	case UInt32:
	case Float:
	case Fixed:
	case Decimal:
	  std::cerr << " minimum=";
	  vprint(std::cerr, *fields[i], ZtVFieldConstant::Minimum);
	  std::cerr << " maximum=";
	  vprint(std::cerr, *fields[i], ZtVFieldConstant::Maximum);
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
    auto scan = ZtJSON::scan(cow);
    ZuCheck(scan.template p<0>() >= 0);
    if (scan.template p<0>() < 0) return 1;
    using Handler = ZtJSON::As<Foo>::template Handler<Foo, ZuFacet::JSON>;
    ZuCheck((Handler::UpdFields::N));
    ZtJSON::handler<Foo, ZuFacet::JSON>(scan.template p<1>()).update(foo);

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

    ZtJSON::save<ZuFacet::Bah>(json, foo);

    json2 = json;

    auto scan = ZtJSON::scan(json); // mutates json (unquotes strings, etc.)
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() < 0) return 1;

    auto bar = ZtJSON::handler<Foo, ZuFacet::Bah>(scan.p<1>()).ctor();

    ZtJSON::save<ZuFacet::Bah>(json3, bar);

    log("JSON: ", json2);

    ZuCheck(json2 == json3);
  }

  {
    // malformed input should be rejected
    char bad[] = "{ id : \"x\" ";
    auto scan = ZtJSON::scan(bad);
    ZuCheck(scan.p<0>() < 0);
  }

  {
    // missing fields should preserve defaults on update
    Foo foo;
    char partial[] = "{\"int_\":7}";
    auto scan = ZtJSON::scan(partial);
    ZuCheck(scan.p<0>() >= 0);
    if (scan.p<0>() >= 0) {
      ZtJSON::handler<Foo, ZuFacet::JSON>(scan.p<1>()).update(foo);
      ZuCheck(foo.int_ == 0);
      ZuCheck(foo.int_ranged == 42);
      ZuCheck(foo.enum_ == Values::Normal);
    }
  }
  return 0;
}
