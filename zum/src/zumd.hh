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
#include <zlib/zum_membership_fbs.h>
#include <zlib/zum_action_fbs.h>
#include <zlib/zum_role_fbs.h>
#include <zlib/zum_audience_fbs.h>
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
ZtEnumNS(ZumAPI, ClientType, int8_t, Browser, Native, Confidential);
ZtEnumNS(ZumAPI, ClientAuthMethod, int8_t, None, ClientSecretBasic);
ZtEnumNS(ZumAPI, GrantKind, int8_t,
  Ceremony, Capability, Code, Refresh);
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

namespace ClientGrant {
  enum { AuthorizationCode = 1, ClientCredentials = 2, RefreshToken = 4 };
}

namespace CoreRole {
  enum : uint64_t { Superuser = 1, CatalogPublisher = 2 };
}
namespace CoreAudience {
  enum : uint64_t { Admin = 1 };
}
namespace ScopeError {
  enum { OK = 0, Malformed, Unavailable, Audience };
}

namespace AuthorityError {
  enum { Invalid = -1 };
}

struct ScopeSelection {
  AppID		appID = 0;
  AudienceID	audienceID = 0;
  String	audience;
  String	scope;
  IDVec		roleIDs;
  bool		identity = false;
};

enum { SchemaVersion = 19 };

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
  (((id),		(Ctor<0>, Keys<0>)),	(String)),
  (((schemaVersion),	(Ctor<1>, Mutable)),	(UInt32, 0)),
  (((coreAppID),	(Ctor<2>, Mutable, JSON::String<>)),	(UInt64)),
  (((bootstrapPhase),	(Ctor<3>, Mutable, Enum<BootstrapPhase::Map>)), (Int8)),
  (((initialUserID),	(Ctor<4>, Mutable, JSON::String<>)),	(UInt64)),
  (((initialClientID),	(Ctor<5>, Mutable)),	(String)),
  (((keyCheck),		(Ctor<6>, Mutable, Hidden)), (Bytes)),
  (((pendingKeyCheck),	(Ctor<7>, Mutable, Hidden)), (Bytes)));
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

  friend ZfStructPrint ZuPrintType(App *);
};
ZfbStruct(ZumAPI, (App, JSON),
  (((id),		(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>)),	(UInt64)),
  (((name),		(Ctor<1>, Keys<1>)),			(String)),
  (((label),		(Ctor<2>, Mutable)),			(String)),
  (((state),		(Ctor<3>, Mutable, Enum<State::Map>)),	(Int8)),
  (((nextActionID),	(Ctor<4>, Mutable)),			(UInt32, 0)),
  (((authVersion),	(Ctor<5>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((catalogRevision),	(Ctor<6>, Mutable, JSON::String<>)),			(UInt64, 0)),
  (((catalogDigest),	(Ctor<7>, Mutable, JSON::Base64URL)),	(Bytes)),
  (((version),		(Ctor<8>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<9>)),				(Int64)),
  (((updated),		(Ctor<10>, Mutable)),			(Int64)),
  (((owner),		(Ctor<11>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(App);

struct Membership {
  AppID		appID = 0;
  UserID	userID = 0;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	authVersion = 1;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Membership *);
};
ZfbStruct(ZumAPI, (Membership, JSON),
  (((appID),		(Ctor<0>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((userID),		(Ctor<1>, (Keys<0, 1>), Group<1>, JSON::String<>)),	(UInt64)),
  (((roleIDs),		(Ctor<2>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((state),		(Ctor<3>, Mutable, Enum<State::Map>)),	(Int8)),
  (((authVersion),	(Ctor<4>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((version),		(Ctor<5>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<6>)),				(Int64)),
  (((updated),		(Ctor<7>, Mutable)),			(Int64)),
  (((owner),		(Ctor<8>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Membership);

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
  (((id),	(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>)),	(UInt64)),
  (((source),	(Ctor<1>, Keys<2>, Enum<UserSource::Map>)), (Int8)),
  (((name),	(Ctor<2>, Keys<2>, Mutable)),		(String)),
  (((profile),	(Ctor<3>, Mutable)),			(String)),
  (((email),	(Ctor<4>, Mutable)),			(String)),
  (((handle),	(Ctor<5>, Keys<1>, Mutable, JSON::Base64URL)), (Bytes)),
  (((created),	(Ctor<6>)),				(Int64)),
  (((updated),	(Ctor<7>, Mutable)),			(Int64)),
  (((state),	(Ctor<8>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),	(Ctor<9>, Mutable, Hidden)),		(UInt128)),
  (((authVersion), (Ctor<10>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((version),		(Ctor<11>, Mutable, JSON::String<>)),			(UInt64, 1)));
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
  (((id),		(Ctor<0>, Keys<0>, JSON::Base64URL)),	(Bytes)),
  (((userID),		(Ctor<1>, Keys<1>, Group<1>, JSON::String<>)),		(UInt64)),
  (((publicKey),	(Ctor<2>, Hidden)),			(Bytes)),
  (((signCount),	(Ctor<3>, Mutable)),			(UInt32, 0)),
  (((created),		(Ctor<4>)),				(Int64)),
  (((updated),		(Ctor<5>, Mutable)),			(Int64)),
  (((state),		(Ctor<6>, Mutable, Enum<State::Map>)),	(Int8)),
  (((backupEligible),	(Ctor<7>)),				(Bool, false)),
  (((backedUp),		(Ctor<8>, Mutable)),			(Bool, false)),
  (((label),		(Ctor<9>, Mutable)),			(String)),
  (((owner),		(Ctor<10>, Mutable, Hidden)),		(UInt128)),
  (((userVersion),	(Ctor<11>, JSON::String<>)),			(UInt64, 1)),
  (((version),		(Ctor<12>, Mutable, JSON::String<>)),			(UInt64, 1)));
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
  (((appID),		(Ctor<0>, (Keys<0, 1>), JSON::String<>)),		(UInt64)),
  (((id),		(Ctor<1>, Keys<0>)),			(UInt32)),
  (((name),		(Ctor<2>, Keys<1>)),			(String)),
  (((label),		(Ctor<3>, Mutable)),			(String)),
  (((state),		(Ctor<4>, Mutable, Enum<State::Map>)),	(Int8)),
  (((origin),		(Ctor<5>, Enum<Origin::Map>)),		(Int8)),
  (((catalogRevision),	(Ctor<6>, Mutable, JSON::String<>)),			(UInt64)),
  (((tombstone),	(Ctor<7>, Mutable)),			(Bool, false)),
  (((version),		(Ctor<8>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<9>)),				(Int64)),
  (((updated),		(Ctor<10>, Mutable)),			(Int64)),
  (((owner),		(Ctor<11>, Mutable, Hidden)),		(UInt128)));
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
  (((appID),		(Ctor<0>, (Keys<0, 1>), JSON::String<>)),		(UInt64)),
  (((id),		(Ctor<1>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((name),		(Ctor<2>, Keys<1>, Mutable)),		(String)),
  (((label),		(Ctor<3>, Mutable)),			(String)),
  (((actions),		(Ctor<4>, Mutable)),			(UDT, ZtBitmap{})),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)),
  (((origin),		(Ctor<6>, Enum<Origin::Map>)),		(Int8)),
  (((catalogRevision),	(Ctor<7>, Mutable, JSON::String<>)),			(UInt64)),
  (((tombstone),	(Ctor<8>, Mutable)),			(Bool, false)),
  (((version),		(Ctor<9>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<10>)),				(Int64)),
  (((updated),		(Ctor<11>, Mutable)),			(Int64)),
  (((owner),		(Ctor<12>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Role);

struct Audience {
  AudienceID	id = 0;
  AppID		appID = 0;
  String	name;
  String	uri;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Audience *);
};
ZfbStruct(ZumAPI, (Audience, JSON),
  (((id),	(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>)),	(UInt64)),
  (((appID),	(Ctor<1>, Keys<2>, Group<2>, JSON::String<>)),	(UInt64)),
  (((name),	(Ctor<2>, Mutable)),			(String)),
  (((uri),	(Ctor<3>, (Keys<1, 2>))),		(String)),
  (((state),	(Ctor<4>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),	(Ctor<5>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),	(Ctor<6>)),				(Int64)),
  (((updated),	(Ctor<7>, Mutable)),			(Int64)),
  (((owner),	(Ctor<8>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Audience);

// Runtime authority resolved from an app role and an audience. This is not a
// database record: the scope name is the role name and its audience is chosen
// from the client's access grant.
struct ScopeAuth {
  Role role;
  AudienceID audienceID = 0;
  String audience;
};

struct Client {
  String	id;
  AppID		appID = 0;
  String	label;
  Bytes		secretDigest;
  uint64_t	secretVersion = 1;
  StringVec	redirects;
  int64_t	created = 0;
  int64_t	updated = 0;
  ClientType::T	type = ClientType::Browser;
  ClientAuthMethod::T authMethod = ClientAuthMethod::None;
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
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((appID),		(Ctor<1>, Keys<1>, Group<1>, JSON::String<>)),		(UInt64)),
  (((label),		(Ctor<2>, Mutable)),			(String)),
  (((secretDigest),	(Ctor<3>, Mutable, Hidden)),		(Bytes)),
  (((secretVersion),	(Ctor<4>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((redirects),	(Ctor<5>, Mutable)),			(StringVec)),
  (((created),		(Ctor<6>)),				(Int64)),
  (((updated),		(Ctor<7>, Mutable)),			(Int64)),
  (((type),		(Ctor<8>, Enum<ClientType::Map>)),	(Int8)),
  (((authMethod),	(Ctor<9>, Mutable, Enum<ClientAuthMethod::Map>)), (Int8)),
  (((grants),		(Ctor<10>, Mutable)),			(UInt8, 0)),
  (((refreshAllowed),	(Ctor<11>, Mutable)),			(Bool, false)),
  (((identityScopes),	(Ctor<12>, Mutable)),			(StringVec)),
  (((state),		(Ctor<13>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<14>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((owner),		(Ctor<15>, Mutable, Hidden)),		(UInt128)),
  (((previousSecretDigest), (Ctor<16>, Mutable, Hidden)),	(Bytes)),
  (((previousSecretExpires),(Ctor<17>, Mutable)),		(Int64)));
ZfbRoot(Client);

struct ClientAccess {
  String	clientID;
  AppID		appID = 0;
  IDVec		audienceIDs;
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
  (((clientID),		(Ctor<0>, (Keys<0, 1>), Group<0>)),		(String)),
  (((appID),		(Ctor<1>, (Keys<0, 1>), Group<1>, JSON::String<>)),	(UInt64)),
  (((audienceIDs),	(Ctor<2>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((roleIDs),		(Ctor<4>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)),
  (((authVersion),	(Ctor<6>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((version),		(Ctor<7>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<8>)),				(Int64)),
  (((updated),		(Ctor<9>, Mutable)),			(Int64)),
  (((owner),		(Ctor<10>, Mutable, Hidden)),		(UInt128)));
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
  (((actorKind),	(Ctor<0>, (Keys<0, 1>), Group<1>, Enum<ActorKind::Map>)), (Int8)),
  (((actorID),		(Ctor<1>, (Keys<0, 1>), Group<1>)),	(String)),
  (((appID),		(Ctor<2>, (Keys<0, 1>), JSON::String<>)),	(UInt64)),
  (((operationIDs),	(Ctor<3>, Mutable)),			(UInt32Vec)),
  (((roleIDs),		(Ctor<4>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<6>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<7>)),				(Int64)),
  (((updated),		(Ctor<8>, Mutable)),			(Int64)),
  (((owner),		(Ctor<9>, Mutable, Hidden)),		(UInt128)));
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
  (((id),		(Ctor<0>, Keys<0>, Descend<0>, JSON::String<>)),	(UInt64)),
  (((name),		(Ctor<1>, Keys<1>)),			(String)),
  (((issuer),		(Ctor<2>, Mutable)),			(String)),
  (((clientID),		(Ctor<3>, Mutable)),			(String)),
  (((clientSecret),	(Ctor<4>, Mutable, Hidden)),		(Bytes)),
  (((scopes),		(Ctor<5>, Mutable)),			(StringVec)),
  (((roleClaim),	(Ctor<6>, Mutable)),			(String)),
  (((claimSource),	(Ctor<7>, Mutable, Enum<ClaimSource::Map>)), (Int8)),
  (((state),		(Ctor<8>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<9>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<10>)),				(Int64)),
  (((updated),		(Ctor<11>, Mutable)),			(Int64)),
  (((owner),		(Ctor<12>, Mutable, Hidden)),		(UInt128)));
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
  (((appID),		(Ctor<0>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((providerID),	(Ctor<1>, Mutable, JSON::String<>)),			(UInt64)),
  (((localFirst),	(Ctor<2>, Mutable)),			(Bool, true)),
  (((eligibilityMode),	(Ctor<3>, Mutable, Enum<EligibilityMode::Map>)), (Int8)),
  (((eligibilityClaim),	(Ctor<4>, Mutable)),			(String)),
  (((eligibilityValues),(Ctor<5>, Mutable)),			(StringVec)),
  (((assignmentMaxAge),(Ctor<6>, Mutable)),			(UInt32)),
  (((sessionIdle),	(Ctor<7>, Mutable)),			(UInt32)),
  (((sessionAbsolute),	(Ctor<8>, Mutable)),			(UInt32)),
  (((tokenLifetime),	(Ctor<9>, Mutable)),			(UInt32)),
  (((consentPolicy),	(Ctor<10>, Mutable, Enum<ConsentPolicy::Map>)), (Int8)),
  (((state),		(Ctor<11>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<12>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<13>)),				(Int64)),
  (((updated),		(Ctor<14>, Mutable)),			(Int64)),
  (((owner),		(Ctor<15>, Mutable, Hidden)),		(UInt128)));
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
  (((providerID),	(Ctor<0>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((issuer),		(Ctor<1>, Keys<0>)),			(String)),
  (((subject),		(Ctor<2>, Keys<0>)),			(String)),
  (((userID),		(Ctor<3>, Keys<1>, JSON::String<>)),			(UInt64)),
  (((version),		(Ctor<4>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<5>)),				(Int64)),
  (((updated),		(Ctor<6>, Mutable)),			(Int64)),
  (((owner),		(Ctor<7>, Mutable, Hidden)),		(UInt128)));
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
  (((appID),		(Ctor<0>, (Keys<0, 1>), (Group<0, 1>), JSON::String<>)), (UInt64)),
  (((providerID),	(Ctor<1>, (Keys<0, 1>), Group<0>, JSON::String<>)),	(UInt64)),
  (((value),		(Ctor<2>, (Keys<0, 1>))),			(String)),
  (((roleID),		(Ctor<3>, Mutable, JSON::String<>)),			(UInt64)),
  (((state),		(Ctor<4>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<5>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<6>)),				(Int64)),
  (((updated),		(Ctor<7>, Mutable)),			(Int64)),
  (((owner),		(Ctor<8>, Mutable, Hidden)),		(UInt128)));
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
  (((appID),		(Ctor<0>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((userID),		(Ctor<1>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((providerID),	(Ctor<2>, Keys<0>, JSON::String<>)),			(UInt64)),
  (((roleValues),	(Ctor<3>, Mutable)),			(StringVec)),
  (((eligible),		(Ctor<4>, Mutable)),			(Bool, false)),
  (((observed),		(Ctor<5>, Mutable)),			(Int64)),
  (((deadline),		(Ctor<6>, Keys<1>, Mutable)),		(Int64)),
  (((source),		(Ctor<7>, Mutable, Enum<ClaimSource::Map>)), (Int8)),
  (((policyVersion),	(Ctor<8>, Mutable, JSON::String<>)),			(UInt64)),
  (((protectedRefreshToken), (Ctor<9>, Mutable, Hidden)),	(Bytes)),
  (((version),		(Ctor<10>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<11>)),				(Int64)),
  (((updated),		(Ctor<12>, Mutable)),			(Int64)),
  (((owner),		(Ctor<13>, Mutable, Hidden)),		(UInt128)));
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
  (((digest),		(Ctor<0>, (Keys<0, 1>), Hidden)),		(Bytes)),
  (((userID),		(Ctor<1>, Keys<1>, Group<1>, JSON::String<>)),		(UInt64)),
  (((providerID),	(Ctor<2>, JSON::String<>)),				(UInt64)),
  (((issuer),		(Ctor<3>)),				(String)),
  (((subject),		(Ctor<4>)),				(String)),
  (((authTime),		(Ctor<5>)),				(Int64)),
  (((idleDeadline),	(Ctor<6>, Keys<2>, Mutable)),		(Int64)),
  (((absoluteDeadline), (Ctor<7>, Mutable)),			(Int64)),
  (((state),		(Ctor<8>, Mutable, Enum<State::Map>)),	(Int8)),
  (((authVersion),	(Ctor<9>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((version),		(Ctor<10>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<11>)),				(Int64)),
  (((updated),		(Ctor<12>, Mutable)),			(Int64)),
  (((owner),		(Ctor<13>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Session);

struct Consent {
  UserID	userID = 0;
  String	clientID;
  AppID		appID = 0;
  AudienceID	audienceID = 0;
  IDVec		roleIDs;
  State::T	state = State::Pending;
  uint64_t	version = 1;
  int64_t	created = 0;
  int64_t	updated = 0;
  uint128_t	owner = 0;

  friend ZfStructPrint ZuPrintType(Consent *);
};
ZfbStruct(ZumAPI, (Consent, JSON),
  (((userID),		(Ctor<0>, (Keys<0, 1>), Group<1>, JSON::String<>)),	(UInt64)),
  (((clientID),		(Ctor<1>, (Keys<0, 1>))),			(String)),
  (((appID),		(Ctor<2>, (Keys<0, 1>), JSON::String<>)),		(UInt64)),
  (((audienceID),	(Ctor<3>, (Keys<0, 1>), JSON::String<>)),		(UInt64)),
  (((roleIDs),		(Ctor<4>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<6>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<7>)),				(Int64)),
  (((updated),		(Ctor<8>, Mutable)),			(Int64)),
  (((owner),		(Ctor<9>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Consent);

struct Grant {
  // All kinds: id, issuer, created, expires, kind, purpose, state, owner.
  // Authorization ceremony/code/refresh: clientID, audience, scope, actions,
  // authVersion, userID, userVersion, and role snapshots.
  // Passkey ceremony/code/refresh: credentialID.  Ceremony: challenge and
  // bindingDigest.  Authorization ceremony/code: redirectURI, pkceChallenge,
  // oauthState and oauthStatePresent preserve absent versus explicitly empty.
  // Refresh: digest, spent, and generation.  Enrollment/capability fields are
  // userName, userHandle, label, and actor.
  // Zdb supplies the primary key separately during row construction.
  Bytes		id;

  // Common identity and lifecycle.  Scalar fields precede variable storage.
  uint128_t	owner = 0;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
  uint64_t	clientVersion = 0;
  uint64_t	membershipVersion = 0;
  uint64_t	policyVersion = 0;
  uint64_t	evidenceVersion = 0;
  AppID		appID = 0;
  AudienceID	audienceID = 0;
  UserID	userID = 0;
  int64_t	created = 0;
  int64_t	expires = 0;
  uint32_t	generation = 0;
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
  BytesVec	spent;
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
  (((id),		(Ctor<0>, (Keys<0, 2, 3>), JSON::Base64URL)), (Bytes)),
  (((issuer),		(Ctor<18>)),				(String)),
  (((userID),		(Ctor<10>, Keys<2>, Group<2>, Mutable, JSON::String<>)),	(UInt64)),
  (((appID),		(Ctor<8>, (Keys<2, 3>), Group<3>, JSON::String<>)),	(UInt64)),
  (((audienceID),	(Ctor<9>, JSON::String<>)),				(UInt64)),
  (((clientID),		(Ctor<19>)),				(String)),
  (((credentialID),	(Ctor<26>, Mutable, JSON::Base64URL)),	(Bytes)),
  (((audience),		(Ctor<20>)),				(String)),
  (((redirectURI),	(Ctor<27>)),				(String)),
  (((actions),		(Ctor<25>, Mutable)),			(UDT, ZtBitmap{})),
  (((digest),		(Ctor<30>, Mutable, Hidden)),		(Bytes)),
  (((spent),		(Ctor<31>, Mutable, Hidden)),		(BytesVec)),
  (((challenge),	(Ctor<32>, Mutable, Hidden)),		(Bytes)),
  (((bindingDigest),	(Ctor<33>, Mutable, Hidden)),		(Bytes)),
  (((pkceChallenge),	(Ctor<28>, Hidden)),			(Bytes)),
  (((oauthState),	(Ctor<29>, Hidden)),			(String)),
  (((authVersion),	(Ctor<2>, Mutable, JSON::String<>)),			(UInt64)),
  (((authTime),		(Ctor<22>, Mutable)),			(Int64)),
  (((created),		(Ctor<11>)),				(Int64)),
  (((expires),		(Ctor<12>, Keys<1>, Mutable)),		(Int64)),
  (((generation),	(Ctor<13>, Mutable)),			(UInt32, 0)),
  (((kind),		(Ctor<14>, Mutable, Enum<GrantKind::Map>)), (Int8)),
  (((purpose),		(Ctor<15>, Enum<GrantPurpose::Map>)),	(Int8)),
  (((state),		(Ctor<16>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),		(Ctor<1>, Mutable, Hidden)),		(UInt128)),
  (((userName),		(Ctor<34>)),				(String)),
  (((userHandle),	(Ctor<35>, Hidden)),			(Bytes)),
  (((requestedRoleIDs),	(Ctor<23>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((roleIDs),		(Ctor<24>, Mutable, JSON::String<>)),			(UInt64Vec)),
  (((label),		(Ctor<36>)),				(String)),
  (((userVersion),	(Ctor<3>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((clientVersion),	(Ctor<4>, Mutable, JSON::String<>)),			(UInt64)),
  (((membershipVersion),(Ctor<5>, Mutable, JSON::String<>)),			(UInt64)),
  (((policyVersion),	(Ctor<6>, Mutable, JSON::String<>)),			(UInt64)),
  (((evidenceVersion),	(Ctor<7>, Mutable, JSON::String<>)),			(UInt64)),
  (((authoritySource),	(Ctor<21>, Mutable, Enum<UserSource::Map>)), (Int8)),
  (((authorityProviderID),(Ctor<40>, Mutable, JSON::String<>)),	(UInt64)),
  (((prompt),		(Ctor<41>, Hidden)),			(String)),
  (((maxAge),		(Ctor<42>)),				(UInt64)),
  (((promptPresent),	(Ctor<43>)),				(Bool, false)),
  (((maxAgePresent),	(Ctor<44>)),				(Bool, false)),
  (((actor),		(Ctor<37>)),				(String)),
  (((oauthStatePresent), (Ctor<17>)),				(Bool, false)),
  (((scope),		(Ctor<38>, Mutable)),			(String)),
  (((nonce),		(Ctor<39>, Hidden)),			(String)));
ZfbRoot(Grant);

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
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((issuer),		(Ctor<1>, Keys<2>, Group<2>)),		(String)),
  (((algorithm),	(Ctor<2>)),				(String)),
  (((providerRef),	(Ctor<3>, Hidden)),			(String)),
  (((publicJwk),	(Ctor<4>)),				(String)),
  (((privateMaterial),	(Ctor<5>, Mutable, Hidden)),		(Bytes)),
  (((notBefore),	(Ctor<6>)),				(Int64)),
  (((retireAfter),	(Ctor<7>, (Keys<1, 2>), Descend<1>, Mutable)), (Int64)),
  (((state),		(Ctor<8>, Mutable, Enum<State::Map>)),	(Int8)),
  (((version),		(Ctor<9>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<10>)),				(Int64)),
  (((updated),		(Ctor<11>, Mutable)),			(Int64)),
  (((owner),		(Ctor<12>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(SignKey);

struct SSFRx {
  String	receiverID;
  AppID	appID = 0;
  String	audience;
  String	deliveryURL;
  String	secretRef;
  uint64_t revision = 1;
  int64_t	updated = 0;
  uint128_t owner = 0;
};
ZfbStruct(ZumAPI, SSFRx,
  (((receiverID), (Ctor<0>, Keys<0>)), (String)),
	(((appID), (Ctor<1>, Keys<1>, Group<1>)), (UInt64)),
  (((audience), (Ctor<2>)), (String)),
  (((deliveryURL), (Ctor<3>)), (String)),
  (((secretRef), (Ctor<4>, Hidden)), (String)),
  (((revision), (Ctor<5>)), (UInt64, 1)),
  (((updated), (Ctor<6>, Mutable)), (Int64)),
  (((owner), (Ctor<7>, Mutable, Hidden)), (UInt128)));
ZfbRoot(SSFRx);

struct SSFDelivery {
	String	eventID;
	String	receiverID;
	String	familyIssuer;
	String	familyID;
	int64_t	familyExpires = 0;
  Bytes	set;
  int64_t	nextDelivery = 0;
  uint128_t owner = 0;
};
ZfbStruct(ZumAPI, SSFDelivery,
	(((eventID), (Ctor<0>, (Keys<0, 1>))), (String)),
	(((receiverID), (Ctor<1>, (Keys<0, 1>))), (String)),
	(((familyIssuer), (Ctor<2>)), (String)),
	(((familyID), (Ctor<3>)), (String)),
	(((familyExpires), (Ctor<4>)), (Int64)),
  (((set), (Ctor<5>, Mutable)), (Bytes)),
  (((nextDelivery), (Ctor<6>, Mutable)), (Int64)),
  (((owner), (Ctor<7>, Mutable, Hidden)), (UInt128)));
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
  String	idempotencyKey;
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
  (((actorKind),	(Ctor<0>, Keys<0>, Enum<ActorKind::Map>)),	(Int8)),
  (((actorID),		(Ctor<1>, Keys<0>)),			(String)),
  (((operation),	(Ctor<2>, Keys<0>)),			(UInt32)),
  (((idempotencyKey),	(Ctor<3>, Keys<0>)),			(String)),
  (((requestDigest),	(Ctor<4>, Hidden)),			(Bytes)),
  (((sagaID),		(Ctor<5>, Hidden)),			(UInt128)),
  (((status),		(Ctor<6>, Mutable,
			Enum<RequestStatus::Map>)),		(Int8)),
  (((resultIDs),	(Ctor<7>, Mutable)),			(StringVec)),
  (((expires),		(Ctor<8>, Mutable)),		(Int64)),
  (((version),		(Ctor<9>, Mutable, JSON::String<>)),			(UInt64, 1)),
  (((created),		(Ctor<10>)),				(Int64)),
  (((updated),		(Ctor<11>, Mutable)),			(Int64)),
  (((owner),		(Ctor<12>, Mutable, Hidden)),
							(UInt128)));
ZfbRoot(IdemRequest);

ZumExtern ZtBitmap intersectActions(ZtBitmap, const ZtBitmap &);
ZumExtern ZtBitmap effectiveActions(
  unsigned actionCount, const IDVec &principalRoleIDs,
  const IDVec &scopeRoleIDs, ZuSpan<const Role>, ZuSpan<const Action>);
ZumExtern bool membershipValid(
  const App &, const Membership &, ZuSpan<const Role>);
ZumExtern bool scopeValid(
  const App &, const ScopeAuth &, const Audience &, ZuSpan<const Role>);
ZumExtern ZtBitmap appEffectiveActions(
  const App &, const Membership &, const ScopeAuth &, const Audience &,
  ZuSpan<const Role>, ZuSpan<const Action>);
ZumExtern bool actionAlloc(App &, ActionID &);
ZumExtern int selectScopes(
  const Client &, const ClientAccess &, ZuCSpan requested,
  ZuSpan<const ScopeAuth>, ScopeSelection &);
ZumExtern int selectGrantedScopes(
  const Client &, const ClientAccess &, const IDVec &grantedRoleIDs,
  bool requestedPresent, ZuCSpan requested,
  ZuSpan<const ScopeAuth>, ScopeSelection &);
ZumExtern int selectGrantedScopes(
  const Client &, const ClientAccess &, const IDVec &grantedRoleIDs,
  ZuCSpan granted,
  bool requestedPresent, ZuCSpan requested,
  ZuSpan<const ScopeAuth>, ScopeSelection &);
ZumExtern bool interactivePrincipal(
  const Grant &, const User &, const Cred &, const Client &);
ZumExtern bool clientPrincipal(const Client &);
ZumExtern int interactiveAuthority(
  const Grant &, const User &, const Cred &, const Client &,
  bool requestedPresent, ZuCSpan requested, unsigned actionCount,
  const ClientAccess &, ZuSpan<const ScopeAuth>, ZuSpan<const Role>,
  ZuSpan<const Action>,
  ScopeSelection &, ZtBitmap &);
ZumExtern int clientAuthority(
  const Client &, const ClientAccess &, ZuCSpan requested,
  unsigned actionCount, ZuSpan<const ScopeAuth>, ZuSpan<const Role>,
  ZuSpan<const Action>,
  ScopeSelection &, ZtBitmap &);
ZumExtern RefreshMatch::T refreshMatch(const Grant &, ZuBSpan digest);
ZumExtern RefreshRotate::T refreshRotate(
  Grant &, ZuBSpan presentedDigest, Bytes nextDigest,
  int64_t now, unsigned generationLimit, unsigned spentLimit);
ZumExtern RefreshRotate::T refreshRotate(
  Grant &, RefreshMatch::T, ZuBSpan presentedDigest, Bytes nextDigest,
  int64_t now, unsigned generationLimit, unsigned spentLimit);

} // namespace Zum

#endif /* zumd_HH */
