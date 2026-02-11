//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmDemangle.hh>

using namespace ZuTestUtil;

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  constexpr auto foo = ZuDefaultAxor();
  using Foo_ = decltype(foo);
  ZuCSpan d1 = ZmDemangle_{"Z1XvEUlTyOT_E_"};
  ZuCSpan d2 = ZmDemangle_{"Z1XvEUlOT_E_"};
  ZuCheck(!!d1 && !!d2);
  ZuCheck(!d1.equals("Z1XvEUlTyOT_E_"));
  ZuCheck(!d2.equals("Z1XvEUlOT_E_"));
  log(d1);
  log(d2);
  log("raw: ", typeid(foo).name());
  log(ZmDemangle<Foo_>{});
  log(ZmDemangle<Foo<Baz, Baz<int>>>{});
}
