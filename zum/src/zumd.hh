//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// passkey authentication and OAuth RBAC records

#ifndef zumd_HH
#define zumd_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZtBitmap.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZumTypes.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/zum_issuer_fbs.h>
#include <zlib/zum_app_fbs.h>
#include <zlib/zum_user_fbs.h>
#include <zlib/zum_cred_fbs.h>
#include <zlib/zum_assignment_fbs.h>
#include <zlib/zum_action_fbs.h>
#include <zlib/zum_role_fbs.h>
#include <zlib/zum_client_fbs.h>
#include <zlib/zum_client_access_fbs.h>
#include <zlib/zum_admin_access_fbs.h>
#include <zlib/zum_provider_fbs.h>
#include <zlib/zum_auth_policy_fbs.h>
#include <zlib/zum_ext_identity_fbs.h>
#include <zlib/zum_role_map_fbs.h>
#include <zlib/zum_evidence_fbs.h>
#include <zlib/zum_session_fbs.h>
#include <zlib/zum_consent_fbs.h>
#include <zlib/zum_grant_fbs.h>
#include <zlib/zum_refresh_fbs.h>
#include <zlib/zum_sign_key_fbs.h>
#include <zlib/zum_request_fbs.h>
#include <zlib/zum_ssf_fbs.h>
#include <zlib/zum_ssf_delivery_fbs.h>

namespace Zum {

// Canonical local-login form: trimmed, valid UTF-8, ASCII case-folded.
ZumAPI bool loginNormalize(String &);

template <typename Field>
using PublicField = ZuBool<
  bool(ZfFieldFilter::Save<Field>{}) &&
  !bool(ZuTypeIn<ZuFieldProp::Hidden, typename Field::Props>{})>;

ZtEnumNS(ZumAPI, State, int8_t,
  Pending, Active, Suspended, Disabled, Revoked, Consumed);
// Deployment profile is stored; the OAuth client type is derived from it.
// Local token storage does not make Browser or Native confidential.
ZtEnumNS(ZumAPI, ClientProfile, int8_t, Browser, Native, Server);
ZtEnumNS(ZumAPI, ClientType, int8_t, Public, Confidential);
inline ClientType::T clientType(ClientProfile::T profile)
{
  switch (profile) {
    case ClientProfile::Browser:
    case ClientProfile::Native: return ClientType::Public;
    case ClientProfile::Server: return ClientType::Confidential;
    default: return ClientType::T(-1);
  }
}
ZtEnumNS(ZumAPI, GrantKind, int8_t,
  Ceremony, Capability, Code);
ZtEnumNS(ZumAPI, GrantPurpose, int8_t,
  Authorization, Enrollment, Bootstrap, AddCredential, Recovery);
ZtEnumNS(ZumAPI, AuditOutcome, int8_t, Success, Failure);
ZtEnumNS(ZumAPI, AuditEvent, int8_t,
  Authentication, RefreshReuse, ClientAuthentication,
  PrincipalChange, RBACChange, Revocation, KeyRotation, CredentialChange,
  Administration);
ZtEnumNS(ZumAPI, RefreshMatch, int8_t, Unknown, Current, Spent);
ZtEnumNS(ZumAPI, RefreshRotate, int8_t,
  Invalid, Unknown, Rotated, Reused, Exhausted);
ZtEnumNS(ZumAPI, BootstrapPhase, int8_t,
  Empty, Core, AdminPending, Ready, Migrating);
ZtEnumNS(ZumAPI, UserSource, int8_t, Local, External);
ZtEnumNS(ZumAPI, Origin, int8_t, Standard, Custom);
ZtEnumNS(ZumAPI, ActorKind, int8_t, User, Client);
ZtEnumNS(ZumAPI, ClaimSource, int8_t, IDToken, UserInfo);
ZtEnumNS(ZumAPI, EligibilityMode, int8_t, ClaimValues, MappedRole);
ZtEnumNS(ZumAPI, ConsentPolicy, int8_t, Explicit, Preauthorized);
ZtEnumNS(ZumAPI, RequestStatus, int8_t, Pending, Complete);

namespace CoreRole {
  enum : uint64_t { Superuser = 1, CatalogPublisher = 2 };
}
namespace ScopeError {
  enum { OK = 0, Malformed, Unavailable };
}

namespace AuthorityError {
  enum { Invalid = -1 };
}

struct ScopeSelection {
  String	scope;
  IDVec		roleIDs;
  bool		identity = false;
};

enum { SchemaVersion = 24 };

struct Issuer {
  String	id;
  uint32_t	schemaVersion = 0;
  AppID		coreAppID = 0;
  BootstrapPhase::T bootstrapPhase = BootstrapPhase::Empty;
  UserID	initialUserID = 0;
  String	initialClientID;
  Bytes		keyCheck;

  // Nonempty while offline secret-key rotation is incomplete.
  Bytes		pendingKeyCheck;

  friend ZfStructPrint ZuPrintType(Issuer *);
};
ZfbStruct(ZumAPI, (Issuer, JSON),
  (id,		(Ctor<0>, Keys<0>),						String),
  (schemaVersion,	(JSON::ID<"schema_version">, Ctor<1>, Mutable,
    Deflt<0>),									UInt32),
  (coreAppID,	(JSON::ID<"core_app_id">, Ctor<2>, Mutable,
    JSON::String<>),								UInt64),
  (bootstrapPhase,	(JSON::ID<"bootstrap_phase">, Ctor<3>, Mutable,
    Enum<BootstrapPhase::Map>),						Int8),
  (initialUserID,	(JSON::ID<"initial_user_id">, Ctor<4>, Mutable,
    JSON::String<>),								UInt64),
  (initialClientID,	(JSON::ID<"initial_client_id">, Ctor<5>, Mutable),	String),
  (keyCheck,		(JSON::ID<"key_check">, Ctor<6>, Mutable, Hidden),	Bytes),
  (pendingKeyCheck,	(JSON::ID<"pending_key_check">, Ctor<7>, Mutable,
    Hidden),									Bytes));
ZfbRoot(Issuer);

struct App {
  AppID		id = 0;
  String	name;
  String	label;
  State::T	state = State::Pending;
  ActionID	nextActionID = 0;
  uint64_t	authVersion = 1;
  uint64_t	catalogRevision = 0;
  Bytes		catalogDigest;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;
  String	audience;

