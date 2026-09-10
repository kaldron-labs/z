//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumDaemon.hh"

#include <zlib/ZumAdmin.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

static AdminResult adminErrorResult(
  unsigned status, ZuCSpan error, ZuCSpan message);

template <typename T, typename = void>
struct HasVersion : public ZuFalse { };
template <typename T>
struct HasVersion<T, decltype((void)ZuDeclVal<T>().version)> : public ZuTrue { };
template <typename T, typename = void>
struct HasOwner : public ZuFalse { };
template <typename T>
struct HasOwner<T, decltype((void)ZuDeclVal<T>().owner)> : public ZuTrue { };

template <typename T, typename = void>
struct HasAuthVer : public ZuFalse { };
template <typename T>
struct HasAuthVer<T, decltype((void)ZuDeclVal<T>().authVersion)> : public ZuTrue { };

static String rowETag(uint64_t);

struct NameInput {
  String name;
  String label;
};
ZfStruct(, (NameInput, JSON),
  (((name),	(Required)),	(String)),
  (((label),	(JSON::Opt)),	(String)));

struct UserInput {
  String name;
  String profile;
  String email;
};
ZfStruct(, (UserInput, JSON),
  (((name),	(Required)),	(String)),
  (((profile),	(JSON::Opt)),	(String)),
  (((email),	(JSON::Opt)),	(String)));

struct AppInput {
  String name;
  String label;
  String integration;
  String clientType;
  StringVec redirectURIs;
  String audienceURI;
};
ZfStruct(, (AppInput, JSON),
  (((name),		(Required)),	(String)),
  (((label),		(JSON::Opt)),	(String)),
  (((integration),	(Required)),	(String)),
  (((clientType),	(JSON::Opt)),	(String)),
  (((redirectURIs),	(JSON::Opt)),	(StringVec)),
  (((audienceURI),	(JSON::Opt)),	(String)));

struct MembershipInput {
  UserID userID = 0;
  IDVec roleIDs;
};
ZfStruct(, (MembershipInput, JSON),
  (((userID),	(Required, JSON::String<>)),	(UInt64)),
  (((roleIDs),	(JSON::Opt, JSON::String<>)),	(UInt64Vec)));

struct RolesInput {
  IDVec roleIDs;
};
ZfStruct(, (RolesInput, JSON),
  (((roleIDs),	(Required, JSON::String<>)),	(UInt64Vec)));

struct ActionsInput {
  ActionIDVec actionIDs;
};
ZfStruct(, (ActionsInput, JSON),
  (((actionIDs),	(Required)),	(UInt32Vec)));

struct ScopeInput {
  AudienceID audienceID = 0;
  String name;
};
ZfStruct(, (ScopeInput, JSON),
  (((audienceID),	(Required, JSON::String<>)),	(UInt64)),
  (((name),		(Required)),	(String)));

struct AudienceInput {
  AppID appID = 0;
  String name;
  String uri;
};
ZfStruct(, (AudienceInput, JSON),
  (((appID),	(Required, JSON::String<>)),	(UInt64)),
  (((name),	(Required)),	(String)),
  (((uri),	(Required)),	(String)));

struct RotateInput {
  uint32_t overlapSeconds = 0;
};
ZfStruct(, (RotateInput, JSON),
  (((overlapSeconds),	(JSON::Opt)),	(UInt32, 0)));

struct ClientInput {
  String id;
  AppID appID = 0;
  String label;
  String type;
  StringVec redirectURIs;
  uint8_t grants = 0;
  bool refreshAllowed = false;
  StringVec identityScopes;
};
ZfStruct(, (ClientInput, JSON),
  (((id),		(JSON::Opt)),	(String)),
  (((appID),		(Required, JSON::String<>)),	(UInt64)),
  (((label),		(JSON::Opt)),	(String)),
  (((type),		(Required)),	(String)),
  (((redirectURIs),	(JSON::Opt)),	(StringVec)),
  (((grants),		(JSON::Opt)),	(UInt8)),
  (((refreshAllowed),	(JSON::Opt)),	(Bool, false)),
  (((identityScopes),	(JSON::Opt)),	(StringVec)));

struct ClientUpdateInput {
  String label;
  StringVec redirectURIs;
  uint8_t grants = 0;
  StringVec identityScopes;
};
ZfStruct(, (ClientUpdateInput, JSON),
  (((label),		(JSON::Opt)),	(String)),
  (((redirectURIs),	(JSON::Opt)),	(StringVec)),
  (((grants),		(JSON::Opt)),	(UInt8)),
  (((identityScopes),	(JSON::Opt)),	(StringVec)));

struct ClientAccessInput {
  IDVec audienceIDs;
  IDVec scopeIDs;
  IDVec roleIDs;
};
ZfStruct(, (ClientAccessInput, JSON),
  (((audienceIDs),	(Required, JSON::String<>)),	(UInt64Vec)),
  (((scopeIDs),		(Required, JSON::String<>)),	(UInt64Vec)),
  (((roleIDs),		(Required, JSON::String<>)),	(UInt64Vec)));

struct AdminAccessInput {
  ActionIDVec operationIDs;
  IDVec roleIDs;
};
ZfStruct(, (AdminAccessInput, JSON),
  (((operationIDs),	(Required)),	(UInt32Vec)),
  (((roleIDs),		(Required, JSON::String<>)),	(UInt64Vec)));

struct ProviderInput {
  String name;
  String issuer;
  String clientID;
  String clientSecret;
  StringVec scopes;
  String roleClaim;
  String claimSource;
};
ZfStruct(, (ProviderInput, JSON),
  (((name),		(Required)),	(String)),
  (((issuer),		(Required)),	(String)),
  (((clientID),		(Required)),	(String)),
  (((clientSecret),	(JSON::Opt)),	(String)),
  (((scopes),		(Required)),	(StringVec)),
  (((roleClaim),	(Required)),	(String)),
  (((claimSource),	(Required)),	(String)));

struct ProviderUpdateInput {
  String issuer;
  String clientID;
  String clientSecret;
  StringVec scopes;
  String roleClaim;
  String claimSource;
};
ZfStruct(, (ProviderUpdateInput, JSON),
  (((issuer),		(JSON::Opt)),	(String)),
  (((clientID),		(JSON::Opt)),	(String)),
  (((clientSecret),	(JSON::Opt)),	(String)),
  (((scopes),		(JSON::Opt)),	(StringVec)),
  (((roleClaim),	(JSON::Opt)),	(String)),
  (((claimSource),	(JSON::Opt)),	(String)));

struct AuthPolicyInput {
  ProviderID providerID = 0;
  bool localFirst = true;
  String eligibilityMode;
  String eligibilityClaim;
  StringVec eligibilityValues;
  uint32_t assignmentMaxAge = 0;
  uint32_t sessionIdle = 1800;
  uint32_t sessionAbsolute = 43200;
  uint32_t tokenLifetime = 300;
  String consentPolicy;
  String state;
};
ZfStruct(, (AuthPolicyInput, JSON),
  (((providerID),	(JSON::Opt, JSON::String<>)),	(UInt64)),
  (((localFirst),	(Required)),	(Bool, true)),
  (((eligibilityMode),	(Required)),	(String)),
  (((eligibilityClaim),	(JSON::Opt)),	(String)),
  (((eligibilityValues),(JSON::Opt)),	(StringVec)),
  (((assignmentMaxAge),(Required)),	(UInt32)),
  (((sessionIdle),	(Required)),	(UInt32)),
  (((sessionAbsolute),	(Required)),	(UInt32)),
  (((tokenLifetime),	(Required)),	(UInt32)),
  (((consentPolicy),	(Required)),	(String)),
  (((state),		(JSON::Opt)),	(String)));

struct RoleMapInput { RoleID roleID = 0; };
ZfStruct(, (RoleMapInput, JSON),
  (((roleID),		(Required, JSON::String<>)),	(UInt64)));

struct SessionSelector {
  UserID userID = 0;
  uint32_t limit = 0;
};
ZfStruct(, (SessionSelector, JSON),
  (((userID),		(Required, JSON::String<>)),	(UInt64)),
  (((limit),		(Required)),	(UInt32)));

struct ConsentSelector {
  UserID userID = 0;
  String clientID;
  AppID appID = 0;
  AudienceID audienceID = 0;
  uint32_t limit = 0;
};
ZfStruct(, (ConsentSelector, JSON),
  (((userID),		(Required, JSON::String<>)),	(UInt64)),
  (((clientID),		(JSON::Opt)),	(String)),
  (((appID),		(JSON::Opt, JSON::String<>)),	(UInt64)),
  (((audienceID),	(JSON::Opt, JSON::String<>)),	(UInt64)),
  (((limit),		(Required)),	(UInt32)));

struct GrantSelector {
  String id;
  UserID userID = 0;
  AppID appID = 0;
  uint32_t limit = 0;
};
ZfStruct(, (GrantSelector, JSON),
  (((id),		(JSON::Opt)),	(String)),
  (((userID),		(JSON::Opt, JSON::String<>)),	(UInt64)),
  (((appID),		(JSON::Opt, JSON::String<>)),	(UInt64)),
  (((limit),		(Required)),	(UInt32)));

struct CleanupInput {
  int64_t before = 0;
  uint32_t limit = 0;
};
ZfStruct(, (CleanupInput, JSON),
  (((before),		(JSON::Opt)),	(Int64)),
  (((limit),		(Required)),	(UInt32)));

struct SignKeyInput {
  String id;
  String algorithm;
  String providerRef;
  String publicJwk;
  String privateMaterial;
  int64_t notBefore = 0;
};
ZfStruct(, (SignKeyInput, JSON),
  (((id),		(Required)),	(String)),
  (((algorithm),	(Required)),	(String)),
  (((providerRef),	(JSON::Opt)),	(String)),
  (((publicJwk),	(Required)),	(String)),
  (((privateMaterial),	(JSON::Opt)),	(String)),
  (((notBefore),	(Required)),	(Int64)));

struct RetireInput { int64_t retireAfter = 0; };
ZfStruct(, (RetireInput, JSON),
  (((retireAfter),	(Required)),	(Int64)));

struct CatalogActionInput {
  String name;
  String label;
};
ZfStruct(, (CatalogActionInput, JSON),
  (((name),		(Required)),	(String)),
  (((label),		(JSON::Opt)),	(String)));
struct CatalogActionVec : public ZtArray<CatalogActionInput, VecHeap> {
  ZuDerive_(CatalogActionVec, (ZtArray<CatalogActionInput, VecHeap>));
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogActionVec *);
};

struct CatalogRoleInput {
  String name;
  String label;
  StringVec actions;
};
ZfStruct(, (CatalogRoleInput, JSON),
  (((name),		(Required)),	(String)),
  (((label),		(JSON::Opt)),	(String)),
  (((actions),		(Required)),	(StringVec)));
struct CatalogRoleVec : public ZtArray<CatalogRoleInput, VecHeap> {
  ZuDerive_(CatalogRoleVec, (ZtArray<CatalogRoleInput, VecHeap>));
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogRoleVec *);
};

struct CatalogScopeInput {
  AudienceID audienceID = 0;
  String name;
  StringVec roles;
};
ZfStruct(, (CatalogScopeInput, JSON),
  (((audienceID),	(Required, JSON::String<>)),	(UInt64)),
  (((name),		(Required)),	(String)),
  (((roles),		(Required)),	(StringVec)));
struct CatalogScopeVec : public ZtArray<CatalogScopeInput, VecHeap> {
  ZuDerive_(CatalogScopeVec, (ZtArray<CatalogScopeInput, VecHeap>));
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CatalogScopeVec *);
};

struct CatalogInput {
  uint64_t revision = 0;
  String digest;
  CatalogActionVec actions;
  CatalogRoleVec roles;
  CatalogScopeVec scopes;
};
ZfStruct(, (CatalogInput, JSON),
  (((revision),		(Required, JSON::String<>)),	(UInt64)),
  (((digest),		(Required)),	(String)),
  (((actions),		(Required)),	(UDT)),
  (((roles),		(Required)),	(UDT)),
  (((scopes),		(Required)),	(UDT)));

struct QueryInput {
  enum Field {
    ID, Name, Source, AppID_, UserID_, AudienceID_, ClientID_, ProviderID_,
    ActorKind_, ActorID_, Value, KeyID, URI, Issuer_, Subject, Operation,
    IdempotencyKey, Cursor, Limit, N
  };
  String id;
  String name;
  String source;
  String appID;
  String userID;
  String audienceID;
  String clientID;
  String providerID;
  String actorKind;
  String actorID;
  String value;
  String keyID;
  String uri;
  String issuer;
  String subject;
  String operation;
  String idempotencyKey;
  String cursor;
  uint32_t limit = 100;
  uint32_t seen = 0;
};
ZfStruct(, (QueryInput, URI),
  (((id),		(Mutable)),	(String)),
  (((name),		(Mutable)),	(String)),
  (((source),		(Mutable)),	(String)),
  (((appID),		(Mutable)),	(String)),
  (((userID),		(Mutable)),	(String)),
  (((audienceID),	(Mutable)),	(String)),
  (((clientID),		(Mutable)),	(String)),
  (((providerID),	(Mutable)),	(String)),
  (((actorKind),	(Mutable)),	(String)),
  (((actorID),		(Mutable)),	(String)),
  (((value),		(Mutable)),	(String)),
  (((keyID),		(Mutable)),	(String)),
  (((uri),		(Mutable)),	(String)),
  (((issuer),		(Mutable)),	(String)),
  (((subject),		(Mutable)),	(String)),
  (((operation),	(Mutable)),	(String)),
  (((idempotencyKey),	(Mutable)),	(String)),
  (((cursor),		(Mutable)),	(String)),
  (((limit),		(Mutable)),	(UInt32, 100)));

struct ServiceAuthorizeInput {
  String clientID;
  String redirectURI;
  String responseType;
  String scope;
  String resource;
  String state;
  String codeChallenge;
  String codeChallengeMethod;
  String nonce;
  String prompt;
  uint32_t maxAge = 0;
};
ZfStruct(, (ServiceAuthorizeInput, JSON),
  (((clientID),		(Mutable)),	(String)),
  (((redirectURI),	(Mutable)),	(String)),
  (((responseType),	(Mutable)),	(String)),
  (((scope),		(Mutable)),	(String)),
  (((resource),		(Mutable)),	(String)),
  (((state),		(Mutable)),	(String)),
  (((codeChallenge),	(Mutable)),	(String)),
  (((codeChallengeMethod),(Mutable)),	(String)),
  (((nonce),		(Mutable)),	(String)),
  (((prompt),		(Mutable)),	(String)),
  (((maxAge),		(Mutable)),	(UInt32)));

struct ServiceFormInput { String form; };
ZfStruct(, (ServiceFormInput, JSON),
  (((form),		(Mutable)),	(String)));

static bool adminQueryInput(String raw, QueryInput &input)
{
  if (!raw) return true;
  unsigned offset = 0;
  while (offset < raw.length()) {
    auto tail = ZuCSpan{raw}.offset(offset);
    auto amp = tail.find<"&">();
    unsigned length = amp >= 0 ? unsigned(amp) : tail.length();
    if (!length) return false;
    ZuCSpan pair{tail.data(), length};
    auto equal = pair.find<"=">();
    ZuCSpan key{pair.data(), equal >= 0 ? unsigned(equal) : pair.length()};
    int field = -1;
    static constexpr ZuCSpan names[] = {
      "id", "name", "source", "appID", "userID", "audienceID",
      "clientID", "providerID", "actorKind", "actorID", "value", "keyID",
      "uri", "issuer", "subject", "operation", "idempotencyKey", "cursor",
      "limit"
    };
    for (unsigned i = 0; i < QueryInput::N; ++i)
      if (key == names[i]) { field = int(i); break; }
    if (field < 0 || input.seen & (1U<<field)) return false;
    input.seen |= 1U<<field;
    if (amp < 0) break;
    offset += length + 1;
  }
  String encoded{"?"};
  encoded << raw;
  auto parsed = ZfURI::scan(encoded.span());
  if (parsed.p<0>() != int(encoded.length()) || !parsed.p<1>()) return false;
  ZfURI::handler<QueryInput, ZuFacet::URI>(parsed.p<1>()).update(input);
  constexpr uint32_t exact = (1U<<QueryInput::Cursor) - 1;
  return input.limit && input.limit <= 1000 &&
    (!(input.seen & (1U<<QueryInput::Cursor)) || !(input.seen & exact));
}

static bool adminQueryFields(int op, uint32_t seen)
{
  auto fields = [seen](uint32_t allowed) { return !(seen & ~allowed); };
  constexpr uint32_t id = 1U<<QueryInput::ID;
  constexpr uint32_t name = 1U<<QueryInput::Name;
  constexpr uint32_t source = 1U<<QueryInput::Source;
  constexpr uint32_t appID = 1U<<QueryInput::AppID_;
  constexpr uint32_t userID = 1U<<QueryInput::UserID_;
  constexpr uint32_t audienceID = 1U<<QueryInput::AudienceID_;
  constexpr uint32_t clientID = 1U<<QueryInput::ClientID_;
  constexpr uint32_t operation = 1U<<QueryInput::Operation;
  constexpr uint32_t idem = 1U<<QueryInput::IdempotencyKey;
  constexpr uint32_t page = (1U<<QueryInput::Limit) |
    (1U<<QueryInput::Cursor);
  switch (op) {
    case MgmtOp::issuerQuery: return !seen;
    case MgmtOp::operationQuery: return fields(operation | idem | page);
    case MgmtOp::appQuery: return fields(id | name | page);
    case MgmtOp::userQuery: return fields(id | name | source | page);
    case MgmtOp::credentialQuery: return fields(page);
    case MgmtOp::membershipQuery: return fields(userID | page);
    case MgmtOp::actionQuery:
    case MgmtOp::roleQuery: return fields(id | name | page);
    case MgmtOp::scopeQuery:
      return fields(id | audienceID | name | page);
    case MgmtOp::audienceQuery: return fields(id | (1U<<QueryInput::URI) | page);
    case MgmtOp::clientQuery: return fields(id | clientID | page);
    case MgmtOp::clientAccessQuery: return fields(clientID | page);
    case MgmtOp::adminAccessQuery:
    case MgmtOp::roleMapQuery: return fields(page);
    case MgmtOp::providerQuery: return fields(id | name | page);
    case MgmtOp::authPolicyQuery: return fields(appID | page);
    case MgmtOp::identityQuery:
    case MgmtOp::evidenceQuery:
    case MgmtOp::sessionQuery:
    case MgmtOp::consentQuery:
    case MgmtOp::grantQuery:
    case MgmtOp::signKeyQuery:
    case MgmtOp::auditQuery: return fields(page);
    default: return !seen;
  }
}

template <typename T>
static bool adminBody(String &body, T &value)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  value = ZfJSON::handler<T, ZuFacet::JSON>(roots[0].ptr()).ctor();
  return true;
}

template <typename T, unsigned N>
static bool adminBodyFields(String &body, T &value,
    const ZuCSpan (&names)[N], uint64_t &seen)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    unsigned i = 0;
    while (i < N && field.p<0>() != names[i]) ++i;
    if (i == N || (seen & (uint64_t{1}<<i))) return false;
    seen |= uint64_t{1}<<i;
  }
  if (!seen) return false;
  value = ZfJSON::handler<T, ZuFacet::JSON>(roots[0].ptr()).ctor();
  return true;
}

static bool stringPatchBody(String &body,
    ZuCSpan first, String &firstValue, ZuCSpan second,
    String &secondValue, unsigned &seen)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    auto value = field.p<1>().ptr();
    if (!value->has<ZfJSON::AnyNode::String>()) return false;
    if (field.p<0>() == first && !(seen & 1U)) {
      firstValue = value->data<ZfJSON::AnyNode::String>();
      seen |= 1U;
    } else if (second && field.p<0>() == second && !(seen & 2U)) {
      secondValue = value->data<ZfJSON::AnyNode::String>();
      seen |= 2U;
    } else {
      return false;
    }
  }
  return seen;
}

static bool randomID(Ztls::Random &rng, uint64_t &id)
{
  do {
    if (!rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}))
      return false;
  } while (!id || id == UINT64_MAX);
  return true;
}

static ClientType::T clientType(ZuCSpan value)
{
  if (value == "browser") return ClientType::Browser;
  if (value == "native") return ClientType::Native;
  if (value == "confidential") return ClientType::Confidential;
  return ClientType::T(-1);
}

static bool clientConfigValid(
    ClientType::T type, uint8_t grants, const StringVec &redirects)
{
  constexpr uint8_t all = ClientGrant::AuthorizationCode |
    ClientGrant::ClientCredentials | ClientGrant::RefreshToken;
  if (type < 0 || type >= ClientType::N || !grants || (grants & ~all))
    return false;
  if ((grants & ClientGrant::AuthorizationCode) && !redirects) return false;
  if (type != ClientType::Confidential &&
      (grants & ClientGrant::ClientCredentials)) return false;
  return true;
}

static bool roleCeiling(const AdminPermit &permit, const IDVec &roleIDs)
{
  if (permit.superuser) return true;
  for (auto roleID: roleIDs) {
    bool found = false;
    for (auto allowed: permit.roleIDs)
      if (roleID == allowed) { found = true; break; }
    if (!found) return false;
  }
  return true;
}

class RoleRefs_ : public ZumPolymorph {
public:
  RoleRefs_(DBContext *context, AppID appID, IDVec ids,
      ZmFn<void(bool)> complete) : m_context{context}, m_appID{appID},
    m_ids{ZuMv(ids)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    for (unsigned i = 0; i < m_ids.length(); ++i) {
      if (!m_ids[i]) { finish_(false); return; }
      for (unsigned j = 0; j < i; ++j)
	if (m_ids[i] == m_ids[j]) { finish_(false); return; }
    }
    next_();
  }

private:
  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ok);
  }

  void next_()
  {
    if (m_offset >= m_ids.length()) { finish_(true); return; }
    auto roles = m_context->roles;
    auto id = m_ids[m_offset++];
    roles->run(0, [self = ZmRef<RoleRefs_>{this}, roles, id]() mutable {
      roles->find<0>(0, ZuFwdTuple(self->m_appID, id), [
          self = ZuMv(self)](ZdbRowRef<Role> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  self->finish_(false);
	  return;
	}
	self->next_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  AppID	m_appID = 0;
  IDVec	m_ids;
  ZmFn<void(bool)> m_complete;
  unsigned	m_offset = 0;
  bool		m_done = false;
};

using ActionRefsFn = ZmFn<void(bool, ZtBitmap)>;

class ActionRefs_ : public ZumPolymorph {
public:
  ActionRefs_(DBContext *context, AppID appID, ActionIDVec ids,
      ActionRefsFn complete) : m_context{context}, m_appID{appID},
    m_ids{ZuMv(ids)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    for (unsigned i = 0; i < m_ids.length(); ++i)
      for (unsigned j = 0; j < i; ++j)
	if (m_ids[i] == m_ids[j]) { finish_(false); return; }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<ActionRefs_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
	  ZdbRowRef<App> app) mutable {
	if (!app || app->data().state != State::Active || app->data().owner) {
	  self->finish_(false);
	  return;
	}
	self->m_actions.length(app->data().nextActionID);
	self->next_();
      });
    });
  }

