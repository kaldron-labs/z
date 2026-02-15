//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiPlatform.hh>

using namespace ZuTestUtil;

void testUsernameHostname()
{
  ZuTestScope(testUsernameHostname);

  ZeError e;
  auto u = Zi::username(&e);
  auto h = Zi::hostname(&e);

  ZuCheck(!!u);
  ZuCheck(!!h);
}

void testVecInit()
{
  ZuTestScope(testVecInit);

  char buf[32] = {0};
  ZiVec v;
  ZiVec_init(v, buf, sizeof(buf));
  ZuCheck(ZiVec_ptr(v) == static_cast<ZiVecPtr>(buf));
  ZuCheck(static_cast<unsigned>(ZiVec_len(v)) == sizeof(buf));
}

void testNullPrimitives()
{
  ZuTestScope(testNullPrimitives);

  ZuCheck(Zi::nullHandle(Zi::nullHandle()));
  ZuCheck(Zi::nullSocket(Zi::nullSocket()));
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testUsernameHostname);
  ZuTestCall(testVecInit);
  ZuTestCall(testNullPrimitives);
  return 0;
}
