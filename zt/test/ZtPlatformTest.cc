//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtLib.hh>
#include <zlib/ZtPlatform.hh>

using namespace ZuTestUtil;

static ZtAPI int macroEcho(int v)
{
  return v;
}

void testPutenvSmoke()
{
  ZuTestScope(testPutenvSmoke);

  static char env[] = "ZT_PLATFORM_TEST_VAR";

  Zt::setenv(env, "one");
  const char *v = ::getenv("ZT_PLATFORM_TEST_VAR");
  ZuCheck(v && !strcmp(v, "one"));

  Zt::setenv(env, "two");
  v = ::getenv("ZT_PLATFORM_TEST_VAR");
  ZuCheck(v && !strcmp(v, "two"));
}

void testLibMacrosCompile()
{
  ZuTestScope(testLibMacrosCompile);

  ZuCheck(macroEcho(7) == 7);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testPutenvSmoke);
  ZuTestCall(testLibMacrosCompile);
  return 0;
}
