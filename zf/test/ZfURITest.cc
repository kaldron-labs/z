//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <string.h>

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfURI.hh>

using namespace ZuTestUtil;

namespace ZfURI {
  ZuTuple<int, int, char> eoc(ZuSpan<char>);
}

ZtEnumNS(, Values, int8_t, High, Low, Normal);

namespace Flags {
  ZtFlags(, Flags, uint8_t, Bit0, Bit1, Bit2);
}

ZtEnumImplNS(Values);
ZtEnumImplNS(Flags);

struct Nested {
  int i1 = 0, i2 = 1;

  friend ZfStructPrint ZuPrintType(Nested *);
};

struct NestedJSON {
  int i1 = 2, i2 = 3;

  friend ZfURI::AsJSON ZfURI_Fmt(NestedJSON *);	// use JSON in URI

  friend ZfStructPrint ZuPrintType(NestedJSON *);
};

ZuStructFacet(Bah);

struct Scalar {
  Scalar() = default;
  Scalar(ZuCSpan value_) : value{value_} { }

  ZtString<>	value;

  template <typename S>
  friend S &operator <<(S &s, const Scalar &v) {
    s << v.value;
    return s;
  }
};

struct ScalarArgs {
  Scalar scalar;
};

ZfStruct((ScalarArgs, Bah),
  (((scalar), (Ctor<0>)), (UDT)));

ZfStruct((Nested, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

ZfStruct((NestedJSON, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

struct Blur : public ZtArray<ZtArray<uint8_t>> {
  ZuDerive_(Blur, ZtArray<ZtArray<uint8_t>>)
  template <typename T>
  Blur(T &&v) {
    this->Base::~Base();
    new (this) Base(ZuFwd<T>(v));
  }
};

struct IntArray : public ZtArray<int> {
  ZuDerive_(IntArray, ZtArray<int>);
  friend ZfURI::AsArray<ZfFieldTC::Int32> ZfURI_Fmt(IntArray *);
};

struct ArrayOpt {
  IntArray values;
};

ZfStruct((ArrayOpt, URI),
  (((values), (Ctor<0>)), (UDT)));

struct Foo {
  const char *string = nullptr;
  ZtArray<uint8_t> bytes;
  ZuID id = "goodbye";
  int int_ = 0;
  int int_ranged = 42;
  unsigned hex = 0xdeadbeef;
  int enum_ = Values::Normal;
  uint128_t flags = Flags::Bit1();
  double float_ = ZuCmp<double>::null();
  double float_ranged = 0.42;
  ZuFixed fixed;
  ZuDecimal decimal;
  ZuTime time_;
  Nested nested;
  NestedJSON nestedJSON;
  Blur bytesVec;

  friend ZfStructPrint ZuPrintType(Foo *);
};

ZfStruct(Foo,
  (((string, Rd),	(Ctor<0>)),	(CString, "hello \"world\"")),
  (((bytes),		(Ctor<1>)),	(Bytes, ZuBSpan{"bytes"})),
  (((id),		(Ctor<2>, Mutable)),	(String, "goodbye")),
  (((int_),		(Ctor<3>)),	(Int32)),
  (((int_ranged),	(Ctor<4>, (Range<0, 100>))),	(Int32, 42)),
  (((hex),		(Ctor<5>, Hex)),
					(UInt32, 0xdeadbeef)),
  (((enum_),		(Ctor<6>, Enum<Values::Map>)),
					(Int32, Values::Normal)),
  (((flags),		(Ctor<7>, Flags<Flags::Map>)),
					(UInt128, Flags::Bit1())),
  (((float_),		(Ctor<8>)),	(Float)),
  (((float_ranged),	(Ctor<9>, (Range<0.0, 1>))), (Float, 0.42)),
  (((fixed),		(Ctor<10>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))),		(Fixed)),
  (((decimal),		(Ctor<11>,
      (Range<ZuDecimal{0}, ZuDecimal{1}>))),	(Decimal)),
  (((time_),		(Ctor<12>)),	(Time)),
  (((nested),		(Ctor<13>)),	(UDT)),
  (((nestedJSON),	(Ctor<14>)),	(UDT)),
  (((bytesVec),		(Ctor<15>)),	(BytesVec)));

ZfStructRender(Foo, Bah,
  (enum_,	URI::ID<"enum-BAH">),
  (int_,	URI::ID<"int-BAH">,	URI::Number<ZuFmt::Right<9>>),
  (float_,	URI::ID<"float-BAH">,	URI::Number<ZuFmt::FP<4>>),
  (bytes,	URI::ID<"bytes-BAH">,	URI::Escaped),
  (string,	URI::PathIndex<0>),
  (id,		URI::PathIndex<1>),
  int_ranged, hex, flags, float_ranged, fixed, decimal,
  time_, nested, nestedJSON, bytesVec);

ZfURIConfig(Bah, (
  ZfURI_ObjectFmt<ZfURI::Array,
    ZfURI_ArrayFmt<ZfURI::Delimited,
      ZfURI_Annotated<true,
	ZfURI_Wrapped<true>>>>));

void roundTrip()
{
  ZuTestScope(roundTrip);

  char empty[] = "";
  auto scan = ZfURI::scan(empty);
  Foo foo = ZfURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
  foo.int_ = 42;
  foo.float_ = 42.01;
  foo.bytesVec = { "xxx", "yyyy", "zzzzz" };
  foo.time_ = Zm::now();

  ZtString<> uri;
  ZfURI::save<ZuFacet::Bah>(uri, foo);
  log("uri=", uri);

  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&foo]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(foo)} << '\n';
    });
  }

  ZtString<> uri_ = uri;
  scan = ZfURI::scan(uri);
  ZuCheck(scan.p<0>() > 0);
  ZuCheck(scan.p<1>());
  if (!scan.p<1>()) return;

  Foo bar = ZfURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&bar]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(bar)} << '\n';
    });
  }

  ZtString<> uri2;
  ZfURI::save<ZuFacet::Bah>(uri2, bar);
  log("uri2=", uri2);
  ZuCheck(uri_ == uri2);
}

