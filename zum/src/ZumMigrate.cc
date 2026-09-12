//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumMigrate.hh>
#include <zlib/ZumDB.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/ZumSecret.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum::Migrate {

using LocalID = ZuTuple<AppID, uint64_t>;
ZmHashKVDerive(U64Index, uint64_t, unsigned,
  (ZmHashHeapID<"Zum.Migrate.U64Index">));
ZmHashKVDerive(LocalIndex, LocalID, unsigned,
  (ZmHashHeapID<"Zum.Migrate.LocalIndex">));
ZmHashKVDerive(StringIndex, String, unsigned,
  (ZmHashHeapID<"Zum.Migrate.StringIndex">));

struct AppData {
  ActionID nextActionID = 0;
};
ZuDerive(AppDataVec,
  (ZtArray<AppData, ZtArrayHeapID<"Zum.Migrate.AppData">>));

struct Mapper_ : public ZumObjectAlloc {
  Plan		plan;
  U64Index	apps;
  StringIndex	appNames;
  U64Index	actions;
  LocalIndex	localActions;
  U64Index	roles;
  LocalIndex	localRoles;
  StringIndex	audiences;
  LocalIndex	localAudiences;
  U64Index	scopes;
  LocalIndex	localScopes;
  StringIndex	clients;
  StringIndex	localClients;
  U64Index	discardedUsers;
  AppDataVec	appData;
};

static bool error_(String &error, ZuCSpan message)
{
  error = message;
  return false;
}

template <typename Hash, typename Key>
static bool add_(Hash &hash, Key key, unsigned index)
{
  if (hash.findVal(key) != ZuCmp<unsigned>::null()) return false;
  hash.add(ZuMv(key), index + 1);
  return true;
}

template <typename Hash, typename Key>
static bool has_(const Hash &hash, const Key &key)
{
  return hash.findVal(key) != ZuCmp<unsigned>::null();
}

template <typename Array, typename Hash, typename Key>
static const typename Array::T *find_(
    const Array &array, const Hash &hash, const Key &key)
{
  unsigned index = hash.findVal(key);
  return index != ZuCmp<unsigned>::null() ? &array[index - 1] : nullptr;
}

Mapper::Mapper() : m_impl{new Mapper_} { }
Mapper::~Mapper() = default;

