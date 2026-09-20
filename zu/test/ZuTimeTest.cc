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

void testRoundTripAndDecimal()
{
  ZuTestScope(testRoundTripAndDecimal);

  ZuDateTimeFmt::CSV fmt;
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{1}}}.fmt(fmt)) == "1970/01/01 00:00:01");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt));
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{-1}}}.fmt(fmt)) == "1969/12/31 23:59:59");
  log(ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt));
  ZuCHECK((ZuCArray<48>{} << ZuDateTime{ZuTime{ZuDecimal{"-1.01"}}}.fmt(fmt)) == "1969/12/31 23:59:58.99");
  ZuCHECK((ZuTime{ZuDecimal{1}} - ZuTime{ZuDecimal{1}}).as_decimal() == ZuDecimal{0});

  ZuTime t{ZuDecimal{"12345.678901234"}};
  ZuDecimal d = t.as_decimal();
  ZuTime rt{d};
  ZuCheck(t == rt);

  ZuDateTime dt{t};
  ZuCheck(dt.as_time() == t);
}

void testInvalidFormatPaths()
{
  ZuTestScope(testInvalidFormatPaths);

  ZuTime t;
  ZuCheck(t.scan("not-a-time") < 0);

  ZuDateTime d;
  ZuDateTimeScan::CSV csv;
  ZuDateTimeScan::ISO iso;
  ZuDateTimeScan::FIX fix;

  ZuCheck(d.scan(csv, "not-a-time") < 0);
  ZuCheck(d.scan(iso, "not-a-time") < 0);
  ZuCheck(d.scan(fix, "not-a-time") < 0);
}

void testIncrementalScan()
{
  ZuTestScope(testIncrementalScan);

  constexpr auto s = "1970/01/01 00:00:01junk"_Zu;
  auto t = ZuTime::eov(s);
  ZuCheck(t.p<0>() == 19);
  ZuCheck(t.p<1>() == ZuTime{1});

  auto d = ZuDateTime::eov(ZuDateTimeScan::CSV{}, s);
  ZuCheck(d.p<0>() == 19);
  ZuCheck(d.p<1>().as_time() == ZuTime{1});

  auto iso = ZuDateTime::eov(
    ZuDateTimeScan::ISO{}, "1970-01-01T00:00:01junk");
  ZuCheck(iso.p<0>() == 19);
  ZuCheck(iso.p<1>().as_time() == ZuTime{1});

  auto invalid = ZuTime::eov("not-a-time");
  ZuCheck(invalid.p<0>() < 0);
  ZuCheck(!*invalid.p<1>());
}

void testNsecBoundaries()
{
  ZuTestScope(testNsecBoundaries);

  ZuTime t{1, 999999999};
  t += ZuTime{0, 1};
  ZuCheck(t.sec() == 2);
  ZuCheck(t.nsec() == 0);

  ZuTime u{2, 0};
  u -= ZuTime{0, 1};
  ZuCheck(u.sec() == 1);
  ZuCheck(u.nsec() == 999999999);
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();
  ZuTestCall(testRoundTripAndDecimal);
  ZuTestCall(testInvalidFormatPaths);
  ZuTestCall(testIncrementalScan);
  ZuTestCall(testNsecBoundaries);
  return 0;
}
