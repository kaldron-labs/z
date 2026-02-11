//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuArray.hh>

int main()
{
  ZuTestMain();
  ZuBitmap<256> a;
  a.set(2, 6);
  a.set(10, 15);
  a.set(100, 256);
  ZuCArray<100> s;
  s << a;
  ZuCheck(s == "2-5,10-14,100-");
  ZuBitmap<256> b(s);
  ZuCheck(a == b);
  ZuCArray<100> t;
  t << b;
  ZuCheck(t == s);
}
