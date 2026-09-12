//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZumDaemon.hh"
#include <zlib/ZumDB.hh>
#include <zlib/ZumKeyDB.hh>

#include <zlib/ZumAdmin.hh>
#include <zlib/ZumService.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

// 144 bits yields a compact 24-character public identifier; confidential
// client secrets use 256 bits.  Enrollment draws both in one RNG call.
enum {
  ClientIDEntropySize = 18,
  ClientSecretEntropySize = 32,
  ClientCredentialEntropySize =
    ClientIDEntropySize + ClientSecretEntropySize
};

static AdminResult adminErrorResult(
  unsigned status, ZuCSpan error, ZuCSpan message);
template <typename Record>
static AdminResult adminItemResult(const Record &, unsigned, StringVec = {});

// Admission and completion must use the same decision.

template <typename T, typename = void>
struct HasVersion : public ZuFalse { };
template <typename T>
struct HasVersion<T, decltype((void)ZuDeclVal<T>().version)> : public ZuTrue { };
template <typename T, typename = void>
struct HasOwner : public ZuFalse { };
template <typename T>
struct HasOwner<T, decltype((void)ZuDeclVal<T>().owner)> : public ZuTrue { };

static String rowETag(uint64_t);

struct AdminErrorWire {
  String error;
  String message;
  String correlationID;
};
ZfStruct(, (AdminErrorWire, JSON),
  (((error),		(Required)),	(String)),
  (((message),		(Required)),	(String)),
  (((correlationID),	(Required)),	(String)));
struct AdminETagItem { String etag; };
ZfStruct(, (AdminETagItem, JSON),
  (((etag),		(Required)),	(String)));
