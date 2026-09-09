//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDemangle.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuVArray.hh>
#include <zlib/ZuMatcher.hh>

using namespace ZuTestUtil;

template <ZuArray> struct A { };
template <auto> struct B { };
template <ZuString> struct D { };

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

struct Base {
  auto foo(this auto &&self) {
    return ZuDemangle<decltype(self)>{};
  }
};
struct Derived : public Base {
  auto bar(this auto &&self) {
    return ZuFwdLike<decltype(self)>(self).foo();
  }
};

struct ArrayIDs { using Keys = ZuStringTL<"ZuVArray_::Array<ZuSpan">; };
struct ElemIDs {
  using Keys = ZuStringTL<"ZuVArray_::Elem<ZuVArray_::Array<ZuSpan">;
};

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

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
    ZuCHECK(ZuMatcher<ArrayIDs>().find(s).p<1>() == 0);
    s = {}; s << ZuDemangle<typename ZuTraits<decltype(a)>::Elem>{};
    log(s);
    ZuCHECK(ZuMatcher<ElemIDs>().find(s).p<1>() == 0);
  }
  {
    ZuCArray<512> s;
    Derived d;
    s << ZuMv(d).bar();
    ZuCHECK(s == "Derived &&");
    log(s);
  }
}