  friend ZfStructPrint ZuPrintType(App *);
};
ZfbStruct(ZumAPI, (App, JSON),
  (id,		(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>),			UInt64),
  (name,		(Ctor<1>, Keys<1>),					String),
  (label,		(Ctor<2>, Mutable),					String),
  (state,		(Ctor<3>, Mutable, Enum<State::Map>),			Int8),
  (nextActionID,	(JSON::ID<"next_action_id">, Ctor<4>, Mutable,
    Deflt<0>),									UInt32),
  (authVersion,	(JSON::ID<"auth_version">, Ctor<5>, Mutable,
    JSON::String<>, Deflt<1>),							UInt64),
  (catalogRevision,	(JSON::ID<"catalog_revision">, Ctor<6>, Mutable, JSON::String<>,
    Deflt<0>),									UInt64),
  (catalogDigest,	(JSON::ID<"catalog_digest">, Ctor<7>, Mutable,
    JSON::Base64URL),								Bytes),
  (version,		(Ctor<8>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<9>),						Int64),
  (updated,		(Ctor<10>, Mutable),					Int64),
  (owner,		(Ctor<11>, Mutable, Hidden),				UInt128),
  (audience,		(Ctor<12>),						String));
ZfbRoot(App);

struct Assignment {
  AppID		appID = 0;
  UserID	userID = 0;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	authVersion = 1;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Assignment *);
};
ZfbStruct(ZumAPI, (Assignment, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, Keys<0>,
    JSON::String<>),							UInt64),
  (userID,		(JSON::ID<"user_id">, Ctor<1>, (Keys<0, 1>), Group<1>,
    JSON::String<>),							UInt64),
  (roleIDs,		(JSON::ID<"role_ids">, Ctor<2>, Mutable,
    JSON::String<>),							UInt64Vec),
  (state,		(Ctor<3>, Mutable, Enum<State::Map>),		Int8),
  (authVersion,	(JSON::ID<"auth_version">, Ctor<4>, Mutable,
    JSON::String<>, Deflt<1>),						UInt64),
  (version,		(Ctor<5>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<6>),					Int64),
  (updated,		(Ctor<7>, Mutable),				Int64),
  (owner,		(Ctor<8>, Mutable, Hidden),			UInt128));
ZfbRoot(Assignment);

struct User {
  UserID	id = 0;
  UserSource::T	source = UserSource::Local;
  String	name;
  String	profile;
  String	email;
  Bytes		handle;
  int64_t	created = 0;
  int64_t	updated = 0;
  State::T	state = State::Pending;
  uint128_t	owner = 0;
  uint64_t	authVersion = 1;
  uint64_t	version = 1;

  friend ZfStructPrint ZuPrintType(User *);
};
ZfbStruct(ZumAPI, (User, JSON),
  (id,	(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>),			UInt64),
  (source,	(Ctor<1>, Keys<2>, Enum<UserSource::Map>),		Int8),
  (name,	(Ctor<2>, Keys<2>, Mutable),				String),
  (profile,	(Ctor<3>, Mutable),					String),
  (email,	(Ctor<4>, Mutable),					String),
  (handle,	(Ctor<5>, Keys<1>, Mutable, JSON::Base64URL),		Bytes),
  (created,	(Ctor<6>),						Int64),
  (updated,	(Ctor<7>, Mutable),					Int64),
  (state,	(Ctor<8>, Mutable, Enum<State::Map>),			Int8),
  (owner,	(Ctor<9>, Mutable, Hidden),				UInt128),
  (authVersion, (JSON::ID<"auth_version">, Ctor<10>, Mutable, JSON::String<>,
    Deflt<1>),								UInt64),
  (version,		(Ctor<11>, Mutable, JSON::String<>, Deflt<1>),	UInt64));
ZfbRoot(User);

struct Cred {
  Bytes		id;
  UserID	userID = 0;
  Bytes		publicKey;
  uint32_t	signCount = 0;
  int64_t	created = 0;
  int64_t	updated = 0;
  State::T	state = State::Pending;
  bool		backupEligible = false;
  bool		backedUp = false;
  String	label;
  uint128_t	owner = 0;
  uint64_t	userVersion = 1;
  uint64_t	version = 1;

  friend ZfStructPrint ZuPrintType(Cred *);
};
ZfbStruct(ZumAPI, (Cred, JSON),
  (id,		(Ctor<0>, Keys<0>, JSON::Base64URL),				Bytes),
  (userID,		(JSON::ID<"user_id">, Ctor<1>, Keys<1>, Group<1>,
    JSON::String<>),								UInt64),
  (publicKey,	(JSON::ID<"public_key">, Ctor<2>, Hidden),			Bytes),
  (signCount,	(JSON::ID<"sign_count">, Ctor<3>, Mutable, Deflt<0>),		UInt32),
  (created,		(Ctor<4>),						Int64),
  (updated,		(Ctor<5>, Mutable),					Int64),
  (state,		(Ctor<6>, Mutable, Enum<State::Map>),			Int8),
  (backupEligible,	(JSON::ID<"backup_eligible">, Ctor<7>, Deflt<false>),	Bool),
  (backedUp,		(JSON::ID<"backed_up">, Ctor<8>, Mutable,
    Deflt<false>),								Bool),
  (label,		(Ctor<9>, Mutable),					String),
  (owner,		(Ctor<10>, Mutable, Hidden),				UInt128),
  (userVersion,	(JSON::ID<"user_version">, Ctor<11>, JSON::String<>,
    Deflt<1>),									UInt64),
  (version,		(Ctor<12>, Mutable, JSON::String<>, Deflt<1>),		UInt64));
ZfbRoot(Cred);

