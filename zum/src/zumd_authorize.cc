//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_authorize.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_jwt.hh>
#include <zlib/zumd_provider_db.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/ZuBox.hh>


#include <zlib/ZtlsRandom.hh>

namespace Zum {

static bool hasID_(const IDVec &ids, uint64_t id)
{
  for (auto value: ids) if (value == id) return true;
  return false;
}

static bool authorizationTarget_(
    const AuthorizeFinishConfig &config, const Grant &grant)
{
  return grant.issuer == config.issuer && grant.appID == config.appID;
}

template <typename Heap = ZuVoid>
class AuthorizeComplete__ : public Heap, public ZmObject  {
public:
  AuthorizeComplete__(AuthorizeFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, AuthorizeResult result)
  {
    m_request->complete([
      self = ZmRef<AuthorizeComplete__>{this}, error,
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
ZuDerive(AuthorizeCompleteHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeComplete", AuthorizeComplete__<>>));
ZuDerive(AuthorizeComplete_, (AuthorizeComplete__<AuthorizeCompleteHeap>));

template <typename Heap = ZuVoid>
class AuthorizeCodeResult__ : public Heap, public ZmObject  {
public:
  AuthorizeCodeResult__(int error_, String location_) :
    error{error_}, location{ZuMv(location_)} { }

  ~AuthorizeCodeResult__()
  {
    if (location && location.mutable_())
      ZuClear(location);
  }

  int		error;
  String	location;
};
ZuDerive(AuthorizeCodeResultHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeCodeResult", AuthorizeCodeResult__<>>));
ZuDerive(AuthorizeCodeResult_,
  (AuthorizeCodeResult__<AuthorizeCodeResultHeap>));

template <typename Heap = ZuVoid>
class AuthorizeCodeComplete__ : public Heap, public ZmObject  {
public:
  AuthorizeCodeComplete__(AuthorizeCodeFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(int error, String location)
  {
    ZmRef<AuthorizeCodeResult_> delivery =
      new AuthorizeCodeResult_{error, ZuMv(location)};
    m_request->complete([
      self = ZmRef<AuthorizeCodeComplete__>{this}, delivery = ZuMv(delivery)
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
ZuDerive(AuthorizeCodeCompleteHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeCodeComplete",
    AuthorizeCodeComplete__<>>));
ZuDerive(AuthorizeCodeComplete_,
  (AuthorizeCodeComplete__<AuthorizeCodeCompleteHeap>));

ZuDerive(ConsentGateFn,
  (ZmFn<void(int), ZmFnHeapID<"Zum.ConsentGateFn">>));

template <typename Heap = ZuVoid>
class ConsentGate__ : public Heap, public ZmPolymorph  {
public:
  ConsentGate__(DBContext *context, Grant grant, Bytes bindingDigest,
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
    policies->run(0, [self = ZmRef<ConsentGate__>{this}, policies]() {
      policies->find<0>(0, ZuFwdTuple(self->m_grant.appID), [
          self = ZuMv(self)](ZdbRowRef<AuthPolicy> row) mutable {
        if (!row || row->data().state != State::Active || row->data().owner) {
          self->finish_(OAuthError::ServerError);
          return;
        }
        switch (row->data().consentPolicy) {
          case ConsentPolicy::Preauthorized:
            if (!self->needsConsent()) self->finish_(AuthorizeIssue::OK);
            else self->consent_();
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
  bool needsConsent() const
  {
    // Offline access always takes the interactive consent path.  This keeps
    // ordinary consent from silently authorizing a refresh-token family.
    return scopeContains(m_grant.scope, "offline_access") ||
      (m_grant.promptPresent && m_grant.prompt == "consent");
  }

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
    consents->run(0, [self = ZmRef<ConsentGate__>{this}, consents]() {
      consents->find<0>(0, ZuFwdTuple(self->m_userID,
          self->m_grant.clientID, self->m_grant.appID), [self = ZuMv(self)](
          ZdbRowRef<Consent> row) mutable {
        bool allowed = row && row->data().state == State::Active &&
          !row->data().owner;
        if (allowed)
          for (auto roleID: self->m_grant.requestedRoleIDs)
            if (!hasID_(row->data().roleIDs, roleID)) {
              allowed = false;
              break;
            }
        if (allowed && !self->needsConsent()) {
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
    grants->run(0, [self = ZmRef<ConsentGate__>{this}, grants]() {
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
ZuDerive(ConsentGateHeap,
  (ZmHeap<"Zum.zumd.authorize.ConsentGate", ConsentGate__<>>));
ZuDerive(ConsentGate_, (ConsentGate__<ConsentGateHeap>));

template <typename Heap = ZuVoid>
class AuthorizeRequest__ : public Heap, public ZmPolymorph  {
public:
  AuthorizeRequest__(
      DBContext *context, Ztls::Random *rng, String query,
      Bytes bindingDigest, AuthorizeConfig config, AuthorizeFn complete) :
    m_context{context}, m_rng{rng}, m_query{ZuMv(query)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_config{ZuMv(config)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_bindingDigest || !m_config.issuer ||
	!m_config.appID ||
	(m_config.passkey && (!m_config.rpID || !m_config.timeout)) ||
	m_config.now <= 0 || m_config.expires <= m_config.now) {
      finish_(OAuthError::ServerError);
      return;
    }
    if (!m_query.mutable_()) m_query.length(m_query.length());
    bool parsed = parseAuthorize({m_query.data(), m_query.length()}, m_params);
    m_profileError = parsed ? validateAuthorize(m_params) :
      ProfileError::Unsupported;
    if (!m_params.has(AuthorizeParams::ClientID) ||
	!m_params.has(AuthorizeParams::RedirectURI) ||
	!m_params.clientID || !m_params.redirectURI) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    String id{m_params.clientID};
    m_context->clients->run(0, [
      self = ZmRef<AuthorizeRequest__>{this}, id = ZuMv(id)
    ]() mutable {
      self->m_context->clients->template find<0>(0, ZuFwdTuple(ZuMv(id)), [
	self = ZuMv(self)
      ](ZdbRowRef<Client> row) mutable { self->client_(ZuMv(row)); });
    });
  }

private:
  void clear_()
  {
    if (m_query && m_query.mutable_())
      ZuClear(m_query);
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
    if (!row || row->data().appID != m_config.appID ||
	!authorizeClient(row->data(), m_params)) {
      finish_(OAuthError::InvalidRequest);
      return;
    }
    m_client = row->data();
    m_result.appID = m_client.appID;
    m_result.issuer = m_config.issuer;
    if (m_params.has(AuthorizeParams::LoginHint))
      m_result.loginHint = m_params.loginHint;
    m_result.redirectURI = m_params.redirectURI;
    m_result.state = m_params.state;
    m_result.statePresent = m_params.has(AuthorizeParams::State);
    m_result.prompt = m_params.prompt;
    m_result.promptPresent = m_params.has(AuthorizeParams::Prompt);
    m_result.redirect = true;
    if (m_profileError) {
      finish_(m_profileError == ProfileError::Unsupported ?
	OAuthError::UnsupportedResponseType : OAuthError::InvalidRequest);
      return;
    }

    if (m_params.has(AuthorizeParams::MaxAge)) {
      m_result.maxAge = ZuBox<uint64_t>{m_params.maxAge};
      m_result.maxAgePresent = true;
    }

    clientScopes(m_context, ZuMv(m_client),
      [self = ZmRef<AuthorizeRequest__>{this}](
	  int error, App app, Client client, ClientAccess access,
	  RoleVec scopes) mutable {
	if (error || app.id != self->m_config.appID ||
	    app.state != State::Active || app.owner) {
	  self->finish_(OAuthError::AccessDenied); return;
	}
	self->m_app = ZuMv(app);
	self->m_client = ZuMv(client);
	self->m_access = ZuMv(access);
	self->m_scopes = ZuMv(scopes);
	self->scopes_();
      });
  }

  void scopes_()
  {
    ScopeSelection selection;
    if (selectScopes(m_client, m_access, m_params.scope,
	m_scopes, selection)) {
      finish_(OAuthError::InvalidScope);
      return;
    }
    if (m_params.has(AuthorizeParams::Resource) &&
	m_app.audience != m_params.resource) {
      finish_(OAuthError::InvalidScope);
      return;
    }
    Grant grant;
    if (!authorizationBegin(*m_rng, grant, m_config.issuer, m_app, m_params,
	selection, m_config.passkey, m_bindingDigest,
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
      self = ZmRef<AuthorizeRequest__>{this}
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
  ClientAccess	m_access;
  RoleVec	m_scopes;
  App		m_app;
  int		m_profileError = ProfileError::OK;
  bool		m_done = false;
};
ZuDerive(AuthorizeRequestHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeRequest", AuthorizeRequest__<>>));
ZuDerive(AuthorizeRequest_, (AuthorizeRequest__<AuthorizeRequestHeap>));

template <typename Heap = ZuVoid>
class AuthorizeFinish__ : public Heap, public ZmPolymorph  {
public:
  AuthorizeFinish__(
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
	!m_config.issuer || !m_config.appID ||
	!m_config.origin || !m_config.rpID || m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    assertionVerify(m_context, m_ceremonyID, m_bindingDigest,
      ZuMv(m_input), m_config.issuer, m_config.appID,
      m_config.origin, m_config.rpID, m_config.now,
      [self = ZmRef<AuthorizeFinish__>{this}](
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
	ZuClear(location);
      return;
    }
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(error, ZuMv(location));
  }

  void asserted_(
      int error, User user, Cred cred, Grant grant, AssertionResult result)
  {
    if (!authorizationTarget_(m_config, grant)) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
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
    logEvent(ZuMv(audit));
    assertionDone_(error, ZuMv(user), ZuMv(cred), ZuMv(grant));
  }

  void assertionDone_(int error, User user, Cred cred, Grant grant)
  {
    if (error) {
      finish_(error == WebAuthnError::Storage ?
	OAuthError::ServerError : OAuthError::AccessDenied, {});
      return;
    }
    if (grant.appID) {
      auto assignments = m_context->assignments;
      assignments->run(0, [self = ZmRef<AuthorizeFinish__>{this},
          assignments, user = ZuMv(user), cred = ZuMv(cred),
          grant = ZuMv(grant)]() mutable {
        auto key = ZuFwdTuple(grant.appID, user.id);
        assignments->find<0>(0, ZuMv(key), [self = ZuMv(self),
            user = ZuMv(user), cred = ZuMv(cred),
            grant = ZuMv(grant)](ZdbRowRef<Assignment> row) mutable {
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
    finish_(OAuthError::AccessDenied, {});
  }

  void authorityLoad_(User user, Cred cred, Grant grant, IDVec roleIDs)
  {
    loadUserAuth(m_context, ZuMv(grant), ZuMv(user),
      ZuMv(cred), ZuMv(roleIDs), [self = ZmRef<AuthorizeFinish__>{this}](
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
      m_authority.actions, [self = ZmRef<AuthorizeFinish__>{this}](
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
        m_config.now, Evidence{}, [self = ZmRef<AuthorizeFinish__>{this},
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
	self = ZmRef<AuthorizeFinish__>{this}
      ](bool ok, String code) mutable {
	self->finished_(ok, ZuMv(code));
      });
  }

  void finished_(bool ok, String code)
  {
    if (!ok) {
      if (code && code.mutable_()) ZuClear(code);
      finish_(OAuthError::ServerError, {});
      return;
    }
    String location = codeRedirect(m_authority.grant, code);
    if (code && code.mutable_()) ZuClear(code);
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
ZuDerive(AuthorizeFinishHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeFinish", AuthorizeFinish__<>>));
ZuDerive(AuthorizeFinish_, (AuthorizeFinish__<AuthorizeFinishHeap>));

template <typename Heap = ZuVoid>
class AuthorizeSessionFinish__ : public Heap, public ZmPolymorph  {
public:
  AuthorizeSessionFinish__(DBContext *context, Ztls::Random *rng,
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
	!m_config.issuer || !m_config.appID ||
	m_session.state != State::Active || m_session.owner ||
	!m_session.userID || !m_session.authVersion ||
	m_session.authTime <= 0 || m_session.authTime > m_config.now ||
	m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, grants]() {
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
	!authorizationTarget_(m_config, row->data()) ||
	!Ztls::ctEqual(row->data().bindingDigest, m_bindingDigest)) {
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    Grant grant{row->data()};
    auto users = m_context->users;
    users->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, users,
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
      finish_(OAuthError::AccessDenied, {});
      return;
    }
    auto assignments = m_context->assignments;
    assignments->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this},
	assignments, grant = ZuMv(grant)]() mutable {
      assignments->find<0>(0,
	ZuFwdTuple(grant.appID, self->m_user.id), [self = ZuMv(self),
	  grant = ZuMv(grant)](ZdbRowRef<Assignment> row) mutable {
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
    policies->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, policies,
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
    providers->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, providers,
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
    evidence->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, evidence,
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
    maps->run(0, [self = ZmRef<AuthorizeSessionFinish__>{this}, maps,
	grant = ZuMv(grant)]() mutable {
      maps->selectRows<0>(ZuFwdTuple(grant.appID,
	  self->m_session.providerID), AuthorityScanLimit::RoleMappingsScan,
          [self, grant = ZuMv(grant)](
	    ZuUnion<void, Tuple> result, unsigned count) mutable {
	if (result.template is<Tuple>()) {
	  if (count > AuthorityScanLimit::RoleMappings) {
	    self->m_mappingOverflow = true; return;
	  }
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
      [self = ZmRef<AuthorizeSessionFinish__>{this}](
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
      m_authority.actions, [self = ZmRef<AuthorizeSessionFinish__>{this}](
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
        Evidence{m_evidence}, [self = ZmRef<AuthorizeSessionFinish__>{this},
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
      [self = ZmRef<AuthorizeSessionFinish__>{this}](
	  bool ok, String code) mutable { self->finished_(ok, ZuMv(code)); });
  }

  void finished_(bool ok, String code)
  {
    if (!ok) {
      if (code && code.mutable_()) ZuClear(code);
      finish_(OAuthError::ServerError, {});
      return;
    }
    String location = codeRedirect(m_authority.grant, code);
    if (code && code.mutable_()) ZuClear(code);
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
ZuDerive(AuthorizeSessionFinishHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeSessionFinish",
    AuthorizeSessionFinish__<>>));
ZuDerive(AuthorizeSessionFinish_,
  (AuthorizeSessionFinish__<AuthorizeSessionFinishHeap>));

template <typename Heap = ZuVoid>
class AuthorizeOIDCFinish__ : public Heap, public ZmPolymorph  {
public:
  AuthorizeOIDCFinish__(
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
	!m_config.issuer || !m_config.appID ||
	!m_user.id || !m_evidence.appID || !m_evidence.providerID ||
	!m_evidence.eligible || m_evidence.userID != m_user.id ||
	m_evidence.deadline <= m_config.now || m_authTime <= 0 ||
	m_config.now <= 0 ||
	m_config.codeExpires <= m_config.now || !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeOIDCFinish__>{this}, grants]() {
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
	!authorizationTarget_(m_config, row->data()) ||
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
      [self = ZmRef<AuthorizeOIDCFinish__>{this}](
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
      m_authority.actions, [self = ZmRef<AuthorizeOIDCFinish__>{this}](
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
        [self = ZmRef<AuthorizeOIDCFinish__>{this},
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
      [self = ZmRef<AuthorizeOIDCFinish__>{this}](
	  bool ok, String code) mutable { self->finished_(ok, ZuMv(code)); });
  }

  void finished_(bool ok, String code)
  {
    if (!ok) {
      if (code && code.mutable_()) ZuClear(code);
      finish_(OAuthError::ServerError, {});
      return;
    }
    String location = codeRedirect(m_authority.grant, code);
    if (code && code.mutable_()) ZuClear(code);
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
ZuDerive(AuthorizeOIDCFinishHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeOIDCFinish", AuthorizeOIDCFinish__<>>));
ZuDerive(AuthorizeOIDCFinish_,
  (AuthorizeOIDCFinish__<AuthorizeOIDCFinishHeap>));

template <typename Heap = ZuVoid>
class AuthorizeConsentFinish__ : public Heap, public ZmPolymorph  {
public:
  AuthorizeConsentFinish__(DB *db, DBContext *context, Ztls::Random *rng,
      Bytes ceremonyID, Bytes bindingDigest, bool approve,
      AuthorizeFinishConfig config, PolicyFn policy,
      AuthorizeCodeFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_ceremonyID{ZuMv(ceremonyID)},
    m_bindingDigest{ZuMv(bindingDigest)}, m_approve{approve},
    m_config{ZuMv(config)}, m_policy{ZuMv(policy)},
    m_complete{ZuMv(complete)} { }

  ~AuthorizeConsentFinish__()
  {
    if (m_code && m_code.mutable_()) ZuClear(m_code);
  }

  void start()
  {
    if (!m_db || !m_context || !m_rng || m_ceremonyID.length() != OpaqueIDSize || !m_bindingDigest ||
        !m_config.issuer || !m_config.appID ||
        m_config.now <= 0 || m_config.codeExpires <= m_config.now ||
        !m_policy) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<AuthorizeConsentFinish__>{this}, grants]() {
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
    if (m_code && m_code.mutable_()) ZuClear(m_code);
    m_code.null();
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
      authorizationTarget_(m_config, grant) &&
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
    clients->run(0, [self = ZmRef<AuthorizeConsentFinish__>{this}, clients,
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
    grants->run(0, [self = ZmRef<AuthorizeConsentFinish__>{this}, grants]() {
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
      m_authority.actions, [self = ZmRef<AuthorizeConsentFinish__>{this}](
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
      m_grant.appID);
    consents->run(0, [self = ZmRef<AuthorizeConsentFinish__>{this}, consents,
        key = ZuMv(key)]() mutable {
      consents->find<0>(0, ZuMv(key), [self](ZdbRowRef<Consent> row) mutable {
        Consent before;
        if (row) before = row->data();
        else before.version = 0;
        self->issue_(ZuMv(before));
      });
    });
  }

  void issue_(Consent before)
  {
    int64_t expires = m_config.codeExpires;
    if (m_authority.authorityDeadline > 0 &&
        m_authority.authorityDeadline < expires)
      expires = m_authority.authorityDeadline;
    ConsentCode change{.beforeGrant = m_grant, .afterGrant = m_grant,
      .beforeConsent = ZuMv(before), .roleIDs = m_authority.selection.roleIDs,
      .now = m_config.now};
    if (!authorizationFinish(*m_rng, change.afterGrant, m_bindingDigest,
      m_authority.user.id, m_authority.cred.id,
      m_authority.principalRoleIDs, ZuMv(m_actions),
      m_authority.app.authVersion, m_authority.user.authVersion,
      m_grant.authTime, expires, m_code)) {
      finish_(OAuthError::ServerError, {});
      return;
    }
    ZdbSagaID id;
    ZuAssert(sizeof(id) == OpaqueIDSize);
    memcpy(&id, m_ceremonyID.data(), sizeof(id));
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(change));
    if (!sagaSubmit(m_db, id, ZuMv(saga),
      [self = ZmRef<AuthorizeConsentFinish__>{this}](bool ok) mutable {
	if (!ok) self->finish_(OAuthError::ServerError, {});
      }, [self = ZmRef<AuthorizeConsentFinish__>{this}](bool ok) mutable {
	String location;
	if (ok) location = codeRedirect(self->m_grant, self->m_code);
	if (self->m_code && self->m_code.mutable_())
	  ZuClear(self->m_code);
	self->m_code.null();
	self->finish_(ok ? AuthorizeIssue::OK : OAuthError::ServerError, ZuMv(location));
      }, ZuTime{expires})) finish_(OAuthError::ServerError, {});
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_ceremonyID;
  Bytes		m_bindingDigest;
  bool		m_approve = false;
  AuthorizeFinishConfig m_config;
  PolicyFn	m_policy;
  AuthorizeCodeFn m_complete;
  Grant		m_grant;
  String	m_code;
  AuthorityData m_authority;
  ZtBitmap	m_actions;
  bool		m_done = false;
};
ZuDerive(AuthorizeConsentFinishHeap,
  (ZmHeap<"Zum.zumd.authorize.AuthorizeConsentFinish",
    AuthorizeConsentFinish__<>>));
ZuDerive(AuthorizeConsentFinish_,
  (AuthorizeConsentFinish__<AuthorizeConsentFinishHeap>));

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
    Requests *requests, ZuTime deadline, DB *db, DBContext *context,
    Ztls::Random &rng, Bytes ceremonyID, Bytes bindingDigest, bool approve,
    AuthorizeFinishConfig config, PolicyFn policy,
    AuthorizeCodeFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<AuthorizeCodeComplete_> state =
    new AuthorizeCodeComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, db, context, &rng,
    ceremonyID = ZuMv(ceremonyID), bindingDigest = ZuMv(bindingDigest),
    approve, config = ZuMv(config), policy = ZuMv(policy)
  ](ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<AuthorizeConsentFinish_> finish = new AuthorizeConsentFinish_{
      db, context, &rng, ZuMv(ceremonyID), ZuMv(bindingDigest), approve,
      ZuMv(config), ZuMv(policy), [state](int error,
          String location) mutable {
        state->complete(error, ZuMv(location));
      }};
    finish->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
