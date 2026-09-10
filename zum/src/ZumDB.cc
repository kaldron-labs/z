//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumDB.hh>


#include <zlib/ZtlsRandom.hh>

namespace Zum {

bool sagaSubmit(
    DB *db, ZdbSagaID id, ZmRef<MSaga> saga,
    SagaFn submit, SagaFn complete)
{
  return db && db->saga(0, id, ZuMv(saga), ZuMv(submit), ZuMv(complete));
}

static bool authorityHasID(const IDVec &ids, uint64_t id)
{
  for (auto value: ids) if (value == id) return true;
  return false;
}

class ClientScopes_ : public ZumPolymorph {
public:
  ClientScopes_(DBContext *context, Client client, ClientScopesFn complete) :
    m_context{context}, m_client{ZuMv(client)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    m_client.scopeIDs.null();
    m_client.audiences.null();
    if (!m_client.appID || m_client.state != State::Active || m_client.owner) {
      finish_(AuthorityError::Invalid);
      return;
    }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<ClientScopes_>{this}, apps]() {
      apps->find<0>(0, ZuFwdTuple(self->m_client.appID), [self = ZuMv(self)](
	  ZdbRowRef<App> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(AuthorityError::Invalid);
	  return;
	}
	self->m_app = row->data();
	self->access_();
      });
    });
  }

private:
  void finish_(int error)
  {
    auto complete = ZuMv(m_complete);
    if (error) { complete(error, App{}, Client{}, ScopeVec{}); return; }
    complete(0, ZuMv(m_app), ZuMv(m_client), ZuMv(m_scopes));
  }

  void access_()
  {
    auto access = m_context->clientAccess;
    access->run(0, [self = ZmRef<ClientScopes_>{this}, access]() {
      access->find<0>(0, ZuFwdTuple(self->m_client.id, self->m_client.appID),
	[self = ZuMv(self)](ZdbRowRef<ClientAccess> row) mutable {
	// No resource approval is needed for identity-only OIDC scopes.
	if (!row) { self->finish_(0); return; }
	if (row->data().state != State::Active || row->data().owner) {
	  self->finish_(AuthorityError::Invalid);
	  return;
	}
	self->m_client.scopeIDs = row->data().scopeIDs;
	self->m_audienceIDs = row->data().audienceIDs;
	self->scope_();
      });
    });
  }

  void scope_()
  {
    if (m_offset >= m_client.scopeIDs.length()) { finish_(0); return; }
    ScopeID id = m_client.scopeIDs[m_offset++];
    auto scopes = m_context->scopes;
    scopes->run(0, [self = ZmRef<ClientScopes_>{this}, scopes, id]() {
      scopes->find<0>(0, ZuFwdTuple(self->m_client.appID, id),
	[self = ZuMv(self)](ZdbRowRef<Scope> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner ||
	    !authorityHasID(self->m_audienceIDs, row->data().audienceID)) {
	  self->scope_();
	  return;
	}
	self->audience_(row->data());
      });
    });
  }

  void audience_(Scope scope)
  {
    auto audiences = m_context->audiences;
    audiences->run(0, [self = ZmRef<ClientScopes_>{this}, audiences,
	scope = ZuMv(scope)]() mutable {
      AudienceID id = scope.audienceID;
      audiences->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self),
	  scope = ZuMv(scope)](ZdbRowRef<Audience> row) mutable {
	if (row && row->data().appID == self->m_client.appID &&
	    row->data().state == State::Active && !row->data().owner &&
	    row->data().uri) {
	  scope.audience = row->data().uri;
	  self->m_client.audiences.push(scope.audience);
	  self->m_scopes.push(ZuMv(scope));
	}
	self->scope_();
      });
    });
  }

  DBContext	*m_context;
  App		m_app;
  Client	m_client;
  IDVec		m_audienceIDs;
  ScopeVec	m_scopes;
  unsigned	m_offset = 0;
  ClientScopesFn m_complete;
};

void clientScopes(DBContext *context, Client client, ClientScopesFn complete)
{
  ZmRef<ClientScopes_> load =
    new ClientScopes_{context, ZuMv(client), ZuMv(complete)};
  load->start();
}