struct Action {
  AppID		appID = 0;
  ActionID	id = 0;
  String	name;
  String	label;
  State::T	state = State::Active;
  Origin::T	origin = Origin::Custom;
  uint64_t	catalogRevision = 0;
  bool		tombstone = false;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Action *);
};
ZfbStruct(ZumAPI, (Action, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, (Keys<0, 1>),
    JSON::String<>),							UInt64),
  (id,		(Ctor<1>, Keys<0>),					UInt32),
  (name,		(Ctor<2>, Keys<1>),				String),
  (label,		(Ctor<3>, Mutable),				String),
  (state,		(Ctor<4>, Mutable, Enum<State::Map>),		Int8),
  (origin,		(Ctor<5>, Enum<Origin::Map>),			Int8),
  (catalogRevision,	(JSON::ID<"catalog_revision">, Ctor<6>, Mutable,
    JSON::String<>),							UInt64),
  (tombstone,	(Ctor<7>, Mutable, Deflt<false>),			Bool),
  (version,		(Ctor<8>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<9>),					Int64),
  (updated,		(Ctor<10>, Mutable),				Int64),
  (owner,		(Ctor<11>, Mutable, Hidden),			UInt128));
ZfbRoot(Action);

struct Role {
  AppID		appID = 0;
  RoleID	id = 0;
  String	name;
  String	label;
  ZtBitmap	actions;
  State::T	state = State::Active;
  Origin::T	origin = Origin::Custom;
  uint64_t	catalogRevision = 0;
  bool		tombstone = false;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Role *);
};
ZfbStruct(ZumAPI, (Role, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, (Keys<0, 1>),
    JSON::String<>),							UInt64),
  (id,		(Ctor<1>, Keys<0>, JSON::String<>),			UInt64),
  (name,		(Ctor<2>, Keys<1>, Mutable),			String),
  (label,		(Ctor<3>, Mutable),				String),
  (actions,		(Ctor<4>, Mutable),				UDT),
  (state,		(Ctor<5>, Mutable, Enum<State::Map>),		Int8),
  (origin,		(Ctor<6>, Enum<Origin::Map>),			Int8),
  (catalogRevision,	(JSON::ID<"catalog_revision">, Ctor<7>, Mutable,
    JSON::String<>),							UInt64),
  (tombstone,	(Ctor<8>, Mutable, Deflt<false>),			Bool),
  (version,		(Ctor<9>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<10>),					Int64),
  (updated,		(Ctor<11>, Mutable),				Int64),
  (owner,		(Ctor<12>, Mutable, Hidden),			UInt128));
ZfbRoot(Role);

struct Client {
  String	id;
  AppID		appID = 0;
  String	label;
  Bytes		secretDigest;
  uint64_t	secretVersion = 1;
  StringVec	redirects;
  int64_t	created = 0;
  int64_t	updated = 0;
  ClientProfile::T	profile = ClientProfile::Browser;
  uint8_t	grants = 0;
  bool		refreshAllowed = false;
  StringVec	identityScopes;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  uint128_t	owner = 0;
  Bytes		previousSecretDigest;
  int64_t	previousSecretExpires = 0;

