//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Typed reader for the pre-application Zum database schema.

#ifndef ZumLegacy_HH
#define ZumLegacy_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/Zum.hh>
#include <zlib/Zdb.hh>

#include <zlib/zum_legacy_issuer_fbs.h>
#include <zlib/zum_legacy_user_fbs.h>
#include <zlib/zum_legacy_cred_fbs.h>
#include <zlib/zum_legacy_action_fbs.h>
#include <zlib/zum_legacy_role_fbs.h>
#include <zlib/zum_legacy_scope_fbs.h>
#include <zlib/zum_legacy_client_fbs.h>
#include <zlib/zum_legacy_grant_fbs.h>
#include <zlib/zum_legacy_sign_key_fbs.h>
#include <zlib/zum_legacy_audit_fbs.h>

namespace Zum::Legacy {

struct Issuer {
  String	id;
  ActionID	nextActionID = 0;
  uint64_t	authVersion = 0;
  uint64_t	nextAuditID = 0;
};
ZfbStruct(ZumAPI, Issuer,
  (((id),		(Ctor<0>, Keys<0>)),	(String)),
  (((nextActionID),	(Ctor<1>, Mutable)),	(UInt32, 0)),
  (((authVersion),	(Ctor<2>, Mutable)),	(UInt64, 0)),
  (((nextAuditID),	(Ctor<3>, Mutable)),	(UInt64, 0)));
ZfbRoot(Issuer);

struct User {
  UserID	id = 0;
  String	name;
  Bytes		handle;
  IDVec		roleIDs;
  int64_t	created = 0;
  int64_t	updated = 0;
  State::T	state = State::Pending;
  uint128_t	owner = 0;
  uint64_t	authVersion = 1;
  String	oidcSub;
};
ZfbStruct(ZumAPI, User,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt64)),
  (((name),	(Ctor<1>, Keys<2>, Mutable)),		(String)),
  (((handle),	(Ctor<2>, Keys<1>)),			(Bytes)),
  (((roleIDs),	(Ctor<3>, Mutable)),			(UInt64Vec)),
  (((created),	(Ctor<4>)),			(Int64)),
  (((updated),	(Ctor<5>, Mutable)),			(Int64)),
  (((state),	(Ctor<6>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),	(Ctor<7>, Mutable, Hidden)),		(UInt128)),
  (((authVersion), (Ctor<8>, Mutable)),			(UInt64, 1)),
  (((oidcSub),	(Ctor<9>, Keys<3>, Mutable)),		(String)));
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
};
ZfbStruct(ZumAPI, Cred,
  (((id),		(Ctor<0>, Keys<0>)),			(Bytes)),
  (((userID),		(Ctor<0>, Keys<1>, Group<1>)),		(UInt64)),
  (((publicKey),	(Ctor<1>)),				(Bytes)),
  (((signCount),	(Ctor<2>, Mutable)),			(UInt32, 0)),
  (((created),		(Ctor<3>)),				(Int64)),
  (((updated),		(Ctor<4>, Mutable)),			(Int64)),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)),
  (((backupEligible),	(Ctor<6>)),				(Bool, false)),
  (((backedUp),		(Ctor<7>, Mutable)),			(Bool, false)),
  (((label),		(Ctor<8>, Mutable)),			(String)),
  (((owner),		(Ctor<9>, Mutable, Hidden)),		(UInt128)),
  (((userVersion),	(Ctor<10>)),				(UInt64, 1)));
ZfbRoot(Cred);

