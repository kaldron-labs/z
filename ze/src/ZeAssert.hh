//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// run-time assertion that falls back to ZeLOGBT in release code

// Usage: ZeAssert(assertion, (captures), msg, return);
//
// in debug mode, this is equivalent to ZmAssert(assertion), i.e.
// if the assertion fails the program will abort
//
// in release mode, ZeBackTrace is called with a Fatal severity level;
// the log event lambda uses captures to append msg to the log, and the
// calling function will execute return, which is typically of the
// form "return deflt" or "throw exception"

// Example:
//
// void foo() {
//   int i = 42, j = 43;
//   ZeAssert(i == j - 1, (i, j), "i=" << i << " j=" << j, return);
// }

#ifndef ZeAssert_HH
#define ZeAssert_HH

#ifndef ZeLib_HH
#include <zlib/ZeLib.hh>
#endif

#include <zlib/ZuFnName.hh>

#include <zlib/ZmAssert.hh>

#include <zlib/ZeLog.hh>

#ifdef NDEBUG
#define ZeAssert(assertion, captures, msg, return_) \
  do { if (ZuUnlikely(!(assertion))) { \
    ZeLOGBT(Fatal, ([ZuPP_Strip(captures)](auto &s) { \
      s << "\"" __FILE__ "\":" << __LINE__ << ' ' << ZuFnName << \
	" Assertion '" #assertion "' failed " << msg; \
    })); return_; } } while (0)
#else
#define ZeAssert(assertion, captures, msg, return_) ZmAssert(assertion)
#endif

#endif /* ZeAssert_HH */
