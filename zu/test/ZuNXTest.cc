//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuDemangle.hh>

#define CHECK(T, NX, x) ZuCheck(x, \
  std::cerr << ZuDemangle<T>{} << " NX=" << unsigned(NX) << '\n')

struct A { };

struct B { B() = default; B(B &&); B &operator =(B &&); };

int i;

template <typename T> void cref(T &&v) {
  ZuAssert((ZuIsSame<decltype(ZuFwdLike<T>(i)), const int &>{}));
}
template <typename T> void lref(T &&v) {
  ZuAssert((ZuIsSame<decltype(ZuFwdLike<T>(i)), int &>{}));
}
template <typename T> void rref(T &&v) {
  ZuAssert((ZuIsSame<decltype(ZuFwdLike<T>(i)), int &&>{}));
}

template <typename T, bool NX>
void test1() {
  ZuTestScope(test1);
  T a, b;
  CHECK(T, NX, noexcept(T(ZuMv(a))) == NX);
  CHECK(T, NX, noexcept(a = ZuMv(b)) == NX);
}

template <typename T, bool NX>
void test2() {
  ZuTestScope(test2);
  ZuTestCall((test1<T, NX>));
  ZuTestCall((test1<ZuArray<T, 4>, NX>));
  ZuTestCall((test1<ZuTuple<T, T>, NX>));
  ZuTestCall((test1<ZuUnion<void, T>, NX>));
  ZuTestCall((test1<ZuTuple<T, ZuUnion<void, T>>, NX>));
  ZuTestCall((test1<ZuUnion<void, ZuTuple<T, T>>, NX>));
  ZuTestCall((test1<ZuArray<ZuUnion<void, T>, 4>, NX>));
  ZuTestCall((test1<ZuArray<ZuTuple<T, T>, 4>, NX>));
  ZuTestCall((test1<ZuArray<ZuTuple<T, ZuUnion<void, T>>, 4>, NX>));
}

int main()
{
  ZuTestMain();
  {
    A a;
    const A &b = a;
    cref(b);
    lref(a);
    rref(A{});
  }

  ZuTestCall((test2<int, true>));
  ZuTestCall((test2<B, false>));
}
