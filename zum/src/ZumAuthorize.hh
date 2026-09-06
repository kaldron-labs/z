//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// asynchronous authorization request admission

#ifndef ZumAuthorize_HH
#define ZumAuthorize_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumDB.hh>
#include <zlib/ZumRequest.hh>

namespace Zum {

namespace AuthorizeIssue {
  enum { OK = -1 };
}

struct AuthorizeConfig {
  String		issuer;
  String		rpID;
  ZfURI::FormLimits	formLimits{8, 32, 16U<<10};
  int64_t		now = 0;
  int64_t		expires = 0;
  uint64_t		timeout = 0;
};

struct AuthorizeResult {
  Bytes		ceremonyID;
  String	options;
  String	redirectURI;
  String	state;
  bool		statePresent = false;
  bool		redirect = false;
};

struct AuthorizeFinishConfig {
  String		origin;
  String		rpID;
  ZfJSON::ScanLimits	jsonLimits;
  int64_t		now = 0;
  int64_t		codeExpires = 0;
};

using AuthorizeFn = ZmFn<void(int, AuthorizeResult),
  ZmFnHeapID<"Zum.AuthorizeFn">>;
using PolicyDoneFn = ZmFn<void(bool, ZtBitmap),
  ZmFnHeapID<"Zum.PolicyDoneFn">>;
using PolicyFn = ZmFn<void(
  const User &, const Client &, const ScopeSelection &,
  const ZtBitmap &, PolicyDoneFn),
  ZmFnHeapID<"Zum.PolicyFn">>;
using AuthorizeCodeFn = ZmFn<void(int, String),
  ZmFnHeapID<"Zum.AuthorizeCodeFn">>;

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
  int64_t authTime, AuthorizeFinishConfig, PolicyFn, AuthorizeCodeFn);

} // namespace Zum

#endif /* ZumAuthorize_HH */
