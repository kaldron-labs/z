//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZuTest.hh>
#include <zlib/ZuSpan.hh>

void testSpanSplice()
{
  ZuTestScope(splice_span);

  // basic splice + shift
  {
    int buf[5] = {1, 2, 3, 4, 5};
    ZuSpan<int> s(buf, 5);
    s.splice(1, 2);
    ZuCheck(s.length() == 3);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 4);
    ZuCheck(s[2] == 5);
  }

  // offset < 0 clamps (offset + length)
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(-2, 3);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // length < 0 adjusted positive
  {
    int buf[4] = {1, 2, 3, 4};
    ZuSpan<int> s(buf, 4);
    s.splice(1, -1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 4);
  }

  // offset > length no-op
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    s.splice(3, 1);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }

  // length == 0 no-op
  {
    int buf[2] = {1, 2};
    ZuSpan<int> s(buf, 2);
    s.splice(0, 0);
    ZuCheck(s.length() == 2);
    ZuCheck(s[0] == 1);
    ZuCheck(s[1] == 2);
  }
}

int main()
{
  ZuTestMain();
  ZuTestCall(testSpanSplice);
  return 0;
}
