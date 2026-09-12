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

#include <zlib/ZuArray.hh>

#include <zlib/Zum.hh>
#include <zlib/ZumJWTVerify.hh>

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
  AppID		appID = 0;
};

struct IDClaims {
  String	issuer;
  String	subject;
  String	audience;
  String	nonce;
  String	name;
  String	preferredUserName;
  String	email;
  StringVec	amr;
  int64_t	iat = 0;
  int64_t	exp = 0;
  int64_t	authTime = 0;
};

struct PreparedJWT {
  String	token;
  ZuBArray<JWTDigestSize> digest;

  PreparedJWT() : digest(JWTDigestSize, false) { }
};

ZumExtern bool interactiveClaims(
  Ztls::Random &, ZuCSpan issuer, const User &, const Client &,
  const ScopeSelection &, const ZtBitmap &, ZuSpan<const Action>,
  ZuCSpan authMethod, int64_t authTime, int64_t now, int64_t expires,
  AccessClaims &);
ZumExtern bool clientClaims(
  Ztls::Random &, ZuCSpan issuer, const Client &,
  const ScopeSelection &, const ZtBitmap &, ZuSpan<const Action>,
  AppID clientAppID, int64_t now, int64_t expires, AccessClaims &);
ZumExtern bool jwtPrepare(
  const AccessClaims &, ZuCSpan kid, const JWTLimits &, PreparedJWT &);
ZumExtern bool idClaims(
  ZuCSpan issuer, const User &, const Client &, const ScopeSelection &,
  ZuCSpan nonce,
  ZuCSpan authMethod, int64_t authTime, int64_t now, int64_t expires,
  IDClaims &);
ZumExtern bool idTokenPrepare(
  const IDClaims &, ZuCSpan kid, const JWTLimits &, PreparedJWT &);
ZumExtern bool scopeContains(ZuCSpan, ZuCSpan);
ZumExtern bool jwtFinish(
  PreparedJWT &, ZuBSpan derSignature, const JWTLimits &);
ZumExtern bool userInfoJSON(const User &, const Principal &, String &);

} // namespace Zum

#endif /* ZumJWT_HH */
