//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuDemangle.hh>

using namespace ZuTestUtil;

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

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuCHECK((ZuIsConvertible<void, void>{}));
  ZuCHECK((ZuIsSame<void, void>{}));
  ZuCHECK((!ZuIsBase<void, void>{}));
  ZuCHECK((!ZuIsConvertible<void, A>{}));
  ZuCHECK((!ZuIsSame<void, A>{}));
  ZuCHECK((!ZuIsBase<A, void>{}));
  ZuCHECK((!ZuIsConvertible<A, void>{}));
  ZuCHECK((!ZuIsSame<A, void>{}));
  ZuCHECK((!ZuIsBase<void, A>{}));
  ZuCHECK((ZuIsConvertible<void *, void *>{}));
  ZuCHECK((ZuIsSame<void *, void *>{}));
  ZuCHECK((!ZuIsBase<void *, void *>{}));
  ZuCHECK((ZuIsConvertible<A *, void *>{}));
  ZuCHECK((!ZuIsSame<A *, void *>{}));
  ZuCHECK((!ZuIsBase<void *, A *>{}));
  ZuCHECK((!ZuIsConvertible<void *, A *>{}));
  ZuCHECK((!ZuIsSame<void *, A *>{}));
  ZuCHECK((!ZuIsBase<A *, void *>{}));
  ZuCHECK((ZuIsConvertible<A, A>{}));
  ZuCHECK((ZuIsSame<A, A>{}));
  ZuCHECK((!ZuIsBase<A, A>{}));
  ZuCHECK((!ZuIsConvertible<A, B>{}));
  ZuCHECK((!ZuIsSame<A, B>{}));
  ZuCHECK((ZuIsBase<B, A>{}));
  ZuCHECK((ZuIsConvertible<B, A>{}));
  ZuCHECK((!ZuIsSame<B, A>{}));
  ZuCHECK((!ZuIsBase<A, B>{}));
  ZuCHECK((!ZuIsConvertible<A, C>{}));
  ZuCHECK((!ZuIsSame<A, C>{}));
  ZuCHECK((!ZuIsBase<C, A>{}));
  ZuCHECK((ZuIsConvertible<C, A>{}));
  ZuCHECK((!ZuIsSame<C, A>{}));
  ZuCHECK((!ZuIsBase<A, C>{}));
  ZuCHECK((ZuIsConvertible<A *, A *>{}));
  ZuCHECK((ZuIsSame<A *, A *>{}));
  ZuCHECK((!ZuIsBase<A *, A *>{}));
  ZuCHECK((!ZuIsConvertible<A *, B *>{}));
  ZuCHECK((!ZuIsSame<A *, B *>{}));
  ZuCHECK((!ZuIsBase<B *, A *>{}));
  ZuCHECK((ZuIsConvertible<B *, A *>{}));
  ZuCHECK((!ZuIsSame<B *, A *>{}));
  ZuCHECK((!ZuIsBase<A *, B *>{}));

  ZuCHECK(ZuTraits<int>::IsPOD);
  ZuCHECK(ZuTraits<void *>::IsPOD);
  ZuCHECK(ZuTraits<A>::IsPOD);
  ZuCHECK(!ZuTraits<D>::IsPOD);
  ZuCHECK((ZuTraits<ZuUnion<int, void *>>::IsPOD));
  ZuCHECK((ZuTraits<ZuUnion<int, void *, A>>::IsPOD));
  ZuCHECK(!(ZuTraits<ZuUnion<int, void *, D>>::IsPOD));
  ZuCHECK((ZuTraits<ZuTuple<int, void *>>::IsPOD));
  ZuCHECK((ZuTraits<ZuTuple<int, void *, A>>::IsPOD));
  ZuCHECK(!(ZuTraits<ZuTuple<int, void *, D>>::IsPOD));

  constexpr auto bar = foo();
  constexpr auto baz = []{ log("Goodbye World"); };
  ZuCHECK((ZuIsSame<const decltype(foo()), const decltype(bar)>{}));
  ZuCHECK((!ZuIsSame<decltype(foo()), decltype(baz)>{}));

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
    ZuCHECK((ZuIsSame<int &, decltype(foo(ZuDeclVal<int &>()))>{}));
    ZuCHECK((ZuIsSame<const int &, decltype(foo(ZuDeclVal<const int &>()))>{}));
    ZuCHECK((!ZuIsSame<int &, const int &>{}));
  }

  {
    ZuCHECK((ZuIsConstructible<int, unsigned>{}));
    ZuCHECK((ZuIsConstructible<unsigned, int>{}));
    ZuCHECK((ZuIsConstructible<short, int>{}));
  }

  {
    using R = int &;
    using T = ZuTuple<R, R>;
    // ZuCHECK((!ZuIsConstructible<int, T>{}));
    ZuCHECK((ZuIsConvertible<int, T>{}));
  }
}
