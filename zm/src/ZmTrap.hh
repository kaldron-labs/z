//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// low-level last-ditch error logging and signal trapping

#ifndef ZmTrap_HH
#define ZmTrap_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4251)
#endif

#include <ZuSpan.hh>

class ZmAPI ZmTrap {
public:
  // trap signals (call once at start of main program)

  static void trap();

  // registering signal handlers is intentionally not MT-safe

  typedef void (*Fn)();

  static void sigintFn(Fn fn);
  static Fn sigintFn();

  static void sighupFn(Fn fn);
  static Fn sighupFn();

  // last-ditch error logging

  static void log(ZuCSpan s);
#ifdef _WIN32
  static void winProgram(ZuCSpan s);
  static void winErrLog(int type, ZuCSpan s);	// Windows error logging
#endif
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* ZmTrap_HH */
