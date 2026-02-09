//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuTest.hh>
#include <zlib/ZuID.hh>
#include <zlib/ZuArray.hh>

bool verbose = false;

template <typename ...Args>
static void log_(Args &&...args) {
  if constexpr (sizeof...(args))
    (std::cerr << ...<< ZuFwd<Args>(args)) << '\n';
}
template <typename ...Args>
static void log(Args &&...args) {
  if (verbose) log_(ZuFwd<Args>(args)...);
}
#define CHECK(x, ...) ZuCheck(x, log_(__VA_ARGS__))

static void test(const char *s)
{
  ZuTestScope(test);
  log(s);
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

static void usage()
{
  std::cerr << "usage: ZuBoxTest [-v]\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  if (argc == 2) {
    if (strcmp(argv[1], "-v")) usage();
    verbose = true;
  }

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
