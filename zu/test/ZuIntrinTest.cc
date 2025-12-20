//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuIntrin.hh>

inline void check(ZuCSpan op, unsigned l, unsigned r, unsigned n, unsigned i) {
  std::cout << (l == r ? "OK  " : "NOK ")
    << op << n << "(1<<" << i << ")\n" << std::flush;
  if (l != r) {
    std::cout << "  l=" << l << " r=" << r << '\n';
    ::exit(1);
  }
}

#define TEST(n) \
  for (unsigned i = 0; i < n; i++) { \
    auto v = uint##n##_t(1)<<i; \
    unsigned l = ZuPP_Eval(ZuPP_Defer(Zu_clz##n)(v)); \
    unsigned r = ZuPP_Eval(ZuPP_Defer(Zu_clz##n##_)(v)); \
    check("clz", l, r, n, i); \
    l = ZuPP_Eval(ZuPP_Defer(Zu_ctz##n)(v)); \
    r = ZuPP_Eval(ZuPP_Defer(Zu_ctz##n##_)(v)); \
    check("ctz", l, r, n, i); \
  }

int main()
{
  TEST(8);
  TEST(16);
  TEST(32);
  TEST(64);
  TEST(128);
}
