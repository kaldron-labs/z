//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <tuple>
#include <utility>
#include <array>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuArray.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuDateTimeFmt::CSV fmt;
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{1}}}.fmt(fmt)) == "1970/01/01 00:00:01");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt));
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt)) == "1969/12/31 23:59:59");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt));
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt)) == "1969/12/31 23:59:58.99");
  ZuCHECK((ZuTime{ZuDecimal{1}} - ZuTime{ZuDecimal{1}}).as_decimal() == ZuDecimal{0});
}
