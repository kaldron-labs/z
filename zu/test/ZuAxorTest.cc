//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZuAssert.hh>

#include <iostream>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

struct Defaults {
  static constexpr auto Fn = ZuDefaultAxor();
};
template <auto Fn_>
struct Axor : public Defaults {
  static constexpr auto Fn = Fn_;
};
template <typename NTP = Defaults>
struct Foo {
  static constexpr auto Fn = NTP::Fn;
  template <typename T>
  static void doit(T &&v) {
    auto x = Fn(ZuFwd<T>(v));
    std::cout << x.i << '\n';
  }
};
struct A {
  A() { std::cout << "constructed\n"; }
  A(const A &a) : i{a.i} { std::cout << "copied\n"; }
  A &operator =(const A &a) { i = a.i; std::cout << "copied (assigned)\n"; return *this; }
  A(A &&a) : i{a.i} { a.i = 0; std::cout << "moved\n"; }
  A &operator =(A &&a) { i = a.i; a.i = 0; std::cout << "moved (assigned)\n"; return *this; }
  ~A() { std::cout << "destroyed\n"; }
  int i = 42;
};
A &&bar(A &&);
int main()
{
  Foo<>::doit(A{});
  Foo<Axor<[](A &&a) -> A && { return static_cast<A &&>(a); }>>::doit(A{});
  Foo<Axor<bar>>::doit(A{});
  A a;
  Foo<>::doit(a);
}
inline A &&bar(A &&a) { return static_cast<A &&>(a); }
