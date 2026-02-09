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

#include <zlib/ZuTest.hh>
#include <zlib/ZuTime.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuArray.hh>

bool verbose = false;

template <typename ...Args>
static void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}
template <typename ...Args>
static void log(Args &&...args) {
  if (verbose) log_(ZuFwd<Args>(args)...);
}
#define CHECK(x, ...) ZuCheck(x, log_(__VA_ARGS__))

static void usage()
{
  std::cerr << "usage: ZuBoxTest [-v]\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    if (strcmp(argv[1], "-v")) usage();
    verbose = true;
  }

  ZuTestMain();

  ZuDateTimeFmt::CSV fmt;
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{1}}}.fmt(fmt)) == "1970/01/01 00:00:01");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt));
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt)) == "1969/12/31 23:59:59");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt));
  CHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt)) == "1969/12/31 23:59:58.99");
  CHECK((ZuTime{ZuDecimal{1}} - ZuTime{ZuDecimal{1}}).as_decimal() == ZuDecimal{0});
}
