//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zum.hh>

using namespace ZuTestUtil;

static void limits()
{
  ZuTestScope(limits);
  ZuCHECK(Zum::KeySize == 32, "SHA-256 key size");
  ZuCHECK(Zum::KeyIDSize == 16, "key ID size");
  ZuCHECK(Zum::MaxAPIKeys <= Zum::MaxQueryLimit, "query covers API keys");
  Zum::User user;
  ZuCHECK(!user.id && !user.flags && !user.failures, "default user state");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(limits);
  return 0;
}
