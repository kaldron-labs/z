//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <tuple>
#include <utility>
#include <array>

#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuArray.hh>

inline void out(const char *s) {
  std::cout << s << '\n' << std::flush;
}

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

int main()
{
  ZuDateTimeFmt::CSV fmt;
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{1}}}.fmt(fmt)) == "1970/01/01 00:00:01");
  std::cout << (ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt)) << '\n';
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt)) == "1969/12/31 23:59:59");
  std::cout << (ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt)) << '\n';
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt)) == "1969/12/31 23:59:58.99");
  CHECK((ZuTime{ZuDecimal{1}} - ZuTime{ZuDecimal{1}}).as_decimal() == ZuDecimal{0});
}
