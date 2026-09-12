//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Explicit semantic mapping from the pre-application Zum schema.

#ifndef ZumMigrate_HH
#define ZumMigrate_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumLegacy.hh>

#include <zlib/ZuDerive.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsRandom.hh>

namespace Zum {
struct DB;
struct DBContext;
}

namespace Zum::Migrate {

struct AppMap {
  AppID		id = 0;
  String	name;
  String	label;
};
ZfStruct(ZumAPI, (AppMap, JSON),
  (((id),	(Ctor<0>, Required, JSON::String<>)),	(UInt64)),
  (((name),	(Ctor<1>, Required)),			(String)),
  (((label),	(Ctor<2>)),				(String)));

struct ActionMap {
  ActionID	oldID = 0;
  AppID		appID = 0;
  ActionID	id = 0;
};
ZfStruct(ZumAPI, (ActionMap, JSON),
  (((oldID),	(Ctor<0>, Required)),			(UInt32)),
  (((appID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((id),	(Ctor<2>, Required)),			(UInt32)));

struct RoleMap {
  RoleID	oldID = 0;
  AppID		appID = 0;
  RoleID	id = 0;
};
ZfStruct(ZumAPI, (RoleMap, JSON),
  (((oldID),	(Ctor<0>, Required, JSON::String<>)),	(UInt64)),
  (((appID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((id),	(Ctor<2>, Required, JSON::String<>)),	(UInt64)));

struct AudienceMap {
  String	uri;
  AppID		appID = 0;
  AudienceID	id = 0;
  String	name;
};
ZfStruct(ZumAPI, (AudienceMap, JSON),
  (((uri),	(Ctor<0>, Required)),			(String)),
  (((appID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((id),	(Ctor<2>, Required, JSON::String<>)),	(UInt64)),
  (((name),	(Ctor<3>, Required)),			(String)));

struct ScopeMap {
  ScopeID	oldID = 0;
  AppID		appID = 0;
  ScopeID	id = 0;
  AudienceID	audienceID = 0;
  IDVec		catalogRoleIDs;
};
ZfStruct(ZumAPI, (ScopeMap, JSON),
  (((oldID),	(Ctor<0>, Required, JSON::String<>)),	(UInt64)),
  (((appID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((id),	(Ctor<2>, Required, JSON::String<>)),	(UInt64)),
  (((audienceID),(Ctor<3>, Required, JSON::String<>)),	(UInt64)),
  (((catalogRoleIDs), (Ctor<4>, JSON::String<>)),		(UInt64Vec)));

struct ClientMap {
  String	oldID;
  AppID		appID = 0;
  String	id;
  String	label;
  ClientAuthMethod::T authMethod = ClientAuthMethod::None;
  StringVec	identityScopes;
};
ZfStruct(ZumAPI, (ClientMap, JSON),
  (((oldID),	(Ctor<0>, Required)),			(String)),
  (((appID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((id),	(Ctor<2>, Required)),			(String)),
  (((label),	(Ctor<3>)),				(String)),
  (((authMethod),(Ctor<4>, Enum<ClientAuthMethod::Map>)),	(Int8)),
  (((identityScopes), (Ctor<5>)),			(StringVec)));

template <typename T, typename HeapID>
struct MapVec_ : public ZtArray<T, ZtArrayHeapID_<HeapID>> {
  ZuDerive_(MapVec_, (ZtArray<T, ZtArrayHeapID_<HeapID>>))
};

#define ZUM_MAP_VEC(Name, T) \
  struct Name##HeapID : public ZuStringT<"Zum.Migrate." #Name> { }; \
  struct Name : public MapVec_<T, Name##HeapID> { \
    ZuDerive_(Name, (MapVec_<T, Name##HeapID>)) \
    friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(Name *); \
  }

ZUM_MAP_VEC(AppMaps, AppMap);
ZUM_MAP_VEC(ActionMaps, ActionMap);
ZUM_MAP_VEC(RoleMaps, RoleMap);
ZUM_MAP_VEC(AudienceMaps, AudienceMap);
ZUM_MAP_VEC(ScopeMaps, ScopeMap);
ZUM_MAP_VEC(ClientMaps, ClientMap);

#undef ZUM_MAP_VEC

struct Plan {
  String	issuer;
  AppID		coreAppID = 0;
  UserID	initialUserID = 0;
  String	initialClientID;
  int64_t	time = 0;
  AppMaps	apps;
  ActionMaps	actions;
  RoleMaps	roles;
  AudienceMaps	audiences;
  ScopeMaps	scopes;
  ClientMaps	clients;
  IDVec		discardExternalUsers;
};
ZfStruct(ZumAPI, (Plan, JSON),
  (((issuer),		(Ctor<0>, Required)),			(String)),
  (((coreAppID),	(Ctor<1>, Required, JSON::String<>)),	(UInt64)),
  (((initialUserID),	(Ctor<2>, Required, JSON::String<>)),	(UInt64)),
  (((initialClientID),	(Ctor<3>, Required)),			(String)),
  (((time),		(Ctor<4>, Required)),			(Int64)),
  (((apps),		(Ctor<5>, Required)),			(UDT)),
  (((actions),		(Ctor<6>, Required)),			(UDT)),
  (((roles),		(Ctor<7>, Required)),			(UDT)),
  (((audiences),	(Ctor<8>, Required)),			(UDT)),
  (((scopes),		(Ctor<9>, Required)),			(UDT)),
  (((clients),		(Ctor<10>, Required)),			(UDT)),
  (((discardExternalUsers), (Ctor<11>, JSON::String<>)),	(UInt64Vec)));

ZuDerive(Memberships, (ZtArray<Membership, VecHeap>));
ZuDerive(Accesses, (ZtArray<ClientAccess, VecHeap>));

struct Mapper_;
class Mapper {
public:
  Mapper();
  ~Mapper();
  Mapper(const Mapper &) = delete;
  Mapper &operator =(const Mapper &) = delete;

  bool init(const Plan &, String &error);

  bool app(const AppMap &, int64_t now, App &) const;
  bool action(const Legacy::Action &, int64_t now, Action &, String &) const;
  bool role(const Legacy::Role &, int64_t now, Role &, String &) const;
  bool audience(const AudienceMap &, int64_t now, Audience &) const;
  bool scope(const Legacy::Scope &, int64_t now, Scope &, String &) const;
  bool discardUser(UserID) const;
  bool user(const Legacy::User &, User &, Memberships &, String &) const;
  bool cred(const Legacy::Cred &, Cred &, String &) const;
  bool client(const Legacy::Client &, Client &, Accesses &, String &) const;

private:
  ZuPtr<Mapper_>	m_impl;
};

ZumExtern bool loadPlan(ZuCSpan path, Plan &, String &error);

ZuDerive(PutFn, (ZmFn<void(bool), ZmFnHeapID<"Zum.Migrate.PutFn">>));

ZumExtern bool start(DB *, DBContext *, Ztls::Random &, const Plan &,
  ZuBSpan dbKey, PutFn);
ZumExtern bool finish(DB *, DBContext *, Ztls::Random &, ZuCSpan issuer,
  PutFn);

struct Config {
  Plan		plan;
  Bytes		dbKey;
};
ZuDerive(CompleteFn,
  (ZmFn<void(bool, String), ZmFnHeapID<"Zum.Migrate.CompleteFn">>));

// Both databases must be active standalone stores. The source is read-only;
// the destination must be empty or a matching interrupted migration target.
ZumExtern bool run(Zdb *, Legacy::DBContext *, DB *, DBContext *,
  Ztls::Random &, Config, CompleteFn);

#define ZUM_MIGRATE_PUT_DECL(T) \
  ZumExtern bool put(DB *, DBContext *, Ztls::Random &, T, PutFn)

ZUM_MIGRATE_PUT_DECL(App);
ZUM_MIGRATE_PUT_DECL(User);
ZUM_MIGRATE_PUT_DECL(Cred);
ZUM_MIGRATE_PUT_DECL(Membership);
ZUM_MIGRATE_PUT_DECL(Action);
ZUM_MIGRATE_PUT_DECL(Role);
ZUM_MIGRATE_PUT_DECL(Scope);
ZUM_MIGRATE_PUT_DECL(Audience);
ZUM_MIGRATE_PUT_DECL(Client);
ZUM_MIGRATE_PUT_DECL(ClientAccess);

#undef ZUM_MIGRATE_PUT_DECL

} // namespace Zum::Migrate

#endif /* ZumMigrate_HH */
