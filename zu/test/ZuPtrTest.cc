//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuPtr.hh>
#include <zlib/ZuTest.hh>

#define CHECK(x) ZuCheck(x)

struct A {
  int *x;
  A(int *x_) : x{x_} { ++*x; }
  ~A() { ++*x; }
};

void foo(A *a) {
  ZuTestScope(foo);
  CHECK(*(a->x) == 1);
}

int main()
{
  ZuTestMain();
  int i = 0;
  {
    ZuPtr<A> a = new A{&i};
    ZuTestCall(foo, ZuMv(a));
  }
  CHECK(i == 2);
}
