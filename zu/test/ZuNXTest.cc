//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuDemangle.hh>

template <typename T, bool NX>
inline void out(const char *s) {
  std::cout
    << s << ' ' << ZuDemangle<T>{}
    << " NX=" << unsigned(NX) << '\n' << std::flush;
}

#define CHECK(T, NX, x) ((x) ? out<T, NX>("OK  " #x) : out<T, NX>("NOK " #x))

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
  T a, b;
  CHECK(T, NX, noexcept(T(ZuMv(a))) == NX);
  CHECK(T, NX, noexcept(a = ZuMv(b)) == NX);
}

template <typename T, bool NX>
void test2() {
  test1<T, NX>();
  test1<ZuArray<T, 4>, NX>();
  test1<ZuTuple<T, T>, NX>();
  test1<ZuUnion<void, T>, NX>();
  test1<ZuTuple<T, ZuUnion<void, T>>, NX>();
  test1<ZuUnion<void, ZuTuple<T, T>>, NX>();
  test1<ZuArray<ZuUnion<void, T>, 4>, NX>();
  test1<ZuArray<ZuTuple<T, T>, 4>, NX>();
  test1<ZuArray<ZuTuple<T, ZuUnion<void, T>>, 4>, NX>();
}

int main()
{
  {
    A a;
    const A &b = a;
    cref(b);
    lref(a);
    rref(A{});
  }

  test2<int, true>();
  test2<B, false>();
}
