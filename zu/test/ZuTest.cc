//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

// static nested sub-test in a function
void foo() {
  ZuTestScope(foo);
  ZuCheck(true);
  ZuCheck(true);
  {
    ZuTest(foo_to_the_power_of_N);
    ZuCheck(true);
  }
}

// run-time variable sub-test in a function
void baz() {
  ZuTestScopeRT(baz);
  ZuCheckRT(true);
}

// run-time variable sub-test calling sub-test
void bar(unsigned n) {
  ZuTestScopeRT(bar);
  for (unsigned i = 0; i < n; i++) ZuTestCallRT(baz);
}

int main()
{
  ZuTestMain();

  // bool harnessed = ::getenv("HARNESS_ACTIVE");

  ZuCheck(true);
  ZuCheck(true);

  { ZuTest(empty); }

  ZuTestCall(foo);

  ZuTestCall(bar, 3);

  {
    ZuTestRepeat(bah, 5);
    for (unsigned i = 0; i < 5; i++) {
      // if (harnessed) ::sleep(1);
      ZuCheck(true);
    }
  }

  return 0;
}
