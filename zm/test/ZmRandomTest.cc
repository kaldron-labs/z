//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmRandom.hh>

using namespace ZuTestUtil;

void testDeterministicSeed()
{
  ZuTestScope(testDeterministicSeed);

  ZmRandom a(123456789U);
  ZmRandom b(123456789U);

  for (int i = 0; i < 128; i++)
    ZuCheck(a.randInt() == b.randInt());

  a.seed(42U);
  b.seed(42U);
  for (int i = 0; i < 64; i++)
    ZuCheck(a.randInt(97) == b.randInt(97));
}

void testRanges()
{
  ZuTestScope(testRanges);

  ZmRandom r(7U);

  for (int i = 0; i < 200; i++) {
    auto v = r.randInt(9);
    ZuCheck(v <= 9);

    double x = r.rand();
    ZuCheck(x >= 0.0 && x <= 1.0);

    double y = r.randExc();
    ZuCheck(y >= 0.0 && y < 1.0);

    double z = r.randDblExc();
    ZuCheck(z > 0.0 && z < 1.0);
  }
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDeterministicSeed);
  ZuTestCall(testRanges);
  return 0;
}