  friend ZfStructPrint ZuPrintType(Client *);
};
ZfbStruct(ZumAPI, (Client, JSON),
  (id,		(Ctor<0>, Keys<0>),						String),
  (appID,		(JSON::ID<"app_id">, Ctor<1>, Keys<1>, Group<1>,
    JSON::String<>),								UInt64),
  (label,		(Ctor<2>, Mutable),					String),
  (secretDigest,	(JSON::ID<"secret_digest">, Ctor<3>, Mutable, Hidden),	Bytes),
  (secretVersion,	(JSON::ID<"secret_version">, Ctor<4>, Mutable,
    JSON::String<>, Deflt<1>),							UInt64),
  (redirects,	(Ctor<5>, Mutable, JSON::ID<"redirect_uris">),			StringVec),
  (created,		(Ctor<6>),						Int64),
  (updated,		(Ctor<7>, Mutable),					Int64),
  (profile,		(Ctor<8>, Enum<ClientProfile::Map>),			Int8),
  (grants,		(Flags<ClientGrant::Map>, JSON::String<ClientGrant::Fmt>, Ctor<9>,
    Mutable, Deflt<0>),							UInt8),
  (refreshAllowed,	(JSON::ID<"refresh_allowed">, Ctor<10>, Mutable,
    Deflt<false>),								Bool),
  (identityScopes,	(JSON::ID<"identity_scopes">, Ctor<11>, Mutable),	StringVec),
  (state,		(Ctor<12>, Mutable, Enum<State::Map>),			Int8),
  (version,		(Ctor<13>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (owner,		(Ctor<14>, Mutable, Hidden),				UInt128),
  (previousSecretDigest, (JSON::ID<"previous_secret_digest">, Ctor<15>,
    Mutable, Hidden),								Bytes),
  (previousSecretExpires,(JSON::ID<"previous_secret_expires">, Ctor<16>,
    Mutable),									Int64));
ZfbRoot(Client);

struct ClientAccess {
  String	clientID;
  AppID		appID = 0;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	authVersion = 1;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(ClientAccess *);
};
ZfbStruct(ZumAPI, (ClientAccess, JSON),
  (clientID,		(JSON::ID<"client_id">, Ctor<0>, (Keys<0, 1>),
    Group<0>),								String),
  (appID,		(JSON::ID<"app_id">, Ctor<1>, (Keys<0, 1>), Group<1>,
    JSON::String<>),							UInt64),
  (roleIDs,		(JSON::ID<"role_ids">, Ctor<2>, Mutable,
    JSON::String<>),							UInt64Vec),
  (state,		(Ctor<3>, Mutable, Enum<State::Map>),		Int8),
  (authVersion,	(JSON::ID<"auth_version">, Ctor<4>, Mutable,
    JSON::String<>, Deflt<1>),						UInt64),
  (version,		(Ctor<5>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<6>),					Int64),
  (updated,		(Ctor<7>, Mutable),				Int64),
  (owner,		(Ctor<8>, Mutable, Hidden),			UInt128));
ZfbRoot(ClientAccess);

struct AdminAccess {
  ActorKind::T	actorKind = ActorKind::User;
  String	actorID;
  AppID		appID = 0;
  ActionIDVec	operationIDs;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(AdminAccess *);
};
ZfbStruct(ZumAPI, (AdminAccess, JSON),
  (actorKind,	(JSON::ID<"actor_kind">, Ctor<0>, (Keys<0, 1>), Group<1>,
    Enum<ActorKind::Map>),						Int8),
  (actorID,		(JSON::ID<"actor_id">, Ctor<1>, (Keys<0, 1>),
    Group<1>),								String),
  (appID,		(JSON::ID<"app_id">, Ctor<2>, (Keys<0, 1>),
    JSON::String<>),							UInt64),
  (operationIDs,	(JSON::ID<"operation_ids">, Ctor<3>, Mutable),	UInt32Vec),
  (roleIDs,		(JSON::ID<"role_ids">, Ctor<4>, Mutable,
    JSON::String<>),							UInt64Vec),
  (state,		(Ctor<5>, Mutable, Enum<State::Map>),		Int8),
  (version,		(Ctor<6>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<7>),					Int64),
  (updated,		(Ctor<8>, Mutable),				Int64),
  (owner,		(Ctor<9>, Mutable, Hidden),			UInt128));
ZfbRoot(AdminAccess);

struct Provider {
  ProviderID	id = 0;
  String	name;
  String	issuer;
  String	clientID;
  Bytes		clientSecret;
  StringVec	scopes;
  String	roleClaim;
  ClaimSource::T claimSource = ClaimSource::IDToken;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Provider *);
};
ZfbStruct(ZumAPI, (Provider, JSON),
  (id,		(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>),			UInt64),
  (name,		(Ctor<1>, Keys<1>),					String),
  (issuer,		(Ctor<2>, Mutable),					String),
  (clientID,		(JSON::ID<"client_id">, Ctor<3>, Mutable),		String),
  (clientSecret,	(JSON::ID<"client_secret">, Ctor<4>, Mutable, Hidden),	Bytes),
  (scopes,		(Ctor<5>, Mutable),					StringVec),
  (roleClaim,	(JSON::ID<"role_claim">, Ctor<6>, Mutable),			String),
  (claimSource,	(JSON::ID<"claim_source">, Ctor<7>, Mutable,
    Enum<ClaimSource::Map>),							Int8),
  (state,		(Ctor<8>, Mutable, Enum<State::Map>),			Int8),
  (version,		(Ctor<9>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<10>),						Int64),
  (updated,		(Ctor<11>, Mutable),					Int64),
  (owner,		(Ctor<12>, Mutable, Hidden),				UInt128));
ZfbRoot(Provider);

struct AuthPolicy {
  AppID		appID = 0;
  ProviderID	providerID = 0;
  bool		localFirst = true;
  EligibilityMode::T eligibilityMode = EligibilityMode::ClaimValues;
  String	eligibilityClaim;
  StringVec	eligibilityValues;
  uint32_t	assignmentMaxAge = 0;
  uint32_t	sessionIdle = 0;
  uint32_t	sessionAbsolute = 0;
  uint32_t	tokenLifetime = 0;
  ConsentPolicy::T consentPolicy = ConsentPolicy::Explicit;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(AuthPolicy *);
};
ZfbStruct(ZumAPI, (AuthPolicy, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, Keys<0>,
    JSON::String<>),								UInt64),
  (providerID,	(JSON::ID<"provider_id">, Ctor<1>, Mutable,
    JSON::String<>),								UInt64),
  (localFirst,	(JSON::ID<"local_first">, Ctor<2>, Mutable,
    Deflt<true>),								Bool),
  (eligibilityMode,	(JSON::ID<"eligibility_mode">, Ctor<3>, Mutable,
    Enum<EligibilityMode::Map>),						Int8),
  (eligibilityClaim,	(JSON::ID<"eligibility_claim">, Ctor<4>, Mutable),	String),
  (eligibilityValues,(JSON::ID<"eligibility_values">, Ctor<5>, Mutable),	StringVec),
  (assignmentMaxAge,(JSON::ID<"assignment_max_age">, Ctor<6>, Mutable),		UInt32),
  (sessionIdle,	(JSON::ID<"session_idle">, Ctor<7>, Mutable),			UInt32),
  (sessionAbsolute,	(JSON::ID<"session_absolute">, Ctor<8>, Mutable),	UInt32),
  (tokenLifetime,	(JSON::ID<"token_lifetime">, Ctor<9>, Mutable),		UInt32),
  (consentPolicy,	(JSON::ID<"consent_policy">, Ctor<10>, Mutable,
    Enum<ConsentPolicy::Map>),							Int8),
  (state,		(Ctor<11>, Mutable, Enum<State::Map>),			Int8),
  (version,		(Ctor<12>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<13>),						Int64),
  (updated,		(Ctor<14>, Mutable),					Int64),
  (owner,		(Ctor<15>, Mutable, Hidden),				UInt128));
ZfbRoot(AuthPolicy);

struct ExtIdentity {
  ProviderID	providerID = 0;
  String	issuer;
  String	subject;
  UserID	userID = 0;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(ExtIdentity *);
};
ZfbStruct(ZumAPI, (ExtIdentity, JSON),
  (providerID,	(JSON::ID<"provider_id">, Ctor<0>, Keys<0>,
    JSON::String<>),								UInt64),
  (issuer,		(Ctor<1>, Keys<0>),					String),
  (subject,		(Ctor<2>, Keys<0>),					String),
  (userID,		(JSON::ID<"user_id">, Ctor<3>, Keys<1>,
    JSON::String<>),								UInt64),
  (version,		(Ctor<4>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<5>),						Int64),
  (updated,		(Ctor<6>, Mutable),					Int64),
  (owner,		(Ctor<7>, Mutable, Hidden),				UInt128));
ZfbRoot(ExtIdentity);