struct Action {
  ActionID	id = 0;
  String	name;
  State::T	state = State::Active;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, Action,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt32)),
  (((name),	(Ctor<1>, Keys<1>)),			(String)),
  (((state),	(Ctor<2>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),	(Ctor<3>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Action);

struct Role {
  RoleID	id = 0;
  String	name;
  ZtBitmap	actions;
  State::T	state = State::Active;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, Role,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt64)),
  (((name),	(Ctor<1>, Keys<1>, Mutable)),		(String)),
  (((actions),	(Ctor<2>, Mutable)),			(UDT, ZtBitmap{})),
  (((state),	(Ctor<3>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),	(Ctor<4>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Role);

struct Scope {
  ScopeID	id = 0;
  String	audience;
  String	name;
  IDVec		roleIDs;
  State::T	state = State::Active;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, Scope,
  (((id),		(Ctor<0>, Keys<0>, Descend<0>)),	(UInt64)),
  (((audience),	(Ctor<1>, Keys<1>, Group<1>)),		(String)),
  (((name),		(Ctor<2>, Keys<1>, Mutable)),		(String)),
  (((roleIDs),		(Ctor<3>, Mutable)),			(UInt64Vec)),
  (((state),		(Ctor<4>, Mutable, Enum<State::Map>)),	(Int8)),
  (((owner),		(Ctor<5>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Scope);

struct Client {
  String	id;
  Bytes		secretDigest;
  StringVec	redirects;
  StringVec	audiences;
  IDVec		scopeIDs;
  IDVec		roleIDs;
  int64_t	created = 0;
  int64_t	updated = 0;
  ClientType::T	type = ClientType::Browser;
  uint8_t	grants = 0;
  State::T	state = State::Pending;
  uint128_t	owner = 0;
};
ZfbStruct(ZumAPI, Client,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((secretDigest),	(Ctor<1>, Mutable, Hidden)),		(Bytes)),
  (((redirects),	(Ctor<2>, Mutable)),			(StringVec)),
  (((audiences),	(Ctor<3>, Mutable)),			(StringVec)),
  (((scopeIDs),		(Ctor<4>, Mutable)),			(UInt64Vec)),
  (((roleIDs),		(Ctor<5>, Mutable)),			(UInt64Vec)),
  (((created),		(Ctor<6>)),				(Int64)),
  (((updated),		(Ctor<7>, Mutable)),			(Int64)),
  (((type),		(Ctor<8>, Enum<ClientType::Map>)),	(Int8)),
  (((grants),		(Ctor<9>, Mutable)),			(UInt8, 0)),
  (((state),		(Ctor<10>, Mutable, Enum<State::Map>)), (Int8)),
  (((owner),		(Ctor<11>, Mutable, Hidden)),		(UInt128)));
ZfbRoot(Client);

// Grants are deliberately not copied: their authority snapshots predate app
// ownership and cannot be safely reinterpreted.  The full shape is retained
// here so the source table can be opened and checked for unsettled owners.
struct Grant {
  Bytes		id;
  uint128_t	owner = 0;
  uint64_t	authVersion = 0;
  uint64_t	userVersion = 1;
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
  int64_t	authTime = 0;
  IDVec		scopeIDs;
  IDVec		roleIDs;
  ZtBitmap	actions;
  Bytes		credentialID;
  String	redirectURI;
  Bytes		pkceChallenge;
  String	oauthState;
  Bytes		digest;
  BytesVec	spent;
  Bytes		challenge;
  Bytes		bindingDigest;
  String	userName;
  Bytes		userHandle;
  String	label;
  String	actor;
};
ZfbStruct(ZumAPI, Grant,
  (((id),		(Ctor<0>, Keys<0>)),			(Bytes)),
  (((issuer),		(Ctor<11>)),				(String)),
  (((userID),		(Ctor<3>, Mutable)),			(UInt64)),
  (((clientID),		(Ctor<12>)),				(String)),
  (((credentialID),	(Ctor<18>, Mutable)),			(Bytes)),
  (((audience),		(Ctor<13>)),				(String)),
  (((redirectURI),	(Ctor<19>)),				(String)),
  (((scopeIDs),		(Ctor<15>, Mutable)),			(UInt64Vec)),
  (((actions),		(Ctor<17>, Mutable)),			(UDT, ZtBitmap{})),
  (((digest),		(Ctor<22>, Mutable, Hidden)),		(Bytes)),
  (((spent),		(Ctor<23>, Mutable, Hidden)),		(BytesVec)),
  (((challenge),	(Ctor<24>, Mutable, Hidden)),		(Bytes)),
  (((bindingDigest),	(Ctor<25>, Mutable, Hidden)),		(Bytes)),
  (((pkceChallenge),	(Ctor<20>, Hidden)),			(Bytes)),
  (((oauthState),	(Ctor<21>, Hidden)),			(String)),
  (((authVersion),	(Ctor<1>, Mutable)),			(UInt64)),
  (((authTime),		(Ctor<14>, Mutable)),			(Int64)),
  (((created),		(Ctor<4>)),				(Int64)),
  (((expires),		(Ctor<5>, Keys<1>, Mutable)),		(Int64)),
  (((generation),	(Ctor<6>, Mutable)),			(UInt32, 0)),
  (((kind),		(Ctor<7>, Mutable, Enum<GrantKind::Map>)), (Int8)),
  (((purpose),		(Ctor<8>, Enum<GrantPurpose::Map>)),	(Int8)),
  (((state),		(Ctor<9>, Mutable, Enum<State::Map>)),	(Int8)),
  (((oauthStatePresent), (Ctor<10>)),			(Bool, false)),
  (((owner),		(Ctor<0>, Mutable, Hidden)),		(UInt128)),
  (((userName),		(Ctor<26>)),				(String)),
  (((userHandle),	(Ctor<27>, Hidden)),			(Bytes)),
  (((roleIDs),		(Ctor<16>)),				(UInt64Vec)),
  (((label),		(Ctor<28>)),				(String)),
  (((userVersion),	(Ctor<2>, Mutable)),			(UInt64, 1)),
  (((actor),		(Ctor<29>)),				(String)));
ZfbRoot(Grant);

struct SignKey {
  String	id;
  String	providerRef;
  String	publicJwk;
  int64_t	notBefore = 0;
  int64_t	retireAfter = 0;
  State::T	state = State::Pending;
};
ZfbStruct(ZumAPI, SignKey,
  (((id),		(Ctor<0>, Keys<0>)),			(String)),
  (((providerRef),	(Ctor<1>, Hidden)),			(String)),
  (((publicJwk),	(Ctor<2>)),				(String)),
  (((notBefore),	(Ctor<3>)),				(Int64)),
  (((retireAfter),	(Ctor<4>, Keys<1>, Descend<1>, Mutable)), (Int64)),
  (((state),		(Ctor<5>, Mutable, Enum<State::Map>)),	(Int8)));
ZfbRoot(SignKey);

struct Audit {
  uint64_t	id = 0;
  int64_t	time = 0;
  String	issuer;
  String	actor;
  String	subject;
  String	target;
  AuditEvent::T	event = AuditEvent::Authentication;
  AuditOutcome::T outcome = AuditOutcome::Success;
  String	detail;
};
ZfbStruct(ZumAPI, Audit,
  (((id),	(Ctor<0>, Keys<0>, Descend<0>)),	(UInt64)),
  (((time),	(Ctor<1>, Keys<1>)),			(Int64)),
  (((issuer),	(Ctor<2>, Keys<0>, Group<0>)),		(String)),
  (((actor),	(Ctor<3>)),				(String)),
  (((subject),	(Ctor<4>)),				(String)),
  (((target),	(Ctor<5>)),				(String)),
  (((event),	(Ctor<6>, Enum<AuditEvent::Map>)),	(Int8)),
  (((outcome),	(Ctor<7>, Enum<AuditOutcome::Map>)),	(Int8)),
  (((detail),	(Ctor<8>)),				(String)));
ZfbRoot(Audit);

ZdbTableDerive(IssuerTable, Issuer);
ZdbTableDerive(UserTable, User);
ZdbTableDerive(CredTable, Cred);
ZdbTableDerive(ActionTable, Action);
ZdbTableDerive(RoleTable, Role);
ZdbTableDerive(ScopeTable, Scope);
ZdbTableDerive(ClientTable, Client);
ZdbTableDerive(GrantTable, Grant);
ZdbTableDerive(SignKeyTable, SignKey);
ZdbTableDerive(AuditTable, Audit);

struct DBContext : public ZumPolymorph {
  ZmRef<IssuerTable>	issuers;
  ZmRef<UserTable>	users;
  ZmRef<CredTable>	creds;
  ZmRef<ActionTable>	actions;
  ZmRef<RoleTable>	roles;
  ZmRef<ScopeTable>	scopes;
  ZmRef<ClientTable>	clients;
  ZmRef<GrantTable>	grants;
  ZmRef<SignKeyTable>	signKeys;
  ZmRef<AuditTable>	audits;
};

ZumExtern ZmRef<DBContext> registerSchema(Zdb *);

} // namespace Zum::Legacy

#endif /* ZumLegacy_HH */