class AuthorityLoad_ : public ZumPolymorph {
public:
  AuthorityLoad_(
      DBContext *context, Grant grant, Client client, bool requestedPresent,
      String requested, int64_t now, AuthorityFn complete) :
    m_context{context}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{true},
    m_ceiling{true}, m_now{now}, m_complete{ZuMv(complete)}
  {
    m_data.grant = ZuMv(grant);
    m_data.client = ZuMv(client);
    m_data.principalRoleIDs = m_data.grant.roleIDs;
  }

  AuthorityLoad_(
      DBContext *context, Grant grant, User user, Cred cred,
      IDVec principalRoleIDs,
      AuthorityFn complete) :
    m_context{context}, m_interactive{true}, m_clientLoad{true},
    m_complete{ZuMv(complete)}
  {
    grant.userID = user.id;
    grant.credentialID = cred.id;
    grant.userVersion = user.authVersion;
    m_data.grant = ZuMv(grant);
    m_data.user = ZuMv(user);
    m_data.cred = ZuMv(cred);
    m_data.principalRoleIDs = ZuMv(principalRoleIDs);
  }

  AuthorityLoad_(
      DBContext *context, String issuer, Client client,
      bool requestedPresent, String requested, AuthorityFn complete) :
    m_context{context}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{false},
    m_complete{ZuMv(complete)}
  {
    m_data.issuer.id = ZuMv(issuer);
    m_data.clientAppID = client.appID;
    m_data.client = ZuMv(client);
  }

  void start()
  {
    if (m_interactive && !m_clientLoad &&
        m_data.grant.authoritySource == UserSource::External) {
      delegatedPolicy_();
      return;
    }
    m_context->issuers->run(0, [self = ZmRef<AuthorityLoad_>{this}]() {
      self->issuer_();
    });
  }

private:
  void finish_(int error)
  {
    auto complete = ZuMv(m_complete);
    if (error) m_data = {};
    complete(error, ZuMv(m_data));
  }

  void delegatedPolicy_()
  {
    if (!m_now || !m_data.grant.appID || !m_data.grant.userID ||
        !m_data.grant.authorityProviderID || !m_data.grant.policyVersion ||
        !m_data.grant.evidenceVersion) {
      finish_(AuthorityError::Invalid);
      return;
    }
    auto policies = m_context->authPolicies;
    policies->run(0, [self = ZmRef<AuthorityLoad_>{this}, policies]() {
      policies->find<0>(0, ZuFwdTuple(self->m_data.grant.appID), [
          self = ZuMv(self)](ZdbRowRef<AuthPolicy> row) mutable {
        if (!row || row->data().state != State::Active || row->data().owner ||
            row->data().providerID !=
              self->m_data.grant.authorityProviderID ||
            row->data().version != self->m_data.grant.policyVersion ||
            !row->data().assignmentMaxAge) {
          self->finish_(AuthorityError::Invalid);
          return;
        }
        self->delegatedProvider_();
      });
    });
  }

  void delegatedProvider_()
  {
    auto providers = m_context->providers;
    providers->find<0>(0, ZuFwdTuple(m_data.grant.authorityProviderID), [
        self = ZmRef<AuthorityLoad_>{this}](ZdbRowRef<Provider> row) mutable {
      if (!row || row->data().state != State::Active || row->data().owner) {
        self->finish_(AuthorityError::Invalid);
        return;
      }
      self->m_provider = row->data();
      self->delegatedEvidence_();
    });
  }

  void delegatedEvidence_()
  {
    auto evidence = m_context->evidence;
    evidence->find<0>(0, ZuFwdTuple(m_data.grant.appID,
        m_data.grant.userID, m_data.grant.authorityProviderID), [
        self = ZmRef<AuthorityLoad_>{this}](ZdbRowRef<Evidence> row) mutable {
      if (!row || row->data().owner || !row->data().eligible ||
          row->data().deadline <= self->m_now ||
          row->data().policyVersion != self->m_data.grant.policyVersion ||
          row->data().version < self->m_data.grant.evidenceVersion ||
          row->data().source != self->m_provider.claimSource) {
        self->finish_(AuthorityError::Invalid);
        return;
      }
      self->m_evidence = row->data();
      self->m_data.authorityDeadline = row->data().deadline;
      self->m_data.principalRoleIDs.null();
      self->delegatedRoles_();
    });
  }