struct RoleMap {
  AppID		appID = 0;
  ProviderID	providerID = 0;
  String	value;
  RoleID	roleID = 0;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(RoleMap *);
};
ZfbStruct(ZumAPI, (RoleMap, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, (Keys<0, 1>), (Group<0, 1>),
    JSON::String<>),							UInt64),
  (providerID,	(JSON::ID<"provider_id">, Ctor<1>, (Keys<0, 1>), Group<0>,
    JSON::String<>),							UInt64),
  (value,		(Ctor<2>, (Keys<0, 1>)),			String),
  (roleID,		(JSON::ID<"role_id">, Ctor<3>, Mutable,
    JSON::String<>),							UInt64),
  (state,		(Ctor<4>, Mutable, Enum<State::Map>),		Int8),
  (version,		(Ctor<5>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<6>),					Int64),
  (updated,		(Ctor<7>, Mutable),				Int64),
  (owner,		(Ctor<8>, Mutable, Hidden),			UInt128));
ZfbRoot(RoleMap);

struct Evidence {
  AppID		appID = 0;
  UserID	userID = 0;
  ProviderID	providerID = 0;
  StringVec	roleValues;
  bool		eligible = false;
  int64_t	observed = 0;
  int64_t	deadline = 0;
  ClaimSource::T source = ClaimSource::IDToken;
  uint64_t	policyVersion = 0;
  // AES-256-GCM envelope; never a plaintext provider refresh token.
  Bytes		protectedRefreshToken;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Evidence *);
};
ZfbStruct(ZumAPI, (Evidence, JSON),
  (appID,		(JSON::ID<"app_id">, Ctor<0>, (Keys<0, 2>), Group<2>,
    JSON::String<>),								UInt64),
  (userID,		(JSON::ID<"user_id">, Ctor<1>, Keys<0>,
    JSON::String<>),								UInt64),
  (providerID,	(JSON::ID<"provider_id">, Ctor<2>, Keys<0>,
    JSON::String<>),								UInt64),
  (roleValues,	(JSON::ID<"role_values">, Ctor<3>, Mutable),			StringVec),
  (eligible,		(Ctor<4>, Mutable, Deflt<false>),			Bool),
  (observed,		(Ctor<5>, Mutable),					Int64),
  (deadline,		(Ctor<6>, Keys<1>, Mutable),				Int64),
  (source,		(Ctor<7>, Mutable, Enum<ClaimSource::Map>),		Int8),
  (policyVersion,	(JSON::ID<"policy_version">, Ctor<8>, Mutable,
    JSON::String<>),								UInt64),
  (protectedRefreshToken, (JSON::ID<"protected_refresh_token">, Ctor<9>,
    Mutable, Hidden),								Bytes),
  (version,		(Ctor<10>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<11>),						Int64),
  (updated,		(Ctor<12>, Mutable),					Int64),
  (owner,		(Ctor<13>, Mutable, Hidden),				UInt128));
ZfbRoot(Evidence);

struct Session {
  Bytes		digest;
  UserID	userID = 0;
  ProviderID	providerID = 0;
  String	issuer;
  String	subject;
  int64_t	authTime = 0;
  int64_t	idleDeadline = 0;
  int64_t	absoluteDeadline = 0;
  State::T	state = State::Pending;
  uint64_t	authVersion = 1;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Session *);
};
ZfbStruct(ZumAPI, (Session, JSON),
  (digest,		(Ctor<0>, (Keys<0, 1>), Hidden),			Bytes),
  (userID,		(JSON::ID<"user_id">, Ctor<1>, Keys<1>, Group<1>,
    JSON::String<>),								UInt64),
  (providerID,	(JSON::ID<"provider_id">, Ctor<2>, JSON::String<>),		UInt64),
  (issuer,		(Ctor<3>),						String),
  (subject,		(Ctor<4>),						String),
  (authTime,		(JSON::ID<"auth_time">, Ctor<5>),			Int64),
  (idleDeadline,	(JSON::ID<"idle_deadline">, Ctor<6>, Keys<2>,
    Mutable),									Int64),
  (absoluteDeadline, (JSON::ID<"absolute_deadline">, Ctor<7>, Mutable),		Int64),
  (state,		(Ctor<8>, Mutable, Enum<State::Map>),			Int8),
  (authVersion,	(JSON::ID<"auth_version">, Ctor<9>, Mutable,
    JSON::String<>, Deflt<1>),							UInt64),
  (version,		(Ctor<10>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<11>),						Int64),
  (updated,		(Ctor<12>, Mutable),					Int64),
  (owner,		(Ctor<13>, Mutable, Hidden),				UInt128));
ZfbRoot(Session);

struct Consent {
  UserID	userID = 0;
  String	clientID;
  AppID		appID = 0;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Consent *);
};
ZfbStruct(ZumAPI, (Consent, JSON),
  (userID,		(JSON::ID<"user_id">, Ctor<0>, (Keys<0, 1>), Group<1>,
    JSON::String<>),								UInt64),
  (clientID,		(JSON::ID<"client_id">, Ctor<1>, (Keys<0, 1>)),		String),
  (appID,		(JSON::ID<"app_id">, Ctor<2>, (Keys<0, 1, 2>),
    Group<2>, JSON::String<>),							UInt64),
  (roleIDs,		(JSON::ID<"role_ids">, Ctor<3>, Mutable,
    JSON::String<>),								UInt64Vec),
  (state,		(Ctor<4>, Mutable, Enum<State::Map>),			Int8),
  (version,		(Ctor<5>, Mutable, JSON::String<>, Deflt<1>),		UInt64),
  (created,		(Ctor<6>),						Int64),
  (updated,		(Ctor<7>, Mutable),					Int64),
  (owner,		(Ctor<8>, Mutable, Hidden),				UInt128));
ZfbRoot(Consent);

struct Grant {
  // All kinds: id, issuer, created, expires, kind, purpose, state, owner.
  // Authorization ceremony/code/refresh: clientID, audience, scope, actions,
  // authVersion, userID, userVersion, and role snapshots.
  // Passkey ceremony/code/refresh: credentialID.  Ceremony: challenge and
  // bindingDigest.  Authorization ceremony/code: redirectURI, pkceChallenge,
  // oauthState and oauthStatePresent preserve absent versus explicitly empty.
  // Code/capability secret: digest. Enrollment/capability fields are
  // userName, userHandle, label, and actor.
  // Zdb supplies the primary key separately during row construction.
  Bytes		id;

