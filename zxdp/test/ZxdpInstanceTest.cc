//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zxdp.hh>

using namespace ZuTestUtil;

static void testCreation()
{
  ZuTestScope(testCreation);
  ZtString<> error;
  ZuCHECK(!Zxdp::Instance::create({}, error) && !!error,
    "missing named creation parameters fail without kernel resources");
  Zxdp::BufPool rx{2, 1514};
  ZuCHECK(!Zxdp::Instance::create({.rxPool = &rx,
      .rx = [](uint8_t *, unsigned) { }}, error) && !rx.ready(),
    "failed setup leaves caller-owned pools unbound");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testCreation);
}
