//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuIntrin.hh>

using namespace ZuTestUtil;

template <unsigned N, typename U>
void test() {
  ZuTestScopeRT(test);
  for (unsigned i = 0; i < N; i++) {
    auto v = U(1)<<i;
    unsigned l, r;

    if      constexpr (N ==   8) l = Zu_clz8(v);
    else if constexpr (N ==  16) l = Zu_clz16(v);
    else if constexpr (N ==  32) l = Zu_clz32(v);
    else if constexpr (N ==  64) l = Zu_clz64(v);
    else if constexpr (N == 128) l = Zu_clz128(v);

    if      constexpr (N ==   8) r = Zu_clz8_(v);
    else if constexpr (N ==  16) r = Zu_clz16_(v);
    else if constexpr (N ==  32) r = Zu_clz32_(v);
    else if constexpr (N ==  64) r = Zu_clz64_(v);
    else if constexpr (N == 128) r = Zu_clz128_(v);

    ZuCheckRT(l == r);
    if (verbose || l != r)
      std::cerr << "clz" << N << "(1<<" << i << ")"
		<< " l=" << l << " r=" << r << '\n';

    if      constexpr (N ==   8) l = Zu_ctz8(v);
    else if constexpr (N ==  16) l = Zu_ctz16(v);
    else if constexpr (N ==  32) l = Zu_ctz32(v);
    else if constexpr (N ==  64) l = Zu_ctz64(v);
    else if constexpr (N == 128) l = Zu_ctz128(v);

    if      constexpr (N ==   8) r = Zu_ctz8_(v);
    else if constexpr (N ==  16) r = Zu_ctz16_(v);
    else if constexpr (N ==  32) r = Zu_ctz32_(v);
    else if constexpr (N ==  64) r = Zu_ctz64_(v);
    else if constexpr (N == 128) r = Zu_ctz128_(v);

    ZuCheckRT(l == r);
    if (verbose || l != r)
      std::cerr << "ctz" << N << "(1<<" << i << ")"
		<< " l=" << l << " r=" << r << '\n';
  }
}

#define TEST(N) ZuTestCall_("test<" #N ">", (test<N, uint##N##_t>))

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  TEST(8);
  TEST(16);
  TEST(32);
  TEST(64);
  TEST(128);
}