  void delegatedRoles_()
  {
    auto mappings = m_context->roleMaps;
    mappings->run(0, [self = ZmRef<AuthorityLoad_>{this}, mappings]() {
      using Tuple = RoleMapTable::Tuple;
      mappings->selectRows<0>(ZuFwdTuple(self->m_data.grant.appID,
          self->m_data.grant.authorityProviderID), 129, [self](
          ZuUnion<void, Tuple> result, unsigned count) mutable {
        if (result.template is<Tuple>()) {
          if (count > 128) { self->m_mappingOverflow = true; return; }
          auto tuple = ZuMv(result).template p<Tuple>();
          ZuTupleCall(ZuMv(tuple), [self](auto &&...args) {
            RoleMap map{ZuFwd<decltype(args)>(args)...};
            if (map.state != State::Active || map.owner || !map.roleID)
              return;
            for (auto &value: self->m_evidence.roleValues)
              if (value == map.value) {
                if (!authorityHasID(self->m_data.principalRoleIDs, map.roleID))
                  self->m_data.principalRoleIDs.push(map.roleID);
                break;
              }
          });
          return;
        }
        if (self->m_mappingOverflow) {
          self->finish_(AuthorityError::Invalid);
          return;
        }
        auto issuers = self->m_context->issuers;
        issuers->run(0, [self = ZuMv(self)]() mutable { self->issuer_(); });
      });
    });
  }

