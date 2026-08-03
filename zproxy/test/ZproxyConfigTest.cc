//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include "zproxyconfig.hh"

using namespace ZuTestUtil;

static void bufferSize()
{
  ZuTestScope(bufferSize);
  ZuCHECK(Zproxy::BufferSize == 32768, "I/O buffer size");
  ZuCHECK(!(Zproxy::BufferSize & (Zproxy::BufferSize - 1)),
      "I/O buffer size is a power of two");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(bufferSize);
  return 0;
}