bool Mapper::init(const Plan &plan, String &error)
{
  error.null();
  if (!plan.issuer || !plan.coreAppID || !plan.initialUserID ||
      !plan.initialClientID || plan.time <= 0)
    return error_(error,
      "issuer, coreAppID, initialUserID, initialClientID and time are required");
  m_impl = new Mapper_{};
  m_impl->plan = plan;
  auto &p = m_impl->plan;
  if (!p.apps) return error_(error, "at least one application is required");
  unsigned appCount = p.apps.length();
  m_impl->appData.length(appCount);
  for (unsigned i = 0; i < appCount; ++i) {
    const auto &map = p.apps[i];
    if (!map.id || map.id == p.coreAppID || !map.name)
      return error_(error, "application id and name are required");
    if (!add_(m_impl->apps, uint64_t(map.id), i) ||
        !add_(m_impl->appNames, map.name, i))
      return error_(error, "duplicate application id or name");
  }
  unsigned actionCount = p.actions.length();
  for (unsigned i = 0; i < actionCount; ++i) {
    const auto &map = p.actions[i];
    auto appIndex = m_impl->apps.findVal(map.appID);
    if (appIndex == ZuCmp<unsigned>::null() ||
        !add_(m_impl->actions, uint64_t(map.oldID), i) ||
        !add_(m_impl->localActions, LocalID{map.appID, map.id}, i))
      return error_(error, "invalid or duplicate action mapping");
    auto &data = m_impl->appData[appIndex - 1];
    if (map.id == UINT32_MAX)
      return error_(error, "mapped action id cannot be UINT32_MAX");
    if (data.nextActionID <= map.id) data.nextActionID = map.id + 1;
  }
  unsigned roleCount = p.roles.length();
  for (unsigned i = 0; i < roleCount; ++i) {
    const auto &map = p.roles[i];
    if (!map.id || !has_(m_impl->apps, uint64_t(map.appID)) ||
        !add_(m_impl->roles, map.oldID, i) ||
        !add_(m_impl->localRoles, LocalID{map.appID, map.id}, i))
      return error_(error, "invalid or duplicate role mapping");
  }
  unsigned audienceCount = p.audiences.length();
  for (unsigned i = 0; i < audienceCount; ++i) {
    const auto &map = p.audiences[i];
    if (!map.id || !map.uri || !map.name ||
        !has_(m_impl->apps, uint64_t(map.appID)) ||
        !add_(m_impl->audiences, map.uri, i) ||
        !add_(m_impl->localAudiences, LocalID{map.appID, map.id}, i))
      return error_(error, "invalid or duplicate audience mapping");
  }
  unsigned scopeCount = p.scopes.length();
  for (unsigned i = 0; i < scopeCount; ++i) {
    const auto &map = p.scopes[i];
    if (!map.id || !has_(m_impl->apps, uint64_t(map.appID)) ||
        !has_(m_impl->localAudiences, LocalID{map.appID, map.audienceID}) ||
        !add_(m_impl->scopes, map.oldID, i) ||
        !add_(m_impl->localScopes, LocalID{map.appID, map.id}, i))
      return error_(error, "invalid or duplicate scope mapping");
    for (auto roleID: map.catalogRoleIDs)
      if (!has_(m_impl->localRoles, LocalID{map.appID, roleID}))
        return error_(error, "scope catalog role belongs to another application");
  }
  unsigned clientCount = p.clients.length();
  for (unsigned i = 0; i < clientCount; ++i) {
    const auto &map = p.clients[i];
    if (!map.oldID || !map.id || map.id == p.initialClientID ||
        !has_(m_impl->apps, uint64_t(map.appID)) ||
        !add_(m_impl->clients, map.oldID, i) ||
        !add_(m_impl->localClients, map.id, i))
      return error_(error, "invalid or duplicate client mapping");
  }
  unsigned discardedCount = p.discardExternalUsers.length();
  for (unsigned i = 0; i < discardedCount; ++i) {
    auto id = p.discardExternalUsers[i];
    if (!id || id == p.initialUserID ||
        !add_(m_impl->discardedUsers, id, i))
      return error_(error, "invalid or duplicate discarded external user");
  }
  return true;
}

bool Mapper::app(const AppMap &map, int64_t now, App &out) const
{
  auto index = m_impl->apps.findVal(map.id);
  if (index == ZuCmp<unsigned>::null() || now <= 0) return false;
  out = App{
    .id = map.id, .name = map.name,
    .label = map.label ? map.label : map.name,
    .state = State::Active,
    .nextActionID = m_impl->appData[index - 1].nextActionID,
    .authVersion = 1, .version = 1, .created = now, .updated = now};
  return true;
}

bool Mapper::action(
    const Legacy::Action &old, int64_t now, Action &out, String &error) const
{
  auto map = find_(m_impl->plan.actions, m_impl->actions, uint64_t(old.id));
  if (!map) return error_(error, "legacy action has no mapping");
  if (old.owner) return error_(error, "legacy action has an unsettled saga owner");
  out = Action{
    .appID = map->appID, .id = map->id, .name = old.name,
    .label = old.name, .state = old.state, .origin = Origin::Custom,
    .version = 1, .created = now, .updated = now};
  return true;
}

bool Mapper::role(
    const Legacy::Role &old, int64_t now, Role &out, String &error) const
{
  auto map = find_(m_impl->plan.roles, m_impl->roles, old.id);
  if (!map) return error_(error, "legacy role has no mapping");
  if (old.owner) return error_(error, "legacy role has an unsettled saga owner");
  ZtBitmap actions{m_impl->appData[m_impl->apps.findVal(map->appID) - 1].nextActionID};
  for (int bit = old.actions.first(); bit >= 0; bit = old.actions.next(bit)) {
    auto action = find_(m_impl->plan.actions, m_impl->actions, uint64_t(bit));
    if (!action) return error_(error, "legacy role references an unmapped action");
    if (action->appID != map->appID)
      return error_(error, "legacy role actions span applications");
    actions.set(action->id);
  }
  out = Role{
    .appID = map->appID, .id = map->id, .name = old.name,
    .label = old.name, .actions = ZuMv(actions), .state = old.state,
    .origin = Origin::Custom, .version = 1, .created = now, .updated = now};
  return true;
}