  void issuer_()
  {
    String id = m_interactive ? m_data.grant.issuer : m_data.issuer.id;
    m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Issuer> row) {
      if (!row || (self->m_interactive && !self->m_clientLoad &&
          self->m_data.client.appID != self->m_data.grant.appID)) {
        self->finish_(AuthorityError::Invalid);
        return;
      }
      self->m_data.issuer = row->data();
      if (!self->m_interactive) {
	if (!self->m_data.client.appID) {
	  self->m_data.app.nextActionID = row->data().nextActionID;
	  self->m_data.app.authVersion = row->data().authVersion;
	  self->m_data.principalRoleIDs = self->m_data.client.roleIDs;
	  self->principal_();
	  return;
	}
	auto apps = self->m_context->apps;
	apps->run(0, [self = ZuMv(self), apps]() mutable {
	  apps->find<0>(0, ZuFwdTuple(self->m_data.client.appID), [
	      self = ZuMv(self)](ZdbRowRef<App> app) mutable {
	    if (!app || app->data().state != State::Active ||
		app->data().owner) {
	      self->finish_(AuthorityError::Invalid);
	      return;
	    }
	    self->clientAccess_();
	  });
	});
	return;
      }
      AppID appID = self->m_interactive ? self->m_data.grant.appID :
        self->m_data.client.appID;
      if (!appID) {
        self->m_data.app.nextActionID = row->data().nextActionID;
        self->m_data.app.authVersion = row->data().authVersion;
        self->principal_();
        return;
      }
      self->m_context->apps->find<0>(0, ZuFwdTuple(appID), [self = ZuMv(self)](
          ZdbRowRef<App> app) mutable {
        if (!app || app->data().state != State::Active || app->data().owner) {
          self->finish_(AuthorityError::Invalid);
          return;
        }
        self->m_data.app = app->data();
        self->principal_();
      });
    });
  }

  void clientAccess_()
  {
    auto access = m_context->clientAccess;
    String clientID = m_data.client.id;
    access->run(0, [self = ZmRef<AuthorityLoad_>{this}, access,
	clientID = ZuMv(clientID)]() mutable {
      using Tuple = ClientAccessTable::Tuple;
      access->selectRows<0>(ZuFwdTuple(ZuMv(clientID)), 65, [self](
	  ZuUnion<void, Tuple> result, unsigned count) mutable {
	if (result.template is<Tuple>()) {
	  if (count > 64) { self->m_accessOverflow = true; return; }
	  auto tuple = ZuMv(result).template p<Tuple>();
	  ZuTupleCall(ZuMv(tuple), [self](auto &&...args) {
	    self->m_access.push(ClientAccess{ZuFwd<decltype(args)>(args)...});
	  });
	  return;
	}
	if (self->m_accessOverflow) {
	  self->finish_(AuthorityError::Invalid);
	  return;
	}
	self->accessNext_();
      });
    });
  }

  void accessNext_()
  {
    m_candidateScopes.null();
    m_candidateAudiences.null();
    m_candidateScope = 0;
    while (m_accessIndex < m_access.length()) {
      m_candidate = m_access[m_accessIndex++];
      if (m_candidate.state != State::Active || m_candidate.owner ||
	  !m_candidate.appID || !m_candidate.scopeIDs) continue;
      auto apps = m_context->apps;
      apps->run(0, [self = ZmRef<AuthorityLoad_>{this}, apps]() mutable {
	apps->find<0>(0, ZuFwdTuple(self->m_candidate.appID), [
	    self = ZuMv(self)](ZdbRowRef<App> app) mutable {
	  if (!app || app->data().state != State::Active || app->data().owner) {
	    self->accessNext_();
	    return;
	  }
	  self->m_candidateApp = app->data();
	  self->candidateScope_();
	});
      });
      return;
    }
    finish_(ScopeError::Unavailable);
  }

  void candidateScope_()
  {
    if (m_candidateScope >= m_candidate.scopeIDs.length()) {
      Client candidate = m_data.client;
      candidate.appID = m_candidate.appID;
      candidate.scopeIDs = m_candidate.scopeIDs;
      candidate.roleIDs = m_candidate.roleIDs;
      candidate.audiences = ZuMv(m_candidateAudiences);
      ScopeSelection selection;
      int error = selectGrantedScopes(candidate, candidate.scopeIDs,
	m_requestedPresent, m_requested, m_candidateScopes, selection);
      if (error) { accessNext_(); return; }
      m_data.client = ZuMv(candidate);
      m_data.app = ZuMv(m_candidateApp);
      m_data.scopes = ZuMv(m_candidateScopes);
      m_data.principalRoleIDs = m_candidate.roleIDs;
      scopesDone_();
      return;
    }
    auto id = m_candidate.scopeIDs[m_candidateScope++];
    auto scopes = m_context->scopes;
    scopes->run(0, [self = ZmRef<AuthorityLoad_>{this}, scopes, id]() mutable {
      scopes->find<0>(0, ZuFwdTuple(self->m_candidate.appID, id), [
	  self = ZuMv(self)](ZdbRowRef<Scope> scope) mutable {
	if (!scope || scope->data().state != State::Active ||
	    scope->data().owner || !authorityHasID(
	      self->m_candidate.audienceIDs, scope->data().audienceID)) {
	  self->candidateScope_();
	  return;
	}
	auto audiences = self->m_context->audiences;
	Scope value = scope->data();
	audiences->run(0, [self = ZuMv(self), audiences,
	    value = ZuMv(value)]() mutable {
	  AudienceID id = value.audienceID;
	  audiences->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self),
	      value = ZuMv(value)](ZdbRowRef<Audience> audience) mutable {
	    if (audience && audience->data().appID == self->m_candidate.appID &&
		audience->data().state == State::Active && !audience->data().owner &&
		audience->data().uri) {
	      value.audience = audience->data().uri;
	      self->m_candidateAudiences.push(value.audience);
	      self->m_candidateScopes.push(ZuMv(value));
	    }
	    self->candidateScope_();
	  });
	});
      });
    });
  }

  void principal_()
  {
    if (m_clientLoad) client_();
    else if (m_interactive) catalog_();
    else scopes_();
  }

  void client_()
  {
    String id = m_data.grant.clientID;
    m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Client> row) {
      if (!row || row->data().appID != self->m_data.grant.appID) {
        self->finish_(AuthorityError::Invalid);
        return;
      }
      self->m_data.client = row->data();
      self->catalog_();
    });
  }

  void catalog_()
  {
    clientScopes(m_context, ZuMv(m_data.client),
      [self = ZmRef<AuthorityLoad_>{this}](
	  int error, App app, Client client, ScopeVec scopes) mutable {
	if (error) { self->finish_(error); return; }
	self->m_data.app = ZuMv(app);
	self->m_data.client = ZuMv(client);
	self->m_data.scopes = ZuMv(scopes);
	if (self->m_clientLoad) self->scopesDone_();
	else self->user_();
      });
  }

  void user_()
  {
    auto id = m_data.grant.userID;
    m_context->users->find<0>(0, ZuFwdTuple(id), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<User> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.user = row->data();
      if (self->m_data.grant.appID &&
	  self->m_data.grant.authoritySource == UserSource::Local)
	self->membership_();
      else self->userDone_();
    });
  }

  void membership_()
  {
    auto memberships = m_context->memberships;
    memberships->run(0, [self = ZmRef<AuthorityLoad_>{this}, memberships]() {
      memberships->find<0>(0, ZuFwdTuple(self->m_data.grant.appID,
	  self->m_data.grant.userID), [self = ZuMv(self)](
	    ZdbRowRef<Membership> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(AuthorityError::Invalid);
	  return;
	}
	self->m_data.principalRoleIDs = row->data().roleIDs;
	self->userDone_();
      });
    });
  }

  void userDone_()
  {
    if (m_data.grant.credentialID) cred_();
    else scopes_();
  }

  void cred_()
  {
    Bytes id = m_data.grant.credentialID;
    m_context->creds->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Cred> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.cred = row->data();
      self->scopes_();
    });
  }

  const IDVec &scopeIDs_() const
  {
    return m_interactive ? m_data.grant.scopeIDs : m_data.client.scopeIDs;
  }

  void scopes_()
  {
    if (m_interactive) { scopesDone_(); return; }
    auto &ids = scopeIDs_();
    if (m_index >= ids.length()) { scopesDone_(); return; }
    auto id = ids[m_index];
    AppID appID = m_interactive ? m_data.grant.appID : m_data.client.appID;
    m_context->scopes->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Scope> row) {
      if (row) self->m_data.scopes.push(row->data());
      ++self->m_index;
      self->scopes_();
    });
  }

  void scopesDone_()
  {
    int error;
    if (m_interactive) {
      if (!interactivePrincipal(m_data.grant, m_data.user,
          m_data.cred, m_data.client)) {
	finish_(AuthorityError::Invalid);
	return;
      }
      error = selectGrantedScopes(m_data.client, m_data.grant.scopeIDs,
	m_data.grant.scope, m_requestedPresent, m_requested,
	m_data.scopes, m_data.selection);
      if (!error && m_data.selection.audience != m_data.grant.audience)
	error = ScopeError::Audience;
    } else {
      if (!clientPrincipal(m_data.client)) {
	finish_(AuthorityError::Invalid);
	return;
      }
      error = selectGrantedScopes(m_data.client, m_data.client.scopeIDs,
	m_requestedPresent, m_requested, m_data.scopes, m_data.selection);
    }
    if (error) { finish_(error); return; }

    auto &principal = m_data.principalRoleIDs;
    for (auto id: principal)
      if (authorityHasID(m_data.selection.roleIDs, id) &&
	  !authorityHasID(m_roleIDs, id))
	m_roleIDs.push(id);
    m_index = 0;
    roles_();
  }

  void roles_()
  {
    if (m_index >= m_roleIDs.length()) { rolesDone_(); return; }
    auto id = m_roleIDs[m_index];
    AppID appID = m_interactive ? m_data.grant.appID : m_data.client.appID;
    m_context->roles->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Role> row) {
      if (row) self->m_data.roles.push(row->data());
      ++self->m_index;
      self->roles_();
    });
  }

  void rolesDone_()
  {
    for (auto &role: m_data.roles)
      if (role.state == State::Active && !role.owner)
	m_actionIDs |= role.actions;
    if (m_ceiling)
      m_actionIDs = intersectActions(ZuMv(m_actionIDs), m_data.grant.actions);
    m_action = m_actionIDs.first();
    actions_();
  }

  void actions_()
  {
    if (m_action < 0 || unsigned(m_action) >= m_data.app.nextActionID) {
      actionsDone_();
      return;
    }
    auto id = ActionID(m_action);
    AppID appID = m_interactive ? m_data.grant.appID : m_data.client.appID;
    m_context->actions->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Action> row) {
      if (row) self->m_data.actionRecords.push(row->data());
      self->m_action = self->m_actionIDs.next(self->m_action);
      self->actions_();
    });
  }

  void actionsDone_()
  {
    const auto &principal = m_data.principalRoleIDs;
    m_data.actions = effectiveActions(m_data.app.nextActionID,
      principal, m_data.selection.roleIDs,
      m_data.roles, m_data.actionRecords);
    finish_(ScopeError::OK);
  }

  DBContext	*m_context = nullptr;
  AuthorityData	m_data;
  ZtArray<ClientAccess, VecHeap> m_access;
  ClientAccess	m_candidate;
  App		m_candidateApp;
  ScopeVec	m_candidateScopes;
  StringVec	m_candidateAudiences;
  String	m_requested;
  IDVec		m_roleIDs;
  ZtBitmap	m_actionIDs;
  unsigned	m_index = 0;
  unsigned	m_accessIndex = 0;
  unsigned	m_candidateScope = 0;
  int		m_action = -1;
  bool		m_requestedPresent = false;
  bool		m_interactive = false;
  bool		m_clientLoad = false;
  bool		m_ceiling = false;
  bool		m_accessOverflow = false;
  bool		m_mappingOverflow = false;
  int64_t	m_now = 0;
  Provider	m_provider;
  Evidence	m_evidence;
  AuthorityFn	m_complete;
};

