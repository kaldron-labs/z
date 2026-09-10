//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumAuthorize.hh>

#include <zlib/ZumAdmin.hh>


#include <zlib/ZtlsRandom.hh>

namespace Zum {

static bool hasID_(const IDVec &ids, uint64_t id)
{
  for (auto value: ids) if (value == id) return true;
  return false;
}

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

ZuDerive(ConsentGateFn,
  (ZmFn<void(int), ZmFnHeapID<"Zum.ConsentGateFn">>));

class ConsentGate_ : public ZumPolymorph {
public:
  ConsentGate_(DBContext *context, Grant grant, Bytes bindingDigest,
      UserID userID, Bytes credentialID, IDVec roleIDs, ZtBitmap actions,
      uint64_t authVersion, uint64_t userVersion, int64_t authTime,
      Evidence evidence, ConsentGateFn complete) :
    m_context{context}, m_grant{ZuMv(grant)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_userID{userID},
    m_credentialID{ZuMv(credentialID)}, m_roleIDs{ZuMv(roleIDs)},
    m_actions{ZuMv(actions)}, m_authVersion{authVersion},
    m_userVersion{userVersion}, m_authTime{authTime},
    m_evidence{ZuMv(evidence)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_grant.id || !m_grant.appID || !m_userID ||
        !m_userVersion || !m_authTime || !m_complete) {
      finish_(OAuthError::ServerError);
      return;
    }
    auto policies = m_context->authPolicies;
    policies->run(0, [self = ZmRef<ConsentGate_>{this}, policies]() {
      policies->find<0>(0, ZuFwdTuple(self->m_grant.appID), [
          self = ZuMv(self)](ZdbRowRef<AuthPolicy> row) mutable {
        if (!row || row->data().state != State::Active || row->data().owner) {
          self->finish_(OAuthError::ServerError);
          return;
        }
        switch (row->data().consentPolicy) {
          case ConsentPolicy::Preauthorized:
            self->finish_(AuthorizeIssue::OK);
            return;
          case ConsentPolicy::Explicit:
            self->consent_();
            return;
          default:
            self->finish_(OAuthError::ServerError);
            return;
        }
      });
    });
  }

private:
  void finish_(int result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(result);
  }

  void consent_()
  {
    auto consents = m_context->consents;
    consents->run(0, [self = ZmRef<ConsentGate_>{this}, consents]() {
      consents->find<0>(0, ZuFwdTuple(self->m_userID,
          self->m_grant.clientID, self->m_grant.appID,
          self->m_grant.audienceID), [self = ZuMv(self)](
          ZdbRowRef<Consent> row) mutable {
        bool allowed = row && row->data().state == State::Active &&
          !row->data().owner;
        if (allowed)
          for (auto scopeID: self->m_grant.scopeIDs)
            if (!hasID_(row->data().scopeIDs, scopeID)) {
              allowed = false;
              break;
            }
        if (allowed) {
          self->finish_(AuthorizeIssue::OK);
          return;
        }
        if (self->m_grant.promptPresent && self->m_grant.prompt == "none") {
          self->finish_(OAuthError::ConsentRequired);
          return;
        }
        self->stage_();
      });
    });
  }

