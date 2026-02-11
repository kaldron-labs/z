//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuAssert.hh>

using namespace ZuTestUtil;

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
    if (verbose) std::cerr << x.i << '\n';
  }
};

#define counter(name) \
  static unsigned name ## _; \
  static void name() { log(#name); ++name ## _; }
counter(constructed)
counter(moved)
counter(copied)
counter(move_assigned)
counter(copy_assigned)
counter(destroyed)

struct A {
  A() { constructed(); }
  A(const A &a) : i{a.i} { copied(); }
  A &operator =(const A &a) { i = a.i; copy_assigned(); return *this; }
  A(A &&a) : i{a.i} { a.i = 0; moved(); }
  A &operator =(A &&a) { i = a.i; a.i = 0; move_assigned(); return *this; }
  ~A() { destroyed(); }
  int i = 42;
};
A &&bar(A &&);

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  Foo<>::doit(A{});
  ZuCHECK(constructed_ == 1 && moved_ == 1 && destroyed_ == 2,
    "constructed=", constructed_, " moved=", moved_,
    " copied=", copied_, " destroyed=", destroyed_);
  Foo<Axor<[](A &&a) -> A && { return static_cast<A &&>(a); }>>::doit(A{});
  ZuCHECK(constructed_ == 2 && moved_ == 2 && destroyed_ == 4,
    "constructed=", constructed_, " moved=", moved_,
    " copied=", copied_, " destroyed=", destroyed_);
  Foo<Axor<bar>>::doit(A{});
  ZuCHECK(constructed_ == 3 && moved_ == 3 && destroyed_ == 6,
    "constructed=", constructed_, " moved=", moved_,
    " copied=", copied_, " destroyed=", destroyed_);
  A a;
  Foo<>::doit(a);
  ZuCHECK(constructed_ == 4 && moved_ == 3 && copied_ == 1 && destroyed_ == 7,
    "constructed=", constructed_, " moved=", moved_,
    " copied=", copied_, " destroyed=", destroyed_);
}

inline A &&bar(A &&a) { return static_cast<A &&>(a); }
