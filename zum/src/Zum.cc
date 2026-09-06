//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/Zum.hh>

#include <zlib/ZtlsSec.hh>

namespace Zum {

ZtEnumImplNS(State);
ZtEnumImplNS(ClientType);
ZtEnumImplNS(GrantKind);
ZtEnumImplNS(GrantPurpose);
ZtEnumImplNS(AuditOutcome);
ZtEnumImplNS(AuditEvent);
ZtEnumImplNS(RefreshMatch);
ZtEnumImplNS(RefreshRotate);

ZtBitmap intersectActions(ZtBitmap actions, const ZtBitmap &limit)
{
  actions &= limit;
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
  principal &= delegated;

  ZtBitmap enabled{actionCount};
  for (auto &action: actionRecords)
    if (action.state == State::Active && !action.owner &&
	action.id < actionCount)
      enabled.set(action.id);
  principal &= enabled;
  return principal;
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

static int addScope(
    const Client &client, ScopeSelection &selection, const Scope &scope)
{
  if (!hasString(client.audiences, scope.audience))
    return ScopeError::Audience;
  if (selection.audience && selection.audience != scope.audience)
    return ScopeError::Audience;
  if (!hasID(selection.scopeIDs, scope.id)) {
    if (selection.scope) selection.scope << ' ';
    selection.scope << scope.name;
    selection.scopeIDs.push(scope.id);
    for (auto roleID: scope.roleIDs)
      if (!hasID(selection.roleIDs, roleID)) selection.roleIDs.push(roleID);
  }
  selection.audience = scope.audience;
  return ScopeError::OK;
}

static int selectScopes_(
    const Client &client, const IDVec *grantedScopeIDs,
    ZuCSpan requested, ZuSpan<const Scope> scopes, ScopeSelection &selection)
{
  if (!requested) return ScopeError::Malformed;
  ScopeSelection next;
  unsigned length = requested.length();
  unsigned offset = 0;
  while (offset < length) {
    unsigned end = offset;
    while (end < length && requested[end] != ' ') {
      uint8_t c = requested[end];
      if (c < 0x21 || c == 0x22 || c == 0x5c || c > 0x7e)
	return ScopeError::Malformed;
      ++end;
    }
    if (end == offset) return ScopeError::Malformed;
    ZuCSpan name{requested.data() + offset, end - offset};
    const Scope *selected = nullptr;
    for (auto &scope: scopes) {
      if (scope.state != State::Active || scope.owner || scope.name != name ||
	  !hasID(client.scopeIDs, scope.id) ||
	  (grantedScopeIDs && !hasID(*grantedScopeIDs, scope.id))) continue;
      if (selected && selected->audience != scope.audience)
	return ScopeError::Audience;
      selected = &scope;
    }
    if (!selected) return ScopeError::Unavailable;
    if (int error = addScope(client, next, *selected)) return error;
    if (end == requested.length()) break;
    offset = end + 1;
    if (offset == requested.length()) return ScopeError::Malformed;
  }
  selection = ZuMv(next);
  return ScopeError::OK;
}

int selectScopes(
    const Client &client, ZuCSpan requested, ZuSpan<const Scope> scopes,
    ScopeSelection &selection)
{
  return selectScopes_(client, nullptr, requested, scopes, selection);
}

int selectGrantedScopes(
    const Client &client, const IDVec &grantedScopeIDs,
    bool requestedPresent, ZuCSpan requested,
    ZuSpan<const Scope> scopes, ScopeSelection &selection)
{
  if (requestedPresent)
    return selectScopes_(
      client, &grantedScopeIDs, requested, scopes, selection);
  ScopeSelection next;
  for (auto scopeID: grantedScopeIDs) {
    const Scope *selected = nullptr;
    for (auto &scope: scopes)
      if (scope.id == scopeID && scope.state == State::Active && !scope.owner &&
	  hasID(client.scopeIDs, scope.id)) {
	selected = &scope;
	break;
      }
    if (!selected) return ScopeError::Unavailable;
    if (int error = addScope(client, next, *selected)) return error;
  }
  if (!next.scopeIDs) return ScopeError::Unavailable;
  selection = ZuMv(next);
  return ScopeError::OK;
}

int interactiveAuthority(
    const Grant &grant, const User &user, const Cred &cred,
    const Client &client, bool requestedPresent, ZuCSpan requested,
    unsigned actionCount, ZuSpan<const Scope> scopes,
    ZuSpan<const Role> roles, ZuSpan<const Action> actionRecords,
    ScopeSelection &selection, ZtBitmap &actions)
{
  if (!interactivePrincipal(grant, user, cred, client))
    return AuthorityError::Invalid;

  ScopeSelection next;
  int error = selectGrantedScopes(client, grant.scopeIDs,
    requestedPresent, requested, scopes, next);
  if (error) return error;
  if (next.audience != grant.audience) return ScopeError::Audience;

  ZtBitmap nextActions = effectiveActions(actionCount,
    user.roleIDs, next.roleIDs, roles, actionRecords);
  nextActions &= grant.actions;
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
       client.type == ClientType::Native) &&
      (client.grants & ClientGrant::AuthorizationCode);
}

bool clientPrincipal(const Client &client)
{
  return client.state == State::Active && !client.owner &&
    client.type == ClientType::Confidential &&
    (client.grants & ClientGrant::ClientCredentials);
}

int clientAuthority(
    const Client &client, ZuCSpan requested, unsigned actionCount,
    ZuSpan<const Scope> scopes, ZuSpan<const Role> roles,
    ZuSpan<const Action> actionRecords, ScopeSelection &selection,
    ZtBitmap &actions)
{
  if (!clientPrincipal(client)) return AuthorityError::Invalid;

  ScopeSelection next;
  int error = selectScopes(client, requested, scopes, next);
  if (error) return error;
  ZtBitmap nextActions = effectiveActions(actionCount,
    client.roleIDs, next.roleIDs, roles, actionRecords);
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
