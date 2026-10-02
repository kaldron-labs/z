//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_provider_db.hh>

namespace Zum {

static bool authorityHasID(const IDVec &ids, uint64_t id)
{
  for (auto value: ids) if (value == id) return true;
  return false;
}

template <typename Heap = ZuVoid>
class ClientScopes__ : public Heap, public ZmPolymorph  {
public:
  ClientScopes__(DBContext *context, Client client, ClientScopesFn complete) :
    m_context{context}, m_client{ZuMv(client)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_client.appID || m_client.state != State::Active || m_client.owner) {
      finish_(AuthorityError::Invalid); return;
    }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<ClientScopes__>{this}, apps]() {
      apps->find<0>(0, ZuFwdTuple(self->m_client.appID), [self = ZuMv(self)](
          ZdbRowRef<App> row) mutable {
        if (!row || !row->data().audience ||
            row->data().state != State::Active || row->data().owner) {
          self->finish_(AuthorityError::Invalid); return;
        }
        self->m_app = row->data(); self->access_();
      });
    });
  }

private:
  void finish_(int error)
  {
    auto complete = ZuMv(m_complete);
    if (error) { complete(error, App{}, Client{}, ClientAccess{}, RoleVec{}); return; }
    complete(0, ZuMv(m_app), ZuMv(m_client), ZuMv(m_access), ZuMv(m_scopes));
  }

  void access_()
  {
    auto access = m_context->clientAccess;
    access->run(0, [self = ZmRef<ClientScopes__>{this}, access]() {
      access->find<0>(0, ZuFwdTuple(self->m_client.id, self->m_client.appID),
        [self = ZuMv(self)](ZdbRowRef<ClientAccess> row) mutable {
          if (!row) { self->finish_(0); return; }
          if (row->data().state != State::Active || row->data().owner) {
            self->finish_(AuthorityError::Invalid); return;
          }
          self->m_access = row->data(); self->role_();
        });
    });
  }

  void role_()
  {
    if (m_roleOffset >= m_access.roleIDs.length()) { finish_(0); return; }
    RoleID id = m_access.roleIDs[m_roleOffset++];
    auto roles = m_context->roles;
    roles->run(0, [self = ZmRef<ClientScopes__>{this}, roles, id]() {
      roles->find<0>(0, ZuFwdTuple(self->m_client.appID, id),
        [self = ZuMv(self)](ZdbRowRef<Role> row) mutable {
          if (!row || row->data().state != State::Active || row->data().owner ||
              row->data().tombstone) { self->role_(); return; }
          self->m_scopes.push(row->data());
          self->role_();
        });
    });
  }

  DBContext *m_context;
  App m_app;
  Client m_client;
  ClientAccess m_access;
  RoleVec m_scopes;
  unsigned m_roleOffset = 0;
  ClientScopesFn m_complete;
};
ZuDerive(ClientScopesHeap,
  (ZmHeap<"Zum.zumd.db.ClientScopes", ClientScopes__<>>));
ZuDerive(ClientScopes_, (ClientScopes__<ClientScopesHeap>));

void clientScopes(DBContext *context, Client client, ClientScopesFn complete)
{
  ZmRef<ClientScopes_> load =
    new ClientScopes_{context, ZuMv(client), ZuMv(complete)};
  load->start();
}

template <typename Heap = ZuVoid>
class AuthorityLoad__ : public Heap, public ZmPolymorph  {
public:
  AuthorityLoad__(
      DBContext *context, Grant grant, Client client, bool requestedPresent,
      String requested, int64_t now, AuthorityFn complete) :
    m_context{context}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{true},
    m_snapshot{true}, m_now{now}, m_complete{ZuMv(complete)}
  {
    m_data.grant = ZuMv(grant);
    m_data.client = ZuMv(client);
    m_data.principalRoleIDs = m_data.grant.roleIDs;
  }

