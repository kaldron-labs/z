//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumDB.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/ZumKeyDB.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZfJSON.hh>

namespace Zum {

int SagaCatalog::match(ZuCSpan type)
{
  struct IDs { using Keys = Zdb_::SagaTypes<List>; };
  static constexpr auto matcher = ZuMatcher<IDs>();
  return matcher.exact(type);
}

struct SignKeyJWK {
  String kty;
  String crv;
  String kid;
  String x;
  String y;
  String alg;
  String use;
  String d;
};
ZfStruct(, (SignKeyJWK, JSON),
  (((kty),		(Required)),	(String)),
  (((crv),		(Required)),	(String)),
  (((kid),		(Required)),	(String)),
  (((x),		(Required)),	(String)),
  (((y),		(Required)),	(String)),
  (((alg),		(JSON::Opt)),	(String)),
  (((use),		(JSON::Opt)),	(String)),
  (((d),		(JSON::Opt)),	(String)));

bool signKeyPublic(const SignKey &key, Bytes &output)
{
  // ZfJSON parses in place; the saved public JWK must remain unchanged.
  String json{key.publicJwk};
  if (!json.mutable_()) json.length(json.length());
  bool valid = [&json, &key, &output]() {
    auto parsed = ZfJSON::scan({json.data(), json.length()});
    if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
	!parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
    auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
    if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
	!roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
    auto jwk = ZfJSON::handler<SignKeyJWK>(roots[0]).ctor();
    if (jwk.d || jwk.kty != "EC" || jwk.crv != "P-256" ||
        jwk.kid != key.id || !jwk.x || !jwk.y ||
        (jwk.alg && jwk.alg != "ES256") ||
        (jwk.use && jwk.use != "sig")) return false;
    // SEC1 uncompressed P-256 point, fixed by the supported ES256 profile.
    output.length(Ztls::COSE::ES256::PublicKeySize, false);
    auto point = output.data();
    enum { Coord = Ztls::COSE::ES256::CoordinateSize };
    point[0] = 4;
    if (!(ZuBase64URL::decode({point + 1, Coord}, ZuBSpan{jwk.x}) == Coord &&
      ZuBase64URL::decode({point + 1 + Coord, Coord}, ZuBSpan{jwk.y}) == Coord &&
      Ztls::COSE::ES256::validPK(output))) return false;
    return true;
  }();
  ZuClear(json.data(), json.length());
  if (!valid) output.null();
  return valid;
}

bool signKeyVerify(const SignKey &key, ZuCSpan token,
    ZuCSpan issuer, ZuCSpan audience, int64_t now,
    const JWTLimits &limits, Principal &principal)
{
  if (key.issuer != issuer || key.algorithm != "ES256" ||
      (key.state != State::Active && key.state != State::Suspended) ||
      key.notBefore > now || (key.retireAfter && key.retireAfter <= now))
    return false;
  Bytes publicKey;
  if (!signKeyPublic(key, publicKey)) return false;
  return audience ? jwtVerify(token, key.id, issuer, audience,
    publicKey, now, limits, principal) : jwtVerifyIssuer(token, key.id,
      issuer, publicKey, now, limits, principal);
}

static bool publicKeyValid(const SignKey &key, ZuBSpan expected = {})
{
  Bytes point;
  return signKeyPublic(key, point) &&
    (!expected || Ztls::ctEqual(point, expected));
}

unsigned KeyAdd::validate() const
{
  if (!values.id || values.id != before.id || !values.issuer || values.algorithm != "ES256" ||
      !values.publicJwk || bool(values.providerRef) == bool(values.privateMaterial) ||
      values.notBefore <= 0 || (values.retireAfter && values.retireAfter <= values.notBefore) ||
      updated <= 0 || !publicKeyValid(values)) return 400;
  return before.version ? 409 : 0;
}

bool signKeyMatch(Ztls::Random &rng, const SignKey &key, ZuBSpan privateKey)
{
  try {
    Ztls::PK::SK_EC signer{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1, privateKey};
    ZuBArray<Ztls::COSE::ES256::PublicKeySize> point(
      Ztls::COSE::ES256::PublicKeySize, false);
    return Ztls::Backend::pkey_ec_export_public(signer.key, point) &&
      publicKeyValid(key, point);
  } catch (...) {
    return false;
  }
}

SignKey KeyAdd::result() const
{
  SignKey item = values;
  item.state = State::Active;
  item.version = 1;
  item.created = item.updated = updated;
  return item;
}

void RecoveryStart::validate(Zdb_::SagaCompleteFn complete)
{
  if (!issuer || !actor || !userID || !capabilityID || !digest || created <= 0 ||
      expires <= created || !version || version == UINT64_MAX || !userVersion) {
    complete(false); return;
  }
  auto issuers = context->issuers;
  issuers->run(0, [this, issuers, complete = ZuMv(complete)]() mutable {
    issuers->find<0>(0, ZuFwdTuple(issuer),
      [complete = ZuMv(complete)](ZdbRowRef<Issuer> row) mutable {
	complete(bool(row));
      });
  });
}

User UserInvite::result() const
{
  return User{.id = values.id, .source = UserSource::Local, .name = values.name,
    .profile = values.profile, .email = values.email, .created = updated,
    .updated = updated, .state = State::Pending, .version = 1};
}

void UserInvite::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id == UINT64_MAX || values.id != before.id || !values.name ||
      !grant.id || !grant.digest || !grant.issuer || grant.userID != values.id ||
      grant.userName != values.name || grant.kind != GrantKind::Capability ||
      grant.purpose != GrantPurpose::Enrollment || grant.state != State::Active ||
      grant.expires <= updated || grant.actor != "precreated") {
    error = 400; complete(false); return;
  }
  if (before.version) { error = 409; complete(false); return; }
  complete(true);
}

