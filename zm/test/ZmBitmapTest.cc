//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// bitmap test program

#include <stdlib.h>

#include <zlib/ZuArray.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBitmap.hh>

using namespace ZuTestUtil;

#define test(T, x) \
  ZuCheck((ZuCArray<32>() << T(x)) == x)
#define test2(T, x, y) \
  ZuCheck((ZuCArray<32>() << T(x)) == y)

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  test(ZmBitmap, "");
  test2(ZmBitmap, ",", "");
  test2(ZmBitmap, ",,", "");
  test(ZmBitmap, "0-");
  test(ZmBitmap, "0,3-");
  test(ZmBitmap, "3-");
  test(ZmBitmap, "3-5,7");
  test(ZmBitmap, "3-5,7,9-");
}
