//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumAuthorize.hh>

#include <zlib/ZumAdmin.hh>


#include <zlib/ZtlsRandom.hh>

namespace Zum {

class AuthorizeComplete_ : public ZumObject {
public:
  AuthorizeComplete_(AuthorizeFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, AuthorizeResult result)
  {
    m_request->complete([
      self = ZmRef<AuthorizeComplete_>{this}, error,
      result = ZuMv(result)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(error, ZuMv(result));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(OAuthError::TemporarilyUnavailable, AuthorizeResult{});
  }

private:
  ZmRef<Request>	m_request;
  AuthorizeFn	m_complete;
};

class AuthorizeCodeResult_ : public ZumObject {
public:
  AuthorizeCodeResult_(int error_, String location_) :
    error{error_}, location{ZuMv(location_)} { }

  ~AuthorizeCodeResult_()
  {
    if (location && location.mutable_())
      ZuClear(location.data(), location.length());
  }

  int		error;
  String	location;
};

class AuthorizeCodeComplete_ : public ZumObject {
public:
  AuthorizeCodeComplete_(AuthorizeCodeFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, String location)
  {
    ZmRef<AuthorizeCodeResult_> delivery =
      new AuthorizeCodeResult_{error, ZuMv(location)};
    m_request->complete([
      self = ZmRef<AuthorizeCodeComplete_>{this}, delivery = ZuMv(delivery)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(delivery->error, ZuMv(delivery->location));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(OAuthError::TemporarilyUnavailable, String{});
  }

private:
  ZmRef<Request>	m_request;
  AuthorizeCodeFn m_complete;
};

class AuthorizeRequest_ : public ZumPolymorph {
public:
  AuthorizeRequest_(
      DBContext *context, Ztls::Random *rng, String query,
      Bytes bindingDigest, AuthorizeConfig config, AuthorizeFn complete) :
    m_context{context}, m_rng{rng}, m_query{ZuMv(query)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_config{ZuMv(config)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_bindingDigest || !m_config.issuer ||
	(m_config.passkey && (!m_config.rpID || !m_config.timeout)) ||
	m_config.now <= 0 || m_config.expires <= m_config.now) {
      finish_(OAuthError::ServerError);
      return;
    }
    if (!m_query.mutable_()) m_query.length(m_query.length());
    parseAuthorize({m_query.data(), m_query.length()}, m_params);
    m_profileError = validateAuthorize(m_params);
    if (!m_params.has(AuthorizeParams::ClientID) ||
	!m_params.has(AuthorizeParams::RedirectURI) ||
	!m_params.clientID || !m_params.redirectURI) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    String id{m_params.clientID};
    m_context->clients->run(0, [
      self = ZmRef<AuthorizeRequest_>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Client> row) mutable { self->client_(ZuMv(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_query && m_query.mutable_())
      ZuClear(m_query.data(), m_query.length());
    m_query.null();
    m_params = {};
  }

  void finish_(int error)
  {
    if (m_done) return;
    m_done = true;
    clear_();
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(m_result));
  }

  void client_(ZdbRowRef<Client> row)
  {
    if (!row || !authorizeClient(row->data(), m_params)) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    m_client = row->data();
    m_result.redirectURI = m_params.redirectURI;
    m_result.state = m_params.state;
    m_result.statePresent = m_params.has(AuthorizeParams::State);
    m_result.redirect = true;
    if (m_profileError) {
      finish_(m_profileError == ProfileError::Unsupported ?
	OAuthError::UnsupportedResponseType : OAuthError::InvalidRequest);
      return;
    }

    String issuer = m_config.issuer;
    m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
      self = ZmRef<AuthorizeRequest_>{this}
    ](ZdbRowRef<Issuer> row) mutable { self->issuer_(ZuMv(row)); });
  }

  void issuer_(ZdbRowRef<Issuer> row)
  {
    if (!row) {
      finish_(OAuthError::ServerError);
      return;
    }
    m_authVersion = row->data().authVersion;
    scope_();
  }

  void scope_()
  {
    if (m_scope >= m_client.scopeIDs.length()) {
      scopes_();
      return;
    }
    auto id = m_client.scopeIDs[m_scope];
    m_context->scopes->find<0>(0, ZuFwdTuple(id), [
      self = ZmRef<AuthorizeRequest_>{this}
    ](ZdbRowRef<Scope> row) mutable {
      if (row) self->m_scopes.push(row->data());
      ++self->m_scope;
      self->scope_();
    });
  }

  void scopes_()
  {
    ScopeSelection selection;
    if (selectScopes(m_client, m_params.scope, m_scopes, selection)) {
      finish_(OAuthError::InvalidScope);
      return;
    }
    Grant grant;
    if (!authorizationBegin(*m_rng, grant, m_config.issuer, m_params,
	selection, m_config.passkey, m_bindingDigest, m_authVersion,
	m_config.now, m_config.expires)) {
      finish_(OAuthError::ServerError);
      return;
    }
    m_result.ceremonyID = grant.id;
    if (m_config.passkey)
      m_result.options = assertionOptions(
	grant.challenge, m_config.rpID, m_config.timeout);
    m_result.redirect = false;
    clear_();
    authorizationInsert(m_context, ZuMv(grant), [
      self = ZmRef<AuthorizeRequest_>{this}
    ](bool ok) mutable {
      self->finish_(ok ? AuthorizeIssue::OK : OAuthError::ServerError);
    });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  String	m_query;
  Bytes		m_bindingDigest;
  AuthorizeConfig m_config;
  AuthorizeFn	m_complete;
  AuthorizeParams m_params;
  AuthorizeResult m_result;
  Client	m_client;
  ScopeVec	m_scopes;
  uint64_t	m_authVersion = 0;
  unsigned	m_scope = 0;
  int		m_profileError = ProfileError::OK;
  bool		m_done = false;
};

class AuthorizeFinish_ : public ZumPolymorph {
public:
  AuthorizeFinish_(
      DBContext *context, Ztls::Random *rng, Bytes ceremonyID,
      Bytes bindingDigest, AssertionInput input,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_input{ZuMv(input)},
    m_config{ZuMv(config)}, m_policy{ZuMv(policy)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_ceremonyID || !m_bindingDigest ||
	!m_config.origin || !m_config.rpID || m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    assertionVerify(m_context, m_ceremonyID, m_bindingDigest,
      ZuMv(m_input), m_config.origin, m_config.rpID, m_config.now,
      [self = ZmRef<AuthorizeFinish_>{this}](
	  int error, User user, Cred cred, Grant grant,
	  AssertionResult result) mutable {
	self->asserted_(error, ZuMv(user), ZuMv(cred), ZuMv(grant), result);
      });
  }

private:
  void finish_(int error, String location)
  {
    if (m_done) {
      if (location && location.mutable_())
	ZuClear(location.data(), location.length());
      return;
    }
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(location));
  }

  void asserted_(
      int error, User user, Cred cred, Grant grant, AssertionResult result)
  {
    if (!grant.issuer) {
      assertionDone_(error, ZuMv(user), ZuMv(cred), ZuMv(grant));
      return;
    }
    Audit audit{
      .time = m_config.now,
      .issuer = grant.issuer,
      .actor = grant.clientID,
      .subject = user.handle ? auditID(user.handle) : String{},
      .target = cred.id ? auditID(cred.id) : String{},
      .event = AuditEvent::Authentication,
      .outcome = AuditOutcome::T(
	error ? AuditOutcome::Failure : AuditOutcome::Success),
	.detail = (error == WebAuthnError::Counter ||
	  result.counter == CounterState::Regression) ?
	String{"counter regression"} : String{}
    };
    auditWrite(m_context, ZuMv(audit), [
      self = ZmRef<AuthorizeFinish_>{this}, error,
      user = ZuMv(user), cred = ZuMv(cred), grant = ZuMv(grant)
    ](int auditError) mutable {
      if (auditError) {
	self->finish_(OAuthError::ServerError, {});
	return;
      }
      self->assertionDone_(error,
	ZuMv(user), ZuMv(cred), ZuMv(grant));
    });
  }

  void assertionDone_(int error, User user, Cred cred, Grant grant)
  {
    if (error) {
      finish_(error == WebAuthnError::Storage ?
	OAuthError::ServerError : OAuthError::AccessDenied, {});
      return;
    }
    IDVec roleIDs = user.roleIDs;
    loadUserAuth(m_context, ZuMv(grant), ZuMv(user),
      ZuMv(cred), ZuMv(roleIDs), [self = ZmRef<AuthorizeFinish_>{this}](
	  int error, AuthorityData data) mutable {
	self->authority_(error, ZuMv(data));
      });
  }

  void authority_(int error, AuthorityData data)
  {
    if (error) {
      finish_(error == AuthorityError::Invalid ?
	OAuthError::AccessDenied : OAuthError::InvalidScope, {});
      return;
    }
    m_authority = ZuMv(data);
    auto policy = ZuMv(m_policy);
    policy(m_authority.user, m_authority.client, m_authority.selection,
      m_authority.actions, [self = ZmRef<AuthorizeFinish_>{this}](
	bool ok, ZtBitmap actions) mutable {
      self->policy_(ok, ZuMv(actions));
    });
  }

  void policy_(bool ok, ZtBitmap actions)
  {
    if (m_done || m_policyDone) return;
    m_policyDone = true;
    if (!ok) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    actions &= m_authority.actions;
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, m_authority.cred.id,
      m_authority.principalRoleIDs, ZuMv(actions),
      m_authority.issuer.authVersion, m_authority.user.authVersion,
      m_config.now, m_config.codeExpires, [
	self = ZmRef<AuthorizeFinish_>{this}
      ](bool ok, String code) mutable {
	self->finished_(ok, ZuMv(code));
      });
  }

  void finished_(bool ok, String code)
  {
    if (!ok) {
      if (code && code.mutable_()) ZuClear(code.data(), code.length());
      finish_(OAuthError::ServerError, {});
      return;
    }
    String location = codeRedirect(m_authority.grant, code);
    if (code && code.mutable_()) ZuClear(code.data(), code.length());
    finish_(AuthorizeIssue::OK, ZuMv(location));
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  AssertionInput m_input;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  AuthorityData m_authority;
  bool		m_policyDone = false;
  bool		m_done = false;
};

class AuthorizeOIDCFinish_ : public ZumPolymorph {
public:
  AuthorizeOIDCFinish_(
      DBContext *context, Ztls::Random *rng, Bytes ceremonyID,
      Bytes bindingDigest, User user, IDVec roleIDs, int64_t authTime,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_user{ZuMv(user)},
    m_roleIDs{ZuMv(roleIDs)}, m_authTime{authTime},
    m_config{ZuMv(config)}, m_policy{ZuMv(policy)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_ceremonyID || !m_bindingDigest ||
	!m_user.id || m_authTime <= 0 || m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeOIDCFinish_>{this}, grants]() {
      Bytes id = self->m_ceremonyID;
      grants->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRowRef<Grant> row) mutable { self->grant_(ZuMv(row)); });
    });
  }

private:
  void finish_(int error, String location)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(location));
  }

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row || row->data().kind != GrantKind::Ceremony ||
	row->data().purpose != GrantPurpose::Authorization ||
	row->data().state != State::Active || row->data().owner ||
	row->data().expires <= m_config.now ||
	!Ztls::ctEqual(row->data().bindingDigest, m_bindingDigest)) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    Grant grant = row->data();
    loadUserAuth(m_context, ZuMv(grant), ZuMv(m_user), {}, ZuMv(m_roleIDs),
      [self = ZmRef<AuthorizeOIDCFinish_>{this}](
	  int error, AuthorityData data) mutable {
	self->authority_(error, ZuMv(data));
      });
  }

  void authority_(int error, AuthorityData data)
  {
    if (error) {
      finish_(error == AuthorityError::Invalid ?
	OAuthError::AccessDenied : OAuthError::InvalidScope, {});
      return;
    }
    m_authority = ZuMv(data);
    auto policy = ZuMv(m_policy);
    policy(m_authority.user, m_authority.client, m_authority.selection,
      m_authority.actions, [self = ZmRef<AuthorizeOIDCFinish_>{this}](
	  bool ok, ZtBitmap actions) mutable {
	self->policy_(ok, ZuMv(actions));
      });
  }

  void policy_(bool ok, ZtBitmap actions)
  {
    if (m_done || m_policyDone) return;
    m_policyDone = true;
    if (!ok) { finish_(OAuthError::AccessDenied, {}); return; }
    actions &= m_authority.actions;
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, {}, m_authority.principalRoleIDs, ZuMv(actions),
      m_authority.issuer.authVersion, m_authority.user.authVersion,
      m_authTime, m_config.codeExpires,
      [self = ZmRef<AuthorizeOIDCFinish_>{this}](
	  bool ok, String code) mutable { self->finished_(ok, ZuMv(code)); });
  }

  void finished_(bool ok, String code)
  {
    if (!ok) {
      if (code && code.mutable_()) ZuClear(code.data(), code.length());
      finish_(OAuthError::ServerError, {});
      return;
    }
    String location = codeRedirect(m_authority.grant, code);
    if (code && code.mutable_()) ZuClear(code.data(), code.length());
    finish_(AuthorizeIssue::OK, ZuMv(location));
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  User		m_user;
  IDVec		m_roleIDs;
  int64_t	m_authTime = 0;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  AuthorityData m_authority;
  bool		m_policyDone = false;
  bool		m_done = false;
};

