//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_provider_db.hh>

namespace Zum {

void ScopeSelect::run(DB *db, const ClientAccess &access,
    bool restricted, const IDVec *omittedIDs, ScopeFn complete)
{
  begin(access, restricted, omittedIDs);
  m_db = db;
  m_complete = ZuMv(complete);
  m_db->roles->run(0, [this]() { next_(); });
}

void ScopeSelect::finish_(int error)
{
  auto complete = ZuMv(m_complete);
  complete(error);
}

void ScopeSelect::next_()
{
  int status = step();
  if (status != Lookup) { finish_(status); return; }
  auto consume = [this](ZdbRowRef<Role> row) {
    int error = row ? role(row->data()) : ScopeError::Unavailable;
    if (error) { finish_(error); return; }
    // Cached finds may complete inline. Posting bounds each scheduler turn.
    m_db->roles->run(0, [this]() { next_(); });
  };
  if (byID())
    m_db->roles->find<0>(0, ZuFwdTuple(m_access->appID, id()), ZuMv(consume));
  else
    m_db->roles->find<1>(0, ZuFwdTuple(m_access->appID, String{name()}),
      ZuMv(consume));
}

class ClientAccessLoad : public ZmNHeap<ClientAccessLoad>, public ZmPolymorph  {
friend inline ZuStringT<"Zum.zumd.db.ClientAccessLoad"> ZmNHeapID(ClientAccessLoad *);

public:
  ClientAccessLoad(DB *db, Client client, ClientAccessLoadFn complete) :
    m_db{db}, m_client{ZuMv(client)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_client.appID || m_client.state != State::Active || m_client.owner) {
      finish_(AuthorityError::Invalid); return;
    }
    auto apps = m_db->apps;
    apps->run(0, [self = ZmRef<ClientAccessLoad>{this}, apps]() {
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
    if (error) { complete(error, App{}, Client{}, ClientAccess{}); return; }
    complete(0, ZuMv(m_app), ZuMv(m_client), ZuMv(m_access));
  }

  void access_()
  {
    auto access = m_db->clientAccess;
    access->run(0, [self = ZmRef<ClientAccessLoad>{this}, access]() {
      access->find<0>(0, ZuFwdTuple(self->m_client.id, self->m_client.appID),
        [self = ZuMv(self)](ZdbRowRef<ClientAccess> row) mutable {
          if (!row) { self->finish_(0); return; }
          if (row->data().state != State::Active || row->data().owner) {
            self->finish_(AuthorityError::Invalid); return;
          }
          self->m_access = row->data(); self->finish_(0);
        });
    });
  }

  DB *m_db;
  App m_app;
  Client m_client;
  ClientAccess m_access;
  ClientAccessLoadFn m_complete;
};

void loadClientAccess(DB *db, Client client, ClientAccessLoadFn complete)
{
  ZmRef<ClientAccessLoad> load =
    new ClientAccessLoad{db, ZuMv(client), ZuMv(complete)};
  load->start();
}

class AuthorityLoad : public ZmNHeap<AuthorityLoad>, public ZmPolymorph  {
friend inline ZuStringT<"Zum.zumd.db.AuthorityLoad"> ZmNHeapID(AuthorityLoad *);

public:
  AuthorityLoad(
      DB *db, Grant grant, Client client, bool requestedPresent,
      String requested, int64_t now, AuthorityFn complete) :
    m_db{db}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{true},
    m_snapshot{true}, m_now{now}, m_complete{ZuMv(complete)}
  {
    m_data.grant = ZuMv(grant);
    m_data.client = ZuMv(client);
    m_data.principalRoleIDs = m_data.grant.roleIDs;
  }

  AuthorityLoad(
      DB *db, Grant grant, User user, Cred cred,
      IDVec principalRoleIDs,
      AuthorityFn complete) :
    m_db{db}, m_interactive{true}, m_clientLoad{true},
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