Provider ProviderAdd::result() const
{
  Provider item = values;
  item.state = State::Active;
  item.version = 1;
  item.created = item.updated = updated;
  item.owner = 0;
  return item;
}

void ProviderAdd::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id == UINT64_MAX || values.id != before.id ||
      !values.name || !values.issuer || !values.clientID || !values.roleClaim ||
      values.claimSource < 0 || values.claimSource >= ClaimSource::N) {
    error = 400; complete(false); return;
  }
  if (before.version) { error = 409; complete(false); return; }
  complete(true);
}

static unsigned appVersionError(const App &app, const App &item)
{
  return !app.version || app.owner || item.owner || item.state != State::Active ||
    item.version != app.version || item.authVersion != app.authVersion ||
    item.updated != app.updated || app.version == UINT64_MAX ||
    app.authVersion == UINT64_MAX ? 409 : 0;
}

unsigned RoleMapDelete::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned AudienceAdd::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned ClientAdd::appError(const App &item) const
{
  return appVersionError(app, item);
}

Client ClientAdd::result() const
{
  Client item = values;
  item.state = State::Active;
  item.version = item.secretVersion = 1;
  item.created = item.updated = updated;
  item.owner = 0;
  return item;
}

void ClientAdd::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id != before.id || !values.appID ||
      values.appID != app.id || values.appID != before.appID ||
      !clientConfigValid(values.type, values.grants, values.redirects)) {
    error = 400; complete(false); return;
  }
  if (values.type == ClientType::Confidential ?
      values.authMethod != ClientAuthMethod::ClientSecretBasic ||
        values.secretDigest.length() != Ztls::SecretHash::Size :
      values.authMethod != ClientAuthMethod::None || bool(values.secretDigest)) {
    error = 400; complete(false); return;
  }
  if (before.version) { error = 409; complete(false); return; }
  complete(true);
}

unsigned RoleAdd::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned ScopeAdd::appError(const App &item) const
{
  return appVersionError(app, item);
}

Role RoleAdd::result() const
{
  return Role{.appID = values.appID, .id = values.id, .name = values.name,
    .label = values.label, .state = State::Active, .origin = Origin::Custom,
    .version = 1, .created = updated, .updated = updated};
}

