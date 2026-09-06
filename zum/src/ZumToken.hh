//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous access-token issuance

#ifndef ZumToken_HH
#define ZumToken_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumDB.hh>
#include <zlib/ZumJWT.hh>
#include <zlib/ZumRequest.hh>

namespace Zum {

namespace TokenIssue {
  enum { OK = -1 };
}

namespace RevokeIssue {
  enum { OK = -1 };
}

using SignatureFn = ZmFn<void(Bytes),
  ZmFnHeapID<"Zum.SignatureFn">>;
using SignFn = ZmFn<void(ZuCSpan, ZuBSpan, SignatureFn),
  ZmFnHeapID<"Zum.SignFn">>;
using TokenFn = ZmFn<void(int, TokenResponse),
  ZmFnHeapID<"Zum.TokenFn">>;
using RevokeFn = ZmFn<void(int), ZmFnHeapID<"Zum.RevokeFn">>;

struct TokenConfig {
  String		issuer;
  String		keyID;
  ZfURI::FormLimits	formLimits{8, 32, 16U<<10};
  JWTLimits		jwtLimits;
  int64_t		now = 0;
  int64_t		accessExpires = 0;
  int64_t		refreshExpires = 0;
  unsigned		generationLimit = 0;
  unsigned		spentLimit = 0;
};

struct RevokeConfig {
  String		issuer;
  ZfURI::FormLimits	formLimits{4, 32, 16U<<10};
  int64_t		now = 0;
};

ZumExtern bool tokenRequest(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  String form, String authorization, TokenConfig, SignFn, TokenFn);
ZumExtern bool revokeRequest(
  Requests *, ZuTime deadline, DBContext *, String form,
  String authorization, RevokeConfig, RevokeFn);

} // namespace Zum

#endif /* ZumToken_HH */