static void authorizeRequest_(
    DBContext *context, Ztls::Random &rng, String query,
    Bytes bindingDigest, AuthorizeConfig config, AuthorizeFn complete)
{
  ZmRef<AuthorizeRequest_> request = new AuthorizeRequest_{context, &rng,
    ZuMv(query), ZuMv(bindingDigest), ZuMv(config), ZuMv(complete)};
  request->start();
}

static void authorizeFinish_(
    DBContext *context, Ztls::Random &rng, Bytes ceremonyID,
    Bytes bindingDigest, AssertionInput input,
    AuthorizeFinishConfig config, PolicyFn policy,
    AuthorizeCodeFn complete)
{
  ZmRef<AuthorizeFinish_> request = new AuthorizeFinish_{context, &rng,
    ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(input), ZuMv(config),
    ZuMv(policy), ZuMv(complete)};
  request->start();
}

bool authorizeRequest(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, String query, Bytes bindingDigest,
    AuthorizeConfig config, AuthorizeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeComplete_> state =
    new AuthorizeComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, &rng, query = ZuMv(query),
    bindingDigest = ZuMv(bindingDigest), config = ZuMv(config)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    authorizeRequest_(context, rng, ZuMv(query), ZuMv(bindingDigest),
      ZuMv(config), [state](int error, AuthorizeResult result) mutable {
	state->complete(error, ZuMv(result));
      });
  }, [state]() mutable { state->cancel(); });
}