Scope ScopeAdd::result() const
{
  return Scope{.appID = values.appID, .id = values.id, .audienceID = values.audienceID,
    .name = values.name, .state = State::Active,
    .origin = Origin::Custom, .version = 1, .created = updated, .updated = updated};
}

template <typename Add>
static unsigned catalogAddError(const Add &def)
{
  if (!def.values.id || def.values.id == UINT64_MAX || def.values.id != def.before.id ||
      !def.values.appID || def.values.appID != def.app.id ||
      def.values.appID != def.before.appID || !def.values.name) return 400;
  return def.before.version ? 409 : 0;
}

void RoleAdd::validate(Zdb_::SagaCompleteFn complete)
{
  error = catalogAddError(*this);
  complete(!error);
}

void ScopeAdd::validate(Zdb_::SagaCompleteFn complete)
{
  if ((error = catalogAddError(*this))) { complete(false); return; }
  if (!values.audienceID) { error = 400; complete(false); return; }
  auto audiences = context->audiences;
  audiences->run(0, [this, audiences, complete = ZuMv(complete)]() mutable {
    audiences->find<0>(0, ZuFwdTuple(values.audienceID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Audience> row) mutable {
	if (!row || row->data().appID != values.appID || row->data().owner ||
	    row->data().state != State::Active) {
	  error = 400; complete(false); return;
	}
	complete(true);
      });
  });
}

Audience AudienceAdd::result() const
{
  Audience item = values;
  item.state = State::Active;
  item.version = 1;
  item.created = item.updated = updated;
  item.owner = 0;
  return item;
}

void AudienceAdd::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.id || values.id == UINT64_MAX || values.id != before.id ||
      !values.appID || values.appID != app.id || values.appID != before.appID ||
      !values.name || !values.uri) {
    error = 400; complete(false); return;
  }
  if (before.version) { error = 409; complete(false); return; }
  complete(true);
}

unsigned RoleMapPut::appError(const App &item) const
{
  return appVersionError(app, item);
}

template <typename Edit, typename Record>
static unsigned putError(const Edit &edit, const Record &item)
{
  if (edit.ifNoneMatch) return 412;
  if (!edit.ifMatch) return 428;
  String etag{"\"v"};
  etag << item.version << '"';
  if (edit.ifMatch != etag || item.version != edit.before.version) return 412;
  return edit.before.owner || item.owner || edit.before.version == UINT64_MAX ? 409 : 0;
}

unsigned RoleMapPut::recordError(const RoleMap &item) const
{
  return putError(*this, item);
}

RoleMap RoleMapPut::result() const
{
  return RoleMap{.appID = before.appID, .providerID = before.providerID,
    .value = before.value, .roleID = roleID, .state = State::Active,
    .version = before.version + 1, .created = before.version ? before.created : updated,
    .updated = updated};
}

unsigned PolicyPut::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned PolicyPut::recordError(const AuthPolicy &item) const
{
  return putError(*this, item);
}

AuthPolicy PolicyPut::result() const
{
  AuthPolicy item = values;
  item.version = before.version + 1;
  item.created = before.version ? before.created : updated;
  item.updated = updated;
  item.owner = 0;
  return item;
}

void PolicyPut::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != before.appID || values.appID != app.id ||
      !values.localFirst || values.eligibilityMode < 0 || values.eligibilityMode >= EligibilityMode::N ||
      values.consentPolicy < 0 || values.consentPolicy >= ConsentPolicy::N ||
      values.state < 0 || values.state >= State::N || !values.assignmentMaxAge ||
      !values.sessionIdle || !values.sessionAbsolute || !values.tokenLifetime ||
      values.sessionIdle > values.sessionAbsolute ||
      (values.providerID && values.eligibilityMode == EligibilityMode::ClaimValues &&
	(!values.eligibilityClaim || !values.eligibilityValues))) {
    error = 400; complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    error = ifMatch ? 412 : 428; complete(false); return;
  }
  if (!values.providerID) { complete(true); return; }
  auto providers = context->providers;
  providers->run(0, [this, providers, complete = ZuMv(complete)]() mutable {
    providers->find<0>(0, ZuFwdTuple(values.providerID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  error = 400; complete(false); return;
	}
	complete(true);
      });
  });
}

