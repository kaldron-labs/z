//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZuTest.hh>
#include <zlib/ZuAssert.hh>

#include <iostream>

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
#define CHECK(x, ...) ZuCheckFail(x, log_(__VA_ARGS__))

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

static void usage()
{
  std::cerr << "usage: ZuAxorTest [-v]\n";
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

  Foo<>::doit(A{});
  CHECK(constructed_ == 1 && moved_ == 1 && destroyed_ == 2);
  Foo<Axor<[](A &&a) -> A && { return static_cast<A &&>(a); }>>::doit(A{});
  CHECK(constructed_ == 2 && moved_ == 2 && destroyed_ == 4);
  Foo<Axor<bar>>::doit(A{});
  CHECK(constructed_ == 3 && moved_ == 3 && destroyed_ == 6);
  A a;
  Foo<>::doit(a);
  CHECK(constructed_ == 4 && moved_ == 3 && copied_ == 1 && destroyed_ == 7);
}

inline A &&bar(A &&a) { return static_cast<A &&>(a); }