  void stage_()
  {
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<ConsentGate_>{this}, grants]() {
      Bytes id = self->m_grant.id;
      grants->findUpd<0, ZuSeq<1, 2>>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
          ZdbRow<Grant> *row) mutable {
        if (!row || row->data().kind != GrantKind::Ceremony ||
            row->data().purpose != GrantPurpose::Authorization ||
            row->data().state != State::Active || row->data().owner ||
            !Ztls::ctEqual(row->data().bindingDigest,
              self->m_bindingDigest)) {
          self->finish_(OAuthError::ServerError);
          return;
        }
        auto &grant = row->data();
        grant.userID = self->m_userID;
        grant.credentialID = ZuMv(self->m_credentialID);
        grant.roleIDs = ZuMv(self->m_roleIDs);
        grant.actions = ZuMv(self->m_actions);
        grant.authVersion = self->m_authVersion;
        grant.userVersion = self->m_userVersion;
        grant.authTime = self->m_authTime;
        if (self->m_evidence.appID) {
          grant.authoritySource = UserSource::External;
          grant.authorityProviderID = self->m_evidence.providerID;
          grant.policyVersion = self->m_evidence.policyVersion;
          grant.evidenceVersion = self->m_evidence.version;
          if (grant.expires > self->m_evidence.deadline)
            grant.expires = self->m_evidence.deadline;
        }
        grant.state = State::Pending;
        if (!row->commit()) {
          self->finish_(OAuthError::ServerError);
          return;
        }
        self->finish_(AuthorizeIssue::Consent);
      });
    });
  }

  DBContext	*m_context = nullptr;
  Grant		m_grant;
  Bytes		m_bindingDigest;
  UserID	m_userID = 0;
  Bytes		m_credentialID;
  IDVec		m_roleIDs;
  ZtBitmap	m_actions;
  uint64_t	m_authVersion = 0;
  uint64_t	m_userVersion = 0;
  int64_t	m_authTime = 0;
  Evidence	m_evidence;
  ConsentGateFn m_complete;
  bool		m_done = false;
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
    if (!row || !authorizeClient(row->data(), m_params) ||
	(m_config.facadeClientID &&
	 (!m_config.facadeAppID || row->data().appID != m_config.facadeAppID))) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    m_client = row->data();
    m_result.appID = m_client.appID;
    if (m_params.has(AuthorizeParams::LoginHint))
      m_result.loginHint = m_params.loginHint;
    m_result.redirectURI = m_params.redirectURI;
    m_result.state = m_params.state;
    m_result.statePresent = m_params.has(AuthorizeParams::State);
    m_result.prompt = m_params.prompt;
    m_result.promptPresent = m_params.has(AuthorizeParams::Prompt);
    if (m_params.has(AuthorizeParams::MaxAge)) {
      for (auto c: m_params.maxAge)
	m_result.maxAge = m_result.maxAge * 10 + unsigned(c - '0');
      m_result.maxAgePresent = true;
    }
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
    clientScopes(m_context, ZuMv(m_client),
      [self = ZmRef<AuthorizeRequest_>{this}](
	  int error, App app, Client client, ScopeVec scopes) mutable {
	if (error) { self->finish_(OAuthError::AccessDenied); return; }
	self->m_authVersion = app.authVersion;
	self->m_client = ZuMv(client);
	self->m_scopes = ZuMv(scopes);
	self->scopes_();
      });
  }

  void scopes_()
  {
    ScopeSelection selection;
    if (selectScopes(m_client, m_params.scope, m_scopes, selection)) {
      finish_(OAuthError::InvalidScope);
      return;
    }
    if (m_params.has(AuthorizeParams::Resource) &&
	selection.audience != m_params.resource) {
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
    grant.facadeClientID = m_config.facadeClientID;
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
    if (grant.appID) {
      auto memberships = m_context->memberships;
      memberships->run(0, [self = ZmRef<AuthorizeFinish_>{this},
          memberships, user = ZuMv(user), cred = ZuMv(cred),
          grant = ZuMv(grant)]() mutable {
        auto key = ZuFwdTuple(grant.appID, user.id);
        memberships->find<0>(0, ZuMv(key), [self = ZuMv(self),
            user = ZuMv(user), cred = ZuMv(cred),
            grant = ZuMv(grant)](ZdbRowRef<Membership> row) mutable {
          if (!row || row->data().state != State::Active ||
              row->data().owner) {
            self->finish_(OAuthError::AccessDenied, {});
            return;
          }
          self->authorityLoad_(ZuMv(user), ZuMv(cred), ZuMv(grant),
            IDVec{row->data().roleIDs});
        });
      });
      return;
    }
    authorityLoad_(ZuMv(user), ZuMv(cred), ZuMv(grant),
      IDVec{user.roleIDs});
  }

  void authorityLoad_(User user, Cred cred, Grant grant, IDVec roleIDs)
  {
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
    actions = intersectActions(ZuMv(actions), m_authority.actions);
    if (m_config.consent) {
      ZmRef<ConsentGate_> gate = new ConsentGate_{m_context,
        Grant{m_authority.grant}, Bytes{m_bindingDigest},
        m_authority.user.id, Bytes{m_authority.cred.id},
        IDVec{m_authority.principalRoleIDs}, ZtBitmap{actions},
        m_authority.app.authVersion, m_authority.user.authVersion,
        m_config.now, Evidence{}, [self = ZmRef<AuthorizeFinish_>{this},
          actions = ZuMv(actions)](int result) mutable {
          if (result == AuthorizeIssue::OK) self->issue_(ZuMv(actions));
          else self->finish_(result, {});
        }};
      gate->start();
      return;
    }
    issue_(ZuMv(actions));
  }

  void issue_(ZtBitmap actions)
  {
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, m_authority.cred.id,
      m_authority.principalRoleIDs, ZuMv(actions),
      m_authority.app.authVersion, m_authority.user.authVersion,
      m_config.now, m_config.codeExpires, Evidence{}, [
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

class AuthorizeSessionFinish_ : public ZumPolymorph {
public:
  AuthorizeSessionFinish_(DBContext *context, Ztls::Random *rng,
      Bytes ceremonyID, Bytes bindingDigest, Session session,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_session{ZuMv(session)},
    m_config{ZuMv(config)},
    m_policy{ZuMv(policy)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_ceremonyID || !m_bindingDigest ||
	m_session.state != State::Active || m_session.owner ||
	!m_session.userID || !m_session.authVersion ||
	m_session.authTime <= 0 || m_session.authTime > m_config.now ||
	m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, grants]() {
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
    Grant grant{row->data()};
    auto users = m_context->users;
    users->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, users,
	grant = ZuMv(grant)]() mutable {
      users->find<0>(0, ZuFwdTuple(self->m_session.userID), [self = ZuMv(self),
	  grant = ZuMv(grant)](ZdbRowRef<User> row) mutable {
	self->user_(ZuMv(row), ZuMv(grant));
      });
    });
  }

  void user_(ZdbRowRef<User> row, Grant grant)
  {
    if (!row || row->data().state != State::Active || row->data().owner ||
	row->data().authVersion != m_session.authVersion) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    m_user = row->data();
    if (m_session.providerID) {
      if (m_user.source != UserSource::External || !grant.appID) {
	finish_(OAuthError::AccessDenied, {});
	return;
      }
      delegatedPolicy_(ZuMv(grant));
      return;
    }
    if (m_user.source != UserSource::Local) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    if (!grant.appID) {
      authorityLoad_(ZuMv(grant), IDVec{m_user.roleIDs});
      return;
    }
    auto memberships = m_context->memberships;
    memberships->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this},
	memberships, grant = ZuMv(grant)]() mutable {
      memberships->find<0>(0,
	ZuFwdTuple(grant.appID, self->m_user.id), [self = ZuMv(self),
	  grant = ZuMv(grant)](ZdbRowRef<Membership> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(OAuthError::AccessDenied, {});
	  return;
	}
	self->authorityLoad_(ZuMv(grant), IDVec{row->data().roleIDs});
      });
    });
  }

  void delegatedPolicy_(Grant grant)
  {
    auto policies = m_context->authPolicies;
    policies->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, policies,
	grant = ZuMv(grant)]() mutable {
      policies->find<0>(0, ZuFwdTuple(grant.appID), [self = ZuMv(self),
	  grant = ZuMv(grant)](ZdbRowRef<AuthPolicy> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner ||
	    row->data().providerID != self->m_session.providerID ||
	    !row->data().assignmentMaxAge) {
	  self->finish_(OAuthError::AccessDenied, {});
	  return;
	}
	self->m_authPolicy = row->data();
	self->delegatedProvider_(ZuMv(grant));
      });
    });
  }

  void delegatedProvider_(Grant grant)
  {
    auto providers = m_context->providers;
    providers->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, providers,
	grant = ZuMv(grant)]() mutable {
      providers->find<0>(0, ZuFwdTuple(self->m_session.providerID), [
	  self = ZuMv(self), grant = ZuMv(grant)](
	    ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner ||
	    !row->data().issuer) {
	  self->finish_(OAuthError::AccessDenied, {});
	  return;
	}
	self->m_provider = row->data();
	self->delegatedEvidence_(ZuMv(grant));
      });
    });
  }

  void delegatedEvidence_(Grant grant)
  {
    auto evidence = m_context->evidence;
    evidence->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, evidence,
	grant = ZuMv(grant)]() mutable {
      evidence->find<0>(0, ZuFwdTuple(grant.appID, self->m_user.id,
	  self->m_session.providerID), [self = ZuMv(self), grant = ZuMv(grant)](
	    ZdbRowRef<Evidence> row) mutable {
	if (!row || row->data().owner || !row->data().eligible ||
	    row->data().deadline <= self->m_config.now ||
	    row->data().policyVersion != self->m_authPolicy.version ||
	    row->data().source != self->m_provider.claimSource) {
	  self->finish_(OAuthError::AccessDenied, {});
	  return;
	}
	self->m_evidence = row->data();
	self->delegatedRoles_(ZuMv(grant));
      });
    });
  }

  void delegatedRoles_(Grant grant)
  {
    using Table = RoleMapTable;
    using Tuple = Table::Tuple;
    auto maps = m_context->roleMaps;
    maps->run(0, [self = ZmRef<AuthorizeSessionFinish_>{this}, maps,
	grant = ZuMv(grant)]() mutable {
      maps->selectRows<0>(ZuFwdTuple(grant.appID,
	  self->m_session.providerID), 129, [self, grant = ZuMv(grant)](
	    ZuUnion<void, Tuple> result, unsigned count) mutable {
	if (result.template is<Tuple>()) {
	  if (count > 128) { self->m_mappingOverflow = true; return; }
	  auto tuple = ZuMv(result).template p<Tuple>();
	  ZuTupleCall(ZuMv(tuple), [self](auto &&...args) mutable {
	    RoleMap map{ZuFwd<decltype(args)>(args)...};
	    if (map.state != State::Active || map.owner || !map.roleID) return;
	    for (auto &value: self->m_evidence.roleValues)
	      if (value == map.value) {
		if (!hasID_(self->m_delegatedRoles, map.roleID))
		  self->m_delegatedRoles.push(map.roleID);
		break;
	      }
	  });
	  return;
	}
	if (self->m_mappingOverflow || !self->m_delegatedRoles) {
	  self->finish_(OAuthError::AccessDenied, {});
	  return;
	}
	self->authorityLoad_(ZuMv(grant), ZuMv(self->m_delegatedRoles));
      });
    });
  }

  void authorityLoad_(Grant grant, IDVec roleIDs)
  {
    loadUserAuth(m_context, ZuMv(grant), ZuMv(m_user), {}, ZuMv(roleIDs),
      [self = ZmRef<AuthorizeSessionFinish_>{this}](
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
      m_authority.actions, [self = ZmRef<AuthorizeSessionFinish_>{this}](
	  bool ok, ZtBitmap actions) mutable {
	self->policy_(ok, ZuMv(actions));
      });
  }

  void policy_(bool ok, ZtBitmap actions)
  {
    if (m_done || m_policyDone) return;
    m_policyDone = true;
    if (!ok) { finish_(OAuthError::AccessDenied, {}); return; }
    actions = intersectActions(ZuMv(actions), m_authority.actions);
    if (m_config.consent) {
      ZmRef<ConsentGate_> gate = new ConsentGate_{m_context,
        Grant{m_authority.grant}, Bytes{m_bindingDigest},
        m_authority.user.id, {}, IDVec{m_authority.principalRoleIDs},
        ZtBitmap{actions}, m_authority.app.authVersion,
        m_authority.user.authVersion, m_session.authTime,
        Evidence{m_evidence}, [self = ZmRef<AuthorizeSessionFinish_>{this},
          actions = ZuMv(actions)](int result) mutable {
          if (result == AuthorizeIssue::OK) self->issue_(ZuMv(actions));
          else self->finish_(result, {});
        }};
      gate->start();
      return;
    }
    issue_(ZuMv(actions));
  }

  void issue_(ZtBitmap actions)
  {
    int64_t codeExpires = m_config.codeExpires;
    if (m_evidence.appID && m_evidence.deadline < codeExpires)
      codeExpires = m_evidence.deadline;
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, {}, m_authority.principalRoleIDs, ZuMv(actions),
      m_authority.app.authVersion, m_authority.user.authVersion,
      m_session.authTime,
      codeExpires, ZuMv(m_evidence),
      [self = ZmRef<AuthorizeSessionFinish_>{this}](
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
  Session	m_session;
  User		m_user;
  AuthPolicy	m_authPolicy;
  Provider	m_provider;
  Evidence	m_evidence;
  IDVec		m_delegatedRoles;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  AuthorityData	m_authority;
  bool		m_mappingOverflow = false;
  bool		m_policyDone = false;
  bool		m_done = false;
};

class AuthorizeOIDCFinish_ : public ZumPolymorph {
public:
  AuthorizeOIDCFinish_(
      DBContext *context, Ztls::Random *rng, Bytes ceremonyID,
      Bytes bindingDigest, User user, IDVec roleIDs, Evidence evidence,
      int64_t authTime,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_user{ZuMv(user)},
    m_roleIDs{ZuMv(roleIDs)}, m_evidence{ZuMv(evidence)},
    m_authTime{authTime},
    m_config{ZuMv(config)}, m_policy{ZuMv(policy)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_ceremonyID || !m_bindingDigest ||
	!m_user.id || !m_evidence.appID || !m_evidence.providerID ||
	!m_evidence.eligible || m_evidence.userID != m_user.id ||
	m_evidence.deadline <= m_config.now || m_authTime <= 0 ||
	m_config.now <= 0 ||
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
    if (grant.appID != m_evidence.appID) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
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
    actions = intersectActions(ZuMv(actions), m_authority.actions);
    if (m_config.consent) {
      ZmRef<ConsentGate_> gate = new ConsentGate_{m_context,
        Grant{m_authority.grant}, Bytes{m_bindingDigest},
        m_authority.user.id, {}, IDVec{m_authority.principalRoleIDs},
        ZtBitmap{actions}, m_authority.app.authVersion,
        m_authority.user.authVersion, m_authTime, Evidence{m_evidence},
        [self = ZmRef<AuthorizeOIDCFinish_>{this},
          actions = ZuMv(actions)](int result) mutable {
          if (result == AuthorizeIssue::OK) self->issue_(ZuMv(actions));
          else self->finish_(result, {});
        }};
      gate->start();
      return;
    }
    issue_(ZuMv(actions));
  }

  void issue_(ZtBitmap actions)
  {
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, {}, m_authority.principalRoleIDs, ZuMv(actions),
      m_authority.app.authVersion, m_authority.user.authVersion,
      m_authTime, m_config.codeExpires < m_evidence.deadline ?
        m_config.codeExpires : m_evidence.deadline,
      ZuMv(m_evidence),
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
  Evidence	m_evidence;
  int64_t	m_authTime = 0;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  AuthorityData m_authority;
  bool		m_policyDone = false;
  bool		m_done = false;
};

class AuthorizeConsentFinish_ : public ZumPolymorph {
public:
  AuthorizeConsentFinish_(DBContext *context, Ztls::Random *rng,
      Bytes ceremonyID, Bytes bindingDigest, bool approve,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_approve{approve},
    m_config{ZuMv(config)}, m_policy{ZuMv(policy)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_ceremonyID || !m_bindingDigest ||
        m_config.now <= 0 || m_config.codeExpires <= m_config.now ||
        !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeConsentFinish_>{this}, grants]() {
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

  bool valid_(const Grant &grant) const
  {
    return grant.kind == GrantKind::Ceremony &&
      grant.purpose == GrantPurpose::Authorization &&
      grant.state == State::Pending && !grant.owner &&
      grant.expires > m_config.now && grant.userID && grant.userVersion &&
      grant.authTime > 0 &&
      Ztls::ctEqual(grant.bindingDigest, m_bindingDigest);
  }

  void grant_(ZdbRowRef<Grant> row)
  {
    if (!row || !valid_(row->data())) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    m_grant = row->data();
    if (!m_approve) { deny_(); return; }
    auto clients = m_context->clients;
    String id = m_grant.clientID;
    clients->run(0, [self = ZmRef<AuthorizeConsentFinish_>{this}, clients,
        id = ZuMv(id)]() mutable {
      clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
          ZdbRowRef<Client> row) mutable {
        if (!row || row->data().state != State::Active || row->data().owner ||
            row->data().appID != self->m_grant.appID) {
          self->finish_(OAuthError::AccessDenied, {});
          return;
        }
        loadGrantAuth(self->m_context, Grant{self->m_grant},
          Client{row->data()}, false, {}, self->m_config.now,
          [self = ZuMv(self)](int error, AuthorityData data) mutable {
            self->authority_(error, ZuMv(data));
          });
      });
    });
  }

  void deny_()
  {
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeConsentFinish_>{this}, grants]() {
      Bytes id = self->m_ceremonyID;
      grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
          ZdbRow<Grant> *row) mutable {
        if (!row || !self->valid_(row->data())) {
          self->finish_(OAuthError::AccessDenied, {});
          return;
        }
        Grant grant = row->data();
        row->data().state = State::Revoked;
        if (!row->commit()) {
          self->finish_(OAuthError::ServerError, {});
          return;
        }
        self->finish_(OAuthError::AccessDenied,
          errorRedirect(grant.redirectURI, OAuthError::AccessDenied,
            grant.oauthState, grant.oauthStatePresent));
      });
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
    auto policy = m_policy;
    policy(m_authority.user, m_authority.client, m_authority.selection,
      m_authority.actions, [self = ZmRef<AuthorizeConsentFinish_>{this}](
          bool ok, ZtBitmap actions) mutable {
        if (!ok) { self->finish_(OAuthError::AccessDenied, {}); return; }
        self->m_actions = ZuMv(actions);
        self->m_actions = intersectActions(
          ZuMv(self->m_actions), self->m_authority.actions);
        self->record_();
      });
  }

  void record_()
  {
    auto consents = m_context->consents;
    auto key = ZuFwdTuple(m_grant.userID, m_grant.clientID,
      m_grant.appID, m_grant.audienceID);
    consents->run(0, [self = ZmRef<AuthorizeConsentFinish_>{this}, consents,
        key = ZuMv(key)]() mutable {
      consents->findUpd<0>(0, ZuMv(key), [self, consents](
          ZdbRow<Consent> *row) mutable {
        if (row) {
          if (row->data().owner) {
            self->finish_(OAuthError::ServerError, {});
            return;
          }
          for (auto id: self->m_grant.scopeIDs)
            if (!hasID_(row->data().scopeIDs, id))
              row->data().scopeIDs.push(id);
          row->data().state = State::Active;
          row->data().updated = self->m_config.now;
          ++row->data().version;
          if (!row->commit()) {
            self->finish_(OAuthError::ServerError, {});
            return;
          }
          self->issue_();
          return;
        }
        ZdbRowRef<Consent> next = new ZdbRow<Consent>{consents, ZdbShard{0}};
        consents->insert(ZuMv(next), [self = ZuMv(self)](
            ZdbRow<Consent> *row) mutable {
          if (!row) {
            self->finish_(OAuthError::ServerError, {});
            return;
          }
          new (row->ptr()) Consent{.userID = self->m_grant.userID,
            .clientID = self->m_grant.clientID,
            .appID = self->m_grant.appID,
            .audienceID = self->m_grant.audienceID,
            .scopeIDs = IDVec{self->m_grant.scopeIDs},
            .state = State::Active, .version = 1,
            .created = self->m_config.now, .updated = self->m_config.now};
          if (!row->commit()) {
            self->finish_(OAuthError::ServerError, {});
            return;
          }
          self->issue_();
        });
      });
    });
  }

  void issue_()
  {
    int64_t expires = m_config.codeExpires;
    if (m_authority.authorityDeadline > 0 &&
        m_authority.authorityDeadline < expires)
      expires = m_authority.authorityDeadline;
    authorizationFinish(m_context, *m_rng, m_ceremonyID, m_bindingDigest,
      m_authority.user.id, m_authority.cred.id,
      m_authority.principalRoleIDs, ZuMv(m_actions),
      m_authority.app.authVersion, m_authority.user.authVersion,
      m_grant.authTime, expires, Evidence{}, [
        self = ZmRef<AuthorizeConsentFinish_>{this}](
          bool ok, String code) mutable {
        if (!ok) {
          if (code && code.mutable_()) ZuClear(code.data(), code.length());
          self->finish_(OAuthError::ServerError, {});
          return;
        }
        String location = codeRedirect(self->m_grant, code);
        if (code && code.mutable_()) ZuClear(code.data(), code.length());
        self->finish_(AuthorizeIssue::OK, ZuMv(location));
      });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  bool		m_approve = false;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  Grant		m_grant;
  AuthorityData m_authority;
  ZtBitmap	m_actions;
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
    IDVec roleIDs, Evidence evidence, int64_t authTime,
    AuthorizeFinishConfig config,
    PolicyFn policy, AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
    ceremonyID = ZuMv(ceremonyID), bindingDigest = ZuMv(bindingDigest),
    user = ZuMv(user), roleIDs = ZuMv(roleIDs), evidence = ZuMv(evidence),
    authTime,
    config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<AuthorizeOIDCFinish_> finish = new AuthorizeOIDCFinish_{
      context, &rng, ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(user),
      ZuMv(roleIDs), ZuMv(evidence), authTime, ZuMv(config), ZuMv(policy),
      [state](int error, String location) mutable {
	state->complete(error, ZuMv(location));
      }};
    finish->start();
  }, [state]() mutable { state->cancel(); });
}

bool authorizeSessionFinish(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes ceremonyID, Bytes bindingDigest, Session session,
    AuthorizeFinishConfig config,
    PolicyFn policy, AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
    ceremonyID = ZuMv(ceremonyID), bindingDigest = ZuMv(bindingDigest),
    session = ZuMv(session),
    config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<AuthorizeSessionFinish_> finish = new AuthorizeSessionFinish_{
      context, &rng, ZuMv(ceremonyID), ZuMv(bindingDigest), ZuMv(session),
      ZuMv(config), ZuMv(policy),
      [state](int error, String location) mutable {
	state->complete(error, ZuMv(location));
      }};
    finish->start();
  }, [state]() mutable { state->cancel(); });
}

bool authorizeConsentFinish(
    Requests *requests, ZuTime deadline, DBContext *context,
    Ztls::Random &rng, Bytes ceremonyID, Bytes bindingDigest, bool approve,
    AuthorizeFinishConfig config, PolicyFn policy,
    AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, &rng,
    ceremonyID = ZuMv(ceremonyID), bindingDigest = ZuMv(bindingDigest),
    approve, config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<AuthorizeConsentFinish_> finish = new AuthorizeConsentFinish_{
      context, &rng, ZuMv(ceremonyID), ZuMv(bindingDigest), approve,
      ZuMv(config), ZuMv(policy), [state](int error,
          String location) mutable {
        state->complete(error, ZuMv(location));
      }};
    finish->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