void RoleMapPut::validate(Zdb_::SagaCompleteFn complete)
{
  if (!before.appID || before.appID != app.id || !before.providerID || !before.value || !roleID) {
    error = 400; complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    error = ifMatch ? 412 : 428; complete(false); return;
  }
  auto providers = context->providers;
  providers->run(0, [this, providers, complete = ZuMv(complete)]() mutable {
    providers->find<0>(0, ZuFwdTuple(before.providerID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  error = 400; complete(false); return;
	}
	auto roles = context->roles;
	roles->run(0, [this, roles, complete = ZuMv(complete)]() mutable {
	  roles->find<0>(0, ZuFwdTuple(before.appID, roleID),
	    [this, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	      if (!row || row->data().owner || row->data().tombstone ||
		  row->data().state != State::Active) {
		error = 400; complete(false); return;
	      }
	      complete(true);
	    });
	});
      });
  });
}

template <typename IDs>
static bool uniqueRefs(const IDs &ids, bool nonzero = true)
{
  unsigned n = ids.length();
  for (unsigned i = 0; i < n; ++i) {
    auto &id = ids[i];
    if (nonzero && !id) return false;
    for (unsigned j = 0; j < i; ++j) if (id == ids[j]) return false;
  }
  return true;
}

template <unsigned Kind, typename Edit>
static void accessRefs(Edit *def, unsigned offset, Zdb_::SagaCompleteFn complete)
{
  const auto &ids = [def]() -> const IDVec & {
    if constexpr (Kind == 0) return def->values.audienceIDs;
    else if constexpr (Kind == 1) return def->values.scopeIDs;
    else return def->values.roleIDs;
  }();
  if (offset == ids.length()) {
    if constexpr (Kind < 2) accessRefs<Kind + 1>(def, 0, ZuMv(complete));
    else complete(true);
    return;
  }
  auto table = [def]() {
    if constexpr (Kind == 0) return def->context->audiences;
    else if constexpr (Kind == 1) return def->context->scopes;
    else return def->context->roles;
  }();
  auto id = ids[offset];
  using T = typename ZuDecay<decltype(*table)>::T;
  table->run(0, [def, table, id, offset, complete = ZuMv(complete)]() mutable {
    typename ZuDecay<decltype(*table)>::template Key<0> key;
    if constexpr (Kind == 0) key = ZuFwdTuple(id);
    else key = ZuFwdTuple(def->values.appID, id);
    table->template find<0>(0, ZuMv(key),
      [def, offset, complete = ZuMv(complete)](ZdbRowRef<T> row) mutable {
	if (!row || row->data().appID != def->values.appID || row->data().owner ||
	    row->data().state != State::Active) {
	  def->error = 400; complete(false); return;
	}
	if constexpr (Kind == 2) {
	  if (row->data().tombstone) { def->error = 400; complete(false); return; }
	} else if constexpr (Kind == 1) {
	  bool approved = false;
	  for (auto id: def->values.audienceIDs)
	    if (id == row->data().audienceID) { approved = true; break; }
	  if (!approved) { def->error = 400; complete(false); return; }
	}
	accessRefs<Kind>(def, offset + 1, ZuMv(complete));
      });
  });
}

unsigned ClientAccessPut::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned AdminAccessPut::appError(const App &item) const
{
  return appVersionError(app, item);
}

unsigned ClientAccessPut::recordError(const ClientAccess &item) const
{
  if (auto code = putError(*this, item)) return code;
  return item.authVersion != before.authVersion || before.authVersion == UINT64_MAX ? 409 : 0;
}

unsigned AdminAccessPut::recordError(const AdminAccess &item) const
{
  return putError(*this, item);
}

template <typename Edit>
static auto accessResult(const Edit &def)
{
  auto item = def.values;
  item.state = State::Active;
  item.version = def.before.version + 1;
  item.created = def.before.version ? def.before.created : def.updated;
  item.updated = def.updated;
  item.owner = 0;
  if constexpr (ZuIsSame<Edit, ClientAccessPut>{})
    item.authVersion = def.before.version ? def.before.authVersion + 1 : 1;
  return item;
}

