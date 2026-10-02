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
    return ZumVaultClient::save(tokens, {}, "zdash").is<ZeException>() ? 1 : 0;
  }
  if (strcmp(argv[1], "run") || argc != 5) return 1;
  ZuCSpan expected{argv[3]};
  int renewals = atoi(argv[4]);
  return ZDashOAuth::run(cf, true,
    [expected, renewals](ZiMultiplex &, ZDashOAuth::Clients &,
	ZDashOAuth::Lease &lease) {
      if (lease.token != expected || lease.renewAt <= Zm::now()) return false;
      ZmSemaphore wait;
      for (int i = 0; i < renewals; ++i) {
	auto previous = lease.renewAt;
	ZumVaultClient::SecretText token{lease.token};
	if (!wait.timedwait(previous) || !lease.refresh(lease) ||
	    lease.renewAt <= previous || lease.token == token) return false;
      }
      return renewals >= 0 || !lease.refresh(lease);
    }) ? 0 : 1;
}
