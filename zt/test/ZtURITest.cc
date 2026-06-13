//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <string.h>

#include <zlib/ZuID.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtStruct.hh>
#include <zlib/ZtURI.hh>

using namespace ZuTestUtil;

namespace ZtURI {
  ZuTuple<int, int, char> eoc(ZuSpan<char>);
}

namespace Values {
  ZtEnum(Values, int8_t, High, Low, Normal);
}

namespace Flags {
  ZtFlags(Flags, uint8_t, Bit0, Bit1, Bit2);
}

struct Nested {
  int i1 = 0, i2 = 1;

  friend ZtStructPrint ZuPrintType(Nested *);
};

struct NestedJSON {
  int i1 = 2, i2 = 3;

  friend ZtURI::AsJSON ZtURI_Fmt(NestedJSON *);	// use JSON in URI

  friend ZtStructPrint ZuPrintType(NestedJSON *);
};

ZuStructFacet(Bah);

ZtStruct((Nested, Bah),
  (((i1), (Ctor<0>)), (Int32)),
  (((i2), (Ctor<1>)), (Int32)));

ZtStruct((NestedJSON, Bah),
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

  friend ZtStructPrint ZuPrintType(Foo *);
};

ZtStruct(Foo,
  (((string, Rd),	(Ctor<0>)),	(CString, "hello \"world\"")),
  (((bytes),		(Ctor<1>)),	(Bytes, ZuBSpan{"bytes"})),
  (((id),		(Ctor<2>, Mutable)),	(String, "goodbye")),
  (((int_),		(Ctor<3>)),	(Int32)),
  (((int_ranged),	(Ctor<4>)),	(Int32, 42, 0, 100)),
  (((hex),		(Ctor<5>, Hex)),
					(UInt32, 0xdeadbeef)),
  (((enum_),		(Ctor<6>, Enum<Values::Map>)),
					(Int32, Values::Normal)),
  (((flags),		(Ctor<7>, Flags<Flags::Map>)),
					(UInt128, Flags::Bit1())),
  (((float_),		(Ctor<8>)),	(Float)),
  (((float_ranged),	(Ctor<9>)),	(Float, 0.42, 0.0, 1)),
  (((fixed),		(Ctor<10>)),	(Fixed)),
  (((decimal),		(Ctor<11>)),	(Decimal)),
  (((time_),		(Ctor<12>)),	(Time)),
  (((nested),		(Ctor<13>)),	(UDT)),
  (((nestedJSON),	(Ctor<14>)),	(UDT)),
  (((bytesVec),		(Ctor<15>)),	(BytesVec)));

ZtStructRender(Foo, Bah,
  (enum_,	URI::ID<"enum-BAH">),
  (int_,	URI::ID<"int-BAH">,	URI::Number<ZuFmt::Right<9>>),
  (float_,	URI::ID<"float-BAH">,	URI::Number<ZuFmt::FP<4>>),
  (bytes,	URI::ID<"bytes-BAH">,	URI::Escaped),
  (string,	URI::PathIndex<0>),
  (id,		URI::PathIndex<1>),
  int_ranged, hex, flags, float_ranged, fixed, decimal,
  time_, nested, nestedJSON, bytesVec);

ZtURIConfig(Bah, (
  ZtURI_ObjectFmt<ZtURI::Array,
    ZtURI_ArrayFmt<ZtURI::Delimited,
      ZtURI_Annotated<true,
	ZtURI_Wrapped<true>>>>));

void roundTrip()
{
  ZuTestScope(roundTrip);

  char empty[] = "";
  auto scan = ZtURI::scan(empty);
  Foo foo = ZtURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
  foo.int_ = 42;
  foo.float_ = 42.01;
  foo.bytesVec = { "xxx", "yyyy", "zzzzz" };
  foo.time_ = Zm::now();

  ZtString<> uri;
  ZtURI::save<ZuFacet::Bah>(uri, foo);
  log("uri=", uri);

  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&foo]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(foo)} << '\n';
    });
  }

  ZtString<> uri_ = uri;
  scan = ZtURI::scan(uri);
  ZuCheck(scan.p<0>() > 0);
  ZuCheck(scan.p<1>());
  if (!scan.p<1>()) return;

  Foo bar = ZtURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
  if (verbose) {
    ZuUnroll::all<ZuFields<Foo>>([&bar]<typename T>() mutable {
      std::cerr
	<< T::id() << '='
	<< typename T::Type::template Print<ZtFmt::Default>{T::get(bar)} << '\n';
    });
  }

  ZtString<> uri2;
  ZtURI::save<ZuFacet::Bah>(uri2, bar);
  log("uri2=", uri2);
  ZuCheck(uri_ == uri2);
}

void malformedURINegatives()
{
  ZuTestScope(malformedURINegatives);

  char badPct[] = "/foo%zz";
  auto scanBadPct = ZtURI::scan(badPct);
  ZuCheck(scanBadPct.p<0>() < 0);

  char badEnd[] = "/foo%";
  auto scanBadEnd = ZtURI::scan(badEnd);
  ZuCheck(scanBadEnd.p<0>() < 0);
}

void reservedCharRoundTrip()
{
  ZuTestScope(reservedCharRoundTrip);

  char empty[] = "";
  auto scan = ZtURI::scan(empty);
  Foo foo = ZtURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();

  foo.string = "a/b?c#d";
  foo.id = "x y%z";
  foo.bytes = "raw+bytes";

  ZtString<> uri;
  ZtURI::save<ZuFacet::Bah>(uri, foo);
  ZtString<> uri_ = uri;

  auto scan2 = ZtURI::scan(uri);
  ZuCheck(scan2.p<0>() > 0);
  ZuCheck(scan2.p<1>());
  if (!scan2.p<1>()) return;

  Foo bar = ZtURI::handler<Foo, ZuFacet::Bah>(scan2.template p<1>()).ctor();
  ZtString<> uri2;
  ZtURI::save<ZuFacet::Bah>(uri2, bar);
  ZuCheck(uri2.length() > 0);
}

void percentPolicies()
{
  ZuTestScope(percentPolicies);

  {
    ZtString<> body;
    ZtURI::URIQuote<true>::quote(body, "a b");
    ZuCheck(body == "a+b");
  }
  {
    char query[] = "foo%20bar&";
    auto r = ZtURI::eos(query);
    ZuCheck(r.p<0>() == 7 && r.p<1>() == 10 && r.p<2>() == '&');
    ZuCheck(ZuCSpan(query, 7) == "foo bar");
  }
  {
    char path[] = "a%2Fb/c";
    auto r = ZtURI::eoc(path);
    ZuCheck(r.p<0>() == 3 && r.p<1>() == 6 && r.p<2>() == '/');
    ZuCheck(ZuCSpan(path, 3) == "a/b");
  }
  {
    char path[] = "a/b";
    auto r = ZtURI::eoc(path);
    ZuCheck(r.p<0>() == 1 && r.p<1>() == 2 && r.p<2>() == '/');
    ZuCheck(ZuCSpan(path, 1) == "a");
  }
  {
    char empty[] = "";
    auto scan = ZtURI::scan(empty);
    Foo foo = ZtURI::handler<Foo, ZuFacet::Bah>(scan.template p<1>()).ctor();
    foo.string = "a<c";

    ZtString<> uri;
    ZtURI::save<ZuFacet::Bah>(uri, foo);
    ZuCheck(strstr(uri.data(), "%3C"));
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roundTrip);
  ZuTestCall(malformedURINegatives);
  ZuTestCall(reservedCharRoundTrip);
  ZuTestCall(percentPolicies);
  return 0;
}
