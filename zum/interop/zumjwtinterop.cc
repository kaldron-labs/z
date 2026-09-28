//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuHex.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZumJWTVerify.hh>

#include <zlib/ZtlsCOSE.hh>

using namespace ZuTestUtil;

static void verify(int argc, char **argv)
{
  ZuTestScope(verify);
  bool ok = argc == 3;
  uint8_t key[Ztls::COSE::ES256::PublicKeySize];
  if (ok)
    ok = ZuHex::decode(key, ZuBSpan{argv[2]}) == sizeof(key) &&
      ZuCSpan{argv[2]}.length() == sizeof(key) * 2;
  Zum::Principal principal;
  if (ok)
    ok = Zum::jwtVerify(argv[1], "interop", "https://issuer.example",
      "orders", key, 150, Zum::JWTLimits{}, principal) &&
      principal.subject == "workload" &&
      principal.appID == 1 &&
      principal.clientID == "workload" && principal.scope == "read" &&
      principal.actions.length() == 1 &&
      principal.actions[0] == "orders.read" && !principal.authMethod;
  ZuCHECK(ok, "OpenSSL ES256 access token verification failed");
}

int main(int argc, char **argv)
{
  ZuTestUtil::parse(1, argv);
  ZuTestMain();
  ZuTestCall(verify, argc, argv);
  return 0;
}