private:
  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ok, ok ? ZuMv(m_actions) : ZtBitmap{});
  }

  void next_()
  {
    if (m_offset >= m_ids.length()) { finish_(true); return; }
    ActionID id = m_ids[m_offset++];
    if (id >= m_actions.length()) { finish_(false); return; }
    auto actions = m_context->actions;
    actions->run(0, [self = ZmRef<ActionRefs_>{this}, actions, id]() mutable {
      actions->find<0>(0, ZuFwdTuple(self->m_appID, id), [self = ZuMv(self),
	  id](ZdbRowRef<Action> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  self->finish_(false);
	  return;
	}
	self->m_actions.set(id);
	self->next_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  AppID	m_appID = 0;
  ActionIDVec	m_ids;
  ActionRefsFn	m_complete;
  ZtBitmap	m_actions;
  unsigned	m_offset = 0;
  bool		m_done = false;
};

static bool uniqueIDs(const IDVec &ids)
{
  for (unsigned i = 0; i < ids.length(); ++i) {
    if (!ids[i]) return false;
    for (unsigned j = 0; j < i; ++j)
      if (ids[i] == ids[j]) return false;
  }
  return true;
}

class ClientAccessRefs_ : public ZumPolymorph {
public:
  ClientAccessRefs_(DBContext *context, ClientAccess access,
      ZmFn<void(bool)> complete) : m_context{context},
    m_access{ZuMv(access)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_access.clientID || !m_access.appID ||
	!uniqueIDs(m_access.audienceIDs) || !uniqueIDs(m_access.scopeIDs) ||
	!uniqueIDs(m_access.roleIDs)) { finish_(false); return; }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<ClientAccessRefs_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_access.appID), [self = ZuMv(self)](
	  ZdbRowRef<App> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(false); return;
	}
	self->client_();
      });
    });
  }

private:
  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ok);
  }

  void client_()
  {
    auto clients = m_context->clients;
    clients->run(0, [self = ZmRef<ClientAccessRefs_>{this}, clients]() mutable {
      clients->find<0>(0, ZuFwdTuple(self->m_access.clientID), [self = ZuMv(self)](
	  ZdbRowRef<Client> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(false); return;
	}
	self->audience_();
      });
    });
  }

  void audience_()
  {
    if (m_offset >= m_access.audienceIDs.length()) {
      m_offset = 0;
      scope_();
      return;
    }
    AudienceID id = m_access.audienceIDs[m_offset++];
    auto audiences = m_context->audiences;
    audiences->run(0, [self = ZmRef<ClientAccessRefs_>{this}, audiences,
	id]() mutable {
      audiences->find<0>(0, ZuFwdTuple(id), [self = ZuMv(self)](
	  ZdbRowRef<Audience> row) mutable {
	if (!row || row->data().appID != self->m_access.appID ||
	    row->data().state != State::Active || row->data().owner) {
	  self->finish_(false); return;
	}
	self->audience_();
      });
    });
  }

  void scope_()
  {
    if (m_offset >= m_access.scopeIDs.length()) {
      ZmRef<RoleRefs_> refs = new RoleRefs_{m_context, m_access.appID,
	IDVec{m_access.roleIDs}, [self = ZmRef<ClientAccessRefs_>{this}](
	  bool ok) mutable { self->finish_(ok); }};
      refs->start();
      return;
    }
    ScopeID id = m_access.scopeIDs[m_offset++];
    auto scopes = m_context->scopes;
    scopes->run(0, [self = ZmRef<ClientAccessRefs_>{this}, scopes,
	id]() mutable {
      scopes->find<0>(0, ZuFwdTuple(self->m_access.appID, id), [
	  self = ZuMv(self)](ZdbRowRef<Scope> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->finish_(false); return;
	}
	bool approved = false;
	for (auto audienceID: self->m_access.audienceIDs)
	  if (audienceID == row->data().audienceID) { approved = true; break; }
	if (!approved) { self->finish_(false); return; }
	self->scope_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  ClientAccess	m_access;
  ZmFn<void(bool)> m_complete;
  unsigned	m_offset = 0;
  bool		m_done = false;
};

class MembershipAdd_ : public ZumPolymorph {
public:
  MembershipAdd_(DBContext *context, Membership membership,
      AdminDoneFn complete) : m_context{context},
    m_membership{ZuMv(membership)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<MembershipAdd_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_membership.appID), [
	  self = ZuMv(self)](ZdbRowRef<App> app) mutable {
	if (!app || app->data().state != State::Active || app->data().owner) {
	  self->finish_(404, "application not found");
	  return;
	}
	self->user_();
      });
    });
  }

private:
  void user_()
  {
    auto users = m_context->users;
    users->run(0, [self = ZmRef<MembershipAdd_>{this}, users]() mutable {
      users->find<0>(0, ZuFwdTuple(self->m_membership.userID), [
	  self = ZuMv(self)](ZdbRowRef<User> user) mutable {
	if (!user || user->data().source != UserSource::Local ||
	    user->data().state == State::Revoked || user->data().owner) {
	  self->finish_(404, "local user not found");
	  return;
	}
	self->insert_();
      });
    });
  }

  void insert_()
  {
    auto memberships = m_context->memberships;
    memberships->run(0, [self = ZmRef<MembershipAdd_>{this},
	memberships]() mutable {
      ZdbRowRef<Membership> row =
	new ZdbRow<Membership>{memberships, ZdbShard{0}};
      memberships->insert(ZuMv(row), [self = ZuMv(self)](
	  ZdbRow<Membership> *row) mutable {
	if (!row) { self->finish_(409, "membership already exists"); return; }
	new (row->ptr()) Membership{self->m_membership};
	if (!row->commit()) { self->finish_(503, "commit failed"); return; }
	String json{"{\"item\":{\"appID\":\""};
	json << row->data().appID << "\",\"userID\":\"" <<
	  row->data().userID << "\",\"etag\":";
	ZfJSON::quote(json, rowETag(row->data().version));
	json << "}}";
	StringVec ids;
	String resultID;
	resultID << row->data().appID << ':' << row->data().userID;
	ids.push(ZuMv(resultID));
	self->finish_(AdminResult{ZuMv(json), 201, ZuMv(ids)});
      });
    });
  }

  void finish_(unsigned status, ZuCSpan message)
  {
    String error{"{\"error\":"};
    ZfJSON::quote(error, status == 404 ? "not_found" :
      status == 409 ? "conflict" : "unavailable");
    error << ",\"message\":";
    ZfJSON::quote(error, message);
    error << ",\"correlationID\":\"\"}";
    finish_(AdminResult{ZuMv(error), status});
  }

  void finish_(AdminResult result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ZuMv(result));
  }

  DBContext	*m_context = nullptr;
  Membership	m_membership;
  AdminDoneFn	m_complete;
  bool		m_done = false;
};

class BulkRevoke_ : public ZumPolymorph {
public:
  enum Kind { Sessions, Consents, Grants };

  BulkRevoke_(DBContext *context, Kind kind, UserID userID,
      String clientID, AppID appID, AudienceID audienceID,
      uint32_t limit, AdminDoneFn complete) : m_context{context},
    m_kind{kind}, m_userID{userID}, m_clientID{ZuMv(clientID)},
    m_appID{appID}, m_audienceID{audienceID}, m_limit{limit},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_limit || m_limit > 1000) {
      finish_(adminErrorResult(400, "invalid_request",
	"invalid bounded revoke selector"));
      return;
    }
    switch (m_kind) {
      case Sessions: sessionSelect_(); return;
      case Consents: consentSelect_(); return;
      case Grants: grantSelect_(); return;
    }
  }

private:
  struct ConsentKey {
    UserID userID = 0;
    String clientID;
    AppID appID = 0;
    AudienceID audienceID = 0;
  };

  void finish_(AdminResult result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ZuMv(result));
  }

  void success_()
  {
    String json{"{\"item\":{\"revoked\":"};
    json << m_removed << "}}";
    finish_(AdminResult{ZuMv(json), 200});
  }

  void sessionSelect_()
  {
    if (!m_userID) {
      finish_(adminErrorResult(400, "invalid_request",
	"session revoke requires userID"));
      return;
    }
    using Table = SessionTable;
    using Tuple = Table::Tuple;
    auto sessions = m_context->sessions;
    sessions->run(0, [self = ZmRef<BulkRevoke_>{this}, sessions]() mutable {
      sessions->selectRows<1>(ZuFwdTuple(self->m_userID), self->m_limit, [
	  self = ZuMv(self)](ZuUnion<void, Tuple> result, unsigned) mutable {
	if (result.template is<Tuple>()) {
	  auto tuple = ZuMv(result).template p<Tuple>();
	  if (tuple.template p<1>() == self->m_userID)
	    self->m_ids.push(Bytes{tuple.template p<0>()});
	  return;
	}
	self->next_();
      });
    });
  }

  void consentSelect_()
  {
    if (!m_userID) {
      finish_(adminErrorResult(400, "invalid_request",
	"consent revoke requires userID"));
      return;
    }
    using Table = ConsentTable;
    using Tuple = Table::Tuple;
    auto consents = m_context->consents;
    consents->run(0, [self = ZmRef<BulkRevoke_>{this}, consents]() mutable {
      consents->selectRows<0>({}, self->m_limit, [self = ZuMv(self)](
	  ZuUnion<void, Tuple> result, unsigned) mutable {
	if (result.template is<Tuple>()) {
	  auto tuple = ZuMv(result).template p<Tuple>();
	  if (tuple.template p<0>() != self->m_userID ||
	      (self->m_clientID &&
	       tuple.template p<1>() != self->m_clientID) ||
	      (self->m_appID && tuple.template p<2>() != self->m_appID) ||
	      (self->m_audienceID &&
	       tuple.template p<3>() != self->m_audienceID)) return;
	  self->m_consentKeys.push(ConsentKey{
	    tuple.template p<0>(), String{tuple.template p<1>()},
	    tuple.template p<2>(), tuple.template p<3>()});
	  return;
	}
	self->next_();
      });
    });
  }

  void grantSelect_()
  {
    if (!m_userID && !m_appID) {
      finish_(adminErrorResult(400, "invalid_request",
	"grant revoke requires userID or appID"));
      return;
    }
    using Table = GrantTable;
    using Tuple = Table::Tuple;
    auto grants = m_context->grants;
    grants->run(0, [self = ZmRef<BulkRevoke_>{this}, grants]() mutable {
      grants->selectRows<0>({}, self->m_limit, [self = ZuMv(self)](
	  ZuUnion<void, Tuple> result, unsigned) mutable {
	if (result.template is<Tuple>()) {
	  auto tuple = ZuMv(result).template p<Tuple>();
	  if ((self->m_userID && tuple.template p<2>() != self->m_userID) ||
	      (self->m_appID && tuple.template p<3>() != self->m_appID)) return;
	  self->m_ids.push(Bytes{tuple.template p<0>()});
	  return;
	}
	self->next_();
      });
    });
  }

  void next_()
  {
    if (m_kind == Consents) {
      if (m_offset >= m_consentKeys.length()) { success_(); return; }
      ConsentKey key = ZuMv(m_consentKeys[m_offset++]);
      auto table = m_context->consents;
      table->run(0, [self = ZmRef<BulkRevoke_>{this}, table,
	  key = ZuMv(key)]() mutable {
	table->findUpd<0>(0, ZuFwdTuple(key.userID, ZuMv(key.clientID),
	    key.appID, key.audienceID), [self = ZuMv(self)](
	      ZdbRow<Consent> *row) mutable { self->update_(row); });
      });
      return;
    }
    if (m_offset >= m_ids.length()) { success_(); return; }
    Bytes id = ZuMv(m_ids[m_offset++]);
    if (m_kind == Sessions) {
      auto table = m_context->sessions;
      table->run(0, [self = ZmRef<BulkRevoke_>{this}, table,
	  id = ZuMv(id)]() mutable {
	table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	    ZdbRow<Session> *row) mutable { self->update_(row); });
      });
    } else {
      auto table = m_context->grants;
      table->run(0, [self = ZmRef<BulkRevoke_>{this}, table,
	  id = ZuMv(id)]() mutable {
	table->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	    ZdbRow<Grant> *row) mutable { self->update_(row); });
      });
    }
  }

  template <typename Row>
  void update_(Row *row)
  {
    if (row && !row->data().owner && row->data().state != State::Revoked &&
	row->data().state != State::Consumed) {
      row->data().state = State::Revoked;
      if constexpr (HasVersion<typename Row::T>{}) {
	++row->data().version;
	row->data().updated = Zm::now().sec();
      }
      if (!row->commit()) {
	finish_(adminErrorResult(503, "unavailable", "commit failed"));
	return;
      }
      ++m_removed;
    }
    next_();
  }

  DBContext	*m_context = nullptr;
  Kind		m_kind = Sessions;
  UserID	m_userID = 0;
  String	m_clientID;
  AppID		m_appID = 0;
  AudienceID	m_audienceID = 0;
  uint32_t	m_limit = 0;
  AdminDoneFn	m_complete;
  BytesVec	m_ids;
  ZtArray<ConsentKey, VecHeap> m_consentKeys;
  unsigned	m_offset = 0;
  unsigned	m_removed = 0;
  bool		m_done = false;
};

bool DaemonParser::operation(Zhttp::Method::T method, Zhttp::Target &target)
{
  if (!Base::operation(method, target)) return false;
  u.dispatch([this](auto, auto &request) { request.app = app; });
  return true;
}

static String encode(ZuBSpan data)
{
  String value;
  value.length(ZuBase64URL::enclen(data.length()));
  value.length(ZuBase64URL::encode(value.span(), data));
  return value;
}

class ActionAdd_ : public ZumPolymorph {
public:
  ActionAdd_(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
      NameInput input, AdminDoneFn complete) : m_db{db}, m_context{context},
    m_rng{rng}, m_appID{appID}, m_input{ZuMv(input)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_appID || !m_input.name) {
      finish_(400, "invalid action creation");
      return;
    }
    uint8_t id[sizeof(m_sagaID)];
    if (!m_rng->random(id)) {
      finish_(503, "saga ID generation failed");
      return;
    }
    memcpy(&m_sagaID, id, sizeof(m_sagaID));
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<ActionAdd_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
          ZdbRowRef<App> row) mutable { self->app_(ZuMv(row)); });
    });
  }

private:
  void finish_(unsigned status, ZuCSpan message)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    if (status != 201) {
      complete(adminErrorResult(status,
	status == 400 ? "invalid_request" :
	status == 409 ? "conflict" : "unavailable", message));
      return;
    }
    String json{"{\"item\":{\"id\":"};
    json << m_actionID << ",\"etag\":";
    ZfJSON::quote(json, rowETag(1));
    json << "}}";
    StringVec ids;
    String id;
    id << m_actionID;
    ids.push(ZuMv(id));
    complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
  }

  void app_(ZdbRowRef<App> row)
  {
    if (!row || row->data().owner || row->data().state != State::Active ||
	row->data().nextActionID == UINT32_MAX ||
	row->data().version == UINT64_MAX ||
	row->data().authVersion == UINT64_MAX) {
      finish_(409, "application cannot add actions");
      return;
    }
    m_actionID = row->data().nextActionID;
    int64_t now = Zm::now().sec();
    AppActionAdd add{.appID = m_appID, .actionID = m_actionID,
      .name = ZuMv(m_input.name), .label = ZuMv(m_input.label),
      .created = now, .oldAppVersion = row->data().version,
      .oldAuthVersion = row->data().authVersion,
      .oldUpdated = row->data().updated};
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(add));
    if (!sagaSubmit(m_db, m_sagaID, ZuMv(saga),
	SagaFn{ZmRef<ActionAdd_>{this}, ZmFnPtr<&ActionAdd_::submitted_>{}},
	SagaFn{ZmRef<ActionAdd_>{this}, ZmFnPtr<&ActionAdd_::completed_>{}}))
      submitted_(false);
  }

  void submitted_(bool ok)
  {
    if (!ok) finish_(503, "action creation unavailable");
  }

  void completed_(bool ok)
  {
    finish_(ok ? 201 : 409,
      ok ? ZuCSpan{} : ZuCSpan{"concurrent or duplicate action"});
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  AppID		m_appID = 0;
  NameInput	m_input;
  AdminDoneFn	m_complete;
  ActionID	m_actionID = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

class AppEnroll_ : public ZumPolymorph {
public:
  AppEnroll_(DB *db, DBContext *context, Ztls::Random *rng, Issuer issuer,
      AppInput input, AdminDoneFn complete) : m_db{db}, m_context{context},
    m_rng{rng}, m_issuer{ZuMv(issuer)}, m_input{ZuMv(input)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    uint8_t random[50];
    bool nativeService = m_input.integration == "nativeService";
    bool oidc = m_input.integration == "oidc";
    if (!m_input.clientType)
      m_input.clientType = nativeService ? "confidential" : "browser";
    ClientType::T clientType = ClientType::Browser;
    if (m_input.clientType == "browser") clientType = ClientType::Browser;
    else if (m_input.clientType == "native") clientType = ClientType::Native;
    else if (m_input.clientType == "confidential")
      clientType = ClientType::Confidential;
    else oidc = nativeService = false;
    bool confidential = clientType == ClientType::Confidential;
    if (!m_context || !m_rng || !m_issuer.coreAppID ||
	!m_input.name || (!nativeService && !oidc) ||
	(nativeService && (!m_input.audienceURI || !confidential)) ||
	(oidc && !m_input.redirectURIs) ||
	!randomID(*m_rng, m_appID) ||
	(m_input.audienceURI && !randomID(*m_rng, m_audienceID)) ||
	!m_rng->random(random)) {
      finish_(400, "invalid application enrollment");
      return;
    }
    m_clientID = nativeService ? "svc_" : "oidc_";
    m_clientID << encode({random, 18});
    if (confidential) {
      m_secret = encode({random + 18, 32});
      m_secretDigest.length(Ztls::SecretHash::Size, false);
      if (!Ztls::secretHash(*m_rng, ZuBSpan{m_secret}, m_secretDigest)) {
	finish_(503, "secret generation failed");
	return;
      }
    }
    int64_t now = Zm::now().sec();
    if (!m_input.label) m_input.label = m_input.name;
    Bytes randomID;
    randomID.length(sizeof(ZdbSagaID), false);
    if (!m_db || !m_rng->random(randomID)) {
      finish_(503, "saga ID generation failed");
      return;
    }
    ZdbSagaID sagaID;
    memcpy(&sagaID, randomID.data(), sizeof(sagaID));
    AppEnrollment enrollment{.coreAppID = m_issuer.coreAppID,
      .appID = m_appID, .appName = ZuMv(m_input.name),
      .appLabel = ZuMv(m_input.label), .audienceID = m_audienceID,
      .audienceURI = ZuMv(m_input.audienceURI), .clientID = m_clientID,
      .secretDigest = ZuMv(m_secretDigest),
      .redirects = ZuMv(m_input.redirectURIs), .clientType = clientType,
      .nativeService = nativeService, .created = now,
      .catalogPublishOp = MgmtOp::catalogPublish,
      .operationQueryOp = MgmtOp::operationQuery};
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(enrollment));
    if (!sagaSubmit(m_db, sagaID, ZuMv(saga),
        [self = ZmRef<AppEnroll_>{this}](bool ok) mutable {
          if (!ok) self->finish_(503, "application enrollment failed");
        }, [self = ZmRef<AppEnroll_>{this}](bool ok) mutable {
          self->finish_(ok ? 201 : 503,
            ok ? ZuCSpan{} : ZuCSpan{"application enrollment failed"});
        })) finish_(503, "application enrollment unavailable");
  }

private:
  void finish_(unsigned status, ZuCSpan message)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    if (status != 201) {
      if (m_secret && m_secret.mutable_())
	ZuClear(m_secret.data(), m_secret.length());
      String body{"{\"error\":"};
      ZfJSON::quote(body, status == 409 ? "conflict" :
	status == 400 ? "invalid_request" : "unavailable");
      body << ",\"message\":";
      ZfJSON::quote(body, message);
      body << ",\"correlationID\":\"\"}";
      complete(AdminResult{ZuMv(body), status});
      return;
    }
    String body{"{\"item\":{\"appID\":\""};
    body << m_appID << "\",\"client_id\":";
    ZfJSON::quote(body, m_clientID);
    if (m_secret) {
      body << ",\"client_secret\":";
      ZfJSON::quote(body, m_secret);
    }
    body << ",\"issuer\":";
    ZfJSON::quote(body, m_issuer.id);
    body << ",\"etag\":";
    ZfJSON::quote(body, rowETag(2));
    body << "}}";
    ZuClear(m_secret.data(), m_secret.length());
    m_secret.null();
    StringVec ids;
    String id;
    id << m_appID;
    ids.push(ZuMv(id));
    ids.push(m_clientID);
    if (m_audienceID) {
      id.null();
      id << m_audienceID;
      ids.push(ZuMv(id));
    }
    complete(AdminResult{ZuMv(body), 201, ZuMv(ids)});
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Issuer	m_issuer;
  AppInput	m_input;
  AdminDoneFn	m_complete;
  AppID		m_appID = 0;
  AudienceID	m_audienceID = 0;
  String	m_clientID;
  String	m_secret;
  Bytes		m_secretDigest;
  bool		m_done = false;
};

class CatalogPublish_ : public ZumPolymorph {
public:
  CatalogPublish_(DBContext *context, Ztls::Random *rng, AppID appID,
      CatalogInput input, String ifMatch, AdminDoneFn complete) :
    m_context{context}, m_rng{rng}, m_appID{appID}, m_input{ZuMv(input)},
    m_ifMatch{ZuMv(ifMatch)}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_rng || !m_appID || !m_input.revision ||
	!m_input.digest || !validate_()) {
      fail_(400, "invalid_request", "invalid catalog manifest");
      return;
    }
    m_digest.length(ZuBase64URL::declen(m_input.digest.length()), false);
    if (ZuBase64URL::decode(m_digest, ZuBSpan{m_input.digest}) !=
	m_digest.length() || m_digest.length() != 32) {
      fail_(400, "invalid_request", "catalog digest must be SHA-256");
      return;
    }
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<CatalogPublish_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
	  ZdbRowRef<App> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner) {
	  self->fail_(404, "not_found", "application not found");
	  return;
	}
	self->m_app = row->data();
	if (self->m_input.revision == self->m_app.catalogRevision &&
	    Ztls::ctEqual(self->m_digest, self->m_app.catalogDigest)) {
	  self->success_();
	  return;
	}
	String expected{"\"catalog-"};
	expected << self->m_app.catalogRevision << '"';
	if (self->m_input.revision <= self->m_app.catalogRevision) {
	  self->fail_(409, "conflict", "catalog revision conflict");
	  return;
	}
	if (!self->m_ifMatch) {
	  self->fail_(428, "precondition_required",
	    "catalog If-Match is required");
	  return;
	}
	if (self->m_ifMatch != expected) {
	  self->fail_(412, "precondition_failed", "catalog ETag mismatch");
	  return;
	}
	self->m_nextActionID = self->m_app.nextActionID;
	self->m_actionIDs.length(self->m_input.actions.length());
	self->m_roleIDs.length(self->m_input.roles.length());
	self->action_();
      });
    });
  }

