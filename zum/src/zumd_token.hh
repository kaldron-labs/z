//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous access-token issuance

#ifndef zumd_token_HH
#define zumd_token_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

struct DB;
struct DBContext;

namespace TokenIssue {
  enum { OK = -1 };
}

namespace RevokeIssue {
  enum { OK = -1 };
}

ZuDerive(SignatureFn,
  (ZmFn<void(Bytes), ZmFnHeapID<"Zum.SignatureFn">>));
ZuDerive(SignFn, (ZmFn<void(const SignKey &, ZuBSpan, SignatureFn),
  ZmFnHeapID<"Zum.SignFn">>));
ZuDerive(TokenFn, (ZmFn<void(int, TokenResponse),
  ZmFnHeapID<"Zum.TokenFn">>));
ZuDerive(RevokeFn, (ZmFn<void(int), ZmFnHeapID<"Zum.RevokeFn">>));
ZuDerive(RefreshRevokeFn, (ZmFn<void(AppID, RefreshID, int64_t),
  ZmFnHeapID<"Zum.RefreshRevokeFn">>));

struct TokenConfig {
  String		issuer;
  AppID			appID = 0;
  JWTLimits		jwtLimits;
  unsigned		maxKeys = 0;
  int64_t		now = 0;
  int64_t		accessExpires = 0;
  int64_t		refreshExpires = 0;
  unsigned		generationLimit = 0;
  unsigned		spentLimit = 0;
  RefreshRevokeFn	event;
};

struct RevokeConfig {
	String		issuer;
	AppID			appID = 0;
	JWTLimits	jwtLimits;
	int64_t		now = 0;
	RefreshRevokeFn	event;
};

ZumExtern bool tokenRequest(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String form, String authorization, TokenConfig, SignFn, TokenFn);
ZumExtern bool revokeRequest(
  Requests *, ZuTime deadline, DBContext *, String form,
  String authorization, RevokeConfig, RevokeFn);

} // namespace Zum

#endif /* zumd_token_HH */
