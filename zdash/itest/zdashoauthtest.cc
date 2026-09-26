//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include "../src/zdash_oauth.hh"

int main(int argc, char **argv)
{
  if (argc == 1) {
    ZuTestUtil::parse(argc, argv);
    ZuTestMain();
    ZuCheck(::system("python3 ./zdashoauthtest.py") == 0);
    return 0;
  }
  if (argc < 3) return 1;
  ZDashOAuth::Config cf;
  cf.issuerURL = argv[2];
  cf.clientID = "client";
  cf.loopbackTest = true;
  if (!strcmp(argv[1], "seed")) {
    ZumVaultClient::Credential tokens{cf.issuerURL, cf.issuerURL,
      cf.clientID, "stale", "refresh-0", "Client offline_access"};
    return ZumVaultClient::save(tokens).is<ZeException>() ? 1 : 0;
  }
  if (strcmp(argv[1], "run") || argc != 4) return 1;
  ZuCSpan expected{argv[3]};
  return ZDashOAuth::run(cf, true,
    [expected](ZiMultiplex &, ZDashOAuth::Clients &, ZuCSpan token) {
      return token == expected;
    }) ? 0 : 1;
}
