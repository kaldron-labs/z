//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// embedded Zum OAuth and passkey HTTP operation owner

#ifndef ZumServer_HH
#define ZumServer_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumAuthorize.hh>
#include <zlib/ZumDiscovery.hh>
#include <zlib/ZumOIDC.hh>
#include <zlib/ZumPasskey.hh>
#include <zlib/ZumToken.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zum {

namespace ReplyType {
  enum {
    Page, Redirect, OK, OAuthError, ClientError, Empty, Discovery,
    ServerError
  };
}

struct ServerReply {
  String	body;
  String	location;
  String	setCookie;
  unsigned	type = ReplyType::OAuthError;
};

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

struct ServerLimits {
  // Tunable bounds for unauthenticated HTTP inputs and database responses.
  unsigned		form = 16U<<10;
  unsigned		ceremonyQuery = 256;
  unsigned		json = 64U<<10;
  ZfCBOR::Limits	cbor{64U<<10, 8, 256, 64U<<10};
  JWTLimits		jwt;
  unsigned		credentialID = 1024;
  unsigned		cookie = 8U<<10;
  unsigned		jwks = 4;
  OIDCLimits		oidc;
  unsigned		oidcPending = 64;
};

struct ServerConfig {
  String	issuer;
  String	rpID;
  String	rpName;
  String	keyID;
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
  unsigned	refreshGenerations = 64;
  unsigned	spentTokens = 64;
  unsigned	authMethod = AuthMethod::Passkey;
};

using ServerFn = ZmFn<void(ServerReply), ZmFnHeapID<"Zum.ServerFn">>;
using ClockFn = ZmFn<int64_t(), ZmFnHeapID<"Zum.ClockFn">>;
using PageFn = ZmFn<String(Bytes, String), ZmFnHeapID<"Zum.PageFn">>;
using AdmitDoneFn = ZmFn<void(PasskeyAdmission),
  ZmFnHeapID<"Zum.AdmitDoneFn">>;
using AdmitFn = ZmFn<void(PasskeyStart, AdmitDoneFn),
  ZmFnHeapID<"Zum.AdmitFn">>;

class ZumAPI Server {
  Server(const Server &) = delete;
  Server &operator =(const Server &) = delete;

public:
  Server() = default;

  bool init(
    DB *, DBContext *, Requests *, ServerConfig,
    ClockFn, PageFn, PolicyFn, AdmitFn, SignFn, OIDCHTTPFn = {});
  void final();

  unsigned authMethod() const { return m_config.authMethod; }

  void authorize(String query, ServerFn);
  void token(String form, String authorization, ServerFn);
  void revoke(String form, String authorization, ServerFn);
  void metadata(ServerFn);
  void jwks(ServerFn);
  void passkeyBegin(String json, ServerFn);
  void passkeyFinish(
    String query, String cookie, String json, ServerFn);
  void oidcCallback(String query, String cookie, ServerFn);

private:
  int64_t now_() const;
  ZuTime deadline_() const;
  bool cookie_(String &, Bytes &);
  String setCookie_(ZuCSpan, bool clear = false) const;
  bool binding_(String &, Bytes &) const;
  void passkeyAdmitted_(
    PasskeyStart, PasskeyAdmission, Bytes, String setCookie, ServerFn);
  void finishGrant_(Bytes, Bytes, String, ServerFn);

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
};

} // namespace Zum

#endif /* ZumServer_HH */