bool Mapper::audience(
    const AudienceMap &map, int64_t now, Audience &out) const
{
  if (!has_(m_impl->audiences, map.uri) || now <= 0) return false;
  out = Audience{
    .id = map.id, .appID = map.appID, .name = map.name, .uri = map.uri,
    .state = State::Active, .version = 1, .created = now, .updated = now};
  return true;
}

bool Mapper::scope(
    const Legacy::Scope &old, int64_t now, Scope &out, String &error) const
{
  auto map = find_(m_impl->plan.scopes, m_impl->scopes, old.id);
  if (!map) return error_(error, "legacy scope has no mapping");
  if (old.owner) return error_(error, "legacy scope has an unsettled saga owner");
  auto audience = find_(m_impl->plan.audiences,
    m_impl->audiences, old.audience);
  if (!audience || audience->appID != map->appID ||
      audience->id != map->audienceID)
    return error_(error, "legacy scope audience mapping disagrees with its application");
  IDVec roles;
  roles.size(old.roleIDs.length());
  for (auto oldID: old.roleIDs) {
    auto role = find_(m_impl->plan.roles, m_impl->roles, oldID);
    if (!role) return error_(error, "legacy scope references an unmapped role");
    if (role->appID != map->appID)
      return error_(error, "legacy scope roles span applications");
    roles.push(role->id);
  }
  out = Scope{
    .appID = map->appID, .id = map->id, .audienceID = map->audienceID,
    .name = old.name, .roleIDs = ZuMv(roles),
    .state = old.state, .origin = Origin::Custom,
    .version = 1, .created = now, .updated = now,
    .catalogRoleIDs = map->catalogRoleIDs};
  return true;
}

bool Mapper::user(const Legacy::User &old, User &out,
    Memberships &memberships, String &error) const
{
  if (!old.id || !old.name)
    return error_(error, "legacy user has an invalid id or name");
  if (old.owner) return error_(error, "legacy user has an unsettled saga owner");
  if (old.oidcSub)
    return error_(error,
      "legacy external user must be explicitly discarded and re-projected");
  out = User{
    .id = old.id, .source = UserSource::Local,
    .name = old.name, .handle = old.handle,
    .created = old.created, .updated = old.updated, .state = old.state,
    .authVersion = old.authVersion, .version = 1};
  memberships.null();
  for (auto oldID: old.roleIDs) {
    auto role = find_(m_impl->plan.roles, m_impl->roles, oldID);
    if (!role) return error_(error, "legacy user references an unmapped role");
    Membership *membership = nullptr;
    for (auto &item: memberships)
      if (item.appID == role->appID) { membership = &item; break; }
    if (!membership) {
      membership = new (memberships.push()) Membership{
        .appID = role->appID, .userID = old.id, .state = State::Active,
        .authVersion = 1, .version = 1,
        .created = old.created, .updated = old.updated};
    }
    membership->roleIDs.push(role->id);
  }
  return true;
}

bool Mapper::discardUser(UserID id) const
{
  return has_(m_impl->discardedUsers, id);
}

bool Mapper::cred(const Legacy::Cred &old, Cred &out, String &error) const
{
  if (!old.id || !old.userID || !old.publicKey)
    return error_(error, "legacy credential is incomplete");
  if (old.owner)
    return error_(error, "legacy credential has an unsettled saga owner");
  out = Cred{
    .id = old.id, .userID = old.userID, .publicKey = old.publicKey,
    .signCount = old.signCount, .created = old.created,
    .updated = old.updated, .state = old.state,
    .backupEligible = old.backupEligible, .backedUp = old.backedUp,
    .label = old.label, .userVersion = old.userVersion, .version = 1};
  return true;
}

static ClientAccess *access_(Accesses &accesses, ZuCSpan clientID,
    AppID appID, const Legacy::Client &old)
{
  for (auto &item: accesses)
    if (item.appID == appID) return &item;
  return new (accesses.push()) ClientAccess{
    .clientID = clientID, .appID = appID, .state = old.state,
    .authVersion = 1, .version = 1,
    .created = old.created, .updated = old.updated};
}