  AuthorityLoad__(
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

  AuthorityLoad__(
      DBContext *context, String issuer, Client client,
      bool requestedPresent, String requested, AuthorityFn complete) :
    m_context{context}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{false},
    m_complete{ZuMv(complete)}
  {
    m_issuer = ZuMv(issuer);
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
    m_context->apps->run(0, [self = ZmRef<AuthorityLoad__>{this}]() {
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
    policies->run(0, [self = ZmRef<AuthorityLoad__>{this}, policies]() {
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
        self = ZmRef<AuthorityLoad__>{this}](ZdbRowRef<Provider> row) mutable {
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
        self = ZmRef<AuthorityLoad__>{this}](ZdbRowRef<Evidence> row) mutable {
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
    mappings->run(0, [self = ZmRef<AuthorityLoad__>{this}, mappings]() {
      using Tuple = RoleMapTable::Tuple;
      mappings->selectRows<0>(ZuFwdTuple(self->m_data.grant.appID,
          self->m_data.grant.authorityProviderID),
          AuthorityScanLimit::RoleMappingsScan, [self](
          ZuUnion<void, Tuple> result, unsigned count) mutable {
        if (result.template is<Tuple>()) {
          if (count > AuthorityScanLimit::RoleMappings) {
            self->m_mappingOverflow = true; return;
          }
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
        auto apps = self->m_context->apps;
        apps->run(0, [self = ZuMv(self)]() mutable { self->issuer_(); });
      });
    });
  }

  void issuer_()
  {
    AppID appID = m_interactive ? m_data.grant.appID : m_data.client.appID;
    if (!appID || (m_interactive ? !m_data.grant.issuer : !m_issuer) ||
	(m_interactive && !m_clientLoad &&
	  m_data.client.appID != m_data.grant.appID)) {
      finish_(AuthorityError::Invalid);
      return;
    }
    m_context->apps->find<0>(0, ZuFwdTuple(appID), [
      self = ZmRef<AuthorityLoad__>{this}
    ](ZdbRowRef<App> app) mutable {
      if (!app || app->data().state != State::Active || app->data().owner) {
	self->finish_(AuthorityError::Invalid);
	return;
      }
	self->m_data.app = app->data();
      if (!self->m_interactive) {
	// Every application has one default same-name confidential client. It
	// authenticates the service itself, so it does not need a resource-role
	// ClientAccess row merely to publish its own catalog.
	if (self->m_data.client.id == app->data().name) {
	  self->m_data.access = ClientAccess{
	    .clientID = self->m_data.client.id, .appID = app->data().id,
	    .state = State::Active};
	  self->m_data.selection.scope = "zum.catalog";
	  self->scopesDone_();
	  return;
	}
	self->clientAccess_();
	return;
      }
      self->principal_();
    });
  }

  void clientAccess_()
  {
    auto access = m_context->clientAccess;
    String clientID = m_data.client.id;
    access->run(0, [self = ZmRef<AuthorityLoad__>{this}, access,
	clientID = ZuMv(clientID)]() mutable {
      using Tuple = ClientAccessTable::Tuple;
      access->selectRows<0>(ZuFwdTuple(ZuMv(clientID)),
        AuthorityScanLimit::ClientAccessScan, [self](
	  ZuUnion<void, Tuple> result, unsigned count) mutable {
	if (result.template is<Tuple>()) {
	  if (count > AuthorityScanLimit::ClientAccess) {
	    self->m_accessOverflow = true; return;
	  }
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
    m_candidateScope = 0;
    unsigned n = m_access.length();
    while (m_accessIndex < n) {
      m_candidate = m_access[m_accessIndex++];
      if (m_candidate.state != State::Active || m_candidate.owner ||
	  !m_candidate.appID || !m_candidate.roleIDs) continue;
      auto apps = m_context->apps;
      apps->run(0, [self = ZmRef<AuthorityLoad__>{this}, apps]() mutable {
	apps->find<0>(0, ZuFwdTuple(self->m_candidate.appID), [
	    self = ZuMv(self)](ZdbRowRef<App> app) mutable {
	  if (!app || !app->data().audience ||
              app->data().state != State::Active || app->data().owner) {
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
    if (m_candidateScope >= m_candidate.roleIDs.length()) {
      ScopeSelection selection;
      int error = selectGrantedScopes(m_data.client, m_candidate,
        m_candidate.roleIDs, m_requestedPresent, m_requested,
        m_candidateScopes, selection);
      if (error) { accessNext_(); return; }
      m_data.access = m_candidate;
      m_data.app = ZuMv(m_candidateApp);
      m_data.scopes = ZuMv(m_candidateScopes);
      m_data.principalRoleIDs = m_candidate.roleIDs;
      scopesDone_();
      return;
    }
    RoleID id = m_candidate.roleIDs[m_candidateScope++];
    auto roles = m_context->roles;
    roles->run(0, [self = ZmRef<AuthorityLoad__>{this}, roles, id]() {
      roles->find<0>(0, ZuFwdTuple(self->m_candidate.appID, id),
        [self = ZuMv(self)](ZdbRowRef<Role> row) mutable {
          if (!row || row->data().state != State::Active || row->data().owner ||
              row->data().tombstone) { self->candidateScope_(); return; }
          self->m_candidateScopes.push(row->data());
          self->candidateScope_();
        });
    });
  }

  void principal_()
  {
    if (m_clientLoad) client_();
    else if (m_interactive) catalog_();
    else scopesDone_();
  }

  void client_()
  {
    String id = m_data.grant.clientID;
    m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad__>{this}
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
      [self = ZmRef<AuthorityLoad__>{this}](
	  int error, App app, Client client, ClientAccess access,
	  RoleVec scopes) mutable {
	if (error) { self->finish_(error); return; }
	self->m_data.app = ZuMv(app);
	self->m_data.client = ZuMv(client);
	self->m_data.access = ZuMv(access);
	self->m_data.scopes = ZuMv(scopes);
	if (self->m_clientLoad) self->scopesDone_();
	else self->user_();
      });
  }

  void user_()
  {
    auto id = m_data.grant.userID;
    m_context->users->find<0>(0, ZuFwdTuple(id), [
      self = ZmRef<AuthorityLoad__>{this}
    ](ZdbRowRef<User> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.user = row->data();
      if (self->m_data.grant.appID &&
	  self->m_data.grant.authoritySource == UserSource::Local)
	self->assignment_();
      else self->userDone_();
    });
  }

  void assignment_()
  {
    auto assignments = m_context->assignments;
    assignments->run(0, [self = ZmRef<AuthorityLoad__>{this}, assignments]() {
      assignments->find<0>(0, ZuFwdTuple(self->m_data.grant.appID,
	  self->m_data.grant.userID), [self = ZuMv(self)](
	    ZdbRowRef<Assignment> row) mutable {
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
    else scopesDone_();
  }

  void cred_()
  {
    Bytes id = m_data.grant.credentialID;
    m_context->creds->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad__>{this}
    ](ZdbRowRef<Cred> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.cred = row->data();
      self->scopesDone_();
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
      error = selectGrantedScopes(m_data.client, m_data.access,
        m_data.grant.requestedRoleIDs,
	m_data.grant.scope, m_requestedPresent, m_requested,
	m_data.scopes, m_data.selection);
      if (!error && m_data.app.audience != m_data.grant.audience)
	error = ScopeError::Unavailable;
    } else {
      if (!clientPrincipal(m_data.client)) {
	finish_(AuthorityError::Invalid);
	return;
      }
	if (m_data.client.id == m_data.app.name &&
	    m_requestedPresent && m_requested == "zum.catalog")
	  error = ScopeError::OK;
	else
	  error = selectGrantedScopes(m_data.client, m_data.access,
	    m_data.access.roleIDs, m_requestedPresent, m_requested,
	    m_data.scopes, m_data.selection);
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
    AppID appID = m_data.app.id;
    m_context->roles->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad__>{this}
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
    if (m_snapshot)
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
    AppID appID = m_data.app.id;
    m_context->actions->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad__>{this}
    ](ZdbRowRef<Action> row) {
      if (row) self->m_data.actionRecords.push(row->data());
      self->m_action = self->m_actionIDs.next(self->m_action);
      self->actions_();
    });
  }

  void actionsDone_()
  {
    if (m_interactive && m_data.user.source == UserSource::External) {
      auto users = m_context->users;
      users->run(0, [self = ZmRef<AuthorityLoad__>{this}, users]() mutable {
        users->find<2>(0, ZuFwdTuple(UserSource::Local, self->m_data.user.name),
          [self = ZuMv(self)](ZdbRowRef<User> row) mutable {
            // Presence wins even for pending/suspended local accounts. Never
            // reinterpret an external session as authentication of that user.
            if (row) self->finish_(AuthorityError::Invalid);
            else self->authorityDone_();
          });
      });
      return;
    }
    authorityDone_();
  }

  void authorityDone_()
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
  RoleVec	m_candidateScopes;
  String	m_requested;
  String	m_issuer;
  IDVec		m_roleIDs;
  ZtBitmap	m_actionIDs;
  unsigned	m_index = 0;
  unsigned	m_accessIndex = 0;
  unsigned	m_candidateScope = 0;
  int		m_action = -1;
  bool		m_requestedPresent = false;
  bool		m_interactive = false;
  bool		m_clientLoad = false;
  bool		m_snapshot = false;
  bool		m_accessOverflow = false;
  bool		m_mappingOverflow = false;
  int64_t	m_now = 0;
  Provider	m_provider;
  Evidence	m_evidence;
  AuthorityFn	m_complete;
};
ZuDerive(AuthorityLoadHeap,
  (ZmHeap<"Zum.zumd.db.AuthorityLoad", AuthorityLoad__<>>));
ZuDerive(AuthorityLoad_, (AuthorityLoad__<AuthorityLoadHeap>));

void loadGrantAuth(
    DBContext *context, Grant grant, Client client, bool requestedPresent,
    String requested, int64_t now, AuthorityFn complete)
{
  ZmRef<AuthorityLoad_> load = new AuthorityLoad_{context, ZuMv(grant),
    ZuMv(client), requestedPresent, ZuMv(requested), now, ZuMv(complete)};
  load->start();
}

void loadGrantAuth(
    DBContext *context, Refresh refresh, Client client, bool requestedPresent,
    String requested, int64_t now, AuthorityFn complete)
{
  Grant grant{
    .authVersion = refresh.authVersion,
    .userVersion = refresh.userVersion,
    .clientVersion = refresh.clientVersion,
    .assignmentVersion = refresh.assignmentVersion,
    .policyVersion = refresh.policyVersion,
    .evidenceVersion = refresh.evidenceVersion,
    .appID = refresh.appID,
    .userID = refresh.userID,
    .issuer = refresh.issuer,
    .clientID = refresh.clientID,
    .audience = refresh.audience,
    .authoritySource = refresh.authoritySource,
    .authTime = refresh.authTime,
    .requestedRoleIDs = refresh.requestedRoleIDs,
    .roleIDs = refresh.roleIDs,
    .actions = refresh.actions,
    .credentialID = refresh.credentialID,
    .scope = refresh.scope,
    .nonce = refresh.nonce,
    .authorityProviderID = refresh.authorityProviderID
  };
  loadGrantAuth(context, ZuMv(grant), ZuMv(client), requestedPresent,
    ZuMv(requested), now, ZuMv(complete));
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

} // namespace Zum