void loadGrantAuth(
    DBContext *context, Grant grant, Client client, bool requestedPresent,
    String requested, int64_t now, AuthorityFn complete)
{
  ZmRef<AuthorityLoad_> load = new AuthorityLoad_{context, ZuMv(grant),
    ZuMv(client), requestedPresent, ZuMv(requested), now, ZuMv(complete)};
  load->start();
}

void loadClientAuth(
    DBContext *context, String issuer, Client client,
    bool requestedPresent, String requested, AuthorityFn complete)
{
  ZmRef<AuthorityLoad_> load = new AuthorityLoad_{context, ZuMv(issuer),
    ZuMv(client), requestedPresent, ZuMv(requested), ZuMv(complete)};
  load->start();
}

void loadUserAuth(
    DBContext *context, Grant grant, User user, Cred cred,
    IDVec principalRoleIDs,
    AuthorityFn complete)
{
  ZmRef<AuthorityLoad_> load = new AuthorityLoad_{context, ZuMv(grant),
    ZuMv(user), ZuMv(cred), ZuMv(principalRoleIDs), ZuMv(complete)};
  load->start();
}

int enrollmentPrepare(
    const Grant &ceremony, ZuBSpan bindingDigest, RegistrationInput &input,
    ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, Enrollment &enrollment)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      (ceremony.purpose != GrantPurpose::Enrollment &&
       ceremony.purpose != GrantPurpose::Bootstrap) ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.userID || !ceremony.userName ||
      !ceremony.userHandle || now <= 0)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  Enrollment next;
  next.ceremonyID = ceremony.id;
  next.userID = ceremony.userID;
  next.name = ceremony.userName;
  next.handle = ceremony.userHandle;
  next.roleIDs = ceremony.roleIDs;
  next.credentialID = ZuMv(result.credentialID);
  next.publicKey = ZuMv(result.publicKey);
  next.signCount = result.signCount;
  next.created = now;
  next.backupEligible = result.backupEligible;
  next.backedUp = result.backedUp;
  next.label = ceremony.label;
  next.precreated = ceremony.purpose == GrantPurpose::Bootstrap ||
    ceremony.actor == "precreated";
  enrollment = ZuMv(next);
  return WebAuthnError::OK;
}

