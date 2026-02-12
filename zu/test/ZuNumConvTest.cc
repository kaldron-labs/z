//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/Zu_aton.hh>
#include <zlib/Zu_ntoa.hh>
#include <zlib/ZuDecimalFn.hh>
#include <zlib/ZuStringFn.hh>
#include <zlib/ZuICmp.hh>

using namespace ZuTestUtil;

void testIntRoundTrip(int64_t v)
{
  ZuTestScope(testIntRoundTrip);

  char buf[Zu_ilen<int64_t>() + 1];
  unsigned n = Zu_itoa(v, buf);
  buf[n] = 0;

  int64_t out = 0;
  unsigned m = Zu_atoi(out, buf, n);
  ZuCheck(m == n);
  ZuCheck(out == v);
}

void testFormatting()
{
  ZuTestScope(testFormatting);

  using Right5 = Zu_nprint<ZuFmt::Right<5, ' '>>;
  char padded[16];
  unsigned n = Right5::itoa(42, padded);
  padded[n] = 0;
  ZuCheck(!std::strcmp(padded, "   42"));

  int parsed = 0;
  unsigned m = Zu_nscan<ZuFmt::Right<5, ' '>>::atoi(parsed, padded, n);
  ZuCheck(m == n);
  ZuCheck(parsed == 42);

  using Hex = Zu_nprint<ZuFmt::Hex<true, ZuFmt::Alt<>>>;
  char hex[32];
  unsigned h = Hex::utoa(uint32_t(0x2a), hex);
  hex[h] = 0;
  ZuCheck(h > 0);
  ZuCheck((hex[h - 1] == 'A' || hex[h - 1] == 'a'));

  uint32_t hexVal = 0;
  ZuCheck(Zu_nscan<ZuFmt::Hex<>>::atou(hexVal, "0x2a", 4) == 4);
  ZuCheck(hexVal == 0x2aU);
}

void testFloatingScan()
{
  ZuTestScope(testFloatingScan);

  double v = 0;
  ZuCheck(Zu_atof(v, "nan", 3) == 3);
  ZuCheck(ZuFP<double>::nan(v));

  ZuCheck(Zu_atof(v, "inf", 3) == 3);
  ZuCheck(ZuFP<double>::inf(v));

  ZuCheck(Zu_atof(v, "-0.0", 4) == 4);
  ZuCheck(v == 0.0);
  ZuCheck(std::signbit(v));

  ZuCheck(Zu_atof(v, "1.5e3", 5) == 3); // exponent currently stops parse
  ZuCheck(v == 1.5);

  ZuCheck(Zu_atof(v, "x", 1) == 0);

  char out[64];
  unsigned n = Zu_ftoa(-0.0, out);
  out[n] = 0;
  double rt = 0;
  ZuCheck(Zu_atof(rt, out, n) == n);
  ZuCheck(rt == 0.0);
  ZuCheck(!std::isnan(rt));
}

void testAgainstCStdlib()
{
  ZuTestScope(testAgainstCStdlib);

  char exp[64];
  std::snprintf(exp, sizeof(exp), "%lld", 1234567LL);

  char out[64];
  unsigned n = Zu_itoa(1234567LL, out);
  out[n] = 0;
  ZuCheck(!std::strcmp(out, exp));

  const char *s = "123.75x";
  double v = 0;
  unsigned m = Zu_atof(v, s, 7);
  char *end = nullptr;
  double c = std::strtod(s, &end);
  ZuCheck(m == unsigned(end - s));
  ZuCheck(std::fabs(v - c) < 1e-12);
}

void testInvalidAndHelpers()
{
  ZuTestScope(testInvalidAndHelpers);

  int i = 0;
  ZuCheck(Zu_atoi(i, "--1", 3) == 0);

  static_assert(ZuDecimalFn::pow10_32(3) == 1000U);
  static_assert(ZuDecimalFn::pow10_64(4) == 10000ULL);

  ZuCheck(ZuICmp<ZuCSpan>::equals("Alpha", "aLpHa"));
  ZuCheck(ZuICmp<ZuCSpan>::cmp("abc", "ABD") < 0);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall_("0", testIntRoundTrip, int64_t(0));
  ZuTestCall_("1", testIntRoundTrip, int64_t(1));
  ZuTestCall_("-1", testIntRoundTrip, int64_t(-1));
  ZuTestCall_("max", testIntRoundTrip, std::numeric_limits<int64_t>::max());
  ZuTestCall_("min+1", testIntRoundTrip, std::numeric_limits<int64_t>::min() + 1);

  ZuTestCall(testFormatting);
  ZuTestCall(testFloatingScan);
  ZuTestCall(testAgainstCStdlib);
  ZuTestCall(testInvalidAndHelpers);
  return 0;
}