bool authorizeFinish(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes ceremonyID, Bytes bindingDigest,
    AssertionInput input, AuthorizeFinishConfig config, PolicyFn policy,
    AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [
    state, context, &rng, ceremonyID = ZuMv(ceremonyID),
    bindingDigest = ZuMv(bindingDigest), input = ZuMv(input),
    config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    authorizeFinish_(context, rng, ZuMv(ceremonyID), ZuMv(bindingDigest),
      ZuMv(input), ZuMv(config), ZuMv(policy), [state](
	  int error, String location) mutable {
	state->complete(error, ZuMv(location));
      });
  }, [state]() mutable { state->cancel(); });
}

bool authorizeOIDCFinish(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes ceremonyID, Bytes bindingDigest, User user,
    IDVec roleIDs, int64_t authTime, AuthorizeFinishConfig config,
    PolicyFn policy, AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
    ceremonyID = ZuMv(ceremonyID), bindingDigest = ZuMv(bindingDigest),
    user = ZuMv(user), roleIDs = ZuMv(roleIDs), authTime,
    config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<AuthorizeOIDCFinish_> finish = new AuthorizeOIDCFinish_{
      context, &rng, ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(user),
      ZuMv(roleIDs), authTime, ZuMv(config), ZuMv(policy),
      [state](int error, String location) mutable {
	state->complete(error, ZuMv(location));
      }};
    finish->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