private:
  bool validate_() const
  {
    for (unsigned i = 0; i < m_input.actions.length(); ++i) {
      if (!m_input.actions[i].name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (m_input.actions[i].name == m_input.actions[j].name) return false;
    }
    for (unsigned i = 0; i < m_input.roles.length(); ++i) {
      const auto &role = m_input.roles[i];
      if (!role.name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (role.name == m_input.roles[j].name) return false;
      for (unsigned a = 0; a < role.actions.length(); ++a) {
	bool found = false;
	for (unsigned j = 0; j < m_input.actions.length(); ++j)
	  if (role.actions[a] == m_input.actions[j].name) {
	    found = true; break;
	  }
	if (!found) return false;
	for (unsigned j = 0; j < a; ++j)
	  if (role.actions[a] == role.actions[j]) return false;
      }
    }
    for (unsigned i = 0; i < m_input.scopes.length(); ++i) {
      const auto &scope = m_input.scopes[i];
      if (!scope.audienceID || !scope.name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (scope.audienceID == m_input.scopes[j].audienceID &&
	    scope.name == m_input.scopes[j].name) return false;
      for (unsigned r = 0; r < scope.roles.length(); ++r) {
	bool found = false;
	for (unsigned j = 0; j < m_input.roles.length(); ++j)
	  if (scope.roles[r] == m_input.roles[j].name) {
	    found = true; break;
	  }
	if (!found) return false;
	for (unsigned j = 0; j < r; ++j)
	  if (scope.roles[r] == scope.roles[j]) return false;
      }
    }
    return true;
  }

  void fail_(unsigned status, ZuCSpan error, ZuCSpan message)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(adminErrorResult(status, error, message));
  }

  void success_()
  {
    if (m_done) return;
    m_done = true;
    String json{"{\"item\":{\"appID\":\""};
    json << m_appID << "\",\"revision\":\"" << m_input.revision <<
      "\",\"etag\":\"catalog-" << m_input.revision << "\"}}";
    StringVec ids;
    String id;
    id << m_appID;
    ids.push(ZuMv(id));
    auto complete = ZuMv(m_complete);
    complete(AdminResult{ZuMv(json), 200, ZuMv(ids)});
  }

  int actionIndex_(ZuCSpan name) const
  {
    for (unsigned i = 0; i < m_input.actions.length(); ++i)
      if (m_input.actions[i].name == name) return int(i);
    return -1;
  }

  int roleIndex_(ZuCSpan name) const
  {
    for (unsigned i = 0; i < m_input.roles.length(); ++i)
      if (m_input.roles[i].name == name) return int(i);
    return -1;
  }

  void action_()
  {
    if (m_offset >= m_input.actions.length()) {
      m_offset = 0;
      role_();
      return;
    }
    unsigned index = m_offset++;
    auto table = m_context->actions;
    String name = m_input.actions[index].name;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name)]() mutable {
      table->findUpd<1>(0, ZuFwdTuple(self->m_appID, ZuMv(name)), [
	  self = ZuMv(self), table, index](ZdbRow<Action> *row) mutable {
	if (row) {
	  auto &action = row->data();
	  if (action.origin != Origin::Standard || action.owner) {
	    self->fail_(409, "conflict", "catalog action name is reserved");
	    return;
	  }
	  self->m_actionIDs[index] = action.id;
	  action.label = self->m_input.actions[index].label;
	  if (!action.label) action.label = action.name;
	  action.catalogRevision = self->m_input.revision;
	  if (action.tombstone) {
	    action.tombstone = false;
	    action.state = State::Active;
	  }
	  ++action.version;
	  action.updated = Zm::now().sec();
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog action commit failed");
	    return;
	  }
	  self->action_();
	  return;
	}
	if (self->m_nextActionID == UINT32_MAX) {
	  self->fail_(409, "conflict", "application action space exhausted");
	  return;
	}
	int64_t now = Zm::now().sec();
	Action action{.appID = self->m_appID,
	  .id = self->m_nextActionID++,
	  .name = self->m_input.actions[index].name,
	  .label = self->m_input.actions[index].label,
	  .state = State::Active, .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	if (!action.label) action.label = action.name;
	self->m_actionIDs[index] = action.id;
	ZdbRowRef<Action> inserted = new ZdbRow<Action>{table, ZdbShard{0}};
	table->insert(ZuMv(inserted), [self = ZuMv(self),
	    action = ZuMv(action)](ZdbRow<Action> *row) mutable {
	  if (!row) {
	    self->fail_(409, "conflict", "catalog action exists");
	    return;
	  }
	  new (row->ptr()) Action{ZuMv(action)};
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog action commit failed");
	    return;
	  }
	  self->action_();
	});
      });
    });
  }

  void role_()
  {
    if (m_offset >= m_input.roles.length()) {
      m_offset = 0;
      scope_();
      return;
    }
    unsigned index = m_offset++;
    ZtBitmap actions;
    actions.length(m_nextActionID);
    for (auto &name: m_input.roles[index].actions) {
      int action = actionIndex_(name);
      if (action < 0) {
	fail_(400, "invalid_request", "role action is unavailable");
	return;
      }
      actions.set(m_actionIDs[action]);
    }
    auto table = m_context->roles;
    String name = m_input.roles[index].name;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name), actions = ZuMv(actions)]() mutable {
      table->findUpd<1>(0, ZuFwdTuple(self->m_appID, ZuMv(name)), [
	  self = ZuMv(self), table, index, actions = ZuMv(actions)](
	    ZdbRow<Role> *row) mutable {
	if (row) {
	  auto &role = row->data();
	  if (role.origin != Origin::Standard || role.owner) {
	    self->fail_(409, "conflict", "catalog role name is reserved");
	    return;
	  }
	  self->m_roleIDs[index] = role.id;
	  role.label = self->m_input.roles[index].label;
	  if (!role.label) role.label = role.name;
	  role.actions = ZuMv(actions);
	  role.catalogRevision = self->m_input.revision;
	  if (role.tombstone) {
	    role.tombstone = false;
	    role.state = State::Active;
	  }
	  ++role.version;
	  role.updated = Zm::now().sec();
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog role commit failed");
	    return;
	  }
	  self->role_();
	  return;
	}
	uint64_t id;
	if (!randomID(*self->m_rng, id)) {
	  self->fail_(503, "unavailable", "role ID generation failed");
	  return;
	}
	int64_t now = Zm::now().sec();
	Role role{.appID = self->m_appID, .id = id,
	  .name = self->m_input.roles[index].name,
	  .label = self->m_input.roles[index].label,
	  .actions = ZuMv(actions), .state = State::Active,
	  .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	if (!role.label) role.label = role.name;
	self->m_roleIDs[index] = id;
	ZdbRowRef<Role> inserted = new ZdbRow<Role>{table, ZdbShard{0}};
	table->insert(ZuMv(inserted), [self = ZuMv(self), role = ZuMv(role)](
	    ZdbRow<Role> *row) mutable {
	  if (!row) {
	    self->fail_(409, "conflict", "catalog role exists");
	    return;
	  }
	  new (row->ptr()) Role{ZuMv(role)};
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog role commit failed");
	    return;
	  }
	  self->role_();
	});
      });
    });
  }

  void scope_()
  {
    if (m_offset >= m_input.scopes.length()) { finish_(); return; }
    unsigned index = m_offset++;
    const auto &input = m_input.scopes[index];
    IDVec roleIDs;
    for (auto &name: input.roles) {
      int role = roleIndex_(name);
      if (role < 0) {
	fail_(400, "invalid_request", "scope role is unavailable");
	return;
      }
      roleIDs.push(m_roleIDs[role]);
    }
    auto audiences = m_context->audiences;
    audiences->run(0, [self = ZmRef<CatalogPublish_>{this}, audiences,
	index, roleIDs = ZuMv(roleIDs)]() mutable {
      audiences->find<0>(0,
	ZuFwdTuple(self->m_input.scopes[index].audienceID), [self = ZuMv(self),
	  index, roleIDs = ZuMv(roleIDs)](ZdbRowRef<Audience> audience) mutable {
	if (!audience || audience->data().appID != self->m_appID ||
	    audience->data().state != State::Active || audience->data().owner) {
	  self->fail_(400, "invalid_request", "invalid catalog audience");
	  return;
	}
	self->scopeWrite_(index, audience->data().uri, ZuMv(roleIDs));
      });
    });
  }

  void scopeWrite_(unsigned index, String audience, IDVec roleIDs)
  {
    auto table = m_context->scopes;
    String name = m_input.scopes[index].name;
    AudienceID audienceID = m_input.scopes[index].audienceID;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name), audience = ZuMv(audience), audienceID,
	roleIDs = ZuMv(roleIDs)]() mutable {
      table->findUpd<1>(0, ZuFwdTuple(self->m_appID, audienceID,
	ZuMv(name)), [self = ZuMv(self), table, index,
	  audience = ZuMv(audience), audienceID,
	  roleIDs = ZuMv(roleIDs)](ZdbRow<Scope> *row) mutable {
	if (row) {
	  auto &scope = row->data();
	  if (scope.origin != Origin::Standard || scope.owner) {
	    self->fail_(409, "conflict", "catalog scope name is reserved");
	    return;
	  }
	  scope.roleIDs = ZuMv(roleIDs);
	  scope.catalogRevision = self->m_input.revision;
	  ++scope.version;
	  scope.updated = Zm::now().sec();
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog scope commit failed");
	    return;
	  }
	  self->scope_();
	  return;
	}
	uint64_t id;
	if (!randomID(*self->m_rng, id)) {
	  self->fail_(503, "unavailable", "scope ID generation failed");
	  return;
	}
	int64_t now = Zm::now().sec();
	Scope scope{.appID = self->m_appID, .id = id,
	  .audienceID = audienceID, .audience = ZuMv(audience),
	  .name = self->m_input.scopes[index].name,
	  .roleIDs = ZuMv(roleIDs), .state = State::Active,
	  .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	ZdbRowRef<Scope> inserted = new ZdbRow<Scope>{table, ZdbShard{0}};
	table->insert(ZuMv(inserted), [self = ZuMv(self), scope = ZuMv(scope)](
	    ZdbRow<Scope> *row) mutable {
	  if (!row) {
	    self->fail_(409, "conflict", "catalog scope exists");
	    return;
	  }
	  new (row->ptr()) Scope{ZuMv(scope)};
	  if (!row->commit()) {
	    self->fail_(503, "unavailable", "catalog scope commit failed");
	    return;
	  }
	  self->scope_();
	});
      });
    });
  }

  void finish_()
  {
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<CatalogPublish_>{this}, apps]() mutable {
      apps->findUpd<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
	  ZdbRow<App> *row) mutable {
	if (!row || row->data().catalogRevision !=
	    self->m_app.catalogRevision || row->data().owner) {
	  self->fail_(409, "conflict", "concurrent catalog publication");
	  return;
	}
	row->data().nextActionID = self->m_nextActionID;
	row->data().catalogRevision = self->m_input.revision;
	row->data().catalogDigest = ZuMv(self->m_digest);
	++row->data().authVersion;
	++row->data().version;
	row->data().updated = Zm::now().sec();
	if (!row->commit()) {
	  self->fail_(503, "unavailable", "catalog publication failed");
	  return;
	}
	self->success_();
      });
    });
  }

  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  AppID		m_appID = 0;
  CatalogInput	m_input;
  String	m_ifMatch;
  AdminDoneFn	m_complete;
  Bytes		m_digest;
  App		m_app;
  ActionID	m_nextActionID = 0;
  ActionIDVec	m_actionIDs;
  IDVec		m_roleIDs;
  unsigned	m_offset = 0;
  bool		m_done = false;
};

String Daemon::error_(ZuCSpan error, ZuCSpan message)
{
  String json{"{\"error\":"};
  ZfJSON::quote(json, error);
  json << ",\"message\":";
  ZfJSON::quote(json, message);
  json << ",\"correlationID\":\"\"}";
  return json;
}

String Daemon::correlation_()
{
  Bytes random;
  random.length(16, false);
  if (!m_rng.random(random)) return {};
  String id;
  id.length(ZuBase64URL::enclen(random.length()));
  id.length(ZuBase64URL::encode(id.span(), random));
  ZuClear(random.data(), random.length());
  return id;
}

String Daemon::correlate_(String json, ZuCSpan id)
{
  if (!id) return json;
  static constexpr ZuCSpan prefix{"\"correlationID\":\""};
  auto offset = json.find(prefix);
  if (offset < 0) return json;
  unsigned value = unsigned(offset) + prefix.length();
  String next;
  next << ZuCSpan{json.data(), value} << id <<
    ZuCSpan{json.data() + value, json.length() - value};
  return next;
}

void Daemon::adminAudit_(int op, String actor, AppID appID,
    String target, String correlationID, AdminResult result,
    AdminDoneFn complete)
{
  if (!managementAudited(op)) {
    result.body = correlate_(ZuMv(result.body), correlationID);
    complete(ZuMv(result));
    return;
  }
  Audit audit = managementAuditRecord(m_config.issuer, op, ZuMv(actor), appID,
    ZuMv(target), String{correlationID}, result.status, Zm::now().sec());
  auditWrite(m_context, ZuMv(audit), [result = ZuMv(result),
      correlationID = ZuMv(correlationID), complete = ZuMv(complete)](
        int error) mutable {
    if (error != AdminError::OK) {
      result = AdminResult{correlate_(error_("unavailable",
	"audit persistence failed"), correlationID), 503};
    } else {
      result.body = correlate_(ZuMv(result.body), correlationID);
    }
    complete(ZuMv(result));
  });
}

String Daemon::issuerJSON_(const Issuer &issuer)
{
  String json{"{\"items\":[{\"id\":"};
  ZfJSON::quote(json, issuer.id);
  json << ",\"schemaVersion\":" << issuer.schemaVersion <<
    ",\"coreAppID\":\"" << issuer.coreAppID <<
    "\",\"bootstrapPhase\":";
  ZfJSON::quote(json, BootstrapPhase::name(issuer.bootstrapPhase));
  json << "}]}";
  return json;
}

static String adminOperationCursorEncode(
    ZuBSpan secret, int operation, unsigned last)
{
  constexpr unsigned headerLength = 3;
  constexpr unsigned macLength = Ztls::HMAC<>::Size;
  Bytes raw;
  raw.length(headerLength + macLength, false);
  raw[0] = 1;
  raw[1] = uint8_t(operation);
  raw[2] = uint8_t(last);
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength});
  hmac.finish({raw.data() + headerLength, macLength});
  String encoded;
  encoded.length(ZuBase64URL::enclen(raw.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), raw));
  ZuClear(raw.data(), raw.length());
  return encoded;
}

static bool adminOperationCursorDecode(
    ZuBSpan secret, int operation, ZuCSpan encoded, unsigned &first)
{
  constexpr unsigned headerLength = 3;
  constexpr unsigned macLength = Ztls::HMAC<>::Size;
  Bytes raw;
  raw.length(ZuBase64URL::declen(encoded.length()), false);
  int length = ZuBase64URL::decode(raw, ZuBSpan{encoded});
  if (length != int(headerLength + macLength)) return false;
  raw.length(unsigned(length));
  uint8_t digest[macLength];
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength});
  hmac.finish(digest);
  bool valid = raw[0] == 1 && raw[1] == uint8_t(operation) &&
    raw[2] < MgmtOp::N && Ztls::ctEqual({digest, macLength},
      {raw.data() + headerLength, macLength});
  if (valid) first = unsigned(raw[2]) + 1;
  ZuClear(raw.data(), raw.length());
  return valid;
}

String Daemon::operationsJSON_(
    unsigned first, unsigned limit, ZuBSpan secret)
{
  String json{"{\"items\":["};
  unsigned end = first + limit;
  if (end > MgmtOp::N) end = MgmtOp::N;
  for (unsigned op = first; op < end; ++op) {
    const auto *route = managementRoute(op);
    if (op != first) json << ',';
    json << "{\"id\":" << op << ",\"name\":";
    ZfJSON::quote(json, MgmtOp::name(op));
    json << ",\"action\":";
    ZfJSON::quote(json, managementAction(op));
    json << ",\"method\":";
    ZfJSON::quote(json, Zhttp::Method::name(route->method));
    json << ",\"path\":";
    ZfJSON::quote(json, route->path);
    json << '}';
  }
  json << ']';
  if (end < MgmtOp::N) {
    json << ",\"nextCursor\":";
    ZfJSON::quote(json,
      adminOperationCursorEncode(secret, MgmtOp::operationQuery, end - 1));
  }
  json << '}';
  return json;
}

template <typename Record, typename Key>
static String adminCursorEncode(
    ZuBSpan secret, int operation, AppID appID, const Key &key)
{
  Zfb::IOBuilder fbb{new ZiIOBufAlloc<>()};
  fbb.Finish(ZfbStruct::save(fbb, key));
  unsigned dataLength = fbb.GetSize();
  constexpr unsigned headerLength = 10;
  constexpr unsigned macLength = Ztls::HMAC<>::Size;
  Bytes raw;
  raw.length(headerLength + dataLength + macLength, false);
  raw[0] = 1;
  raw[1] = uint8_t(operation);
  ZuBigEndian<uint64_t> app{appID};
  memcpy(raw.data() + 2, &app, sizeof(app));
  memcpy(raw.data() + headerLength, fbb.GetBufferPointer(), dataLength);
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength + dataLength});
  hmac.finish({raw.data() + headerLength + dataLength, macLength});
  String encoded;
  encoded.length(ZuBase64URL::enclen(raw.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), raw));
  ZuClear(raw.data(), raw.length());
  return encoded;
}

template <typename Table>
static bool adminCursorDecode(
    ZuBSpan secret, int operation, AppID appID, ZuCSpan encoded,
    typename Table::template Key<0> &key)
{
  constexpr unsigned headerLength = 10;
  constexpr unsigned macLength = Ztls::HMAC<>::Size;
  Bytes raw;
  raw.length(ZuBase64URL::declen(encoded.length()), false);
  int length = ZuBase64URL::decode(raw, ZuBSpan{encoded});
  if (length < int(headerLength + macLength + 1)) return false;
  raw.length(unsigned(length));
  ZuBigEndian<uint64_t> app;
  memcpy(static_cast<void *>(&app), raw.data() + 2, sizeof(app));
  if (raw[0] != 1 || raw[1] != uint8_t(operation) || uint64_t(app) != appID)
    return false;
  unsigned dataLength = raw.length() - headerLength - macLength;
  uint8_t digest[macLength];
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength + dataLength});
  hmac.finish(digest);
  if (!Ztls::ctEqual({digest, macLength},
      {raw.data() + headerLength + dataLength, macLength})) {
    ZuClear(raw.data(), raw.length());
    return false;
  }
  using Record = typename Table::T;
  auto fbo = ZfbStruct::verify<Record>(
    {raw.data() + headerLength, dataLength});
  if (!fbo) {
    ZuClear(raw.data(), raw.length());
    return false;
  }
  Record record{ZfbStruct::ctor<Record>(fbo)};
  key = ZuStructKey<0>(record);
  ZuClear(raw.data(), raw.length());
  return true;
}

template <typename Table, typename Match>
class AdminQuery_ : public ZumObject {
  using Tuple = typename Table::Tuple;
  using Record = typename Table::T;
  using Key = typename Table::template Key<0>;

public:
  AdminQuery_(Table *table, Match match, unsigned limit, String cursor,
      int operation, AppID appID, Bytes secret, AdminDoneFn complete) :
    m_table{table}, m_match{ZuMv(match)}, m_limit{limit},
    m_cursor{ZuMv(cursor)}, m_operation{operation}, m_appID{appID},
    m_secret{ZuMv(secret)}, m_complete{ZuMv(complete)} { }

  ~AdminQuery_()
  {
    if (m_secret.mutable_()) ZuClear(m_secret.data(), m_secret.length());
  }

  void start()
  {
    if (!m_table || !m_limit || !m_secret || !m_complete) {
      finish_(adminErrorResult(503, "unavailable", "query unavailable"));
      return;
    }
    m_table->run(0, [self = ZmRef<AdminQuery_>{this}]() mutable {
      if (self->m_cursor) {
        Key key;
        if (!adminCursorDecode<Table>(self->m_secret, self->m_operation,
            self->m_appID, self->m_cursor, key)) {
          self->finish_(adminErrorResult(
            400, "invalid_request", "invalid query cursor"));
          return;
        }
        self->m_scanKey = ZuMv(key);
        self->scanNext_();
      } else {
        self->scanFirst_();
      }
    });
  }

private:
  enum { ScanSize = 1001 };

  void scanFirst_()
  {
    m_rawCount = 0;
    m_table->template selectRows<0>({}, ScanSize,
      [self = ZmRef<AdminQuery_>{this}](ZuUnion<void, Tuple> result,
          unsigned count) mutable { self->receive_(ZuMv(result), count); });
  }

  void scanNext_()
  {
    m_rawCount = 0;
    m_table->template nextRows<0>(m_scanKey, false, ScanSize,
      [self = ZmRef<AdminQuery_>{this}](ZuUnion<void, Tuple> result,
          unsigned count) mutable { self->receive_(ZuMv(result), count); });
  }

  void receive_(ZuUnion<void, Tuple> result, unsigned count)
  {
    if (m_done) return;
    if (result.template is<Tuple>()) {
      auto tuple = ZuMv(result).template p<Tuple>();
      m_rawCount = count;
      m_scanKey = ZuStructKey<0>(tuple);
      if (!m_match(tuple)) return;
      if (m_count >= m_limit) { m_more = true; return; }
      m_lastKey = m_scanKey;
      if (m_count++) m_json << ',';
      String encoded;
      ZfJSON::AsObject::Handler<Tuple, ZuFacet::JSON>::
        template save<PublicField>(encoded, tuple);
      if constexpr (HasVersion<Record>{}) {
        constexpr unsigned version =
          ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Record>>{};
        encoded.length(encoded.length() - 1);
        encoded << ",\"etag\":";
        ZfJSON::quote(encoded, rowETag(tuple.template p<version>()));
        encoded << '}';
      }
      m_json << encoded;
      return;
    }
    if (!m_more && m_rawCount == ScanSize) { scanNext_(); return; }
    m_json << ']';
    if (m_more) {
      m_json << ",\"nextCursor\":";
      ZfJSON::quote(m_json, adminCursorEncode<Record>(m_secret,
        m_operation, m_appID, m_lastKey));
    }
    m_json << '}';
    finish_(AdminResult{ZuMv(m_json), 200});
  }

