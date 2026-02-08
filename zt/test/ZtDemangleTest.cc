//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZuTest.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuVArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZtDemangle.hh>

// FIXME - add String, Array, Tuple, Union transforms to ZtDemangle.cc

#include <iostream>

template <ZuArray> struct A { };
template <auto> struct B { };
template <ZuString> struct D { };

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

int main()
{
  ZuTestMain();

  ZtDemangle::init();

  constexpr auto foo = ZuDefaultAxor();
  using Foo_ = decltype(foo);
  std::cerr << ZuDemangle_{"Z1XvEUlTyOT_E_"} << '\n';
  std::cerr << ZuDemangle_{"Z1XvEUlOT_E_"} << '\n';
  std::cerr << "raw: " << typeid(foo).name() << '\n';
  std::cerr << ZuDemangle<Foo_>{} << '\n';
  std::cerr << ZuDemangle<Foo<Baz, Baz<int>>>{} << '\n';

  {
    ZuVArray<ZuBSpan> a;
    ZuCArray<512> s;
    s << ZuDemangle<decltype(a)>{};
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"ZuVArray_::Array<ZuSpan">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<typename ZuTraits<decltype(a)>::Elem>{};
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"ZuVArray_::Elem<ZuVArray_::Array<ZuSpan">().find(s).p<1>() == 0);
  }
  {
    using A_ = A<"foobar">;
    using B_ = B<[]{ return "foobar"; }>;
    using C_ = B<"foobar"_Zu>;
    using D_ = D<"foobar">;
    using E_ = A<"a\"b\\c\n">;
    using F_ = ZuTuple<A<"foo">, A<"bar">>;
    ZuCArray<512> s;
    s << ZuDemangle<A_>{};
    ZuCheck(s == "A<\"foobar\">");
    s = {}; s << ZuDemangle<E_>{};
    std::cerr << s << '\n';
    ZuCheck(s == "A<\"a\\\"b\\\\c\\n\">");
    s = {}; s << ZuDemangle<F_>{};
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"A<\"foo\">">().find(s).p<1>() == 0);
    ZuCheck(ZuMatcher<"A<\"bar\">">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<B_>{};
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"B<main">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<C_>{};
    std::cerr << typeid(C_).name() << '\n';
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"B<ZuString">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<D_>{};
    std::cerr << s << '\n';
    ZuCheck(ZuMatcher<"D<ZuString">().find(s).p<1>() == 0);
  }
}
