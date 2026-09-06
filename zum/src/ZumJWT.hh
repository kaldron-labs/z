//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ES256 access-token preparation and resource authorization

#ifndef ZumJWT_HH
#define ZumJWT_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zum.hh>

namespace Ztls { class Random; }

namespace Zum {

// JWT IDs use 128 bits; ES256 hashes the signing input with SHA-256.
enum { JWTIDSize = 16, JWTDigestSize = 32 };

struct AccessClaims {
  String	issuer;
  String	subject;
  String	audience;
  String	clientID;
  String	jti;
  String	scope;
  StringVec	actions;
  StringVec	amr;
  int64_t	iat = 0;
  int64_t	nbf = 0;
  int64_t	exp = 0;
  int64_t	authTime = 0;
};

struct Principal {
  String	subject;
  String	clientID;
  String	scope;
  StringVec	actions;
  int64_t	expires = 0;
  int64_t	authTime = 0;
  String	authMethod;
};

struct JWTLimits {
  unsigned	token = 8U<<10;
  unsigned	json = 4U<<10;
  unsigned	depth = 3;
  unsigned	nodes = 256;
  unsigned	string = 512;
  unsigned	actions = 128;
};

struct PreparedJWT {
  String	token;
  uint8_t	digest[JWTDigestSize];
};

struct JWTHeader {
  String	type;
  String	algorithm;
  String	keyID;
};

ZumExtern bool interactiveClaims(
  Ztls::Random &, ZuCSpan issuer, const User &, const Client &,
  const ScopeSelection &, const ZtBitmap &, ZuSpan<const Action>,
  ZuCSpan authMethod, int64_t authTime, int64_t now, int64_t expires,
  AccessClaims &);
ZumExtern bool clientClaims(
  Ztls::Random &, ZuCSpan issuer, const Client &,
  const ScopeSelection &, const ZtBitmap &, ZuSpan<const Action>,
  int64_t now, int64_t expires, AccessClaims &);
ZumExtern bool jwtPrepare(
  const AccessClaims &, ZuCSpan kid, const JWTLimits &, PreparedJWT &);
ZumExtern bool jwtFinish(
  PreparedJWT &, ZuBSpan derSignature, const JWTLimits &);
ZumExtern bool jwtHeader(
  ZuCSpan token, const JWTLimits &, JWTHeader &);
ZumExtern bool jwtES256(
  ZuCSpan token, ZuBSpan publicKey, const JWTLimits &,
  JWTHeader &, String &claims);
ZumExtern bool jwtVerify(
  ZuCSpan token, ZuCSpan kid, ZuCSpan issuer, ZuCSpan audience,
  ZuBSpan publicKey, int64_t now, const JWTLimits &, Principal &);

} // namespace Zum

#endif /* ZumJWT_HH */
