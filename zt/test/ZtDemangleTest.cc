//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuVArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtDemangle.hh>

using namespace ZuTestUtil;

template <ZuArray> struct A { };
template <auto> struct B { };
template <ZuString> struct D { };

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZtDemangle::init();

  constexpr auto foo = ZuDefaultAxor();
  using Foo_ = decltype(foo);
  log(ZuDemangle_{"Z1XvEUlTyOT_E_"});
  log(ZuDemangle_{"Z1XvEUlOT_E_"});
  log("raw: ", typeid(foo).name());
  log(ZuDemangle<Foo_>{});
  log(ZuDemangle<Foo<Baz, Baz<int>>>{});

  ZuCArray<512> s;

  {
    ZuVArray<ZuBSpan> a;
    (s = {}) << ZuDemangle<decltype(a)>{};
    log(s);
    ZuCheck(ZuMatcher<"ZuVArray_::Array<ZuSpan">().find(s).p<1>() == 0);
    (s = {}) << ZuDemangle<typename ZuTraits<decltype(a)>::Elem>{};
    log(s);
    ZuCheck(ZuMatcher<"ZuVArray_::Elem<ZuVArray_::Array<ZuSpan">().find(s).p<1>() == 0);
  }
  {
    (s = {}) << ZuDemangle<A<"foobar">>{};
    log(s);
    ZuCheck(s == "A<\"foobar\">");
    (s = {}) << ZuDemangle<A<ZuArray({1,2,3})>>{};
    log(s);
    ZuCheck(s == "A<ZuArray<int>({1,2,3})>");
    (s = {}) << ZuDemangle<A<"a\"b\\c\n">>{};
    log(s);
    ZuCheck(s == "A<\"a\\\"b\\\\c\\n\">");
    (s = {}) << ZuDemangle<ZuTuple<A<"foo">, A<"bar">>>{};
    log(s);
    ZuCheck(ZuMatcher<"A<\"foo\">">().find(s).p<1>() == 0);
    ZuCheck(ZuMatcher<"A<\"bar\">">().find(s).p<1>() == 0);
    (s = {}) << ZuDemangle<B<[]{ return "foobar"; }>>{};
    log(s);
    ZuCheck(ZuMatcher<"B<main">().find(s).p<1>() == 0);
    (s = {}) << ZuDemangle<B<"foobar"_Zu>>{};
    log(s);
    ZuCheck(s == "B<\"foobar\">");
    (s = {}) << ZuDemangle<D<"foobar">>{};
    log(s);
    ZuCheck(s == "D<\"foobar\">");
    (s = {}) << ZuDemangle<decltype(ZuMatcher<"foo", "bar", "baz", "bah">())>{};
    log(s);
    ZuCheck(ZuMatcher<"ZuStringTL<\"foo\", \"bar\", \"baz\", \"bah\">">().find(s).p<1>() == 0);
  }
}
