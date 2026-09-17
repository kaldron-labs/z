//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_token.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>

#include <zlib/zumd_admin.hh>


#include <zlib/ZtlsRandom.hh>

namespace Zum {

static void clientToken(
  DBContext *, Ztls::Random &, String issuer, Client, bool requestedPresent,
  String requested, SignKey, int64_t now, int64_t expires,
  JWTLimits, SignFn, TokenFn);
static void refreshToken(
  DBContext *, Ztls::Random &, Refresh, Bytes presentedDigest,
  Client, bool requestedPresent, String requested, SignKey,
  int64_t now, int64_t expires, unsigned generationLimit,
  unsigned spentLimit, JWTLimits, RefreshRevokeFn, SignFn, TokenFn);
static void codeToken(
  DB *, DBContext *, Ztls::Random &, Grant, Bytes codeDigest,
  Client, bool requestedPresent, String requested, SignKey,
  int64_t now, int64_t accessExpires, int64_t refreshExpires,
  JWTLimits, SignFn, TokenFn);

class TokenResult_ : public ZumObject {
public:
  TokenResult_(int error_, TokenResponse response_) :
    error{error_}, response{ZuMv(response_)} { }

  ~TokenResult_() { tokenClear(response); }

  int			error;
  TokenResponse	response;
};

class TokenComplete_ : public ZumObject {
public:
  TokenComplete_(TokenFn complete) : m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, TokenResponse response)
  {
    ZmRef<TokenResult_> delivery = new TokenResult_{error, ZuMv(response)};
    m_request->complete([
      self = ZmRef<TokenComplete_>{this}, delivery = ZuMv(delivery)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(delivery->error, ZuMv(delivery->response));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(OAuthError::TemporarilyUnavailable, TokenResponse{});
  }

private:
  ZmRef<Request>	m_request;
  TokenFn	m_complete;
};

class ClientToken_ : public ZumPolymorph {
public:
  ClientToken_(
      DBContext *context, Ztls::Random *rng, String issuer, Client client,
      bool requestedPresent, String requested, SignKey key,
      int64_t now, int64_t expires, JWTLimits limits,
      SignFn sign, TokenFn complete) :
    m_context{context}, m_rng{rng}, m_issuer{ZuMv(issuer)},
    m_client{ZuMv(client)}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_key{ZuMv(key)},
    m_now{now}, m_expires{expires}, m_limits{limits}, m_sign{ZuMv(sign)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_issuer || !m_sign ||
	m_key.state != State::Active || !m_key.id ||
	bool(m_key.providerRef) == bool(m_key.privateMaterial) ||
	m_key.notBefore > m_now ||
	(m_key.retireAfter && m_expires > m_key.retireAfter) ||
	m_now <= 0 || m_expires <= m_now) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    loadClientAuth(m_context, m_issuer, m_client,
      m_requestedPresent, m_requested, [
      self = ZmRef<ClientToken_>{this}
    ](int error, AuthorityData data) mutable {
      self->authority_(error, ZuMv(data));
    });
  }

private:
  void finish_(int error, TokenResponse response)
  {
    if (m_done) {
      tokenClear(response);
      return;
    }
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(response));
  }

