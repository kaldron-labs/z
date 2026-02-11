//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuDateTime.hh>

#include <zlib/ZmTime.hh>

using namespace ZuTestUtil;

struct Null {
  template <typename S> void print(S &s) const { s << "null"; }
  friend ZuPrintFn ZuPrintType(Null *);
};

template <int NDP>
void roundTrip(ZuDateTime d1)
{
  ZuTestScope(roundTrip);

  ZuCArray<32> fix;
  ZuDateTimeFmt::FIX<NDP, Null> fmt;
  fix << d1.fmt(fmt);
  ZuDateTime d2(ZuDateTimeScan::FIX{}, fix);
  ZuCArray<32> fix2;
  fix2 << d2.fmt(fmt);

  log("fix=", fix);
  log("fix2=", fix2);
  ZuCheck(d1 == d2);
  ZuCheck(fix == fix2);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall_("ndp0 epoch", (roundTrip<0>), ZuDateTime{time_t(0)});
  ZuTestCall_("ndp0 day1", (roundTrip<0>), ZuDateTime{1, 1, 1});
  ZuTestCall_("ndp-9 now", (roundTrip<-9>), ZuDateTime{Zm::now()});
  ZuTestCall_("ndp-3 decimal", (roundTrip<-3>), ZuDateTime{ZuTime{ZuDecimal{"0.01"}}});
  return 0;
}