  AuthorityLoad(
      DB *db, String issuer, Client client,
      bool requestedPresent, String requested, AuthorityFn complete) :
    m_db{db}, m_requested{ZuMv(requested)},
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
      delegated_0();
      return;
    }
    m_db->apps->run(0, [self = ZmRef<AuthorityLoad>{this}]() {
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

  void delegated_0()
  {
    if (!m_now || !m_data.grant.appID || !m_data.grant.userID ||
        !m_data.grant.authorityProviderID || !m_data.grant.policyVersion ||
        !m_data.grant.evidenceVersion) {
      finish_(AuthorityError::Invalid);
      return;
    }
    auto policies = m_db->authPolicies;
    policies->run(0, [self = ZmRef<AuthorityLoad>{this}, policies]() {
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
        self->delegated_1();
      });
    });
  }

  void delegated_1()
  {
    auto providers = m_db->providers;
    providers->find<0>(0, ZuFwdTuple(m_data.grant.authorityProviderID), [
        self = ZmRef<AuthorityLoad>{this}](ZdbRowRef<Provider> row) mutable {
      if (!row || row->data().state != State::Active || row->data().owner) {
        self->finish_(AuthorityError::Invalid);
        return;
      }
      self->m_provider = row->data();
      self->delegated_2();
    });
  }

  void delegated_2()
  {
    auto evidence = m_db->evidence;
    evidence->find<0>(0, ZuFwdTuple(m_data.grant.appID,
        m_data.grant.userID, m_data.grant.authorityProviderID), [
        self = ZmRef<AuthorityLoad>{this}](ZdbRowRef<Evidence> row) mutable {
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
      self->delegated_3();
    });
  }

