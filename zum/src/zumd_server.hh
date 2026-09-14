//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// embedded Zum OAuth and passkey HTTP operation owner

#ifndef zumd_server_HH
#define zumd_server_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_passkey.hh>
#include <zlib/zumd_session.hh>
#include <zlib/zumd_token.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zum {

namespace ReplyType {
  enum {
    Page, Redirect, OK, OAuthError, ClientError, BearerError, Empty, Discovery,
    ServerError
  };
}

struct ServerReply {
  String	body;
  String	location;
  String	setCookie;
  unsigned	type = ReplyType::OAuthError;
};

struct AppServer {
  String	issuer;
  String	audience;
  AppID		appID = 0;
  AudienceID	audienceID = 0;
  uint32_t	tokenLifetime = 0;
  uint32_t	sessionIdle = 0;
  uint32_t	sessionAbsolute = 0;

  ZuOpBool
  bool operator !() const {
    return !appID || !audienceID || !issuer || !audience ||
      !tokenLifetime || !sessionIdle || !sessionAbsolute;
  }
};
ZuDerive(AppServerFn, (ZmFn<void(AppServer),
  ZmFnHeapID<"Zum.AppServerFn">>));

namespace PasskeyStartType {
  enum { Enrollment, Bootstrap, AddCredential, Recovery };
}

struct PasskeyStart {
  String	capability;
  unsigned	type = PasskeyStartType::Enrollment;
};

struct PasskeyAdmission {
  EnrollmentBeginConfig	enrollment;
  CredentialBeginConfig	credential;
  RecoveryBeginConfig	recovery;
  bool			allowed = false;
};

// HTTP parser ceilings.  These are transport limits, not service policy;
// ServerLimits::valid() keeps configurable policy within the parser range.
namespace ServerLimitMax {
  enum {
    Query = 16U<<10,
    Form = 16U<<10,
    CeremonyQuery = 256,
    JSON = 64U<<10
  };
}

// Administrative scans use one extra row to preserve overflow semantics.
namespace AdminQueryLimit {
  enum { Results = 1000, Scan = Results + 1 };
}

struct ServerLimits {
  // Tunable bounds for unauthenticated HTTP inputs and database responses.
  unsigned		form = ServerLimitMax::Form;
  unsigned		ceremonyQuery = ServerLimitMax::CeremonyQuery;
  unsigned		json = ServerLimitMax::JSON;
  JWTLimits		jwt;
  unsigned		credentialID = 1024;
  unsigned		cookie = 8U<<10;
  unsigned		jwks = 4;
  unsigned		metadataScopes = 1024;
  OIDCLimits		oidc;
  unsigned		oidcPending = 64;

  bool valid() const {
    return form && form <= ServerLimitMax::Form &&
      ceremonyQuery && ceremonyQuery <= ServerLimitMax::CeremonyQuery &&
      json && json <= ServerLimitMax::JSON && oidc.valid();
  }
};

struct ServerConfig {
  String	issuer;
  String	rpID;
  String	rpName;
  String	cookieName{"zum_tx"};
  String	cookiePath{"/"};
  OIDCConfig	oidc;
  ServerLimits	limits;
  uint64_t	requestTimeout = 15;
  uint64_t	oidcTimeout = 300;
  uint64_t	passkeyTimeout = 60000;
  int64_t	ceremonyLifetime = 300;
  int64_t	codeLifetime = 60;
  int64_t	accessLifetime = 300;
  int64_t	refreshLifetime = 86400;
  int64_t	sessionIdle = 1800;
  int64_t	sessionAbsolute = 43200;
  unsigned	refreshGenerations = 64;
  unsigned	spentTokens = 64;
  unsigned	authMethod = AuthMethod::Passkey;
	RefreshRevokeFn refreshRevoke;
};

ZuDerive(ServerFn,
  (ZmFn<void(ServerReply), ZmFnHeapID<"Zum.ServerFn">>));
ZuDerive(ClockFn, (ZmFn<int64_t(), ZmFnHeapID<"Zum.ClockFn">>));
ZuDerive(PageFn,
  (ZmFn<String(AppID, Bytes, String), ZmFnHeapID<"Zum.PageFn">>));
ZuDerive(AdmitDoneFn, (ZmFn<void(PasskeyAdmission),
  ZmFnHeapID<"Zum.AdmitDoneFn">>));