int credentialPrepare(
    const Grant &ceremony, ZuBSpan bindingDigest, RegistrationInput &input,
    ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, CredentialAdd &add)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      ceremony.purpose != GrantPurpose::AddCredential ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.issuer || !ceremony.userID || !ceremony.userHandle || now <= 0)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  CredentialAdd next{
    .ceremonyID = ceremony.id,
    .issuer = ceremony.issuer,
    .userID = ceremony.userID,
    .userHandle = ceremony.userHandle,
    .credentialID = ZuMv(result.credentialID),
    .publicKey = ZuMv(result.publicKey),
    .signCount = result.signCount,
    .created = now,
    .backupEligible = result.backupEligible,
    .backedUp = result.backedUp,
    .label = ceremony.label,
    .userVersion = ceremony.userVersion
  };
  add = ZuMv(next);
  return WebAuthnError::OK;
}

int recoveryPrepare(
    const Grant &ceremony, const User &user, ZuBSpan bindingDigest,
    RegistrationInput &input, ZuCSpan origin, ZuCSpan rpID,
    unsigned credentialIDMax,
    int64_t now, RecoveryEnroll &recovery)
{
  if (ceremony.kind != GrantKind::Ceremony ||
      ceremony.purpose != GrantPurpose::Recovery ||
      ceremony.state != State::Active || ceremony.owner ||
      !ceremony.challenge || ceremony.expires <= now ||
      !Ztls::ctEqual(ceremony.bindingDigest, bindingDigest))
    return WebAuthnError::Ceremony;
  if (!ceremony.issuer || !ceremony.actor || !ceremony.userID ||
      !ceremony.userHandle || now <= 0 ||
      user.id != ceremony.userID || user.state != State::Suspended ||
      user.owner || user.authVersion != ceremony.userVersion || !user.handle)
    return WebAuthnError::Fields;

  RegistrationResult result;
  int error = verifyRegistration(input,
    RegistrationState{ceremony.challenge, origin, rpID},
    credentialIDMax, result);
  if (error) return error;

  recovery = RecoveryEnroll{
    .ceremonyID = ceremony.id,
    .issuer = ceremony.issuer,
    .actor = ceremony.actor,
    .userID = ceremony.userID,
    .userVersion = ceremony.userVersion,
    .oldHandle = user.handle,
    .newHandle = ceremony.userHandle,
    .credentialID = ZuMv(result.credentialID),
    .publicKey = ZuMv(result.publicKey),
    .signCount = result.signCount,
    .created = now,
    .backupEligible = result.backupEligible,
    .backedUp = result.backedUp,
    .label = ceremony.label
  };
  return WebAuthnError::OK;
}