  void finish_(AdminResult result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ZuMv(result));
  }

  Table		*m_table = nullptr;
  Match		m_match;
  unsigned	m_limit = 0;
  String	m_cursor;
  int		m_operation = -1;
  AppID		m_appID = 0;
  Bytes		m_secret;
  AdminDoneFn	m_complete;
  Key		m_scanKey;
  Key		m_lastKey;
  String	m_json{"{\"items\":["};
  unsigned	m_count = 0;
  unsigned	m_rawCount = 0;
  bool		m_more = false;
  bool		m_done = false;
};

template <typename Table, typename Match>
static void adminQueryIf(Table *table, Match match, unsigned limit,
    String cursor, int operation, AppID appID, Bytes secret,
    AdminDoneFn complete)
{
  ZmRef<AdminQuery_<Table, Match>> query = new AdminQuery_<Table, Match>{
    table, ZuMv(match), limit, ZuMv(cursor), operation, appID, ZuMv(secret),
    ZuMv(complete)};
  query->start();
}

template <typename Table>
static void adminQuery(Table *table, unsigned limit, String cursor,
    int operation, Bytes secret, AdminDoneFn complete)
{
  adminQueryIf(table, [](const auto &) { return true; }, limit,
    ZuMv(cursor), operation, 0, ZuMv(secret), ZuMv(complete));
}

template <unsigned KeyID, typename Table>
static void adminFind(Table *table, typename Table::template Key<KeyID> key,
    AdminDoneFn complete)
{
  table->run(0, [table, key = ZuMv(key),
      complete = ZuMv(complete)]() mutable {
    using Record = typename Table::T;
    table->template find<KeyID>(0, ZuMv(key), [
        complete = ZuMv(complete)](ZdbRowRef<Record> row) mutable {
      String json{"{\"items\":["};
      if (row) {
	String encoded;
	ZfJSON::AsObject::Handler<Record, ZuFacet::JSON>::
	  template save<PublicField>(encoded, row->data());
	if constexpr (HasVersion<Record>{}) {
	  encoded.length(encoded.length() - 1);
	  encoded << ",\"etag\":";
	  String etag = rowETag(row->data().version);
	  ZfJSON::quote(encoded, etag);
	  encoded << '}';
	}
	json << encoded;
      }
      json << "]}";
      complete(AdminResult{ZuMv(json), 200});
    });
  });
}

template <unsigned Field, typename Table>
static void adminQueryApp(
    Table *table, AppID appID, unsigned limit, String cursor,
    int operation, Bytes secret, AdminDoneFn complete)
{
  adminQueryIf(table, [appID](const auto &tuple) {
    return tuple.template p<Field>() == appID;
  }, limit, ZuMv(cursor), operation, appID, ZuMv(secret), ZuMv(complete));
}

bool Daemon::adminTargetApp(ZuCSpan target, AppID &appID)
{
  constexpr ZuCSpan prefix{"/admin/apps/"};
  if (target.prefix(prefix) != prefix.length()) return false;
  target.offset(prefix.length());
  auto slash = target.find<"/">();
  if (slash >= 0) target = {target.data(), unsigned(slash)};
  ZuBox<uint64_t> parsed{target};
  appID = parsed;
  return appID;
}

static String rowETag(uint64_t version)
{
  String etag{"\"v"};
  etag << version << '"';
  return etag;
}

static AdminResult adminErrorResult(
    unsigned status, ZuCSpan error, ZuCSpan message)
{
  String json{"{\"error\":"};
  ZfJSON::quote(json, error);
  json << ",\"message\":";
  ZfJSON::quote(json, message);
  json << ",\"correlationID\":\"\"}";
  return AdminResult{ZuMv(json), status};
}

template <typename Record>
static AdminResult adminItemResult(
    const Record &record, unsigned status, StringVec ids = {})
{
  String item;
  ZfJSON::AsObject::Handler<Record, ZuFacet::JSON>::
    template save<PublicField>(item, record);
  if constexpr (HasVersion<Record>{}) {
    item.length(item.length() - 1);
    item << ",\"etag\":";
    ZfJSON::quote(item, rowETag(record.version));
    item << '}';
  }
  String json{"{\"item\":"};
  json << item << '}';
  return AdminResult{ZuMv(json), status, ZuMv(ids)};
}

static bool stateBody(String &body, State::T &state)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  bool seen = false;
  State::T next = State::T(-1);
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    if (field.p<0>() != "state" || seen) return false;
    auto value = field.p<1>().ptr();
    if (!value->has<ZfJSON::AnyNode::String>()) return false;
    next = State::lookup(value->data<ZfJSON::AnyNode::String>());
    seen = true;
  }
  if (!seen || next < 0 || next >= State::N ||
      next == State::Pending || next == State::Consumed) return false;
  state = next;
  return true;
}

template <typename Table>
static void adminState(Table *table, typename Table::template Key<0> key,
    String body, String ifMatch,
    bool user, AdminDoneFn complete)
{
  State::T state;
  if (!stateBody(body, state)) {
    complete(AdminResult{
      "{\"error\":\"invalid_request\",\"message\":\"invalid state body\","
      "\"correlationID\":\"\"}", 400});
    return;
  }
  if (!ifMatch) {
    complete(AdminResult{
      "{\"error\":\"precondition_required\",\"message\":\"If-Match is required\","
      "\"correlationID\":\"\"}", 428});
    return;
  }
  table->run(0, [table, key = ZuMv(key),
      ifMatch = ZuMv(ifMatch), state, user,
      complete = ZuMv(complete)]() mutable {
    table->template findUpd<0>(0, ZuMv(key), [ifMatch = ZuMv(ifMatch),
        state, user, complete = ZuMv(complete)](
          ZdbRow<typename Table::T> *row) mutable {
      if (!row) {
	complete(AdminResult{
	  "{\"error\":\"not_found\",\"message\":\"target not found\","
	  "\"correlationID\":\"\"}", 404});
	return;
      }
      auto &item = row->data();
      if (ifMatch != rowETag(item.version)) {
	complete(AdminResult{
	  "{\"error\":\"precondition_failed\",\"message\":\"ETag mismatch\","
	  "\"correlationID\":\"\"}", 412});
	return;
      }
      bool owned = false;
      if constexpr (HasOwner<typename Table::T>{}) owned = bool(item.owner);
      if (owned || item.state == State::Revoked ||
          (user && item.state == State::Pending && state == State::Active)) {
	complete(AdminResult{
	  "{\"error\":\"conflict\",\"message\":\"invalid state transition\","
	  "\"correlationID\":\"\"}", 409});
	return;
      }
      if (item.state != state) {
	bool exhausted = item.version == UINT64_MAX;
	if constexpr (HasAuthVer<typename Table::T>{})
	  exhausted |= item.authVersion == UINT64_MAX;
	if (exhausted) {
	  complete(adminErrorResult(409, "conflict", "version exhausted"));
	  return;
	}
	item.state = state;
	++item.version;
	if constexpr (HasAuthVer<typename Table::T>{}) ++item.authVersion;
	item.updated = Zm::now().sec();
	if (!row->commit()) {
	  complete(AdminResult{
	    "{\"error\":\"unavailable\",\"message\":\"commit failed\","
	    "\"correlationID\":\"\"}", 503});
	  return;
	}
      }
      String json{"{\"item\":{\"etag\":"};
      ZfJSON::quote(json, rowETag(item.version));
      json << "}}";
      complete(AdminResult{ZuMv(json), 200});
    });
  });
}

template <typename Table, typename Update>
static void adminUpdate(Table *table, typename Table::template Key<0> key,
    String ifMatch,
    Update update, AdminDoneFn complete)
{
  if (!ifMatch) {
    complete(AdminResult{
      "{\"error\":\"precondition_required\",\"message\":\"If-Match is required\","
      "\"correlationID\":\"\"}", 428});
    return;
  }
  table->run(0, [table, key = ZuMv(key), ifMatch = ZuMv(ifMatch),
      update = ZuMv(update), complete = ZuMv(complete)]() mutable {
    table->template findUpd<0>(0, ZuMv(key), [ifMatch = ZuMv(ifMatch),
        update = ZuMv(update), complete = ZuMv(complete)](
          ZdbRow<typename Table::T> *row) mutable {
      if (!row) {
	complete(AdminResult{
	  "{\"error\":\"not_found\",\"message\":\"target not found\","
	  "\"correlationID\":\"\"}", 404});
	return;
      }
      auto &item = row->data();
      if (ifMatch != rowETag(item.version)) {
	complete(AdminResult{
	  "{\"error\":\"precondition_failed\",\"message\":\"ETag mismatch\","
	  "\"correlationID\":\"\"}", 412});
	return;
      }
      bool owned = false;
      if constexpr (HasOwner<typename Table::T>{}) owned = bool(item.owner);
      if (owned || !update(item)) {
	complete(AdminResult{
	  "{\"error\":\"conflict\",\"message\":\"update is not permitted\","
	  "\"correlationID\":\"\"}", 409});
	return;
      }
      ++item.version;
      item.updated = Zm::now().sec();
      if (!row->commit()) {
	complete(AdminResult{
	  "{\"error\":\"unavailable\",\"message\":\"commit failed\","
	  "\"correlationID\":\"\"}", 503});
	return;
      }
      String json{"{\"item\":{\"etag\":"};
      ZfJSON::quote(json, rowETag(item.version));
      json << "}}";
      complete(AdminResult{ZuMv(json), 200});
    });
  });
}

template <typename Table, typename Record, typename Replace>
static void adminPut(Table *table, typename Table::template Key<0> key, Record value,
    String ifMatch, String ifNoneMatch, Replace replace,
    AdminDoneFn complete)
{
  table->run(0, [table, key = ZuMv(key), value = ZuMv(value),
      ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
      replace = ZuMv(replace), complete = ZuMv(complete)]() mutable {
    table->template findUpd<0>(0, ZuMv(key), [table,
	value = ZuMv(value), ifMatch = ZuMv(ifMatch),
	ifNoneMatch = ZuMv(ifNoneMatch), replace = ZuMv(replace),
	complete = ZuMv(complete)](ZdbRow<typename Table::T> *row) mutable {
      if (row) {
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required",
	    "If-Match is required"));
	  return;
	}
	if (ifMatch != rowETag(row->data().version)) {
	  complete(adminErrorResult(412, "precondition_failed",
	    "ETag mismatch"));
	  return;
	}
	bool owned = false;
	if constexpr (HasOwner<typename Table::T>{})
	  owned = bool(row->data().owner);
	if (owned || !replace(row->data(), ZuMv(value))) {
	  complete(adminErrorResult(409, "conflict",
	    "replacement is not permitted"));
	  return;
	}
	++row->data().version;
	row->data().updated = Zm::now().sec();
	if (!row->commit()) {
	  complete(adminErrorResult(503, "unavailable", "commit failed"));
	  return;
	}
	complete(adminItemResult(row->data(), 200));
	return;
      }
      if (ifNoneMatch != "*") {
	complete(adminErrorResult(428, "precondition_required",
	  "If-None-Match: * is required for creation"));
	return;
      }
      auto inserted = ZdbRowRef<typename Table::T>{
	new ZdbRow<typename Table::T>{table, ZdbShard{0}}};
      table->insert(ZuMv(inserted), [value = ZuMv(value),
	  complete = ZuMv(complete)](ZdbRow<typename Table::T> *created) mutable {
	if (!created) {
	  complete(adminErrorResult(409, "conflict", "target already exists"));
	  return;
	}
	new (created->ptr()) typename Table::T{ZuMv(value)};
	if (!created->commit()) {
	  complete(adminErrorResult(503, "unavailable", "commit failed"));
	  return;
	}
	complete(adminItemResult(created->data(), 201));
      });
    });
  });
}

static bool pathValue(
    ZuCSpan target, ZuCSpan prefix, ZuCSpan suffix, String &value)
{
  if (target.prefix(prefix) != prefix.length() ||
      target.length() <= prefix.length() + suffix.length()) return false;
  target.offset(prefix.length());
  if (suffix) {
    if (target.length() <= suffix.length()) return false;
    ZuCSpan tail{target.data() + target.length() - suffix.length(),
      suffix.length()};
    if (tail != suffix) return false;
  }
  if (suffix)
    target = {target.data(), target.length() - suffix.length()};
  if (!target || target.find<"/">() >= 0) return false;
  String decoded{target};
  auto result = ZfURI::eoc(decoded.span());
  if (result.p<0>() < 0 || result.p<1>() != int(target.length()) ||
      result.p<2>()) return false;
  decoded.length(unsigned(result.p<0>()));
  for (char c: decoded) if (!c) return false;
  value = ZuMv(decoded);
  return true;
}

static bool pathUInt(
    ZuCSpan target, ZuCSpan prefix, ZuCSpan suffix, uint64_t &value)
{
  String encoded;
  if (!pathValue(target, prefix, suffix, encoded)) return false;
  ZuBox<uint64_t> parsed{encoded};
  value = parsed;
  return value;
}

static bool pathBytes(
    ZuCSpan target, ZuCSpan prefix, ZuCSpan suffix, Bytes &value)
{
  String encoded;
  if (!pathValue(target, prefix, suffix, encoded)) return false;
  Bytes next;
  next.length(ZuBase64URL::declen(encoded.length()), false);
  if (ZuBase64URL::decode(next, ZuBSpan{encoded}) != next.length() || !next)
    return false;
  value = ZuMv(next);
  return true;
}

