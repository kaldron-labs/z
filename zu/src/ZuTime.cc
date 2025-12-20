//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// nanosecond precision time class

#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>

unsigned ZuTime::scan(ZuCSpan s)
{
  ZuDateTimeScan::CSV fmt;
  ZuDateTime t;
  auto n = t.scan(fmt, s);
  *this = t.as_time();
  return n;
}