struct AdminETagReply { AdminETagItem item; };
ZfStruct(, (AdminETagReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminMembershipItem {
  AppID appID = 0;
  UserID userID = 0;
  String etag;
};
ZfStruct(, (AdminMembershipItem, JSON),
  (((appID),		(JSON::String<>, Required)), (UInt64)),
  (((userID),		(JSON::String<>, Required)), (UInt64)),
  (((etag),		(Required)),	(String)));
struct AdminMembershipReply { AdminMembershipItem item; };
ZfStruct(, (AdminMembershipReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminRemovedItem { uint32_t removed = 0; };
ZfStruct(, (AdminRemovedItem, JSON),
  (((removed),		(Required)),	(UInt32)));
struct AdminRevokedItem { uint32_t revoked = 0; };
ZfStruct(, (AdminRevokedItem, JSON),
  (((revoked),		(Required)),	(UInt32)));
struct AdminRemovedReply { AdminRemovedItem item; };
ZfStruct(, (AdminRemovedReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminRevokedReply { AdminRevokedItem item; };
ZfStruct(, (AdminRevokedReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminActionItem { ActionID id = 0; String etag; };
ZfStruct(, (AdminActionItem, JSON),
  (((id),		(Required)),	(UInt32)),
  (((etag),		(Required)),	(String)));
struct AdminActionReply { AdminActionItem item; };
ZfStruct(, (AdminActionReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminEnrollItem {
  AppID appID = 0;
  String clientID;
  String clientSecret;
  String issuer;
  String etag;
};
ZfStruct(, (AdminEnrollItem, JSON),
  (((appID),		(JSON::String<>, Required)), (UInt64)),
  (((clientID),		(JSON::ID<"client_id">, Required)), (String)),
  (((clientSecret),	(JSON::ID<"client_secret">, JSON::Opt)), (String)),
  (((issuer),		(Required)),	(String)),
  (((etag),		(Required)),	(String)));
struct AdminEnrollReply { AdminEnrollItem item; };
ZfStruct(, (AdminEnrollReply, JSON),
  (((item),		(Required)),	(UDT)));
using AdminJSON = ZfJSON::Union<>;
struct AdminNullReply { AdminJSON item; };
ZfStruct(, (AdminNullReply, JSON),
  (((item),		(Required)),	(UDT)));
ZuDerive(AdminJSONArray,
  (ZtArray<AdminJSON, ZtArrayHeapID<"Zum.Admin.JSON">>));
struct AdminJSONVec : public AdminJSONArray {
  ZuDerive_(AdminJSONVec, AdminJSONArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(AdminJSONVec *);
};
struct AdminItemsReply {
  AdminJSONVec items;
  String nextCursor;
};
ZfStruct(, (AdminItemsReply, JSON),
  (((items),		(Required)),	(UDT)),
  (((nextCursor),	(JSON::Opt)),	(String)));
struct AdminCatalogItem { AppID appID = 0; uint64_t revision = 0; String etag; };
ZfStruct(, (AdminCatalogItem, JSON),
  (((appID),		(JSON::String<>, Required)), (UInt64)),
  (((revision),		(JSON::String<>, Required)), (UInt64)),
  (((etag),		(Required)),	(String)));
struct AdminCatalogReply { AdminCatalogItem item; };
ZfStruct(, (AdminCatalogReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminIssuerItem {
  String id;
  uint32_t schemaVersion = 0;
  AppID coreAppID = 0;
  String bootstrapPhase;
};
ZfStruct(, (AdminIssuerItem, JSON),
  (((id),		(Required)),	(String)),
  (((schemaVersion),	(Required)),	(UInt32)),
  (((coreAppID),	(JSON::String<>, Required)), (UInt64)),
  (((bootstrapPhase),	(Required)),	(String)));
ZuDerive(AdminIssuerArray, (ZtArray<AdminIssuerItem,
  ZtArrayHeapID<"Zum.Admin.Issuers">>));
struct AdminIssuerVec : public AdminIssuerArray {
  ZuDerive_(AdminIssuerVec, AdminIssuerArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(AdminIssuerVec *);
};
struct AdminIssuerReply { AdminIssuerVec items; };
ZfStruct(, (AdminIssuerReply, JSON),
  (((items),		(Required)),	(UDT)));
struct AdminOperationItem {
  unsigned id = 0;
  String name;
  String action;
  String method;
  String path;
};
ZfStruct(, (AdminOperationItem, JSON),
  (((id),		(Required)),	(UInt32)),
  (((name),		(Required)),	(String)),
  (((action),		(Required)),	(String)),
  (((method),		(Required)),	(String)),
  (((path),		(Required)),	(String)));
ZuDerive(AdminOperationArray, (ZtArray<AdminOperationItem,
  ZtArrayHeapID<"Zum.Admin.Operations">>));
struct AdminOperationVec : public AdminOperationArray {
  ZuDerive_(AdminOperationVec, AdminOperationArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(AdminOperationVec *);
};
struct AdminOperationsReply {
  AdminOperationVec items;
  String nextCursor;
};
ZfStruct(, (AdminOperationsReply, JSON),
  (((items),		(Required)),	(UDT)),
  (((nextCursor),	(JSON::Opt)),	(String)));
struct AdminRecoveryItem { UserID id = 0; String recoveryURL; };
ZfStruct(, (AdminRecoveryItem, JSON),
  (((id),		(JSON::String<>, Required)), (UInt64)),
  (((recoveryURL),	(Required)),	(String)));
struct AdminRecoveryReply { AdminRecoveryItem item; };
ZfStruct(, (AdminRecoveryReply, JSON),
  (((item),		(Required)),	(UDT)));
struct AdminIdemWire {
  String operationID;
  String status;
  StringVec resultIDs;
};
ZfStruct(, (AdminIdemWire, JSON),
  (((operationID),	(Required)),	(String)),
  (((status),		(Required)),	(String)),
  (((resultIDs),	(JSON::Opt)),	(StringVec)));
struct StateInput { String state; };
ZfStruct(, (StateInput, JSON),
  (((state),		(Required)),	(String)));

template <typename T>
static String adminJSON(T value)
{
  String json;
  ZfJSON::save(json, value);
  return json;
}

static ZfJSON::AnyNode *adminJSONField(
    const ZfJSON::AnyNode *object, ZuCSpan name)
{
  if (!object || !object->has<ZfJSON::AnyNode::Object>()) return nullptr;
  for (auto &field: object->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == name) return field.p<1>();
  return nullptr;
}

static bool adminJSONParse(
    String &source, ZuPtr<ZfJSON::AnyNode> &owner, AdminJSON &json)
{
  if (!source.mutable_()) source.length(source.length());
  auto parsed = ZfJSON::scan(source.span());
  if (parsed.p<0>() != int(source.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0])) return false;
  owner = ZuMv(roots[0]);
  json = static_cast<const ZfJSON::AnyNode *>(owner.ptr());
  return true;
}

static bool adminJSONAddString(
    ZfJSON::AnyNode *object, ZuCSpan name, ZuSpan<char> value)
{
  if (!object || !object->has<ZfJSON::AnyNode::Object>() ||
      adminJSONField(object, name)) return false;
  object->data<ZfJSON::AnyNode::Object>().push(ZfJSON::AnyNode::Field{
    name, new ZfJSON::Node<ZfJSON::AnyNode::String>{value}});
  return true;
}

template <typename Record>
static String adminPublicJSON(const Record &record)
{
  String json;
  ZfJSON::AsObject::Handler<Record, ZuFacet::JSON>::
    template save<PublicField>(json, record);
  return json;
}

struct AdminEncoded {
  String json;
  String etag;
};
ZuDerive(AdminEncodedVec, (ZtArray<AdminEncoded,
  ZtArrayHeapID<"Zum.Admin.Encoded">>));
ZuDerive(AdminJSONOwners, (ZtArray<ZuPtr<ZfJSON::AnyNode>,
  ZtArrayHeapID<"Zum.Admin.JSONOwners">>));

static bool adminItemsJSON(
    AdminEncodedVec &encoded, String nextCursor, String &body)
{
  AdminJSONOwners owners;
  AdminJSONVec items;
  for (auto &value: encoded) {
    ZuPtr<ZfJSON::AnyNode> owner;
    AdminJSON item;
    if (!adminJSONParse(value.json, owner, item) ||
	(value.etag && !adminJSONAddString(owner, "etag", value.etag.span())))
      return false;
    owners.push(ZuMv(owner));
    items.push(ZuMv(item));
  }
  ZfJSON::save(body, AdminItemsReply{ZuMv(items), ZuMv(nextCursor)});
  return true;
}

static bool adminAddItemString(AdminResult &result, ZuCSpan name, ZuCSpan value)
{
  String stored{value};
  ZuPtr<ZfJSON::AnyNode> owner;
  AdminJSON json;
  if (!adminJSONParse(result.body, owner, json) ||
      !adminJSONAddString(adminJSONField(owner, "item"), name, stored.span()))
    return false;
  String body;
  ZfJSON::save(body, json);
  result.body = ZuMv(body);
  return true;
}

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
struct ClientUpdateFields {
  using Keys = ZuStringTL<"label", "redirectURIs", "grants", "identityScopes">;
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
struct ProviderUpdateFields {
  using Keys = ZuStringTL<"issuer", "clientID", "clientSecret", "scopes",
    "roleClaim", "claimSource">;
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
  (((providerID),	(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
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
  (((appID),		(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
  (((audienceID),	(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
  (((limit),		(Required)),	(UInt32)));

struct GrantSelector {
  String id;
  UserID userID = 0;
  AppID appID = 0;
  uint32_t limit = 0;
};
ZfStruct(, (GrantSelector, JSON),
  (((id),		(JSON::Opt)),	(String)),
  (((userID),		(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
  (((appID),		(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
  (((limit),		(Required)),	(UInt32)));

struct CleanupInput {
  int64_t before = 0;
  uint32_t limit = 0;
};
ZfStruct(, (CleanupInput, JSON),
  (((before),		(JSON::Opt)),	(Int64, 0)),
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

struct CatalogInput {
  ServiceCatalog catalog;
  uint64_t revision = 0;
  String digest;
};
ZfStruct(, (CatalogInput, JSON),
  (((catalog),		(Required)),	(UDT)),
  (((revision),		(Required, JSON::String<>)),	(UInt64)),
  (((digest),		(Required)),	(String)));

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
struct QueryFields {
  using Keys = ZuStringTL<"id", "name", "source", "appID", "userID",
    "audienceID", "clientID", "providerID", "actorKind", "actorID", "value",
    "keyID", "uri", "issuer", "subject", "operation", "idempotencyKey",
    "cursor", "limit">;
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
struct ServiceAuthorizeFields {
  using Keys = ZuStringTL<"clientID", "redirectURI", "responseType", "scope",
    "resource", "state", "codeChallenge", "codeChallengeMethod", "nonce",
    "prompt", "maxAge">;
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
struct ServiceFormFields { using Keys = ZuStringTL<"form">; };
struct ClientTypes {
  using Keys = ZuStringTL<"browser", "native", "confidential">;
};
ZfStruct(, (ServiceFormInput, JSON),
  (((form),		(Mutable)),	(String)));

// Require a complete framework parse and reject its null sentinel.
static bool adminUInt(ZuCSpan text, uint64_t &value)
{
  ZuBox<uint64_t> parsed;
  if (parsed.scan(text) != int(text.length()) ||
      ZuCmp<uint64_t>::null(parsed)) return false;
  value = parsed;
  return true;
}

static bool adminActorID(ActorKind::T kind, String &id)
{
  if (!id || id.find<"/">() >= 0) return false;
  switch (kind) {
    case ActorKind::User: {
      uint64_t userID;
      if (!adminUInt(id, userID) || !userID) return false;
      id.length(0);
      id << userID;
      return true;
    }
    case ActorKind::Client: return true;
    default: return false;
  }
}

static bool adminQueryInput(String raw, QueryInput &input)
{
  if (!raw) return true;
  constexpr auto matcher = ZuMatcher<QueryFields>();
  unsigned offset = 0;
  unsigned rawLength = raw.length();
  while (offset < rawLength) {
    auto tail = ZuCSpan{raw}.offset(offset);
    auto amp = tail.find<"&">();
    unsigned length = amp >= 0 ? unsigned(amp) : tail.length();
    if (!length) return false;
    ZuCSpan pair{tail.data(), length};
    auto equal = pair.find<"=">();
    if (equal < 0 || unsigned(equal) + 1 == pair.length()) return false;
    ZuCSpan key{pair.data(), equal >= 0 ? unsigned(equal) : pair.length()};
    int field = matcher.exact(key);
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
    (!(input.seen & (1U<<QueryInput::Cursor)) ||
      !(input.seen & (exact & ~(1U<<QueryInput::UserID_))));
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
  if (op != MgmtOp::credentialQuery && (seen & userID) &&
      (seen & (1U<<QueryInput::Cursor))) return false;
  switch (op) {
    case MgmtOp::issuerQuery: return !seen;
    case MgmtOp::operationQuery: return fields(operation | idem | page);
    case MgmtOp::appQuery: return fields(id | name | page);
    case MgmtOp::userQuery: return fields(id | name | source | page);
    case MgmtOp::credentialQuery:
      return fields(id | userID | page) && !((seen & id) && (seen & userID));
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
    case MgmtOp::signKeyQuery: return fields(page);
    default: return !seen;
  }
}

template <typename T>
static bool adminBody(String &body, T &value)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  value = ZfJSON::handler<T, ZuFacet::JSON>(roots[0].ptr()).ctor();
  return true;
}

template <typename Fields, typename T>
static bool adminBodyFields(String &body, T &value, uint64_t &seen)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  seen = 0;
  constexpr auto matcher = ZuMatcher<Fields>();
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    int found = matcher.exact(field.p<0>());
    if (found < 0 || found >= 64) return false;
    unsigned i = found;
    if (seen & (uint64_t{1}<<i)) return false;
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
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
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

template <typename ID>
static bool randomID(Ztls::Random &rng, ID &id)
{
  do {
    if (!rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}))
      return false;
  } while (!id || id == ZuCmp<ID>::null());
  return true;
}

static ClientType::T clientType(ZuCSpan value)
{
  if (value == "browser") return ClientType::Browser;
  if (value == "native") return ClientType::Native;
  if (value == "confidential") return ClientType::Confidential;
  return ClientType::T(-1);
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

template <typename Edit>
class AppEdit_ : public ZumPolymorph {
public:
  enum { AppFirst = ZuIsSame<Edit, RoleEdit>{} || ZuIsSame<Edit, ScopeEdit>{} ||
    ZuIsSame<Edit, ActionEdit>{},
    Creation = ZuIsSame<Edit, ProviderAdd>{} || ZuIsSame<Edit, AudienceAdd>{} ||
      ZuIsSame<Edit, RoleAdd>{} || ZuIsSame<Edit, ScopeAdd>{} ||
      ZuIsSame<Edit, UserInvite>{} || ZuIsSame<Edit, ClientAdd>{} || ZuIsSame<Edit, KeyAdd>{} };
  using Snapshot = ZuIf<AppFirst, App, ZuDecay<decltype(ZuDeclVal<Edit>().before)>>;

  AppEdit_(DB *db, DBContext *context, Ztls::Random *rng,
      Edit change, AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_change{ZuMv(change)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    source_()->run(0, [self = ZmRef<AppEdit_>{this}]() mutable {
      ZuStructKeyT<Snapshot, 0> key{ZuStructKey<0>(self->snapshot_())};
      self->source_()->template find<0>(0, ZuMv(key),
	[self = ZuMv(self)](ZdbRowRef<Snapshot> row) mutable {
	  if (row) self->snapshot_() = row->data();
	  else self->snapshot_().version = 0;
	  if constexpr (AppFirst) self->record_();
	  else if constexpr (ZuIsSame<Edit, UserInvite>{}) self->external_();
	  else if constexpr (ZuIsSame<Edit, AudienceEdit>{} || ZuIsSame<Edit, AudienceAdd>{} ||
	      ZuIsSame<Edit, ClientAdd>{} ||
	      ZuIsSame<Edit, RoleAdd>{} || ZuIsSame<Edit, ScopeAdd>{} ||
	      ZuIsSame<Edit, RoleMapDelete>{} ||
	      ZuIsSame<Edit, RoleMapPut>{} || ZuIsSame<Edit, PolicyPut>{} ||
	      ZuIsSame<Edit, ClientAccessPut>{} || ZuIsSame<Edit, AdminAccessPut>{})
	    self->owner_();
	  else self->submit_();
	});
    });
  }

private:
  void external_()
  {
    auto users = m_context->users;
    users->find<2>(0, ZuFwdTuple(UserSource::External, m_change.values.name),
      [self = ZmRef<AppEdit_>{this}](ZdbRowRef<User> row) mutable {
        if (row) self->m_change.external = row->data();
        self->submit_();
      });
  }

  auto source_() const
  {
    if constexpr (ZuIsSame<Edit, UserEdit>{} || ZuIsSame<Edit, UserInvite>{})
      return m_context->users;
    else if constexpr (ZuIsSame<Edit, CredEdit>{}) return m_context->creds;
    else if constexpr (ZuIsSame<Edit, AudienceEdit>{} || ZuIsSame<Edit, AudienceAdd>{})
      return m_context->audiences;
    else if constexpr (ZuIsSame<Edit, ProviderEdit>{} || ZuIsSame<Edit, ProviderAdd>{})
      return m_context->providers;
    else if constexpr (ZuIsSame<Edit, ClientEdit>{} || ZuIsSame<Edit, ClientAdd>{})
      return m_context->clients;
    else if constexpr (ZuIsSame<Edit, KeyRetire>{} || ZuIsSame<Edit, KeyAdd>{})
      return m_context->signKeys;
    else if constexpr (ZuIsSame<Edit, ClientAccessState>{} || ZuIsSame<Edit, ClientAccessPut>{})
      return m_context->clientAccess;
    else if constexpr (ZuIsSame<Edit, AdminAccessState>{} || ZuIsSame<Edit, AdminAccessPut>{})
      return m_context->adminAccess;
    else if constexpr (ZuIsSame<Edit, RoleMapDelete>{} || ZuIsSame<Edit, RoleMapPut>{})
      return m_context->roleMaps;
    else if constexpr (ZuIsSame<Edit, PolicyPut>{}) return m_context->authPolicies;
    else if constexpr (ZuIsSame<Edit, RoleAdd>{}) return m_context->roles;
    else if constexpr (ZuIsSame<Edit, ScopeAdd>{}) return m_context->scopes;
    else return m_context->apps;
  }

  Snapshot &snapshot_()
  {
    if constexpr (AppFirst) return m_change.app;
    else return m_change.before;
  }

  auto table_() const
  {
    if constexpr (ZuIsSame<Edit, RoleEdit>{}) return m_context->roles;
    else if constexpr (ZuIsSame<Edit, ScopeEdit>{}) return m_context->scopes;
    else return m_context->actions;
  }

  void owner_()
  {
    m_change.app.id = m_change.before.appID;
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<AppEdit_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_change.app.id),
	[self = ZuMv(self)](ZdbRowRef<App> row) mutable {
	  if (row) self->m_change.app = row->data();
	  else self->m_change.app.version = 0;
	  self->submit_();
	});
    });
  }

  void record_()
  {
    table_()->run(0, [self = ZmRef<AppEdit_>{this}]() mutable {
      self->table_()->template find<0>(0,
	ZuFwdTuple(self->m_change.app.id, self->m_change.before.id),
	[self = ZuMv(self)](ZdbRowRef<ZuDecay<decltype(ZuDeclVal<Edit>().before)>> row) mutable {
	  if (row) self->m_change.before = row->data();
	  else self->m_change.before.version = 0;
	  self->submit_();
	});
    });
  }

  void submit_()
  {
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) {
      finish_(503); return;
    }
    m_resultVersion = m_change.before.version + !m_change.unchanged();
    m_change.updated = Zm::now().sec();
    m_saga = new MSaga{};
    m_saga->init(ZuMv(m_change));
    if (!sagaSubmit(m_db, id, m_saga,
	[self = ZmRef<AppEdit_>{this}](bool ok) mutable {
	  if (!ok) self->finish_(503);
	}, [self = ZmRef<AppEdit_>{this}](bool ok) mutable {
	  if (!self->m_complete) return;
	  unsigned status = ok ? 200 : self->m_saga->u.cdispatch(
	    [](auto, const auto &change) -> unsigned {
	      if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Edit>{})
		return change.error ? change.error : 503;
	      else return 503;
	    });
	  self->finish_(status);
	})) finish_(503);
  }

  void finish_(unsigned status)
  {
    if (!m_complete) return;
    auto complete = ZuMv(m_complete);
    if constexpr (ZuIsSame<Edit, RoleMapPut>{} || ZuIsSame<Edit, PolicyPut>{} ||
	ZuIsSame<Edit, ClientAccessPut>{} || ZuIsSame<Edit, AdminAccessPut>{} ||
	Creation || ZuIsSame<Edit, ClientEdit>{}) {
      if (status == 200) {
	auto result = m_saga->u.cdispatch([](auto, const auto &change) -> AdminResult {
	  if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Edit>{}) {
	    StringVec ids;
	    if constexpr (Creation)
	      ids.push(String{} << change.values.id);
	    if constexpr (ZuIsSame<Edit, ClientEdit>{})
	      if (change.fields == ClientEdit::Secret) ids.push(change.before.id);
	    return adminItemResult(change.result(), change.before.version ? 200 : 201, ZuMv(ids));
	  } else return adminErrorResult(503, "unavailable", "operation unavailable");
	});
	m_saga = nullptr;
	complete(ZuMv(result));
	return;
      }
    }
    m_saga = nullptr;
    if (status != 200) {
      complete(adminErrorResult(status,
	status == 400 ? "invalid_request" : status == 404 ? "not_found" :
	status == 412 ? "precondition_failed" : status == 428 ? "precondition_required" :
	status == 409 ? "conflict" : "unavailable",
	status == 400 ? "invalid reference" :
	status == 412 ? "ETag mismatch" : "catalog update failed"));
      return;
    }
    if constexpr (ZuIsSame<Edit, RoleMapDelete>{}) {
      complete(AdminResult{adminJSON(AdminNullReply{}), 200});
      return;
    }
    String json = adminJSON(AdminETagReply{{rowETag(m_resultVersion)}});
    complete(AdminResult{ZuMv(json), 200});
  }

  DB *m_db;
  DBContext *m_context;
  Ztls::Random *m_rng;
  Edit m_change;
  ZmRef<MSaga> m_saga;
  AdminDoneFn m_complete;
  uint64_t m_resultVersion = 0;
};

class MembershipAdd_ : public ZumPolymorph {
public:
  MembershipAdd_(DB *db, Ztls::Random *rng, MembershipAdd add,
      AdminDoneFn complete) : m_db{db}, m_rng{rng},
    m_appID{add.appID}, m_userID{add.userID}, m_complete{ZuMv(complete)}
  {
    m_saga = new MSaga{};
    m_saga->init(ZuMv(add));
  }

  void start()
  {
    ZdbSagaID id;
    if (!randomID(*m_rng, id) || !sagaSubmit(m_db, id, m_saga,
	[self = ZmRef<MembershipAdd_>{this}](bool ok) mutable {
	  if (!ok) self->finish_(503);
	}, [self = ZmRef<MembershipAdd_>{this}](bool ok) mutable {
	  if (!self->m_complete) return;
	  unsigned status = ok ? 201 : self->m_saga->u.cdispatch(
	    [](auto, const auto &add) -> unsigned {
	      if constexpr (ZuIsSame<ZuDecay<decltype(add)>, MembershipAdd>{})
		return add.error;
	      else return 503;
	    });
	  self->finish_(status);
	})) finish_(503);
  }

private:
  void finish_(unsigned status)
  {
    if (!m_complete) return;
    auto complete = ZuMv(m_complete);
    m_saga = nullptr;
    if (status != 201) {
      complete(adminErrorResult(status,
	status == 404 ? "not_found" : status == 409 ? "conflict" : "unavailable",
	status == 404 ? "application or local user not found" :
	status == 409 ? "membership already exists" : "membership creation failed"));
      return;
    }
    String json = adminJSON(AdminMembershipReply{{
      m_appID, m_userID, rowETag(1)}});
    StringVec ids;
    ids.push(String{} << m_appID << ':' << m_userID);
    complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
  }

  DB		*m_db = nullptr;
  Ztls::Random	*m_rng = nullptr;
  AppID		m_appID = 0;
  UserID	m_userID = 0;
  ZmRef<MSaga>	m_saga;
  AdminDoneFn	m_complete;
};

class BulkRevoke_ : public ZumPolymorph {
public:
  enum { Sessions, Consents, Grants, Cleanup };

  BulkRevoke_(DB *db, DBContext *context, Ztls::Random *rng, int kind, UserID userID,
      String clientID, AppID appID, AudienceID audienceID,
      uint32_t limit, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng},
    m_kind{kind}, m_userID{userID}, m_clientID{ZuMv(clientID)},
    m_appID{appID}, m_audienceID{audienceID}, m_limit{limit},
    m_complete{ZuMv(complete)} { m_change.request = ZuMv(request); }

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
      case Cleanup: cleanupSelect_(); return;
    }
  }

  void one(Bytes id)
  {
    auto table = m_context->grants;
    table->run(0, [self = ZmRef<BulkRevoke_>{this}, table, id = ZuMv(id)]() mutable {
      table->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self)](
	  ZdbRowRef<Grant> row) mutable {
	if (!row) {
	  self->finish_(adminErrorResult(404, "not_found", "grant not found"));
	  return;
	}
	if (row->data().owner || row->data().state == State::Consumed) {
	  self->finish_(adminErrorResult(409, "conflict", "grant cannot be revoked"));
	  return;
	}
	self->m_change.grants.push(SagaImage::save(row->data()));
	self->submit_();
      });
    });
  }

private:

  void finish_(AdminResult result)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    m_saga = nullptr;
    complete(ZuMv(result));
  }

  void success_()
  {
    String json = m_kind == Cleanup ?
      adminJSON(AdminRemovedReply{{m_removed}}) :
      adminJSON(AdminRevokedReply{{m_removed}});
    finish_(AdminResult{ZuMv(json), 200});
  }

  template <unsigned KeyID, typename Table, typename Match>
  void scan_(Table *table, BytesVec *images,
      typename Zdb_::SplitKey<typename Table::T, KeyID>::GroupKey group, Match match,
      typename Table::template Key<KeyID> key = {}, bool next = false)
  {
    table->run(0, [self = ZmRef<BulkRevoke_>{this}, table, images,
	group = ZuMv(group), match = ZuMv(match), key = ZuMv(key), next]() mutable {
      using Tuple = typename Table::Tuple;
      auto receive = [self, table, images, group, match = ZuMv(match),
	  last = key, raw = unsigned{0}, stop = false](
	    ZuUnion<void, Tuple> result, unsigned count) mutable {
	if (result.template is<Tuple>()) {
	  raw = count;
	  if (stop || images->length() >= self->m_limit) return;
	  auto tuple = ZuMv(result).template p<Tuple>();
	  last = ZuStructKey<KeyID>(tuple);
	  int matched = match(tuple);
	  if (matched < 0) { stop = true; return; }
	  if (!matched) return;
	  using Record = typename Table::T;
	  constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">, ZuFieldIDs<Record>>{};
	  constexpr unsigned state = ZuTypeIndex<ZuStringT<"state">, ZuFieldIDs<Record>>{};
	  if (tuple.template p<owner>()) return;
	  if (self->m_kind != Cleanup &&
	      (tuple.template p<state>() == State::Revoked ||
	       tuple.template p<state>() == State::Consumed)) return;
	  images->push(SagaImage::save(tuple));
	  return;
	}
	if (self->m_kind != Cleanup && !stop &&
	    images->length() < self->m_limit && raw == self->m_limit) {
	  self->template scan_<KeyID>(table, images, ZuMv(group),
	    ZuMv(match), ZuMv(last), true);
	  return;
	}
	self->submit_();
      };
      if (next)
	table->template nextRows<KeyID>(ZuMv(key), false, self->m_limit, ZuMv(receive));
      else
	table->template selectRows<KeyID>(ZuMv(group), self->m_limit, ZuMv(receive));
    });
  }

  void sessionSelect_()
  {
    if (!m_userID) {
      finish_(adminErrorResult(400, "invalid_request", "session revoke requires userID"));
      return;
    }
    scan_<1>(m_context->sessions, &m_change.sessions, ZuFwdTuple(m_userID),
      [userID = m_userID](const auto &tuple) -> int {
	return tuple.template p<1>() == userID ? 1 : -1;
      });
  }

  void consentSelect_()
  {
    if (!m_userID) {
      finish_(adminErrorResult(400, "invalid_request", "consent revoke requires userID"));
      return;
    }
    scan_<1>(m_context->consents, &m_change.consents, ZuFwdTuple(m_userID),
      [this](const auto &tuple) -> int {
	if (tuple.template p<0>() != m_userID) return -1;
	return (!m_clientID || tuple.template p<1>() == m_clientID) &&
	  (!m_appID || tuple.template p<2>() == m_appID) &&
	  (!m_audienceID || tuple.template p<3>() == m_audienceID);
      });
  }

  void grantSelect_()
  {
    if (!m_userID && !m_appID) {
      finish_(adminErrorResult(400, "invalid_request", "grant revoke requires userID or appID"));
      return;
    }
    if (m_userID) {
      scan_<2>(m_context->grants, &m_change.grants, ZuFwdTuple(m_userID),
	[this](const auto &tuple) -> int {
	  if (tuple.template p<2>() != m_userID) return -1;
	  return !m_appID || tuple.template p<3>() == m_appID;
	});
    } else {
      scan_<3>(m_context->grants, &m_change.grants, ZuFwdTuple(m_appID),
	[appID = m_appID](const auto &tuple) -> int {
	  return tuple.template p<3>() == appID ? 1 : -1;
	});
    }
  }

  void cleanupSelect_()
  {
    m_change.updated = Zm::now().sec();
    scan_<1>(m_context->grants, &m_change.grants, {},
      [updated = m_change.updated](const auto &tuple) -> int {
	return tuple.template p<21>() <= updated ? 1 : -1;
      });
  }

  void submit_()
  {
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) {
      failed_(503); return;
    }
    m_saga = new MSaga{};
    if (m_kind == Cleanup) {
      m_removed = m_change.grants.length();
      m_saga->init(GrantCleanup{.grants = ZuMv(m_change.grants),
	.updated = m_change.updated, .request = ZuMv(m_change.request)});
    } else {
      if (!m_removed) m_removed = m_change.count();
      m_change.updated = Zm::now().sec();
      m_saga->init(ZuMv(m_change));
    }
    if (!sagaSubmit(m_db, id, m_saga,
	[self = ZmRef<BulkRevoke_>{this}](bool ok) mutable {
	  if (!ok) self->failed_(503);
	}, [self = ZmRef<BulkRevoke_>{this}](bool ok) mutable {
	  if (self->m_done) return;
	  if (ok) { self->success_(); return; }
	  unsigned status = self->m_saga->u.cdispatch([](auto, const auto &change) -> unsigned {
	    if constexpr (ZuIsSame<ZuDecay<decltype(change)>, Revoke>{} ||
		ZuIsSame<ZuDecay<decltype(change)>, GrantCleanup>{})
	      return change.error ? change.error : 503;
	    else return 503;
	  });
	  self->failed_(status);
	})) failed_(503);
  }

  void failed_(unsigned status)
  {
    finish_(adminErrorResult(status, status == 409 ? "conflict" :
      status == 400 ? "invalid_request" : "unavailable", "revocation failed"));
  }

  DB		*m_db;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng;
  int		m_kind = Sessions;
  UserID	m_userID = 0;
  String	m_clientID;
  AppID		m_appID = 0;
  AudienceID	m_audienceID = 0;
  uint32_t	m_limit = 0;
  AdminDoneFn	m_complete;
  Revoke	m_change;
  ZmRef<MSaga>	m_saga;
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

class MembershipChange_ : public ZumPolymorph {
public:
  MembershipChange_(DB *db, DBContext *context, Ztls::Random *rng,
      AppID appID, UserID userID, IDVec roles, State::T state,
      String ifMatch, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_appID{appID},
    m_userID{userID}, m_roles{ZuMv(roles)}, m_state{state},
    m_complete{ZuMv(complete)}
  {
    m_change.request = ZuMv(request);
    m_change.assignRoles = state == State::N;
    m_change.ifMatch = ZuMv(ifMatch);
  }

  void start()
  {
    app_();
  }

private:
  void finish_(unsigned status, ZuCSpan error = {}, ZuCSpan message = {})
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    m_saga = nullptr;
    if (status != 200) {
      complete(adminErrorResult(status, error, message));
      return;
    }
    String json = adminJSON(AdminETagReply{{rowETag(m_resultVersion)}});
    complete(AdminResult{ZuMv(json), 200});
  }

  void app_()
  {
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<MembershipChange_>{this}, apps]() mutable {
      apps->find<0>(0, ZuFwdTuple(self->m_appID), [self = ZuMv(self)](
	  ZdbRowRef<App> row) mutable {
	self->m_change.appID = self->m_appID;
	self->m_change.userID = self->m_userID;
	self->m_change.appVersion = row ? row->data().version : 0;
	self->m_change.appAuthVersion = row ? row->data().authVersion : 0;
	self->m_change.appUpdated = row ? row->data().updated : 0;
	self->member_();
      });
    });
  }

  void member_()
  {
    auto members = m_context->memberships;
    members->run(0, [self = ZmRef<MembershipChange_>{this}, members]() mutable {
      members->find<0>(0, ZuFwdTuple(self->m_appID, self->m_userID), [
	  self = ZuMv(self)](ZdbRowRef<Membership> row) mutable {
	auto &change = self->m_change;
	change.oldRoles = row ? row->data().roleIDs : IDVec{};
	change.newRoles = self->m_state == State::N ?
	  ZuMv(self->m_roles) : IDVec{change.oldRoles};
	change.oldState = row ? row->data().state : State::Pending;
	change.newState = self->m_state == State::N ? change.oldState : self->m_state;
	change.version = row ? row->data().version : 0;
	change.authVersion = row ? row->data().authVersion : 0;
	change.oldUpdated = row ? row->data().updated : 0;
	change.updated = Zm::now().sec();
	self->m_resultVersion = change.version + !change.unchanged();
	self->submit_();
      });
    });
  }

  void submit_()
  {
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) {
      finish_(503, "unavailable", "saga ID generation failed");
      return;
    }
    m_saga = new MSaga{};
    m_saga->init(ZuMv(m_change));
    if (!sagaSubmit(m_db, id, m_saga,
	SagaFn{ZmRef<MembershipChange_>{this},
	  ZmFnPtr<&MembershipChange_::submitted_>{}},
	SagaFn{ZmRef<MembershipChange_>{this},
	  ZmFnPtr<&MembershipChange_::completed_>{}})) submitted_(false);
  }

  void submitted_(bool ok)
  {
    if (!ok) finish_(503, "unavailable", "membership change unavailable");
  }

  void completed_(bool ok)
  {
    if (m_done) return;
    if (ok) {
      finish_(200);
      return;
    }
    unsigned error = m_saga->u.cdispatch([](auto, const auto &change) -> unsigned {
      if constexpr (ZuIsSame<ZuDecay<decltype(change)>, MembershipChange>{})
	return change.error;
      else return 0;
    });
    switch (error) {
      case 400:
	finish_(400, "invalid_request", "invalid role reference");
	return;
      case 409:
	finish_(409, "conflict", "application or local membership cannot be changed");
	return;
      case 404:
	finish_(404, "not_found", "membership not found");
	return;
      case 412:
	finish_(412, "precondition_failed", "ETag mismatch");
	return;
    }
    finish_(503, "unavailable", "membership change failed");
  }

  DB		*m_db;
  DBContext	*m_context;
  Ztls::Random	*m_rng;
  AppID		m_appID;
  UserID	m_userID;
  IDVec		m_roles;
  State::T	m_state;
  AdminDoneFn	m_complete;
  MembershipChange m_change;
  ZmRef<MSaga> m_saga;
  uint64_t	m_resultVersion = 0;
  bool		m_done = false;
};

class ActionAdd_ : public ZumPolymorph {
public:
  ActionAdd_(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
      NameInput input, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context},
    m_rng{rng}, m_appID{appID}, m_input{ZuMv(input)},
    m_request{ZuMv(request)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_appID || !m_input.name) {
      finish_(400, "invalid action creation");
      return;
    }
    if (!randomID(*m_rng, m_sagaID)) {
      finish_(503, "saga ID generation failed");
      return;
    }
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
    String json = adminJSON(AdminActionReply{{m_actionID, rowETag(1)}});
    StringVec ids;
    String id;
    id << m_actionID;
    ids.push(ZuMv(id));
    complete(AdminResult{ZuMv(json), 201, ZuMv(ids)});
  }

  void app_(ZdbRowRef<App> row)
  {
    // Snapshot the immutable recovery inputs; the saga decides whether the
    // application may allocate this action and checks the snapshot on-shard.
    m_actionID = row ? row->data().nextActionID : UINT32_MAX;
    int64_t now = Zm::now().sec();
    AppActionAdd add{.appID = m_appID, .actionID = m_actionID,
      .name = ZuMv(m_input.name), .label = ZuMv(m_input.label),
      .created = now, .oldAppVersion = row ? row->data().version : 0,
      .oldAuthVersion = row ? row->data().authVersion : 0,
      .oldUpdated = row ? row->data().updated : 0,
      .request = ZuMv(m_request)};
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
  IdemRequest	m_request;
  AdminDoneFn	m_complete;
  ActionID	m_actionID = 0;
  ZdbSagaID	m_sagaID = 0;
  bool		m_done = false;
};

class AppEnroll_ : public ZumPolymorph {
public:
  AppEnroll_(DB *db, DBContext *context, Ztls::Random *rng, Issuer issuer,
      AppInput input, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context},
    m_rng{rng}, m_issuer{ZuMv(issuer)}, m_input{ZuMv(input)},
    m_request{ZuMv(request)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    ZuBArray<ClientCredentialEntropySize> random(
      ClientCredentialEntropySize, false);
    bool nativeService = m_input.integration == "nativeService";
    bool oidc = m_input.integration == "oidc";
    if (!m_input.clientType)
      m_input.clientType = nativeService ? "confidential" : "browser";
    ClientType::T clientType = ClientType::Browser;
    constexpr auto matcher = ZuMatcher<ClientTypes>();
    switch (matcher.exact(m_input.clientType)) {
      case 0: clientType = ClientType::Browser; break;
      case 1: clientType = ClientType::Native; break;
      case 2: clientType = ClientType::Confidential; break;
      default: oidc = nativeService = false; break;
    }
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
    m_clientID << encode({random.data(), ClientIDEntropySize});
    if (confidential) {
      m_secret = encode({random.data() + ClientIDEntropySize,
	ClientSecretEntropySize});
      m_secretDigest.length(Ztls::SecretHash::Size, false);
      if (!Ztls::secretHash(*m_rng, ZuBSpan{m_secret}, m_secretDigest)) {
	finish_(503, "secret generation failed");
	return;
      }
    }
    int64_t now = Zm::now().sec();
    if (!m_input.label) m_input.label = m_input.name;
    ZdbSagaID sagaID;
    if (!m_db || !randomID(*m_rng, sagaID)) {
      finish_(503, "saga ID generation failed");
      return;
    }
    AppEnrollment enrollment{.coreAppID = m_issuer.coreAppID,
      .appID = m_appID, .appName = ZuMv(m_input.name),
      .appLabel = ZuMv(m_input.label), .audienceID = m_audienceID,
      .audienceURI = ZuMv(m_input.audienceURI), .clientID = m_clientID,
      .secretDigest = ZuMv(m_secretDigest),
      .redirects = ZuMv(m_input.redirectURIs), .clientType = clientType,
      .nativeService = nativeService, .created = now,
      .catalogPublishOp = MgmtOp::catalogPublish,
      .operationQueryOp = MgmtOp::operationQuery,
      .request = ZuMv(m_request)};
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
      String body = adminJSON(AdminErrorWire{
        status == 409 ? "conflict" :
          status == 400 ? "invalid_request" : "unavailable",
        message, {}});
      complete(AdminResult{ZuMv(body), status});
      return;
    }
    String body = adminJSON(AdminEnrollReply{{m_appID, m_clientID,
      m_secret, m_issuer.id, rowETag(2)}});
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
  IdemRequest	m_request;
  AdminDoneFn	m_complete;
  AppID		m_appID = 0;
  AudienceID	m_audienceID = 0;
  String	m_clientID;
  String	m_secret;
  Bytes		m_secretDigest;
  bool		m_done = false;
};

template <typename Table, typename Present>
class CatalogRetire_ : public ZumObject {
  using T = typename Table::T;
  using Tuple = typename Table::Tuple;
  using Key = typename Table::template Key<0>;
public:
  CatalogRetire_(Table *table, AppID appID, uint64_t revision, int64_t updated,
      CatalogRows *rows, Present present, SagaFn complete) :
    m_table{table}, m_appID{appID}, m_revision{revision}, m_updated{updated},
    m_rows{rows}, m_present{ZuMv(present)}, m_complete{ZuMv(complete)},
    m_key{ZuFwdTuple(appID, 0)} { }
  void start(bool first = true)
  {
    m_count = 0;
    m_table->run(0, [self = ZmRef<CatalogRetire_>{this}, first]() mutable {
      self->m_table->template nextRows<0>(self->m_key, first, PageSize,
	[self = ZuMv(self)](ZuUnion<void, Tuple> result, unsigned count) mutable {
	  self->receive_(ZuMv(result), count);
	});
    });
  }
private:
  enum { PageSize = 1000 }; // same bound as management query pages
  void receive_(ZuUnion<void, Tuple> result, unsigned count)
  {
    if (result.template is<Tuple>()) {
      auto tuple = ZuMv(result).template p<Tuple>();
      m_count = count;
      m_key = ZuStructKey<0>(tuple);
      ZuTupleCall(ZuMv(tuple), [this](auto &&...args) {
	T before{ZuFwd<decltype(args)>(args)...};
	if (before.appID != m_appID) { m_end = true; return; }
	if (before.origin != Origin::Standard || m_present(before)) return;
	if (before.owner) { m_ok = false; return; }
	if (before.state != State::Active) return;
	if constexpr (!ZuIsSame<T, Scope>{}) if (before.tombstone) return;
	if (before.version == UINT64_MAX) { m_ok = false; return; }
	T after = before;
	after.state = State::Disabled;
	after.catalogRevision = m_revision;
	++after.version;
	after.updated = m_updated;
	m_rows->change(before, after);
      });
      return;
    }
    if (m_ok && !m_end && m_count == PageSize) { start(false); return; }
    auto complete = ZuMv(m_complete);
    complete(m_ok);
  }
  Table *m_table;
  AppID m_appID;
  uint64_t m_revision;
  int64_t m_updated;
  CatalogRows *m_rows;
  Present m_present;
  SagaFn m_complete;
  Key m_key;
  unsigned m_count = 0;
  bool m_ok = true;
  bool m_end = false;
};

class CatalogPublish_ : public ZumPolymorph {
public:
  CatalogPublish_(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
      CatalogInput input, String ifMatch, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_appID{appID}, m_input{ZuMv(input)},
    m_ifMatch{ZuMv(ifMatch)}, m_complete{ZuMv(complete)}
    { m_change.request = ZuMv(request); }

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
	self->m_replay = self->m_input.revision == self->m_app.catalogRevision &&
	  Ztls::ctEqual(self->m_digest, self->m_app.catalogDigest);
	String expected{"\"catalog-"};
	expected << self->m_app.catalogRevision << '"';
	if (!self->m_replay && self->m_input.revision <= self->m_app.catalogRevision) {
	  self->fail_(409, "conflict", "catalog revision conflict");
	  return;
	}
	if (!self->m_replay && !self->m_ifMatch) {
	  self->fail_(428, "precondition_required",
	    "catalog If-Match is required");
	  return;
	}
	if (!self->m_replay && self->m_ifMatch != expected) {
	  self->fail_(412, "precondition_failed", "catalog ETag mismatch");
	  return;
	}
	if (!self->digest_()) {
	  self->fail_(400, "invalid_request", "catalog content digest mismatch");
	  return;
	}
	self->m_change.before = self->m_app;
	self->m_change.after = self->m_app;
	self->m_change.after.updated = Zm::now().sec();
	self->m_nextActionID = self->m_app.nextActionID;
	self->m_actionIDs.length(self->m_input.catalog.actions.length());
	self->m_roleIDs.length(self->m_input.catalog.roles.length());
	self->action_();
      });
    });
  }

private:
  bool digest_() const
  {
    String json;
    ZfJSON::save(json, m_input.catalog);
    ZuBArray<Ztls::MD<>::Size> digest;
    digest.length(Ztls::MD<>::Size);
    Ztls::MD<> md;
    md.update(ZuBSpan{json});
    md.finish(digest);
    return Ztls::ctEqual(digest, m_digest);
  }

  bool validate_() const
  {
    unsigned actionCount = m_input.catalog.actions.length();
    for (unsigned i = 0; i < actionCount; ++i) {
      const auto &action = m_input.catalog.actions[i];
      if (!action.name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (action.name ==
	    m_input.catalog.actions[j].name) return false;
    }
    unsigned roleCount = m_input.catalog.roles.length();
    for (unsigned i = 0; i < roleCount; ++i) {
      const auto &role = m_input.catalog.roles[i];
      if (!role.name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (role.name == m_input.catalog.roles[j].name) return false;
      unsigned roleActionCount = role.actions.length();
      for (unsigned a = 0; a < roleActionCount; ++a) {
	auto &name = role.actions[a];
	bool found = false;
	for (unsigned j = 0; j < actionCount; ++j)
	  if (name == m_input.catalog.actions[j].name) {
	    found = true; break;
	  }
	if (!found) return false;
	for (unsigned j = 0; j < a; ++j)
	  if (name == role.actions[j]) return false;
      }
    }
    unsigned scopeCount = m_input.catalog.scopes.length();
    for (unsigned i = 0; i < scopeCount; ++i) {
      const auto &scope = m_input.catalog.scopes[i];
      if (!scope.audienceID || !scope.name) return false;
      for (unsigned j = 0; j < i; ++j)
	if (scope.audienceID == m_input.catalog.scopes[j].audienceID &&
	    scope.name == m_input.catalog.scopes[j].name) return false;
      unsigned scopeRoleCount = scope.roles.length();
      for (unsigned r = 0; r < scopeRoleCount; ++r) {
	auto &name = scope.roles[r];
	bool found = false;
	for (unsigned j = 0; j < roleCount; ++j)
	  if (name == m_input.catalog.roles[j].name) {
	    found = true; break;
	  }
	if (!found) return false;
	for (unsigned j = 0; j < r; ++j)
	  if (name == scope.roles[j]) return false;
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
    String etag{"catalog-"};
    etag << m_input.revision;
    String json = adminJSON(AdminCatalogReply{{
      m_appID, m_input.revision, ZuMv(etag)}});
    StringVec ids;
    String id;
    id << m_appID;
    ids.push(ZuMv(id));
    auto complete = ZuMv(m_complete);
    complete(AdminResult{ZuMv(json), 200, ZuMv(ids)});
  }

  int actionIndex_(ZuCSpan name) const
  {
    unsigned n = m_input.catalog.actions.length();
    for (unsigned i = 0; i < n; ++i)
      if (m_input.catalog.actions[i].name == name) return int(i);
    return -1;
  }

  int roleIndex_(ZuCSpan name) const
  {
    unsigned n = m_input.catalog.roles.length();
    for (unsigned i = 0; i < n; ++i)
      if (m_input.catalog.roles[i].name == name) return int(i);
    return -1;
  }

  void action_()
  {
    if (m_offset >= m_input.catalog.actions.length()) {
      m_offset = 0;
      role_();
      return;
    }
    unsigned index = m_offset++;
    auto table = m_context->actions;
    String name = m_input.catalog.actions[index].name;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name)]() mutable {
      table->find<1>(0, ZuFwdTuple(self->m_appID, ZuMv(name)), [
	  self = ZuMv(self), index](ZdbRowRef<Action> row) mutable {
	if (row) {
	  auto action = row->data();
	  if (action.origin != Origin::Standard || action.owner) {
	    self->fail_(409, "conflict", "catalog action name is reserved");
	    return;
	  }
	  self->m_actionIDs[index] = action.id;
	  if (self->m_replay && !action.tombstone) { self->action_(); return; }
	  action.label = self->m_input.catalog.actions[index].label;
	  if (!action.label) action.label = action.name;
	  action.catalogRevision = self->m_input.revision;
	  if (action.tombstone) {
	    action.tombstone = false;
	    action.state = State::Active;
	  }
	  ++action.version;
	  action.updated = self->m_change.after.updated;
	  self->m_change.actions.change(row->data(), action);
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
	  .name = self->m_input.catalog.actions[index].name,
	  .label = self->m_input.catalog.actions[index].label,
	  .state = State::Active, .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	if (!action.label) action.label = action.name;
	self->m_actionIDs[index] = action.id;
	self->m_change.actions.add(action);
	self->action_();
      });
    });
  }

  void role_()
  {
    if (m_offset >= m_input.catalog.roles.length()) {
      m_offset = 0;
      scope_();
      return;
    }
    unsigned index = m_offset++;
    ZtBitmap actions;
    actions.length(m_nextActionID);
    for (auto &name: m_input.catalog.roles[index].actions) {
      int action = actionIndex_(name);
      if (action < 0) {
	fail_(400, "invalid_request", "role action is unavailable");
	return;
      }
      actions.set(m_actionIDs[action]);
    }
    auto table = m_context->roles;
    String name = m_input.catalog.roles[index].name;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name), actions = ZuMv(actions)]() mutable {
      table->find<1>(0, ZuFwdTuple(self->m_appID, ZuMv(name)), [
	  self = ZuMv(self), index, actions = ZuMv(actions)](
	    ZdbRowRef<Role> row) mutable {
	if (row) {
	  auto role = row->data();
	  if (role.origin != Origin::Standard || role.owner) {
	    self->fail_(409, "conflict", "catalog role name is reserved");
	    return;
	  }
	  self->m_roleIDs[index] = role.id;
	  if (self->m_replay && !role.tombstone) { self->role_(); return; }
	  role.label = self->m_input.catalog.roles[index].label;
	  if (!role.label) role.label = role.name;
	  role.actions = ZuMv(actions);
	  role.catalogRevision = self->m_input.revision;
	  if (role.tombstone) {
	    role.tombstone = false;
	    role.state = State::Active;
	  }
	  ++role.version;
	  role.updated = self->m_change.after.updated;
	  self->m_change.roles.change(row->data(), role);
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
	  .name = self->m_input.catalog.roles[index].name,
	  .label = self->m_input.catalog.roles[index].label,
	  .actions = ZuMv(actions), .state = State::Active,
	  .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	if (!role.label) role.label = role.name;
	self->m_roleIDs[index] = id;
	self->m_change.roles.add(role);
	self->role_();
      });
    });
  }

  void scope_()
  {
    if (m_offset >= m_input.catalog.scopes.length()) { retire_(); return; }
    unsigned index = m_offset++;
    const auto &input = m_input.catalog.scopes[index];
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
	ZuFwdTuple(self->m_input.catalog.scopes[index].audienceID), [self = ZuMv(self),
	  index, roleIDs = ZuMv(roleIDs)](ZdbRowRef<Audience> audience) mutable {
	if (!audience || audience->data().appID != self->m_appID ||
	    audience->data().state != State::Active || audience->data().owner) {
	  self->fail_(400, "invalid_request", "invalid catalog audience");
	  return;
	}
	self->scopeWrite_(index, ZuMv(roleIDs));
      });
    });
  }

  void scopeWrite_(unsigned index, IDVec roleIDs)
  {
    auto table = m_context->scopes;
    String name = m_input.catalog.scopes[index].name;
    AudienceID audienceID = m_input.catalog.scopes[index].audienceID;
    table->run(0, [self = ZmRef<CatalogPublish_>{this}, table, index,
	name = ZuMv(name), audienceID,
	roleIDs = ZuMv(roleIDs)]() mutable {
      table->find<1>(0, ZuFwdTuple(self->m_appID, audienceID,
	ZuMv(name)), [self = ZuMv(self), index,
	  audienceID,
	  roleIDs = ZuMv(roleIDs)](ZdbRowRef<Scope> row) mutable {
	if (row) {
	  auto scope = row->data();
	  if (scope.origin != Origin::Standard || scope.owner) {
	    self->fail_(409, "conflict", "catalog scope name is reserved");
	    return;
	  }
	  if (self->m_replay) { self->scope_(); return; }
	  scope.roleIDs = catalogScopeRoles(scope, roleIDs);
	  scope.catalogRoleIDs = ZuMv(roleIDs);
	  scope.catalogRevision = self->m_input.revision;
	  ++scope.version;
	  scope.updated = self->m_change.after.updated;
	  self->m_change.scopes.change(row->data(), scope);
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
	  .audienceID = audienceID,
	  .name = self->m_input.catalog.scopes[index].name,
	  .roleIDs = ZuMv(roleIDs), .state = State::Active,
	  .origin = Origin::Standard,
	  .catalogRevision = self->m_input.revision, .version = 1,
	  .created = now, .updated = now};
	scope.catalogRoleIDs = scope.roleIDs;
	self->m_change.scopes.add(scope);
	self->scope_();
      });
    });
  }

  template <typename Table, typename Present>
  void retire_(Table *table, CatalogRows &rows, Present present)
  {
    ZmRef<CatalogRetire_<Table, Present>> retire =
      new CatalogRetire_<Table, Present>{table, m_appID, m_input.revision,
	m_change.after.updated, &rows, ZuMv(present),
	[self = ZmRef<CatalogPublish_>{this}](bool ok) mutable {
	  if (!ok) { self->fail_(409, "conflict", "catalog retirement conflict"); return; }
	  ++self->m_retire;
	  self->retire_();
	}};
    retire->start();
  }

  void retire_()
  {
    if (m_replay) { finish_(); return; }
    switch (m_retire) {
      case 0:
	retire_(m_context->actions, m_change.actions,
	  [this](const Action &action) { return actionIndex_(action.name) >= 0; });
	break;
      case 1:
	retire_(m_context->roles, m_change.roles,
	  [this](const Role &role) { return roleIndex_(role.name) >= 0; });
	break;
      case 2:
	retire_(m_context->scopes, m_change.scopes, [this](const Scope &scope) {
	  for (const auto &item: m_input.catalog.scopes)
	    if (item.audienceID == scope.audienceID && item.name == scope.name) return true;
	  return false;
	});
	break;
      default: finish_(); break;
    }
  }

  void finish_()
  {
    if (m_replay && !m_change.actions.added && !m_change.actions.changed &&
	!m_change.roles.added && !m_change.roles.changed &&
	!m_change.scopes.added && !m_change.scopes.changed) {
      m_change.after = m_change.before;
    } else {
      auto &app = m_change.after;
      if (app.version == UINT64_MAX || app.authVersion == UINT64_MAX) {
	fail_(409, "conflict", "application version exhausted"); return;
      }
      app.nextActionID = m_nextActionID;
      app.catalogRevision = m_input.revision;
      app.catalogDigest = m_digest;
      ++app.authVersion;
      ++app.version;
    }
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) {
      fail_(503, "unavailable", "saga ID generation failed"); return;
    }
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(m_change));
    if (!sagaSubmit(m_db, id, ZuMv(saga),
	[self = ZmRef<CatalogPublish_>{this}](bool ok) mutable {
	  if (!ok) self->fail_(503, "unavailable", "catalog publication unavailable");
	}, [self = ZmRef<CatalogPublish_>{this}](bool ok) mutable {
	  if (ok) self->success_();
	  else self->fail_(409, "conflict", "catalog publication conflict");
	})) fail_(503, "unavailable", "catalog publication unavailable");
  }

  DB		*m_db;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  AppID		m_appID = 0;
  CatalogInput	m_input;
  String	m_ifMatch;
  AdminDoneFn	m_complete;
  Bytes		m_digest;
  App		m_app;
  CatalogPublish m_change;
  ActionID	m_nextActionID = 0;
  ActionIDVec	m_actionIDs;
  IDVec		m_roleIDs;
  unsigned	m_offset = 0;
  unsigned	m_retire = 0;
  bool		m_done = false;
  bool		m_replay = false;
};

String Daemon::error_(ZuCSpan error, ZuCSpan message)
{
  return adminJSON(AdminErrorWire{error, message, {}});
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
  ZuPtr<ZfJSON::AnyNode> owner;
  AdminJSON parsed;
  if (!adminJSONParse(json, owner, parsed) ||
      !owner->has<ZfJSON::AnyNode::Object>()) return json;
  auto error = ZfJSON::handler<AdminErrorWire>(owner).ctor();
  if (!error.error || !error.message || error.correlationID) return json;
  error.correlationID = id;
  return adminJSON(ZuMv(error));
}

void Daemon::adminAudit_(int op, String actor, AppID appID,
    String target, String correlationID, AdminResult result,
    AdminDoneFn complete)
{
  if (!managementAudited(op)) {
    if (result.status >= 400)
      result.body = correlate_(ZuMv(result.body), correlationID);
    complete(ZuMv(result));
    return;
  }
  Audit audit = managementAuditRecord(m_config.issuer, op, ZuMv(actor), appID,
    ZuMv(target), String{correlationID}, result.status, Zm::now().sec());
  logEvent(ZuMv(audit));
  if (result.status >= 400)
    result.body = correlate_(ZuMv(result.body), correlationID);
  complete(ZuMv(result));
}

String Daemon::issuerJSON_(const Issuer &issuer)
{
  return adminJSON(AdminIssuerReply{{AdminIssuerItem{
    issuer.id, issuer.schemaVersion, issuer.coreAppID,
    BootstrapPhase::name(issuer.bootstrapPhase)}}});
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
  ZuBArray<macLength> digest(macLength, false);
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength});
  hmac.finish(digest);
  bool valid = raw[0] == 1 && raw[1] == uint8_t(operation) &&
    raw[2] < MgmtOp::N && Ztls::ctEqual(digest,
      {raw.data() + headerLength, macLength});
  if (valid) first = unsigned(raw[2]) + 1;
  ZuClear(raw.data(), raw.length());
  return valid;
}

String Daemon::operationsJSON_(
    unsigned first, unsigned limit, ZuBSpan secret)
{
  AdminOperationsReply reply;
  unsigned end = first, count = 0;
  while (end < MgmtOp::N && count < limit) {
    unsigned op = end++;
    const auto *route = managementRoute(op);
    if (!route) continue;
    ++count;
    reply.items.push(AdminOperationItem{op, MgmtOp::name(op),
      managementAction(op), Zhttp::Method::name(route->method), route->path});
  }
  while (end < MgmtOp::N && !managementRoute(end)) ++end;
  if (end < MgmtOp::N)
    reply.nextCursor = adminOperationCursorEncode(
      secret, MgmtOp::operationQuery, end - 1);
  return adminJSON(ZuMv(reply));
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

template <typename Table, unsigned KeyID = 0>
static bool adminCursorDecode(
    ZuBSpan secret, int operation, AppID appID, ZuCSpan encoded,
    typename Table::template Key<KeyID> &key)
{
  constexpr unsigned headerLength = 10;
  constexpr unsigned macLength = Ztls::HMAC<>::Size;
  if (encoded.length() % 4 == 1) return false;
  for (auto c: encoded)
    if (!ZuBase64URL::is(c)) return false;
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
  ZuBArray<macLength> digest(macLength, false);
  Ztls::HMAC<> hmac;
  hmac.start(secret);
  hmac.update({raw.data(), headerLength + dataLength});
  hmac.finish(digest);
  if (!Ztls::ctEqual(digest,
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
  key = ZfbStruct::ctor<typename Table::template Key<KeyID>>(fbo);
  ZuClear(raw.data(), raw.length());
  return true;
}

template <typename Table, typename Match, typename Group = ZuTuple<>,
  unsigned KeyID = 0>
class AdminQuery_ : public ZumObject {
  using Tuple = typename Table::Tuple;
  using Record = typename Table::T;
  using Key = typename Table::template Key<KeyID>;

public:
  AdminQuery_(Table *table, Match match, unsigned limit, String cursor,
      int operation, AppID appID, Bytes secret, AdminDoneFn complete,
      Group group = {}) :
    m_table{table}, m_match{ZuMv(match)}, m_limit{limit},
    m_cursor{ZuMv(cursor)}, m_operation{operation}, m_appID{appID},
    m_secret{ZuMv(secret)}, m_complete{ZuMv(complete)},
    m_group{ZuMv(group)} { }

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
        if (!adminCursorDecode<Table, KeyID>(self->m_secret, self->m_operation,
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
    auto receive = [self = ZmRef<AdminQuery_>{this}](
        ZuUnion<void, Tuple> result, unsigned count) mutable {
      self->receive_(ZuMv(result), count);
    };
    if constexpr (Group::N)
      m_table->template selectRows<KeyID>(ZuMv(m_group), ScanSize, ZuMv(receive));
    else
      m_table->template selectRows<KeyID>({}, ScanSize, ZuMv(receive));
  }

  void scanNext_()
  {
    m_rawCount = 0;
    m_table->template nextRows<KeyID>(m_scanKey, false, ScanSize,
      [self = ZmRef<AdminQuery_>{this}](ZuUnion<void, Tuple> result,
          unsigned count) mutable { self->receive_(ZuMv(result), count); });
  }

  void receive_(ZuUnion<void, Tuple> result, unsigned count)
  {
    if (m_done) return;
    if (result.template is<Tuple>()) {
      auto tuple = ZuMv(result).template p<Tuple>();
      m_rawCount = count;
      m_scanKey = ZuStructKey<KeyID>(tuple);
      if (!m_match(tuple)) return;
      if (m_count >= m_limit) { m_more = true; return; }
      m_lastKey = m_scanKey;
      ++m_count;
      String etag;
      if constexpr (HasVersion<Record>{}) {
        constexpr unsigned version =
          ZuTypeIndex<ZuStringT<"version">, ZuFieldIDs<Record>>{};
        etag = rowETag(tuple.template p<version>());
      }
      m_items.push(AdminEncoded{
	adminPublicJSON(tuple), ZuMv(etag)});
      return;
    }
    if (!m_more && m_rawCount == ScanSize) { scanNext_(); return; }
    String nextCursor;
    if (m_more) nextCursor = adminCursorEncode<Record>(m_secret,
      m_operation, m_appID, m_lastKey);
    String body;
    if (!adminItemsJSON(m_items, ZuMv(nextCursor), body)) {
      finish_(adminErrorResult(503, "unavailable", "query encoding failed"));
      return;
    }
    finish_(AdminResult{ZuMv(body), 200});
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
  Group		m_group;
  Key		m_scanKey;
  Key		m_lastKey;
  AdminEncodedVec m_items;
  unsigned	m_count = 0;
  unsigned	m_rawCount = 0;
  bool		m_more = false;
  bool		m_done = false;
};

template <unsigned KeyID = 0, typename Table, typename Match,
  typename Group = ZuTuple<>>
static void adminQueryIf(Table *table, Match match, unsigned limit,
    String cursor, int operation, AppID appID, Bytes secret,
    AdminDoneFn complete, Group group = {})
{
  using Query = AdminQuery_<Table, Match, Group, KeyID>;
  ZmRef<Query> query = new Query{
    table, ZuMv(match), limit, ZuMv(cursor), operation, appID, ZuMv(secret),
    ZuMv(complete), ZuMv(group)};
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
      AdminEncodedVec encoded;
      if (row) {
	String etag;
	if constexpr (HasVersion<Record>{}) {
	  etag = rowETag(row->data().version);
	}
	encoded.push(AdminEncoded{
	  adminPublicJSON(row->data()), ZuMv(etag)});
      }
      String body;
      if (!adminItemsJSON(encoded, {}, body)) {
	complete(adminErrorResult(503, "unavailable", "query encoding failed"));
	return;
      }
      complete(AdminResult{ZuMv(body), 200});
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
  return adminUInt(target, appID) && appID;
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
  return AdminResult{adminJSON(AdminErrorWire{error, message, {}}), status};
}

template <typename Record>
static AdminResult adminItemResult(
    const Record &record, unsigned status, StringVec ids)
{
  String etag;
  if constexpr (HasVersion<Record>{}) etag = rowETag(record.version);
  String source = adminPublicJSON(record);
  ZuPtr<ZfJSON::AnyNode> owner;
  AdminJSON item;
  if (!adminJSONParse(source, owner, item) ||
      (etag && !adminJSONAddString(owner, "etag", etag.span())))
    return adminErrorResult(503, "unavailable", "response encoding failed");
  return AdminResult{adminJSON(AdminNullReply{ZuMv(item)}),
    status, ZuMv(ids)};
}

static bool stateBody(String &body, State::T &state)
{
  if (!body || body.length() > (64U<<10)) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  auto input = ZfJSON::handler<StateInput>(roots[0]).ctor();
  State::T next = State::lookup(input.state);
  if (!input.state || next < 0 || next >= State::N ||
      next == State::Pending || next == State::Consumed) return false;
  state = next;
  return true;
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
    ZuCSpan target, ZuCSpan prefix, ZuCSpan suffix, uint64_t &value,
    bool allowZero = false)
{
  String encoded;
  if (!pathValue(target, prefix, suffix, encoded)) return false;
  return adminUInt(encoded, value) && (value || allowZero);
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
    IdemRequest request, AdminDoneFn complete)
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
	  uint64_t id;
	  operation = adminUInt(queryInput.operation, id) && id < MgmtOp::N ? int(id) : -1;
	}
	if (!managementRoute(operation)) {
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
	uint64_t id;
	if (!adminUInt(queryInput.id, id) || !id) break;
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
	uint64_t id;
	if (!adminUInt(queryInput.id, id) || !id) break;
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
      if (queryInput.id) {
	Bytes id;
	id.length(ZuBase64URL::declen(queryInput.id.length()), false);
	if (!id || ZuBase64URL::decode(id, ZuBSpan{queryInput.id}) != id.length()) break;
	adminFind<0>(m_context->creds, ZuFwdTuple(ZuMv(id)), ZuMv(complete));
	return;
      }
      if (queryInput.userID) {
	uint64_t id;
	if (!adminUInt(queryInput.userID, id) || !id) break;
	adminQueryIf<1>(m_context->creds,
	  [id](const auto &row) { return row.template p<1>() == id; },
	  queryInput.limit, String{queryInput.cursor}, op, id,
	  Bytes{m_config.dbKey}, ZuMv(complete), ZuFwdTuple(UserID{id}));
	return;
      }
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
	    uint64_t id;
	    if (!adminUInt(queryInput.userID, id) || !id) {
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
	    uint64_t id;
	    if (!adminUInt(queryInput.id, id) || id > UINT32_MAX) {
	      complete(AdminResult{
		error_("invalid_request", "invalid action id filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->actions,
	      ZuFwdTuple(appID, ActionID(id)), ZuMv(complete));
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
	    uint64_t id;
	    if (!adminUInt(queryInput.id, id) || !id) {
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
	    uint64_t id;
	    if (!adminUInt(queryInput.id, id) || !id) {
	      complete(AdminResult{
		error_("invalid_request", "invalid scope id filter"), 400});
	      return;
	    }
	    adminFind<0>(m_context->scopes,
	      ZuFwdTuple(appID, ScopeID{id}), ZuMv(complete));
	    return;
	  }
	  if (queryInput.audienceID && queryInput.name) {
	    uint64_t id;
	    if (!adminUInt(queryInput.audienceID, id) || !id) {
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
	  adminQueryIf<1>(m_context->clientAccess, [](const auto &) { return true; },
	    queryInput.limit, String{queryInput.cursor}, op, appID,
	    Bytes{m_config.dbKey}, ZuMv(complete), ZuTuple<AppID>{appID}); return;
	case MgmtOp::adminAccessQuery:
	  adminQueryApp<2>(m_context->adminAccess, appID, queryInput.limit,
	    String{queryInput.cursor}, op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	default:
	  adminQueryIf<1>(m_context->roleMaps, [](const auto &) { return true; },
	    queryInput.limit, String{queryInput.cursor}, op, appID,
	    Bytes{m_config.dbKey}, ZuMv(complete), ZuTuple<AppID>{appID}); return;
      }
    }
    case MgmtOp::audienceQuery:
      if (queryInput.id) {
	uint64_t id;
	if (!adminUInt(queryInput.id, id) || !id) break;
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
	uint64_t id;
	if (!adminUInt(queryInput.id, id) || !id) break;
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
	uint64_t id;
	if (!adminUInt(queryInput.appID, id) || !id) break;
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
    case MgmtOp::appUpdate: {
      uint64_t id;
      String label, unused;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/apps/", {}, id) ||
	  !stringPatchBody(body, "label", label, {}, unused, seen)) break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<AppChange>> change = new AppEdit_<AppChange>{m_db, m_context, &m_rng,
	AppChange{.before = App{.id = id}, .ifMatch = ZuMv(ifMatch),
	  .label = ZuMv(label), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::userUpdate: {
      uint64_t id;
      String profile, email;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/users/", {}, id) ||
	  !stringPatchBody(body, "profile", profile, "email", email, seen))
	break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<UserEdit>> change = new AppEdit_<UserEdit>{m_db, m_context, &m_rng,
	UserEdit{.before = User{.id = id}, .ifMatch = ZuMv(ifMatch),
	  .profile = ZuMv(profile), .request = ZuMv(request),
	  .email = ZuMv(email), .fields = uint8_t(seen)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::credentialUpdate: {
      Bytes id;
      String label, unused;
      unsigned seen = 0;
      if (!pathBytes(target, "/admin/credentials/", {}, id) ||
	  !stringPatchBody(body, "label", label, {}, unused, seen)) break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<CredEdit>> change = new AppEdit_<CredEdit>{m_db, m_context, &m_rng,
	CredEdit{.before = Cred{.id = ZuMv(id)}, .ifMatch = ZuMv(ifMatch),
	  .label = ZuMv(label), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<RoleEdit>> change = new AppEdit_<RoleEdit>{m_db, m_context, &m_rng,
	RoleEdit{.app = App{.id = appID}, .before = Role{.appID = appID, .id = id},
	  .ifMatch = ZuMv(ifMatch), .request = ZuMv(request),
	  .kind = RoleEdit::Label, .label = ZuMv(label)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::audienceUpdate: {
      uint64_t id;
      String name, unused;
      unsigned seen = 0;
      if (!pathUInt(target, "/admin/audiences/", {}, id) ||
	  !stringPatchBody(body, "name", name, {}, unused, seen)) break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<AudienceEdit>> change = new AppEdit_<AudienceEdit>{m_db, m_context, &m_rng,
	AudienceEdit{.before = Audience{.id = id}, .ifMatch = ZuMv(ifMatch),
	  .request = ZuMv(request), .name = ZuMv(name)}, ZuMv(complete)};
      change->start();
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
	  request = ZuMv(request), complete = ZuMv(complete)]() mutable {
	String issuer = m_config.issuer;
	issuers->find<0>(0, ZuFwdTuple(ZuMv(issuer)), [this,
	    input = ZuMv(input), request = ZuMv(request), complete = ZuMv(complete)](
	      ZdbRowRef<Issuer> issuer) mutable {
	  if (!issuer) {
	    complete(AdminResult{
	      error_("unavailable", "issuer is unavailable"), 503});
	    return;
	  }
	  ZmRef<AppEnroll_> enroll = new AppEnroll_{m_db, m_context, &m_rng,
	    issuer->data(), ZuMv(input), ZuMv(request), ZuMv(complete)};
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
      OpaqueToken capability;
      if (!opaqueIssue(m_rng, capability)) {
	complete(adminErrorResult(503, "unavailable", "enrollment capability failed"));
	return;
      }
      int64_t now = Zm::now().sec();
      Grant grant{.id = ZuMv(capability.id), .userID = id, .created = now,
	.expires = now + 86400, .kind = GrantKind::Capability,
	.purpose = GrantPurpose::Enrollment, .state = State::Active,
	.issuer = m_config.issuer, .digest = ZuMv(capability.digest),
	.userName = input.name, .label = "Zum passkey", .actor = "precreated"};
      ZmRef<AppEdit_<UserInvite>> invite = new AppEdit_<UserInvite>{
	m_db, m_context, &m_rng, UserInvite{.before = User{.id = id},
	  .values = User{.id = id, .name = ZuMv(input.name),
	    .profile = ZuMv(input.profile), .email = ZuMv(input.email)},
	  .grant = ZuMv(grant), .request = ZuMv(request)},
	[issuer = String{m_config.issuer}, token = ZuMv(capability.token),
	    complete = ZuMv(complete)](AdminResult result) mutable {
	  if (result.status == 201) {
	    if (issuer[issuer.length() - 1] == '/') issuer.length(issuer.length() - 1);
	    issuer << "/enroll?capability=" << token;
	    if (!adminAddItemString(result, "enrollmentURL", issuer))
	      result = adminErrorResult(503, "unavailable", "response encoding failed");
	    ZuClear(issuer.data(), issuer.length());
	  }
	  ZuClear(token.data(), token.length());
	  complete(ZuMv(result));
	}};
      invite->start();
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
	  ifMatch = ZuMv(ifMatch), request = ZuMv(request), complete = ZuMv(complete)]() mutable {
	users->find<0>(0, ZuFwdTuple(userID), [this, userID,
	    actor = ZuMv(actor), ifMatch = ZuMv(ifMatch),
	    request = ZuMv(request), complete = ZuMv(complete)](ZdbRowRef<User> user) mutable {
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
	      .version = user->data().version, .request = ZuMv(request)},
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
	      String json = adminJSON(AdminRecoveryReply{{userID, ZuMv(url)}});
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
      ZmRef<AppEdit_<RoleAdd>> change = new AppEdit_<RoleAdd>{
	m_db, m_context, &m_rng, RoleAdd{
	  .before = Role{.appID = appID, .id = id},
	  .values = Role{.appID = appID, .id = id,
	    .name = ZuMv(input.name), .label = ZuMv(input.label)},
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
	appID, ZuMv(input), ZuMv(request), ZuMv(complete)};
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
      ZmRef<AppEdit_<AudienceAdd>> change = new AppEdit_<AudienceAdd>{
	m_db, m_context, &m_rng, AudienceAdd{
	  .before = Audience{.id = id, .appID = input.appID},
	  .values = Audience{.id = id, .appID = input.appID,
	    .name = ZuMv(input.name), .uri = ZuMv(input.uri)},
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      ZmRef<AppEdit_<ScopeAdd>> change = new AppEdit_<ScopeAdd>{
	m_db, m_context, &m_rng, ScopeAdd{
	  .before = Scope{.appID = appID, .id = id},
	  .values = Scope{.appID = appID, .id = id,
	    .audienceID = input.audienceID, .name = ZuMv(input.name)},
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      MembershipAdd membership{.appID = appID, .userID = input.userID,
	.created = now, .request = ZuMv(request)};
      ZmRef<MembershipAdd_> add = new MembershipAdd_{
	m_db, &m_rng, ZuMv(membership), ZuMv(complete)};
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
      ZmRef<MembershipChange_> change = new MembershipChange_{
	m_db, m_context, &m_rng, appID, userID, ZuMv(roleIDs), State::N,
	ZuMv(ifMatch), ZuMv(request), ZuMv(complete)};
      change->start();
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
      ZmRef<AppEdit_<RoleEdit>> change = new AppEdit_<RoleEdit>{m_db, m_context, &m_rng,
	RoleEdit{.app = App{.id = appID}, .before = Role{.appID = appID, .id = roleID},
	  .actionIDs = ZuMv(input.actionIDs), .ifMatch = ZuMv(ifMatch),
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      ZmRef<AppEdit_<ScopeEdit>> change = new AppEdit_<ScopeEdit>{m_db, m_context, &m_rng,
	ScopeEdit{.app = App{.id = appID}, .before = Scope{.appID = appID, .id = scopeID},
	  .roleIDs = ZuMv(input.roleIDs), .ifMatch = ZuMv(ifMatch),
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      ZuBArray<ClientSecretEntropySize> random(
	ClientSecretEntropySize, false);
      if (!m_rng.random(random)) {
	ZuClear(random.data(), random.length());
	complete(AdminResult{
	  error_("unavailable", "secret generation failed"), 503});
	return;
      }
      String secret = encode(random);
      ZuClear(random.data(), random.length());
      Bytes digest;
      digest.length(Ztls::SecretHash::Size, false);
      if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	ZuClear(secret.data(), secret.length());
	complete(AdminResult{
	  error_("unavailable", "secret generation failed"), 503});
	return;
      }
      ZmRef<AppEdit_<ClientEdit>> change = new AppEdit_<ClientEdit>{
	m_db, m_context, &m_rng, ClientEdit{
	  .before = Client{.id = ZuMv(clientID)}, .ifMatch = ZuMv(ifMatch),
	  .values = Client{.secretDigest = ZuMv(digest)}, .fields = ClientEdit::Secret,
	  .request = ZuMv(request), .overlapSeconds = input.overlapSeconds},
	[secret = ZuMv(secret), complete = ZuMv(complete)](AdminResult result) mutable {
	  if (result.status == 200) {
	    if (!adminAddItemString(result, "client_secret", secret))
	      result = adminErrorResult(503, "unavailable", "response encoding failed");
	  }
	  ZuClear(secret.data(), secret.length());
	  complete(ZuMv(result));
	}};
      change->start();
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
      ZuBArray<ClientCredentialEntropySize> random(
	ClientCredentialEntropySize, false);
      if (!m_rng.random(random)) {
	ZuClear(random.data(), random.length());
	complete(adminErrorResult(503, "unavailable",
	  "client credential generation failed"));
	return;
      }
      String clientID{"cli_"};
      clientID << encode({random.data(), ClientIDEntropySize});
      String secret;
      Bytes digest;
      if (type == ClientType::Confidential) {
	secret = encode({random.data() + ClientIDEntropySize,
	  ClientSecretEntropySize});
	digest.length(Ztls::SecretHash::Size, false);
	if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	  ZuClear(random.data(), random.length());
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
      ZuClear(random.data(), random.length());
      ZmRef<AppEdit_<ClientAdd>> add = new AppEdit_<ClientAdd>{
	m_db, m_context, &m_rng, ClientAdd{
	  .before = Client{.id = client.id, .appID = client.appID},
	  .values = ZuMv(client), .request = ZuMv(request)},
	[secret = ZuMv(secret), complete = ZuMv(complete)](AdminResult result) mutable {
	  if (result.status == 201 && secret) {
	    if (!adminAddItemString(result, "client_secret", secret))
	      result = adminErrorResult(503, "unavailable", "response encoding failed");
	  }
	  if (secret) ZuClear(secret.data(), secret.length());
	  complete(ZuMv(result));
	}};
      add->start();
      return;
    }
    case MgmtOp::clientUpdate: {
      String id;
      ClientUpdateInput input;
      uint64_t seen;
      if (!pathValue(target, "/admin/clients/", {}, id) ||
	  !adminBodyFields<ClientUpdateFields>(body, input, seen)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client update"));
	return;
      }
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<ClientEdit>> change = new AppEdit_<ClientEdit>{m_db, m_context, &m_rng,
	ClientEdit{.before = Client{.id = ZuMv(id)}, .ifMatch = ZuMv(ifMatch),
	  .values = Client{.label = ZuMv(input.label), .redirects = ZuMv(input.redirectURIs),
	    .grants = input.grants, .identityScopes = ZuMv(input.identityScopes)},
	  .fields = uint8_t(seen), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      ZmRef<AppEdit_<ClientAccessPut>> change = new AppEdit_<ClientAccessPut>{
	m_db, m_context, &m_rng, ClientAccessPut{
	  .before = ClientAccess{.clientID = clientID, .appID = appID},
	  .values = ClientAccess{.clientID = ZuMv(clientID), .appID = appID,
	    .audienceIDs = ZuMv(input.audienceIDs), .scopeIDs = ZuMv(input.scopeIDs),
	    .roleIDs = ZuMv(input.roleIDs)}, .ifMatch = ZuMv(ifMatch),
	  .ifNoneMatch = ZuMv(ifNoneMatch), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::clientAccessState: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/client-access/";
      String clientID;
      if (pathValue(target, prefix, "/state", clientID)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<ClientAccessState>> change = new AppEdit_<ClientAccessState>{
	  m_db, m_context, &m_rng, ClientAccessState{
	    .before = ClientAccess{.clientID = ZuMv(clientID), .appID = appID},
	    .ifMatch = ZuMv(ifMatch), .request = ZuMv(request), .state = state}, ZuMv(complete)};
	change->start();
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
      String actorID{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      ActorKind::T kind = kindName == "user" ? ActorKind::User :
	kindName == "client" ? ActorKind::Client : ActorKind::T(-1);
      AdminAccessInput input;
      if (!adminActorID(kind, actorID) ||
	  !adminBody(body, input)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid administrative access replacement"));
	return;
      }
      ZmRef<AppEdit_<AdminAccessPut>> change = new AppEdit_<AdminAccessPut>{
	m_db, m_context, &m_rng, AdminAccessPut{
	  .before = AdminAccess{.actorKind = kind, .actorID = String{actorID}, .appID = appID},
	  .values = AdminAccess{.actorKind = kind, .actorID = ZuMv(actorID), .appID = appID,
	    .operationIDs = ZuMv(input.operationIDs), .roleIDs = ZuMv(input.roleIDs)},
	  .ifMatch = ZuMv(ifMatch), .ifNoneMatch = ZuMv(ifNoneMatch),
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      String actorID{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      ActorKind::T kind = kindName == "user" ? ActorKind::User :
	kindName == "client" ? ActorKind::Client : ActorKind::T(-1);
      if (!adminActorID(kind, actorID)) break;
      State::T state;
      if (!stateBody(body, state)) {
	complete(adminErrorResult(400, "invalid_request", "invalid state"));
	return;
      }
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<AdminAccessState>> change = new AppEdit_<AdminAccessState>{
	m_db, m_context, &m_rng, AdminAccessState{
	  .before = AdminAccess{.actorKind = kind, .actorID = ZuMv(actorID), .appID = appID},
	  .ifMatch = ZuMv(ifMatch), .request = ZuMv(request), .state = state}, ZuMv(complete)};
      change->start();
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
      ZmRef<AppEdit_<ProviderAdd>> change = new AppEdit_<ProviderAdd>{
	m_db, m_context, &m_rng, ProviderAdd{
	  .before = Provider{.id = id},
	  .values = Provider{.id = id, .name = ZuMv(input.name),
	    .issuer = ZuMv(input.issuer), .clientID = ZuMv(input.clientID),
	    .clientSecret = ZuMv(protectedSecret), .scopes = ZuMv(input.scopes),
	    .roleClaim = ZuMv(input.roleClaim), .claimSource = source},
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      if (!pathUInt(target, "/admin/providers/", {}, id) ||
	  !adminBodyFields<ProviderUpdateFields>(body, input, seen)) {
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
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<ProviderEdit>> change = new AppEdit_<ProviderEdit>{m_db, m_context, &m_rng,
	ProviderEdit{.before = Provider{.id = id}, .ifMatch = ZuMv(ifMatch),
	  .values = Provider{.issuer = ZuMv(input.issuer), .clientID = ZuMv(input.clientID),
	    .clientSecret = ZuMv(protectedSecret), .scopes = ZuMv(input.scopes),
	    .roleClaim = ZuMv(input.roleClaim), .claimSource = source},
	  .fields = uint8_t(seen), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      AuthPolicy policy{.appID = appID, .providerID = input.providerID,
	.localFirst = input.localFirst, .eligibilityMode = eligibility,
	.eligibilityClaim = ZuMv(input.eligibilityClaim),
	.eligibilityValues = ZuMv(input.eligibilityValues),
	.assignmentMaxAge = input.assignmentMaxAge,
	.sessionIdle = input.sessionIdle,
	.sessionAbsolute = input.sessionAbsolute,
	.tokenLifetime = input.tokenLifetime, .consentPolicy = consent,
	.state = state};
      ZmRef<AppEdit_<PolicyPut>> change = new AppEdit_<PolicyPut>{
	m_db, m_context, &m_rng, PolicyPut{
	  .before = AuthPolicy{.appID = appID}, .values = ZuMv(policy),
	  .ifMatch = ZuMv(ifMatch), .ifNoneMatch = ZuMv(ifNoneMatch),
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      uint64_t providerID;
      if (!adminUInt({rest.data(), unsigned(slash)}, providerID) || !providerID) break;
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
      ZmRef<AppEdit_<RoleMapPut>> change = new AppEdit_<RoleMapPut>{
	m_db, m_context, &m_rng, RoleMapPut{
	  .before = RoleMap{.appID = appID, .providerID = providerID,
	    .value = String{ZuCSpan{decoded}}}, .roleID = input.roleID,
	  .ifMatch = ZuMv(ifMatch), .ifNoneMatch = ZuMv(ifNoneMatch),
	  .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::roleMapDelete: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      String prefix{"/admin/apps/"};
      prefix << appID << "/role-mappings/";
      if (target.prefix(prefix) != prefix) break;
      ZuCSpan rest{target.data() + prefix.length(),
	target.length() - prefix.length()};
      auto slash = rest.find<"/">();
      if (slash <= 0 || unsigned(slash) + 1 >= rest.length()) break;
      uint64_t providerID;
      if (!adminUInt({rest.data(), unsigned(slash)}, providerID) || !providerID) break;
      ZuCSpan encoded{rest.data() + slash + 1,
	rest.length() - unsigned(slash) - 1};
      Bytes decoded;
      decoded.length(ZuBase64URL::declen(encoded.length()), false);
      if (!providerID ||
	  ZuBase64URL::decode(decoded, ZuBSpan{encoded}) != decoded.length() ||
	  !decoded) break;
      String value{decoded};
      ZmRef<AppEdit_<RoleMapDelete>> change = new AppEdit_<RoleMapDelete>{
	m_db, m_context, &m_rng, RoleMapDelete{
	  .before = RoleMap{.appID = appID, .providerID = providerID, .value = ZuMv(value)},
	  .ifMatch = ZuMv(ifMatch), .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
      return;
    }
    case MgmtOp::roleDelete: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID << "/roles/";
      uint64_t roleID;
      if (!pathUInt(target, prefix, {}, roleID)) break;
      roleDelete_(appID, RoleID{roleID}, ZuMv(ifMatch),
        ZuMv(request), ZuMv(complete));
      return;
    }
    case MgmtOp::sessionRevoke: {
      SessionSelector input;
      if (!adminBody(body, input)) break;
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Sessions, input.userID, {}, 0, 0, input.limit,
	ZuMv(request), ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::consentRevoke: {
      ConsentSelector input;
      if (!adminBody(body, input)) break;
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Consents, input.userID, ZuMv(input.clientID),
	input.appID, input.audienceID, input.limit, ZuMv(request), ZuMv(complete)};
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
	ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	  BulkRevoke_::Grants, 0, {}, 0, 0, 1, ZuMv(request), ZuMv(complete)};
	revoke->one(ZuMv(id));
	return;
      }
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Grants, input.userID, {}, input.appID, 0,
	input.limit, ZuMv(request), ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::grantCleanup: {
      CleanupInput input;
      if (!adminBody(body, input) || input.before) break;
      ZmRef<BulkRevoke_> cleanup = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Cleanup, 0, {}, 0, 0, input.limit, ZuMv(request), ZuMv(complete)};
      cleanup->start();
      return;
    }
    case MgmtOp::signKeyAdd: {
      SignKeyInput input;
      if (!adminBody(body, input) || input.algorithm != "ES256" ||
	  input.providerRef || !input.privateMaterial) break;
      Bytes protectedMaterial;
      Bytes plain;
      plain.length(ZuBase64URL::declen(input.privateMaterial.length()), false);
      if (ZuBase64URL::decode(plain, ZuBSpan{input.privateMaterial}) !=
	  plain.length() || !plain || !signKeyMatch(m_rng,
	    SignKey{.id = input.id, .publicJwk = input.publicJwk}, plain) ||
	  !serverSecretEncrypt(m_rng,
	    m_config.dbKey, m_config.issuer, "zum.sign_key", input.id,
	    "privateMaterial", plain, protectedMaterial)) {
	if (plain) ZuClear(plain.data(), plain.length());
	ZuClear(input.privateMaterial.data(), input.privateMaterial.length());
	complete(adminErrorResult(400, "invalid_request",
	  "invalid signing private material"));
	return;
      }
      ZuClear(plain.data(), plain.length());
      ZuClear(input.privateMaterial.data(), input.privateMaterial.length());
      int64_t now = Zm::now().sec();
      SignKey key{.id = ZuMv(input.id), .issuer = m_config.issuer,
	.algorithm = ZuMv(input.algorithm),
	.providerRef = ZuMv(input.providerRef),
	.publicJwk = ZuMv(input.publicJwk),
	.privateMaterial = ZuMv(protectedMaterial),
	.notBefore = input.notBefore, .state = State::Active,
	.version = 1, .created = now, .updated = now};
      ZmRef<AppEdit_<KeyAdd>> add = new AppEdit_<KeyAdd>{
	m_db, m_context, &m_rng, KeyAdd{.before = SignKey{.id = key.id},
	  .values = ZuMv(key), .request = ZuMv(request)}, ZuMv(complete)};
      add->start();
      return;
    }
    case MgmtOp::signKeyRetire: {
      String id;
      RetireInput input;
      if (!pathValue(target, "/admin/signing-keys/", "/retire", id) ||
	  !adminBody(body, input)) break;
      if (!ifMatch) {
	complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	return;
      }
      ZmRef<AppEdit_<KeyRetire>> change = new AppEdit_<KeyRetire>{m_db, m_context, &m_rng,
	KeyRetire{.before = SignKey{.id = ZuMv(id)}, .ifMatch = ZuMv(ifMatch),
	  .retireAfter = input.retireAfter, .request = ZuMv(request)}, ZuMv(complete)};
      change->start();
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
      ZmRef<CatalogPublish_> publish = new CatalogPublish_{m_db, m_context, &m_rng,
	appID, ZuMv(input), ZuMv(ifMatch), ZuMv(request), ZuMv(complete)};
      publish->start();
      return;
    }
    case MgmtOp::appState: {
      uint64_t id;
      if (pathUInt(target, "/admin/apps/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<AppChange>> change = new AppEdit_<AppChange>{m_db, m_context, &m_rng,
	  AppChange{.before = App{.id = id}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .stateOnly = true, .state = state}, ZuMv(complete)};
	change->start();
	return;
      }
      break;
    }
    case MgmtOp::userState: {
      uint64_t id;
      if (pathUInt(target, "/admin/users/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<UserEdit>> change = new AppEdit_<UserEdit>{m_db, m_context, &m_rng,
	  UserEdit{.before = User{.id = id}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .stateOnly = true, .state = state}, ZuMv(complete)};
	change->start();
	return;
      }
      break;
    }
    case MgmtOp::credentialState: {
      Bytes id;
      if (pathBytes(target, "/admin/credentials/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<CredEdit>> change = new AppEdit_<CredEdit>{m_db, m_context, &m_rng,
	  CredEdit{.before = Cred{.id = ZuMv(id)}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .stateOnly = true, .state = state}, ZuMv(complete)};
	change->start();
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
      if (!pathUInt(target, prefix, "/state", id, op == MgmtOp::actionState)) break;
      switch (op) {
	case MgmtOp::membershipState: {
	  State::T state;
	  if (!stateBody(body, state)) {
	    complete(adminErrorResult(400, "invalid_request", "invalid state"));
	    return;
	  }
	  if (!ifMatch) {
	    complete(adminErrorResult(428, "precondition_required",
	      "If-Match is required"));
	    return;
	  }
	  ZmRef<MembershipChange_> change = new MembershipChange_{
	    m_db, m_context, &m_rng, appID, id, {}, state,
	    ZuMv(ifMatch), ZuMv(request), ZuMv(complete)};
	  change->start();
	  return;
	}
	case MgmtOp::actionState: {
	  if (id > UINT32_MAX) break;
	  State::T state;
	  if (!stateBody(body, state)) {
	    complete(adminErrorResult(400, "invalid_request", "invalid state"));
	    return;
	  }
	  if (!ifMatch) {
	    complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	    return;
	  }
	  ZmRef<AppEdit_<ActionEdit>> change = new AppEdit_<ActionEdit>{m_db, m_context, &m_rng,
	    ActionEdit{.app = App{.id = appID},
	      .before = Action{.appID = appID, .id = ActionID(id)},
	      .ifMatch = ZuMv(ifMatch), .request = ZuMv(request), .state = state},
	    ZuMv(complete)};
	  change->start();
	  return;
	}
	case MgmtOp::roleState: {
	  State::T state;
	  if (!stateBody(body, state)) {
	    complete(adminErrorResult(400, "invalid_request", "invalid state"));
	    return;
	  }
	  if (!ifMatch) {
	    complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	    return;
	  }
	  ZmRef<AppEdit_<RoleEdit>> change = new AppEdit_<RoleEdit>{m_db, m_context, &m_rng,
	    RoleEdit{.app = App{.id = appID}, .before = Role{.appID = appID, .id = id},
	      .ifMatch = ZuMv(ifMatch), .request = ZuMv(request),
	      .kind = RoleEdit::Status, .state = state}, ZuMv(complete)};
	  change->start();
	  return;
	}
	default: {
	  State::T state;
	  if (!stateBody(body, state)) {
	    complete(adminErrorResult(400, "invalid_request", "invalid state"));
	    return;
	  }
	  if (!ifMatch) {
	    complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	    return;
	  }
	  ZmRef<AppEdit_<ScopeEdit>> change = new AppEdit_<ScopeEdit>{m_db, m_context, &m_rng,
	    ScopeEdit{.app = App{.id = appID}, .before = Scope{.appID = appID, .id = id},
	      .ifMatch = ZuMv(ifMatch), .request = ZuMv(request),
	      .kind = ScopeEdit::Status, .state = state}, ZuMv(complete)};
	  change->start();
	  return;
	}
      }
      break;
    }
    case MgmtOp::audienceState: {
      uint64_t id;
      if (pathUInt(target, "/admin/audiences/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<AudienceEdit>> change = new AppEdit_<AudienceEdit>{m_db, m_context, &m_rng,
	  AudienceEdit{.before = Audience{.id = id}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .state = state, .stateOnly = true}, ZuMv(complete)};
	change->start();
	return;
      }
      break;
    }
    case MgmtOp::clientState: {
      String id;
      if (pathValue(target, "/admin/clients/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<ClientEdit>> change = new AppEdit_<ClientEdit>{m_db, m_context, &m_rng,
	  ClientEdit{.before = Client{.id = ZuMv(id)}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .stateOnly = true, .state = state}, ZuMv(complete)};
	change->start();
	return;
      }
      break;
    }
    case MgmtOp::providerState: {
      uint64_t id;
      if (pathUInt(target, "/admin/providers/", "/state", id)) {
	State::T state;
	if (!stateBody(body, state)) {
	  complete(adminErrorResult(400, "invalid_request", "invalid state"));
	  return;
	}
	if (!ifMatch) {
	  complete(adminErrorResult(428, "precondition_required", "If-Match is required"));
	  return;
	}
	ZmRef<AppEdit_<ProviderEdit>> change = new AppEdit_<ProviderEdit>{m_db, m_context, &m_rng,
	  ProviderEdit{.before = Provider{.id = id}, .ifMatch = ZuMv(ifMatch),
	    .request = ZuMv(request), .stateOnly = true, .state = state}, ZuMv(complete)};
	change->start();
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

static void serviceField(
    String &query, bool &first, ZuCSpan name, ZuCSpan value)
{
  if (!first) query << '&';
  first = false;
  ZfURI::PathQuote::quote(query, name);
  query << '=';
  ZfURI::PathQuote::quote(query, value);
}

static bool serviceAuthorizeQuery(String body, String &query)
{
  ServiceAuthorizeInput input;
  uint64_t seen = 0;
  constexpr uint64_t required = (uint64_t{1}<<0) | (uint64_t{1}<<1) |
    (uint64_t{1}<<2) | (uint64_t{1}<<3) | (uint64_t{1}<<6) |
    (uint64_t{1}<<7);
  if (!adminBodyFields<ServiceAuthorizeFields>(body, input, seen) ||
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
  ServiceFormInput input;
  uint64_t seen = 0;
  if (!adminBodyFields<ServiceFormFields>(body, input, seen) ||
      seen != 1 || !input.form)
    return false;
  form = ZuMv(input.form);
  return true;
}

void Daemon::serviceCall_(
    int operation, String authorization, String body, AdminDoneFn complete)
{
  auto cancel = [complete]() mutable {
    complete(AdminResult{
      error_("temporarily_unavailable", "request unavailable"), 503});
  };
  if (!m_requests->run(Zm::now() + ZuTime{double(m_config.requestTimeout)},
      [this, operation, authorization = ZuMv(authorization), body = ZuMv(body),
        complete](ZmRef<Request> request) mutable {
    serviceRequest_(operation, ZuMv(authorization), ZuMv(body),
      [request = ZuMv(request), complete = ZuMv(complete)](AdminResult result) mutable {
        request->complete([complete = ZuMv(complete), result = ZuMv(result)]() mutable {
          complete(ZuMv(result));
        });
      });
  }, cancel)) cancel();
}

void Daemon::serviceRequest_(
    int operation, String authorization, String body, AdminDoneFn complete)
{
  if (!m_requests->active()) {
    complete(AdminResult{
      error_("temporarily_unavailable", "server is inactive"), 503});
    return;
  }
  adminAuth_(ZuMv(authorization), [this, operation, body = ZuMv(body),
    complete = ZuMv(complete)
  ](bool authenticated, Principal principal) mutable {
    if (!authenticated || principal.authMethod || !principal.appID ||
	!principal.clientID) {
      complete(AdminResult{
	error_("invalid_token", "invalid service bearer token"), 401});
      return;
    }
    serviceAuthed_(operation, ZuMv(principal), ZuMv(body), ZuMv(complete));
  });
}

void Daemon::serviceAuthed_(
    int operation, Principal principal, String body, AdminDoneFn complete)
{
  if (!m_requests->active()) {
    complete(AdminResult{
      error_("temporarily_unavailable", "server is inactive"), 503});
    return;
  }
  unsigned actionID = operation == ServiceOp::Authorize ?
    CoreAction::FacadeAuthorize : operation == ServiceOp::Token ?
      CoreAction::FacadeToken : CoreAction::FacadeRevoke;
  bool permitted = false;
  auto required = coreAction(actionID);
  for (const auto &action: principal.actions)
    if (action == required) { permitted = true; break; }
  if (!permitted) {
    complete(AdminResult{
      error_("forbidden", "service operation is not permitted"), 403});
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
              auto finish = [this, complete = ZuMv(complete)](
                  ServerReply reply) mutable {
                if (!m_requests->active()) {
                  complete(AdminResult{
                    error_("temporarily_unavailable", "server is inactive"), 503});
                  return;
                }
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

void Daemon::adminAuth_(String authorization, AdminAuthFn complete)
{
  static constexpr ZuCSpan prefix{"Bearer "};
  if (authorization.length() <= prefix.length() ||
      ZuCSpan{authorization}.prefix(prefix) != prefix.length()) {
    complete(false, Principal{});
    return;
  }
  authorization.splice(0, prefix.length());
  JWTHeader header;
  int64_t now = Zm::now().sec();
  if (now <= 0 || !jwtHeader(authorization, JWTLimits{}, header)) {
    complete(false, Principal{});
    return;
  }
  String audience{m_config.issuer};
  if (audience[audience.length() - 1] == '/')
    audience.length(audience.length() - 1);
  audience << "/admin";
  m_context->signKeys->find<0>(0, ZuFwdTuple(ZuMv(header.keyID)), [
    this, now, authorization = ZuMv(authorization), audience = ZuMv(audience),
    complete = ZuMv(complete)
  ](ZdbRowRef<SignKey> row) mutable {
    Principal principal;
    bool valid = row && signKeyVerify(row->data(), authorization,
      m_config.issuer, audience, now, JWTLimits{}, principal);
    complete(valid, ZuMv(principal));
  });
}

void Daemon::adminRequest_(int op, String authorization, String target,
    String query, String body, String ifMatch, String ifNoneMatch,
    String idempotencyKey, String correlationID, AdminDoneFn complete)
{
  auto issuers = m_context->issuers;
  issuers->run(0, [this, issuers, op, authorization = ZuMv(authorization),
      target = ZuMv(target), query = ZuMv(query), body = ZuMv(body),
      ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
      idempotencyKey = ZuMv(idempotencyKey), correlationID = ZuMv(correlationID),
      complete = ZuMv(complete)]() mutable {
    issuers->find<0>(0, ZuFwdTuple(m_config.issuer), [this, op,
        authorization = ZuMv(authorization), target = ZuMv(target),
        query = ZuMv(query), body = ZuMv(body), ifMatch = ZuMv(ifMatch),
        ifNoneMatch = ZuMv(ifNoneMatch), idempotencyKey = ZuMv(idempotencyKey),
        correlationID = ZuMv(correlationID), complete = ZuMv(complete)](
          ZdbRowRef<Issuer> row) mutable {
      if (!m_requests->active() || !row ||
          row->data().bootstrapPhase != BootstrapPhase::Ready) {
	complete(AdminResult{error_("unavailable", "server is not ready"), 503});
	return;
      }
      adminAuth_(ZuMv(authorization), [this, op, target = ZuMv(target),
          query = ZuMv(query), body = ZuMv(body), ifMatch = ZuMv(ifMatch),
          ifNoneMatch = ZuMv(ifNoneMatch), idempotencyKey = ZuMv(idempotencyKey),
          correlationID = ZuMv(correlationID), complete = ZuMv(complete)](
            bool authenticated, Principal principal) mutable {
	if (!authenticated) {
	  complete(AdminResult{error_("invalid_token", "invalid bearer token"), 401});
	  return;
	}
	String required = managementAction(op);
	bool permitted = false;
	for (const auto &action: principal.actions)
	  if (action == required) { permitted = true; break; }
	if (!permitted) {
	  complete(AdminResult{error_("forbidden", "operation is not permitted"), 403});
	  return;
	}
	String subject = principal.subject;
	bool interactive = bool(principal.authMethod);
	AppID targetApp = 0;
	bool appScoped = adminTargetApp(target, targetApp);
	adminAccess_(op, ZuMv(subject), interactive, targetApp, appScoped,
	  [this, op, principal = ZuMv(principal), target = ZuMv(target),
	    query = ZuMv(query), body = ZuMv(body), ifMatch = ZuMv(ifMatch),
	    ifNoneMatch = ZuMv(ifNoneMatch), idempotencyKey = ZuMv(idempotencyKey),
	    correlationID = ZuMv(correlationID), complete = ZuMv(complete)](
	      AdminPermit permit) mutable {
	    if (!permit.allowed) {
	      complete(AdminResult{error_("forbidden", "operation is not permitted"), 403});
	      return;
	    }
	    if (managementNeedsIdempotency(op) && !idempotencyKey) {
	      complete(AdminResult{error_("invalid_request", "Idempotency-Key is required"), 400});
	      return;
	    }
	    auto actorKind = principal.authMethod ? ActorKind::User : ActorKind::Client;
	    String actorID = principal.subject;
	    String requestKey = idempotencyKey;
	    Bytes requestDigest;
	    if (requestKey)
	      requestDigest = idemDigest_(op, target, body, ifMatch, ifNoneMatch);
	    idemBegin_(op, actorKind, ZuMv(actorID), ZuMv(requestDigest), ZuMv(requestKey),
	      [this, op, principal = ZuMv(principal), permit = ZuMv(permit),
	        target = ZuMv(target), query = ZuMv(query), body = ZuMv(body),
	        ifMatch = ZuMv(ifMatch), ifNoneMatch = ZuMv(ifNoneMatch),
	        idempotencyKey = ZuMv(idempotencyKey), correlationID = ZuMv(correlationID),
	        complete = ZuMv(complete)](IdemBegin idem) mutable {
		if (!idem.execute) { complete(ZuMv(idem.result)); return; }
		String auditActor = principal.subject;
		AppID auditAppID = permit.targetApp;
		String auditTarget = target;
		adminCall_(op, ZuMv(principal), ZuMv(permit), ZuMv(target), ZuMv(query),
		  ZuMv(body), ZuMv(ifMatch), ZuMv(ifNoneMatch), ZuMv(idempotencyKey),
		  ZuMv(idem.request), [this, op, auditActor = ZuMv(auditActor), auditAppID,
		    auditTarget = ZuMv(auditTarget), correlationID = ZuMv(correlationID),
		    complete = ZuMv(complete)](AdminResult result) mutable {
		    adminAudit_(op, ZuMv(auditActor), auditAppID, ZuMv(auditTarget),
		      ZuMv(correlationID), ZuMv(result), ZuMv(complete));
		  });
	      });
	  });
      });
    });
  });
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
      if (!row || row->data().owner || row->data().state != State::Active) {
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
	if (!row || row->data().owner || row->data().state != State::Active) {
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
      if (!user || user->data().owner ||
          user->data().source != UserSource::Local ||
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
	      if (membership && !membership->data().owner &&
	          membership->data().state == State::Active)
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
	          bool allowed = role && !role->data().owner &&
	            role->data().state == State::Active &&
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
  md.finish(digest);
  ZuClear(canonical.data(), canonical.length());
  return digest;
}

void Daemon::idemBegin_(int op, ActorKind::T actorKind, String actorID,
    Bytes digest, String idemKey, IdemBeginFn complete)
{
  auto route = managementRoute(op);
  if (!idemKey || (route && route->method == Zhttp::Method::GET)) {
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
      ActionID(op), ZuMv(lookupKey)), [actorKind,
        actorID = ZuMv(actorID), op, idemKey = ZuMv(idemKey),
        digest = ZuMv(digest), complete = ZuMv(complete)](
          ZdbRowRef<IdemRequest> row) mutable {
      if (row) {
	if (!Ztls::ctEqual(row->data().requestDigest, digest)) {
	  complete(IdemBegin{.result = adminErrorResult(409, "conflict",
	    "idempotency key reused with different request")});
	  return;
	}
	if (row->data().status == RequestStatus::Complete) {
	  complete(IdemBegin{.result = AdminResult{adminJSON(AdminIdemWire{
	    idemKey, "complete", row->data().resultIDs}), 200}});
	} else if (row->data().status == RequestStatus::Pending) {
	  complete(IdemBegin{.result = AdminResult{adminJSON(AdminIdemWire{
	    idemKey, "pending", {}}), 202}});
	} else {
	  complete(IdemBegin{.result = adminErrorResult(
	    503, "unavailable", "invalid request state")});
	}
	return;
      }
      int64_t now = Zm::now().sec();
      IdemRequest request{.actorKind = actorKind, .actorID = actorID,
	.operation = ActionID(op), .idempotencyKey = idemKey,
	.requestDigest = digest, .status = RequestStatus::Pending,
	.expires = now + 86400, .version = 1,
	.created = now, .updated = now};
      complete(IdemBegin{.request = ZuMv(request), .execute = true});
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

class AuthRouteLoad_ : public ZumObject {
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
      auto key = ZuFwdTuple(UserSource::Local, self->m_login);
      users->find<2>(0, ZuMv(key), [
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
    policies->run(0, [self = ZmRef<AuthRouteLoad_>{this}, policies]() mutable {
      auto key = ZuFwdTuple(self->m_appID);
      policies->find<0>(0, ZuMv(key), [
          self = ZuMv(self)](ZdbRowRef<AuthPolicy> row) mutable {
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
    });
  }

  void provider_()
  {
    auto providers = m_context->providers;
    providers->run(0, [self = ZmRef<AuthRouteLoad_>{this}, providers]() mutable {
      auto key = ZuFwdTuple(self->m_providerID);
      providers->find<0>(0, ZuMv(key), [
          self = ZuMv(self)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner ||
	    !row->data().issuer || !row->data().clientID ||
	    !row->data().roleClaim || !row->data().scopes ||
	    (row->data().claimSource != ClaimSource::IDToken &&
	     row->data().claimSource != ClaimSource::UserInfo)) {
	  self->finish_(AuthRouteType::Error);
	  return;
	}
	const auto &provider = row->data();
	self->m_config.issuer = provider.issuer;
	self->m_config.clientID = provider.clientID;
	self->m_config.oidcScopes = provider.scopes;
	self->m_config.roleClaim = provider.roleClaim;
	self->m_config.claimSource = provider.claimSource;
	self->m_config.loginHint = self->m_login;
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
	  self->m_config.clientSecret = plain;
	  ZuClear(plain.data(), plain.length());
	}
	self->maps_();
      });
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
    roles->run(0, [self = ZmRef<AuthRouteLoad_>{this}, roles]() mutable {
      auto map = self->m_maps[self->m_mapIndex++];
      auto key = ZuFwdTuple(self->m_appID, map.roleID);
      roles->find<0>(0, ZuMv(key), [
          self = ZuMv(self), map = ZuMv(map)](
          ZdbRowRef<Role> row) mutable {
	if (row && row->data().state == State::Active &&
	    !row->data().owner && !row->data().tombstone)
	  self->m_config.roleMap.push(ZuMv(map));
	self->role_();
      });
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
  ServerConfig config;
  int64_t now = Zm::now().sec();
  SignKey key = ZmBlock<SignKey>{}([this, now, &config](auto wake) mutable {
    m_context->signKeys->run(0, [this, now, expires = now + config.accessLifetime,
      maxKeys = config.limits.jwks, wake = ZuMv(wake)]() mutable {
      signKeyLoad(m_context, m_config.issuer, now, expires, maxKeys, [
        wake = ZuMv(wake)](SignKey key) mutable { wake(ZuMv(key)); });
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
    if (!signKeyMatch(m_rng, key, privateKey)) {
      ZuClear(privateKey.data(), privateKey.length());
      m_key = nullptr;
      return false;
    }
  } catch (...) {
    ZuClear(privateKey.data(), privateKey.length());
    return false;
  }
  ZuClear(privateKey.data(), privateKey.length());
  m_signKeyID = ZuMv(key.id);
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
  ServerConfig provider{
    .issuer = m_config.issuer, .rpID = m_config.rpID,
    .rpName = m_config.rpName, .requestTimeout = m_config.requestTimeout,
    .authMethod = AuthMethod::LocalFirst};
  if (!m_db || !m_context || !m_requests || !m_mx || !m_config.issuer ||
      !m_config.rpID || !m_config.admin || !m_config.requestTimeout ||
      !m_rng.init())
    return false;
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
          admission.enrollment.label = "Zum administrator passkey";
          admission.enrollment.userID = m_config.bootstrap.adminUserID;
        }
        complete(ZuMv(admission));
      },
      [this](const SignKey &record, ZuBSpan digest, SignatureFn complete) {
        Bytes signature;
        auto sign = [this, digest, &signature](auto &key) {
	  auto result = key.sign(m_rng, digest, [&signature](ZuBSpan der) {
	    signature = Bytes{der};
	  });
	  if (result.template is<ZeException>()) signature.null();
        };
        if (!record.providerRef && record.issuer == m_config.issuer && record.privateMaterial) {
	  if (record.id == m_signKeyID) {
	    sign(*m_key);
	  } else {
	    Bytes privateKey;
	    if (serverSecretDecrypt(m_config.dbKey, record.issuer,
		"zum.sign_key", record.id, "privateMaterial", record.privateMaterial, privateKey)) {
	      try {
		Ztls::PK::SK_EC key{m_rng, Ztls::PK::OIDs::EC_GRP_SECP256R1, privateKey};
		ZuClear(privateKey.data(), privateKey.length());
		sign(key);
	      } catch (...) { signature.null(); }
	    }
	    if (privateKey) ZuClear(privateKey.data(), privateKey.length());
	  }
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

bool Daemon::prepare(ServerBootstrapResult bootstrap)
{
  if (!loadKey_()) return false;
  m_config.bootstrap = ZuMv(bootstrap);
  return true;
}

bool Daemon::start()
{
  return m_httpInited && m_http.start();
}

void Daemon::stop()
{
  if (m_requests) m_requests->deactivate();
  m_provider.stop();
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
  m_signKeyID.null();
  if (m_config.dbKey && m_config.dbKey.mutable_())
    ZuClear(m_config.dbKey.data(), m_config.dbKey.length());
}

void Daemon::listening(int, unsigned) { }
void Daemon::listenFailed(int, bool) { }

} // namespace Zum