bool Mapper::client(const Legacy::Client &old, Client &out,
    Accesses &accesses, String &error) const
{
  auto map = find_(m_impl->plan.clients, m_impl->clients, old.id);
  if (!map) return error_(error, "legacy client has no mapping");
  if (old.owner) return error_(error, "legacy client has an unsettled saga owner");
  accesses.null();
  out = Client{
    .id = map->id, .appID = map->appID,
    .label = map->label ? map->label : old.id,
    .secretDigest = old.secretDigest, .secretVersion = 1,
    .redirects = old.redirects,
    .created = old.created, .updated = old.updated, .type = old.type,
    .authMethod = map->authMethod, .grants = old.grants,
    .refreshAllowed = bool(old.grants & ClientGrant::RefreshToken),
    .identityScopes = map->identityScopes, .state = old.state, .version = 1};
  for (const auto &uri: old.audiences) {
    auto audience = find_(m_impl->plan.audiences, m_impl->audiences, uri);
    if (!audience)
      return error_(error, "legacy client references an unmapped audience");
    access_(accesses, map->id, audience->appID, old)->audienceIDs.push(audience->id);
  }
  for (auto oldID: old.scopeIDs) {
    auto scope = find_(m_impl->plan.scopes, m_impl->scopes, oldID);
    if (!scope) return error_(error, "legacy client references an unmapped scope");
    access_(accesses, map->id, scope->appID, old)->scopeIDs.push(scope->id);
  }
  for (auto oldID: old.roleIDs) {
    auto role = find_(m_impl->plan.roles, m_impl->roles, oldID);
    if (!role) return error_(error, "legacy client references an unmapped role");
    access_(accesses, map->id, role->appID, old)->roleIDs.push(role->id);
  }
  return true;
}

bool loadPlan(ZuCSpan path, Plan &plan, String &error)
{
  error.null();
  ZiFile file;
  if (file.open(Zi::Path{path}, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
    return error_(error, "cannot open migration mapping");
  auto size = file.size();
  if (size <= 0 || uint64_t(size) > unsigned(INT_MAX))
    return error_(error, "invalid migration mapping size");
  String json;
  json.length(unsigned(size));
  if (file.read(json.data(), unsigned(size)) != size)
    return error_(error, "cannot read migration mapping");
  auto scan = ZfJSON::scan({json.data(), json.length()});
  if (scan.p<0>() != int(json.length()) || !scan.p<1>() ||
      !scan.p<1>()->has<ZfJSON::NodeArray>() ||
      scan.p<1>()->data<ZfJSON::NodeArray>().length() != 1 ||
      !(*scan.p<1>())[0]->has<ZfJSON::AnyNode::Object>() ||
      !ZfJSON::unique((*scan.p<1>())[0]))
    return error_(error, "invalid migration mapping JSON");
  try {
    plan = ZfJSON::handler<Plan>((*scan.p<1>())[0]).ctor();
  } catch (const ZeError &) {
    return error_(error, "invalid migration mapping fields");
  }
  Mapper mapper;
  return mapper.init(plan, error);
}

template <typename Def>
static bool submit_(DB *db, Ztls::Random &rng, Def def, PutFn complete)
{
  ZdbSagaID id;
  if (!db || !rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}) || !id) {
    complete(false); return false;
  }
  ZmRef<MSaga> saga = new MSaga{};
  saga->init(ZuMv(def));
  auto done = [complete = ZuMv(complete)](bool ok) mutable {
    if (complete) complete(ok);
  };
  if (!sagaSubmit(db, id, ZuMv(saga),
      [done](bool ok) mutable { if (!ok) done(false); }, done)) {
    done(false);
    return false;
  }
  return true;
}

bool start(DB *db, DBContext *context, Ztls::Random &rng, const Plan &plan,
    ZuBSpan dbKey, PutFn complete)
{
  if (!db || !context || dbKey.length() != 32) {
    complete(false); return false;
  }
  Issuer value{
    .id = plan.issuer, .schemaVersion = SchemaVersion,
    .coreAppID = plan.coreAppID,
    .bootstrapPhase = BootstrapPhase::Migrating,
    .initialUserID = plan.initialUserID,
    .initialClientID = plan.initialClientID,
    .keyCheck = serverKeyCheck(dbKey)};
  if (!value.keyCheck) { complete(false); return false; }
  auto table = context->issuers;
  table->run(0, [db, table, &rng, value = ZuMv(value),
    complete = ZuMv(complete)]() mutable {
    table->find<0>(0, ZuFwdTuple(value.id), [db, &rng,
      value = ZuMv(value), complete = ZuMv(complete)](
        ZdbRowRef<Issuer> existing) mutable {
      if (existing) {
        const auto &saved = existing->data();
        complete(saved.id == value.id &&
          saved.schemaVersion == value.schemaVersion &&
          saved.coreAppID == value.coreAppID &&
          (saved.bootstrapPhase == BootstrapPhase::Migrating ||
           saved.bootstrapPhase == BootstrapPhase::Empty) &&
          saved.initialUserID == value.initialUserID &&
          saved.initialClientID == value.initialClientID &&
          Ztls::ctEqual(saved.keyCheck, value.keyCheck));
        return;
      }
      submit_(db, rng, MigrationStart{.issuer = ZuMv(value)},
        ZuMv(complete));
    });
  });
  return true;
}