  // Common identity and lifecycle.  Scalar fields precede variable storage.
  uint128_t	owner = 0;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  uint64_t	clientVersion = 0;
  uint64_t	assignmentVersion = 0;
  uint64_t	policyVersion = 0;
  uint64_t	evidenceVersion = 0;
  AppID		appID = 0;
  UserID	userID = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  GrantKind::T	kind = GrantKind::Ceremony;
  GrantPurpose::T purpose = GrantPurpose::Authorization;
  State::T	state = State::Pending;
  bool		oauthStatePresent = false;
  String	issuer;
  String	clientID;
  String	audience;
  UserSource::T	authoritySource = UserSource::Local;

  // Authorization snapshot.
  int64_t	authTime = 0;
  IDVec		requestedRoleIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		credentialID;

  // OAuth authorization-code request.
  String	redirectURI;
  Bytes		pkceChallenge;
  String	oauthState;

  // Secret and ceremony payload.
  Bytes		digest;
  Bytes		challenge;
  Bytes		bindingDigest;

  // Enrollment, recovery, and audit attribution.
  String	userName;
  Bytes		userHandle;
  String	label;
  String	actor;

  // Exact OAuth grant, including identity scopes and OIDC nonce.
  String	scope;
  String	nonce;
  ProviderID	authorityProviderID = 0;
  String	prompt;
  uint64_t	maxAge = 0;
  bool		promptPresent = false;
  bool		maxAgePresent = false;

  friend ZfStructPrint ZuPrintType(Grant *);
};
ZfbStruct(ZumAPI, (Grant, JSON),
  (id,		(Ctor<0>, (Keys<0, 2, 3>), JSON::Base64URL),			Bytes),
  (issuer,		(Ctor<17>),						String),
  (userID,		(JSON::ID<"user_id">, Ctor<9>, Keys<2>, Group<2>, Mutable,
    JSON::String<>),								UInt64),
  (appID,		(JSON::ID<"app_id">, Ctor<8>, (Keys<2, 3>), Group<3>,
    JSON::String<>),								UInt64),
  (clientID,		(JSON::ID<"client_id">, Ctor<18>),			String),
  (credentialID,	(JSON::ID<"credential_id">, Ctor<25>, Mutable,
    JSON::Base64URL),								Bytes),
  (audience,		(Ctor<19>),						String),
  (redirectURI,	(JSON::ID<"redirect_uri">, Ctor<26>),				String),
  (actions,		(Ctor<24>, Mutable),					UDT),
  (digest,		(Ctor<29>, Mutable, Hidden),				Bytes),
  (challenge,	(Ctor<31>, Mutable, Hidden),					Bytes),
  (bindingDigest,	(JSON::ID<"binding_digest">, Ctor<32>, Mutable,
    Hidden),									Bytes),
  (pkceChallenge,	(JSON::ID<"pkce_challenge">, Ctor<27>, Hidden),		Bytes),
  (oauthState,	(JSON::ID<"oauth_state">, Ctor<28>, Hidden),			String),
  (authVersion,	(JSON::ID<"auth_version">, Ctor<2>, Mutable,
    JSON::String<>),								UInt64),
  (authTime,		(JSON::ID<"auth_time">, Ctor<21>, Mutable),		Int64),
  (created,		(Ctor<10>),						Int64),
  (expires,		(Ctor<11>, Keys<1>, Mutable),				Int64),
  (kind,		(Ctor<13>, Mutable, Enum<GrantKind::Map>),		Int8),
  (purpose,		(Ctor<14>, Enum<GrantPurpose::Map>),			Int8),
  (state,		(Ctor<15>, Mutable, Enum<State::Map>),			Int8),
  (owner,		(Ctor<1>, Mutable, Hidden),				UInt128),
  (userName,		(JSON::ID<"user_name">, Ctor<33>),			String),
  (userHandle,	(JSON::ID<"user_handle">, Ctor<34>, Hidden),			Bytes),
  (requestedRoleIDs,	(JSON::ID<"requested_role_ids">, Ctor<22>, Mutable,
    JSON::String<>),								UInt64Vec),
  (roleIDs,		(JSON::ID<"role_ids">, Ctor<23>, Mutable,
    JSON::String<>),								UInt64Vec),
  (label,		(Ctor<35>),						String),
  (userVersion,	(JSON::ID<"user_version">, Ctor<3>, Mutable,
    JSON::String<>, Deflt<1>),							UInt64),
  (clientVersion,	(JSON::ID<"client_version">, Ctor<4>, Mutable,
    JSON::String<>),								UInt64),
  (assignmentVersion,(JSON::ID<"assignment_version">, Ctor<5>, Mutable,
    JSON::String<>),								UInt64),
  (policyVersion,	(JSON::ID<"policy_version">, Ctor<6>, Mutable,
    JSON::String<>),								UInt64),
  (evidenceVersion,	(JSON::ID<"evidence_version">, Ctor<7>, Mutable,
    JSON::String<>),								UInt64),
  (authoritySource,	(JSON::ID<"authority_source">, Ctor<20>, Mutable,
    Enum<UserSource::Map>),							Int8),
  (authorityProviderID,(JSON::ID<"authority_provider_id">, Ctor<39>, Mutable,
    JSON::String<>),								UInt64),
  (prompt,		(Ctor<40>, Hidden),					String),
  (maxAge,		(JSON::ID<"max_age">, Ctor<41>),			UInt64),
  (promptPresent,	(JSON::ID<"prompt_present">, Ctor<42>, Deflt<false>),	Bool),
  (maxAgePresent,	(JSON::ID<"max_age_present">, Ctor<43>, Deflt<false>),	Bool),
  (actor,		(Ctor<36>),						String),
  (oauthStatePresent, (JSON::ID<"oauth_state_present">, Ctor<16>,
    Deflt<false>),								Bool),
  (scope,		(Ctor<37>, Mutable),					String),
  (nonce,		(Ctor<38>, Hidden),					String));
ZfbRoot(Grant);

