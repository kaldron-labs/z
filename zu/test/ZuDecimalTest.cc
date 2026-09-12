//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDecimal.hh>
#include <zlib/ZuFixed.hh>

#include <zlib/zu_decimal.h>

template <typename ...Args>
static void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}

int main()
{
  ZuTestMain();
  // check basic string scan
  ZuCheck((double)(ZuDecimal{"0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"."}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{".0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"0."}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"0.0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"-0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"-."}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"-.0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"-0."}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"-0.0"}.as_fp()) == 0.0);
  ZuCheck((double)(ZuDecimal{"1000.42"}.as_fp()) == 1000.42);
  ZuCheck((double)(ZuDecimal{"-1000.42"}.as_fp()) == -1000.42);
  // check basic value scanning
  {
    auto v = ZuDecimal{"1000.42"};
    ZuCheck((ZuCArray<44>() << v.value) == "1000420000000000000000");
    ZuCheck((double)(v.as_fp()) == 1000.42);
    v = ZuDecimal{"-1000.4200000000000000001"};
    ZuCheck((ZuCArray<44>() << v.value) == "-1000420000000000000000");
    ZuCheck((double)(v.as_fp()) == -1000.42);
  }
  // check leading/trailing zeros
  ZuCheck((double)(ZuDecimal{"001"}.as_fp()) == 1.0);
  ZuCheck((double)(ZuDecimal{"1.000"}.as_fp()) == 1.0);
  ZuCheck((double)(ZuDecimal{"001.000"}.as_fp()) == 1.0);
  ZuCheck((double)(ZuDecimal{"00.100100100"}.as_fp()) == .1001001);
  ZuCheck((double)(ZuDecimal{"0.10010010"}.as_fp()) == .1001001);
  ZuCheck((double)(ZuDecimal{".1001001"}.as_fp()) == .1001001);
  {
    // check basic multiply
    double v = (ZuDecimal{"1000.42"} * ZuDecimal{2.5}).as_fp();
    ZuCheck(v == 2501.05);
    v = (ZuDecimal{"-1000.42"} * ZuDecimal{2.5}).as_fp();
    ZuCheck(v == -2501.05);
  }
  {
    // check overflow multiply
    ZuDecimal f{"10000000000000000"};
    int128_t v = (f * f).value;
    ZuCheck(!*ZuBoxed(v));
    f = 10;
    v = (f * f).value;
    ZuCheck((double)(ZuDecimal{ZuDecimal::Unscaled{v}}.as_fp()) == 100.0);
  }
  {
    // check underflow multiply
    ZuDecimal f{".000000000000000001"};
    ZuCheck((int)f.value == 1);
    auto v = (f * f).value;
    ZuCheck(!v);
    ZuDecimal g{".00000000000000001"};
    ZuCheck((int)g.value == 10);
    v = (g * ZuDecimal{".1"}).value;
    ZuCheck((ZuDecimal{ZuDecimal::Unscaled{v}}.as_fp() == .000000000000000001L));
    v = (g * ZuDecimal{".01"}).value;
    ZuCheck(!(int)v);
  }
  ZuCheck((!*ZuDecimal{""}));
  // check overflow/underflow strings
  ZuCheck((!*ZuDecimal{"1000000000000000000"}));
  ZuCheck((!ZuDecimal{".0000000000000000001"}));
  // check formatted printing
  {
    ZuCArray<60> s;
    s << ZuDecimal{"42000.42"}.fmt<ZuFmt::Comma<>>();
    ZuCheck(s == "42,000.42");
  }
  ZuCheck((ZuDecimal{".000000000000000001"}.ndp() == 18));
  ZuCheck((ZuDecimal{".10000000000000001"}.ndp() == 17));
  ZuCheck((ZuDecimal{".0000000000000001"}.ndp() == 16));
  ZuCheck((ZuDecimal{".100000000000001"}.ndp() == 15));
  ZuCheck((ZuDecimal{".00000000000001"}.ndp() == 14));
  ZuCheck((ZuDecimal{".1000000000001"}.ndp() == 13));
  ZuCheck((ZuDecimal{".000000000001"}.ndp() == 12));
  ZuCheck((ZuDecimal{".10000000001"}.ndp() == 11));
  ZuCheck((ZuDecimal{".0000000001"}.ndp() == 10));
  ZuCheck((ZuDecimal{".100000001"}.ndp() == 9));
  ZuCheck((ZuDecimal{".00000001"}.ndp() == 8));
  ZuCheck((ZuDecimal{".1000001"}.ndp() == 7));
  ZuCheck((ZuDecimal{".000001"}.ndp() == 6));
  ZuCheck((ZuDecimal{".10001"}.ndp() == 5));
  ZuCheck((ZuDecimal{".0001"}.ndp() == 4));
  ZuCheck((ZuDecimal{".101"}.ndp() == 3));
  ZuCheck((ZuDecimal{".01"}.ndp() == 2));
  ZuCheck((ZuDecimal{".1"}.ndp() == 1));
  ZuCheck((ZuDecimal{"0"}.ndp() == 0));
  ZuCheck((ZuDecimal{"1"}.ndp() == 0));
  ZuCheck((ZuDecimal{"100000000000000000"}.ndp() == 0));
  ZuCheck((ZuFixed{ZuDecimal("1.0001")}.ndp == 4));
  ZuCheck(((ZuCArray<32>{} << ZuFixed{ZuDecimal("1.0001")}) == "1.0001"));
  ZuCheck(((ZuCArray<32>{} << ZuFixed{ZuDecimal("-12.34")}) == "-12.34"));
  ZuCheck(((ZuCArray<32>{} << ZuFixed{ZuDecimal("0")}) == "0"));
  ZuCheck(((ZuCArray<32>{} << ZuFixed{ZuDecimal("1")}) == "1"));
  ZuCheck(((ZuCArray<48>{} << ZuFixed{ZuDecimal(".000000000000000001")}) == "0.000000000000000001"));
  ZuCheck(((ZuCArray<48>{} << ZuFixed{ZuDecimal("999999999999999999")}) == "999999999999999999"));

  {
    ZuPun<zu_decimal, ZuDecimal> u;
    zu_decimal l_, r_;
    zu_decimal_in(&u.in, "42.01");
    ZuCheck(((ZuCArray<40>{} << u.out) == "42.01"));
    ZuCheck((!zu_decimal_cmp(&u.in, &u.in)));
    zu_decimal_in(&l_, "42");
    zu_decimal_in(&r_, "42.010000000000000001");
    ZuCheck((zu_decimal_cmp(&l_, &u.in) < 0));
    ZuCheck((zu_decimal_cmp(&u.in, &r_) < 0));
    zu_decimal_add(&u.in, &l_, &r_);
    ZuCheck(((ZuCArray<40>{} << u.out) == "84.010000000000000001"));
    zu_decimal_sub(&u.in, &u.in, &l_);
    ZuCheck((!zu_decimal_cmp(&u.in, &r_)));
    zu_decimal_mul(&u.in, &l_, &r_);
    ZuCheck(((ZuCArray<40>{} << u.out) == "1764.420000000000000042"));
    char buf[40] = { 0 };
    zu_decimal_out(buf, 39, &u.in);
    // ZuCSpan(buf) would span the entire buffer, not compute strlen
    ZuCheck(ZuCSpan(&buf[0]) == "1764.420000000000000042", log_(buf));
    zu_decimal_div(&u.in, &u.in, &r_);
    ZuCheck(((ZuCArray<40>{} << u.out) == "42"));
  }
  {
    ZuDecimal d;
    ZuCheck(d.scan("0") == 1);
  }
  {
    auto d = ZuDecimal::eov("42.5junk");
    ZuCheck(d.p<0>() == 4);
    ZuCheck(d.p<1>() == ZuDecimal{"42.5"});
    auto f = ZuDecimal::eov("junk");
    ZuCheck(f.p<0>() < 0);
    ZuCheck(!*f.p<1>());
    auto nan = ZuDecimal::eov("nanjunk");
    ZuCheck(nan.p<0>() == 3);
    ZuCheck(!*nan.p<1>());

    auto v = ZuFixed::eov("42.5junk");
    ZuCheck(v.p<0>() == 4);
    ZuCheck(v.p<1>() == ZuFixed{ZuDecimal{"42.5"}});
    auto vf = ZuFixed::eov("junk");
    ZuCheck(vf.p<0>() < 0);
    ZuCheck(!*vf.p<1>());
    auto vnan = ZuFixed::eov("nanjunk");
    ZuCheck(vnan.p<0>() == 3);
    ZuCheck(!*vnan.p<1>());
  }
  {
    ZuDecimal d;
    ZuDecimal e = -d;
    ZuCheck(!*e);
  }
  {
    ZuDecimal d{"100000000000000000"};
    ZuDecimal e{"-0.1"};
    d /= e; // overflow
    ZuCheck(!*d);
  }
}
