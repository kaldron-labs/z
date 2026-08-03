//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZdbPQ.hh>

using namespace ZuTestUtil;

static void vectorLayout()
{
  ZuTestScope(vectorLayout);
  ZuCHECK(ZdbPQ::vecSize(0, 8) == sizeof(ZdbPQ::VecHdr),
      "empty vector header size");
  ZuCHECK(ZdbPQ::vecSize(3, 8) ==
      sizeof(ZdbPQ::VecHdr) + 3 * (sizeof(ZdbPQ::VecElem) + 8),
      "fixed vector wire size");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(vectorLayout);
  return 0;
}
