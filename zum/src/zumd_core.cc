//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_scope.hh>

#include <zlib/ZuUTF.hh>

#include <zlib/ZtlsSec.hh>

namespace Zum {

bool loginNormalize(String &name)
{
  enum { Max = 256 }; // bounded login index key and OIDC login_hint
  unsigned begin = 0, end = name.length();
  while (begin < end) {
    auto c = name[begin];
    if (c != ' ' && c != '\t') break;
    ++begin;
  }
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
ZtEnumImplNS(ClientProfile);
ZtEnumImplNS(ClientType);
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
ZtEnumImplNS(ScopeFlag);

ZtBitmap intersectActions(ZtBitmap actions, const ZtBitmap &limit)
{
  actions &= limit;
  if (actions.length() > limit.length()) actions.length(limit.length());
  return actions;
}

bool actionAlloc(App &app, ActionID &id)
{
  if (!app.id || app.nextActionID == UINT32_MAX) return false;
  id = app.nextActionID++;
  ++app.authVersion;
  ++app.version;
  return true;
}

template <typename Fn>
static void scopeTokens(ZuCSpan text, Fn fn)
{
  unsigned offset = 0, length = text.length();
  while (offset < length) {
    while (offset < length && text[offset] == ' ') ++offset;
    if (offset == length) break;
    unsigned end = offset;
    while (end < length && text[end] != ' ') ++end;
    fn(ZuCSpan{text.data() + offset, end - offset});
    offset = end;
  }
}

void ScopeSelect::init(const Client &client, bool requestedPresent, ZuCSpan requested,
    const IDVec *grantedIDs, const String *granted)
{
  m_tokens.null();
  m_scopes.clean();
  m_ids.clean();
  m_name = {};
  m_client = &client;
  m_grantedText = granted && *granted;
  m_hasGrant = grantedIDs;
  for (auto &name: client.identityScopes)
    scope_(name)->val() |= ScopeFlag::Identity();
  if (grantedIDs)
    for (auto id: *grantedIDs)
      id_(id)->val() |= ScopeIDFlag::Granted();
  if (granted)
    scopeTokens(*granted, [this, requestedPresent](ZuCSpan name) {
      auto scope = scope_(name);
      scope->val() |= ScopeFlag::Granted();
      if (!requestedPresent) request_(scope);
    });
  // An omitted request uses the ordered grant tokens from the same parse.
  if (!requestedPresent) return;
  scopeTokens(requested, [this](ZuCSpan name) {
    request_(scope_(name));
  });
}

ScopeNames::Node *ScopeSelect::scope_(ZuCSpan name)
{
  if (auto scope = m_scopes.find(name)) return scope;
  return m_scopes.add(String{name}, ScopeFlag::T{0});
}

void ScopeSelect::request_(ScopeNames::Node *scope)
{
  if (scope->val() & ScopeFlag::Requested()) return;
  scope->val() |= ScopeFlag::Requested();
  m_tokens.push(scope);
}

ScopeIDs::Node *ScopeSelect::id_(RoleID id)
{
  if (auto node = m_ids.find(id)) return node;
  return m_ids.add(id, ScopeIDFlag::T{0});
}

void ScopeSelect::begin(const ClientAccess &access, bool restricted,
    const IDVec *omittedIDs)
{
  m_name = {};
  {
    auto i = m_scopes.iter();
    while (auto scope = i()) {
      scope->val() &= ~ScopeFlag::Selected();
      if (!scope->val()) i.del(scope);
    }
  }
  m_next = {};
  {
    auto i = m_ids.iter();
    while (auto node = i()) {
      node->val() &= ScopeIDFlag::Granted();
      if (!node->val()) i.del(node);
    }
  }
  m_access = &access;
  m_restricted = restricted;
  m_omittedIDs = omittedIDs;
  m_offset = 0;
  for (auto id: access.roleIDs)
    id_(id)->val() |= ScopeIDFlag::Access();
}

void ScopeSelect::addName_(ScopeNames::Node *scope)
{
  if (scope->val() & ScopeFlag::Selected()) return;
  if (m_next.scope) m_next.scope << ' ';
  m_next.scope << scope->key();
  scope->val() |= ScopeFlag::Selected();
}

int ScopeSelect::step()
{
  if (m_omittedIDs) {
    unsigned n = m_omittedIDs->length();
    while (m_offset < n) {
      m_id = (*m_omittedIDs)[m_offset++];
      if (selected(m_id)) continue;
      m_name = {};
      return Lookup;
    }
    return m_next.roleIDs ? ScopeError::OK : ScopeError::Unavailable;
  }
  unsigned n = m_tokens.length();
  while (m_offset < n) {
    auto scope = m_tokens[m_offset++];
    m_name = scope->key();
    bool identity = scope->val() & ScopeFlag::Identity();
    if (m_restricted) {
      if (m_grantedText) {
	if (!(scope->val() & ScopeFlag::Granted())) return ScopeError::Unavailable;
      } else if (identity) return ScopeError::Unavailable;
    }
    if (m_name == "offline_access") {
      identity = m_client->state == State::Active && !m_client->owner &&
	m_client->refreshAllowed && (m_client->grants & ClientGrant::Refresh());
    }
    if (!identity) return Lookup;
    addName_(scope);
    m_next.identity = true;
  }
  return m_next.scope ? ScopeError::OK : ScopeError::Malformed;
}

int ScopeSelect::role(const Role &role)
{
  auto node = m_ids.find(role.id);
  if (role.appID != m_access->appID || m_access->clientID != m_client->id ||
      role.state != State::Active || role.owner || role.tombstone ||
      !node || !(node->val() & ScopeIDFlag::Access()) ||
      (m_restricted && m_hasGrant && !(node->val() & ScopeIDFlag::Granted())))
    return ScopeError::Unavailable;
  if (!(node->val() & ScopeIDFlag::Selected())) {
    addName_(scope_(role.name));
    node->val() |= ScopeIDFlag::Selected();
    m_next.roleIDs.push(role.id);
  }
  return ScopeError::OK;
}

ScopeSelection ScopeSelect::take()
{
  return ZuMv(m_next);
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
      (client.profile == ClientProfile::Browser ||
       client.profile == ClientProfile::Native ||
       client.profile == ClientProfile::Server) &&
      (client.grants & ClientGrant::AuthCode());
}

bool clientPrincipal(const Client &client)
{
  return client.state == State::Active && !client.owner &&
    clientType(client.profile) == ClientType::Confidential &&
    (client.grants & ClientGrant::ClientCredentials());
}

RefreshMatch::T refreshMatch(const Refresh &grant, ZuBSpan digest)
{
  if (Ztls::ctEqual(grant.digest, digest)) return RefreshMatch::Current;
  for (auto &spent: grant.spent)
    if (Ztls::ctEqual(spent, digest)) return RefreshMatch::Spent;
  return RefreshMatch::Unknown;
}

RefreshRotate::T refreshRotate(
    Refresh &grant, ZuBSpan presentedDigest, Bytes nextDigest,
    int64_t now, unsigned generationLimit, unsigned spentLimit)
{
  return refreshRotate(grant, refreshMatch(grant, presentedDigest),
    presentedDigest, ZuMv(nextDigest), now, generationLimit, spentLimit);
}

RefreshRotate::T refreshRotate(
    Refresh &grant, RefreshMatch::T match, ZuBSpan presentedDigest,
    Bytes nextDigest, int64_t now, unsigned generationLimit,
    unsigned spentLimit)
{
  if (grant.state != State::Active || grant.owner ||
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
