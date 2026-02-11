//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtBitmap.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZtBitmap a{256U};
  a.set(2, 6);
  a.set(10, 15);
  a.set(100, 256);
  ZuCArray<100> s;
  s << a;
  log(s);
  ZuCheck(s == "2-5,10-14,100-", log_(s));
  ZuBitmap<256> b(s);
  s = {}; s << b;
  ZuCheck(s == "2-5,10-14,100-", log_(s));
  return 0;
}