void Daemon::adminCall_(int op, Principal principal, AdminPermit permit,
    String target, String query, String body,
    String ifMatch, String ifNoneMatch, String idempotencyKey,
    AdminDoneFn complete)
{
  QueryInput queryInput;
  if (!adminQueryInput(ZuMv(query), queryInput)) {
    complete(AdminResult{
      error_("invalid_request", "invalid query parameters"), 400});
    return;
  }
  if (!adminQueryFields(op, queryInput.seen)) {
    complete(AdminResult{
      error_("invalid_request", "unsupported query filter"), 400});
    return;
  }
  uint32_t filters = queryInput.seen &
    ~((1U<<QueryInput::Limit) | (1U<<QueryInput::Cursor));
  auto one = [](uint32_t value) { return !value || !(value & (value - 1)); };
  bool filterShape = one(filters);
  if (op == MgmtOp::operationQuery)
    filterShape = !filters || filters == ((1U<<QueryInput::Operation) |
      (1U<<QueryInput::IdempotencyKey));
  else if (op == MgmtOp::userQuery)
    filterShape = !filters || filters == (1U<<QueryInput::ID) ||
      filters == (1U<<QueryInput::Name) ||
      filters == ((1U<<QueryInput::Name) | (1U<<QueryInput::Source));
  else if (op == MgmtOp::scopeQuery)
    filterShape = !filters || filters == (1U<<QueryInput::ID) ||
      filters == ((1U<<QueryInput::AudienceID_) | (1U<<QueryInput::Name));
  if (!filterShape) {
    complete(AdminResult{
      error_("invalid_request", "conflicting query filters"), 400});
    return;
  }
  switch (op) {
    case MgmtOp::issuerQuery: {
      auto issuers = m_context->issuers;
      issuers->run(0, [this, issuers,
          complete = ZuMv(complete)]() mutable {
	String issuer = m_config.issuer;
	issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [
	    complete = ZuMv(complete)](ZdbRowRef<Issuer> row) mutable {
	  if (!row) {
	    complete(AdminResult{
	      error_("not_found", "issuer not found"), 404});
	    return;
	  }
	  complete(AdminResult{issuerJSON_(row->data()), 200});
	});
      });
      return;
    }
    case MgmtOp::operationQuery:
      if (queryInput.idempotencyKey && queryInput.operation) {
	int operation = MgmtOp::lookup(queryInput.operation);
	if (operation < 0 || operation >= MgmtOp::N) {
	  uint64_t id = ZuBox<uint64_t>{queryInput.operation};
	  operation = id < MgmtOp::N ? int(id) : -1;
	}
	if (operation < 0) {
	  complete(AdminResult{
	    error_("invalid_request", "invalid operation filter"), 400});
	  return;
	}
	ActorKind::T kind = principal.authMethod ?
	  ActorKind::User : ActorKind::Client;
	adminFind<0>(m_context->requests, ZuFwdTuple(kind,
	  String{principal.subject}, ActionID(operation),
	  ZuMv(queryInput.idempotencyKey)), ZuMv(complete));
	return;
      }
      if (queryInput.idempotencyKey || queryInput.operation) {
	complete(AdminResult{
	  error_("invalid_request",
	    "operation and idempotencyKey must be supplied together"), 400});
	return;
      }
      {
	unsigned first = 0;
	if (queryInput.cursor && !adminOperationCursorDecode(m_config.dbKey,
	    op, queryInput.cursor, first)) {
	  complete(AdminResult{
	    error_("invalid_request", "invalid query cursor"), 400});
	  return;
	}
	complete(AdminResult{operationsJSON_(first, queryInput.limit,
	  m_config.dbKey), 200});
	return;
      }
    case MgmtOp::appQuery:
      if (queryInput.id) {
	uint64_t id = ZuBox<uint64_t>{queryInput.id};
	if (!id) break;
	adminFind<0>(m_context->apps, ZuFwdTuple(AppID{id}),
	  ZuMv(complete));
	return;
      }
      if (queryInput.name) {
	adminFind<1>(m_context->apps, ZuFwdTuple(ZuMv(queryInput.name)),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->apps, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::userQuery:
      if (queryInput.id) {
	uint64_t id = ZuBox<uint64_t>{queryInput.id};
	if (!id) break;
	adminFind<0>(m_context->users, ZuFwdTuple(UserID{id}),
	  ZuMv(complete));
	return;
      }
      if (queryInput.name) {
	UserSource::T source = queryInput.source ?
	  UserSource::lookup(queryInput.source) : UserSource::Local;
	if (source < 0 || source >= UserSource::N) break;
	adminFind<2>(m_context->users,
	  ZuFwdTuple(source, ZuMv(queryInput.name)), ZuMv(complete));
	return;
      }
      adminQuery(m_context->users, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::credentialQuery:
      adminQuery(m_context->creds, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::membershipQuery:
    case MgmtOp::actionQuery:
    case MgmtOp::roleQuery:
    case MgmtOp::scopeQuery:
    case MgmtOp::clientAccessQuery:
    case MgmtOp::adminAccessQuery:
    case MgmtOp::roleMapQuery: {
      AppID appID;
      if (!adminTargetApp(target, appID)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid application path"), 400});
	return;
      }
      switch (op) {
	case MgmtOp::membershipQuery:
	  if (queryInput.userID) {
	    uint64_t id = ZuBox<uint64_t>{queryInput.userID};
	    if (!id) {
	      complete(AdminResult{
		error_("invalid_request", "invalid userID filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->memberships,
	      ZuFwdTuple(appID, UserID{id}), ZuMv(complete));
	    return;
	  }
	  adminQueryApp<0>(m_context->memberships, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::actionQuery:
	  if (queryInput.id) {
	    uint64_t id = ZuBox<uint64_t>{queryInput.id};
	    if (id > UINT32_MAX) {
	      complete(AdminResult{
		error_("invalid_request", "invalid action id filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->actions,
	      ZuFwdTuple(appID, static_cast<ActionID>(id)), ZuMv(complete));
	    return;
	  }
	  if (queryInput.name) {
	    adminFind<1>(m_context->actions,
	      ZuFwdTuple(appID, ZuMv(queryInput.name)), ZuMv(complete));
	    return;
	  }
	  adminQueryApp<0>(m_context->actions, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::roleQuery:
	  if (queryInput.id) {
	    uint64_t id = ZuBox<uint64_t>{queryInput.id};
	    if (!id) {
	      complete(AdminResult{
		error_("invalid_request", "invalid role id filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->roles,
	      ZuFwdTuple(appID, RoleID{id}), ZuMv(complete));
	    return;
	  }
	  if (queryInput.name) {
	    adminFind<1>(m_context->roles,
	      ZuFwdTuple(appID, ZuMv(queryInput.name)), ZuMv(complete));
	    return;
	  }
	  adminQueryApp<0>(m_context->roles, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::scopeQuery:
	  if (queryInput.id) {
	    uint64_t id = ZuBox<uint64_t>{queryInput.id};
	    if (!id) {
	      complete(AdminResult{
		error_("invalid_request", "invalid scope id filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->scopes,
	      ZuFwdTuple(appID, ScopeID{id}), ZuMv(complete));
	    return;
	  }
	  if (queryInput.audienceID && queryInput.name) {
	    uint64_t id = ZuBox<uint64_t>{queryInput.audienceID};
	    if (!id) {
	      complete(AdminResult{
		error_("invalid_request", "invalid audienceID filter"), 400});
	      return;
	    }
	    adminFind<1>(m_context->scopes,
	      ZuFwdTuple(appID, AudienceID{id}, ZuMv(queryInput.name)),
	      ZuMv(complete));
	    return;
	  }
	  adminQueryApp<0>(m_context->scopes, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::clientAccessQuery:
	  if (queryInput.clientID) {
	    adminFind<0>(m_context->clientAccess,
	      ZuFwdTuple(ZuMv(queryInput.clientID), appID), ZuMv(complete));
	    return;
	  }
	  adminQueryApp<1>(m_context->clientAccess, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::adminAccessQuery:
	  adminQueryApp<2>(m_context->adminAccess, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	default:
	  adminQueryApp<0>(m_context->roleMaps, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
      }
    }
    case MgmtOp::audienceQuery:
      if (queryInput.id) {
	uint64_t id = ZuBox<uint64_t>{queryInput.id};
	if (!id) break;
	adminFind<0>(m_context->audiences, ZuFwdTuple(AudienceID{id}),
	  ZuMv(complete));
	return;
      }
      if (queryInput.uri) {
	adminFind<1>(m_context->audiences, ZuFwdTuple(ZuMv(queryInput.uri)),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->audiences, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::clientQuery:
      if (queryInput.id || queryInput.clientID) {
	String id = queryInput.id ? ZuMv(queryInput.id) :
	  ZuMv(queryInput.clientID);
	adminFind<0>(m_context->clients, ZuFwdTuple(ZuMv(id)),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->clients, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::providerQuery:
      if (queryInput.id) {
	uint64_t id = ZuBox<uint64_t>{queryInput.id};
	if (!id) break;
	adminFind<0>(m_context->providers, ZuFwdTuple(ProviderID{id}),
	  ZuMv(complete));
	return;
      }
      if (queryInput.name) {
	adminFind<1>(m_context->providers, ZuFwdTuple(ZuMv(queryInput.name)),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->providers, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::authPolicyQuery:
      if (queryInput.appID) {
	uint64_t id = ZuBox<uint64_t>{queryInput.appID};
	if (!id) break;
	adminFind<0>(m_context->authPolicies, ZuFwdTuple(AppID{id}),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->authPolicies, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::identityQuery:
      adminQuery(m_context->extIdentities, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::evidenceQuery:
      adminQuery(m_context->evidence, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::sessionQuery:
      adminQuery(m_context->sessions, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::consentQuery:
      adminQuery(m_context->consents, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::grantQuery:
      adminQuery(m_context->grants, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::signKeyQuery:
      adminQuery(m_context->signKeys, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::auditQuery:
      adminQuery(m_context->audits, queryInput.limit,
	String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::appUpdate: {
      uint64_t id;
      String label, unused;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/apps/", {}, id) ||
	  !stringPatchBody(body, "label", label, {}, unused, seen)) break;
      adminUpdate(m_context->apps, ZuFwdTuple(AppID{id}), ZuMv(ifMatch),
	[label = ZuMv(label)](App &app) mutable {
	  app.label = ZuMv(label);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::userUpdate: {
      uint64_t id;
      String profile, email;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/users/", {}, id) ||
	  !stringPatchBody(body, "profile", profile, "email", email, seen))
	break;
      adminUpdate(m_context->users, ZuFwdTuple(UserID{id}), ZuMv(ifMatch),
	[profile = ZuMv(profile), email = ZuMv(email), seen](User &user) mutable {
	  if (seen & 1U) user.profile = ZuMv(profile);
	  if (seen & 2U) user.email = ZuMv(email);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::credentialUpdate: {
      Bytes id;
      String label, unused;
      unsigned seen = 0;
      if (!pathBytes(target, "/admin/credentials/", {}, id) ||
	  !stringPatchBody(body, "label", label, {}, unused, seen)) break;
      adminUpdate(m_context->creds, ZuFwdTuple(ZuMv(id)), ZuMv(ifMatch),
	[label = ZuMv(label)](Cred &cred) mutable {
	  cred.label = ZuMv(label);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::roleUpdate: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/roles/";
      uint64_t id;
      String label, unused;
      unsigned seen = 0;
      if (!pathUInt(target, prefix, {}, id) ||
	  !stringPatchBody(body, "label", label, {}, unused, seen)) break;
      adminUpdate(m_context->roles, ZuFwdTuple(appID, RoleID{id}),
	ZuMv(ifMatch), [label = ZuMv(label)](Role &role) mutable {
	  if (role.tombstone) return false;
	  role.label = ZuMv(label);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::audienceUpdate: {
      uint64_t id;
      String name, unused;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/audiences/", {}, id) ||
	  !stringPatchBody(body, "name", name, {}, unused, seen)) break;
      adminUpdate(m_context->audiences, ZuFwdTuple(AudienceID{id}),
	ZuMv(ifMatch), [name = ZuMv(name)](Audience &audience) mutable {
	  audience.name = ZuMv(name);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::appEnroll: {
      AppInput input;
      if (!idempotencyKey || !adminBody(body, input)) {
	complete(AdminResult{
	  error_("invalid_request", !idempotencyKey ?
	    "Idempotency-Key is required" :
	    "invalid application enrollment"), 400});
	return;
      }
      auto issuers = m_context->issuers;
      issuers->run(0, [this, issuers, input = ZuMv(input),
	  complete = ZuMv(complete)]() mutable {
	String issuer = m_config.issuer;
	issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [this,
	    input = ZuMv(input), complete = ZuMv(complete)](
	      ZdbRowRef<Issuer> issuer) mutable {
	  if (!issuer) {
	    complete(AdminResult{
	      error_("unavailable", "issuer is unavailable"), 503});
	    return;
	  }
	  ZmRef<AppEnroll_> enroll = new AppEnroll_{m_db, m_context, &m_rng,
	    issuer->data(), ZuMv(input), ZuMv(complete)};
	  enroll->start();
	});
      });
      return;
    }
    case MgmtOp::userInvite: {
      UserInput input;
      if (!idempotencyKey || !adminBody(body, input) || !input.name) {
	complete(AdminResult{
	  error_("invalid_request",
	    !idempotencyKey ? "Idempotency-Key is required" :
	    "invalid user invitation"), 400});
	return;
      }
      uint64_t id;
      if (!loginNormalize(input.name) || !randomID(m_rng, id)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid local login name"), 400});
	return;
      }
      auto users = m_context->users;
      int64_t now = Zm::now().sec();
      User user{.id = id, .source = UserSource::Local,
	.name = ZuMv(input.name), .profile = ZuMv(input.profile),
	.email = ZuMv(input.email), .created = now, .updated = now,
	.state = State::Pending, .version = 1};
      users->run(0, [this, users, user = ZuMv(user),
	  complete = ZuMv(complete)]() mutable {
	ZdbRowRef<User> row = new ZdbRow<User>{users, ZdbShard{0}};
	users->insert(ZuMv(row), [this, user = ZuMv(user),
	    complete = ZuMv(complete)](ZdbRow<User> *row) mutable {
	  if (!row) {
	    complete(AdminResult{
	      "{\"error\":\"conflict\",\"message\":\"user already exists\","
	      "\"correlationID\":\"\"}", 409});
	    return;
	  }
	  new (row->ptr()) User{ZuMv(user)};
	  if (!row->commit()) {
	    complete(AdminResult{
	      "{\"error\":\"unavailable\",\"message\":\"commit failed\","
	      "\"correlationID\":\"\"}", 503});
	    return;
	  }
	  User invitedUser = row->data();
	  int64_t now = Zm::now().sec();
	  bool started = enrollmentIssue(m_requests,
	    Zm::now() + ZuTime{10}, m_context, m_rng,
	    EnrollmentIssueConfig{.issuer = m_config.issuer,
	      .userName = invitedUser.name, .label = "Zum passkey",
	      .userID = invitedUser.id, .now = now, .expires = now + 86400},
	    [this, user = ZuMv(invitedUser), complete](
		bool ok, String capability) mutable {
	      if (!ok) {
		complete(AdminResult{
		  error_("unavailable", "enrollment capability failed"), 503});
		return;
	      }
	      String url{m_config.issuer};
	      if (url[url.length() - 1] == '/') url.length(url.length() - 1);
	      url << "/enroll?capability=" << capability;
	      String json{"{\"item\":{\"id\":\""};
	      json << user.id << "\",\"etag\":";
	      ZfJSON::quote(json, rowETag(user.version));
	      json << ",\"enrollmentURL\":";
	      ZfJSON::quote(json, url);
	      json << "}}";
	      ZuClear(capability.data(), capability.length());
	      StringVec ids;
	      String id;
	      id << user.id;
	      ids.push(ZuMv(id));
	      complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	    });
	  if (!started)
	    complete(AdminResult{
	      error_("unavailable", "enrollment request unavailable"), 503});
	});
      });
      return;
    }
    case MgmtOp::userRecover: {
      uint64_t userID;
      if (!idempotencyKey ||
	  !pathUInt(target, "/admin/users/", "/recover", userID)) {
	complete(AdminResult{error_("invalid_request",
	  !idempotencyKey ? "Idempotency-Key is required" :
	  "invalid user recovery"), 400});
	return;
      }
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required",
	  "If-Match is required"));
	return;
      }
      auto users = m_context->users;
      String actor = principal.subject;
      users->run(0, [this, users, userID, actor = ZuMv(actor),
	  ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)]() mutable {
	users->find<0>(0, ZuFwdTuple(userID), [this, userID,
	    actor = ZuMv(actor), ifMatch = ZuMv(ifMatch),
	    complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
	  if (!user) {
	    complete(adminErrorResult(404, "not_found", "user not found"));
	    return;
	  }
	  if (ifMatch != rowETag(user->data().version)) {
	    complete(adminErrorResult(412, "precondition_failed", "ETag mismatch"));
	    return;
	  }
	  int64_t now = Zm::now().sec();
	  bool started = recoveryIssue(m_requests, Zm::now() + ZuTime{10},
	    m_db, m_context, m_rng, RecoveryIssueConfig{
	      .issuer = m_config.issuer, .actor = ZuMv(actor),
	      .userID = userID, .now = now, .expires = now + 3600,
	      .version = user->data().version},
	    [this, userID, complete](
		bool ok, String capability) mutable {
	      if (!ok) {
		complete(AdminResult{
		  error_("conflict", "user cannot be recovered"), 409});
		return;
	      }
	      String url{m_config.issuer};
	      if (url[url.length() - 1] == '/') url.length(url.length() - 1);
	      url << "/enroll?purpose=recovery&capability=" << capability;
	      String json{"{\"item\":{\"id\":\""};
	      json << userID << "\",\"recoveryURL\":";
	      ZfJSON::quote(json, url);
	      json << "}}";
	      ZuClear(capability.data(), capability.length());
	      complete(AdminResult{ZuMv(json), 200});
	    });
	  if (!started)
	    complete(AdminResult{
	      error_("unavailable", "recovery request unavailable"), 503});
	});
      });
      return;
    }
    case MgmtOp::roleAdd: {
      AppID appID;
      NameInput input;
      uint64_t id;
      if (!idempotencyKey || !adminTargetApp(target, appID) ||
	  !adminBody(body, input) || !input.name || !randomID(m_rng, id)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid role creation"), 400});
	return;
      }
      int64_t now = Zm::now().sec();
      Role role{.appID = appID, .id = id, .name = ZuMv(input.name),
	.label = ZuMv(input.label), .state = State::Active,
	.origin = Origin::Custom, .version = 1,
	.created = now, .updated = now};
      auto roles = m_context->roles;
      roles->run(0, [roles, role = ZuMv(role),
	  complete = ZuMv(complete)]() mutable {
	ZdbRowRef<Role> row = new ZdbRow<Role>{roles, ZdbShard{0}};
	roles->insert(ZuMv(row), [role = ZuMv(role),
	    complete = ZuMv(complete)](ZdbRow<Role> *row) mutable {
	  if (!row) {
	    complete(AdminResult{
	      "{\"error\":\"conflict\",\"message\":\"role already exists\","
	      "\"correlationID\":\"\"}", 409});
	    return;
	  }
	  new (row->ptr()) Role{ZuMv(role)};
	  if (!row->commit()) {
	    complete(AdminResult{
	      "{\"error\":\"unavailable\",\"message\":\"commit failed\","
	      "\"correlationID\":\"\"}", 503});
	    return;
	  }
	  String json{"{\"item\":{\"id\":\""};
	  json << row->data().id << "\",\"etag\":";
	  ZfJSON::quote(json, rowETag(row->data().version));
	  json << "}}";
	  StringVec ids;
	  String resultID;
	  resultID << row->data().id;
	  ids.push(ZuMv(resultID));
	  complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	});
      });
      return;
    }
    case MgmtOp::actionAdd: {
      AppID appID;
      NameInput input;
      if (!idempotencyKey || !adminTargetApp(target, appID) ||
	  !adminBody(body, input) || !input.name) {
	complete(AdminResult{
	  error_("invalid_request", "invalid action creation"), 400});
	return;
      }
      ZmRef<ActionAdd_> add = new ActionAdd_{m_db, m_context, &m_rng,
	appID, ZuMv(input), ZuMv(complete)};
      add->start();
      return;
    }
    case MgmtOp::audienceAdd: {
      AudienceInput input;
      uint64_t id;
      if (!idempotencyKey || !adminBody(body, input) || !input.appID ||
	  !input.name || !input.uri || !randomID(m_rng, id)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid audience creation"), 400});
	return;
      }
      auto apps = m_context->apps;
      apps->run(0, [this, apps, input = ZuMv(input), id,
	  complete = ZuMv(complete)]() mutable {
	apps->find<0>(0, ZuFwdTuple(input.appID), [this,
	    input = ZuMv(input), id, complete = ZuMv(complete)](
	      ZdbRowRef<App> app) mutable {
	  if (!app || app->data().state != State::Active || app->data().owner) {
	    complete(AdminResult{
	      error_("not_found", "application not found"), 404});
	    return;
	  }
	  int64_t now = Zm::now().sec();
	  Audience audience{.id = id, .appID = input.appID,
	    .name = ZuMv(input.name), .uri = ZuMv(input.uri),
	    .state = State::Active, .version = 1,
	    .created = now, .updated = now};
	  auto audiences = m_context->audiences;
	  audiences->run(0, [audiences, audience = ZuMv(audience),
	      complete = ZuMv(complete)]() mutable {
	    ZdbRowRef<Audience> row =
	      new ZdbRow<Audience>{audiences, ZdbShard{0}};
	    audiences->insert(ZuMv(row), [audience = ZuMv(audience),
		complete = ZuMv(complete)](ZdbRow<Audience> *row) mutable {
	      if (!row) {
		complete(AdminResult{
		  "{\"error\":\"conflict\",\"message\":\"audience exists\","
		  "\"correlationID\":\"\"}", 409});
		return;
	      }
	      new (row->ptr()) Audience{ZuMv(audience)};
	      if (!row->commit()) {
		complete(AdminResult{
		  "{\"error\":\"unavailable\",\"message\":\"commit failed\","
		  "\"correlationID\":\"\"}", 503});
		return;
	      }
	      String json{"{\"item\":{\"id\":\""};
	      json << row->data().id << "\",\"etag\":";
	      ZfJSON::quote(json, rowETag(row->data().version));
	      json << "}}";
	      StringVec ids;
	      String resultID;
	      resultID << row->data().id;
	      ids.push(ZuMv(resultID));
	      complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	    });
	  });
	});
      });
      return;
    }
    case MgmtOp::scopeAdd: {
      AppID appID;
      ScopeInput input;
      uint64_t id;
      if (!idempotencyKey || !adminTargetApp(target, appID) ||
	  !adminBody(body, input) || !input.audienceID || !input.name ||
	  !randomID(m_rng, id)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid scope creation"), 400});
	return;
      }
      auto audiences = m_context->audiences;
      audiences->run(0, [this, audiences, appID, input = ZuMv(input), id,
	  complete = ZuMv(complete)]() mutable {
	audiences->find<0>(0, ZuFwdTuple(input.audienceID), [this, appID,
	    input = ZuMv(input), id, complete = ZuMv(complete)](
	      ZdbRowRef<Audience> audience) mutable {
	  if (!audience || audience->data().appID != appID ||
	      audience->data().state != State::Active || audience->data().owner) {
	    complete(AdminResult{
	      error_("invalid_request", "invalid audience reference"), 400});
	    return;
	  }
	  int64_t now = Zm::now().sec();
	  Scope scope{.appID = appID, .id = id,
	    .audienceID = input.audienceID, .audience = audience->data().uri,
	    .name = ZuMv(input.name), .state = State::Active,
	    .origin = Origin::Custom, .version = 1,
	    .created = now, .updated = now};
	  auto scopes = m_context->scopes;
	  scopes->run(0, [scopes, scope = ZuMv(scope),
	      complete = ZuMv(complete)]() mutable {
	    ZdbRowRef<Scope> row = new ZdbRow<Scope>{scopes, ZdbShard{0}};
	    scopes->insert(ZuMv(row), [scope = ZuMv(scope),
		complete = ZuMv(complete)](ZdbRow<Scope> *row) mutable {
	      if (!row) {
		complete(AdminResult{
		  "{\"error\":\"conflict\",\"message\":\"scope exists\","
		  "\"correlationID\":\"\"}", 409});
		return;
	      }
	      new (row->ptr()) Scope{ZuMv(scope)};
	      if (!row->commit()) {
		complete(AdminResult{
		  "{\"error\":\"unavailable\",\"message\":\"commit failed\","
		  "\"correlationID\":\"\"}", 503});
		return;
	      }
	      String json{"{\"item\":{\"id\":\""};
	      json << row->data().id << "\",\"etag\":";
	      ZfJSON::quote(json, rowETag(row->data().version));
	      json << "}}";
	      StringVec ids;
	      String resultID;
	      resultID << row->data().id;
	      ids.push(ZuMv(resultID));
	      complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	    });
	  });
	});
      });
      return;
    }
    case MgmtOp::membershipAdd: {
      AppID appID;
      MembershipInput input;
      if (!idempotencyKey || !adminTargetApp(target, appID) ||
	  !adminBody(body, input) || !input.userID || input.roleIDs) {
	complete(AdminResult{
	  error_("invalid_request",
	    "membership creation requires userID and no initial roles"), 400});
	return;
      }
      int64_t now = Zm::now().sec();
      Membership membership{.appID = appID, .userID = input.userID,
	.state = State::Active, .version = 1,
	.created = now, .updated = now};
      ZmRef<MembershipAdd_> add = new MembershipAdd_{
	m_context, ZuMv(membership), ZuMv(complete)};
      add->start();
      return;
    }
    case MgmtOp::membershipRoles: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/memberships/";
      uint64_t userID;
      RolesInput input;
      if (!pathUInt(target, prefix, "/roles", userID) ||
	  !adminBody(body, input)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid membership roles"), 400});
	return;
      }
      if (!ifMatch) {
	complete(AdminResult{
	  error_("precondition_required", "If-Match is required"), 428});
	return;
      }
      IDVec roleIDs = ZuMv(input.roleIDs);
      if (!roleCeiling(permit, roleIDs)) {
	complete(AdminResult{
	  error_("forbidden", "role assignment exceeds delegation"), 403});
	return;
      }
      ZmRef<RoleRefs_> refs = new RoleRefs_{m_context, appID,
	IDVec{roleIDs}, [this, appID, userID, roleIDs = ZuMv(roleIDs),
	    ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)](
	      bool valid) mutable {
	  if (!valid) {
	    complete(AdminResult{
	      error_("invalid_request", "invalid role reference"), 400});
	    return;
	  }
	  auto memberships = m_context->memberships;
	  memberships->run(0, [memberships, appID, userID,
	      roleIDs = ZuMv(roleIDs),
	      ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)]() mutable {
	    memberships->findUpd<0>(0, ZuFwdTuple(appID, userID), [
	        roleIDs = ZuMv(roleIDs), ifMatch = ZuMv(ifMatch),
	        complete = ZuMv(complete)](ZdbRow<Membership> *row) mutable {
	      if (!row) {
	        complete(AdminResult{
	          error_("not_found", "membership not found"), 404});
	        return;
	      }
	      if (ifMatch != rowETag(row->data().version)) {
	        complete(AdminResult{
	          error_("precondition_failed", "ETag mismatch"), 412});
	        return;
	      }
	      row->data().roleIDs = ZuMv(roleIDs);
	      ++row->data().authVersion;
	      ++row->data().version;
	      row->data().updated = Zm::now().sec();
	      if (!row->commit()) {
	        complete(AdminResult{
	          error_("unavailable", "commit failed"), 503});
	        return;
	      }
	      String json{"{\"item\":{\"etag\":"};
	      ZfJSON::quote(json, rowETag(row->data().version));
	      json << "}}";
	      complete(AdminResult{ZuMv(json), 200});
	    });
	  });
	} };
      refs->start();
      return;
    }
    case MgmtOp::roleActions: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/roles/";
      uint64_t roleID;
      ActionsInput input;
      if (!pathUInt(target, prefix, "/actions", roleID) ||
	  !adminBody(body, input)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid role actions"), 400});
	return;
      }
      if (!ifMatch) {
	complete(AdminResult{
	  error_("precondition_required", "If-Match is required"), 428});
	return;
      }
      ZmRef<ActionRefs_> refs = new ActionRefs_{m_context, appID,
	ZuMv(input.actionIDs), [this, appID, roleID,
	  ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)](
	    bool valid, ZtBitmap actions) mutable {
	  if (!valid) {
	    complete(AdminResult{
	      error_("invalid_request", "invalid action reference"), 400});
	    return;
	  }
	  adminUpdate(m_context->roles, ZuFwdTuple(appID, RoleID{roleID}),
	    ZuMv(ifMatch), [actions = ZuMv(actions)](Role &role) mutable {
	      if (role.tombstone) return false;
	      role.actions = ZuMv(actions);
	      return true;
	    }, ZuMv(complete));
	} };
      refs->start();
      return;
    }
    case MgmtOp::scopeRoles: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/scopes/";
      uint64_t scopeID;
      RolesInput input;
      if (!pathUInt(target, prefix, "/roles", scopeID) ||
	  !adminBody(body, input)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid scope roles"), 400});
	return;
      }
      if (!roleCeiling(permit, input.roleIDs)) {
	complete(AdminResult{
	  error_("forbidden", "scope roles exceed delegation"), 403});
	return;
      }
      if (!ifMatch) {
	complete(AdminResult{
	  error_("precondition_required", "If-Match is required"), 428});
	return;
      }
      IDVec roleIDs = ZuMv(input.roleIDs);
      ZmRef<RoleRefs_> refs = new RoleRefs_{m_context, appID,
	IDVec{roleIDs}, [this, appID, scopeID, roleIDs = ZuMv(roleIDs),
	  ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)](
	    bool valid) mutable {
	  if (!valid) {
	    complete(AdminResult{
	      error_("invalid_request", "invalid role reference"), 400});
	    return;
	  }
	  adminUpdate(m_context->scopes,
	    ZuFwdTuple(appID, ScopeID{scopeID}), ZuMv(ifMatch),
	    [roleIDs = ZuMv(roleIDs)](Scope &scope) mutable {
	      scope.roleIDs = ZuMv(roleIDs);
	      return true;
	    }, ZuMv(complete));
	} };
      refs->start();
      return;
    }
    case MgmtOp::clientSecretRotate: {
      String clientID;
      RotateInput input;
      if (!pathValue(target, "/admin/clients/", "/rotate-secret", clientID) ||
	  !adminBody(body, input)) {
	complete(AdminResult{
	  error_("invalid_request", "invalid client secret rotation"), 400});
	return;
      }
      if (!ifMatch) {
	complete(AdminResult{
	  error_("precondition_required", "If-Match is required"), 428});
	return;
      }
      uint8_t random[32];
      if (!m_rng.random(random)) {
	complete(AdminResult{
	  error_("unavailable", "secret generation failed"), 503});
	return;
      }
      String secret = encode({random, sizeof(random)});
      Bytes digest;
      digest.length(Ztls::SecretHash::Size, false);
      if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	ZuClear(secret.data(), secret.length());
	complete(AdminResult{
	  error_("unavailable", "secret generation failed"), 503});
	return;
      }
      auto clients = m_context->clients;
      clients->run(0, [clients, clientID = String{clientID},
	  ifMatch = ZuMv(ifMatch), input, secret = ZuMv(secret),
	  digest = ZuMv(digest), complete = ZuMv(complete)]() mutable {
	clients->findUpd<0>(0, ZuFwdTuple(clientID), [clientID = ZuMv(clientID),
	    ifMatch = ZuMv(ifMatch), input, secret = ZuMv(secret),
	    digest = ZuMv(digest), complete = ZuMv(complete)](
	      ZdbRow<Client> *row) mutable {
	  auto fail = [&secret, &complete](ZuCSpan error, ZuCSpan message,
	      unsigned status) mutable {
	    ZuClear(secret.data(), secret.length());
	    String json{"{\"error\":"};
	    ZfJSON::quote(json, error);
	    json << ",\"message\":";
	    ZfJSON::quote(json, message);
	    json << ",\"correlationID\":\"\"}";
	    complete(AdminResult{ZuMv(json), status});
	  };
	  if (!row) { fail("not_found", "client not found", 404); return; }
	  auto &client = row->data();
	  if (ifMatch != rowETag(client.version)) {
	    fail("precondition_failed", "ETag mismatch", 412); return;
	  }
	  if (client.type != ClientType::Confidential ||
	      client.authMethod != ClientAuthMethod::ClientSecretBasic ||
	      client.state == State::Revoked || client.owner) {
	    fail("conflict", "client cannot rotate a secret", 409); return;
	  }
	  if (input.overlapSeconds) {
	    client.previousSecretDigest = client.secretDigest;
	    client.previousSecretExpires = Zm::now().sec() +
	      input.overlapSeconds;
	  } else {
	    client.previousSecretDigest.null();
	    client.previousSecretExpires = 0;
	  }
	  client.secretDigest = ZuMv(digest);
	  ++client.secretVersion;
	  ++client.version;
	  client.updated = Zm::now().sec();
	  if (!row->commit()) {
	    fail("unavailable", "commit failed", 503); return;
	  }
	  String json{"{\"item\":{\"id\":"};
	  ZfJSON::quote(json, clientID);
	  json << ",\"client_secret\":";
	  ZfJSON::quote(json, secret);
	  json << ",\"secretVersion\":\"" << client.secretVersion <<
	    "\",\"etag\":";
	  ZfJSON::quote(json, rowETag(client.version));
	  json << "}}";
	  ZuClear(secret.data(), secret.length());
	  StringVec ids;
	  ids.push(clientID);
	  complete(AdminResult{ZuMv(json), 200, ZuMv(ids)});
	});
      });
      return;
    }
    case MgmtOp::clientAdd: {
      ClientInput input;
      if (!adminBody(body, input) || !input.appID || input.id) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client creation"));
	return;
      }
      ClientType::T type = clientType(input.type);
      if (!clientConfigValid(type, input.grants, input.redirectURIs)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client configuration"));
	return;
      }
      uint8_t random[50];
      if (!m_rng.random(random)) {
	complete(adminErrorResult(503, "unavailable",
	  "client credential generation failed"));
	return;
      }
      String clientID{"cli_"};
      clientID << encode({random, 18});
      String secret;
      Bytes digest;
      if (type == ClientType::Confidential) {
	secret = encode({random + 18, 32});
	digest.length(Ztls::SecretHash::Size, false);
	if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	  ZuClear(secret.data(), secret.length());
	  complete(adminErrorResult(503, "unavailable",
	    "client credential generation failed"));
	  return;
	}
      }
      int64_t now = Zm::now().sec();
      Client client{.id = clientID, .appID = input.appID,
	.label = ZuMv(input.label), .secretDigest = ZuMv(digest),
	.secretVersion = 1, .redirects = ZuMv(input.redirectURIs),
	.created = now, .updated = now, .type = type,
	.authMethod = ClientAuthMethod::T(type == ClientType::Confidential ?
	  ClientAuthMethod::ClientSecretBasic : ClientAuthMethod::None),
	.grants = input.grants, .refreshAllowed = input.refreshAllowed,
	.identityScopes = ZuMv(input.identityScopes), .state = State::Active,
	.version = 1};
      auto apps = m_context->apps;
      apps->run(0, [this, apps, client = ZuMv(client),
	  secret = ZuMv(secret), complete = ZuMv(complete)]() mutable {
	apps->find<0>(0, ZuFwdTuple(client.appID), [this,
	    client = ZuMv(client), secret = ZuMv(secret),
	    complete = ZuMv(complete)](ZdbRowRef<App> app) mutable {
	  if (!app || app->data().state != State::Active || app->data().owner) {
	    if (secret) ZuClear(secret.data(), secret.length());
	    complete(adminErrorResult(404, "not_found",
	      "application not found"));
	    return;
	  }
	  auto clients = m_context->clients;
	  clients->run(0, [clients, client = ZuMv(client),
	      secret = ZuMv(secret), complete = ZuMv(complete)]() mutable {
	    ZdbRowRef<Client> row = new ZdbRow<Client>{clients, ZdbShard{0}};
	    clients->insert(ZuMv(row), [client = ZuMv(client),
	        secret = ZuMv(secret), complete = ZuMv(complete)](
	          ZdbRow<Client> *row) mutable {
	      if (!row) {
		if (secret) ZuClear(secret.data(), secret.length());
		complete(adminErrorResult(409, "conflict", "client exists"));
		return;
	      }
	      new (row->ptr()) Client{ZuMv(client)};
	      if (!row->commit()) {
		if (secret) ZuClear(secret.data(), secret.length());
		complete(adminErrorResult(503, "unavailable", "commit failed"));
		return;
	      }
	      String json{"{\"item\":{\"id\":"};
	      ZfJSON::quote(json, row->data().id);
	      if (secret) {
		json << ",\"client_secret\":";
		ZfJSON::quote(json, secret);
	      }
	      json << ",\"etag\":";
	      ZfJSON::quote(json, rowETag(row->data().version));
	      json << "}}";
	      if (secret) ZuClear(secret.data(), secret.length());
	      StringVec ids;
	      ids.push(row->data().id);
	      complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	    });
	  });
	});
      });
      return;
    }
    case MgmtOp::clientUpdate: {
      String id;
      ClientUpdateInput input;
      uint64_t seen;
      static constexpr ZuCSpan fields[] = {
	"label", "redirectURIs", "grants", "identityScopes"};
      if (!pathValue(target, "/admin/clients/", {}, id) ||
	  !adminBodyFields(body, input, fields, seen)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client update"));
	return;
      }
      adminUpdate(m_context->clients, ZuFwdTuple(String{id}), ZuMv(ifMatch),
	[input = ZuMv(input), seen](Client &client) mutable {
	  if (client.state == State::Revoked) return false;
	  StringVec redirects = (seen & 2) ? input.redirectURIs : client.redirects;
	  uint8_t grants = (seen & 4) ? input.grants : client.grants;
	  if (!clientConfigValid(client.type, grants, redirects)) return false;
	  if (seen & 1) client.label = ZuMv(input.label);
	  if (seen & 2) client.redirects = ZuMv(input.redirectURIs);
	  if (seen & 4) client.grants = input.grants;
	  if (seen & 8) client.identityScopes = ZuMv(input.identityScopes);
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::clientAccessSet: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/client-access/";
      String clientID;
      ClientAccessInput input;
      if (!pathValue(target, prefix, {}, clientID) ||
	  !adminBody(body, input)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client access replacement"));
	return;
      }
      if (!roleCeiling(permit, input.roleIDs)) {
	complete(adminErrorResult(403, "forbidden",
	  "client roles exceed delegation"));
	return;
      }
      int64_t now = Zm::now().sec();
      ClientAccess access{.clientID = String{clientID}, .appID = appID,
	.audienceIDs = ZuMv(input.audienceIDs),
	.scopeIDs = ZuMv(input.scopeIDs), .roleIDs = ZuMv(input.roleIDs),
	.state = State::Active, .authVersion = 1, .version = 1,
	.created = now, .updated = now};
      ZmRef<ClientAccessRefs_> refs = new ClientAccessRefs_{m_context,
	ClientAccess{access}, [this, access = ZuMv(access),
	    ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
	    complete = ZuMv(complete)](bool valid) mutable {
	  if (!valid) {
	    complete(adminErrorResult(400, "invalid_request",
	      "invalid client access reference"));
	    return;
	  }
	  ClientAccessTable::Key<0> key{ZuFwdTuple(access.clientID, access.appID)};
	  adminPut(m_context->clientAccess, ZuMv(key), ZuMv(access),
	    ZuMv(ifMatch), ZuMv(ifNoneMatch),
	    [](ClientAccess &current, ClientAccess next) mutable {
	      current.audienceIDs = ZuMv(next.audienceIDs);
	      current.scopeIDs = ZuMv(next.scopeIDs);
	      current.roleIDs = ZuMv(next.roleIDs);
	      current.state = State::Active;
	      ++current.authVersion;
	      return true;
	    }, ZuMv(complete));
	}};
      refs->start();
      return;
    }
    case MgmtOp::clientAccessState: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/client-access/";
      String clientID;
      if (pathValue(target, prefix, "/state", clientID)) {
	adminState(m_context->clientAccess,
	  ZuFwdTuple(String{clientID}, appID), ZuMv(body), ZuMv(ifMatch),
	  false, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::adminAccessSet: {
      if (!permit.superuser) {
	complete(adminErrorResult(403, "forbidden",
	  "superuser authority is required"));
	return;
      }
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/admin-access/";
      if (target.prefix(prefix) != prefix) break;
      ZuCSpan rest{target.data() + prefix.length(),
	target.length() - prefix.length()};
      auto slash = rest.find<"/">();
      if (slash <= 0 || unsigned(slash) + 1 >= rest.length()) break;
      ZuCSpan kindName{rest.data(), unsigned(slash)};
      ZuCSpan actorID{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      ActorKind::T kind = kindName == "user" ? ActorKind::User :
	kindName == "client" ? ActorKind::Client : ActorKind::T(-1);
      AdminAccessInput input;
      if (kind < 0 || !actorID || actorID.find<"/">() >= 0 ||
	  !adminBody(body, input) || !uniqueIDs(input.roleIDs)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid administrative access replacement"));
	return;
      }
      for (unsigned i = 0; i < input.operationIDs.length(); ++i) {
	if (input.operationIDs[i] >= MgmtOp::N) {
	  complete(adminErrorResult(400, "invalid_request",
	    "invalid management operation reference"));
	  return;
	}
	for (unsigned j = 0; j < i; ++j)
	  if (input.operationIDs[i] == input.operationIDs[j]) {
	    complete(adminErrorResult(400, "invalid_request",
	      "duplicate management operation reference"));
	    return;
	  }
      }
      int64_t now = Zm::now().sec();
      AdminAccess access{.actorKind = kind, .actorID = String{actorID},
	.appID = appID, .operationIDs = ZuMv(input.operationIDs),
	.roleIDs = ZuMv(input.roleIDs), .state = State::Active,
	.version = 1, .created = now, .updated = now};
      ZmRef<RoleRefs_> refs = new RoleRefs_{m_context, appID,
	IDVec{access.roleIDs}, [this, access = ZuMv(access),
	    ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
	    complete = ZuMv(complete)](bool valid) mutable {
	  if (!valid) {
	    complete(adminErrorResult(400, "invalid_request",
	      "invalid role ceiling"));
	    return;
	  }
	  AdminAccessTable::Key<0> key{
	    ZuFwdTuple(access.actorKind, access.actorID, access.appID)};
	  adminPut(m_context->adminAccess, ZuMv(key), ZuMv(access),
	    ZuMv(ifMatch), ZuMv(ifNoneMatch),
	    [](AdminAccess &current, AdminAccess next) mutable {
	      current.operationIDs = ZuMv(next.operationIDs);
	      current.roleIDs = ZuMv(next.roleIDs);
	      current.state = State::Active;
	      return true;
	    }, ZuMv(complete));
	}};
      refs->start();
      return;
    }
    case MgmtOp::adminAccessState: {
      if (!permit.superuser) {
	complete(adminErrorResult(403, "forbidden",
	  "superuser authority is required"));
	return;
      }
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/admin-access/";
      if (target.prefix(prefix) != prefix ||
	  target.length() <= prefix.length() + 7) break;
      ZuCSpan rest{target.data() + prefix.length(),
	target.length() - prefix.length() - 6};
      if (ZuCSpan{target.data() + target.length() - 6, 6} != "/state")
	break;
      auto slash = rest.find<"/">();
      if (slash <= 0 || unsigned(slash) + 1 >= rest.length()) break;
      ZuCSpan kindName{rest.data(), unsigned(slash)};
      ZuCSpan actorID{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      ActorKind::T kind = kindName == "user" ? ActorKind::User :
	kindName == "client" ? ActorKind::Client : ActorKind::T(-1);
      if (kind < 0 || actorID.find<"/">() >= 0) break;
      adminState(m_context->adminAccess,
	ZuFwdTuple(kind, String{actorID}, appID), ZuMv(body), ZuMv(ifMatch),
	false, ZuMv(complete));
      return;
    }
    case MgmtOp::providerAdd: {
      if (!permit.superuser) {
	complete(adminErrorResult(403, "forbidden",
	  "superuser authority is required"));
	return;
      }
      ProviderInput input;
      uint64_t id;
      if (!adminBody(body, input) || !randomID(m_rng, id)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid provider creation"));
	return;
      }
      ClaimSource::T source = ClaimSource::lookup(input.claimSource);
      if (source < 0 || source >= ClaimSource::N) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid provider claim source"));
	return;
      }
      Bytes protectedSecret;
      String recordID;
      recordID << id;
      if (input.clientSecret && !serverSecretEncrypt(m_rng, m_config.dbKey,
	  m_config.issuer, "provider", recordID, "clientSecret",
	  ZuBSpan{input.clientSecret}, protectedSecret)) {
	ZuClear(input.clientSecret.data(), input.clientSecret.length());
	complete(adminErrorResult(503, "unavailable",
	  "provider secret encryption failed"));
	return;
      }
      if (input.clientSecret)
	ZuClear(input.clientSecret.data(), input.clientSecret.length());
      int64_t now = Zm::now().sec();
      Provider provider{.id = id, .name = ZuMv(input.name),
	.issuer = ZuMv(input.issuer), .clientID = ZuMv(input.clientID),
	.clientSecret = ZuMv(protectedSecret), .scopes = ZuMv(input.scopes),
	.roleClaim = ZuMv(input.roleClaim), .claimSource = source,
	.state = State::Active, .version = 1, .created = now, .updated = now};
      auto providers = m_context->providers;
      providers->run(0, [providers, provider = ZuMv(provider),
	  complete = ZuMv(complete)]() mutable {
	ZdbRowRef<Provider> row = new ZdbRow<Provider>{providers, ZdbShard{0}};
	providers->insert(ZuMv(row), [provider = ZuMv(provider),
	    complete = ZuMv(complete)](ZdbRow<Provider> *row) mutable {
	  if (!row) {
	    complete(adminErrorResult(409, "conflict", "provider exists"));
	    return;
	  }
	  new (row->ptr()) Provider{ZuMv(provider)};
	  if (!row->commit()) {
	    complete(adminErrorResult(503, "unavailable", "commit failed"));
	    return;
	  }
	  StringVec ids;
	  String id;
	  id << row->data().id;
	  ids.push(ZuMv(id));
	  complete(adminItemResult(row->data(), 201, ZuMv(ids)));
	});
      });
      return;
    }
    case MgmtOp::providerUpdate: {
      if (!permit.superuser) {
	complete(adminErrorResult(403, "forbidden",
	  "superuser authority is required"));
	return;
      }
      uint64_t id;
      ProviderUpdateInput input;
      uint64_t seen;
      static constexpr ZuCSpan fields[] = {"issuer", "clientID",
	"clientSecret", "scopes", "roleClaim", "claimSource"};
      if (!pathUInt(target, "/admin/providers/", {}, id) ||
	  !adminBodyFields(body, input, fields, seen)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid provider update"));
	return;
      }
      ClaimSource::T source = ClaimSource::IDToken;
      if (seen & 32) {
	source = ClaimSource::lookup(input.claimSource);
	if (source < 0 || source >= ClaimSource::N) {
	  complete(adminErrorResult(400, "invalid_request",
	    "invalid provider claim source"));
	  return;
	}
      }
      Bytes protectedSecret;
      if (seen & 4) {
	String recordID;
	recordID << id;
	if (!input.clientSecret || !serverSecretEncrypt(m_rng, m_config.dbKey,
	    m_config.issuer, "provider", recordID, "clientSecret",
	    ZuBSpan{input.clientSecret}, protectedSecret)) {
	  if (input.clientSecret)
	    ZuClear(input.clientSecret.data(), input.clientSecret.length());
	  complete(adminErrorResult(400, "invalid_request",
	    "invalid provider secret"));
	  return;
	}
	ZuClear(input.clientSecret.data(), input.clientSecret.length());
      }
      adminUpdate(m_context->providers, ZuFwdTuple(ProviderID{id}),
	ZuMv(ifMatch), [input = ZuMv(input), protectedSecret = ZuMv(protectedSecret),
	  seen, source](Provider &provider) mutable {
	  if (provider.state == State::Revoked) return false;
	  if (seen & 1) provider.issuer = ZuMv(input.issuer);
	  if (seen & 2) provider.clientID = ZuMv(input.clientID);
	  if (seen & 4) provider.clientSecret = ZuMv(protectedSecret);
	  if (seen & 8) provider.scopes = ZuMv(input.scopes);
	  if (seen & 16) provider.roleClaim = ZuMv(input.roleClaim);
	  if (seen & 32) provider.claimSource = source;
	  return provider.issuer && provider.clientID && provider.roleClaim;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::authPolicySet: {
      AppID appID;
      AuthPolicyInput input;
      if (!adminTargetApp(target, appID) || !adminBody(body, input)) break;
      EligibilityMode::T eligibility =
	EligibilityMode::lookup(input.eligibilityMode);
      ConsentPolicy::T consent = ConsentPolicy::lookup(input.consentPolicy);
      State::T state = input.state ? State::lookup(input.state) : State::Active;
      if (!input.localFirst || eligibility < 0 ||
	  eligibility >= EligibilityMode::N || consent < 0 ||
	  consent >= ConsentPolicy::N || state < 0 || state >= State::N ||
	  !input.assignmentMaxAge || !input.sessionIdle ||
	  !input.sessionAbsolute || !input.tokenLifetime ||
	  input.sessionIdle > input.sessionAbsolute ||
	  (input.providerID && eligibility == EligibilityMode::ClaimValues &&
	    (!input.eligibilityClaim || !input.eligibilityValues))) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid authentication policy"));
	return;
      }
      int64_t now = Zm::now().sec();
      AuthPolicy policy{.appID = appID, .providerID = input.providerID,
	.localFirst = true, .eligibilityMode = eligibility,
	.eligibilityClaim = ZuMv(input.eligibilityClaim),
	.eligibilityValues = ZuMv(input.eligibilityValues),
	.assignmentMaxAge = input.assignmentMaxAge,
	.sessionIdle = input.sessionIdle,
	.sessionAbsolute = input.sessionAbsolute,
	.tokenLifetime = input.tokenLifetime, .consentPolicy = consent,
	.state = state, .version = 1, .created = now, .updated = now};
      adminPut(m_context->authPolicies, ZuFwdTuple(appID), ZuMv(policy),
	ZuMv(ifMatch), ZuMv(ifNoneMatch),
	[](AuthPolicy &current, AuthPolicy next) mutable {
	  uint64_t version = current.version;
	  int64_t created = current.created;
	  current = ZuMv(next);
	  current.version = version;
	  current.created = created;
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::roleMapSet: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/role-mappings/";
      if (target.prefix(prefix) != prefix) break;
      ZuCSpan rest{target.data() + prefix.length(),
	target.length() - prefix.length()};
      auto slash = rest.find<"/">();
      if (slash <= 0 || unsigned(slash) + 1 >= rest.length()) break;
      uint64_t providerID = ZuBox<uint64_t>{
	ZuCSpan{rest.data(), unsigned(slash)}};
      ZuCSpan encoded{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      Bytes decoded;
      decoded.length(ZuBase64URL::declen(encoded.length()), false);
      RoleMapInput input;
      if (!providerID || !adminBody(body, input) || !input.roleID ||
	  ZuBase64URL::decode(decoded, ZuBSpan{encoded}) != decoded.length() ||
	  !decoded) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid role mapping"));
	return;
      }
      if (!roleCeiling(permit, IDVec{input.roleID})) {
	complete(adminErrorResult(403, "forbidden",
	  "role mapping exceeds delegation"));
	return;
      }
      int64_t now = Zm::now().sec();
      RoleMap map{.appID = appID, .providerID = providerID,
	.value = String{ZuCSpan{reinterpret_cast<const char *>(decoded.data()),
	  decoded.length()}}, .roleID = input.roleID, .state = State::Active,
	.version = 1, .created = now, .updated = now};
      ZmRef<RoleRefs_> refs = new RoleRefs_{m_context, appID,
	IDVec{input.roleID}, [this, map = ZuMv(map),
	    ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
	    complete = ZuMv(complete)](bool valid) mutable {
	  if (!valid) {
	    complete(adminErrorResult(400, "invalid_request",
	      "invalid role reference"));
	    return;
	  }
	  RoleMapTable::Key<0> key{ZuFwdTuple(map.appID, map.providerID, map.value)};
	  adminPut(m_context->roleMaps, ZuMv(key), ZuMv(map),
	    ZuMv(ifMatch), ZuMv(ifNoneMatch),
	    [](RoleMap &current, RoleMap next) mutable {
	      current.roleID = next.roleID;
	      current.state = State::Active;
	      return true;
	    }, ZuMv(complete));
	}};
      refs->start();
      return;
    }
    case MgmtOp::roleMapDelete: {
      AppID appID;
      if (!adminTargetApp(target, appID) || !ifMatch) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/role-mappings/";
      if (target.prefix(prefix) != prefix) break;
      ZuCSpan rest{target.data() + prefix.length(),
	target.length() - prefix.length()};
      auto slash = rest.find<"/">();
      if (slash <= 0 || unsigned(slash) + 1 >= rest.length()) break;
      uint64_t providerID = ZuBox<uint64_t>{
	ZuCSpan{rest.data(), unsigned(slash)}};
      ZuCSpan encoded{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      Bytes decoded;
      decoded.length(ZuBase64URL::declen(encoded.length()), false);
      if (!providerID ||
	  ZuBase64URL::decode(decoded, ZuBSpan{encoded}) != decoded.length() ||
	  !decoded) break;
      String value{ZuCSpan{reinterpret_cast<const char *>(decoded.data()),
	decoded.length()}};
      auto maps = m_context->roleMaps;
      maps->run(0, [maps, appID, providerID, value = ZuMv(value),
	  ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)]() mutable {
	maps->findDel<0>(0, ZuFwdTuple(appID, providerID, ZuMv(value)), [
	    ifMatch = ZuMv(ifMatch), complete = ZuMv(complete)](
	      ZdbRow<RoleMap> *row) mutable {
	  if (!row) {
	    complete(adminErrorResult(404, "not_found", "role mapping not found"));
	    return;
	  }
	  if (ifMatch != rowETag(row->data().version)) {
	    complete(adminErrorResult(412, "precondition_failed", "ETag mismatch"));
	    return;
	  }
	  if (!row->commit()) {
	    complete(adminErrorResult(503, "unavailable", "commit failed"));
	    return;
	  }
	  complete(AdminResult{"{\"item\":null}", 200});
	});
      });
      return;
    }
    case MgmtOp::roleDelete: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/roles/";
      uint64_t roleID;
      if (!pathUInt(target, prefix, {}, roleID)) break;
      adminUpdate(m_context->roles, ZuFwdTuple(appID, RoleID{roleID}),
	ZuMv(ifMatch), [](Role &role) mutable {
	  if (role.tombstone || role.owner) return false;
	  role.actions = ZtBitmap{};
	  role.state = State::Revoked;
	  role.tombstone = true;
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::sessionRevoke: {
      SessionSelector input;
      if (!adminBody(body, input)) break;
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_context,
	BulkRevoke_::Sessions, input.userID, {}, 0, 0, input.limit,
	ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::consentRevoke: {
      ConsentSelector input;
      if (!adminBody(body, input)) break;
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_context,
	BulkRevoke_::Consents, input.userID, ZuMv(input.clientID),
	input.appID, input.audienceID, input.limit, ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::grantRevoke: {
      GrantSelector input;
      if (!adminBody(body, input) || !input.limit || input.limit > 1000)
	break;
      if (input.id) {
	Bytes id;
	id.length(ZuBase64URL::declen(input.id.length()), false);
	if (ZuBase64URL::decode(id, ZuBSpan{input.id}) != id.length() || !id ||
	    input.userID || input.appID) break;
	auto grants = m_context->grants;
	grants->run(0, [grants, id = ZuMv(id),
	    complete = ZuMv(complete)]() mutable {
	  grants->findUpd<0>(0, ZuFwdTuple(ZuMv(id)), [
	      complete = ZuMv(complete)](ZdbRow<Grant> *row) mutable {
	    if (!row) {
	      complete(adminErrorResult(404, "not_found", "grant not found"));
	      return;
	    }
	    if (row->data().owner || row->data().state == State::Consumed) {
	      complete(adminErrorResult(409, "conflict",
		"grant cannot be revoked"));
	      return;
	    }
	    if (row->data().state != State::Revoked) {
	      row->data().state = State::Revoked;
	      if (!row->commit()) {
		complete(adminErrorResult(503, "unavailable", "commit failed"));
		return;
	      }
	    }
	    complete(AdminResult{"{\"item\":{\"revoked\":1}}", 200});
	  });
	});
	return;
      }
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_context,
	BulkRevoke_::Grants, input.userID, {}, input.appID, 0,
	input.limit, ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::grantCleanup: {
      CleanupInput input;
      if (!adminBody(body, input) || input.before) break;
      if (!grantCleanup(m_requests, Zm::now() + ZuTime{15}, m_context,
	  Zm::now().sec(), input.limit, [complete = ZuMv(complete)](
	    int error, unsigned removed) mutable {
	  if (error != AdminError::OK) {
	    complete(adminErrorResult(error == AdminError::Invalid ? 400 : 503,
	      error == AdminError::Invalid ? "invalid_request" : "unavailable",
	      "grant cleanup failed"));
	    return;
	  }
	  String json{"{\"item\":{\"removed\":"};
	  json << removed << "}}";
	  complete(AdminResult{ZuMv(json), 200});
	})) complete(adminErrorResult(503, "unavailable", "request unavailable"));
      return;
    }
    case MgmtOp::auditCleanup: {
      CleanupInput input;
      if (!adminBody(body, input) || input.before <= 0) break;
      if (!auditCleanup(m_requests, Zm::now() + ZuTime{15}, m_context,
	  input.before, input.limit, [complete = ZuMv(complete)](
	    int error, unsigned removed) mutable {
	  if (error != AdminError::OK) {
	    complete(adminErrorResult(error == AdminError::Invalid ? 400 : 503,
	      error == AdminError::Invalid ? "invalid_request" : "unavailable",
	      "audit cleanup failed"));
	    return;
	  }
	  String json{"{\"item\":{\"removed\":"};
	  json << removed << "}}";
	  complete(AdminResult{ZuMv(json), 200});
	})) complete(adminErrorResult(503, "unavailable", "request unavailable"));
      return;
    }
    case MgmtOp::signKeyAdd: {
      SignKeyInput input;
      if (!adminBody(body, input) || input.algorithm != "ES256" ||
	  bool(input.providerRef) == bool(input.privateMaterial)) break;
      Bytes protectedMaterial;
      if (input.privateMaterial) {
	Bytes plain;
	plain.length(ZuBase64URL::declen(input.privateMaterial.length()), false);
	if (ZuBase64URL::decode(plain, ZuBSpan{input.privateMaterial}) !=
	    plain.length() || !plain || !serverSecretEncrypt(m_rng,
	      m_config.dbKey, m_config.issuer, "signKey", input.id,
	      "privateMaterial", plain, protectedMaterial)) {
	  if (plain) ZuClear(plain.data(), plain.length());
	  ZuClear(input.privateMaterial.data(), input.privateMaterial.length());
	  complete(adminErrorResult(400, "invalid_request",
	    "invalid signing private material"));
	  return;
	}
	ZuClear(plain.data(), plain.length());
	ZuClear(input.privateMaterial.data(), input.privateMaterial.length());
      }
      int64_t now = Zm::now().sec();
      SignKey key{.id = ZuMv(input.id), .issuer = m_config.issuer,
	.algorithm = ZuMv(input.algorithm),
	.providerRef = ZuMv(input.providerRef),
	.publicJwk = ZuMv(input.publicJwk),
	.privateMaterial = ZuMv(protectedMaterial),
	.notBefore = input.notBefore, .state = State::Active,
	.version = 1, .created = now, .updated = now};
      String resultID = key.id;
      if (!signKeyAdd(m_requests, Zm::now() + ZuTime{15}, m_context,
	  m_config.issuer, String{principal.subject}, ZuMv(key), now,
	  [resultID = ZuMv(resultID), complete = ZuMv(complete)](
	    int error) mutable {
	  if (error != AdminError::OK) {
	    complete(adminErrorResult(error == AdminError::Invalid ? 400 : 503,
	      error == AdminError::Invalid ? "invalid_request" : "unavailable",
	      "signing key creation failed"));
	    return;
	  }
	  String json{"{\"item\":{\"id\":"};
	  ZfJSON::quote(json, resultID);
	  json << "}}";
	  StringVec ids;
	  ids.push(resultID);
	  complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
	})) complete(adminErrorResult(503, "unavailable", "request unavailable"));
      return;
    }
    case MgmtOp::signKeyRetire: {
      String id;
      RetireInput input;
      if (!pathValue(target, "/admin/signing-keys/", "/retire", id) ||
	  !adminBody(body, input) || input.retireAfter <= Zm::now().sec()) break;
      adminUpdate(m_context->signKeys, ZuFwdTuple(String{id}), ZuMv(ifMatch),
	[input](SignKey &key) mutable {
	  if (key.state == State::Revoked || key.state == State::Consumed)
	    return false;
	  key.retireAfter = input.retireAfter;
	  key.state = State::Suspended;
	  return true;
	}, ZuMv(complete));
      return;
    }
    case MgmtOp::catalogPublish: {
      AppID appID;
      CatalogInput input;
      if (!adminTargetApp(target, appID) || !adminBody(body, input)) break;
      if (principal.authMethod || permit.superuser ||
	  permit.targetApp != appID ||
	  (!principal.clientID && !principal.subject)) {
	complete(adminErrorResult(403, "forbidden",
	  "catalog publisher does not own the application"));
	return;
      }
      ZmRef<CatalogPublish_> publish = new CatalogPublish_{m_context, &m_rng,
	appID, ZuMv(input), ZuMv(ifMatch), ZuMv(complete)};
      publish->start();
      return;
    }
    case MgmtOp::appState: {
      uint64_t id;
      if (pathUInt(target, "/admin/apps/", "/state", id)) {
	adminState(m_context->apps, ZuFwdTuple(AppID{id}), ZuMv(body),
	  ZuMv(ifMatch), false, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::userState: {
      uint64_t id;
      if (pathUInt(target, "/admin/users/", "/state", id)) {
	adminState(m_context->users, ZuFwdTuple(UserID{id}), ZuMv(body),
	  ZuMv(ifMatch), true, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::credentialState: {
      Bytes id;
      if (pathBytes(target, "/admin/credentials/", "/state", id)) {
	adminState(m_context->creds, ZuFwdTuple(ZuMv(id)), ZuMv(body),
	  ZuMv(ifMatch), false, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::membershipState:
    case MgmtOp::actionState:
    case MgmtOp::roleState:
    case MgmtOp::scopeState: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID;
      switch (op) {
	case MgmtOp::membershipState: prefix << "/memberships/"; break;
	case MgmtOp::actionState: prefix << "/actions/"; break;
	case MgmtOp::roleState: prefix << "/roles/"; break;
	default: prefix << "/scopes/"; break;
      }
      uint64_t id;
      if (!pathUInt(target, prefix, "/state", id)) break;
      switch (op) {
	case MgmtOp::membershipState:
	  adminState(m_context->memberships,
	    ZuFwdTuple(appID, UserID{id}), ZuMv(body), ZuMv(ifMatch),
	    false, ZuMv(complete)); return;
	case MgmtOp::actionState:
	  if (id > UINT32_MAX) break;
	  adminState(m_context->actions,
	    ZuFwdTuple(appID, static_cast<ActionID>(id)),
	    ZuMv(body), ZuMv(ifMatch),
	    false, ZuMv(complete)); return;
	case MgmtOp::roleState:
	  adminState(m_context->roles,
	    ZuFwdTuple(appID, RoleID{id}), ZuMv(body), ZuMv(ifMatch),
	    false, ZuMv(complete)); return;
	default:
	  adminState(m_context->scopes,
	    ZuFwdTuple(appID, ScopeID{id}), ZuMv(body), ZuMv(ifMatch),
	    false, ZuMv(complete)); return;
      }
      break;
    }
    case MgmtOp::audienceState: {
      uint64_t id;
      if (pathUInt(target, "/admin/audiences/", "/state", id)) {
	adminState(m_context->audiences, ZuFwdTuple(AudienceID{id}),
	  ZuMv(body), ZuMv(ifMatch), false, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::clientState: {
      String id;
      if (pathValue(target, "/admin/clients/", "/state", id)) {
	adminState(m_context->clients, ZuFwdTuple(String{id}), ZuMv(body),
	  ZuMv(ifMatch), false, ZuMv(complete));
	return;
      }
      break;
    }
    case MgmtOp::providerState: {
      uint64_t id;
      if (pathUInt(target, "/admin/providers/", "/state", id)) {
	adminState(m_context->providers, ZuFwdTuple(ProviderID{id}),
	  ZuMv(body), ZuMv(ifMatch), false, ZuMv(complete));
	return;
      }
      break;
    }
    default:
      break;
  }
  complete(AdminResult{
    error_("invalid_request", "invalid operation input"), 400});
}

static void serviceEscape(String &out, ZuCSpan value)
{
  static constexpr char hex[] = "0123456789ABCDEF";
  for (uint8_t c: ZuBSpan{value}) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	(c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
	c == '~') out << char(c);
    else out << '%' << hex[c >> 4] << hex[c & 15];
  }
}

static void serviceField(
    String &query, bool &first, ZuCSpan name, ZuCSpan value)
{
  query << (first ? '?' : '&');
  first = false;
  serviceEscape(query, name);
  query << '=';
  serviceEscape(query, value);
}

static bool serviceAuthorizeQuery(String body, String &query)
{
  static constexpr ZuCSpan names[] = {
    "clientID", "redirectURI", "responseType", "scope", "resource",
    "state", "codeChallenge", "codeChallengeMethod", "nonce", "prompt",
    "maxAge"
  };
  ServiceAuthorizeInput input;
  uint64_t seen = 0;
  constexpr uint64_t required = (uint64_t{1}<<0) | (uint64_t{1}<<1) |
    (uint64_t{1}<<2) | (uint64_t{1}<<3) | (uint64_t{1}<<6) |
    (uint64_t{1}<<7);
  if (!adminBodyFields(body, input, names, seen) ||
      (seen & required) != required || !input.clientID ||
      !input.redirectURI || !input.responseType || !input.scope ||
      !input.codeChallenge || !input.codeChallengeMethod) return false;
  bool first = true;
  serviceField(query, first, "client_id", input.clientID);
  serviceField(query, first, "redirect_uri", input.redirectURI);
  serviceField(query, first, "response_type", input.responseType);
  serviceField(query, first, "scope", input.scope);
  if (seen & (uint64_t{1}<<4))
    serviceField(query, first, "resource", input.resource);
  if (seen & (uint64_t{1}<<5))
    serviceField(query, first, "state", input.state);
  serviceField(query, first, "code_challenge", input.codeChallenge);
  serviceField(query, first, "code_challenge_method",
    input.codeChallengeMethod);
  if (seen & (uint64_t{1}<<8))
    serviceField(query, first, "nonce", input.nonce);
  if (seen & (uint64_t{1}<<9))
    serviceField(query, first, "prompt", input.prompt);
  if (seen & (uint64_t{1}<<10)) {
    String value;
    value << input.maxAge;
    serviceField(query, first, "max_age", value);
  }
  return true;
}

static bool serviceForm(String body, String &form)
{
  static constexpr ZuCSpan names[] = {"form"};
  ServiceFormInput input;
  uint64_t seen = 0;
  if (!adminBodyFields(body, input, names, seen) || seen != 1 || !input.form)
    return false;
  form = ZuMv(input.form);
  return true;
}

void Daemon::serviceCall_(
    int operation, String authorization, String body, AdminDoneFn complete)
{
  static constexpr ZuCSpan prefix{"Bearer "};
  Principal principal;
  String audience{m_config.issuer};
  if (audience && audience[audience.length() - 1] == '/')
    audience.length(audience.length() - 1);
  audience << "/admin";
  unsigned actionID = operation == ServiceOp::Authorize ?
    CoreAction::FacadeAuthorize : operation == ServiceOp::Token ?
      CoreAction::FacadeToken : CoreAction::FacadeRevoke;
  bool authenticated = authorization.length() > prefix.length() &&
    ZuCSpan{authorization}.prefix(prefix) == prefix.length();
  if (authenticated) authorization.splice(0, prefix.length());
  authenticated = authenticated && jwtVerify(authorization, "bootstrap",
    m_config.issuer, audience, m_publicKey, Zm::now().sec(), JWTLimits{},
    principal) && !principal.authMethod && principal.appID &&
    principal.clientID;
  bool permitted = false;
  auto required = coreAction(actionID);
  if (authenticated)
    for (const auto &action: principal.actions)
      if (action == required) { permitted = true; break; }
  if (!authenticated || !permitted) {
    complete(AdminResult{error_(authenticated ? "forbidden" :
      "invalid_token", authenticated ? "service operation is not permitted" :
      "invalid service bearer token"), authenticated ? 403U : 401U});
    return;
  }
  auto issuers = m_context->issuers;
  issuers->run(0, [this, issuers, operation,
      principal = ZuMv(principal), body = ZuMv(body),
      complete = ZuMv(complete)]() mutable {
    String issuer = m_config.issuer;
    issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [this, operation,
        principal = ZuMv(principal), body = ZuMv(body),
        complete = ZuMv(complete)](ZdbRowRef<Issuer> row) mutable {
      if (!row || row->data().bootstrapPhase != BootstrapPhase::Ready) {
        complete(AdminResult{
          error_("temporarily_unavailable", "server is not ready"), 503});
        return;
      }
      auto clients = m_context->clients;
      clients->run(0, [this, clients, operation,
          principal = ZuMv(principal), body = ZuMv(body),
          complete = ZuMv(complete)]() mutable {
        String clientID = principal.clientID;
        clients->find<0>(0, ZuFwdTuple(ZuMv(clientID)), [this, operation,
            principal = ZuMv(principal), body = ZuMv(body),
            complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
          if (!row || row->data().owner ||
              row->data().state != State::Active ||
              row->data().type != ClientType::Confidential ||
              row->data().appID != principal.appID) {
            complete(AdminResult{
              error_("invalid_token", "inactive service client"), 401});
            return;
          }
          auto apps = m_context->apps;
          apps->run(0, [this, apps, operation,
              principal = ZuMv(principal), body = ZuMv(body),
              complete = ZuMv(complete)]() mutable {
            apps->find<0>(0, ZuFwdTuple(principal.appID), [this, operation,
                principal = ZuMv(principal), body = ZuMv(body),
                complete = ZuMv(complete)](ZdbRowRef<App> row) mutable {
              if (!row || row->data().owner ||
                  row->data().state != State::Active ||
                  row->data().serviceClientID != principal.clientID) {
                complete(AdminResult{
                  error_("invalid_token", "inactive service application"),
                  401});
                return;
              }
              String input;
              bool valid = operation == ServiceOp::Authorize ?
                serviceAuthorizeQuery(ZuMv(body), input) :
                serviceForm(ZuMv(body), input);
              if (!valid) {
                complete(AdminResult{
                  oauthErrorJSON(OAuthError::InvalidRequest), 400});
                return;
              }
              auto finish = [complete = ZuMv(complete)](
                  ServerReply reply) mutable {
                unsigned status = reply.type == ReplyType::OK ||
                  reply.type == ReplyType::Empty ? 200 :
                  reply.type == ReplyType::ServerError ? 503 : 400;
                complete(AdminResult{ZuMv(reply.body), status});
              };
              if (operation == ServiceOp::Authorize)
                m_provider.facadeAuthorize(ZuMv(input),
                  String{principal.clientID}, principal.appID, ZuMv(finish));
              else if (operation == ServiceOp::Token)
                m_provider.token(ZuMv(input), {},
                  String{principal.clientID}, ZuMv(finish));
              else
                m_provider.revoke(ZuMv(input), {},
                  String{principal.clientID}, ZuMv(finish));
            });
          });
        });
      });
    });
  });
}

bool Daemon::adminAuth_(ZuCSpan authorization,
    Principal &principal) const
{
  static constexpr ZuCSpan prefix{"Bearer "};
  if (authorization.length() <= prefix.length() ||
      authorization.prefix(prefix) != prefix.length()) return false;
  authorization.offset(prefix.length());
  String audience{m_config.issuer};
  if (audience[audience.length() - 1] == '/')
    audience.length(audience.length() - 1);
  audience << "/admin";
  return jwtVerify(authorization, "bootstrap", m_config.issuer, audience,
    m_publicKey, Zm::now().sec(), JWTLimits{}, principal);
}

static bool decodeSubject(ZuCSpan subject, Bytes &handle)
{
  if (!subject) return false;
  Bytes next;
  next.length(ZuBase64URL::declen(subject.length()), false);
  if (ZuBase64URL::decode(next, ZuBSpan{subject}) != next.length())
    return false;
  handle = ZuMv(next);
  return handle;
}

static void adminDelegation(
    DBContext *context, ActorKind::T actorKind, String actorID,
    AppID appID, int op, AdminAccessFn complete)
{
  auto access = context->adminAccess;
  access->run(0, [access, actorKind, actorID = ZuMv(actorID), appID, op,
      complete = ZuMv(complete)]() mutable {
    access->find<0>(0,
      ZuFwdTuple(actorKind, ZuMv(actorID), appID), [op, appID,
        complete = ZuMv(complete)](ZdbRowRef<AdminAccess> row) mutable {
      if (!row || row->data().state != State::Active) {
	complete(AdminPermit{.targetApp = appID});
	return;
      }
      for (auto permitted: row->data().operationIDs)
	if (permitted == unsigned(op)) {
	  complete(AdminPermit{.roleIDs = row->data().roleIDs,
	    .targetApp = appID, .allowed = true});
	  return;
	}
      complete(AdminPermit{.targetApp = appID});
    });
  });
}

void Daemon::adminAccess_(
    int op, String subject, bool interactive, AppID targetApp, bool appScoped,
    AdminAccessFn complete)
{
  static constexpr RoleID superuserRoleID = CoreRole::Superuser;
  if (!interactive) {
    auto clients = m_context->clients;
    String clientID = ZuMv(subject);
    clients->run(0, [this, clients, clientID = ZuMv(clientID),
        targetApp, appScoped, op, complete = ZuMv(complete)]() mutable {
      String lookup = clientID;
      clients->find<0>(0, ZuFwdTuple(ZuMv(lookup)), [this,
          clientID = ZuMv(clientID), targetApp, appScoped, op,
          complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	if (!row || row->data().state != State::Active) {
	  complete(AdminPermit{});
	  return;
	}
	if (!appScoped && op == MgmtOp::operationQuery) {
	  targetApp = row->data().appID;
	  appScoped = true;
	}
	if (!appScoped) { complete(AdminPermit{}); return; }
	adminDelegation(m_context, ActorKind::Client, ZuMv(clientID),
	  targetApp, op, ZuMv(complete));
      });
    });
    return;
  }

  Bytes handle;
  if (!decodeSubject(subject, handle)) {
    complete(AdminPermit{});
    return;
  }
  auto users = m_context->users;
  users->run(0, [this, users, handle = ZuMv(handle), targetApp,
      appScoped, op, complete = ZuMv(complete)]() mutable {
    users->find<1>(0, ZuFwdTuple(ZuMv(handle)), [this, targetApp,
        appScoped, op, complete = ZuMv(complete)](
          ZdbRowRef<User> user) mutable {
      if (!user || user->data().source != UserSource::Local ||
          user->data().state != State::Active) {
	complete(AdminPermit{});
	return;
      }
      UserID userID = user->data().id;
      auto issuers = m_context->issuers;
      issuers->run(0, [this, issuers, userID, targetApp, appScoped, op,
          complete = ZuMv(complete)]() mutable {
	String issuer = m_config.issuer;
	issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [this, userID,
	    targetApp, appScoped, op, complete = ZuMv(complete)](
	      ZdbRowRef<Issuer> issuer) mutable {
	  if (!issuer) { complete(AdminPermit{}); return; }
	  AppID coreApp = issuer->data().coreAppID;
	  auto memberships = m_context->memberships;
	  memberships->run(0, [this, memberships, userID, coreApp,
	      targetApp, appScoped, op, complete = ZuMv(complete)]() mutable {
	    memberships->find<0>(0, ZuFwdTuple(coreApp, userID), [this,
	        userID, coreApp, targetApp, appScoped, op,
	        complete = ZuMv(complete)](
	          ZdbRowRef<Membership> membership) mutable {
	      bool assigned = false;
	      if (membership && membership->data().state == State::Active)
	        for (auto roleID: membership->data().roleIDs)
	          if (roleID == superuserRoleID) { assigned = true; break; }
	      if (!assigned) {
	        if (!appScoped) { complete(AdminPermit{}); return; }
	        String actorID;
	        actorID << userID;
	        adminDelegation(m_context, ActorKind::User, ZuMv(actorID),
	          targetApp, op, ZuMv(complete));
	        return;
	      }
	      auto roles = m_context->roles;
	      roles->run(0, [roles, coreApp,
	          complete = ZuMv(complete)]() mutable {
	        roles->find<0>(0, ZuFwdTuple(coreApp, superuserRoleID), [coreApp,
	            complete = ZuMv(complete)](ZdbRowRef<Role> role) mutable {
	          bool allowed = role && role->data().state == State::Active &&
	            !role->data().tombstone;
	          complete(AdminPermit{.targetApp = coreApp,
	            .allowed = allowed, .superuser = allowed});
	        });
	      });
	    });
	  });
	});
      });
    });
  });
}

Bytes Daemon::idemDigest_(int op,
    ZuCSpan target, ZuCSpan body, ZuCSpan ifMatch, ZuCSpan ifNoneMatch)
{
  String canonical;
  canonical << op << '\n' << target << '\n' << ifMatch << '\n' <<
    ifNoneMatch << '\n' << body;
  Bytes digest;
  digest.length(Ztls::MD<>::Size, false);
  Ztls::MD<> md;
  md.update(ZuBSpan{canonical});
  md.finish(digest.span());
  return digest;
}

void Daemon::idemBegin_(int op, ActorKind::T actorKind, String actorID,
    Bytes digest, String idemKey, IdemBeginFn complete)
{
  if (!idemKey) {
    complete(IdemBegin{.execute = true});
    return;
  }
  auto requests = m_context->requests;
  requests->run(0, [requests, actorKind, actorID = ZuMv(actorID), op,
      idemKey = ZuMv(idemKey), digest = ZuMv(digest),
      complete = ZuMv(complete)]() mutable {
    String lookupActor = actorID;
    String lookupKey = idemKey;
    requests->find<0>(0, ZuFwdTuple(actorKind, ZuMv(lookupActor),
      ActionID(op), ZuMv(lookupKey)), [requests, actorKind,
        actorID = ZuMv(actorID), op, idemKey = ZuMv(idemKey),
        digest = ZuMv(digest), complete = ZuMv(complete)](
          ZdbRowRef<IdemRequest> row) mutable {
      if (row) {
	if (!Ztls::ctEqual(row->data().requestDigest, digest)) {
	  complete(IdemBegin{.result = AdminResult{
	    "{\"error\":\"conflict\",\"message\":\"idempotency key reused with different request\","
	    "\"correlationID\":\"\"}", 409}});
	  return;
	}
	String body{"{\"operationID\":"};
	ZfJSON::quote(body, idemKey);
	if (row->data().status == RequestStatus::Complete) {
	  body << ",\"status\":\"complete\",\"resultIDs\":[";
	  for (unsigned i = 0; i < row->data().resultIDs.length(); ++i) {
	    if (i) body << ',';
	    ZfJSON::quote(body, row->data().resultIDs[i]);
	  }
	  body << "]}";
	  complete(IdemBegin{.result = AdminResult{ZuMv(body), 200}});
	} else if (row->data().status == RequestStatus::Pending) {
	  body << ",\"status\":\"pending\"}";
	  complete(IdemBegin{.result = AdminResult{ZuMv(body), 202}});
	} else {
	  complete(IdemBegin{.result = AdminResult{
	    "{\"error\":\"conflict\",\"message\":\"previous request failed\","
	    "\"correlationID\":\"\"}", 409}});
	}
	return;
      }
      int64_t now = Zm::now().sec();
      IdemRequest request{.actorKind = actorKind, .actorID = actorID,
	.operation = ActionID(op), .idempotencyKey = idemKey,
	.requestDigest = digest, .status = RequestStatus::Pending,
	.expires = now + 86400, .version = 1,
	.created = now, .updated = now};
      ZdbRowRef<IdemRequest> next =
	new ZdbRow<IdemRequest>{requests, ZdbShard{0}};
      requests->insert(ZuMv(next), [actorKind, actorID = ZuMv(actorID),
	  idemKey = ZuMv(idemKey), digest = ZuMv(digest),
	  request = ZuMv(request), complete = ZuMv(complete)](
	    ZdbRow<IdemRequest> *row) mutable {
	if (!row) {
	  complete(IdemBegin{.result = AdminResult{
	    "{\"error\":\"conflict\",\"message\":\"concurrent idempotent request\","
	    "\"correlationID\":\"\"}", 409}});
	  return;
	}
	new (row->ptr()) IdemRequest{ZuMv(request)};
	if (!row->commit()) {
	  complete(IdemBegin{.result = AdminResult{
	    "{\"error\":\"unavailable\",\"message\":\"request commit failed\","
	    "\"correlationID\":\"\"}", 503}});
	  return;
	}
	complete(IdemBegin{.actorKind = actorKind,
	  .actorID = ZuMv(actorID), .digest = ZuMv(digest),
	  .execute = true});
      });
    });
  });
}

void Daemon::idemFinish_(ActorKind::T actorKind, String actorID, int op,
    String key, Bytes digest, AdminResult result, AdminDoneFn complete)
{
  if (!key) { complete(ZuMv(result)); return; }
  auto requests = m_context->requests;
  requests->run(0, [requests, actorKind, actorID = ZuMv(actorID), op,
      key = ZuMv(key), digest = ZuMv(digest), result = ZuMv(result),
      complete = ZuMv(complete)]() mutable {
    String lookupActor = actorID;
    String lookupKey = key;
    requests->findUpd<0>(0, ZuFwdTuple(actorKind, ZuMv(lookupActor),
      ActionID(op), ZuMv(lookupKey)), [digest = ZuMv(digest),
        result = ZuMv(result), complete = ZuMv(complete)](
          ZdbRow<IdemRequest> *row) mutable {
      if (!row || row->data().status != RequestStatus::Pending ||
	  !Ztls::ctEqual(row->data().requestDigest, digest)) {
	complete(AdminResult{
	  "{\"error\":\"unavailable\",\"message\":\"request finalization failed\","
	  "\"correlationID\":\"\"}", 503});
	return;
      }
      row->data().status = result.status >= 200 && result.status < 300 ?
	RequestStatus::Complete : RequestStatus::Failed;
      if (row->data().status == RequestStatus::Complete)
	row->data().resultIDs = result.resultIDs;
      ++row->data().version;
      row->data().updated = Zm::now().sec();
      if (!row->commit()) {
	complete(AdminResult{
	  "{\"error\":\"unavailable\",\"message\":\"request finalization failed\","
	  "\"correlationID\":\"\"}", 503});
	return;
      }
      complete(ZuMv(result));
    });
  });
}

String Daemon::page_(Bytes ceremonyID, String options)
{
  String page;
  page << "<!doctype html><meta charset=utf-8><title>Zum login</title>"
    "<meta name=referrer content=no-referrer><h1>Zum login</h1>"
    "<label>Login <input id=user autocomplete=username></label>"
    "<button id=route>Continue</button>"
    "<p><button id=login>Use passkey</button></p><pre id=out></pre><script>"
    "const id='" << encode(ceremonyID) << "',o=" << options << ";"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "return x}function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),"
    "type:c.type,response:{clientDataJSON:e(r.clientDataJSON),"
    "authenticatorData:e(r.authenticatorData),signature:e(r.signature),"
    "userHandle:r.userHandle?e(r.userHandle):null}}}"
    "route.onclick=()=>{const v=user.value.trim();if(!v)return;"
    "const f=document.createElement('form');f.method='post';f.action='/login';"
    "for(const [n,x] of [['id',id],['login',v]]){const i=document.createElement('input');"
    "i.type='hidden';i.name=n;i.value=x;f.append(i)}document.body.append(f);f.submit()};"
    "login.onclick=async()=>{try{out.textContent='Waiting for passkey';"
    "const c=await navigator.credentials.get(opts(o));const r=await fetch("
    "'/passkey/finish?id='+id,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c)),redirect:'error'});if(!r.ok)throw Error(await r.text());"
    "if((r.headers.get('content-type')||'').startsWith('text/html')){"
    "const html=await r.text();document.open();document.write(html);document.close();return}"
    "const j=await r.json();if(!j.redirectURI)throw Error('missing redirect');"
    "location.assign(j.redirectURI)}"
    "catch(x){out.textContent=x}}</script>";
  return page;
}

String Daemon::bootstrapPage_()
{
  return String{
    "<!doctype html><meta charset=utf-8><title>Enroll Zum administrator</title>"
    "<meta name=referrer content=no-referrer><h1>Enroll Zum administrator</h1>"
    "<button id=enroll>Create passkey</button><pre id=out></pre><script>"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "x.publicKey.user.id=d(x.publicKey.user.id);return x}"
    "function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),type:c.type,"
    "response:{clientDataJSON:e(r.clientDataJSON),attestationObject:e(r.attestationObject)}}}"
    "enroll.onclick=async()=>{try{const cap=new URLSearchParams(location.search).get('capability');"
    "if(!cap)throw Error('missing capability');out.textContent='Creating passkey';"
    "let r=await fetch('/passkey/begin',{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify({purpose:'bootstrap',capability:cap})}),j=await r.json();"
    "if(!r.ok)throw Error(JSON.stringify(j));const c=await navigator.credentials.create(opts(j.options));"
    "r=await fetch('/passkey/finish?id='+j.ceremony,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c))});if(!r.ok)throw Error(await r.text());"
    "history.replaceState({},'',location.pathname);out.textContent='Administrator enrolled'}"
    "catch(x){out.textContent=x}}</script>"};
}

String Daemon::enrollPage_()
{
  return String{
    "<!doctype html><meta charset=utf-8><title>Enroll Zum passkey</title>"
    "<meta name=referrer content=no-referrer><h1>Enroll Zum passkey</h1>"
    "<button id=enroll>Create passkey</button><pre id=out></pre><script>"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "x.publicKey.user.id=d(x.publicKey.user.id);return x}"
    "function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),type:c.type,"
    "response:{clientDataJSON:e(r.clientDataJSON),attestationObject:e(r.attestationObject)}}}"
    "enroll.onclick=async()=>{try{const cap=new URLSearchParams(location.search).get('capability');"
    "if(!cap)throw Error('missing capability');out.textContent='Creating passkey';"
    "const purpose=new URLSearchParams(location.search).get('purpose')||'enrollment';"
    "let r=await fetch('/passkey/begin',{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify({purpose,capability:cap})}),j=await r.json();"
    "if(!r.ok)throw Error(JSON.stringify(j));const c=await navigator.credentials.create(opts(j.options));"
    "r=await fetch('/passkey/finish?id='+j.ceremony,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c))});if(!r.ok)throw Error(await r.text());"
    "history.replaceState({},'',location.pathname);out.textContent='Passkey enrolled'}"
    "catch(x){out.textContent=x}}</script>"};
}

class AuthRouteLoad_ : public ZmObject {
public:
  AuthRouteLoad_(DBContext *context, Bytes dbKey, String localIssuer,
      AppID appID, String login, AuthRouteDoneFn complete) :
    m_context{context}, m_dbKey{ZuMv(dbKey)},
    m_localIssuer{ZuMv(localIssuer)}, m_appID{appID},
    m_login{ZuMv(login)}, m_complete{ZuMv(complete)} { }

  ~AuthRouteLoad_()
  {
    if (m_dbKey && m_dbKey.mutable_())
      ZuClear(m_dbKey.data(), m_dbKey.length());
  }

  void start()
  {
    if (!m_context || !m_appID || !loginNormalize(m_login)) {
      finish_(AuthRouteType::Error);
      return;
    }
    auto users = m_context->users;
    users->run(0, [self = ZmRef<AuthRouteLoad_>{this}, users]() mutable {
      users->find<2>(0, ZuFwdTuple(UserSource::Local, self->m_login), [
          self = ZuMv(self)](ZdbRowRef<User> user) mutable {
        // Presence is authoritative: a disabled or suspended local identity
        // must never fall through to an upstream identity of the same name.
        if (user) { self->finish_(AuthRouteType::Local); return; }
        self->policy_();
      });
    });
  }

private:
  void policy_()
  {
    auto policies = m_context->authPolicies;
    policies->find<0>(0, ZuFwdTuple(m_appID), [
        self = ZmRef<AuthRouteLoad_>{this}](ZdbRowRef<AuthPolicy> row) mutable {
      if (!row || row->data().state != State::Active ||
          row->data().owner || !row->data().providerID) {
        self->finish_(AuthRouteType::Local);
        return;
      }
      const auto &policy = row->data();
      if (!policy.localFirst || !policy.assignmentMaxAge ||
          (policy.eligibilityMode == EligibilityMode::ClaimValues &&
           (!policy.eligibilityClaim || !policy.eligibilityValues))) {
        self->finish_(AuthRouteType::Error);
        return;
      }
      self->m_providerID = policy.providerID;
      self->m_config.appID = policy.appID;
      self->m_config.providerID = policy.providerID;
      self->m_config.policyVersion = policy.version;
      self->m_config.assignmentMaxAge = policy.assignmentMaxAge;
      self->m_config.eligibilityMode = policy.eligibilityMode;
      self->m_config.eligibilityClaim = policy.eligibilityClaim;
      self->m_config.eligibilityValues = policy.eligibilityValues;
      self->provider_();
    });
  }

  void provider_()
  {
    auto providers = m_context->providers;
    providers->find<0>(0, ZuFwdTuple(m_providerID), [
        self = ZmRef<AuthRouteLoad_>{this}](ZdbRowRef<Provider> row) mutable {
      if (!row || row->data().state != State::Active || row->data().owner ||
          !row->data().issuer || !row->data().clientID ||
          !row->data().roleClaim || !row->data().scopes ||
          row->data().claimSource != ClaimSource::IDToken) {
        self->finish_(AuthRouteType::Error);
        return;
      }
      const auto &provider = row->data();
      self->m_config.issuer = provider.issuer;
      self->m_config.clientID = provider.clientID;
      self->m_config.oidcScopes = provider.scopes;
      self->m_config.roleClaim = provider.roleClaim;
      self->m_config.claimSource = provider.claimSource;
      self->m_config.roles = OIDCRoles::Mapped;
      self->m_config.clientAuth = provider.clientSecret ?
        OIDCClientAuth::Basic : OIDCClientAuth::None;
      self->m_config.redirectURI = self->m_localIssuer;
      if (self->m_config.redirectURI[
          self->m_config.redirectURI.length() - 1] == '/')
        self->m_config.redirectURI.length(
          self->m_config.redirectURI.length() - 1);
      self->m_config.redirectURI << "/oidc/callback";
      if (provider.clientSecret) {
        String recordID;
        recordID << provider.id;
        Bytes plain;
        if (!serverSecretDecrypt(self->m_dbKey, self->m_localIssuer,
            "provider", recordID, "clientSecret", provider.clientSecret,
            plain)) {
          self->finish_(AuthRouteType::Error);
          return;
        }
        self->m_config.clientSecret = ZuCSpan{
          reinterpret_cast<const char *>(plain.data()), plain.length()};
        ZuClear(plain.data(), plain.length());
      }
      self->maps_();
    });
  }

  void maps_()
  {
    using Table = RoleMapTable;
    using Tuple = Table::Tuple;
    auto maps = m_context->roleMaps;
    maps->selectRows<0>(ZuFwdTuple(m_appID, m_providerID), 257, [
        self = ZmRef<AuthRouteLoad_>{this}](
          ZuUnion<void, Tuple> result, unsigned count) mutable {
      if (result.template is<Tuple>()) {
        if (count > 256) { self->m_overflow = true; return; }
        auto tuple = ZuMv(result).template p<Tuple>();
        ZuTupleCall(ZuMv(tuple), [self](auto &&...args) mutable {
          RoleMap map{ZuFwd<decltype(args)>(args)...};
          if (map.state == State::Active && !map.owner && map.roleID)
            self->m_maps.push(OIDCRoleMap{
              ZuMv(map.value), map.roleID});
        });
        return;
      }
      if (self->m_overflow || !self->m_maps) {
        self->finish_(AuthRouteType::Error);
        return;
      }
      self->role_();
    });
  }

  void role_()
  {
    if (m_mapIndex >= m_maps.length()) {
      if (!oidcConfigValid(m_config)) finish_(AuthRouteType::Error);
      else finish_(AuthRouteType::Upstream);
      return;
    }
    auto roles = m_context->roles;
    auto map = m_maps[m_mapIndex++];
    roles->find<0>(0, ZuFwdTuple(m_appID, map.roleID), [
        self = ZmRef<AuthRouteLoad_>{this}, map = ZuMv(map)](
          ZdbRowRef<Role> row) mutable {
      if (row && row->data().state == State::Active &&
          !row->data().owner && !row->data().tombstone)
        self->m_config.roleMap.push(ZuMv(map));
      self->role_();
    });
  }

  void finish_(unsigned type)
  {
    if (m_done) return;
    m_done = true;
    if (type != AuthRouteType::Upstream && m_config.clientSecret &&
        m_config.clientSecret.mutable_())
      ZuClear(m_config.clientSecret.data(), m_config.clientSecret.length());
    auto complete = ZuMv(m_complete);
    complete(AuthRoute{.oidc = type == AuthRouteType::Upstream ?
      ZuMv(m_config) : OIDCConfig{}, .type = type});
  }

  DBContext		*m_context = nullptr;
  Bytes			m_dbKey;
  String		m_localIssuer;
  AppID			m_appID = 0;
  ProviderID		m_providerID = 0;
  String		m_login;
  OIDCConfig		m_config;
  OIDCRoleMapVec	m_maps;
  unsigned		m_mapIndex = 0;
  AuthRouteDoneFn	m_complete;
  bool			m_overflow = false;
  bool			m_done = false;
};

void Daemon::authRoute_(
    AppID appID, String login, AuthRouteDoneFn complete)
{
  ZmRef<AuthRouteLoad_> load = new AuthRouteLoad_{
    m_context, m_config.dbKey, m_config.issuer, appID,
    ZuMv(login), ZuMv(complete)};
  load->start();
}

bool Daemon::loadKey_()
{
  SignKey key = ZmBlock<SignKey>{}([this](auto wake) mutable {
    m_context->signKeys->run(0, [this, wake = ZuMv(wake)]() mutable {
      m_context->signKeys->find<0>(0, ZuFwdTuple(ZuCSpan{"bootstrap"}), [
          wake = ZuMv(wake)](ZdbRowRef<SignKey> row) mutable {
        wake(row ? SignKey{row->data()} : SignKey{});
      });
    });
  });
  if (!key.id || key.state != State::Active || !key.privateMaterial) return false;
  Bytes privateKey;
  if (!serverSecretDecrypt(m_config.dbKey, m_config.issuer,
      "zum.sign_key", key.id, "privateMaterial", key.privateMaterial,
      privateKey)) return false;
  try {
    m_key = new Ztls::PK::SK_EC{m_rng,
      Ztls::PK::OIDs::EC_GRP_SECP256R1, privateKey};
    unsigned n = Ztls::Backend::pkey_ec_public_size(m_key->key);
    m_publicKey.length(n);
    if (!n || !Ztls::Backend::pkey_ec_export_public(
        m_key->key, m_publicKey)) {
      ZuClear(privateKey.data(), privateKey.length());
      m_key = nullptr;
      m_publicKey.null();
      return false;
    }
  } catch (...) {
    ZuClear(privateKey.data(), privateKey.length());
    return false;
  }
  ZuClear(privateKey.data(), privateKey.length());
  return true;
}

bool Daemon::init(
    DB *db, DBContext *context, Requests *requests, ZiMultiplex *mx,
    DaemonConfig config)
{
  m_db = db;
  m_context = context;
  m_requests = requests;
  m_mx = mx;
  m_config = ZuMv(config);
  if (!m_db || !m_context || !m_requests || !m_mx || !m_config.issuer ||
      !m_config.rpID || !m_config.admin || !m_rng.init() || !loadKey_())
    return false;
  ServerConfig provider{
    .issuer = m_config.issuer, .rpID = m_config.rpID,
    .rpName = m_config.rpName, .keyID = "bootstrap",
    .publicKey = m_publicKey, .authMethod = AuthMethod::LocalFirst};
  OIDCHTTPFn upstreamHTTP = m_config.upstreamHTTP;
  if (!upstreamHTTP) upstreamHTTP = OIDCHTTPFn{[](
      OIDCHTTPRequest, OIDCHTTPDoneFn complete) {
    complete(503, String{});
  }};
  if (!m_provider.init(m_db, m_context, m_requests, ZuMv(provider),
      []() { return Zm::now().sec(); },
      [](Bytes id, String options) {
        return page_(ZuMv(id), ZuMv(options));
      },
      [](const User &, const Client &, const ScopeSelection &,
          const ZtBitmap &allowed, PolicyDoneFn complete) {
        complete(true, ZtBitmap{allowed});
      },
      [this](PasskeyStart start, AdmitDoneFn complete) {
        PasskeyAdmission admission;
        if (start.type == PasskeyStartType::Enrollment && start.capability) {
          admission.allowed = true;
        } else if (start.type == PasskeyStartType::Recovery &&
            start.capability) {
          admission.allowed = true;
          admission.recovery.displayName = "Zum user";
          admission.recovery.label = "Recovered Zum passkey";
        } else if (start.type == PasskeyStartType::Bootstrap) {
          admission.allowed = true;
          admission.enrollment.name = m_config.admin;
          admission.enrollment.displayName = m_config.admin;
          admission.enrollment.roleIDs.push(1);
          admission.enrollment.label = "Zum administrator passkey";
          admission.enrollment.userID = m_config.bootstrap.adminUserID;
        }
        complete(ZuMv(admission));
      },
      [this](ZuCSpan provider, ZuBSpan digest, SignatureFn complete) {
        Bytes signature;
        if (!provider) {
          auto result = m_key->sign(m_rng, digest, [&signature](ZuBSpan der) {
            signature = Bytes{der};
          });
          if (result.template is<ZeException>()) signature.null();
        }
        complete(ZuMv(signature));
      }, ZuMv(upstreamHTTP), [this](
          AppID appID, String login, AuthRouteDoneFn complete) {
        authRoute_(appID, ZuMv(login), ZuMv(complete));
      })) return false;
  HTTP<Daemon>::init(m_provider);
  DaemonParser parser;
  parser.init(*this);
  auto http = Zhttp::ServerConfig().localIP(ZiIP(m_config.addr))
    .port(m_config.port).idleTimeout(30).retainedBodyMax(64U<<10).tcp();
  m_httpInited = m_http.init(
    Zhttp::HubConfig{m_mx, "rx", "tx"}, ZuMv(http), this);
  return m_httpInited;
}

bool Daemon::start()
{
  return m_httpInited && m_http.start();
}

void Daemon::stop()
{
  if (m_httpInited) (void)m_http.stop();
}

void Daemon::final()
{
  if (m_httpInited) {
    m_http.final();
    m_httpInited = false;
  }
  m_provider.final();
  m_key = nullptr;
  m_publicKey.null();
  if (m_config.dbKey && m_config.dbKey.mutable_())
    ZuClear(m_config.dbKey.data(), m_config.dbKey.length());
}

void Daemon::listening(int, unsigned) { }
void Daemon::listenFailed(int, bool) { }

} // namespace Zum
