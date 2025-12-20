//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZmDemangle.hh>

#include <iostream>

template <template <typename> class, typename>
struct Foo { template <unsigned> static int bar(const char *); };

template <typename> struct Baz { };

int main()
{
  constexpr auto foo = ZuDefaultAxor();
  std::cout << ZmDemangle{"Z1XvEUlTyOT_E_"} << '\n';
  std::cout << ZmDemangle{"Z1XvEUlOT_E_"} << '\n';
  std::cout << "raw: " << typeid(foo).name() << '\n';
  std::cout << ZmDemangle{typeid(foo).name()} << '\n';
  std::cout << ZmDemangle{typeid(Foo<Baz, Baz<int>>).name()} << '\n';
}