void fieldlessUDT()
{
  ZuTestScope(fieldlessUDT);

  ScalarArgs value{Scalar{"hello world"}};
  ZtString<> uri;
  ZfURI::save<ZuFacet::Bah>(uri, value);
  auto scan = ZfURI::scan(uri);
  ZuCheck(scan.p<0>() == int(uri.length()));
  auto loaded =
    ZfURI::handler<ScalarArgs, ZuFacet::Bah>(scan.p<1>()).ctor();
  ZuCheck(loaded.scalar.value == "hello world");
}

void arraySave()
{
  ZuTestScope(arraySave);

  ArrayOpt value;
  value.values.push(1);
  value.values.push(2);
  ZtString<> uri;
  ZfURI::save(uri, value);
  ZuCheck(uri == "?values[0]=1&values[1]=2");
}

void malformedURINegatives()
{
  ZuTestScope(malformedURINegatives);

  char badPct[] = "/foo%zz";
  auto scanBadPct = ZfURI::scan(badPct);
  ZuCheck(scanBadPct.p<0>() < 0);

  char badEnd[] = "/foo%";
  auto scanBadEnd = ZfURI::scan(badEnd);
  ZuCheck(scanBadEnd.p<0>() < 0);
}

void integerRange()
{
  ZuTestScope(integerRange);

  char minimum_[] = "?int_ranged=0";
  auto minimumScan = ZfURI::scan(minimum_);
  auto minimum = ZfURI::handler<Foo, ZuFacet::Bah>(minimumScan.p<1>()).ctor();
  ZuCheck(minimum.int_ranged == 0);

  char maximum_[] = "?int_ranged=100tail";
  auto maximumScan = ZfURI::scan(maximum_);
  auto maximum = ZfURI::handler<Foo, ZuFacet::Bah>(maximumScan.p<1>()).ctor();
  ZuCheck(maximum.int_ranged == 100);

  char below_[] = "?int_ranged=-1";
  auto belowScan = ZfURI::scan(below_);
  auto below = ZfURI::handler<Foo, ZuFacet::Bah>(belowScan.p<1>()).ctor();
  ZuCheck(below.int_ranged == ZuCmp<int>::null());

  char above_[] = "?int_ranged=101";
  auto aboveScan = ZfURI::scan(above_);
  auto above = ZfURI::handler<Foo, ZuFacet::Bah>(aboveScan.p<1>()).ctor();
  ZuCheck(above.int_ranged == ZuCmp<int>::null());
}

void realRange()
{
  ZuTestScope(realRange);

  char outside_[] = "?float_ranged=1.1&fixed=-0.1&decimal=1.1";
  auto scan = ZfURI::scan(outside_);
  auto outside = ZfURI::handler<Foo, ZuFacet::Bah>(scan.p<1>()).ctor();
  ZuCheck(ZuCmp<double>::null(outside.float_ranged));
  ZuCheck(ZuCmp<ZuFixed>::null(outside.fixed));
  ZuCheck(ZuCmp<ZuDecimal>::null(outside.decimal));
}