struct Refresh {
  Bytes		id;
  uint128_t	owner = 0;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  uint64_t	clientVersion = 0;
  uint64_t	assignmentVersion = 0;
  uint64_t	policyVersion = 0;
  uint64_t	evidenceVersion = 0;
  AppID		appID = 0;
  UserID	userID = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  uint32_t	generation = 0;
  State::T	state = State::Pending;
  String	issuer;
  String	clientID;
  String	audience;
  UserSource::T	authoritySource = UserSource::Local;
  int64_t	authTime = 0;
  IDVec		requestedRoleIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		credentialID;
  Bytes		digest;
  BytesVec	spent;
  String	scope;
  String	nonce;
  ProviderID	authorityProviderID = 0;
  uint64_t	version = 1;
  int64_t	updated = 0;

  friend ZfStructPrint ZuPrintType(Refresh *);
};
ZfbStruct(ZumAPI, (Refresh, JSON),
  (id, (Ctor<0>, (Keys<0, 2, 3>), JSON::Base64URL),				Bytes),
  (issuer, (Ctor<17>),								String),
  (userID, (JSON::ID<"user_id">, Ctor<9>, Keys<2>, Group<2>, Mutable,
    JSON::String<>),								UInt64),
  (appID, (JSON::ID<"app_id">, Ctor<8>, (Keys<2, 3>), Group<3>,
    JSON::String<>),								UInt64),
  (clientID, (JSON::ID<"client_id">, Ctor<18>),					String),
  (credentialID, (JSON::ID<"credential_id">, Ctor<25>, Mutable,
    JSON::Base64URL),								Bytes),
  (audience, (Ctor<19>),							String),
  (actions, (Ctor<24>, Mutable),						UDT),
  (digest, (Ctor<29>, Mutable, Hidden),						Bytes),
  (spent, (Ctor<30>, Mutable, Hidden),						BytesVec),
  (authVersion, (JSON::ID<"auth_version">, Ctor<2>, Mutable,
    JSON::String<>),								UInt64),
  (authTime, (JSON::ID<"auth_time">, Ctor<21>, Mutable),			Int64),
  (created, (Ctor<10>),								Int64),
  (expires, (Ctor<11>, Keys<1>, Mutable),					Int64),
  (generation, (Ctor<12>, Mutable, Deflt<0>),					UInt32),
  (state, (Ctor<15>, Mutable, Enum<State::Map>),				Int8),
  (owner, (Ctor<1>, Mutable, Hidden),						UInt128),
  (requestedRoleIDs, (JSON::ID<"requested_role_ids">, Ctor<22>, Mutable,
    JSON::String<>),								UInt64Vec),
  (roleIDs, (JSON::ID<"role_ids">, Ctor<23>, Mutable, JSON::String<>),		UInt64Vec),
  (userVersion, (JSON::ID<"user_version">, Ctor<3>, Mutable, JSON::String<>,
    Deflt<1>),									UInt64),
  (clientVersion, (JSON::ID<"client_version">, Ctor<4>, Mutable,
    JSON::String<>),								UInt64),
  (assignmentVersion, (JSON::ID<"assignment_version">, Ctor<5>, Mutable,
    JSON::String<>),								UInt64),
  (policyVersion, (JSON::ID<"policy_version">, Ctor<6>, Mutable,
    JSON::String<>),								UInt64),
  (evidenceVersion, (JSON::ID<"evidence_version">, Ctor<7>, Mutable,
    JSON::String<>),								UInt64),
  (authoritySource, (JSON::ID<"authority_source">, Ctor<20>, Mutable,
    Enum<UserSource::Map>),							Int8),
  (scope, (Ctor<37>, Mutable),							String),
  (nonce, (Ctor<38>, Hidden),							String),
  (authorityProviderID, (JSON::ID<"authority_provider_id">, Ctor<39>, Mutable,
    JSON::String<>),								UInt64),
  (version, (Ctor<40>, Mutable, JSON::String<>, Deflt<1>),			UInt64),
  (updated, (Ctor<41>, Mutable),						Int64));
ZfbRoot(Refresh);

struct SignKey {
  String	id;
  String	issuer;
  String	algorithm;
  String	providerRef;
  String	publicJwk;
  Bytes		privateMaterial;
  int64_t	notBefore = 0;
  int64_t	retireAfter = 0;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(SignKey *);
};
ZfbStruct(ZumAPI, (SignKey, JSON),
  (id,		(Ctor<0>, Keys<0>),					String),
  (issuer,		(Ctor<1>, Keys<2>, Group<2>),			String),
  (algorithm,	(Ctor<2>),						String),
  (providerRef,	(JSON::ID<"provider_ref">, Ctor<3>, Hidden),		String),
  (publicJwk,	(JSON::ID<"public_jwk">, Ctor<4>),			String),
  (privateMaterial,	(JSON::ID<"private_material">, Ctor<5>, Mutable,
    Hidden),								Bytes),
  (notBefore,	(JSON::ID<"not_before">, Ctor<6>),			Int64),
  (retireAfter,	(JSON::ID<"retire_after">, Ctor<7>, (Keys<1, 2>),
    Descend<1>, Mutable),						Int64),
  (state,		(Ctor<8>, Mutable, Enum<State::Map>),		Int8),
  (version,		(Ctor<9>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<10>),					Int64),
  (updated,		(Ctor<11>, Mutable),				Int64),
  (owner,		(Ctor<12>, Mutable, Hidden),			UInt128));
ZfbRoot(SignKey);

struct SSFRx {
  int64_t expires = 0;
  AppID appID = 0;
  String receiverID;
  String audience;
  String deliveryURL;
  Bytes callbackAuth;
  uint64_t revision = 1;
  int64_t updated = 0;
  uint128_t owner = 0;
  unsigned errors = 0;
};
ZfbStruct(ZumAPI, SSFRx,
  (expires, (Ctor<0>, Keys<2>, Mutable),		Int64),
  (appID, (Ctor<1>, Keys<1>, Group<1>),			UInt64),
  (receiverID, (Ctor<2>, (Keys<0, 1, 2>)),		String),
  (audience, (Ctor<3>),					String),
  (deliveryURL, (Ctor<4>, Mutable),			String),
  (callbackAuth, (Ctor<5>, Mutable, Hidden),		Bytes),
  (revision, (Ctor<6>, Mutable, Deflt<1>),		UInt64),
  (updated, (Ctor<7>, Mutable),				Int64),
  (owner, (Ctor<8>, Mutable, Hidden),			UInt128),
  (errors, (Ctor<9>, Mutable),				UInt32));
