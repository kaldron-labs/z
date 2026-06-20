//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// run-time assertion that falls back to ZiLOGBT in release code

// Usage: ZiAssert(assertion, (captures), msg, return);
//
// in debug mode, this is equivalent to ZmAssert(assertion), i.e.
// if the assertion fails the program will abort
//
// in release mode, ZiLogBT is called with a Fatal severity level;
// the log event lambda uses captures to append msg to the log, and the
// calling function will execute return, which is typically of the
// form "return deflt" or "throw exception"

// Example:
//
// void foo() {
//   int i = 42, j = 43;
//   ZiAssert(i == j - 1, (i, j), "i=" << i << " j=" << j, return);
// }

#ifndef ZiAssert_HH
#define ZiAssert_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuFnName.hh>

#include <zlib/ZmAssert.hh>

#include <zlib/ZiLog.hh>

#ifdef ZDEBUG
#define ZiAssert(x, captures, component, msg, fallback) \
  ZmAssert(x, fallback)
#else
#define ZiAssert(x, component, captures, msg, fallback) \
  do { if (ZuUnlikely(!(x))) { \
    ZiLOGBT(Fatal, component, ([ZuPP_Strip(captures)](auto &s) { \
      s << " Assertion '" #x "' failed " << msg; \
    })); ZuPP_Strip(fallback); } } while (0)
#endif

#endif /* ZiAssert_HH */
