//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zumd_daemon.hh"
#include <zlib/zumd_db.hh>
#include <zlib/zumd_key_db.hh>

#include <zlib/zumd_admin.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZtlsHMAC.hh>
#include <zlib/ZtlsMD.hh>

namespace Zum {

// Catalog limits bound validation work and the size of the published
// manifest independently from the transport JSON ceiling.
namespace CatalogLimit {
  enum {
    Actions = 1024,
    Roles = 256,
    Clients = 256,
    RoleActions = 1024,
    ClientRedirects = 256,
    ClientRoles = 256,
    TotalItems = 4096
  };
}

struct CatalogName {
  String name;
  unsigned index = 0;
  CatalogName(String name_, unsigned index_) :
    name{ZuMv(name_)}, index{index_} { }
};
static const String &catalogName(const CatalogName &item)
{
  return item.name;
}
ZmHashDerive(CatalogNameHash, CatalogName,
  (ZmHashNode<CatalogName,
    ZmHashKey<catalogName,
      ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.Catalog.Lookup">>>>));

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

struct AdminError {
  String error;
  String message;
  String correlationID;
};
ZfStruct(, (AdminError, JSON),
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
struct AdminIdem {
  String operationID;
  String status;
  StringVec resultIDs;
};
ZfStruct(, (AdminIdem, JSON),
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
  if (roots.length() != 1) return false;
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
  String clientType;
  StringVec redirectURIs;
  String audience;
};
ZfStruct(, (AppInput, JSON),
  (((name),		(Required)),	(String)),
  (((label),		(JSON::Opt)),	(String)),
  (((clientType),	(JSON::Opt)),	(String)),
  (((redirectURIs),	(JSON::Opt)),	(StringVec)),
  (((audience),	(JSON::Opt)),	(String)));

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

struct RotateInput {
  uint32_t overlapSeconds = 0;
};
ZfStruct(, (RotateInput, JSON),
  (((overlapSeconds),	(JSON::Opt)),	(UInt32, 0)));

struct ClientInput {
  String id;
  AppID appID = 0;
  String label;
  String profile;
  StringVec redirectURIs;
  uint8_t grants = 0;
  bool refreshAllowed = false;
  StringVec identityScopes;
};
ZfStruct(, (ClientInput, JSON),
  (((id),		(JSON::Opt)),	(String)),
  (((appID),		(Required, JSON::String<>)),	(UInt64)),
  (((label),		(JSON::Opt)),	(String)),
  (((profile),		(Required)),	(String)),
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
  IDVec roleIDs;
};
ZfStruct(, (ClientAccessInput, JSON),
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
  uint32_t limit = 0;
};
ZfStruct(, (ConsentSelector, JSON),
  (((userID),		(Required, JSON::String<>)),	(UInt64)),
  (((clientID),		(JSON::Opt)),	(String)),
  (((appID),		(JSON::Opt, JSON::String<>)),	(UInt64, 0)),
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
  AppID appID = 0;
  String id;
  String algorithm;
  String providerRef;
  String publicJwk;
  String privateMaterial;
  int64_t notBefore = 0;
};
ZfStruct(, (SignKeyInput, JSON),
  (((appID),		(JSON::String<>, Required)), (UInt64)),
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
  CatalogData catalog;
  uint64_t revision = 0;
  String digest;
};
ZfStruct(, (CatalogInput, JSON),
  (((catalog),		(Required)),	(UDT)),
  (((revision),		(Required, JSON::String<>)),	(UInt64)),
  (((digest),		(Required)),	(String)));

struct QueryInput {
  enum {
    ID, Name, Source, AppID_, UserID_, ClientID_, ProviderID_,
    ActorKind_, ActorID_, Value, KeyID, Issuer_, Subject, Operation,
    IdempotencyKey, Cursor, Limit, N
  };
  String id;
  String name;
  String source;
  String appID;
  String userID;
  String clientID;
  String providerID;
  String actorKind;
  String actorID;
  String value;
  String keyID;
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
    "clientID", "providerID", "actorKind", "actorID", "value",
    "keyID", "issuer", "subject", "operation", "idempotencyKey",
    "cursor", "limit">;
};
ZfStruct(, (QueryInput, URI),
  (((id),		(Mutable)),	(String)),
  (((name),		(Mutable)),	(String)),
  (((source),		(Mutable)),	(String)),
  (((appID),		(Mutable)),	(String)),
  (((userID),		(Mutable)),	(String)),
  (((clientID),		(Mutable)),	(String)),
  (((providerID),	(Mutable)),	(String)),
  (((actorKind),	(Mutable)),	(String)),
  (((actorID),		(Mutable)),	(String)),
  (((value),		(Mutable)),	(String)),
  (((keyID),		(Mutable)),	(String)),
  (((issuer),		(Mutable)),	(String)),
  (((subject),		(Mutable)),	(String)),
  (((operation),	(Mutable)),	(String)),
  (((idempotencyKey),	(Mutable)),	(String)),
  (((cursor),		(Mutable)),	(String)),
  (((limit),		(Mutable)),	(UInt32, 100)));

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
  auto handler = ZfURI::handler<QueryInput, ZuFacet::URI>(parsed.p<1>());
  if (!handler.valid) return false;
  handler.update(input);
  constexpr uint32_t exact = (1U<<QueryInput::Cursor) - 1;
  return input.limit && input.limit <= AdminQueryLimit::Results &&
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
  if (!body || body.length() > ServerLimitMax::JSON) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<T, ZuFacet::JSON>(roots[0].ptr());
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

template <typename Fields, typename T>
static bool adminBodyFields(String &body, T &value, uint64_t &seen)
{
  if (!body || body.length() > ServerLimitMax::JSON) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
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
  auto handler = ZfJSON::handler<T, ZuFacet::JSON>(roots[0].ptr());
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

static bool stringPatchBody(String &body,
    ZuCSpan first, String &firstValue, ZuCSpan second,
    String &secondValue, unsigned &seen)
{
  if (!body || body.length() > ServerLimitMax::JSON) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
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

static ClientProfile::T clientProfile(ZuCSpan value)
{
  if (value == "browser") return ClientProfile::Browser;
  if (value == "native") return ClientProfile::Native;
  if (value == "server") return ClientProfile::Server;
  return ClientProfile::T(-1);
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

template <typename Edit, typename Heap = ZuVoid>
class AppEdit__ : public Heap, public ZmPolymorph  {
public:
  enum { AppFirst = ZuIsSame<Edit, RoleEdit>{} ||
    ZuIsSame<Edit, ActionEdit>{},
    Creation = ZuIsSame<Edit, ProviderAdd>{} || ZuIsSame<Edit, RoleAdd>{} ||
      ZuIsSame<Edit, UserInvite>{} || ZuIsSame<Edit, ClientAdd>{} || ZuIsSame<Edit, KeyAdd>{} };
  using Snapshot = ZuIf<AppFirst, App, ZuDecay<decltype(ZuDeclVal<Edit>().before)>>;

  AppEdit__(DB *db, DBContext *context, Ztls::Random *rng,
      Edit change, AdminDoneFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_change{ZuMv(change)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    source_()->run(0, [self = ZmRef<AppEdit__>{this}]() mutable {
      ZuStructKeyT<Snapshot, 0> key{ZuStructKey<0>(self->snapshot_())};
      self->source_()->template find<0>(0, ZuMv(key),
	[self = ZuMv(self)](ZdbRowRef<Snapshot> row) mutable {
	  if (row) self->snapshot_() = row->data();
	  else self->snapshot_().version = 0;
	  if constexpr (AppFirst) self->record_();
	  else if constexpr (ZuIsSame<Edit, UserInvite>{}) self->external_();
	  else if constexpr (ZuIsSame<Edit, ClientAdd>{} ||
	      ZuIsSame<Edit, RoleAdd>{} ||
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
      [self = ZmRef<AppEdit__>{this}](ZdbRowRef<User> row) mutable {
        if (row) self->m_change.external = row->data();
        self->submit_();
      });
  }

  auto source_() const
  {
    if constexpr (ZuIsSame<Edit, UserEdit>{} || ZuIsSame<Edit, UserInvite>{})
      return m_context->users;
    else if constexpr (ZuIsSame<Edit, CredEdit>{}) return m_context->creds;
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
    else return m_context->actions;
  }

  void owner_()
  {
    m_change.app.id = m_change.before.appID;
    auto apps = m_context->apps;
    apps->run(0, [self = ZmRef<AppEdit__>{this}, apps]() mutable {
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
    table_()->run(0, [self = ZmRef<AppEdit__>{this}]() mutable {
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
	[self = ZmRef<AppEdit__>{this}](bool ok) mutable {
	  if (!ok) self->finish_(503);
	}, [self = ZmRef<AppEdit__>{this}](bool ok) mutable {
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
template <typename Edit>
ZuDerive(AppEdit_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.AppEdit", AppEdit__<Edit>>));
template <typename Edit>
ZuDerive(AppEdit_, (AppEdit__<Edit, AppEdit_Heap<Edit>>));

template <typename Heap = ZuVoid>
class MembershipAdd__ : public Heap, public ZmPolymorph  {
public:
  MembershipAdd__(DB *db, Ztls::Random *rng, MembershipAdd add,
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
	[self = ZmRef<MembershipAdd__>{this}](bool ok) mutable {
	  if (!ok) self->finish_(503);
	}, [self = ZmRef<MembershipAdd__>{this}](bool ok) mutable {
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
ZuDerive(MembershipAdd_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.MembershipAdd", MembershipAdd__<>>));
ZuDerive(MembershipAdd_, (MembershipAdd__<MembershipAdd_Heap>));

template <typename Heap = ZuVoid>
class BulkRevoke__ : public Heap, public ZmPolymorph  {
public:
  enum { Sessions, Consents, Grants, Cleanup };

  struct RefreshNotice {
	AppID		appID = 0;
	RefreshID	id;
	int64_t		expires = 0;
  };
  ZuDerive(RefreshNoticeVec, (ZtArray<RefreshNotice,
    ZtArrayHeapID<"Zum.Admin.RefreshNotice">>));

  BulkRevoke__(DB *db, DBContext *context, Ztls::Random *rng, int kind, UserID userID,
      String clientID, AppID appID,
      uint32_t limit, IdemRequest request, AdminDoneFn complete,
      RefreshRevokeFn event = {}, MaintenanceFn maintenance = {}) :
    m_db{db}, m_context{context}, m_rng{rng},
    m_kind{kind}, m_userID{userID}, m_clientID{ZuMv(clientID)},
    m_appID{appID}, m_limit{limit},
    m_complete{ZuMv(complete)}, m_event{ZuMv(event)},
    m_maintenance{ZuMv(maintenance)} {
    m_change.request = ZuMv(request);
  }

  void start()
  {
    if (!m_context || !m_limit || m_limit > AdminQueryLimit::Results) {
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
    table->run(0, [self = ZmRef<BulkRevoke__>{this}, table, id = ZuMv(id)]() mutable {
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
    if (m_maintenance) {
      bool ok = result.status == 200;
      auto maintenance = ZuMv(m_maintenance);
      maintenance(MaintenanceResult{.selected = m_removed,
	.deleted = ok ? m_removed : 0, .skipped = m_skipped,
	.conflicted = result.status == 409 ? 1U : 0U,
	.failed = ok ? 0U : 1U, .ok = ok});
    }
    auto complete = ZuMv(m_complete);
    m_saga = nullptr;
    complete(ZuMv(result));
  }

  void success_()
  {
    for (auto &notice: m_refreshNotices)
      if (m_event) m_event(notice.appID, notice.id, notice.expires);
    String json = m_kind == Cleanup ?
      adminJSON(AdminRemovedReply{{m_removed}}) :
      adminJSON(AdminRevokedReply{{m_removed}});
    finish_(AdminResult{ZuMv(json), 200});
  }

  template <unsigned KeyID, typename Table, typename Images, typename Match,
      typename Image>
  void scan_(Table *table, Images *images,
      typename Zdb_::SplitKey<typename Table::T, KeyID>::GroupKey group, Match match,
      Image image, typename Table::template Key<KeyID> key = {}, bool next = false)
  {
    table->run(0, [self = ZmRef<BulkRevoke__>{this}, table, images,
	group = ZuMv(group), match = ZuMv(match), image = ZuMv(image),
	key = ZuMv(key), next]() mutable {
      using Tuple = typename Table::Tuple;
      auto receive = [self, table, images, group, match = ZuMv(match),
	  image = ZuMv(image), last = key, raw = unsigned{0}, stop = false](
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
	  if (tuple.template p<owner>()) { ++self->m_skipped; return; }
	  if (self->m_kind != Cleanup &&
	      (tuple.template p<state>() == State::Revoked ||
	       tuple.template p<state>() == State::Consumed)) return;
	  images->push(image(tuple));
	  return;
	}
	if (!stop && images->length() < self->m_limit &&
	    raw == self->m_limit) {
	  self->template scan_<KeyID>(table, images, ZuMv(group),
	    ZuMv(match), ZuMv(image), ZuMv(last), true);
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
      }, [](const auto &item) { return SagaImage::save(item); });
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
	  (!m_appID || tuple.template p<2>() == m_appID);
      }, [](const auto &item) { return SagaImage::save(item); });
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
	}, [](const auto &item) { return SagaImage::save(item); });
    } else {
      scan_<3>(m_context->grants, &m_change.grants, ZuFwdTuple(m_appID),
	[appID = m_appID](const auto &tuple) -> int {
	  return tuple.template p<3>() == appID ? 1 : -1;
	}, [](const auto &item) { return SagaImage::save(item); });
    }
  }

  void cleanupSelect_()
  {
    m_change.updated = Zm::now().sec();
    scan_<1>(m_context->grants, &m_cleanupGrants, {},
      [updated = m_change.updated](const auto &tuple) -> int {
	constexpr unsigned expires =
	  ZuTypeIndex<ZuStringT<"expires">, ZuFieldIDs<Grant>>{};
	const auto state = tuple.template p<
	  ZuTypeIndex<ZuStringT<"state">, ZuFieldIDs<Grant>>{}>();
	return state != State::Pending && tuple.template p<expires>() <= updated ?
	  1 : -1;
      }, [](const auto &item) { return grantDelete(item); });
  }

  void submit_()
  {
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) {
      failed_(503); return;
    }
    m_saga = new MSaga{};
    if (m_kind == Cleanup) {
      m_removed = m_cleanupGrants.length();
      m_saga->init(GrantCleanup{.grants = ZuMv(m_cleanupGrants),
	.updated = m_change.updated, .request = ZuMv(m_change.request)});
    } else {
      if (!m_removed) m_removed = m_change.count();
      m_change.updated = Zm::now().sec();
      m_saga->init(ZuMv(m_change));
    }
    if (!sagaSubmit(m_db, id, m_saga,
	[self = ZmRef<BulkRevoke__>{this}](bool ok) mutable {
	  if (!ok) self->failed_(503);
	}, [self = ZmRef<BulkRevoke__>{this}](bool ok) mutable {
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
  uint32_t	m_limit = 0;
  AdminDoneFn	m_complete;
  RefreshRevokeFn	m_event;
  MaintenanceFn		m_maintenance;
  RefreshNoticeVec	m_refreshNotices;
  Revoke	m_change;
  GrantDeleteVec m_cleanupGrants;
  ZmRef<MSaga>	m_saga;
  unsigned	m_removed = 0;
  unsigned	m_skipped = 0;
  bool		m_done = false;
};
ZuDerive(BulkRevoke_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.BulkRevoke", BulkRevoke__<>>));
ZuDerive(BulkRevoke_, (BulkRevoke__<BulkRevoke_Heap>));

static String encode(ZuBSpan data)
{
  String value;
  value.length(ZuBase64URL::enclen(data.length()));
  value.length(ZuBase64URL::encode(value.span(), data));
  return value;
}

void daemonGrantCleanup(DB *db, DBContext *context, Ztls::Random *rng,
    unsigned limit, MaintenanceFn complete)
{
  if (!db || !context || !rng || !limit || limit > DaemonCleanupLimit::Rows) {
    complete(MaintenanceResult{.failed = 1}); return;
  }
  ZmRef<BulkRevoke_> cleanup = new BulkRevoke_{db, context, rng,
    BulkRevoke_::Cleanup, 0, {}, 0, limit, IdemRequest{},
    [](AdminResult) {}, {}, ZuMv(complete)};
  cleanup->start();
}

class ExpiredCleanup_ : public ZmPolymorph {
public:
  enum { Refreshes, Sessions };

  ExpiredCleanup_(DB *db, DBContext *context, Ztls::Random *rng,
      int kind, unsigned limit, MaintenanceFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_kind{kind}, m_limit{limit},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_limit ||
        m_limit > DaemonCleanupLimit::Rows) { finish_(false); return; }
    if (m_kind == Refreshes) refresh_();
    else session_();
  }

private:
  template <unsigned KeyID, typename Record, typename Table, typename Images,
      typename Match, typename Image>
  void scan_(Table *table, Images *images, Match match, Image image,
      typename Table::template Key<KeyID> key = {}, bool next = false)
  {
    table->run(0, [self = ZmRef<ExpiredCleanup_>{this}, table, images,
        match = ZuMv(match), image = ZuMv(image), key = ZuMv(key), next]() mutable {
      using Tuple = typename Table::Tuple;
      auto receive = [self, table, images, match = ZuMv(match), image = ZuMv(image),
          last = key, raw = unsigned{0}](ZuUnion<void, Tuple> result,
          unsigned count) mutable {
        if (result.template is<Tuple>()) {
          raw = count;
          if (images->length() >= self->m_limit) return;
          auto tuple = ZuMv(result).template p<Tuple>();
          last = ZuStructKey<KeyID>(tuple);
          constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
            ZuFieldIDs<Record>>{};
          if (tuple.template p<owner>()) { ++self->m_skipped; return; }
          if (match(tuple)) images->push(image(tuple));
          return;
        }
        if (images->length() < self->m_limit && raw == self->m_limit) {
          self->template scan_<KeyID, Record>(table, images, ZuMv(match),
            ZuMv(image), ZuMv(last), true);
          return;
        }
        self->submit_<Record>();
      };
      if (next)
        table->template nextRows<KeyID>(ZuMv(key), false, self->m_limit,
          ZuMv(receive));
      else
        table->template selectRows<KeyID>({}, self->m_limit, ZuMv(receive));
    });
  }

  void refresh_()
  {
    scan_<1, Refresh>(m_context->refresh, &m_refreshes,
      [updated = int64_t(Zm::now().sec())](const auto &tuple) {
        constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
          ZuFieldIDs<Refresh>>{};
        constexpr unsigned expires = ZuTypeIndex<ZuStringT<"expires">,
          ZuFieldIDs<Refresh>>{};
        return !tuple.template p<owner>() &&
          tuple.template p<expires>() <= updated;
      }, [](const auto &item) { return refreshDelete(item); });
  }

  void session_()
  {
    scan_<2, Session>(m_context->sessions, &m_sessions,
      [updated = int64_t(Zm::now().sec())](const auto &tuple) {
        constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
          ZuFieldIDs<Session>>{};
        constexpr unsigned idle = ZuTypeIndex<ZuStringT<"idleDeadline">,
          ZuFieldIDs<Session>>{};
        constexpr unsigned absolute = ZuTypeIndex<
          ZuStringT<"absoluteDeadline">, ZuFieldIDs<Session>>{};
        return !tuple.template p<owner>() &&
          (tuple.template p<idle>() <= updated ||
           tuple.template p<absolute>() <= updated);
      }, [](const auto &item) { return sessionDelete(item); });
  }

  template <typename Record>
  void submit_()
  {
    unsigned selected = 0;
    if constexpr (ZuIsSame<Record, Refresh>{}) selected = m_refreshes.length();
    else selected = m_sessions.length();
    if (!selected) { finish_(true); return; }
    m_selected = selected;
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) { finish_(false); return; }
    m_saga = new MSaga{};
    int64_t updated = Zm::now().sec();
    if constexpr (ZuIsSame<Record, Refresh>{})
      m_saga->init(RefreshCleanup{.refreshes = ZuMv(m_refreshes), .updated = updated});
    else
      m_saga->init(SessionCleanup{.sessions = ZuMv(m_sessions), .updated = updated});
    if (!sagaSubmit(m_db, id, m_saga,
        [self = ZmRef<ExpiredCleanup_>{this}](bool ok) mutable {
          if (!ok) self->finish_(false);
        }, [self = ZmRef<ExpiredCleanup_>{this}](bool ok) mutable {
          self->finish_(ok);
        })) finish_(false);
  }

  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(MaintenanceResult{.selected = m_selected,
      .deleted = ok ? m_selected : 0, .skipped = m_skipped,
      .failed = ok ? 0U : 1U, .ok = ok});
  }

  DB *m_db;
  DBContext *m_context;
  Ztls::Random *m_rng;
  int m_kind;
  unsigned m_limit;
  MaintenanceFn m_complete;
  RefreshDeleteVec m_refreshes;
  SessionDeleteVec m_sessions;
  ZmRef<MSaga> m_saga;
  bool m_done = false;
  unsigned m_selected = 0;
  unsigned m_skipped = 0;
};

void daemonRefreshCleanup(DB *db, DBContext *context, Ztls::Random *rng,
    unsigned limit, MaintenanceFn complete)
{
  ZmRef<ExpiredCleanup_> cleanup = new ExpiredCleanup_{db, context, rng,
    ExpiredCleanup_::Refreshes, limit, ZuMv(complete)};
  cleanup->start();
}

void daemonSessionCleanup(DB *db, DBContext *context, Ztls::Random *rng,
    unsigned limit, MaintenanceFn complete)
{
  ZmRef<ExpiredCleanup_> cleanup = new ExpiredCleanup_{db, context, rng,
    ExpiredCleanup_::Sessions, limit, ZuMv(complete)};
  cleanup->start();
}

class AppCleanup_ : public ZmPolymorph {
public:
  AppCleanup_(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
      unsigned limit, MaintenanceFn complete) :
    m_db{db}, m_context{context}, m_rng{rng}, m_appID{appID}, m_limit{limit},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_db || !m_context || !m_rng || !m_appID || !m_limit ||
        m_limit > DaemonCleanupLimit::Rows) { finish_(false); return; }
    if (!m_evidenceDone) evidence_();
    else consent_();
  }

private:
  template <unsigned KeyID, typename Table, typename Images, typename Match,
      typename Image>
  void scan_(Table *table, Images *images, Match match, Image image,
      unsigned limit, typename Table::template Key<KeyID> key = {},
      bool next = false)
  {
    table->run(0, [self = ZmRef<AppCleanup_>{this}, table, images,
        match = ZuMv(match), image = ZuMv(image), limit, key = ZuMv(key), next]() mutable {
      using Tuple = typename Table::Tuple;
      auto receive = [self, table, images, match = ZuMv(match), image = ZuMv(image),
          last = key, limit, raw = unsigned{0}](ZuUnion<void, Tuple> result,
          unsigned count) mutable {
        if (result.template is<Tuple>()) {
          raw = count;
          if (images->length() >= limit) return;
          auto tuple = ZuMv(result).template p<Tuple>();
          last = ZuStructKey<KeyID>(tuple);
          using Record = typename Table::T;
          constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
            ZuFieldIDs<Record>>{};
          if (tuple.template p<owner>()) { ++self->m_skipped; return; }
          if (match(tuple)) images->push(image(tuple));
          return;
        }
        if (images->length() < limit && raw == limit) {
          self->template scan_<KeyID>(table, images, ZuMv(match), ZuMv(image),
            limit, ZuMv(last), true);
          return;
        }
        self->submit_();
      };
      if (next)
        table->template nextRows<KeyID>(ZuMv(key), false, limit, ZuMv(receive));
      else
        table->template selectRows<KeyID>(ZuFwdTuple(self->m_appID), limit,
          ZuMv(receive));
    });
  }

  void evidence_()
  {
    scan_<2>(m_context->evidence, &m_evidence,
      [appID = m_appID](const auto &tuple) {
        constexpr unsigned app = ZuTypeIndex<ZuStringT<"appID">,
          ZuFieldIDs<Evidence>>{};
        constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
          ZuFieldIDs<Evidence>>{};
        return tuple.template p<app>() == appID && !tuple.template p<owner>();
      }, [](const auto &item) { return evidenceDelete(item); }, m_limit);
  }

  void consent_()
  {
    unsigned limit = m_limit - m_evidence.length();
    if (!limit) { submit_(); return; }
    scan_<2>(m_context->consents, &m_consents,
      [appID = m_appID](const auto &tuple) {
        constexpr unsigned app = ZuTypeIndex<ZuStringT<"appID">,
          ZuFieldIDs<Consent>>{};
        constexpr unsigned owner = ZuTypeIndex<ZuStringT<"owner">,
          ZuFieldIDs<Consent>>{};
        return tuple.template p<app>() == appID && !tuple.template p<owner>();
      }, [](const auto &item) { return consentDelete(item); }, limit);
  }

  void submit_()
  {
    if (!m_evidence.length() && !m_consents.length()) {
      if (!m_evidenceDone) { m_evidenceDone = true; start(); }
      else finish_(true);
      return;
    }
    ZdbSagaID id;
    if (!randomID(*m_rng, id)) { finish_(false); return; }
    m_selected += m_evidence.length() + m_consents.length();
    m_saga = new MSaga{};
    m_saga->init(AppCleanup{.appID = m_appID,
      .evidence = ZuMv(m_evidence), .consents = ZuMv(m_consents),
      .updated = Zm::now().sec()});
    if (!sagaSubmit(m_db, id, m_saga, SagaFn{},
        [self = ZmRef<AppCleanup_>{this}](bool ok) mutable {
          if (!ok) self->finish_(false);
          else self->start();
        })) finish_(false);
  }

  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(MaintenanceResult{.selected = m_selected,
      .deleted = ok ? m_selected : 0, .skipped = m_skipped,
      .failed = ok ? 0U : 1U, .ok = ok});
  }

  DB *m_db;
  DBContext *m_context;
  Ztls::Random *m_rng;
  AppID m_appID;
  unsigned m_limit;
  MaintenanceFn m_complete;
  EvidenceDeleteVec m_evidence;
  ConsentDeleteVec m_consents;
  ZmRef<MSaga> m_saga;
  bool m_evidenceDone = false;
  bool m_done = false;
  unsigned m_selected = 0;
  unsigned m_skipped = 0;
};

void daemonAppCleanup(DB *db, DBContext *context, Ztls::Random *rng,
    AppID appID, unsigned limit, MaintenanceFn complete)
{
  ZmRef<AppCleanup_> cleanup = new AppCleanup_{db, context, rng, appID,
    limit, ZuMv(complete)};
  cleanup->start();
}

template <typename Heap = ZuVoid>
class MembershipChange__ : public Heap, public ZmPolymorph  {
public:
  MembershipChange__(DB *db, DBContext *context, Ztls::Random *rng,
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
    apps->run(0, [self = ZmRef<MembershipChange__>{this}, apps]() mutable {
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
    members->run(0, [self = ZmRef<MembershipChange__>{this}, members]() mutable {
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
	SagaFn{ZmRef<MembershipChange__>{this},
	  ZmFnPtr<&MembershipChange__::submitted_>{}},
	SagaFn{ZmRef<MembershipChange__>{this},
	  ZmFnPtr<&MembershipChange__::completed_>{}})) submitted_(false);
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
ZuDerive(MembershipChange_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.MembershipChange", MembershipChange__<>>));
ZuDerive(MembershipChange_, (MembershipChange__<MembershipChange_Heap>));

template <typename Heap = ZuVoid>
class ActionAdd__ : public Heap, public ZmPolymorph  {
public:
  ActionAdd__(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
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
    apps->run(0, [self = ZmRef<ActionAdd__>{this}, apps]() mutable {
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
	SagaFn{ZmRef<ActionAdd__>{this}, ZmFnPtr<&ActionAdd__::submitted_>{}},
	SagaFn{ZmRef<ActionAdd__>{this}, ZmFnPtr<&ActionAdd__::completed_>{}}))
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
ZuDerive(ActionAdd_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.ActionAdd", ActionAdd__<>>));
ZuDerive(ActionAdd_, (ActionAdd__<ActionAdd_Heap>));

template <typename Heap = ZuVoid>
class AppEnroll__ : public Heap, public ZmPolymorph  {
public:
  AppEnroll__(DB *db, DBContext *context, Ztls::Random *rng, Bytes dbKey,
      Issuer issuer,
      AppInput input, IdemRequest request, AdminDoneFn complete) :
    m_db{db}, m_context{context},
    m_rng{rng}, m_dbKey{ZuMv(dbKey)}, m_issuer{ZuMv(issuer)}, m_input{ZuMv(input)},
    m_request{ZuMv(request)},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    ZuBArray<ClientCredentialEntropySize> random(
      ClientCredentialEntropySize, false);
	if (!m_context || !m_rng || !m_issuer.coreAppID || !m_input.name ||
	    !m_input.audience || !randomID(*m_rng, m_appID) ||
	    !m_rng->random(random)) {
      finish_(400, "invalid application enrollment");
      return;
    }
    m_clientID = m_input.name;
	m_secret = encode({random.data() + ClientIDEntropySize,
	    ClientSecretEntropySize});
	m_secretDigest.length(Ztls::SecretHash::Size, false);
	if (!Ztls::secretHash(*m_rng, ZuBSpan{m_secret}, m_secretDigest)) {
	finish_(503, "secret generation failed");
	return;
    }
    int64_t now = Zm::now().sec();
    SignKey signKey;
    String keyID;
    keyID << "app_" << m_appID << "_1";
    if (!appIssuer(m_issuer.id, m_appID, m_appIssuer) ||
	!signKeyCreate(*m_rng, m_dbKey, m_appIssuer, keyID, now, signKey)) {
      finish_(503, "application signing-key generation failed");
      return;
    }
    if (!m_input.label) m_input.label = m_input.name;
    ZdbSagaID sagaID;
    if (!m_db || !randomID(*m_rng, sagaID)) {
      finish_(503, "saga ID generation failed");
      return;
    }
    AppEnrollment enrollment{.coreAppID = m_issuer.coreAppID,
      .appID = m_appID, .appName = ZuMv(m_input.name),
      .appLabel = ZuMv(m_input.label),
      .audience = ZuMv(m_input.audience), .signKey = ZuMv(signKey),
      .clientID = m_clientID,
	      .secretDigest = ZuMv(m_secretDigest), .created = now,
      .catalogPublishOp = MgmtOp::catalogPublish,
      .operationQueryOp = MgmtOp::operationQuery,
      .request = ZuMv(m_request)};
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(enrollment));
    if (!sagaSubmit(m_db, sagaID, ZuMv(saga),
        [self = ZmRef<AppEnroll__>{this}](bool ok) mutable {
          if (!ok) self->finish_(503, "application enrollment failed");
        }, [self = ZmRef<AppEnroll__>{this}](bool ok) mutable {
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
	ZuClear(m_secret);
      String body = adminJSON(AdminError{
        status == 409 ? "conflict" :
          status == 400 ? "invalid_request" : "unavailable",
        message, {}});
      complete(AdminResult{ZuMv(body), status});
      return;
    }
    String body = adminJSON(AdminEnrollReply{{m_appID, m_clientID,
      m_secret, m_appIssuer, rowETag(2)}});
    ZuClear(m_secret);
    m_secret.null();
    StringVec ids;
    String id;
    id << m_appID;
    ids.push(ZuMv(id));
    ids.push(m_clientID);
    complete(AdminResult{ZuMv(body), 201, ZuMv(ids)});
  }

  DB		*m_db = nullptr;
  DBContext	*m_context = nullptr;
  Ztls::Random	*m_rng = nullptr;
  Bytes		m_dbKey;
  Issuer	m_issuer;
  AppInput	m_input;
  IdemRequest	m_request;
  AdminDoneFn	m_complete;
  AppID		m_appID = 0;
  String	m_appIssuer;
  String	m_clientID;
  String	m_secret;
  Bytes		m_secretDigest;
  bool		m_done = false;
};
ZuDerive(AppEnroll_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.AppEnroll", AppEnroll__<>>));
ZuDerive(AppEnroll_, (AppEnroll__<AppEnroll_Heap>));

template <typename Table, typename Present, typename Heap = ZuVoid>
class CatalogRetire__ : public Heap, public ZmObject  {
  using T = typename Table::T;
  using Tuple = typename Table::Tuple;
  using Key = typename Table::template Key<0>;
public:
  CatalogRetire__(Table *table, AppID appID, uint64_t revision, int64_t updated,
      CatalogRows *rows, Present present, SagaFn complete) :
    m_table{table}, m_appID{appID}, m_revision{revision}, m_updated{updated},
    m_rows{rows}, m_present{ZuMv(present)}, m_complete{ZuMv(complete)},
    m_key{ZuFwdTuple(appID, 0)} { }
  void start(bool first = true)
  {
    m_count = 0;
    m_table->run(0, [self = ZmRef<CatalogRetire__>{this}, first]() mutable {
      self->m_table->template nextRows<0>(self->m_key, first, PageSize,
	[self = ZuMv(self)](ZuUnion<void, Tuple> result, unsigned count) mutable {
	  self->receive_(ZuMv(result), count);
	});
    });
  }
private:
  enum { PageSize = AdminQueryLimit::Results };
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
        if (before.tombstone) return;
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
template <typename Table, typename Present>
ZuDerive(CatalogRetire_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.CatalogRetire",
    CatalogRetire__<Table, Present>>));
template <typename Table, typename Present>
ZuDerive(CatalogRetire_, (CatalogRetire__<Table, Present,
  CatalogRetire_Heap<Table, Present>>));

template <typename Heap = ZuVoid>
class CatalogPublish__ : public Heap, public ZmPolymorph  {
public:
  CatalogPublish__(DB *db, DBContext *context, Ztls::Random *rng, AppID appID,
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
    apps->run(0, [self = ZmRef<CatalogPublish__>{this}, apps]() mutable {
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

  bool validate_()
  {
    unsigned actionCount = m_input.catalog.actions.length();
    unsigned roleCount = m_input.catalog.roles.length();
    unsigned clientCount = m_input.catalog.clients.length();
    if (!actionCount || actionCount > CatalogLimit::Actions ||
        roleCount > CatalogLimit::Roles || clientCount > CatalogLimit::Clients)
      return false;
    uint64_t total = uint64_t(actionCount) + roleCount + clientCount;
    if (total > CatalogLimit::TotalItems) return false;
    for (unsigned i = 0; i < actionCount; ++i) {
      const auto &action = m_input.catalog.actions[i];
      if (!action.name) return false;
      if (m_actionNames.find(action.name)) return false;
      m_actionNames.add(CatalogName{action.name, i});
    }
    for (unsigned i = 0; i < roleCount; ++i) {
      const auto &role = m_input.catalog.roles[i];
      if (!role.name) return false;
      if (m_roleNames.find(role.name)) return false;
      m_roleNames.add(CatalogName{role.name, i});
      unsigned roleActionCount = role.actions.length();
      if (roleActionCount > CatalogLimit::RoleActions) return false;
      ZtBitmap seen;
      seen.length(actionCount);
      for (unsigned a = 0; a < roleActionCount; ++a) {
	const auto &name = role.actions[a];
	int action = actionIndex_(name);
	if (action < 0 || seen[action]) return false;
	seen.set(action);
      }
    }
    unsigned clientIndex = 0;
    for (const auto &client: m_input.catalog.clients) {
      if (!client.id || client.redirectURIs.length() > CatalogLimit::ClientRedirects ||
          client.roles.length() > CatalogLimit::ClientRoles) return false;
      if (m_clientNames.find(client.id)) return false;
      m_clientNames.add(CatalogName{client.id, clientIndex});
      ZtBitmap seen;
      seen.length(roleCount);
      for (const auto &name: client.roles) {
	int role = roleIndex_(name);
	if (role < 0 || seen[role]) return false;
	seen.set(role);
      }
      total += client.redirectURIs.length() + client.roles.length();
      if (total > CatalogLimit::TotalItems) return false;
      ++clientIndex;
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

  int actionIndex_(const String &name) const
  {
    auto item = m_actionNames.find(name);
    return item ? int(item->index) : -1;
  }

  int roleIndex_(const String &name) const
  {
    auto item = m_roleNames.find(name);
    return item ? int(item->index) : -1;
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
    table->run(0, [self = ZmRef<CatalogPublish__>{this}, table, index,
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
      client_();
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
    table->run(0, [self = ZmRef<CatalogPublish__>{this}, table, index,
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

  void client_()
  {
    if (m_offset >= m_input.catalog.clients.length()) {
      m_offset = 0;
      retire_();
      return;
    }
    unsigned index = m_offset++;
    const auto &input = m_input.catalog.clients[index];
    ClientProfile::T profile = clientProfile(input.profile);
    if (!input.id || profile < 0 || profile == ClientProfile::Server ||
        !clientConfigValid(profile, input.grants, input.refreshAllowed,
          input.redirectURIs)) {
      fail_(400, "invalid_request", "invalid catalog client");
      return;
    }
    auto table = m_context->clients;
    String id = input.id;
    table->run(0, [self = ZmRef<CatalogPublish__>{this}, table, index,
	 id = ZuMv(id), profile]() mutable {
      table->find<0>(0, ZuFwdTuple(ZuMv(id)), [self = ZuMv(self), index,
		 profile](ZdbRowRef<Client> row) mutable {
	const auto &input = self->m_input.catalog.clients[index];
	Client next;
	if (row) {
	  if (row->data().appID != self->m_appID || row->data().owner) {
	    self->fail_(409, "conflict", "catalog client belongs to another application");
	    return;
	  }
	  if (row->data().profile != profile) {
	    self->fail_(409, "conflict", "catalog client profile is immutable");
	    return;
	  }
	  next = row->data();
	  if (self->m_replay && next.state == State::Active) {
	    self->clientAccess_(index, next.id);
	    return;
	  }
	  next.label = input.label;
	  if (!next.label) next.label = input.id;
	  next.redirects = input.redirectURIs;
	  next.grants = input.grants;
	  next.refreshAllowed = input.refreshAllowed;
	  next.identityScopes = input.identityScopes;
	  next.profile = profile;
	  next.state = State::Active;
	  if (next.version == UINT64_MAX) {
	    self->fail_(409, "conflict", "catalog client version exhausted");
	    return;
	  }
	  ++next.version;
	  self->m_change.clients.change(row->data(), next);
	} else {
	  int64_t now = self->m_change.after.updated;
	  next = Client{.id = input.id, .appID = self->m_appID,
	    .label = input.label, .redirects = input.redirectURIs,
	    .created = now, .updated = now, .profile = profile,
	    .grants = input.grants,
	    .refreshAllowed = input.refreshAllowed,
	    .identityScopes = input.identityScopes, .state = State::Active,
	    .version = 1};
	  if (!next.label) next.label = next.id;
	  self->m_change.clients.add(next);
	}
	self->clientAccess_(index, next.id);
      });
    });
  }

  void clientAccess_(unsigned index, const String &clientID)
  {
    IDVec roleIDs;
    for (auto &name: m_input.catalog.clients[index].roles) {
      int role = roleIndex_(name);
      if (role < 0) {
	fail_(400, "invalid_request", "client role is unavailable");
	return;
      }
      roleIDs.push(m_roleIDs[role]);
    }
    auto table = m_context->clientAccess;
    table->run(0, [self = ZmRef<CatalogPublish__>{this}, table, index,
	 roleIDs = ZuMv(roleIDs), clientID = String{clientID}]() mutable {
      table->find<0>(0, ZuFwdTuple(ZuMv(clientID), self->m_appID),
	[self = ZuMv(self), index, roleIDs = ZuMv(roleIDs)](
	    ZdbRowRef<ClientAccess> row) mutable {
	  if (row) {
	    if (row->data().owner || row->data().version == UINT64_MAX) {
	      self->fail_(409, "conflict", "catalog client access is busy");
	      return;
	    }
	    bool same = row->data().state == State::Active &&
	      row->data().roleIDs.length() == roleIDs.length();
	    if (same)
	      for (unsigned i = 0; i < roleIDs.length(); ++i)
		if (row->data().roleIDs[i] != roleIDs[i]) { same = false; break; }
	    if (self->m_replay && same) {
	      self->client_();
	      return;
	    }
	    ClientAccess next = row->data();
	    next.roleIDs = ZuMv(roleIDs);
	    next.state = State::Active;
	    ++next.version;
	    ++next.authVersion;
	    next.updated = self->m_change.after.updated;
	    self->m_change.clientAccess.change(row->data(), next);
	  } else {
	    ClientAccess next{.clientID = self->m_input.catalog.clients[index].id,
	      .appID = self->m_appID, .roleIDs = ZuMv(roleIDs),
	      .state = State::Active, .version = 1,
	      .created = self->m_change.after.updated,
	      .updated = self->m_change.after.updated};
	    self->m_change.clientAccess.add(next);
	  }
	  self->client_();
	});
    });
  }

  template <typename Table, typename Present>
  void retire_(Table *table, CatalogRows &rows, Present present)
  {
    ZmRef<CatalogRetire_<Table, Present>> retire =
      new CatalogRetire_<Table, Present>{table, m_appID, m_input.revision,
	m_change.after.updated, &rows, ZuMv(present),
	[self = ZmRef<CatalogPublish__>{this}](bool ok) mutable {
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
      default: finish_(); break;
    }
  }

  void finish_()
  {
    if (m_replay && !m_change.actions.added && !m_change.actions.changed &&
	!m_change.roles.added && !m_change.roles.changed &&
	!m_change.clients.added && !m_change.clients.changed &&
	!m_change.clientAccess.added && !m_change.clientAccess.changed) {
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
	[self = ZmRef<CatalogPublish__>{this}](bool ok) mutable {
	  if (!ok) self->fail_(503, "unavailable", "catalog publication unavailable");
	}, [self = ZmRef<CatalogPublish__>{this}](bool ok) mutable {
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
  CatalogNameHash	m_actionNames;
  CatalogNameHash	m_roleNames;
  CatalogNameHash	m_clientNames;
  unsigned	m_offset = 0;
  unsigned	m_retire = 0;
  bool		m_done = false;
  bool		m_replay = false;
};
ZuDerive(CatalogPublish_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.CatalogPublish", CatalogPublish__<>>));
ZuDerive(CatalogPublish_, (CatalogPublish__<CatalogPublish_Heap>));

String Daemon::error_(ZuCSpan error, ZuCSpan message)
{
  return adminJSON(AdminError{error, message, {}});
}

String Daemon::correlation_()
{
  Bytes random;
  random.length(16, false);
  if (!m_rng.random(random)) return {};
  String id;
  id.length(ZuBase64URL::enclen(random.length()));
  id.length(ZuBase64URL::encode(id.span(), random));
  ZuClear(random);
  return id;
}

String Daemon::correlate_(String json, ZuCSpan id)
{
  if (!id) return json;
  ZuPtr<ZfJSON::AnyNode> owner;
  AdminJSON parsed;
  if (!adminJSONParse(json, owner, parsed) ||
      !owner->has<ZfJSON::AnyNode::Object>()) return json;
  auto handler = ZfJSON::handler<AdminError>(owner);
  if (!handler.valid) return json;
  auto error = handler.ctor();
  if (!error.error || !error.message || error.correlationID) return json;
  error.correlationID = id;
  return adminJSON(ZuMv(error));
}

void Daemon::adminAudit_(int op, String actor, AppID appID,
    String target, String correlationID, AdminResult result,
    AdminDoneFn complete)
{
  if (!managementAudited(op)) {
    complete(ZuMv(result));
    return;
  }
  Audit audit = managementAuditRecord(m_config.issuer, op, ZuMv(actor), appID,
    ZuMv(target), correlationID, result.status, Zm::now().sec());
  logEvent(ZuMv(audit));
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
  ZuClear(raw);
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
  ZuClear(raw);
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
  ZuClear(raw);
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
    ZuClear(raw);
    return false;
  }
  using Record = typename Table::T;
  auto fbo = ZfbStruct::verify<Record>(
    {raw.data() + headerLength, dataLength});
  if (!fbo) {
    ZuClear(raw);
    return false;
  }
  key = ZfbStruct::ctor<typename Table::template Key<KeyID>>(fbo);
  ZuClear(raw);
  return true;
}

template <typename Table, typename Match, typename Group,
  unsigned KeyID, typename Heap = ZuVoid>
class AdminQuery__ : public Heap, public ZmObject  {
  using Tuple = typename Table::Tuple;
  using Record = typename Table::T;
  using Key = typename Table::template Key<KeyID>;

public:
  AdminQuery__(Table *table, Match match, unsigned limit, String cursor,
      int operation, AppID appID, Bytes secret, AdminDoneFn complete,
      Group group = {}) :
    m_table{table}, m_match{ZuMv(match)}, m_limit{limit},
    m_cursor{ZuMv(cursor)}, m_operation{operation}, m_appID{appID},
    m_secret{ZuMv(secret)}, m_complete{ZuMv(complete)},
    m_group{ZuMv(group)} { }

  ~AdminQuery__()
  {
    if (m_secret.mutable_()) ZuClear(m_secret);
  }

  void start()
  {
    if (!m_table || !m_limit || !m_secret || !m_complete) {
      finish_(adminErrorResult(503, "unavailable", "query unavailable"));
      return;
    }
    m_table->run(0, [self = ZmRef<AdminQuery__>{this}]() mutable {
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
  enum { ScanSize = AdminQueryLimit::Scan };

  void scanFirst_()
  {
    m_rawCount = 0;
    auto receive = [self = ZmRef<AdminQuery__>{this}](
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
      [self = ZmRef<AdminQuery__>{this}](ZuUnion<void, Tuple> result,
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
template <typename Table, typename Match, typename Group = ZuTuple<>,
  unsigned KeyID = 0>
ZuDerive(AdminQuery_Heap,
  (ZmHeap<"Zum.zumd.daemon.admin.AdminQuery",
    AdminQuery__<Table, Match, Group, KeyID>>));
template <typename Table, typename Match, typename Group = ZuTuple<>,
  unsigned KeyID = 0>
ZuDerive(AdminQuery_, (AdminQuery__<Table, Match, Group, KeyID,
  AdminQuery_Heap<Table, Match, Group, KeyID>>));

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

template <unsigned KeyID = 0, typename Table>
static void adminQuery(Table *table, unsigned limit, String cursor,
    int operation, Bytes secret, AdminDoneFn complete)
{
  adminQueryIf<KeyID>(table, [](const auto &) { return true; }, limit,
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
  constexpr auto prefix = "/admin/apps/"_Zu;
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
  return AdminResult{adminJSON(AdminError{error, message, {}}), status};
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
  if (!body || body.length() > ServerLimitMax::JSON) return false;
  if (!body.mutable_()) body.length(body.length());
  auto parsed = ZfJSON::scan({body.data(), body.length()});
  if (parsed.p<0>() != int(body.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<StateInput>(roots[0]);
  if (!handler.valid) return false;
  auto input = handler.ctor();
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
	  principal.subject, ActionID(operation),
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
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	  queryInput.limit, ZuMv(queryInput.cursor), op, id,
	  Bytes{m_config.dbKey}, ZuMv(complete), ZuFwdTuple(UserID{id}));
	return;
      }
      adminQuery(m_context->creds, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::membershipQuery:
    case MgmtOp::actionQuery:
    case MgmtOp::roleQuery:
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
	    ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	    ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	    ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	case MgmtOp::clientAccessQuery:
	  if (queryInput.clientID) {
	    adminFind<0>(m_context->clientAccess,
	      ZuFwdTuple(ZuMv(queryInput.clientID), appID), ZuMv(complete));
	    return;
	  }
	  adminQueryIf<1>(m_context->clientAccess, [](const auto &) { return true; },
	    queryInput.limit, ZuMv(queryInput.cursor), op, appID,
	    Bytes{m_config.dbKey}, ZuMv(complete), ZuTuple<AppID>{appID}); return;
	case MgmtOp::adminAccessQuery:
	  adminQueryApp<2>(m_context->adminAccess, appID, queryInput.limit,
	    ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	    ZuMv(complete)); return;
	default:
	  adminQueryIf<1>(m_context->roleMaps, [](const auto &) { return true; },
	    queryInput.limit, ZuMv(queryInput.cursor), op, appID,
	    Bytes{m_config.dbKey}, ZuMv(complete), ZuTuple<AppID>{appID}); return;
      }
    }
    case MgmtOp::clientQuery:
      if (queryInput.id || queryInput.clientID) {
	String id = queryInput.id ? ZuMv(queryInput.id) :
	  ZuMv(queryInput.clientID);
	adminFind<0>(m_context->clients, ZuFwdTuple(ZuMv(id)),
	  ZuMv(complete));
	return;
      }
      adminQuery(m_context->clients, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::identityQuery:
      adminQuery(m_context->extIdentities, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::evidenceQuery:
      adminQuery<1>(m_context->evidence, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::sessionQuery:
      adminQuery(m_context->sessions, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::consentQuery:
      adminQuery(m_context->consents, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::grantQuery:
      adminQuery(m_context->grants, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
	ZuMv(complete)); return;
    case MgmtOp::signKeyQuery:
      adminQuery(m_context->signKeys, queryInput.limit,
	ZuMv(queryInput.cursor), op, Bytes{m_config.dbKey},
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
	    Bytes{m_config.dbKey}, issuer->data(), ZuMv(input), ZuMv(request),
	    ZuMv(complete)};
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
      String issuer;
      if (!appIssuer(m_config.issuer, m_config.bootstrap.coreAppID, issuer) ||
	  !opaqueIssue(m_rng, capability)) {
	complete(adminErrorResult(503, "unavailable", "enrollment capability failed"));
	return;
      }
      int64_t now = Zm::now().sec();
      Grant grant{.id = ZuMv(capability.id),
	.appID = m_config.bootstrap.coreAppID, .userID = id, .created = now,
	.expires = now + 86400, .kind = GrantKind::Capability,
	.purpose = GrantPurpose::Enrollment, .state = State::Active,
	.issuer = ZuMv(issuer), .digest = ZuMv(capability.digest),
	.userName = input.name, .label = "Zum passkey", .actor = "precreated"};
      ZmRef<AppEdit_<UserInvite>> invite = new AppEdit_<UserInvite>{
	m_db, m_context, &m_rng, UserInvite{.before = User{.id = id},
	  .values = User{.id = id, .name = ZuMv(input.name),
	    .profile = ZuMv(input.profile), .email = ZuMv(input.email)},
	  .grant = ZuMv(grant), .request = ZuMv(request)},
	[issuer = m_config.issuer, token = ZuMv(capability.token),
	    complete = ZuMv(complete)](AdminResult result) mutable {
	  if (result.status == 201) {
	    auto length = issuer.length();
	    if (length && issuer[length - 1] == '/') issuer.length(length - 1);
	    issuer << "/enroll?capability=" << token;
	    if (!adminAddItemString(result, "enrollmentURL", issuer))
	      result = adminErrorResult(503, "unavailable", "response encoding failed");
	    ZuClear(issuer);
	  }
	  ZuClear(token);
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
	  String issuer;
	  if (!appIssuer(m_config.issuer, m_config.bootstrap.coreAppID,
	      issuer)) {
	    complete(AdminResult{
	      error_("unavailable", "issuer is unavailable"), 503});
	    return;
	  }
	  bool started = recoveryIssue(m_requests, Zm::now() + ZuTime{10},
	    m_db, m_context, m_rng, RecoveryIssueConfig{
	      .issuer = ZuMv(issuer), .actor = ZuMv(actor),
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
	      ZuClear(capability);
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
	ZuClear(random);
	complete(AdminResult{
	  error_("unavailable", "secret generation failed"), 503});
	return;
      }
      String secret = encode(random);
      ZuClear(random);
      Bytes digest;
      digest.length(Ztls::SecretHash::Size, false);
      if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	ZuClear(secret);
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
	  ZuClear(secret);
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
      ClientProfile::T profile = clientProfile(input.profile);
      if (!clientConfigValid(profile, input.grants, input.refreshAllowed,
          input.redirectURIs)) {
	complete(adminErrorResult(400, "invalid_request",
	  "invalid client configuration"));
	return;
      }
      ZuBArray<ClientCredentialEntropySize> random(
	ClientCredentialEntropySize, false);
      if (!m_rng.random(random)) {
	ZuClear(random);
	complete(adminErrorResult(503, "unavailable",
	  "client credential generation failed"));
	return;
      }
      String clientID{"cli_"};
      clientID << encode({random.data(), ClientIDEntropySize});
      String secret;
      Bytes digest;
      if (clientType(profile) == ClientType::Confidential) {
	secret = encode({random.data() + ClientIDEntropySize,
	  ClientSecretEntropySize});
	digest.length(Ztls::SecretHash::Size, false);
	if (!Ztls::secretHash(m_rng, ZuBSpan{secret}, digest)) {
	  ZuClear(random);
	  ZuClear(secret);
	  complete(adminErrorResult(503, "unavailable",
	    "client credential generation failed"));
	  return;
	}
      }
      int64_t now = Zm::now().sec();
      Client client{.id = clientID, .appID = input.appID,
	.label = ZuMv(input.label), .secretDigest = ZuMv(digest),
	.secretVersion = 1, .redirects = ZuMv(input.redirectURIs),
	.created = now, .updated = now, .profile = profile,
	.grants = input.grants, .refreshAllowed = input.refreshAllowed,
	.identityScopes = ZuMv(input.identityScopes), .state = State::Active,
	.version = 1};
      ZuClear(random);
      ZmRef<AppEdit_<ClientAdd>> add = new AppEdit_<ClientAdd>{
	m_db, m_context, &m_rng, ClientAdd{
	  .before = Client{.id = client.id, .appID = client.appID},
	  .values = ZuMv(client), .request = ZuMv(request)},
	[secret = ZuMv(secret), complete = ZuMv(complete)](AdminResult result) mutable {
	  if (result.status == 201 && secret) {
	    if (!adminAddItemString(result, "client_secret", secret))
	      result = adminErrorResult(503, "unavailable", "response encoding failed");
	  }
	  if (secret) ZuClear(secret);
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
	  .before = AdminAccess{.actorKind = kind, .actorID = actorID, .appID = appID},
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
	ZuClear(input.clientSecret);
	complete(adminErrorResult(503, "unavailable",
	  "provider secret encryption failed"));
	return;
      }
      if (input.clientSecret)
	ZuClear(input.clientSecret);
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
	    ZuClear(input.clientSecret);
	  complete(adminErrorResult(400, "invalid_request",
	    "invalid provider secret"));
	  return;
	}
	ZuClear(input.clientSecret);
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
	BulkRevoke_::Sessions, input.userID, {}, 0, input.limit,
	ZuMv(request), ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::consentRevoke: {
      ConsentSelector input;
      if (!adminBody(body, input)) break;
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Consents, input.userID, ZuMv(input.clientID),
	input.appID, input.limit, ZuMv(request), ZuMv(complete)};
      revoke->start();
      return;
    }
    case MgmtOp::grantRevoke: {
      GrantSelector input;
      if (!adminBody(body, input) || !input.limit ||
          input.limit > AdminQueryLimit::Results)
	break;
      if (input.id) {
	Bytes id;
	id.length(ZuBase64URL::declen(input.id.length()), false);
	if (ZuBase64URL::decode(id, ZuBSpan{input.id}) != id.length() || !id ||
	    input.userID || input.appID) break;
	ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	  BulkRevoke_::Grants, 0, {}, 0, 1, ZuMv(request), ZuMv(complete),
	  m_config.refreshRevoke};
	revoke->one(ZuMv(id));
	return;
      }
      ZmRef<BulkRevoke_> revoke = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Grants, input.userID, {}, input.appID,
	input.limit, ZuMv(request), ZuMv(complete), m_config.refreshRevoke};
      revoke->start();
      return;
    }
    case MgmtOp::grantCleanup: {
      CleanupInput input;
      if (!adminBody(body, input) || input.before) break;
      ZmRef<BulkRevoke_> cleanup = new BulkRevoke_{m_db, m_context, &m_rng,
	BulkRevoke_::Cleanup, 0, {}, 0, input.limit, ZuMv(request), ZuMv(complete)};
      cleanup->start();
      return;
    }
    case MgmtOp::signKeyAdd: {
      SignKeyInput input;
      if (!adminBody(body, input) || input.algorithm != "ES256" ||
	  !input.appID || input.providerRef || !input.privateMaterial) break;
      String issuer;
      if (!appIssuer(m_config.issuer, input.appID, issuer)) break;
      Bytes protectedMaterial;
      Bytes plain;
      plain.length(ZuBase64URL::declen(input.privateMaterial.length()), false);
      if (ZuBase64URL::decode(plain, ZuBSpan{input.privateMaterial}) !=
	  plain.length() || !plain || !signKeyMatch(m_rng,
	    SignKey{.id = input.id, .publicJwk = input.publicJwk}, plain) ||
	  !serverSecretEncrypt(m_rng,
	    m_config.dbKey, issuer, "zum.sign_key", input.id,
	    "privateMaterial", plain, protectedMaterial)) {
	if (plain) ZuClear(plain);
	ZuClear(input.privateMaterial);
	complete(adminErrorResult(400, "invalid_request",
	  "invalid signing private material"));
	return;
      }
      ZuClear(plain);
      ZuClear(input.privateMaterial);
      int64_t now = Zm::now().sec();
      SignKey key{.id = ZuMv(input.id), .issuer = ZuMv(issuer),
	.algorithm = ZuMv(input.algorithm),
	.providerRef = ZuMv(input.providerRef),
	.publicJwk = ZuMv(input.publicJwk),
	.privateMaterial = ZuMv(protectedMaterial),
	.notBefore = input.notBefore, .state = State::Active,
	.version = 1, .created = now, .updated = now};
      auto apps = m_context->apps;
      apps->run(0, [this, apps, appID = input.appID, key = ZuMv(key),
	  request = ZuMv(request), complete = ZuMv(complete)]() mutable {
	apps->find<0>(0, ZuFwdTuple(appID), [this, key = ZuMv(key),
	    request = ZuMv(request), complete = ZuMv(complete)](
	      ZdbRowRef<App> app) mutable {
	  if (!app || app->data().state != State::Active || app->data().owner) {
	    complete(adminErrorResult(404, "not_found",
	      "application not found"));
	    return;
	  }
	  ZmRef<AppEdit_<KeyAdd>> add = new AppEdit_<KeyAdd>{
	    m_db, m_context, &m_rng,
	    KeyAdd{.before = SignKey{.id = key.id}, .values = ZuMv(key),
	      .request = ZuMv(request)}, ZuMv(complete)};
	  add->start();
	});
      });
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
	AdminDoneFn appComplete = ZuMv(complete);
	if (state == State::Revoked) {
	  complete = AdminDoneFn{[this, id, appComplete = ZuMv(appComplete)](
	      AdminResult result) mutable {
	    if (result.status != 200) { appComplete(ZuMv(result)); return; }
	    daemonAppCleanup(m_db, m_context, &m_rng, id,
      DaemonCleanupLimit::Rows,
      [appComplete = ZuMv(appComplete), result = ZuMv(result)](
          MaintenanceResult cleanup) mutable {
        if (cleanup.ok) appComplete(ZuMv(result));
	        else appComplete(adminErrorResult(503, "unavailable",
          "application cleanup failed"));
      });
	  }};
	} else complete = ZuMv(appComplete);
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
    case MgmtOp::roleState: {
      AppID appID;
      if (!adminTargetApp(target, appID)) break;
      String prefix{"/admin/apps/"};
      prefix << appID;
      switch (op) {
	case MgmtOp::membershipState: prefix << "/memberships/"; break;
	case MgmtOp::actionState: prefix << "/actions/"; break;
	case MgmtOp::roleState: prefix << "/roles/"; break;
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

void Daemon::adminAuth_(String authorization, AdminAuthFn complete)
{
  static constexpr auto prefix = "Bearer "_Zu;
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
  m_context->signKeys->find<0>(0, ZuFwdTuple(ZuMv(header.keyID)), [
    this, now, authorization = ZuMv(authorization),
    complete = ZuMv(complete)
  ](ZdbRowRef<SignKey> row) mutable {
    Principal principal;
    bool valid = row && signKeyVerify(row->data(), authorization,
      row->data().issuer, {}, now, JWTLimits{}, principal);
    String appIssuer_;
    valid = valid && principal.appID &&
      (row->data().issuer == m_config.issuer ||
       (appIssuer(m_config.issuer, principal.appID, appIssuer_) &&
	appIssuer_ == row->data().issuer));
    if (!valid) {
      complete(false, Principal{});
      return;
    }
    AppID appID = principal.appID;
    m_context->apps->find<0>(0, ZuFwdTuple(appID), [
      principal = ZuMv(principal), complete = ZuMv(complete)
    ](ZdbRowRef<App> app) mutable {
      bool active = app && app->data().state == State::Active &&
	!app->data().owner && principal.audience == app->data().audience;
      complete(active, active ? ZuMv(principal) : Principal{});
    });
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
	// An application's default same-name client receives its administrative
	// capabilities from AdminAccess. Its workload token therefore does not
	// carry these management actions in the core application's catalog.
	bool appAdmin = !principal.authMethod && principal.clientID &&
	 principal.clientID == principal.subject && principal.appID &&
	 (op == MgmtOp::catalogPublish || op == MgmtOp::operationQuery);
	if (!permitted && !appAdmin) {
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

static void adminDelegation(
    DBContext *context, String actorID, int op, AdminAccessFn complete)
{
  enum { UniqueLimit = 2 };
  using Table = AdminAccessTable;
  using Tuple = Table::Tuple;
  auto access = context->adminAccess;
  access->run(0, [access, actorID = ZuMv(actorID), op,
      complete = ZuMv(complete)]() mutable {
    access->selectRows<1>(
      ZuFwdTuple(ActorKind::Client, ZuMv(actorID)), UniqueLimit, [
        op, complete = ZuMv(complete), permit = AdminPermit{},
        ambiguous = false](ZuUnion<void, Tuple> result,
          unsigned count) mutable {
      if (result.template is<Tuple>()) {
	if (count >= UniqueLimit) {
	  ambiguous = true;
	  return;
	}
	auto tuple = ZuMv(result).template p<Tuple>();
	ZuTupleCall(ZuMv(tuple), [&permit, op](auto &&...args) mutable {
	  AdminAccess row{ZuFwd<decltype(args)>(args)...};
	  permit.targetApp = row.appID;
	  if (row.owner || row.state != State::Active) return;
	  for (auto permitted: row.operationIDs)
	    if (permitted == unsigned(op)) {
	      permit.roleIDs = ZuMv(row.roleIDs);
	      permit.allowed = true;
	      return;
	    }
	});
	return;
      }
      complete(ambiguous ? AdminPermit{} : ZuMv(permit));
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
	  adminDelegation(m_context, ZuMv(clientID), op, ZuMv(complete));
	  return;
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
  ZuClear(canonical);
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
	  complete(IdemBegin{.result = AdminResult{adminJSON(AdminIdem{
	    idemKey, "complete", row->data().resultIDs}), 200}});
	} else if (row->data().status == RequestStatus::Pending) {
	  complete(IdemBegin{.result = AdminResult{adminJSON(AdminIdem{
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

} // namespace Zum