bool finish(DB *db, DBContext *context, Ztls::Random &rng,
    ZuCSpan issuer, PutFn complete)
{
  if (!db || !context || !issuer) { complete(false); return false; }
  auto table = context->issuers;
  String id{issuer};
  table->run(0, [db, table, &rng, id = ZuMv(id),
    complete = ZuMv(complete)]() mutable {
    table->find<0>(0, ZuFwdTuple(id), [db, &rng, id = ZuMv(id),
      complete = ZuMv(complete)](ZdbRowRef<Issuer> row) mutable {
      if (!row) { complete(false); return; }
      if (row->data().bootstrapPhase == BootstrapPhase::Empty) {
        complete(true); return;
      }
      if (row->data().bootstrapPhase != BootstrapPhase::Migrating) {
        complete(false); return;
      }
      submit_(db, rng, MigrationFinish{.issuer = ZuMv(id)}, ZuMv(complete));
    });
  });
  return true;
}

template <typename T, typename Table>
static bool put_(DB *db, Table *table, Ztls::Random &rng,
    uint32_t kind, T value, PutFn complete)
{
  if (!db || !table || value.owner) { complete(false); return false; }
  Bytes image = SagaImage::save(value);
  typename Table::template Key<0> key{ZuStructKey<0>(value)};
  table->run(0, [db, table, &rng, kind, key = ZuMv(key),
    image = ZuMv(image), complete = ZuMv(complete)]() mutable {
    table->template find<0>(0, key, [db, &rng, kind,
      image = ZuMv(image), complete = ZuMv(complete)](
        ZdbRowRef<T> existing) mutable {
      if (existing) {
        complete(SagaImage::save(existing->data()) == image);
        return;
      }
      ZdbSagaID id;
      if (!rng.random({reinterpret_cast<uint8_t *>(&id), sizeof(id)}) || !id) {
        complete(false); return;
      }
      ZmRef<MSaga> saga = new MSaga{};
      saga->init(MigrationPut{.kind = kind, .image = ZuMv(image)});
      auto done = [complete = ZuMv(complete)](bool ok) mutable {
        if (complete) complete(ok);
      };
      if (!sagaSubmit(db, id, ZuMv(saga),
          [done](bool ok) mutable { if (!ok) done(false); }, done))
        done(false);
    });
  });
  return true;
}

#define ZUM_MIGRATE_PUT_IMPL(T, MEMBER, KIND) \
  bool put(DB *db, DBContext *context, Ztls::Random &rng, \
      T value, PutFn complete) \
  { \
    return context && put_(db, context->MEMBER, rng, \
      MigrationPut::Kind::KIND, ZuMv(value), ZuMv(complete)); \
  }

ZUM_MIGRATE_PUT_IMPL(App, apps, App)
ZUM_MIGRATE_PUT_IMPL(User, users, User)
ZUM_MIGRATE_PUT_IMPL(Cred, creds, Cred)
ZUM_MIGRATE_PUT_IMPL(Membership, memberships, Membership)
ZUM_MIGRATE_PUT_IMPL(Action, actions, Action)
ZUM_MIGRATE_PUT_IMPL(Role, roles, Role)
ZUM_MIGRATE_PUT_IMPL(Scope, scopes, Scope)
ZUM_MIGRATE_PUT_IMPL(Audience, audiences, Audience)
ZUM_MIGRATE_PUT_IMPL(Client, clients, Client)
ZUM_MIGRATE_PUT_IMPL(ClientAccess, clientAccess, ClientAccess)

#undef ZUM_MIGRATE_PUT_IMPL

} // namespace Zum::Migrate
