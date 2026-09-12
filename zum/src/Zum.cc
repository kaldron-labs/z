//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zum.hh>
#include <zlib/ZumMgmt.hh>

#include <zlib/ZuUTF.hh>

#include <zlib/ZtlsSec.hh>

namespace Zum {

bool loginNormalize(String &name)
{
  enum { Max = 256 }; // bounded login index key and OIDC login_hint
  unsigned begin = 0, end = name.length();
  while (begin < end && (name[begin] == ' ' || name[begin] == '\t')) ++begin;
  while (end > begin && (name[end - 1] == ' ' || name[end - 1] == '\t')) --end;
  if (begin) name.splice(0, begin);
  name.length(end - begin);
  if (!name || name.length() > Max) return false;
  for (unsigned i = 0, n = name.length(); i < n;) {
    uint32_t c;
    unsigned width = ZuUTF8::in(
      reinterpret_cast<const uint8_t *>(name.data() + i), n - i, c);
    if (!width || c == 0x7f || c < 0x20) return false;
    for (unsigned j = 1; j < width; ++j)
      if ((uint8_t(name[i + j]) & 0xc0) != 0x80) return false;
    if ((width == 2 && c < 0x80) || (width == 3 && c < 0x800) ||
	(width == 4 && c < 0x10000) || c > 0x10ffff ||
	(c >= 0xd800 && c <= 0xdfff)) return false;
    if (c >= 'A' && c <= 'Z') name[i] = char(c + ('a' - 'A'));
    i += width;
  }
  return true;
}

ZtEnumImplNS(State);
ZtEnumImplNS(ClientType);
ZtEnumImplNS(ClientAuthMethod);
ZtEnumImplNS(GrantKind);
ZtEnumImplNS(GrantPurpose);
ZtEnumImplNS(AuditOutcome);
ZtEnumImplNS(AuditEvent);
ZtEnumImplNS(RefreshMatch);
ZtEnumImplNS(RefreshRotate);
ZtEnumImplNS(BootstrapPhase);
ZtEnumImplNS(UserSource);
ZtEnumImplNS(Origin);
ZtEnumImplNS(ActorKind);
ZtEnumImplNS(ClaimSource);
ZtEnumImplNS(EligibilityMode);
ZtEnumImplNS(ConsentPolicy);
ZtEnumImplNS(RequestStatus);

ZtBitmap intersectActions(ZtBitmap actions, const ZtBitmap &limit)
{
  actions &= limit;
  if (actions.length() > limit.length()) actions.length(limit.length());
  return actions;
}

static void addRoles(
    ZtBitmap &actions, const IDVec &ids, ZuSpan<const Role> roles)
{
  for (auto id: ids)
    for (auto &role: roles)
      if (role.id == id) {
	if (role.state == State::Active && !role.owner) actions |= role.actions;
	break;
      }
}

ZtBitmap effectiveActions(
    unsigned actionCount, const IDVec &principalRoleIDs,
    const IDVec &scopeRoleIDs, ZuSpan<const Role> roles,
    ZuSpan<const Action> actionRecords)
{
  if (!actionCount) return {};
  ZtBitmap principal{actionCount};
  ZtBitmap delegated{actionCount};
  addRoles(principal, principalRoleIDs, roles);
  addRoles(delegated, scopeRoleIDs, roles);
  principal = intersectActions(ZuMv(principal), delegated);

  ZtBitmap enabled{actionCount};
  for (auto &action: actionRecords)
    if (action.state == State::Active && !action.owner &&
	action.id < actionCount)
      enabled.set(action.id);
  principal = intersectActions(ZuMv(principal), enabled);
  return principal;
}

static const Role *findRole(
    AppID appID, RoleID id, ZuSpan<const Role> roles)
{
  for (auto &role: roles)
    if (role.appID == appID && role.id == id && !role.tombstone)
      return &role;
  return nullptr;
}

static bool roleRefsValid(
    AppID appID, const IDVec &ids, ZuSpan<const Role> roles)
{
  for (auto id: ids)
    if (!findRole(appID, id, roles)) return false;
  return true;
}

bool membershipValid(
    const App &app, const Membership &membership, ZuSpan<const Role> roles)
{
  return app.id && membership.appID == app.id && membership.userID &&
    !membership.owner && roleRefsValid(app.id, membership.roleIDs, roles);
}

bool scopeValid(
    const App &app, const Scope &scope, const Audience &audience,
    ZuSpan<const Role> roles)
{
  return app.id && scope.appID == app.id && audience.appID == app.id &&
    scope.audienceID == audience.id && !scope.owner && !audience.owner &&
    roleRefsValid(app.id, scope.roleIDs, roles);
}

ZtBitmap appEffectiveActions(
    const App &app, const Membership &membership, const Scope &scope,
    const Audience &audience, ZuSpan<const Role> roles,
    ZuSpan<const Action> actions)
{
  if (app.state != State::Active || app.owner ||
      membership.state != State::Active ||
      scope.state != State::Active || audience.state != State::Active ||
      !membershipValid(app, membership, roles) ||
      !scopeValid(app, scope, audience, roles)) return {};

  ZtBitmap principal{app.nextActionID};
  ZtBitmap delegated{app.nextActionID};
  for (auto id: membership.roleIDs) {
    auto role = findRole(app.id, id, roles);
    if (role->state == State::Active && !role->owner)
      principal |= role->actions;
  }
  for (auto id: scope.roleIDs) {
    auto role = findRole(app.id, id, roles);
    if (role->state == State::Active && !role->owner)
      delegated |= role->actions;
  }
  principal = intersectActions(ZuMv(principal), delegated);

  ZtBitmap enabled{app.nextActionID};
  for (auto &action: actions)
    if (action.appID == app.id && action.id < app.nextActionID &&
        action.state == State::Active && !action.owner && !action.tombstone)
      enabled.set(action.id);
  principal = intersectActions(ZuMv(principal), enabled);
  return principal;
}

bool actionAlloc(App &app, ActionID &id)
{
  if (!app.id || app.nextActionID == UINT32_MAX) return false;
  id = app.nextActionID++;
  ++app.authVersion;
  ++app.version;
  return true;
}

static bool hasID(const IDVec &ids, uint64_t id)
{
  for (auto value: ids) if (value == id) return true;
  return false;
}

static bool hasString(const StringVec &strings, ZuCSpan string)
{
  for (auto &value: strings) if (value == string) return true;
  return false;
}

static bool scopeName(ZuCSpan scopes, ZuCSpan name)
{
  unsigned offset = 0;
  unsigned n = scopes.length();
  while (offset < n) {
    while (offset < n && scopes[offset] == ' ') ++offset;
    unsigned end = offset;
    while (end < n && scopes[end] != ' ') ++end;
    if (ZuCSpan{scopes.data() + offset, end - offset} == name) return true;
    offset = end;
  }
  return false;
}

static void addScopeName(ScopeSelection &selection, ZuCSpan name)
{
  if (scopeName(selection.scope, name)) return;
  if (selection.scope) selection.scope << ' ';
  selection.scope << name;
}

static bool addIdentityScope(
    const Client &client, ScopeSelection &selection, ZuCSpan name)
{
  if (!hasString(client.identityScopes, name)) return false;
  addScopeName(selection, name);
  selection.identity = true;
  if (!selection.appID) selection.appID = client.appID;
  return true;
}

static int addScope(
    const Client &client, const ClientAccess &access,
    ScopeSelection &selection, const ScopeAuth &resolved)
{
  const auto &scope = resolved.scope;
  if (scope.appID != access.appID || access.clientID != client.id)
    return ScopeError::Unavailable;
  if (!hasID(access.audienceIDs, scope.audienceID))
    return ScopeError::Audience;
  if (selection.audience && selection.audience != resolved.audience)
    return ScopeError::Audience;
  if (!hasID(selection.scopeIDs, scope.id)) {
    addScopeName(selection, scope.name);
    selection.scopeIDs.push(scope.id);
    for (auto roleID: scope.roleIDs)
      if (!hasID(selection.roleIDs, roleID)) selection.roleIDs.push(roleID);
  }
  selection.audience = resolved.audience;
  selection.appID = scope.appID;
  selection.audienceID = scope.audienceID;
  return ScopeError::OK;
}

static int selectScopes_(
    const Client &client, const ClientAccess &access,
    const IDVec *grantedScopeIDs,
    ZuCSpan granted,
    ZuCSpan requested, ZuSpan<const ScopeAuth> scopes,
    ScopeSelection &selection)
{
  if (!requested) return ScopeError::Malformed;
  ScopeSelection next;
  unsigned length = requested.length();
  unsigned offset = 0;
  while (offset < length) {
    while (offset < length && requested[offset] == ' ') ++offset;
    if (offset == length) break;
    unsigned end = offset;
    while (end < length && requested[end] != ' ') ++end;
    ZuCSpan name{requested.data() + offset, end - offset};
    if (grantedScopeIDs) {
      if (granted) {
        if (!scopeName(granted, name)) return ScopeError::Unavailable;
      } else if (hasString(client.identityScopes, name)) {
        return ScopeError::Unavailable;
      }
    }
    if (addIdentityScope(client, next, name)) {
      if (end == length) break;
      offset = end + 1;
      continue;
    }
    const ScopeAuth *selected = nullptr;
    for (auto &resolved: scopes) {
      const auto &scope = resolved.scope;
      if (scope.state != State::Active || scope.owner || scope.name != name ||
	  !hasID(access.scopeIDs, scope.id) ||
	  (grantedScopeIDs && !hasID(*grantedScopeIDs, scope.id))) continue;
	  if (selected && selected->audience != resolved.audience)
	return ScopeError::Audience;
      selected = &resolved;
    }
    if (!selected) return ScopeError::Unavailable;
    if (int error = addScope(client, access, next, *selected)) return error;
    if (end == length) break;
    offset = end + 1;
  }
  if (!next.scope) return ScopeError::Malformed;
  if (!next.audience) next.audience = client.id;
  selection = ZuMv(next);
  return ScopeError::OK;
}

int selectScopes(
    const Client &client, const ClientAccess &access, ZuCSpan requested,
    ZuSpan<const ScopeAuth> scopes, ScopeSelection &selection)
{
  return selectScopes_(client, access, nullptr, {}, requested,
    scopes, selection);
}

int selectGrantedScopes(
    const Client &client, const ClientAccess &access,
    const IDVec &grantedScopeIDs,
    bool requestedPresent, ZuCSpan requested,
    ZuSpan<const ScopeAuth> scopes, ScopeSelection &selection)
{
  if (requestedPresent)
    return selectScopes_(
      client, access, &grantedScopeIDs, {}, requested, scopes, selection);
  ScopeSelection next;
  for (auto scopeID: grantedScopeIDs) {
    const ScopeAuth *selected = nullptr;
    for (auto &resolved: scopes) {
      const auto &scope = resolved.scope;
      if (scope.id == scopeID && scope.state == State::Active && !scope.owner &&
	  hasID(access.scopeIDs, scope.id)) {
	selected = &resolved;
	break;
      }
    }
    if (!selected) return ScopeError::Unavailable;
    if (int error = addScope(client, access, next, *selected)) return error;
  }
  if (!next.scopeIDs) return ScopeError::Unavailable;
  selection = ZuMv(next);
  return ScopeError::OK;
}

int selectGrantedScopes(
    const Client &client, const ClientAccess &access,
    const IDVec &grantedScopeIDs, ZuCSpan granted,
    bool requestedPresent, ZuCSpan requested,
    ZuSpan<const ScopeAuth> scopes, ScopeSelection &selection)
{
  return selectScopes_(client, access, &grantedScopeIDs, granted,
    requestedPresent ? requested : granted, scopes, selection);
}

int interactiveAuthority(
    const Grant &grant, const User &user, const Cred &cred,
    const Client &client, bool requestedPresent, ZuCSpan requested,
    unsigned actionCount, const ClientAccess &access,
    ZuSpan<const ScopeAuth> scopes,
    ZuSpan<const Role> roles, ZuSpan<const Action> actionRecords,
    ScopeSelection &selection, ZtBitmap &actions)
{
  if (!interactivePrincipal(grant, user, cred, client))
    return AuthorityError::Invalid;

  ScopeSelection next;
  int error = selectGrantedScopes(client, access,
    grant.scopeIDs, grant.scope,
    requestedPresent, requested, scopes, next);
  if (error) return error;
  if (next.audience != grant.audience) return ScopeError::Audience;

  ZtBitmap nextActions = effectiveActions(actionCount,
    grant.roleIDs, next.roleIDs, roles, actionRecords);
  nextActions = intersectActions(ZuMv(nextActions), grant.actions);
  selection = ZuMv(next);
  actions = ZuMv(nextActions);
  return ScopeError::OK;
}

bool interactivePrincipal(
    const Grant &grant, const User &user, const Cred &cred,
    const Client &client)
{
  bool credential = !grant.credentialID ||
    (cred.state == State::Active && !cred.owner && cred.userID == user.id &&
     cred.id == grant.credentialID && cred.publicKey &&
     cred.userVersion == user.authVersion);
  return user.state == State::Active && !user.owner && user.id && user.handle &&
      credential && grant.userID == user.id &&
      !grant.owner && grant.userVersion == user.authVersion &&
      client.state == State::Active && !client.owner &&
      client.id == grant.clientID &&
      (client.type == ClientType::Browser ||
       client.type == ClientType::Native ||
       client.type == ClientType::Confidential) &&
      (client.grants & ClientGrant::AuthorizationCode);
}

bool clientPrincipal(const Client &client)
{
  return client.state == State::Active && !client.owner &&
    client.type == ClientType::Confidential &&
    (client.grants & ClientGrant::ClientCredentials);
}

int clientAuthority(
    const Client &client, const ClientAccess &access, ZuCSpan requested,
    unsigned actionCount, ZuSpan<const ScopeAuth> scopes,
    ZuSpan<const Role> roles,
    ZuSpan<const Action> actionRecords, ScopeSelection &selection,
    ZtBitmap &actions)
{
  if (!clientPrincipal(client)) return AuthorityError::Invalid;

  ScopeSelection next;
  int error = selectScopes(client, access, requested, scopes, next);
  if (error) return error;
  if (next.identity) return ScopeError::Unavailable;
  ZtBitmap nextActions = effectiveActions(actionCount,
    access.roleIDs, next.roleIDs, roles, actionRecords);
  selection = ZuMv(next);
  actions = ZuMv(nextActions);
  return ScopeError::OK;
}

RefreshMatch::T refreshMatch(const Grant &grant, ZuBSpan digest)
{
  if (Ztls::ctEqual(grant.digest, digest)) return RefreshMatch::Current;
  for (auto &spent: grant.spent)
    if (Ztls::ctEqual(spent, digest)) return RefreshMatch::Spent;
  return RefreshMatch::Unknown;
}

RefreshRotate::T refreshRotate(
    Grant &grant, ZuBSpan presentedDigest, Bytes nextDigest,
    int64_t now, unsigned generationLimit, unsigned spentLimit)
{
  return refreshRotate(grant, refreshMatch(grant, presentedDigest),
    presentedDigest, ZuMv(nextDigest), now, generationLimit, spentLimit);
}

RefreshRotate::T refreshRotate(
    Grant &grant, RefreshMatch::T match, ZuBSpan presentedDigest,
    Bytes nextDigest, int64_t now, unsigned generationLimit,
    unsigned spentLimit)
{
  if (grant.kind != GrantKind::Refresh || grant.state != State::Active ||
      grant.owner ||
      now <= 0 || grant.expires <= now || !presentedDigest)
    return RefreshRotate::Invalid;
  switch (match) {
    case RefreshMatch::Unknown:
      return RefreshRotate::Unknown;
    case RefreshMatch::Spent:
      grant.state = State::Revoked;
      return RefreshRotate::Reused;
    default:
      break;
  }
  if (grant.generation >= generationLimit ||
      grant.spent.length() >= spentLimit) return RefreshRotate::Exhausted;
  if (!nextDigest) return RefreshRotate::Invalid;
  grant.spent.push(ZuMv(grant.digest));
  grant.digest = ZuMv(nextDigest);
  ++grant.generation;
  return RefreshRotate::Rotated;
}

} // namespace Zum
