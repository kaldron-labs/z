//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuArray.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);
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

  ZuCheck(a.first() == 2);
  ZuCheck(a.next(2) == 3);
  ZuCheck(a.last() == 255);
  ZuCheck(a.prev(255) == 254);

  ZuBitmap<256> c;
  c.set(60, 68);
  c.set(130, 190);
  ZuCheck(c.first() == 60);
  ZuCheck(c.last() == 189);
  ZuCheck(c.next(67) == 130);
  ZuCheck(c.prev(130) == 67);

  c.clr(64, 188);
  ZuCheck(c.first() == 60);
  ZuCheck(c.last() == 189);
  ZuCheck(c.next(63) == 188);

  ZuBitmap<256> empty;
  ZuCArray<32> es;
  es << empty;
  ZuCheck(es == "");

  ZuBitmap<256> full;
  full.fill();
  ZuCArray<32> fs;
  fs << full;
  ZuCheck(fs == "0-");

  ZuBitmap<256> rt(fs);
  ZuCheck(rt == full);
  return 0;
}
