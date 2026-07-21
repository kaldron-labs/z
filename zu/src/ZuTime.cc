//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// nanosecond precision time class

#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>

int ZuTime::scan(ZuCSpan s)
{
  auto r = eov(s);
  *this = r.p<1>();
  return r.p<0>();
}

ZuTuple<int, ZuTime> ZuTime::eov(ZuCSpan s)
{
  auto r = ZuDateTime::eov(ZuDateTimeScan::CSV{}, s);
  if (r.p<0>() < 0) return {-1, ZuTime{}};
  return {r.p<0>(), r.p<1>().as_time()};
}