ClientAccess ClientAccessPut::result() const { return accessResult(*this); }
AdminAccess AdminAccessPut::result() const { return accessResult(*this); }

void ClientAccessPut::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != app.id || values.appID != before.appID ||
      !values.clientID || values.clientID != before.clientID ||
      !uniqueRefs(values.audienceIDs) || !uniqueRefs(values.scopeIDs) || !uniqueRefs(values.roleIDs)) {
    error = 400; complete(false); return;
  }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    error = ifMatch ? 412 : 428; complete(false); return;
  }
  auto clients = context->clients;
  clients->run(0, [this, clients, complete = ZuMv(complete)]() mutable {
    clients->find<0>(0, ZuFwdTuple(values.clientID),
      [this, complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	if (!row || row->data().owner || row->data().state != State::Active) {
	  error = 400; complete(false); return;
	}
	accessRefs<0>(this, 0, ZuMv(complete));
      });
  });
}

void AdminAccessPut::validate(Zdb_::SagaCompleteFn complete)
{
  if (!values.appID || values.appID != app.id || values.appID != before.appID ||
      !values.actorID || values.actorID != before.actorID || values.actorKind != before.actorKind ||
      (values.actorKind != ActorKind::User && values.actorKind != ActorKind::Client) ||
      !uniqueRefs(values.roleIDs) || !uniqueRefs(values.operationIDs, false)) {
    error = 400; complete(false); return;
  }
  for (auto id: values.operationIDs)
    if (id >= MgmtOp::N || !managementRoute(id)) { error = 400; complete(false); return; }
  if (!before.version && (ifMatch || ifNoneMatch != "*")) {
    error = ifMatch ? 412 : 428; complete(false); return;
  }
  switch (values.actorKind) {
    case ActorKind::User: {
      ZuBox<UserID> id;
      if (id.scan(values.actorID) != int(values.actorID.length()) ||
	  ZuCmp<UserID>::null(id) || !id) {
	error = 400; complete(false); return;
      }
      auto table = context->users;
      table->run(0, [this, table, id = UserID{id}, complete = ZuMv(complete)]() mutable {
	table->find<0>(0, ZuFwdTuple(id),
	  [this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	    if (!row || row->data().owner || row->data().state == State::Revoked ||
		row->data().state == State::Consumed) {
	      error = 400; complete(false); return;
	    }
	    accessRefs<2>(this, 0, ZuMv(complete));
	  });
      });
      return;
    }
    case ActorKind::Client: {
      auto table = context->clients;
      table->run(0, [this, table, complete = ZuMv(complete)]() mutable {
	table->find<0>(0, ZuFwdTuple(values.actorID),
	  [this, complete = ZuMv(complete)](ZdbRowRef<Client> row) mutable {
	    if (!row || row->data().owner || row->data().state != State::Active ||
		row->data().type != ClientType::Confidential ||
		row->data().authMethod != ClientAuthMethod::ClientSecretBasic) {
	      error = 400; complete(false); return;
	    }
	    accessRefs<2>(this, 0, ZuMv(complete));
	  });
      });
      return;
    }
  }
}

unsigned RoleMapDelete::recordError(const RoleMap &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ? 409 : 0;
}

template <typename Edit, typename Record>
static unsigned accessStateError(const Edit &edit, const Record &item)
{
  if (edit.state < 0 || edit.state >= State::N ||
      edit.state == State::Pending || edit.state == State::Consumed) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (edit.ifMatch != etag || item.version != edit.before.version) return 412;
  if (!edit.before.version || edit.before.owner || item.owner || item.state == State::Revoked ||
      (!edit.unchanged() && edit.before.version == UINT64_MAX)) return 409;
  if constexpr (ZuIsSame<Record, ClientAccess>{})
    if (item.authVersion != edit.before.authVersion ||
        (!edit.unchanged() && edit.before.authVersion == UINT64_MAX)) return 409;
  return 0;
}

