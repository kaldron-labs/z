//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <math.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZdbSLCodec.hh>

using namespace ZuTestUtil;

static void integers()
{
  ZuTestScope(integers);
  uint8_t data[16];
  ZdbSL::saveU64(data, UINT64_C(0x0123456789abcdef));
  static const uint8_t expected[] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  ZuCHECK(!memcmp(data, expected, sizeof(expected)), "U64 golden bytes");
  uint64_t u64 = 0;
  ZuCHECK(ZdbSL::loadU64({data, 8}, u64) &&
      u64 == UINT64_C(0x0123456789abcdef), "U64 round trip");
  ZdbSL::saveS128(data, -1);
  ZuCHECK(data[0] == 0x7f && data[15] == 0xff, "S128 sign bias");
  int128_t s128 = 0;
  ZuCHECK(ZdbSL::loadS128({data, 16}, s128) && s128 == -1,
    "S128 round trip");
}

static void floating()
{
  ZuTestScope(floating);
  uint8_t neg[8], zero[8], pos[8], nan[8];
  ZdbSL::saveFloat(neg, -1.0);
  ZdbSL::saveFloat(zero, -0.0);
  ZdbSL::saveFloat(pos, 1.0);
  ZdbSL::saveFloat(nan, NAN);
  ZuCHECK(memcmp(neg, zero, 8) < 0 && memcmp(zero, pos, 8) < 0 &&
    memcmp(pos, nan, 8) < 0, "F64 lexical numerical order");
  double v;
  ZuCHECK(ZdbSL::loadFloat({zero, 8}, v) && v == 0.0 && !signbit(v),
    "F64 signed zero normalization");
  uint8_t ninf[8], subnormal[8], inf[8];
  ZdbSL::saveFloat(ninf, -INFINITY);
  ZdbSL::saveFloat(subnormal, nextafter(0.0, 1.0));
  ZdbSL::saveFloat(inf, INFINITY);
  ZuCHECK(memcmp(ninf, neg, 8) < 0 && memcmp(zero, subnormal, 8) < 0 &&
      memcmp(subnormal, pos, 8) < 0 && memcmp(pos, inf, 8) < 0 &&
      memcmp(inf, nan, 8) < 0,
    "F64 infinities and subnormal order");
}

static void decimal()
{
  ZuTestScope(decimal);
  uint8_t neg[16], zero[16], one[16], next[16];
  ZdbSL::saveDecimal(neg, ZuDecimal{"-1"});
  ZdbSL::saveDecimal(zero, ZuDecimal{"0"});
  ZdbSL::saveDecimal(one, ZuDecimal{"1"});
  ZdbSL::saveDecimal(next, ZuDecimal{"1.000000000000000001"});
  ZuCHECK(memcmp(neg, zero, 16) < 0 && memcmp(zero, one, 16) < 0 &&
      memcmp(one, next, 16) < 0, "Decimal lexical numerical order");
  ZuDecimal value;
  ZuCHECK(ZdbSL::loadDecimal({next, 16}, value) &&
      value == ZuDecimal{"1.000000000000000001"}, "Decimal round trip");
  uint8_t fixedZero[16], decimalZero[16], fixedNull[16], decimalNull[16];
  ZdbSL::saveFixed(fixedZero, ZuFixed{"0", 2});
  ZdbSL::saveDecimal(decimalZero, ZuDecimal{"0"});
  ZdbSL::saveFixed(fixedNull, ZuFixed{});
  ZdbSL::saveDecimal(decimalNull, ZuDecimal{});
  ZuCHECK(!memcmp(fixedZero, decimalZero, 16), "Fixed zero normalization");
  ZuCHECK(!memcmp(fixedNull, decimalNull, 16), "Fixed null preservation");
  ZuFixed fixed;
  ZuCHECK(ZdbSL::loadFixed({fixedZero, 16}, fixed) && *fixed && !fixed,
    "Fixed zero round trip");
  ZuCHECK(ZdbSL::loadFixed({fixedNull, 16}, fixed) && !*fixed,
    "Fixed null round trip");
}

static void temporal()
{
  ZuTestScope(temporal);
  uint8_t data[12];
  ZuTime time{-1, 999999999};
  ZdbSL::saveTime(data, time);
  ZuTime time_;
  ZuCHECK(ZdbSL::loadTime({data, 12}, time_) && time_ == time,
    "Time round trip");
  ZuDateTime datetime{ZuDateTime::Julian{-1}, 86399, 999999999};
  ZdbSL::saveDateTime(data, datetime);
  ZuDateTime datetime_;
  ZuCHECK(ZdbSL::loadDateTime({data, 12}, datetime_) && datetime_ == datetime,
    "DateTime round trip");
  uint8_t before[12], after[12];
  ZdbSL::saveTime(before, ZuTime{-1, 999999999});
  ZdbSL::saveTime(after, ZuTime{0, 0});
  ZuCHECK(memcmp(before, after, 12) < 0, "Time second boundary order");
  ZdbSL::saveDateTime(before,
    ZuDateTime{ZuDateTime::Julian{-1}, 86399, 999999999});
  ZdbSL::saveDateTime(after, ZuDateTime{ZuDateTime::Julian{0}, 0, 0});
  ZuCHECK(memcmp(before, after, 12) < 0, "DateTime day boundary order");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(integers);
  ZuTestCall(floating);
  ZuTestCall(decimal);
  ZuTestCall(temporal);
  return 0;
}