void reservedCharRoundTrip()
{
  ZuTestScope(reservedCharRoundTrip);

  char empty[] = "";
  auto scan = ZfURI::scan(empty);
  Foo foo = ZfURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();

  foo.string = "a/b?c#d";
  foo.id = "x y%z";
  foo.bytes = "raw+bytes";

  ZtString<> uri;
  ZfURI::save<ZuFacet::Bah>(uri, foo);
  ZtString<> uri_ = uri;

  auto scan2 = ZfURI::scan(uri);
  ZuCheck(scan2.p<0>() > 0);
  ZuCheck(scan2.p<1>());
  if (!scan2.p<1>()) return;

  Foo bar = ZfURI::handler<Foo, ZuFacet::Bah>(scan2.template p<1>()).ctor();
  ZtString<> uri2;
  ZfURI::save<ZuFacet::Bah>(uri2, bar);
  ZuCheck(uri2.length() > 0);
}

void percentPolicies()
{
  ZuTestScope(percentPolicies);

  {
    ZtString<> body;
    ZfURI::URIQuote<true>::quote(body, "a b");
    ZuCheck(body == "a+b");
  }
  {
    ZtString<> path;
    ZfURI::PathQuote::quote(path, "a b?#/");
    ZuCheck(path == "a%20b%3F%23%2F");
  }
  {
    char query[] = "foo%20bar&";
    auto r = ZfURI::eos(query);
    ZuCheck(r.p<0>() == 7 && r.p<1>() == 10 && r.p<2>() == '&');
    ZuCheck(ZuCSpan(query, 7) == "foo bar");
  }
  {
    char path[] = "a%2Fb/c";
    auto r = ZfURI::eoc(path);
    ZuCheck(r.p<0>() == 3 && r.p<1>() == 6 && r.p<2>() == '/');
    ZuCheck(ZuCSpan(path, 3) == "a/b");
  }
  {
    char path[] = "a/b";
    auto r = ZfURI::eoc(path);
    ZuCheck(r.p<0>() == 1 && r.p<1>() == 2 && r.p<2>() == '/');
    ZuCheck(ZuCSpan(path, 1) == "a");
  }
  {
    char empty[] = "";
    auto scan = ZfURI::scan(empty);
    Foo foo = ZfURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
    foo.string = "a<c";

    ZtString<> uri;
    ZfURI::save<ZuFacet::Bah>(uri, foo);
    ZuCheck(strstr(uri.data(), "%3C"));
  }
}

void arrayCoercion()
{
  ZuTestScope(arrayCoercion);

  char data[] = "[a,b]";
  ZuPtr<ZfURI::AnyNode> node = ZfURI::newNode<ZfURI::AnyNode::String>(
    ZuSpan<char>{data, sizeof(data) - 1});
  using Vec = ZfURI::LoadVec<
    ZuFacet::Bah, ZfFieldFilter::Load, ZfFieldTC::CString,
    ZuTypeList<>, ZuCSpan>;
  ZfURI::asArray<ZfURI::Config<ZuFacet::Bah>>(node);
  const auto &array = node->data<ZfURI::AnyNode::Array>();
  Vec a{array};
  Vec b{array};

  ZuCheck(node->has<ZfURI::AnyNode::Array>());
  ZuCheck(data[2] == 0 && data[4] == 0);
  ZuCheck(a.length() == 2);
  ZuCheck(a.get(0) == "a");
  ZuCheck(a.get(1) == "b");
  ZuCheck(b.length() == 2);
  ZuCheck(b.get(0) == "a");
  ZuCheck(b.get(1) == "b");
}

void delimitedLoad()
{
  ZuTestScope(delimitedLoad);

  char uri[] = "?bytesVec[]=[eHh4,eXl5eQ,enp6eno]";
  auto scan = ZfURI::scan(uri);
  ZuCheck(scan.p<0>() > 0);
  auto &root = scan.p<1>();
  ZuCheck(root && root->has<ZfURI::AnyNode::Object>());
  if (!root || !root->has<ZfURI::AnyNode::Object>()) return;
  auto field = root->data<ZfURI::AnyNode::Object>().find("bytesVec");
  ZuCheck(field && field->val()->has<ZfURI::AnyNode::String>());

  Foo foo = ZfURI::handler<Foo, ZuFacet::Bah>(root).ctor();
  ZuCheck(field->val()->has<ZfURI::AnyNode::Array>());
  ZuCheck(foo.bytesVec.length() == 3);
  ZuCheck(foo.bytesVec[0] == ZuBSpan{"xxx"});
  ZuCheck(foo.bytesVec[1] == ZuBSpan{"yyyy"});
  ZuCheck(foo.bytesVec[2] == ZuBSpan{"zzzzz"});
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roundTrip);
  ZuTestCall(fieldlessUDT);
  ZuTestCall(arraySave);
  ZuTestCall(malformedURINegatives);
  ZuTestCall(integerRange);
  ZuTestCall(realRange);
  ZuTestCall(reservedCharRoundTrip);
  ZuTestCall(percentPolicies);
  ZuTestCall(arrayCoercion);
  ZuTestCall(delimitedLoad);
  return 0;
}
