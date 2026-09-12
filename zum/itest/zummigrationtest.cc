//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZumDB.hh>
#include <zlib/ZumLegacy.hh>
#include <zlib/ZumMigrate.hh>

using namespace ZuTestUtil;

struct LegacyDB : public Zdb { ZmSemaphore active; };
struct CurrentDB : public Zum::DB { ZmSemaphore active; };

static void legacyUp(Zdb *db, ZdbHost *)
{
  static_cast<LegacyDB *>(db)->active.post();
}

static void currentUp(Zdb *db, ZdbHost *)
{
  static_cast<CurrentDB *>(db)->active.post();
}

static ZuPtr<const ZfCf::AnyNode> config(ZuCSpan connect)
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"},
    ZfCf::DefVal{::getenv("ZUM_TEST_MODULE")});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{connect});
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store, module: ${MODULE}, connection: ${CONNECT}},\n"
    "  hostID: self, hosts: {self: {standalone: true}}, tables: {}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 5, rxThread: rx, txThread: tx, threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: shard, isolated: true}\n"
    "  }\n"
    "}\n", {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

template <typename Table>
static bool insert(Table *table, typename Table::T value)
{
  using T = typename Table::T;
  return ZmBlock<bool>{}([table, value = ZuMv(value)](auto wake) mutable {
    table->run(0, [table, value = ZuMv(value), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(ZuMv(row), [value = ZuMv(value), wake = ZuMv(wake)](
          ZdbRow<T> *row) mutable {
        if (!row) { wake(false); return; }
        new (row->ptr()) T{ZuMv(value)};
        wake(bool(row->commit()));
      });
    });
  });
}

template <typename Table>
static bool insert(ZmRef<Table> table, typename Table::T value)
{
  return insert(table.ptr(), ZuMv(value));
}

template <typename Table, typename Key>
static bool find(Table *table, Key key, typename Table::T &value)
{
  using T = typename Table::T;
  return ZmBlock<bool>{}([table, key = ZuMv(key), &value](auto wake) mutable {
    table->run(0, [table, key = ZuMv(key), &value,
      wake = ZuMv(wake)]() mutable {
      table->template find<0>(0, ZuMv(key), [&value, wake = ZuMv(wake)](
          ZdbRowRef<T> row) mutable {
        if (!row) { wake(false); return; }
        value = row->data();
        wake(true);
      });
    });
  });
}

struct LegacyStore {
  ZuPtr<ZiMultiplex>	mx;
  ZmRef<LegacyDB>	db;
  ZmRef<Zum::Legacy::DBContext> context;

  bool start(ZuCSpan connect)
  {
    auto cf = config(connect);
    if (!cf) return false;
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};
    db = new LegacyDB{};
    db->init(ZdbCf{cf->resolve("zdb")}, mx.ptr(),
      ZdbHandler{.upFn = legacyUp});
    context = Zum::Legacy::registerSchema(db);
    if (!mx->start() || !db->start()) return false;
    db->active.wait();
    return true;
  }

  bool stop()
  {
    bool ok = db->stop();
    mx->stop();
    context = {};
    db->final();
    db = {};
    mx = {};
    return ok;
  }
};

struct CurrentStore {
  ZuPtr<ZiMultiplex>	mx;
  ZmRef<CurrentDB>	db;
  ZmRef<Zum::DBContext>	context;

  bool start(ZuCSpan connect)
  {
    auto cf = config(connect);
    if (!cf) return false;
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};
    db = new CurrentDB{};
    db->init(ZdbCf{cf->resolve("zdb")}, mx.ptr(),
      ZdbHandler{.upFn = currentUp});
    context = Zum::registerSchema(db);
    if (!mx->start() || !db->start()) return false;
    db->active.wait();
    return true;
  }

  bool stop()
  {
    bool ok = db->stop();
    mx->stop();
    context = {};
    db->final();
    db = {};
    mx = {};
    return ok;
  }
};

