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

class AuthorityLoad_ : public ZumPolymorph {
public:
  AuthorityLoad_(
      DBContext *context, Grant grant, Client client, bool requestedPresent,
      String requested, AuthorityFn complete) :
    m_context{context}, m_requested{ZuMv(requested)},
    m_requestedPresent{requestedPresent}, m_interactive{true},
    m_ceiling{true}, m_complete{ZuMv(complete)}
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
    m_data.client = ZuMv(client);
    m_data.principalRoleIDs = m_data.client.roleIDs;
  }

  void start()
  {
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

  void issuer_()
  {
    String id = m_interactive ? m_data.grant.issuer : m_data.issuer.id;
    m_context->issuers->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Issuer> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.issuer = row->data();
      if (self->m_clientLoad) self->client_();
      else if (self->m_interactive) self->user_();
      else self->scopes_();
    });
  }

  void client_()
  {
    String id = m_data.grant.clientID;
    m_context->clients->find<0>(0, ZuFwdTuple(ZuMv(id)), [
      self = ZmRef<AuthorityLoad_>{this}
    ](ZdbRowRef<Client> row) {
      if (!row) { self->finish_(AuthorityError::Invalid); return; }
      self->m_data.client = row->data();
      self->scopes_();
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
      if (self->m_data.grant.credentialID) self->cred_();
      else self->scopes_();
    });
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
    auto &ids = scopeIDs_();
    if (m_index >= ids.length()) { scopesDone_(); return; }
    auto id = ids[m_index];
    m_context->scopes->find<0>(0, ZuFwdTuple(id), [
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
	m_requestedPresent, m_requested, m_data.scopes, m_data.selection);
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
    m_context->roles->find<0>(0, ZuFwdTuple(id), [
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
    if (m_ceiling) m_actionIDs &= m_data.grant.actions;
    m_action = m_actionIDs.first();
    actions_();
  }

  void actions_()
  {
    if (m_action < 0 || unsigned(m_action) >= m_data.issuer.nextActionID) {
      actionsDone_();
      return;
    }
    auto id = ActionID(m_action);
    m_context->actions->find<0>(0, ZuFwdTuple(id), [
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
    m_data.actions = effectiveActions(m_data.issuer.nextActionID,
      principal, m_data.selection.roleIDs,
      m_data.roles, m_data.actionRecords);
    finish_(ScopeError::OK);
  }

  DBContext	*m_context = nullptr;
  AuthorityData	m_data;
  String	m_requested;
  IDVec		m_roleIDs;
  ZtBitmap	m_actionIDs;
  unsigned	m_index = 0;
  int		m_action = -1;
  bool		m_requestedPresent = false;
  bool		m_interactive = false;
  bool		m_clientLoad = false;
  bool		m_ceiling = false;
  AuthorityFn	m_complete;
};

void loadGrantAuth(
    DBContext *context, Grant grant, Client client, bool requestedPresent,
    String requested, AuthorityFn complete)
{
  ZmRef<AuthorityLoad_> load = new AuthorityLoad_{context, ZuMv(grant),
    ZuMv(client), requestedPresent, ZuMv(requested), ZuMv(complete)};
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
    IDVec scopeIDs, ZtBitmap actions, uint64_t authVersion,
    int64_t now, int64_t expires, CodeFamily &family, String &refreshToken)
{
  OpaqueToken refresh;
  if (!opaqueIssue(rng, refresh)) return false;
  CodeFamily next;
  next.codeID = code.id;
  next.codeDigest = codeDigest;
  next.familyID = ZuMv(refresh.id);
  next.issuer = code.issuer;
  next.userID = code.userID;
  next.clientID = code.clientID;
  next.credentialID = code.credentialID;
  next.audience = code.audience;
  next.scopeIDs = ZuMv(scopeIDs);
  next.roleIDs = code.roleIDs;
  next.actions = ZuMv(actions);
  next.digest = ZuMv(refresh.digest);
  next.authVersion = authVersion;
  next.userVersion = code.userVersion;
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
  context->users = db->initTable<User>("zum.user");
  context->creds = db->initTable<Cred>("zum.cred");
  context->actions = db->initTable<Action>("zum.action");
  context->roles = db->initTable<Role>("zum.role");
  context->scopes = db->initTable<Scope>("zum.scope");
  context->clients = db->initTable<Client>("zum.client");
  context->grants = db->initTable<Grant>("zum.grant");
  context->signKeys = db->initTable<SignKey>("zum.signKey");
  context->audits = db->initTable<Audit>("zum.audit");
  db->sagas(context);
  return context;
}

} // namespace Zum
