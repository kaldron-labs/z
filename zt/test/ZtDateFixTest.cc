//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtLib.hh>

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTime.hh>

#define CHECK(x) ((x) ? puts("OK  " #x) : puts("NOK " #x))

struct Null {
  template <typename S> void print(S &s) const { s << "null"; }
  friend ZuPrintFn ZuPrintType(Null *);
};

template <int NDP>
void test(ZuDateTime d1)
{
  ZuCArray<32> fix;
  ZuDateTimeFmt::FIX<NDP, Null> fmt;
  fix << d1.fmt(fmt);
  puts(fix.terminate());
  ZuDateTime d2(ZuDateTimeScan::FIX{}, fix);
  puts((ZuCArray<32>() << d2.fmt(fmt)).terminate());
  CHECK(d1 == d2);
}

int main(int argc, char **argv)
{
  test<0>(ZuDateTime{time_t(0)});
  test<0>(ZuDateTime{1, 1, 1});
  test<-9>(ZuDateTime{Zm::now()});
  test<-3>(ZuDateTime{ZuTime{ZuDecimal{"0.01"}}});

  if (argc < 2) { fputs("Usage: ZuDateTimeFixTest N\n", stderr); Zm::exit(1); }
  unsigned n = atoi(argv[1]);
  {
    ZuDateTimeFmt::FIX<-9, Null> fmt;
    ZuCArray<32> fix;
    ZuTime start, end;
    start = Zm::now();
    for (unsigned i = 0; i < n; i++) {
      ZuDateTime d1{Zm::now()};
      fix << d1.fmt(fmt);
      ZuDateTime d2{ZuDateTimeScan::FIX{}, fix};
    }
    end = Zm::now();
    end -= start;
    ZuDecimal d1 = end.as_decimal() / n;
    printf("time per cycle 1: %s\n", (ZuCArray<32>{} << d1).data());

    start = Zm::now();
    for (unsigned i = 0; i < n; i++) {
      ZuDateTime d1{Zm::now()};
      fix << d1.fmt(fmt);
    }
    end = Zm::now();
    end -= start;
    ZuDecimal d2 = end.as_decimal() / n;
    printf("time per cycle 2: %s\n", (ZuCArray<32>{} << d2).data());

    start = Zm::now();
    for (unsigned i = 0; i < n; i++) {
      ZuDateTime d1{Zm::now()};
    }
    end = Zm::now();
    end -= start;
    ZuDecimal d3 = end.as_decimal() / n;
    printf("time per cycle 3: %s\n", (ZuCArray<32>{} << d3).data());

    printf("time per FIX format print: %s\n",
      (ZuCArray<32>{} << (d2 - d3)).data());
    printf("time per FIX format scan: %s\n",
      (ZuCArray<32>{} << (d1 - d2)).data());
  }
}