unsigned ClientAccessState::recordError(const ClientAccess &item) const
{
  return accessStateError(*this, item);
}

unsigned AdminAccessState::recordError(const AdminAccess &item) const
{
  return accessStateError(*this, item);
}

unsigned KeyRetire::recordError(const SignKey &item) const
{
  if (retireAfter <= updated) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.version == UINT64_MAX ||
    item.state == State::Revoked || item.state == State::Consumed ? 409 : 0;
}

bool clientConfigValid(
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

void ClientEdit::edit(Client &item) const
{
  if (fields == Secret) {
    if (overlapSeconds) {
	item.previousSecretDigest = item.secretDigest;
	item.previousSecretExpires = updated + overlapSeconds;
    } else {
	item.previousSecretDigest.null();
	item.previousSecretExpires = 0;
    }
    item.secretDigest = values.secretDigest;
    item.secretVersion = before.secretVersion + 1;
    return;
  }
  if (fields & 1) item.label = values.label;
  if (fields & 2) item.redirects = values.redirects;
  if (fields & 4) item.grants = values.grants;
  if (fields & 8) item.identityScopes = values.identityScopes;
}

unsigned ClientEdit::recordError(const Client &item) const
{
  if (stateOnly ? state < 0 || state >= State::N :
      !fields || (fields != Secret && (fields & ~15U)))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  if (!before.version || before.owner || item.owner || item.state == State::Revoked ||
      (!unchanged() && before.version == UINT64_MAX)) return 409;
  if (!stateOnly && fields == Secret) {
    if (values.secretDigest.length() != Ztls::SecretHash::Size ||
        updated > INT64_MAX - overlapSeconds) return 400;
    return item.type != ClientType::Confidential ||
      item.authMethod != ClientAuthMethod::ClientSecretBasic ||
      item.secretVersion != before.secretVersion || before.secretVersion == UINT64_MAX ? 409 : 0;
  }
  if (!stateOnly && !clientConfigValid(item.type,
      (fields & 4) ? values.grants : item.grants,
      (fields & 2) ? values.redirects : item.redirects)) return 409;
  return 0;
}

Client ClientEdit::result() const
{
  Client item = before;
  if (!unchanged()) {
    if (stateOnly) item.state = state;
    else edit(item);
    item.version = before.version + 1;
    item.updated = updated;
  }
  item.owner = 0;
  return item;
}

void ProviderEdit::edit(Provider &item) const
{
  if (fields & 1) item.issuer = values.issuer;
  if (fields & 2) item.clientID = values.clientID;
  if (fields & 4) item.clientSecret = values.clientSecret;
  if (fields & 8) item.scopes = values.scopes;
  if (fields & 16) item.roleClaim = values.roleClaim;
  if (fields & 32) item.claimSource = values.claimSource;
}

unsigned ProviderEdit::recordError(const Provider &item) const
{
  if (stateOnly ? state < 0 || state >= State::N : !fields || (fields & ~63U))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  if (!before.version || before.owner || item.owner || item.state == State::Revoked ||
      (!unchanged() && before.version == UINT64_MAX)) return 409;
  if (stateOnly) return 0;
  if (!((fields & 1) ? values.issuer : item.issuer) ||
      !((fields & 2) ? values.clientID : item.clientID) ||
      !((fields & 16) ? values.roleClaim : item.roleClaim) ||
      ((fields & 4) && !values.clientSecret) ||
      ((fields & 32) && (values.claimSource < 0 || values.claimSource >= ClaimSource::N)))
    return 409;
  return 0;
}

unsigned AudienceEdit::appError(const App &item) const
{
  if (stateOnly && (state < 0 || state >= State::N)) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.version != app.version || item.authVersion != app.authVersion ||
    item.updated != app.updated || (!unchanged() &&
      (app.version == UINT64_MAX || (stateOnly && app.authVersion == UINT64_MAX))) ? 409 : 0;
}

unsigned AudienceEdit::audienceError(const Audience &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    (stateOnly && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

unsigned CredEdit::credError(const Cred &item) const
{
  if (stateOnly && (state < 0 || state >= State::N)) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.updated != before.updated ||
    (stateOnly && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

unsigned UserEdit::userError(const User &item) const
{
  if (stateOnly ? state < 0 || state >= State::N : !fields || (fields & ~3U))
    return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.authVersion != before.authVersion ||
    (stateOnly && (item.source != UserSource::Local ||
      item.state == State::Revoked ||
      (state == State::Active && (item.state == State::Pending || !item.handle)))) ||
    (!unchanged() && (before.version == UINT64_MAX ||
      (stateOnly && before.authVersion == UINT64_MAX))) ? 409 : 0;
}

unsigned AppChange::appError(const App &item) const
{
  if (stateOnly && (state < 0 || state >= State::N)) return 400;
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.owner || item.owner ||
    item.authVersion != before.authVersion ||
    (stateOnly && item.state == State::Revoked) ||
    (!unchanged() && (before.version == UINT64_MAX ||
      (stateOnly && before.authVersion == UINT64_MAX))) ? 409 : 0;
}

unsigned ActionEdit::appError(const App &item) const
{
  if (state < 0 || state >= State::N) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.version != app.version || item.authVersion != app.authVersion ||
    item.updated != app.updated || (!unchanged() &&
      (app.version == UINT64_MAX || app.authVersion == UINT64_MAX)) ? 409 : 0;
}

unsigned ActionEdit::actionError(const Action &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    item.tombstone || item.state == State::Revoked ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

unsigned ScopeEdit::appError(const App &item) const
{
  if (kind < Roles || kind > Status ||
      (kind == Status && (state < 0 || state >= State::N))) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.updated != app.updated || item.version != app.version ||
    item.authVersion != app.authVersion ||
    (!unchanged() && (app.version == UINT64_MAX ||
      app.authVersion == UINT64_MAX)) ? 409 : 0;
}

unsigned ScopeEdit::scopeError(const Scope &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    (kind == Status && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

void ScopeEdit::validate(unsigned offset, Zdb_::SagaCompleteFn complete)
{
  if (kind != Roles || offset == roleIDs.length()) { complete(true); return; }
  auto id = roleIDs[offset];
  if (!id) { complete(false); return; }
  for (unsigned i = 0; i < offset; ++i)
    if (roleIDs[i] == id) { complete(false); return; }
  context->roles->run(0, [this, offset, id, complete = ZuMv(complete)]() mutable {
    context->roles->find<0>(0, ZuFwdTuple(app.id, id),
      [this, offset, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  complete(false); return;
	}
	validate(offset + 1, ZuMv(complete));
      });
  });
}

unsigned RoleEdit::appError(const App &item) const
{
  if (kind < Actions || kind > Status ||
      (kind == Status && (state < 0 || state >= State::N))) return 400;
  return !app.version || item.state != State::Active || item.owner || app.owner ||
    item.updated != app.updated || item.version != app.version ||
    item.authVersion != app.authVersion || item.nextActionID != app.nextActionID ||
    (!unchanged() && (app.version == UINT64_MAX ||
      (kind != Label && app.authVersion == UINT64_MAX))) ? 409 : 0;
}

unsigned RoleEdit::roleError(const Role &item) const
{
  String etag{"\"v"};
  etag << item.version << '"';
  if (ifMatch != etag || item.version != before.version) return 412;
  return !before.version || before.appID != app.id || before.owner || item.owner ||
    item.tombstone || (kind == Status && item.state == State::Revoked) ||
    (!unchanged() && before.version == UINT64_MAX) ? 409 : 0;
}

void RoleEdit::validate(unsigned offset, Zdb_::SagaCompleteFn complete)
{
  if (kind != Actions) { complete(true); return; }
  if (!offset) {
    actions.length(app.nextActionID);
    actions.zero();
  }
  if (offset == actionIDs.length()) { complete(true); return; }
  auto id = actionIDs[offset];
  if (id >= app.nextActionID) { complete(false); return; }
  for (unsigned i = 0; i < offset; ++i)
    if (actionIDs[i] == id) { complete(false); return; }
  context->actions->run(0, [this, offset, id, complete = ZuMv(complete)]() mutable {
    context->actions->find<0>(0, ZuFwdTuple(app.id, id),
      [this, offset, id, complete = ZuMv(complete)](ZdbRowRef<Action> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  complete(false); return;
	}
	actions.set(id);
	validate(offset + 1, ZuMv(complete));
      });
  });
}

bool MembershipChange::appValid(const App &item, bool fwd) const
{
  return item.state == State::Active && item.version == appVersion &&
    item.authVersion == appAuthVersion && item.updated == appUpdated &&
    item.owner == (fwd ? uint128_t{0} : saga->id()) &&
    appVersion != UINT64_MAX && appAuthVersion != UINT64_MAX;
}

unsigned MembershipChange::memberError(const Membership &item, bool fwd) const
{
  if (fwd) {
    if (ifMatch) {
      String etag{"\"v"};
      etag << item.version << '"';
      if (ifMatch != etag) return 412;
    }
    if (item.version != version) return 412;
    if (item.state == State::Revoked) return 409;
  }
  return version == UINT64_MAX || authVersion == UINT64_MAX ||
    item.owner != (fwd ? uint128_t{0} : saga->id()) ||
    item.version != version + !fwd ||
    item.authVersion != authVersion + !fwd ||
    item.updated != (fwd ? oldUpdated : updated) ||
    item.state != (fwd ? oldState : newState) ||
    item.roleIDs != (fwd ? oldRoles : newRoles) ? 409 : 0;
}

void MembershipChange::validate(Zdb_::SagaCompleteFn complete)
{
  context->users->run(0, [this, complete = ZuMv(complete)]() mutable {
    context->users->find<0>(0, ZuFwdTuple(userID),
      [this, complete = ZuMv(complete)](ZdbRowRef<User> row) mutable {
	if (!row || row->data().source != UserSource::Local || row->data().owner) {
	  error = 409;
	  complete(false);
	  return;
	}
	if (!assignRoles) { complete(true); return; }
	roles(0, [this, complete = ZuMv(complete)](bool valid) mutable {
	  if (!valid) error = 400;
	  complete(valid);
	});
      });
  });
}

void MembershipChange::roles(unsigned offset, Zdb_::SagaCompleteFn complete)
{
  if (offset == newRoles.length()) { complete(true); return; }
  auto id = newRoles[offset];
  if (!id) { complete(false); return; }
  for (unsigned i = 0; i < offset; ++i)
    if (newRoles[i] == id) { complete(false); return; }
  context->roles->run(0, [this, offset, id, complete = ZuMv(complete)]() mutable {
    context->roles->find<0>(0, ZuFwdTuple(appID, id),
      [this, offset, complete = ZuMv(complete)](ZdbRowRef<Role> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().tombstone || row->data().owner) {
	  complete(false);
	  return;
	}
	roles(offset + 1, ZuMv(complete));
      });
  });
}

void sagaInit(DB *db, ZmRef<DBContext> context)
{
  db->sagas(ZuMv(context));
}

bool sagaSubmit(
    DB *db, ZdbSagaID id, ZmRef<MSaga> saga,
    SagaFn submit, SagaFn complete, ZuTime deadline)
{
  return db && db->saga(0, id, ZuMv(saga), ZuMv(submit), ZuMv(complete),
    deadline);
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
  next.credentialID = ZuMv(result.credentialID);
  next.publicKey = ZuMv(result.publicKey);
  next.signCount = result.signCount;
  next.created = now;
  next.backupEligible = result.backupEligible;
  next.backedUp = result.backedUp;
  next.label = ceremony.label;
  next.precreated = ceremony.purpose == GrantPurpose::Bootstrap ||
    ceremony.actor == "precreated";
  next.beforeGrant = ceremony;
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
    .userVersion = ceremony.userVersion,
    .beforeGrant = ceremony
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
    .label = ceremony.label,
    .beforeGrant = ceremony,
    .beforeUser = user
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
  next.beforeGrant = code;
  family = ZuMv(next);
  refreshToken = ZuMv(refresh.token);
  return true;
}

} // namespace Zum