  void delegated_3()
  {
    auto mappings = m_db->roleMaps;
    mappings->run(0, [self = ZmRef<AuthorityLoad>{this}, mappings]() {
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
                if (!self->m_loadedIDs.find(map.roleID)) {
		  self->m_loadedIDs.add(map.roleID);
		  self->m_data.principalRoleIDs.push(map.roleID);
		}
                break;
              }
          });
          return;
        }
        if (self->m_mappingOverflow) {
          self->finish_(AuthorityError::Invalid);
          return;
        }
        auto apps = self->m_db->apps;
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
    m_db->apps->find<0>(0, ZuFwdTuple(appID), [
      self = ZmRef<AuthorityLoad>{this}
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
	  self->select_0();
	  return;
	}
	self->clientAccess_0();
	return;
      }
      self->principal_();
    });
  }

  void clientAccess_0()
  {
    m_select.init(m_data.client, m_requestedPresent, m_requested);
    m_requested.null();
    auto access = m_db->clientAccess;
    String clientID = m_data.client.id;
    access->run(0, [self = ZmRef<AuthorityLoad>{this}, access,
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
	// The store has finished filling the candidate batch. Transfer it to
	// the row owner before selecting applications or mutating authority.
	self->m_db->apps->run(0, [self]() {
	  if (self->m_accessOverflow) {
	    self->finish_(AuthorityError::Invalid); return;
	  }
	  self->clientAccess_1();
	});
      });
    });
  }

  void clientAccess_1()
  {
    unsigned n = m_access.length();
    while (m_accessIndex < n) {
      m_candidate = m_access[m_accessIndex++];
      if (m_candidate.state != State::Active || m_candidate.owner ||
	  !m_candidate.appID || !m_candidate.roleIDs) continue;
      auto apps = m_db->apps;
      apps->run(0, [self = ZmRef<AuthorityLoad>{this}, apps]() mutable {
	apps->find<0>(0, ZuFwdTuple(self->m_candidate.appID), [
	    self = ZuMv(self)](ZdbRowRef<App> app) mutable {
	  if (!app || !app->data().audience ||
              app->data().state != State::Active || app->data().owner) {
	    self->clientAccess_1();
	    return;
	  }
	  self->m_candidateApp = app->data();
	  self->clientAccess_2();
	});
      });
      return;
    }
    finish_(ScopeError::Unavailable);
  }

  void clientAccess_2()
  {
    m_select.run(m_db, m_candidate, true,
      m_requestedPresent ? nullptr : &m_candidate.roleIDs,
      [self = ZmRef<AuthorityLoad>{this}](int error) mutable {
	if (error) { self->clientAccess_1(); return; }
	self->m_data.selection = self->m_select.take();
	self->m_data.access = ZuMv(self->m_candidate);
	self->m_data.app = ZuMv(self->m_candidateApp);
	self->m_data.principalRoleIDs = self->m_data.access.roleIDs;
	if (!clientPrincipal(self->m_data.client)) {
	  self->finish_(AuthorityError::Invalid); return;
	}
	self->select_1();
      });
  }

  void principal_()
  {
    if (m_clientLoad) client_();
    else if (m_interactive) catalog_();
    else select_0();
  }

  void client_()
  {
    String id = m_data.grant.clientID;
    m_db->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad>{this}
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
    loadClientAccess(m_db, ZuMv(m_data.client),
      [self = ZmRef<AuthorityLoad>{this}](
	  int error, App app, Client client, ClientAccess access) mutable {
	if (error) { self->finish_(error); return; }
	self->m_data.app = ZuMv(app);
	self->m_data.client = ZuMv(client);
	self->m_data.access = ZuMv(access);
	if (self->m_clientLoad) self->select_0();
	else self->user_0();
      });
  }

  void user_0()
  {
    auto id = m_data.grant.userID;
    m_db->users->find<0>(0, ZuFwdTuple(id), [
      self = ZmRef<AuthorityLoad>{this}
    ](ZdbRowRef<User> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.user = row->data();
      if (self->m_data.grant.appID &&
	  self->m_data.grant.authoritySource == UserSource::Local)
	self->assignment_();
      else self->user_1();
    });
  }

  void assignment_()
  {
    auto assignments = m_db->assignments;
    assignments->run(0, [self = ZmRef<AuthorityLoad>{this}, assignments]() {
      assignments->find<0>(0, ZuFwdTuple(self->m_data.grant.appID,
	  self->m_data.grant.userID), [self = ZuMv(self)](
	    ZdbRowRef<Assignment> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(AuthorityError::Invalid);
	  return;
	}
	self->m_data.principalRoleIDs = row->data().roleIDs;
	self->user_1();
      });
    });
  }

  void user_1()
  {
    if (m_data.grant.credentialID) cred_();
    else select_0();
  }

  void cred_()
  {
    Bytes id = m_data.grant.credentialID;
    m_db->creds->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad>{this}
    ](ZdbRowRef<Cred> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.cred = row->data();
      self->select_0();
    });
  }

  void select_0()
  {
    if (m_interactive) {
      if (!interactivePrincipal(m_data.grant, m_data.user,
	  m_data.cred, m_data.client)) {
	finish_(AuthorityError::Invalid); return;
      }
      m_select.init(m_data.client, m_requestedPresent, m_requested,
	&m_data.grant.requestedRoleIDs, &m_data.grant.scope);
      m_requested.null();
      m_select.run(m_db, m_data.access, true, nullptr,
	[self = ZmRef<AuthorityLoad>{this}](int error) mutable {
	  if (!error && self->m_data.app.audience != self->m_data.grant.audience)
	    error = ScopeError::Unavailable;
	  if (error) { self->finish_(error); return; }
	  self->m_data.selection = self->m_select.take();
	  self->select_1();
	});
      return;
    }
    if (!clientPrincipal(m_data.client)) {
      finish_(AuthorityError::Invalid); return;
    }
    if (m_data.client.id == m_data.app.name &&
	m_requestedPresent && m_requested == "zum.catalog") {
      select_1(); return;
    }
    m_select.init(m_data.client, m_requestedPresent, m_requested);
    m_requested.null();
    m_select.run(m_db, m_data.access, true,
      m_requestedPresent ? nullptr : &m_data.access.roleIDs,
      [self = ZmRef<AuthorityLoad>{this}](int error) mutable {
	if (error) { self->finish_(error); return; }
	self->m_data.selection = self->m_select.take();
	self->select_1();
      });
  }

  void select_1()
  {
    m_loadedIDs.clean();
    for (auto id: m_data.principalRoleIDs)
      if (m_select.selected(id) && !m_loadedIDs.find(id)) {
	m_loadedIDs.add(id);
	m_roleIDs.push(id);
      }
    m_index = 0;
    roles_0();
  }

  void roles_0()
  {
    if (m_index >= m_roleIDs.length()) { roles_1(); return; }
    auto id = m_roleIDs[m_index];
    AppID appID = m_data.app.id;
    m_db->roles->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad>{this}
    ](ZdbRowRef<Role> row) {
      if (row && row->data().state == State::Active && !row->data().owner)
	self->m_actionIDs |= row->data().actions;
      ++self->m_index;
      self->m_db->roles->run(0, [self]() { self->roles_0(); });
    });
  }

  void roles_1()
  {
    if (m_snapshot)
      m_actionIDs = intersectActions(ZuMv(m_actionIDs), m_data.grant.actions);
    m_data.actions.length(m_data.app.nextActionID);
    m_action = m_actionIDs.first();
    actions_0();
  }

  void actions_0()
  {
    if (m_action < 0 || unsigned(m_action) >= m_data.app.nextActionID) {
      actions_1();
      return;
    }
    auto id = ActionID(m_action);
    AppID appID = m_data.app.id;
    m_db->actions->find<0>(0, ZuFwdTuple(appID, id), [
      self = ZmRef<AuthorityLoad>{this}
    ](ZdbRowRef<Action> row) {
      if (row) {
	if (row->data().state == State::Active && !row->data().owner)
	  self->m_data.actions.set(row->data().id);
	self->m_data.actionRecords.push(row->data());
      }
      self->m_action = self->m_actionIDs.next(self->m_action);
      self->m_db->actions->run(0, [self]() { self->actions_0(); });
    });
  }

  void actions_1()
  {
    if (m_interactive && m_data.user.source == UserSource::External) {
      auto users = m_db->users;
      users->run(0, [self = ZmRef<AuthorityLoad>{this}, users]() mutable {
        users->find<2>(0, ZuFwdTuple(UserSource::Local, self->m_data.user.name),
          [self = ZuMv(self)](ZdbRowRef<User> row) mutable {
            // Presence wins even for pending/suspended local accounts. Never
            // reinterpret an external session as authentication of that user.
            if (row) self->finish_(AuthorityError::Invalid);
            else self->actions_2();
          });
      });
      return;
    }
    actions_2();
  }

  void actions_2()
  {
    finish_(ScopeError::OK);
  }

  DB	*m_db = nullptr;
  AuthorityData	m_data;
  ScopeSelect	m_select;
  using LoadedIDs = ZmRBTree<RoleID,
    ZmRBTreeUnique<true, ZmRBTreeHeapID<"Zum.Authority.LoadedIDs">>>;
  LoadedIDs	m_loadedIDs;
  ZtArray<ClientAccess, VecHeap> m_access;
  ClientAccess	m_candidate;
  App		m_candidateApp;
  String	m_requested;
  String	m_issuer;
  IDVec		m_roleIDs;
  ZtBitmap	m_actionIDs;
  unsigned	m_index = 0;
  unsigned	m_accessIndex = 0;
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

