//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtWindow.hh>

using namespace ZuTestUtil;

void testSetClrPtrAndWrap()
{
  ZuTestScope(testSetClrPtrAndWrap);

  ZtWindow<int> w(8);
  w.set(0, 10);
  w.set(1, 11);
  ZuCheck(w.ptr(0) && *w.ptr(0) == 10);
  ZuCheck(w.ptr(1) && *w.ptr(1) == 11);

  w.clr(1);
  ZuCheck(!w.ptr(1));

  for (unsigned i = 0; i < 16; i++)
    w.set(i, int(i + 100));

  // offset should have advanced to 8 after writing index 15
  ZuCheck(!w.ptr(7));
  ZuCheck(w.ptr(8) && *w.ptr(8) == 108);
  ZuCheck(w.ptr(15) && *w.ptr(15) == 115);
}

void testLargeJumpAndIteration()
{
  ZuTestScope(testLargeJumpAndIteration);

  ZtWindow<int> w(4);
  w.set(0, 1);
  w.set(1, 2);
  w.set(2, 3);
  w.set(3, 4);
  w.set(4, 5); // wrap once

  ZuCheck(!w.ptr(0));
  ZuCheck(w.ptr(4) && *w.ptr(4) == 5);

  w.clear();
  ZuCheck(!w.ptr(4));

  w.set(9, 6);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testSetClrPtrAndWrap);
  ZuTestCall(testLargeJumpAndIteration);
  return 0;
}