static Zum::Migrate::Plan plan()
{
  using namespace Zum;
  using namespace Zum::Migrate;
  Plan p{.issuer = "https://iam.example", .coreAppID = 900,
    .initialUserID = 901, .initialClientID = "zum-admin", .time = 10};
  p.apps.push(AppMap{.id = 10, .name = "alpha", .label = "Alpha"});
  p.apps.push(AppMap{.id = 20, .name = "beta", .label = "Beta"});
  p.actions.push(ActionMap{.oldID = 1, .appID = 10, .id = 5});
  p.actions.push(ActionMap{.oldID = 2, .appID = 20, .id = 6});
  p.roles.push(Migrate::RoleMap{.oldID = 100, .appID = 10, .id = 7});
  p.roles.push(Migrate::RoleMap{.oldID = 200, .appID = 20, .id = 8});
  p.audiences.push(AudienceMap{
    .uri = "https://alpha.example", .appID = 10, .id = 11,
    .name = "alpha"});
  p.audiences.push(AudienceMap{
    .uri = "https://beta.example", .appID = 20, .id = 12,
    .name = "beta"});
  p.scopes.push(ScopeMap{.oldID = 1000, .appID = 10, .id = 13,
    .audienceID = 11, .catalogRoleIDs = {7}});
  p.scopes.push(ScopeMap{.oldID = 2000, .appID = 20, .id = 14,
    .audienceID = 12, .catalogRoleIDs = {8}});
  p.clients.push(ClientMap{.oldID = "legacy-client", .appID = 10,
    .id = "client", .label = "Migrated client",
    .authMethod = ClientAuthMethod::ClientSecretBasic,
    .identityScopes = {"openid", "profile"}});
  p.discardExternalUsers = {43};
  return p;
}

static bool populate(ZuCSpan connect)
{
  using namespace Zum;
  LegacyStore store;
  if (!store.start(connect)) return false;
  auto context = store.context;
  ZtBitmap alphaActions{3};
  alphaActions.set(1);
  ZtBitmap betaActions{3};
  betaActions.set(2);
  bool ok =
    insert(context->issuers, Legacy::Issuer{
      .id = "https://iam.example", .nextActionID = 3,
      .authVersion = 4, .nextAuditID = 2}) &&
    insert(context->actions, Legacy::Action{.id = 1, .name = "read"}) &&
    insert(context->actions, Legacy::Action{.id = 2, .name = "write"}) &&
    insert(context->roles, Legacy::Role{
      .id = 100, .name = "reader", .actions = ZuMv(alphaActions)}) &&
    insert(context->roles, Legacy::Role{
      .id = 200, .name = "writer", .actions = ZuMv(betaActions)}) &&
    insert(context->scopes, Legacy::Scope{.id = 1000,
      .audience = "https://alpha.example", .name = "read",
      .roleIDs = {100}}) &&
    insert(context->scopes, Legacy::Scope{.id = 2000,
      .audience = "https://beta.example", .name = "write",
      .roleIDs = {200}}) &&
    insert(context->users, Legacy::User{.id = 42, .name = "local",
      .handle = Bytes{ZuBSpan{"local-handle"}}, .roleIDs = {100, 200},
      .created = 10, .updated = 11, .state = State::Active,
      .authVersion = 3}) &&
    insert(context->users, Legacy::User{.id = 43, .name = "external",
      .roleIDs = {100}, .created = 10, .updated = 11,
      .state = State::Active, .authVersion = 2, .oidcSub = "subject"}) &&
    insert(context->creds, Legacy::Cred{
      .id = Bytes{ZuBSpan{"local-cred"}}, .userID = 42,
      .publicKey = Bytes{ZuBSpan{"local-key"}}, .created = 10,
      .updated = 11, .state = State::Active}) &&
    insert(context->creds, Legacy::Cred{
      .id = Bytes{ZuBSpan{"external-cred"}}, .userID = 43,
      .publicKey = Bytes{ZuBSpan{"external-key"}}, .created = 10,
      .updated = 11, .state = State::Active}) &&
    insert(context->clients, Legacy::Client{.id = "legacy-client",
      .secretDigest = Bytes{ZuBSpan{"digest"}},
      .redirects = {"http://127.0.0.1/callback"},
      .audiences = {"https://alpha.example", "https://beta.example"},
      .scopeIDs = {1000, 2000}, .roleIDs = {100, 200},
      .created = 10, .updated = 11, .type = ClientType::Confidential,
      .grants = uint8_t(ClientGrant::AuthorizationCode |
        ClientGrant::RefreshToken), .state = State::Active}) &&
    insert(context->grants, Legacy::Grant{
      .id = Bytes{ZuBSpan{"legacy-grant"}}}) &&
    insert(context->signKeys, Legacy::SignKey{
      .id = "legacy-key", .publicJwk = "{}", .state = State::Active}) &&
    insert(context->audits, Legacy::Audit{.id = 1, .time = 10,
      .issuer = "https://iam.example", .actor = "local"});
  return store.stop() && ok;
}