ZuDerive(AdmitFn, (ZmFn<void(PasskeyStart, AdmitDoneFn),
  ZmFnHeapID<"Zum.AdmitFn">>));
struct AuthRoute {
  OIDCConfig	oidc;
  unsigned	type = 0;
};
namespace AuthRouteType { enum { Local, OIDC, Error }; }
ZuDerive(AuthRouteDoneFn, (ZmFn<void(AuthRoute),
  ZmFnHeapID<"Zum.AuthRouteDoneFn">>));
ZuDerive(AuthRouteFn, (ZmFn<void(AppID, String, AuthRouteDoneFn),
  ZmFnHeapID<"Zum.AuthRouteFn">>));

class ZumAPI Server {
  Server(const Server &) = delete;
  Server &operator =(const Server &) = delete;

public:
  Server() = default;

  bool init(
    DB *, DBContext *, Requests *, ServerConfig,
    ClockFn, PageFn, PolicyFn, AdmitFn, SignFn, OIDCHTTPFn = {},
    AuthRouteFn = {});
  // Cancel OIDC transactions; retain callbacks/configuration until the
  // owner has drained HTTP and Zdb, then call final().
  void stop();
  void final();

  unsigned authMethod() const { return m_config.authMethod; }

  void authorize(AppID, String query, ServerFn);
  void authorize(AppID, String query, String cookie, ServerFn);
  void token(AppID, String form, String authorization, ServerFn);
  void revoke(AppID, String form, String authorization, ServerFn);
  void login(AppID, String cookie, ServerFn);
  void login(AppID, String form, String cookie, ServerFn);
  void consent(AppID, String form, String cookie, ServerFn);
  void logout(AppID, String form, String cookie, ServerFn);
  void metadata(AppID, ServerFn);
  void ready(ServerFn);
  void jwks(AppID, ServerFn);
  void userInfo(AppID, String authorization, ServerFn);
  void passkeyBegin(AppID, String json, ServerFn);
  void passkeyFinish(
    AppID, String query, String cookie, String json, ServerFn);
  void oidcCallback(AppID, String query, String cookie, ServerFn);

private:
  void app_(AppID, AppServerFn);
  void policy_(AppServer, AppServerFn);
  void authorize_(AppServer, String, String, ServerFn);
  void login_(AppServer, String, ServerFn);
  void login_(AppServer, String, String, ServerFn);
  void consent_(AppServer, String, String, ServerFn);
  void logout_(AppServer, String, String, ServerFn);
  void userInfo_(AppServer, String, ServerFn);
  void userInfoVerify_(AppServer, String, ServerFn);
  void passkeyBegin_(AppServer, String, ServerFn);
  void passkeyFinish_(AppServer, String, String, String, ServerFn);
  void oidcCallback_(AppServer, String, String, ServerFn);
  int64_t now_() const;
  ZuTime deadline_() const;
  bool cookie_(String &, Bytes &);
  String setCookie_(
    ZuCSpan, bool clear = false, int64_t lifetime = 0) const;
  bool binding_(String &, Bytes &) const;
  bool binding_(String &, String &, Bytes &) const;
  String csrf_(ZuBSpan) const;
  void passkeyAdmitted_(
    AppServer, PasskeyStart, PasskeyAdmission, Bytes,
    String setCookie, ServerFn);
  void finishGrant_(AppServer, Bytes, Bytes, String, ServerFn);
  void sessionReply_(Bytes, String, String, uint32_t, uint32_t, ServerFn);
  void userInfo_(String, ServerFn);
  void userInfo_(Principal, ServerFn);
  void authorizeReply_(int, AuthorizeResult, String, ServerFn);
  void sessionAuthorize_(
    AuthorizeResult, Bytes, String, String, uint32_t, ServerFn);

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Requests	*m_requests = nullptr;
  ServerConfig	m_config;
  ClockFn	m_clock;
  PageFn	m_page;
  PolicyFn	m_policy;
  AdmitFn	m_admit;
  SignFn	m_sign;
  Ztls::Random	m_rng;
  Ztls::Random	m_cookieRng;
  OIDC		m_oidc;
  AuthRouteFn	m_authRoute;
};

} // namespace Zum

#endif /* zumd_server_HH */
