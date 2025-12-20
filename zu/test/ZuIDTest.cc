//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuID.hh>
#include <zlib/ZuArray.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

static void test(const char *s)
{
  out(s);
  unsigned n = strlen(s);
  if (n > 8) n = 8;
  ZuID a(s);
  printf("%u %u\n", n, a.length());
  CHECK(a.length() == n);
  CHECK(!memcmp(a.data(), s, n));
  CHECK(a.span() == ZuCSpan(s, n));
  ZuCArray<9> b; b << a;
  CHECK(a.span() == b);
}

int main()
{
  test("a");
  test("ab");
  test("abc");
  test("abcd");
  test("abcde");
  test("abcdef");
  test("abcdefg");
  test("abcdefgh");
  test("abcdefghi");
}
