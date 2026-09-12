//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// upstream OIDC configuration and local role resolution

#ifndef ZumOIDC_HH
#define ZumOIDC_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmScheduler.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZumOAuth.hh>
#include <zlib/ZumJWT.hh>

namespace Zum {

struct DBContext;

// OIDC state, nonce, and PKCE verifier each carry 256 bits of entropy.
enum { OIDCRandomSize = 32 };

namespace AuthMethod {
  enum { Passkey, OIDC, LocalFirst };
}

namespace OIDCRoles {
  enum { Local, Mapped };
}

namespace OIDCClientAuth {
  enum { Basic, Post, None };
}

namespace OIDCHTTPMethod {
  enum { GET, POST };
}

struct OIDCRoleMap {
  String	value;
  RoleID	roleID = 0;
};

ZuDerive(OIDCRoleMapVec, (ZtArray<OIDCRoleMap, VecHeap>));

struct OIDCConfig {
  AppID		appID = 0;
  ProviderID	providerID = 0;
  uint64_t	policyVersion = 0;
  uint32_t	assignmentMaxAge = 0;
  String	issuer;
  String	authorizeEndpoint;
  String	tokenEndpoint;
  String	jwksEndpoint;
  String	userinfoEndpoint;
  String	clientID;
  String	clientSecret;
  String	redirectURI;
  StringVec	oidcScopes;
  String	roleClaim;
  OIDCRoleMapVec roleMap;
  EligibilityMode::T eligibilityMode = EligibilityMode::MappedRole;
  String	eligibilityClaim;
  StringVec	eligibilityValues;
  ClaimSource::T claimSource = ClaimSource::IDToken;
  unsigned	roles = OIDCRoles::Local;
  unsigned	clientAuth = OIDCClientAuth::Basic;
  String	prompt;
  String	loginHint;
  uint64_t	maxAge = 0;
  bool		maxAgePresent = false;
};

struct OIDCClaims {
  String	issuer;
  String	subject;
  String	nonce;
  StringVec	audience;
  StringVec	roleValues;
  StringVec	eligibilityValues;
  int64_t	iat = 0;
  int64_t	authTime = 0;
  int64_t	expires = 0;
};

struct OIDCLimits {
  // Tunable bounds for upstream responses and pending login state.
  JWTLimits	jwt;
  unsigned	audiences = 8;
  unsigned	roleValues = 128;
  unsigned	response = 64U<<10;
  unsigned	callback = 16U<<10;
  unsigned	keys = 32;
  int64_t	clockSkew = 60;
};

ZuDerive(OIDCUserFn, (ZmFn<void(bool, User, IDVec, Evidence),
  ZmFnHeapID<"Zum.OIDCUserFn">>));

struct OIDCHTTPRequest {
  String	url;
  String	contentType;
  String	authorization;
  String	body;
  unsigned	method = OIDCHTTPMethod::GET;
};

ZuDerive(OIDCHTTPDoneFn, (ZmFn<void(unsigned, String),
  ZmFnHeapID<"Zum.OIDCHTTPDoneFn">>));
ZuDerive(OIDCHTTPFn, (ZmFn<void(OIDCHTTPRequest, OIDCHTTPDoneFn),
  ZmFnHeapID<"Zum.OIDCHTTPFn">>));
ZuDerive(OIDCBeginFn, (ZmFn<void(bool, String),
  ZmFnHeapID<"Zum.OIDCBeginFn">>));
ZuDerive(OIDCFinishFn,
  (ZmFn<void(bool, Bytes, User, IDVec, Evidence, int64_t),
  ZmFnHeapID<"Zum.OIDCFinishFn">>));
ZuDerive(OIDCClockFn,
  (ZmFn<int64_t(), ZmFnHeapID<"Zum.OIDCClockFn">>));

class OIDCState;

class ZumAPI OIDC {
  OIDC(const OIDC &) = delete;
  OIDC &operator =(const OIDC &) = delete;

public:
  OIDC();
  ~OIDC();

  bool init(
    ZmScheduler *, unsigned sid, DBContext *, OIDCLimits,
    unsigned pendingLimit, uint64_t timeout, OIDCClockFn, OIDCHTTPFn);
  void final();

  bool begin(Bytes grantID, OIDCConfig, OIDCBeginFn);
  bool finish(String query, OIDCFinishFn);

private:
  ZmRef<OIDCState>	m_state;
};

ZumExtern bool oidcConfigValid(const OIDCConfig &);
ZumExtern IDVec oidcMapRoles(
  ZuSpan<const String>, ZuSpan<const OIDCRoleMap>);
ZumExtern bool oidcVerifyIDToken(
  ZuCSpan token, ZuBSpan publicKey, ZuCSpan nonce,
  const OIDCConfig &, int64_t now, const OIDCLimits &, OIDCClaims &);
ZumExtern void oidcLoadUser(
  DBContext *, String subject, const OIDCConfig &, StringVec roleValues,
  StringVec eligibilityValues, int64_t now, OIDCUserFn);

} // namespace Zum

#endif /* ZumOIDC_HH */