  void authority_(int error, AuthorityData data)
  {
    if (error) {
      finish_(error == AuthorityError::Invalid ?
	OAuthError::InvalidClient : OAuthError::InvalidScope, {});
      return;
    }
    m_authVersion = data.app.authVersion;
    m_appID = data.app.id;
    AccessClaims claims;
    if (!clientClaims(*m_rng, m_issuer, data.app, data.client, data.selection,
	data.actions, data.actionRecords, data.clientAppID,
	m_now, m_expires, claims) ||
	!jwtPrepare(claims, m_key.id, m_limits, m_prepared)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_scope = ZuMv(data.selection.scope);
    auto sign = ZuMv(m_sign);
    sign(m_key, m_prepared.digest, [
      self = ZmRef<ClientToken_>{this}
    ](Bytes signature) mutable {
      self->signed_(ZuMv(signature));
    });
  }

  void signed_(Bytes signature)
  {
    if (m_done || m_signDone) return;
    m_signDone = true;
    if (!signature || !jwtFinish(m_prepared, signature, m_limits)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    TokenResponse response{
      .accessToken = ZuMv(m_prepared.token),
      .scope = ZuMv(m_scope),
      .expiresIn = uint64_t(m_expires - m_now)
    };
    tokenRelease(m_context, m_issuer, m_appID, {}, m_authVersion, m_now,
      ZuMv(response), [self = ZmRef<ClientToken_>{this}](
	  bool ok, TokenResponse response) mutable {
	self->finish_(ok ? TokenIssue::OK : OAuthError::InvalidClient,
	  ZuMv(response));
      });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_issuer;
  Client	m_client;
  String	m_requested;
  bool		m_requestedPresent = false;
  SignKey	m_key;
  int64_t	m_now = 0;
  int64_t	m_expires = 0;
  JWTLimits	m_limits;
  SignFn	m_sign;
  TokenFn	m_complete;
  PreparedJWT	m_prepared;
  String	m_scope;
  AppID		m_appID = 0;
  uint64_t	m_authVersion = 0;
  bool		m_signDone = false;
  bool		m_done = false;
};

class RefreshToken_ : public ZumPolymorph {
public:
  RefreshToken_(
      DBContext *context, Ztls::Random *rng, Refresh family,
      Bytes presentedDigest, Client client, bool requestedPresent,
      String requested, SignKey key, int64_t now, int64_t expires,
      unsigned generationLimit, unsigned spentLimit, JWTLimits limits,
      RefreshRevokeFn event, SignFn sign, TokenFn complete) :
    m_context{context}, m_rng{rng}, m_family{ZuMv(family)},
    m_presentedDigest{ZuMv(presentedDigest)}, m_client{ZuMv(client)},
    m_requested{ZuMv(requested)}, m_requestedPresent{requestedPresent},
    m_key{ZuMv(key)}, m_now{now}, m_expires{expires},
    m_generationLimit{generationLimit}, m_spentLimit{spentLimit},
    m_limits{limits}, m_event{ZuMv(event)}, m_sign{ZuMv(sign)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_presentedDigest || !m_sign ||
	m_family.state != State::Active || m_family.expires <= m_now ||
	m_family.clientID != m_client.id ||
	m_key.state != State::Active || !m_key.id ||
	bool(m_key.providerRef) == bool(m_key.privateMaterial) ||
	m_key.notBefore > m_now ||
	m_now <= 0 || m_expires <= m_now ||
	!m_generationLimit || !m_spentLimit) {
      finish_(OAuthError::InvalidGrant, {});
      return;
    }
    switch (refreshMatch(m_family, m_presentedDigest)) {
      case RefreshMatch::Unknown:
	finish_(OAuthError::InvalidGrant, {});
	return;
      case RefreshMatch::Spent:
	reuse_();
	return;
      default:
	break;
    }
    loadGrantAuth(m_context, m_family, m_client,
      m_requestedPresent, m_requested, m_now,
      [self = ZmRef<RefreshToken_>{this}](
	  int error, AuthorityData data) mutable {
	self->authority_(error, ZuMv(data));
      });
  }

private:
  void finish_(int error, TokenResponse response)
  {
    if (m_done) { tokenClear(response); return; }
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(response));
  }

  void reuseAudit_()
  {
    logEvent(Audit{
      .time = m_now,
      .issuer = m_family.issuer,
      .actor = m_family.clientID,
      .target = auditID(m_family.id),
      .event = AuditEvent::RefreshReuse,
      .outcome = AuditOutcome::Failure
    });
    finish_(OAuthError::InvalidGrant, {});
  }

  void reuse_()
  {
    auto refresh = m_context->refresh;
    refresh->run(0, [self = ZmRef<RefreshToken_>{this}, refresh]() {
      Bytes id = self->m_family.id;
      refresh->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRow<Refresh> *row) mutable {
	if (row && row->data().issuer == self->m_family.issuer &&
	    row->data().state == State::Active &&
	    row->data().expires > self->m_now &&
	    refreshMatch(row->data(), self->m_presentedDigest) ==
	      RefreshMatch::Spent) {
	  row->data().state = State::Revoked;
	  if (!row->commit()) {
	    self->finish_(OAuthError::ServerError, {});
	    return;
	  }
	  if (self->m_event) self->m_event(self->m_family.appID,
	    RefreshID{self->m_family.issuer, auditID(self->m_family.id)},
	    self->m_family.expires);
	  self->reuseAudit_();
	  return;
	}
	self->finish_(OAuthError::InvalidGrant, {});
      });
    });
  }

  void authority_(int error, AuthorityData data)
  {
    if (error) {
      finish_(error == AuthorityError::Invalid ?
	OAuthError::InvalidGrant : OAuthError::InvalidScope, {});
      return;
    }
    if (data.authorityDeadline)
      m_expires = m_expires < data.authorityDeadline ?
        m_expires : data.authorityDeadline;
    if (m_expires <= m_now ||
        (m_key.retireAfter && m_expires > m_key.retireAfter)) {
      finish_(OAuthError::InvalidGrant, {});
      return;
    }
    m_authVersion = data.app.authVersion;
    m_requestedRoleIDs = data.selection.roleIDs;
    m_actions = data.actions;
    AccessClaims claims;
    if (!interactiveClaims(*m_rng, m_family.issuer, data.app, data.user, data.client,
	data.selection, data.actions, data.actionRecords,
	m_family.credentialID ? ZuCSpan{"passkey"} : ZuCSpan{"oidc"},
	m_family.authTime,
	m_now, m_expires, claims) ||
	!jwtPrepare(claims, m_key.id, m_limits, m_prepared)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_hasID = scopeContains(data.selection.scope, "openid");
    if (m_hasID) {
      IDClaims id;
      if (!idClaims(m_family.issuer, data.user, data.client, data.selection,
          m_family.nonce,
          m_family.credentialID ? ZuCSpan{"passkey"} : ZuCSpan{"oidc"},
          m_family.authTime, m_now, m_expires, id) ||
          !idTokenPrepare(id, m_key.id, m_limits, m_idPrepared)) {
	finish_(OAuthError::ServerError, {});
	return;
      }
    }
    m_response.scope = ZuMv(data.selection.scope);
    m_sign(m_key, m_prepared.digest, [
      self = ZmRef<RefreshToken_>{this}
    ](Bytes signature) mutable { self->signed_(ZuMv(signature)); });
  }

  void signed_(Bytes signature)
  {
    if (m_done || m_signDone) return;
    m_signDone = true;
    if (!signature || !jwtFinish(m_prepared, signature, m_limits)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_response.accessToken = ZuMv(m_prepared.token);
    m_response.expiresIn = uint64_t(m_expires - m_now);
    if (m_hasID) {
      m_sign(m_key, m_idPrepared.digest, [
        self = ZmRef<RefreshToken_>{this}
      ](Bytes signature) mutable { self->idSigned_(ZuMv(signature)); });
      return;
    }
    rotate_();
  }

  void idSigned_(Bytes signature)
  {
    if (m_done || m_idSignDone) return;
    m_idSignDone = true;
    if (!signature || !jwtFinish(m_idPrepared, signature, m_limits)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_response.idToken = ZuMv(m_idPrepared.token);
    rotate_();
  }

  void rotate_()
  {
    refreshFinish(m_context, *m_rng, m_family.id, m_presentedDigest,
      m_family.issuer, m_family.appID, m_response.scope,
      ZuMv(m_requestedRoleIDs), ZuMv(m_actions), m_authVersion,
      m_family.userID, m_family.userVersion, m_now,
      m_generationLimit, m_spentLimit, [
	self = ZmRef<RefreshToken_>{this}
      ](RefreshRotate::T result, String refresh) mutable {
	self->rotated_(result, ZuMv(refresh));
      });
  }

  void rotated_(RefreshRotate::T result, String refresh)
  {
    if (m_done) {
      if (refresh && refresh.mutable_())
	ZuClear(refresh.data(), refresh.length());
      return;
    }
    if (result == RefreshRotate::Reused) {
      tokenClear(m_response);
      reuseAudit_();
      return;
    }
    if (result != RefreshRotate::Rotated) {
      tokenClear(m_response);
      finish_(result == RefreshRotate::Invalid ?
	OAuthError::ServerError : OAuthError::InvalidGrant, {});
      return;
    }
    m_response.refreshToken = ZuMv(refresh);
    tokenRelease(m_context, m_family.issuer, m_family.appID,
      m_family.id, m_authVersion,
      m_now, ZuMv(m_response), [self = ZmRef<RefreshToken_>{this}](
	  bool ok, TokenResponse response) mutable {
	self->finish_(ok ? TokenIssue::OK : OAuthError::InvalidGrant,
	  ZuMv(response));
      });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Refresh		m_family;
  Bytes		m_presentedDigest;
  Client	m_client;
  String	m_requested;
  bool		m_requestedPresent = false;
  SignKey	m_key;
  int64_t	m_now = 0;
  int64_t	m_expires = 0;
  unsigned	m_generationLimit = 0;
  unsigned	m_spentLimit = 0;
  JWTLimits	m_limits;
  RefreshRevokeFn m_event;
  SignFn	m_sign;
  TokenFn	m_complete;
  PreparedJWT	m_prepared;
  PreparedJWT	m_idPrepared;
  TokenResponse	m_response;
  IDVec		m_requestedRoleIDs;
  ZtBitmap	m_actions;
  uint64_t	m_authVersion = 0;
  bool		m_signDone = false;
  bool		m_idSignDone = false;
  bool		m_hasID = false;
  bool		m_done = false;
};

class CodeToken_ : public ZumPolymorph {
public:
  CodeToken_(
      DB *db, DBContext *context, Ztls::Random *rng, Grant code,
      Bytes codeDigest, Client client, bool requestedPresent,
      String requested, SignKey key, int64_t now, int64_t accessExpires,
      int64_t refreshExpires, JWTLimits limits, SignFn sign,
      TokenFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_code{ZuMv(code)},
    m_codeDigest{ZuMv(codeDigest)}, m_client{ZuMv(client)},
    m_requested{ZuMv(requested)}, m_requestedPresent{requestedPresent},
    m_key{ZuMv(key)}, m_now{now}, m_accessExpires{accessExpires},
    m_refreshExpires{refreshExpires}, m_limits{limits},
    m_sign{ZuMv(sign)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng ||
	m_code.id.length() != OpaqueIDSize ||
	!m_codeDigest || !m_sign || m_key.state != State::Active ||
	!m_key.id ||
	bool(m_key.providerRef) == bool(m_key.privateMaterial) ||
	m_key.notBefore > m_now ||
	m_now <= 0 ||
	m_accessExpires <= m_now || m_refreshExpires <= m_now) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    loadGrantAuth(m_context, m_code, m_client,
      m_requestedPresent, m_requested, m_now,
      [self = ZmRef<CodeToken_>{this}](
	  int error, AuthorityData data) mutable {
	self->authority_(error, ZuMv(data));
      });
  }

private:
  void finish_(int error, TokenResponse response)
  {
    if (m_done) {
      tokenClear(response);
      return;
    }
    m_done = true;
    if (error != TokenIssue::OK) {
      tokenClear(m_response);
      tokenClear(response);
    }
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(response));
  }

  void authority_(int error, AuthorityData data)
  {
    if (error) {
      finish_(error == AuthorityError::Invalid ?
	OAuthError::InvalidGrant : OAuthError::InvalidScope, {});
      return;
    }
    if (data.authorityDeadline)
      m_accessExpires = m_accessExpires < data.authorityDeadline ?
        m_accessExpires : data.authorityDeadline;
    if (m_accessExpires <= m_now ||
        (m_key.retireAfter && m_accessExpires > m_key.retireAfter)) {
      finish_(OAuthError::InvalidGrant, {});
      return;
    }
    m_authVersion = data.app.authVersion;
    m_requestedRoleIDs = data.selection.roleIDs;
    m_actions = data.actions;
    AccessClaims claims;
    if (!interactiveClaims(*m_rng, m_code.issuer, data.app, data.user, data.client,
	data.selection, data.actions, data.actionRecords,
	m_code.credentialID ? ZuCSpan{"passkey"} : ZuCSpan{"oidc"}, m_code.authTime,
	m_now, m_accessExpires, claims) ||
	!jwtPrepare(claims, m_key.id, m_limits, m_prepared)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_hasID = scopeContains(data.selection.scope, "openid");
    if (m_hasID) {
      IDClaims id;
      if (!idClaims(m_code.issuer, data.user, data.client, data.selection,
          m_code.nonce,
          m_code.credentialID ? ZuCSpan{"passkey"} : ZuCSpan{"oidc"},
          m_code.authTime, m_now, m_accessExpires, id) ||
          !idTokenPrepare(id, m_key.id, m_limits, m_idPrepared)) {
	finish_(OAuthError::ServerError, {});
	return;
      }
    }
    m_response.scope = ZuMv(data.selection.scope);
    m_sign(m_key, m_prepared.digest, [
      self = ZmRef<CodeToken_>{this}
    ](Bytes signature) mutable { self->signed_(ZuMv(signature)); });
  }

  void signed_(Bytes signature)
  {
    if (m_done || m_signDone) return;
    m_signDone = true;
    if (!signature || !jwtFinish(m_prepared, signature, m_limits)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_response.accessToken = ZuMv(m_prepared.token);
    m_response.expiresIn = uint64_t(m_accessExpires - m_now);
    if (m_hasID) {
      m_sign(m_key, m_idPrepared.digest, [
        self = ZmRef<CodeToken_>{this}
      ](Bytes signature) mutable { self->idSigned_(ZuMv(signature)); });
      return;
    }
    tokens_();
  }

  void idSigned_(Bytes signature)
  {
    if (m_done || m_idSignDone) return;
    m_idSignDone = true;
    if (!signature || !jwtFinish(m_idPrepared, signature, m_limits)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_response.idToken = ZuMv(m_idPrepared.token);
    tokens_();
  }

  void tokens_()
  {
    if (!m_client.refreshAllowed ||
	!(m_client.grants & ClientGrant::RefreshToken) ||
	!scopeContains(m_response.scope, "offline_access")) {
      consume_();
      return;
    }
    CodeFamily family;
    if (!codeFamilyPrepare(*m_rng, m_code, m_codeDigest,
      m_response.scope, ZuMv(m_requestedRoleIDs), ZuMv(m_actions), m_authVersion, m_now,
	m_refreshExpires, family, m_response.refreshToken)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    m_familyID = family.familyID;
    ZdbSagaID sagaID;
    ZuAssert((sizeof(sagaID) == 16));
    memcpy(&sagaID, m_code.id.data(), sizeof(sagaID));
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(family));
    if (!sagaSubmit(m_db, sagaID, ZuMv(saga),
      SagaFn{ZmRef<CodeToken_>{this}, ZmFnPtr<&CodeToken_::sagaSubmit_>{}},
      SagaFn{ZmRef<CodeToken_>{this}, ZmFnPtr<&CodeToken_::saga_>{}},
      ZuTime{m_refreshExpires}))
      sagaSubmit_(false);
  }

  void consume_()
  {
    codeConsume(m_context, m_code.id, m_codeDigest, m_now,
      [self = ZmRef<CodeToken_>{this}](bool ok) mutable {
        if (!ok) {
          self->finish_(OAuthError::InvalidGrant, {});
          return;
        }
        self->release_();
      });
  }

  void sagaSubmit_(bool ok) { if (!ok) finish_(OAuthError::ServerError, {}); }

  void saga_(bool ok)
  {
    if (m_done) return;
    if (!ok) {
      finish_(OAuthError::InvalidGrant, {});
      return;
    }
    release_();
  }

  void release_()
  {
    tokenRelease(m_context, m_code.issuer, m_code.appID, m_familyID, m_authVersion,
      m_now, ZuMv(m_response), [self = ZmRef<CodeToken_>{this}](
	  bool ok, TokenResponse response) mutable {
	self->finish_(ok ? TokenIssue::OK : OAuthError::InvalidGrant,
	  ZuMv(response));
      });
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Grant		m_code;
  Bytes		m_codeDigest;
  Client	m_client;
  String	m_requested;
  bool		m_requestedPresent = false;
  SignKey	m_key;
  int64_t	m_now = 0;
  int64_t	m_accessExpires = 0;
  int64_t	m_refreshExpires = 0;
  JWTLimits	m_limits;
  SignFn	m_sign;
  TokenFn	m_complete;
  PreparedJWT	m_prepared;
  PreparedJWT	m_idPrepared;
  TokenResponse	m_response;
  IDVec		m_requestedRoleIDs;
  ZtBitmap	m_actions;
  Bytes		m_familyID;
  uint64_t	m_authVersion = 0;
  bool		m_signDone = false;
  bool		m_idSignDone = false;
  bool		m_hasID = false;
  bool		m_done = false;
};

class TokenRequest_ : public ZumPolymorph {
public:
  TokenRequest_(
      DB *db, DBContext *context, Ztls::Random *rng,
      String form, String authorization, TokenConfig config,
      SignFn sign, TokenFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_form{ZuMv(form)},
    m_authorization{ZuMv(authorization)}, m_config{ZuMv(config)},
    m_sign{ZuMv(sign)}, m_complete{ZuMv(complete)} { }

  ~TokenRequest_() { clear_(); }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_config.issuer ||
	!m_config.appID) {
      finish_(OAuthError::ServerError);
      return;
    }
    if (!m_form.mutable_()) m_form.length(m_form.length());
    bool parsed = parseToken({m_form.data(), m_form.length()}, m_params);
    int error = parsed ? validateToken(m_params, m_grant) :
      ProfileError::Unsupported;
    if (error) {
      finish_(error == ProfileError::Unsupported ?
	OAuthError::UnsupportedGrantType : OAuthError::InvalidRequest);
      return;
    }
    if (m_authorization) {
      if (!m_authorization.mutable_())
	m_authorization.length(m_authorization.length());
      if (!parseBasic({m_authorization.data(), m_authorization.length()},
	  m_basic)) {
	clientAuthFail_(OAuthError::InvalidClient, {});
	return;
      }
      m_hasBasic = true;
    }
    ZuCSpan clientID = m_hasBasic ? m_basic.clientID : m_params.clientID;
    if (!clientID) {
	clientAuthFail_(OAuthError::InvalidClient, {});
      return;
    }
    String id{clientID};
    m_context->clients->run(0, [
      self = ZmRef<TokenRequest_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Client> row) mutable { self->client_(ZuMv(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_form && m_form.mutable_())
      ZuClear(m_form.data(), m_form.length());
    if (m_authorization && m_authorization.mutable_())
      ZuClear(m_authorization.data(), m_authorization.length());
    m_form.null();
    m_authorization.null();
    m_params = {};
    m_basic = {};
  }

  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    clear_();
    auto complete = ZuMv(m_complete);
    complete(error, TokenResponse{});
  }

  void clientAuthFail_(int error, String actor)
  {
    clear_();
    logEvent(Audit{
      .time = m_config.now,
      .issuer = m_config.issuer,
      .actor = ZuMv(actor),
      .event = AuditEvent::ClientAuthentication,
      .outcome = AuditOutcome::Failure
    });
    finish_(error);
  }

  void client_(ZdbRowRef<Client> row)
  {
    if (!row || row->data().appID != m_config.appID) {
	ZuCSpan id = m_hasBasic ? m_basic.clientID : m_params.clientID;
	clientAuthFail_(OAuthError::InvalidClient, id);
      return;
    }
    m_client = row->data();
    int error = authenticateClient(m_client, m_grant, m_params,
      m_hasBasic ? &m_basic : nullptr, m_config.now);
    if (error) {
	clientAuthFail_(error == ClientAuth::UnauthorizedGrant ?
	  OAuthError::UnauthorizedClient : OAuthError::InvalidClient,
	  m_client.id);
      return;
    }
    if (m_authorization && m_authorization.mutable_())
      ZuClear(m_authorization.data(), m_authorization.length());
    m_authorization.null();
    m_basic = {};
    if (m_grant == TokenGrant::ClientCredentials) {
      m_requested = m_params.scope;
      m_requestedPresent = m_params.has(TokenParams::Scope);
      loadKey_();
      return;
    }
    ZuCSpan opaque = m_grant == TokenGrant::AuthorizationCode ?
      m_params.code : m_params.refreshToken;
    Bytes id;
    if (!opaqueParse(opaque, id, m_digest)) {
      finish_(OAuthError::InvalidGrant);
      return;
    }
    if (m_grant == TokenGrant::RefreshToken) {
      m_context->refresh->find<0>(0, ZuFwdTuple(ZuMv(id)), [
        self = ZmRef<TokenRequest_>{this}
      ](ZdbRowRef<Refresh> row) mutable { self->refresh_(ZuMv(row)); });
      return;
    }
    m_context->grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<TokenRequest_>{this}
    ](ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
  }

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row || row->data().issuer != m_config.issuer) {
      finish_(OAuthError::InvalidGrant);
      return;
    }
    Grant grant = row->data();
    m_requested = m_params.scope;
    m_requestedPresent = m_params.has(TokenParams::Scope);
    if (m_grant == TokenGrant::AuthorizationCode) {
      if (!codeMatches(grant, m_digest, m_client.id,
	  m_params.redirectURI, m_params.codeVerifier, m_config.now)) {
	finish_(OAuthError::InvalidGrant);
	return;
      }
    }
    m_grantRecord = ZuMv(grant);
    loadKey_();
  }

  void refresh_(ZdbRowRef<Refresh> row)
  {
    if (!row || row->data().issuer != m_config.issuer) {
      finish_(OAuthError::InvalidGrant); return;
    }
    m_refreshRecord = row->data();
    m_requested = m_params.scope;
    m_requestedPresent = m_params.has(TokenParams::Scope);
    loadKey_();
  }

  void loadKey_()
  {
    signKeyLoad(m_context, m_config.issuer, m_config.now,
      m_config.accessExpires, m_config.maxKeys, [
      self = ZmRef<TokenRequest_>{this}
    ](SignKey key) mutable { self->key_(ZuMv(key)); });
  }

  void key_(SignKey key)
  {
    if (!key.id) {
      finish_(OAuthError::ServerError);
      return;
    }
    clear_();
    auto complete = ZuMv(m_complete);
    if (m_grant == TokenGrant::ClientCredentials) {
      clientToken(m_context, *m_rng, ZuMv(m_config.issuer),
	ZuMv(m_client), m_requestedPresent, ZuMv(m_requested), ZuMv(key),
	m_config.now, m_config.accessExpires, m_config.jwtLimits,
	ZuMv(m_sign), ZuMv(complete));
    } else if (m_grant == TokenGrant::AuthorizationCode) {
      codeToken(m_db, m_context, *m_rng, ZuMv(m_grantRecord),
	ZuMv(m_digest), ZuMv(m_client), m_requestedPresent,
	ZuMv(m_requested), ZuMv(key), m_config.now,
	m_config.accessExpires, m_config.refreshExpires, m_config.jwtLimits,
	ZuMv(m_sign), ZuMv(complete));
    } else {
      refreshToken(m_context, *m_rng, ZuMv(m_refreshRecord),
	ZuMv(m_digest), ZuMv(m_client), m_requestedPresent,
	ZuMv(m_requested), ZuMv(key), m_config.now, m_config.accessExpires,
	m_config.generationLimit, m_config.spentLimit, m_config.jwtLimits,
	m_config.event, ZuMv(m_sign), ZuMv(complete));
    }
    m_done = true;
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_form;
  String	m_authorization;
  TokenConfig	m_config;
  SignFn	m_sign;
  TokenFn	m_complete;
  TokenParams	m_params;
  BasicAuth	m_basic;
  Client	m_client;
  Grant		m_grantRecord;
  Refresh	m_refreshRecord;
  String	m_requested;
  Bytes		m_digest;
  int		m_grant = TokenGrant::Invalid;
  bool		m_requestedPresent = false;
  bool		m_hasBasic = false;
  bool		m_done = false;
};

static void clientToken(
    DBContext *context, Ztls::Random &rng, String issuer, Client client,
    bool requestedPresent, String requested, SignKey key,
    int64_t now, int64_t expires, JWTLimits limits,
    SignFn sign, TokenFn complete)
{
  ZmRef<ClientToken_> request = new ClientToken_{context, &rng,
    ZuMv(issuer), ZuMv(client), requestedPresent, ZuMv(requested),
    ZuMv(key), now, expires, limits, ZuMv(sign), ZuMv(complete)};
  request->start();
}

static void refreshToken(
    DBContext *context, Ztls::Random &rng, Refresh family,
    Bytes presentedDigest, Client client, bool requestedPresent,
    String requested, SignKey key, int64_t now, int64_t expires,
    unsigned generationLimit, unsigned spentLimit, JWTLimits limits,
    RefreshRevokeFn event, SignFn sign, TokenFn complete)
{
  ZmRef<RefreshToken_> request = new RefreshToken_{context, &rng,
    ZuMv(family), ZuMv(presentedDigest), ZuMv(client), requestedPresent,
    ZuMv(requested), ZuMv(key), now, expires, generationLimit, spentLimit,
    limits, ZuMv(event), ZuMv(sign), ZuMv(complete)};
  request->start();
}

static void codeToken(
    DB *db, DBContext *context, Ztls::Random &rng, Grant code,
    Bytes codeDigest, Client client, bool requestedPresent,
    String requested, SignKey key, int64_t now, int64_t accessExpires,
    int64_t refreshExpires, JWTLimits limits, SignFn sign,
    TokenFn complete)
{
  ZmRef<CodeToken_> request = new CodeToken_{db, context, &rng,
    ZuMv(code), ZuMv(codeDigest), ZuMv(client), requestedPresent,
    ZuMv(requested), ZuMv(key), now, accessExpires, refreshExpires,
    limits, ZuMv(sign), ZuMv(complete)};
  request->start();
}

bool tokenRequest(
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, String form, String authorization,
    TokenConfig config, SignFn sign, TokenFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<TokenComplete_> state = new TokenComplete_{ZuMv(complete)};
  ZmRef<TokenRequest_> token = new TokenRequest_{db, context, &rng,
    ZuMv(form), ZuMv(authorization), ZuMv(config), ZuMv(sign),
    [state](int error, TokenResponse response) mutable {
      state->complete(error, ZuMv(response));
    }};
  return requests->run(deadline, [
    state, token
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    token->start();
  }, [state]() mutable { state->cancel(); });
}

class RevokeComplete_ : public ZumObject {
public:
  RevokeComplete_(RevokeFn complete) : m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error)
  {
    m_request->complete([
      self = ZmRef<RevokeComplete_>{this}, error
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error);
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(OAuthError::TemporarilyUnavailable);
  }

private:
  ZmRef<Request>	m_request;
  RevokeFn	m_complete;
};

class RevokeRequest_ : public ZumPolymorph {
public:
  RevokeRequest_(
      DBContext *context, String form, String authorization,
      RevokeConfig config, RevokeFn complete) :
    m_context{context}, m_form{ZuMv(form)},
    m_authorization{ZuMv(authorization)}, m_config{ZuMv(config)},
    m_complete{ZuMv(complete)} { }

  ~RevokeRequest_() { clear_(); }

  void start()
  {
    if (!m_context || !m_config.issuer || !m_config.appID ||
	m_config.now <= 0) {
      finish_(OAuthError::ServerError);
      return;
    }
    if (!m_form.mutable_()) m_form.length(m_form.length());
    if (!parseRevoke({m_form.data(), m_form.length()}, m_params) ||
	validateRevoke(m_params)) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    m_hasBasic = bool(m_authorization);
    if (m_hasBasic) {
      if (!m_authorization.mutable_())
	m_authorization.length(m_authorization.length());
      if (!parseBasic({m_authorization.data(), m_authorization.length()},
	  m_basic) || (m_params.has(RevokeParams::ClientID) &&
	  m_params.clientID != m_basic.clientID)) {
	finish_(OAuthError::InvalidClient);
	return;
      }
    }
    ZuCSpan id = m_hasBasic ? m_basic.clientID : m_params.clientID;
    if (!id) {
      finish_(OAuthError::InvalidClient);
      return;
    }
    String clientID{id};
    m_context->clients->run(0, [
      self = ZmRef<RevokeRequest_>{this}, clientID = ZuMv(clientID)
    ]() mutable {
      self->m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [
        self = ZuMv(self)
      ](ZdbRowRef<Client> row) mutable { self->client_(ZuMv(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_form && m_form.mutable_()) ZuClear(m_form.data(), m_form.length());
    if (m_authorization && m_authorization.mutable_())
      ZuClear(m_authorization.data(), m_authorization.length());
    m_form.null();
    m_authorization.null();
    m_params = {};
    m_basic = {};
  }

  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    clear_();
    auto complete = ZuMv(m_complete);
    complete(error);
  }

  void client_(ZdbRowRef<Client> row)
  {
    if (!row || row->data().appID != m_config.appID ||
	row->data().state != State::Active || row->data().owner) {
      finish_(OAuthError::InvalidClient);
      return;
    }
    m_client = row->data();
    if (m_client.type == ClientType::Confidential) {
      bool secret = m_hasBasic &&
	(Ztls::secretVerify(m_client.secretDigest, m_basic.secret) ||
	 (m_config.now > 0 && m_client.previousSecretExpires > m_config.now &&
	  m_client.previousSecretDigest && Ztls::secretVerify(
	    m_client.previousSecretDigest, m_basic.secret)));
      if (!secret) {
	finish_(OAuthError::InvalidClient);
	return;
      }
    } else if (m_hasBasic || m_params.clientID != m_client.id) {
      finish_(OAuthError::InvalidClient);
      return;
    }
    if (!opaqueParse(m_params.token, m_familyID, m_digest)) {
      access_();
      return;
    }
    auto refresh = m_context->refresh;
    Bytes id = m_familyID;
    refresh->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<RevokeRequest_>{this}
    ](ZdbRow<Refresh> *row) mutable {
      if (row) { self->grant_(row); return; }
      // Retain revocation for opaque grant records written by older stores.
      auto grants = self->m_context->grants;
      Bytes id = self->m_familyID;
      grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self](ZdbRow<Grant> *row) mutable {
        self->grantLegacy_(row);
      });
    });
  }

  void access_()
  {
    // JWT access tokens are self-contained and remain valid until exp.  The
    // OAuth revocation endpoint accepts the request but only refresh-family
    // revocation is propagated by SSF.
    finish_(RevokeIssue::OK);
  }

  void grant_(ZdbRow<Refresh> *row)
  {
    if (!row ||
	row->data().issuer != m_config.issuer ||
	row->data().clientID != m_client.id || row->data().owner ||
	refreshMatch(row->data(), m_digest) == RefreshMatch::Unknown) {
      finish_(RevokeIssue::OK);
      return;
    }
    if (row->data().state == State::Revoked) {
      finish_(RevokeIssue::OK);
      return;
    }
    if (row->data().state != State::Active) {
      finish_(RevokeIssue::OK);
      return;
    }
    row->data().state = State::Revoked;
    if (!row->commit()) {
      finish_(OAuthError::ServerError);
      return;
    }
    if (m_config.event && row->data().expires > m_config.now)
      m_config.event(m_config.appID,
      RefreshID{m_config.issuer, auditID(m_familyID)}, row->data().expires);
    logEvent(Audit{
      .time = m_config.now,
      .issuer = m_config.issuer,
      .actor = m_client.id,
      .subject = auditID(m_familyID),
      .event = AuditEvent::Revocation,
      .outcome = AuditOutcome::Success,
      .detail = "refresh token"
    });
    finish_(RevokeIssue::OK);
  }

  void grantLegacy_(ZdbRow<Grant> *row)
  {
    if (!row || row->data().issuer != m_config.issuer ||
        row->data().clientID != m_client.id || row->data().owner ||
        !Ztls::ctEqual(row->data().digest, m_digest) ||
        row->data().state != State::Active) {
      finish_(RevokeIssue::OK);
      return;
    }
    row->data().state = State::Revoked;
    if (!row->commit()) { finish_(OAuthError::ServerError); return; }
    logEvent(Audit{
      .time = m_config.now,
      .issuer = m_config.issuer,
      .actor = m_client.id,
      .subject = auditID(m_familyID),
      .event = AuditEvent::Revocation,
      .outcome = AuditOutcome::Success,
      .detail = "opaque grant"
    });
    finish_(RevokeIssue::OK);
  }

  DBContext	*m_context = nullptr;
  String	m_form;
  String	m_authorization;
  RevokeConfig	m_config;
  RevokeFn	m_complete;
  RevokeParams	m_params;
  BasicAuth	m_basic;
  Client	m_client;
  Bytes		m_familyID;
  Bytes		m_digest;
  bool		m_hasBasic = false;
  bool		m_done = false;
};

bool revokeRequest(
    Requests *requests, ZuTime deadline, DBContext *context,
    String form, String authorization, RevokeConfig config,
    RevokeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<RevokeComplete_> state = new RevokeComplete_{ZuMv(complete)};
  ZmRef<RevokeRequest_> revoke = new RevokeRequest_{context, ZuMv(form),
    ZuMv(authorization), ZuMv(config), [state](int error) mutable {
      state->complete(error);
    }};
  return requests->run(deadline, [state, revoke](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    revoke->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
