//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuVArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZtRegex.hh>
#include <zlib/ZtDemangle.hh>

bool verbose = false;

template <typename ...Args>
static void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}
template <typename ...Args>
static void log(Args &&...args) {
  if (verbose) log_(ZuFwd<Args>(args)...);
}
#define CHECK(x, ...) ZuCheck(x, log_(__VA_ARGS__))

template <ZuArray> struct A { };
template <auto> struct B { };
template <ZuString> struct D { };

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

static void usage()
{
  std::cerr << "usage: ZuBoxTest [-v]\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    if (strcmp(argv[1], "-v")) usage();
    verbose = true;
  }

  ZuTestMain();

  ZtDemangle::init();

  constexpr auto foo = ZuDefaultAxor();
  using Foo_ = decltype(foo);
  log(ZuDemangle_{"Z1XvEUlTyOT_E_"});
  log(ZuDemangle_{"Z1XvEUlOT_E_"});
  log("raw: ", typeid(foo).name());
  log(ZuDemangle<Foo_>{});
  log(ZuDemangle<Foo<Baz, Baz<int>>>{});

  {
    ZuVArray<ZuBSpan> a;
    ZuCArray<512> s;
    s << ZuDemangle<decltype(a)>{};
    log(s);
    ZuCheck(ZuMatcher<"ZuVArray_::Array<ZuSpan">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<typename ZuTraits<decltype(a)>::Elem>{};
    log(s);
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
    ZuCheck(s == "A<\"a\\\"b\\\\c\\n\">");
    s = {}; s << ZuDemangle<F_>{};
    ZuCheck(ZuMatcher<"A<\"foo\">">().find(s).p<1>() == 0);
    ZuCheck(ZuMatcher<"A<\"bar\">">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<B_>{};
    ZuCheck(ZuMatcher<"B<main">().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<C_>{};
    ZuCheck(s == "B<\"foobar\">");
    s = {}; s << ZuDemangle<D_>{};
    ZuCheck(s == "D<\"foobar\">");
    s = {}; s << ZuDemangle<A<ZuArray({1,2,3})>>{};
    ZuCheck(s == "A<ZuArray<int>({1,2,3})>");
  }
}
