//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <cstdint>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBitfield.hh>

using namespace ZuTestUtil;

template <unsigned Width>
void runWidth()
{
  ZuTestScope(runWidth);

  constexpr uint64_t max = [] {
    if constexpr (Width == 64)
      return ~uint64_t(0);
    else
      return (uint64_t(1) << Width) - 1;
  }();

  ZuBitfield<128, Width> b;
  ZuCheck(!!(!b));

  b.set(0, max);
  b[1] = (max > 1) ? (max - 1) : 1;
  b[127] = max;

  ZuCheck(b.get(0) == max);
  ZuCheck(uint64_t(b[1]) == ((max > 1) ? (max - 1) : 1));
  ZuCheck(b.get(127) == max);

  b[2] = 0;
  ZuCheck(!b[2]);

  b.zero();
  ZuCheck(!!(!b));

  b.fill();
  ZuCheck(!(!b));
  ZuCheck(b.get(0) == max);
  ZuCheck(b.get(127) == max);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();

  ZuTestCall_("w=1", runWidth<1>);
  ZuTestCall_("w=2", runWidth<2>);
  ZuTestCall_("w=4", runWidth<4>);
  ZuTestCall_("w=8", runWidth<8>);
  ZuTestCall_("w=16", runWidth<16>);
  ZuTestCall_("w=32", runWidth<32>);
  ZuTestCall_("w=64", runWidth<64>);
  return 0;
}
