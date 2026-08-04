//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// compile-time assertion

#ifndef ZuAssert_HH
#define ZuAssert_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <assert.h>

#include <zlib/ZuPP.hh>

#define ZuAssert_1(x) static_assert((x), #x)
#define ZuAssert_2(x, d) static_assert((x), d)
#define ZuAssert_N(_0, _1, Fn, ...) Fn
#define ZuAssert__(...) \
  ZuAssert_N(__VA_ARGS__, \
    ZuAssert_2(__VA_ARGS__), \
    ZuAssert_1(__VA_ARGS__))
// ZuAssert() dependents may call it using ZuPP_Eval
#define ZuAssert(...) \
  ZuPP_Eval__(ZuPP_Defer(ZuAssert__)(__VA_ARGS__))

// compile time C assert
#define ZuCAssert(x) switch (0) { case 0: case (x): ; }

#endif /* ZuAssert_HH */
