//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// assertions

// differs from assert() in that an assertion failure hangs the program
// rather than crashing it, permitting live debugging

#ifndef ZmAssert_HH
#define ZmAssert_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

// need to export regardless of NDEBUG to support debug build application
// linking with release build Z

extern "C" {
  ZmExtern void ZmAssert_fail(
      const char *expr, const char *file, unsigned line, const char *fn);
  ZmExtern void ZmAssert_failed();
}

#ifdef ZDEBUG
#include <zlib/ZuFnName.hh>
#define ZmAssert(x, ...) \
  ((x) ? void() : ZmAssert_fail(#x, __FILE__, __LINE__, ZuFnName))
#else /* ZDEBUG */
#define ZmAssert_1(x) (void())
#define ZmAssert_2(x, fallback) \
  do { if (ZuUnlikely(!(x))) { ZuPP_Strip(fallback); } } while (0)
#define ZmAssert_N(_0, _1, Fn, ...) Fn
#define ZmAssert(...) \
  ZmAssert_N(__VA_ARGS__, \
    ZmAssert_2(__VA_ARGS__), \
    ZmAssert_1(__VA_ARGS__))
#endif /* ZDEBUG */

#endif /* ZmAssert_HH */
