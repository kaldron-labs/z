//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous authorization request admission

#ifndef zumd_authorize_HH
#define zumd_authorize_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_webauthn.hh>
#include <zlib/zumd_request.hh>

namespace Zum {

template <typename Heap> struct DBContext_;
using DBContext = DBContext_<
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<ZuVoid>>>;
struct DB;

namespace AuthorizeIssue {
  enum { Consent = -2, OK = -1 };
}

struct AuthorizeConfig {
  String		issuer;
  AppID			appID = 0;
  String		rpID;
  int64_t		now = 0;
  int64_t		expires = 0;
  uint64_t		timeout = 0;
  bool			passkey = false;
};

struct AuthorizeResult {
  Bytes		ceremonyID;
  AppID		appID = 0;
  String	issuer;
  String	loginHint;
  String	options;
  String	redirectURI;
  String	state;
  String	prompt;
  uint64_t	maxAge = 0;
  bool		statePresent = false;
  bool		promptPresent = false;
  bool		maxAgePresent = false;
  bool		redirect = false;
};

struct AuthorizeFinishConfig {
  String		issuer;
  AppID			appID = 0;
  String		origin;
  String		rpID;
  int64_t		now = 0;
  int64_t		codeExpires = 0;
  bool			consent = false;
};

ZuDerive(AuthorizeFn, (ZmFn<void(int, AuthorizeResult),
  ZmFnHeapID<"Zum.AuthorizeFn">>));
ZuDerive(PolicyDoneFn, (ZmFn<void(bool, ZtBitmap),
  ZmFnHeapID<"Zum.PolicyDoneFn">>));
ZuDerive(PolicyFn, (ZmFn<void(
  const User &, const Client &, const ScopeSelection &,
  const ZtBitmap &, PolicyDoneFn),
  ZmFnHeapID<"Zum.PolicyFn">>));
ZuDerive(AuthorizeCodeFn, (ZmFn<void(int, String),
  ZmFnHeapID<"Zum.AuthorizeCodeFn">>));

ZumExtern bool authorizeRequest(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  String query, Bytes bindingDigest, AuthorizeConfig, AuthorizeFn);
ZumExtern bool authorizeFinish(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  Bytes ceremonyID, Bytes bindingDigest, AssertionInput,
  AuthorizeFinishConfig, PolicyFn, AuthorizeCodeFn);
ZumExtern bool authorizeOIDCFinish(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  Bytes ceremonyID, Bytes bindingDigest, User, IDVec roleIDs,
  Evidence, int64_t authTime, AuthorizeFinishConfig, PolicyFn,
  AuthorizeCodeFn);
ZumExtern bool authorizeSessionFinish(
  Requests *, ZuTime deadline, DBContext *, Ztls::Random &,
  Bytes ceremonyID, Bytes bindingDigest, Session,
  AuthorizeFinishConfig, PolicyFn, AuthorizeCodeFn);
ZumExtern bool authorizeConsentFinish(
  Requests *, ZuTime deadline, DB *, DBContext *, Ztls::Random &,
  Bytes ceremonyID, Bytes bindingDigest, bool approve,
  AuthorizeFinishConfig, PolicyFn, AuthorizeCodeFn);

} // namespace Zum

#endif /* zumd_authorize_HH */