void loadGrantAuth(
    DB *db, Grant grant, Client client, bool requestedPresent,
    String requested, int64_t now, AuthorityFn complete)
{
  ZmRef<AuthorityLoad> load = new AuthorityLoad{db, ZuMv(grant),
    ZuMv(client), requestedPresent, ZuMv(requested), now, ZuMv(complete)};
  load->start();
}

void loadGrantAuth(
    DB *db, Refresh refresh, Client client, bool requestedPresent,
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
  loadGrantAuth(db, ZuMv(grant), ZuMv(client), requestedPresent,
    ZuMv(requested), now, ZuMv(complete));
}

void loadClientAuth(
    DB *db, String issuer, Client client,
    bool requestedPresent, String requested, AuthorityFn complete)
{
  ZmRef<AuthorityLoad> load = new AuthorityLoad{db, ZuMv(issuer),
    ZuMv(client), requestedPresent, ZuMv(requested), ZuMv(complete)};
  load->start();
}

void loadUserAuth(
    DB *db, Grant grant, User user, Cred cred,
    IDVec principalRoleIDs,
    AuthorityFn complete)
{
  ZmRef<AuthorityLoad> load = new AuthorityLoad{db, ZuMv(grant),
    ZuMv(user), ZuMv(cred), ZuMv(principalRoleIDs), ZuMv(complete)};
  load->start();
}

} // namespace Zum
