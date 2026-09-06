//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuHex.hh>

#include <zlib/ZumJWT.hh>

#include <zlib/ZtlsSec.hh>

int main(int argc, char **argv)
{
  bool ok = argc == 3;
  uint8_t key[Ztls::ES256::PublicKeySize];
  if (ok)
    ok = ZuHex::decode(key, ZuBSpan{argv[2]}) == sizeof(key) &&
      ZuCSpan{argv[2]}.length() == sizeof(key) * 2;
  Zum::Principal principal;
  if (ok)
    ok = Zum::jwtVerify(argv[1], "interop", "https://issuer.example",
      "orders", key, 150, Zum::JWTLimits{}, principal) &&
      principal.subject == "workload" &&
      principal.clientID == "workload" && principal.scope == "read" &&
      principal.actions.length() == 1 &&
      principal.actions[0] == "orders.read" && !principal.authMethod;
  std::cout << "1..1\n" << (ok ? "ok" : "not ok") <<
    " 1 - OpenSSL ES256 access token\n";
  return ok ? 0 : 1;
}