ZfbRoot(SSFRx);

struct SSFDelivery {
	String	eventID;
	String	receiverID;
	String	familyIssuer;
	String	familyID;
	int64_t	familyExpires = 0;
  Bytes	set;
  int64_t	nextDelivery = 0;
  uint64_t receiverRevision = 0;
  uint128_t owner = 0;
};
ZfbStruct(ZumAPI, SSFDelivery,
  (eventID, (Ctor<0>, (Keys<0, 1>)),		String),
  (receiverID, (Ctor<1>, (Keys<0, 1>)),		String),
  (familyIssuer, (Ctor<2>),			String),
  (familyID, (Ctor<3>),				String),
  (familyExpires, (Ctor<4>),			Int64),
  (set, (Ctor<5>, Mutable),			Bytes),
  (nextDelivery, (Ctor<6>, Mutable),		Int64),
  (receiverRevision, (Ctor<7>),			UInt64),
  (owner, (Ctor<8>, Mutable, Hidden),		UInt128));
ZfbRoot(SSFDelivery);

// Ephemeral diagnostic event, not a database record.
struct Audit {
  int64_t	time = 0;
  String	issuer;
  AppID		appID = 0;
  ActionID	operationID = 0;
  String	actor;
  String	subject;
  String	target;
  AuditEvent::T	event = AuditEvent::Authentication;
  AuditOutcome::T outcome = AuditOutcome::Success;
  String	correlationID;
  String	detail;

};

struct IdemRequest {
  ActorKind::T	actorKind = ActorKind::User;
  String	actorID;
  ActionID	operation = 0;
  String	idempotence;
  Bytes		requestDigest;
  uint128_t	sagaID = 0;
  RequestStatus::T status = RequestStatus::Pending;
  StringVec	resultIDs;
  int64_t	expires = 0;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(IdemRequest *);
};
ZfbStruct(ZumAPI, (IdemRequest, JSON),
  (actorKind,	(JSON::ID<"actor_kind">, Ctor<0>, Keys<0>,
    Enum<ActorKind::Map>),						Int8),
  (actorID,		(JSON::ID<"actor_id">, Ctor<1>, Keys<0>),	String),
  (operation,	(Ctor<2>, Keys<0>),					UInt32),
  (idempotence,	(Ctor<3>, Keys<0>),					String),
  (requestDigest,	(JSON::ID<"request_digest">, Ctor<4>, Hidden),	Bytes),
  (sagaID,		(JSON::ID<"saga_id">, Ctor<5>, Hidden),		UInt128),
  (status,		(Ctor<6>, Mutable,
			Enum<RequestStatus::Map>),			Int8),
  (resultIDs,	(JSON::ID<"result_ids">, Ctor<7>, Mutable),		StringVec),
  (expires,		(Ctor<8>, Mutable),				Int64),
  (version,		(Ctor<9>, Mutable, JSON::String<>, Deflt<1>),	UInt64),
  (created,		(Ctor<10>),					Int64),
  (updated,		(Ctor<11>, Mutable),				Int64),
  (owner,		(Ctor<12>, Mutable, Hidden),			UInt128));
ZfbRoot(IdemRequest);

ZumExtern ZtBitmap intersectActions(ZtBitmap, const ZtBitmap &);
ZumExtern ZtBitmap effectiveActions(
  unsigned actionCount, const IDVec &principalRoleIDs,
  const IDVec &scopeRoleIDs, ZuSpan<const Role>, ZuSpan<const Action>);
ZumExtern bool assignmentValid(
  const App &, const Assignment &, ZuSpan<const Role>);
ZumExtern bool scopeValid(
  const App &, const Role &, ZuSpan<const Role>);
ZumExtern ZtBitmap appEffectiveActions(
  const App &, const Assignment &, const Role &,
  ZuSpan<const Role>, ZuSpan<const Action>);
ZumExtern bool actionAlloc(App &, ActionID &);
ZumExtern int selectScopes(
  const Client &, const ClientAccess &, ZuCSpan requested,
  ZuSpan<const Role>, ScopeSelection &);
ZumExtern int selectGrantedScopes(
  const Client &, const ClientAccess &, const IDVec &grantedRoleIDs,
  bool requestedPresent, ZuCSpan requested,
  ZuSpan<const Role>, ScopeSelection &);
ZumExtern int selectGrantedScopes(
  const Client &, const ClientAccess &, const IDVec &grantedRoleIDs,
  ZuCSpan granted,
  bool requestedPresent, ZuCSpan requested,
  ZuSpan<const Role>, ScopeSelection &);
ZumExtern bool interactivePrincipal(
  const Grant &, const User &, const Cred &, const Client &);
ZumExtern bool clientPrincipal(const Client &);
ZumExtern int interactiveAuthority(
  const Grant &, const User &, const Cred &, const Client &,
  bool requestedPresent, ZuCSpan requested, unsigned actionCount,
  const ClientAccess &, ZuSpan<const Role>, ZuSpan<const Role>,
  ZuSpan<const Action>,
  ScopeSelection &, ZtBitmap &);
ZumExtern int clientAuthority(
  const Client &, const ClientAccess &, ZuCSpan requested,
  unsigned actionCount, ZuSpan<const Role>, ZuSpan<const Role>,
  ZuSpan<const Action>,
  ScopeSelection &, ZtBitmap &);
ZumExtern RefreshMatch::T refreshMatch(const Refresh &, ZuBSpan digest);
ZumExtern RefreshRotate::T refreshRotate(
  Refresh &, ZuBSpan presentedDigest, Bytes nextDigest,
  int64_t now, unsigned generationLimit, unsigned spentLimit);
ZumExtern RefreshRotate::T refreshRotate(
  Refresh &, RefreshMatch::T, ZuBSpan presentedDigest, Bytes nextDigest,
  int64_t now, unsigned generationLimit, unsigned spentLimit);

} // namespace Zum

#endif /* zumd_HH */
