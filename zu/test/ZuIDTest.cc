//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuArray.hh>

using namespace ZuTestUtil;

static void test(const char *s)
{
  ZuTestScope(test);
  log(s);
  unsigned n = strlen(s);
  if (n > 8) n = 8;
  ZuID a(s);
  printf("%u %u\n", n, a.length());
  ZuCHECK(a.length() == n);
  ZuCHECK(!memcmp(a.data(), s, n));
  ZuCHECK(a.span() == ZuCSpan(s, n));
  ZuCArray<9> b; b << a;
  ZuCHECK(a.span() == b);
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuTestCall(test, "a");
  ZuTestCall(test, "ab");
  ZuTestCall(test, "abc");
  ZuTestCall(test, "abcd");
  ZuTestCall(test, "abcde");
  ZuTestCall(test, "abcdef");
  ZuTestCall(test, "abcdefg");
  ZuTestCall(test, "abcdefgh");
  ZuTestCall(test, "abcdefghi");
}
