//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmTime.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  ZuTime t;
  ZuCheck(!*t);
  ZuCheck(!!t);
  ZuCheck(t);
  ZuTime t2 = 0;
  ZuCheck(*t2);
  ZuCheck(!t2);
  log(Zm::now());
}