bool codeFamilyPrepare(
    Ztls::Random &rng, const Grant &code, ZuBSpan codeDigest,
    String scope, IDVec scopeIDs, ZtBitmap actions, uint64_t authVersion,
    int64_t now, int64_t expires, CodeFamily &family, String &refreshToken)
{
  OpaqueToken refresh;
  if (!opaqueIssue(rng, refresh)) return false;
  CodeFamily next;
  next.codeID = code.id;
  next.codeDigest = codeDigest;
  next.familyID = ZuMv(refresh.id);
  next.issuer = code.issuer;
  next.appID = code.appID;
  next.authorityProviderID = code.authorityProviderID;
  next.userID = code.userID;
  next.clientID = code.clientID;
  next.facadeClientID = code.facadeClientID;
  next.credentialID = code.credentialID;
  next.audience = code.audience;
  next.scope = ZuMv(scope);
  next.nonce = code.nonce;
  next.scopeIDs = ZuMv(scopeIDs);
  next.roleIDs = code.roleIDs;
  next.actions = ZuMv(actions);
  next.digest = ZuMv(refresh.digest);
  next.authVersion = authVersion;
  next.userVersion = code.userVersion;
  next.policyVersion = code.policyVersion;
  next.evidenceVersion = code.evidenceVersion;
  next.authoritySource = code.authoritySource;
  next.authTime = code.authTime;
  next.created = now;
  next.expires = expires;
  family = ZuMv(next);
  refreshToken = ZuMv(refresh.token);
  return true;
}

ZmRef<DBContext> registerSchema(DB *db)
{
  ZmRef<DBContext> context = new DBContext{};
  context->issuers = db->initTable<Issuer>("zum.issuer");
  context->apps = db->initTable<App>("zum.app");
  context->users = db->initTable<User>("zum.user");
  context->creds = db->initTable<Cred>("zum.cred");
  context->memberships = db->initTable<Membership>("zum.membership");
  context->actions = db->initTable<Action>("zum.action");
  context->roles = db->initTable<Role>("zum.role");
  context->scopes = db->initTable<Scope>("zum.scope");
  context->audiences = db->initTable<Audience>("zum.audience");
  context->clients = db->initTable<Client>("zum.client");
  context->clientAccess =
    db->initTable<ClientAccess>("zum.client_access");
  context->adminAccess =
    db->initTable<AdminAccess>("zum.admin_access");
  context->providers = db->initTable<Provider>("zum.provider");
  context->authPolicies =
    db->initTable<AuthPolicy>("zum.auth_policy");
  context->extIdentities =
    db->initTable<ExtIdentity>("zum.ext_identity");
  context->roleMaps = db->initTable<RoleMap>("zum.role_map");
  context->evidence = db->initTable<Evidence>("zum.evidence");
  context->sessions = db->initTable<Session>("zum.session");
  context->consents = db->initTable<Consent>("zum.consent");
  context->grants = db->initTable<Grant>("zum.grant");
  context->signKeys = db->initTable<SignKey>("zum.sign_key");
  context->audits = db->initTable<Audit>("zum.audit");
  context->requests = db->initTable<IdemRequest>("zum.request");
  db->sagas(context);
  return context;
}

} // namespace Zum
