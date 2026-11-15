//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Private hooks for the separately compiled scheduler progress test.

#ifndef ZmSchedProgressTest_HH
#define ZmSchedProgressTest_HH

class ZmScheduler;

struct ZmSchedProgressTest {
  enum { Overflow, Shift };
  static void hook(unsigned, const ZmScheduler *, bool = false);
};

#endif /* ZmSchedProgressTest_HH */
