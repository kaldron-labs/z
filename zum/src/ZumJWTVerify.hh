//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// service-side ES256 access-token verification

#ifndef ZumJWTVerify_HH
#define ZumJWTVerify_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>

namespace Zum {

struct Principal {
  String	subject;
  String	clientID;
  String	scope;
  StringVec	actions;
  int64_t	expires = 0;
  int64_t	authTime = 0;
  String	authMethod;
  AppID		appID = 0;
};

struct JWTLimits {
  // Tunable allocation/work bounds for bearer tokens received from peers.
  unsigned	token = 8U<<10;
  unsigned	json = 4U<<10;
  unsigned	actions = 128;
};

struct JWTHeader {
  String	type;
  String	algorithm;
  String	keyID;
};

ZumAPI bool jwtHeader(
  ZuCSpan token, const JWTLimits &, JWTHeader &);
ZumAPI bool jwtES256(
  ZuCSpan token, ZuBSpan publicKey, const JWTLimits &,
  JWTHeader &, String &claims);
ZumAPI bool jwtVerify(
  ZuCSpan token, ZuCSpan kid, ZuCSpan issuer, ZuCSpan audience,
  ZuBSpan publicKey, int64_t now, const JWTLimits &, Principal &);
ZumAPI bool jwtVerifyIssuer(
  ZuCSpan token, ZuCSpan kid, ZuCSpan issuer,
  ZuBSpan publicKey, int64_t now, const JWTLimits &, Principal &);

} // namespace Zum

#endif /* ZumJWTVerify_HH */
