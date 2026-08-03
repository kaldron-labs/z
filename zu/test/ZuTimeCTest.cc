//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstring>

#include <zlib/ZuTestUtil.hh>
#include <zlib/zu_lib.h>
#include <zlib/zu_decimal.h>
#include <zlib/zu_time.h>

using namespace ZuTestUtil;

void testParseAndPrint()
{
  ZuTestScope(testParseAndPrint);

  zu_time t;
  zu_time_init(&t);
  ZuCheck(!zu_time_null(&t));

  ZuCheck(zu_time_in_csv(&t, "1970/01/01 00:00:01.25") > 0);

  char csv[64];
  ZuCheck(zu_time_out_csv(csv, sizeof(csv), &t));

  zu_time round;
  zu_time_init(&round);
  ZuCheck(zu_time_in_csv(&round, csv) == std::strlen(csv));
  ZuCheck(zu_time_cmp(&t, &round) == 0);

  zu_time iso;
  zu_time_init(&iso);
  ZuCheck(zu_time_in_iso(&iso, "1970-01-01T00:00:02Z") > 0);
  char outISO[64];
  ZuCheck(zu_time_out_iso(outISO, sizeof(outISO), &iso));
  ZuCheck(outISO[0]);

  zu_time fix;
  zu_time_init(&fix);
  ZuCheck(zu_time_in_fix(&fix, "19700101-00:00:03") > 0);
  char outFIX[64];
  ZuCheck(zu_time_out_fix(outFIX, sizeof(outFIX), &fix));
  ZuCheck(outFIX[0]);

  ZuCheck(zu_time_in_csv(&t, "bad") < 0);
  ZuCheck(zu_time_in_iso(&t, "bad") < 0);
  ZuCheck(zu_time_in_fix(&t, "bad") < 0);
}

void testDecimalAndMath()
{
  ZuTestScope(testDecimalAndMath);

  zu_time base;
  zu_time_init(&base);
  ZuCheck(zu_time_in_csv(&base, "1970/01/01 00:00:10.5") > 0);

  zu_decimal delta;
  ZuCheck(zu_decimal_in(&delta, "2.25") > 0);

  zu_time sum;
  zu_time_add(&sum, &base, &delta);

  zu_time back;
  zu_time_sub(&back, &sum, &delta);
  ZuCheck(zu_time_cmp(&base, &back) == 0);

  zu_decimal diff;
  zu_time_delta(&diff, &sum, &base);
  ZuCheck(zu_decimal_cmp(&diff, &delta) == 0);

  zu_decimal d;
  zu_time_to_decimal(&d, &base);
  zu_time fromDecimal;
  zu_time_from_decimal(&fromDecimal, &d);
  ZuCheck(zu_time_cmp(&base, &fromDecimal) == 0);

  ZuCheck(zu_time_hash(&base) == zu_time_hash(&fromDecimal));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testParseAndPrint);
  ZuTestCall(testDecimalAndMath);
  return 0;
}
