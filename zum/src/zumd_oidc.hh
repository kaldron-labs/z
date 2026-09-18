//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OIDC client, provider transport, and local role resolution

#ifndef zumd_oidc_HH
#define zumd_oidc_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmScheduler.hh>
#include <zlib/ZmRef.hh>

#include <zlib/zumd_oauth.hh>
#include <zlib/zumd_jwt.hh>

class ZiMultiplex;

namespace Zum {

template <typename Heap> struct DBContext_;
using DBContext = DBContext_<
  ZmHeap<"Zum.zumd.db.context.DBContext", DBContext_<ZuVoid>>>;

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

// OIDC provider response and callback ceilings are imposed by the local
// transport/parser; OIDCLimits may select lower service-policy values.
namespace OIDCLimitMax {
  enum { Response = 64U<<10, Callback = 16U<<10 };
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
  // Tunable bounds for OIDC provider responses and pending login state.
  JWTLimits	jwt;
  unsigned	audiences = 8;
  unsigned	roleValues = 128;
  unsigned	response = OIDCLimitMax::Response;
  unsigned	callback = OIDCLimitMax::Callback;
  unsigned	keys = 32;
  int64_t	clockSkew = 60;

  bool valid() const {
    return jwt.token && jwt.json && jwt.actions && audiences &&
      roleValues && response && response <= OIDCLimitMax::Response &&
      callback && callback <= OIDCLimitMax::Callback && keys &&
      clockSkew >= 0;
  }
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
  bool finish(AppID, String query, OIDCFinishFn);

private:
  ZmRef<OIDCState>	m_state;
};

class OIDCHTTPState;

class OIDCHTTP {
public:
  // Small deployments normally use only a handful of provider origins; callers
  // can raise this bound without changing the per-origin HTTP concurrency.
  enum { DefaultOrigins = 32 };

  OIDCHTTP();
  ~OIDCHTTP();

  OIDCHTTP(const OIDCHTTP &) = delete;
  OIDCHTTP &operator =(const OIDCHTTP &) = delete;

  // Own mutable transport state on a non-I/O scheduler shard. The caller drains
  // users of fn() before final(), which runs on the main thread with mx alive.
  // Empty caPath uses native system trust; otherwise use a CA file/directory.
  bool init(ZiMultiplex *, unsigned sid, unsigned origins = DefaultOrigins,
      ZuCSpan caPath = {});
  OIDCHTTPFn fn() const;
  void final();

private:
  ZmRef<OIDCHTTPState> m_state;
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

#endif /* zumd_oidc_HH */
