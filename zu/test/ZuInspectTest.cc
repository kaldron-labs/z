//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuDemangle.hh>

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

struct A { };
struct B : public A { };
struct C { operator A() { return A(); } };
struct D : public C { ~D() { log("~D()"); } };

constexpr auto foo() { return []{ log("Hello World"); }; }

struct A_Print : public ZuPrintDelegate {
  template <typename S>
  static void print(S &s, const A &) { s << "A{}"; }
};
A_Print ZuPrintType(A *);
using APtr = A *;
using Baz = decltype(ZuPrintType(ZuDeclVal<APtr *>()));
struct APtr_Print : public ZuPrintDelegate {
  template <typename S>
  static void print(S &s, APtr) { s << "&A{}"; }
};
APtr_Print ZuPrintType(APtr *);

template <typename T> struct E { T v; };

static void usage()
{
  std::cerr << "usage: ZuInspectTest [-v]\n";
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

  CHECK((ZuIsConvertible<void, void>{}));
  CHECK((ZuIsSame<void, void>{}));
  CHECK((!ZuIsBase<void, void>{}));
  CHECK((!ZuIsConvertible<void, A>{}));
  CHECK((!ZuIsSame<void, A>{}));
  CHECK((!ZuIsBase<A, void>{}));
  CHECK((!ZuIsConvertible<A, void>{}));
  CHECK((!ZuIsSame<A, void>{}));
  CHECK((!ZuIsBase<void, A>{}));
  CHECK((ZuIsConvertible<void *, void *>{}));
  CHECK((ZuIsSame<void *, void *>{}));
  CHECK((!ZuIsBase<void *, void *>{}));
  CHECK((ZuIsConvertible<A *, void *>{}));
  CHECK((!ZuIsSame<A *, void *>{}));
  CHECK((!ZuIsBase<void *, A *>{}));
  CHECK((!ZuIsConvertible<void *, A *>{}));
  CHECK((!ZuIsSame<void *, A *>{}));
  CHECK((!ZuIsBase<A *, void *>{}));
  CHECK((ZuIsConvertible<A, A>{}));
  CHECK((ZuIsSame<A, A>{}));
  CHECK((!ZuIsBase<A, A>{}));
  CHECK((!ZuIsConvertible<A, B>{}));
  CHECK((!ZuIsSame<A, B>{}));
  CHECK((ZuIsBase<B, A>{}));
  CHECK((ZuIsConvertible<B, A>{}));
  CHECK((!ZuIsSame<B, A>{}));
  CHECK((!ZuIsBase<A, B>{}));
  CHECK((!ZuIsConvertible<A, C>{}));
  CHECK((!ZuIsSame<A, C>{}));
  CHECK((!ZuIsBase<C, A>{}));
  CHECK((ZuIsConvertible<C, A>{}));
  CHECK((!ZuIsSame<C, A>{}));
  CHECK((!ZuIsBase<A, C>{}));
  CHECK((ZuIsConvertible<A *, A *>{}));
  CHECK((ZuIsSame<A *, A *>{}));
  CHECK((!ZuIsBase<A *, A *>{}));
  CHECK((!ZuIsConvertible<A *, B *>{}));
  CHECK((!ZuIsSame<A *, B *>{}));
  CHECK((!ZuIsBase<B *, A *>{}));
  CHECK((ZuIsConvertible<B *, A *>{}));
  CHECK((!ZuIsSame<B *, A *>{}));
  CHECK((!ZuIsBase<A *, B *>{}));

  CHECK(ZuTraits<int>::IsPOD);
  CHECK(ZuTraits<void *>::IsPOD);
  CHECK(ZuTraits<A>::IsPOD);
  CHECK(!ZuTraits<D>::IsPOD);
  CHECK((ZuTraits<ZuUnion<int, void *>>::IsPOD));
  CHECK((ZuTraits<ZuUnion<int, void *, A>>::IsPOD));
  CHECK(!(ZuTraits<ZuUnion<int, void *, D>>::IsPOD));
  CHECK((ZuTraits<ZuTuple<int, void *>>::IsPOD));
  CHECK((ZuTraits<ZuTuple<int, void *, A>>::IsPOD));
  CHECK(!(ZuTraits<ZuTuple<int, void *, D>>::IsPOD));

  constexpr auto bar = foo();
  constexpr auto baz = []{ log("Goodbye World"); };
  CHECK((ZuIsSame<const decltype(foo()), const decltype(bar)>{}));
  CHECK((!ZuIsSame<decltype(foo()), decltype(baz)>{}));

  bar(); baz();

  {
    A a;
    ZuAssert((ZuIsBase<decltype(std::cerr), std::ios_base>{}));
    log(a);
    log(&a);
  }
  {
    const int &foo(const int &);
    int &foo(int &);
    CHECK((ZuIsSame<int &, decltype(foo(ZuDeclVal<int &>()))>{}));
    CHECK((ZuIsSame<const int &, decltype(foo(ZuDeclVal<const int &>()))>{}));
    CHECK((!ZuIsSame<int &, const int &>{}));
  }

  {
    CHECK((ZuIsConstructible<int, unsigned>{}));
    CHECK((ZuIsConstructible<unsigned, int>{}));
    CHECK((ZuIsConstructible<short, int>{}));
  }

  {
    using R = int &;
    using T = ZuTuple<R, R>;
    // CHECK((!ZuIsConstructible<int, T>{}));
    CHECK((ZuIsConvertible<int, T>{}));
  }
}