static bool stageInterrupted(ZuCSpan connect, ZuBSpan dbKey)
{
  using namespace Zum;
  using namespace Zum::Migrate;
  CurrentStore store;
  if (!store.start(connect)) return false;
  Ztls::Random rng;
  if (!rng.init()) { store.stop(); return false; }
  auto p = plan();
  Mapper mapper;
  String error;
  App alpha, beta;
  if (!mapper.init(p, error) || !mapper.app(p.apps[0], p.time, alpha) ||
      !mapper.app(p.apps[1], p.time, beta)) {
    store.stop();
    return false;
  }
  bool ok = ZmBlock<bool>{}([
    &store, &rng, &p, dbKey
  ](auto wake) mutable {
    Migrate::start(store.db, store.context, rng, p, dbKey,
      [wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
  });
  if (ok)
    ok = ZmBlock<bool>{}([
      &store, &rng, &alpha
    ](auto wake) mutable {
      Migrate::put(store.db, store.context, rng, ZuMv(alpha),
        [wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
    });
  if (ok) {
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(MigrationPut{.kind = MigrationPut::Kind::App,
      .image = SagaImage::save(beta)});
    Zdb_::SagaPayload payload;
    MSaga::save(saga, payload);
    auto table = ZmBlock<ZmRef<ZdbTable<Zdb_::SagaData>>>{}([
      db = store.db](auto wake) mutable {
        db->run([db, wake = ZuMv(wake)]() mutable {
          wake(static_cast<ZdbTable<Zdb_::SagaData> *>(
            db->table("saga").ptr()));
        });
      });
    ok = insert(table, Zdb_::SagaData{
      .type = MigrationPut::Type{}(), .id = ZdbSagaID{12345}, .shard = 0,
      .data = ZuMv(payload)});
  }
  return store.stop() && ok;
}

static bool writePlan(ZuCSpan path)
{
  ZtString<> json;
  ZfJSON::save(json, plan());
  ZiFile file;
  if (file.open(Zi::Path{path}, ZiFile::Write, 0600) != Zi::OK) return false;
  bool ok = file.write(json.data(), json.length()) == Zi::OK;
  file.close();
  return ok;
}

static bool runZumd(
    ZuCSpan sourceConnect, ZuCSpan targetConnect, ZuCSpan mapping,
    ZuCSpan output, ZuCSpan log, ZuCSpan encodedKey, bool migrate)
{
  ZtString<> sourceOption, targetOption, mappingOption, outputOption;
  ZtString<> moduleOption;
  moduleOption << "--module=" << ::getenv("ZUM_TEST_MODULE");
  sourceOption << "--source-connect=" << sourceConnect;
  targetOption << "--connect=" << targetConnect;
  mappingOption << "--migrate=" << mapping;
  outputOption << "--bootstrap-output=" << output;
  pid_t pid = fork();
  if (!pid) {
    int fd = open(ZtString<>{log}.data(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
    setenv("ZUM_DB_KEY", ZtString<>{encodedKey}.data(), 1);
    if (migrate) {
      char *argv[] = {
        const_cast<char *>("../src/zumd"),
        const_cast<char *>(moduleOption.data()),
        const_cast<char *>(mappingOption.data()),
        const_cast<char *>(sourceOption.data()),
        const_cast<char *>(targetOption.data()), nullptr};
      execv(argv[0], argv);
    } else {
      char *argv[] = {
        const_cast<char *>("../src/zumd"),
        const_cast<char *>(moduleOption.data()),
        const_cast<char *>(targetOption.data()),
        const_cast<char *>("--issuer=https://iam.example"),
        const_cast<char *>("--admin=admin@example.com"),
        const_cast<char *>(outputOption.data()),
        const_cast<char *>("--once"), nullptr};
      execv(argv[0], argv);
    }
    _exit(127);
  }
  if (pid < 0) return false;
  int status;
  while (waitpid(pid, &status, 0) < 0)
    if (errno != EINTR) return false;
  bool ok = WIFEXITED(status) && !WEXITSTATUS(status);
  if (!ok) {
    ZiFile file;
    if (file.open(Zi::Path{log}, ZiFile::ReadOnly) == Zi::OK) {
      auto length = file.size();
      if (length > 0 && length < (1U << 20)) {
        ZtString<> diagnostic;
        diagnostic.length(unsigned(length));
        if (file.read(diagnostic.data(), diagnostic.length()) ==
            int(diagnostic.length()))
          std::cerr << diagnostic;
      }
      file.close();
    }
  }
  return ok;
}

static bool verifyMigrated(ZuCSpan connect, int phase)
{
  using namespace Zum;
  CurrentStore store;
  if (!store.start(connect)) return false;
  auto context = store.context;
  Issuer issuer;
  App alpha, beta, core;
  User local, external, admin;
  Cred localCred, externalCred;
  Membership alphaMember, betaMember;
  Action alphaAction, betaAction;
  Role alphaRole, betaRole;
  Scope alphaScope, betaScope;
  Client client;
  ClientAccess alphaAccess, betaAccess;
  bool ok =
    find(context->issuers, ZuFwdTuple("https://iam.example"), issuer) &&
    issuer.schemaVersion == SchemaVersion &&
    issuer.bootstrapPhase == phase && issuer.coreAppID == 900 &&
    issuer.initialUserID == 901 && issuer.initialClientID == "zum-admin" &&
    find(context->apps, ZuFwdTuple(AppID{10}), alpha) &&
    find(context->apps, ZuFwdTuple(AppID{20}), beta) &&
    alpha.nextActionID == 6 && beta.nextActionID == 7 &&
    find(context->users, ZuFwdTuple(UserID{42}), local) &&
    local.source == UserSource::Local &&
    !find(context->users, ZuFwdTuple(UserID{43}), external) &&
    find(context->creds, ZuFwdTuple(Bytes{ZuBSpan{"local-cred"}}),
      localCred) &&
    !find(context->creds,
      ZuFwdTuple(Bytes{ZuBSpan{"external-cred"}}), externalCred) &&
    find(context->memberships, ZuFwdTuple(AppID{10}, UserID{42}),
      alphaMember) && alphaMember.roleIDs == IDVec{7} &&
    find(context->memberships, ZuFwdTuple(AppID{20}, UserID{42}),
      betaMember) && betaMember.roleIDs == IDVec{8} &&
    find(context->actions, ZuFwdTuple(AppID{10}, ActionID{5}),
      alphaAction) && alphaAction.name == "read" &&
    find(context->actions, ZuFwdTuple(AppID{20}, ActionID{6}),
      betaAction) && betaAction.name == "write" &&
    find(context->roles, ZuFwdTuple(AppID{10}, RoleID{7}), alphaRole) &&
    alphaRole.actions.get(5) &&
    find(context->roles, ZuFwdTuple(AppID{20}, RoleID{8}), betaRole) &&
    betaRole.actions.get(6) &&
    find(context->scopes, ZuFwdTuple(AppID{10}, ScopeID{13}),
      alphaScope) && alphaScope.catalogRoleIDs == IDVec{7} &&
    find(context->scopes, ZuFwdTuple(AppID{20}, ScopeID{14}),
      betaScope) && betaScope.catalogRoleIDs == IDVec{8} &&
    find(context->clients, ZuFwdTuple("client"), client) &&
    client.appID == 10 && client.secretDigest == Bytes{ZuBSpan{"digest"}} &&
    find(context->clientAccess, ZuFwdTuple("client", AppID{10}),
      alphaAccess) && alphaAccess.roleIDs == IDVec{7} &&
    find(context->clientAccess, ZuFwdTuple("client", AppID{20}),
      betaAccess) && betaAccess.roleIDs == IDVec{8};
  if (phase == BootstrapPhase::AdminPending)
    ok = ok && find(context->apps, ZuFwdTuple(AppID{900}), core) &&
      find(context->users, ZuFwdTuple(UserID{901}), admin) &&
      admin.source == UserSource::Local;
  return store.stop() && ok;
}

static void migration()
{
  ZuTestScope(migration);
  ZuCSpan sourceConnect = ::getenv("ZUM_MIGRATION_SOURCE_CONNECT");
  ZuCSpan targetConnect = ::getenv("ZUM_MIGRATION_TARGET_CONNECT");
  char temp[] = "/tmp/zummigrationtest.XXXXXX";
  char *dir = mkdtemp(temp);
  ZuCheck(dir);
  if (!dir) return;
  ZtString<> mapping, output, log;
  mapping << dir << "/mapping.json";
  output << dir << "/bootstrap";
  log << dir << "/zumd.log";

  Zum::Bytes key;
  key.length(32, false);
  for (unsigned i = 0; i < key.length(); ++i) key[i] = uint8_t(i + 1);
  ZtString<> encoded;
  encoded.length(ZuBase64::enclen(key.length()));
  encoded.length(ZuBase64::encode(encoded.span(), key));

  ZuCheck(populate(sourceConnect));
  ZuCheck(writePlan(mapping));
  ZuCheck(stageInterrupted(targetConnect, key));
  ZuCheck(runZumd(sourceConnect, targetConnect, mapping, output, log,
    encoded, true));
  ZuCheck(verifyMigrated(targetConnect, Zum::BootstrapPhase::Empty));
  ZuCheck(runZumd(sourceConnect, targetConnect, mapping, output, log,
    encoded, true));
  ZuCheck(verifyMigrated(targetConnect, Zum::BootstrapPhase::Empty));
  ZuCheck(runZumd({}, targetConnect, {}, output, log, encoded, false));
  ZuCheck(verifyMigrated(targetConnect, Zum::BootstrapPhase::AdminPending));
  ZuCheck(runZumd({}, targetConnect, {}, output, log, encoded, false));
  ZuCheck(verifyMigrated(targetConnect, Zum::BootstrapPhase::AdminPending));

  ZuClear(key.data(), key.length());
  ZuClear(encoded.data(), encoded.length());
  unlink(mapping.data());
  unlink(output.data());
  unlink(log.data());
  rmdir(dir);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  if (!::getenv("ZUM_TEST_MODULE") ||
      !::getenv("ZUM_MIGRATION_SOURCE_CONNECT") ||
      !::getenv("ZUM_MIGRATION_TARGET_CONNECT")) {
    std::cerr << "set ZUM_TEST_MODULE, ZUM_MIGRATION_SOURCE_CONNECT and "
      "ZUM_MIGRATION_TARGET_CONNECT\n";
    return 1;
  }
  ZuTestCall(migration);
  return 0;
}
