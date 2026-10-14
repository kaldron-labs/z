//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZfCf.hh>
#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZiLog.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZdbMemStore.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_saga_image.hh>
#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_saga_catalog_publish.hh>

using namespace ZuTestUtil;

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> dbConfig()
{
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store},\n"
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
    "}\n");
  return ZuMv(scan.p<1>());
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto test = static_cast<TestDB *>(db);
  test->requests->activate();
  test->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<TestDB *>(db)->requests->deactivate();
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{

  return ZmBlock<bool>{}([
    table, data = ZuMv(data)
  ](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [
	data = ZuMv(data), wake = ZuMv(wake)
      ](ZdbRow<T> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) T{ZuMv(data)};
	wake(row->commit());
      });
    });
  });
}

static bool runSaga(TestDB *db, Zum::SagaRef saga, ZdbSagaID id)
{
  ZmSemaphore completed;
  bool terminal = false;
  bool submitted = ZmBlock<bool>{}([
      db, id, saga = ZuMv(saga), &completed, &terminal
  ](auto wake) mutable {
    if (!Zum::sagaSubmit(db, id, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[&completed, &terminal](bool ok) {
	  terminal = ok;
	  completed.post();
	})) wake(false);
  });
  if (!submitted) return false;
  completed.wait();
  return terminal;
}

static void roleNames(TestDB *db, Zum::DB *context)
{
  ZuTestScope(roleNames);
  auto app = [context](Zum::AppID id) {
    return ZmBlock<Zum::App>{}([context, id](auto wake) mutable {
      context->apps->run(0, [context, id, wake = ZuMv(wake)]() mutable {
	context->apps->find<0>(0, ZuFwdTuple(id),
	  [wake = ZuMv(wake)](ZdbRowRef<Zum::App> row) mutable {
	    wake(row ? Zum::App{row->data()} : Zum::App{});
	  });
      });
    });
  };
  auto add = [](Zum::App snapshot, Zum::RoleID id, Zum::String name) {
    auto appID = snapshot.id;
    return Zum::RoleAddArgs{.app = ZuMv(snapshot),
      .before = Zum::Role{.appID = appID, .id = id, .version = 0},
      .values = Zum::Role{.appID = appID, .id = id, .name = ZuMv(name)},
      .updated = 101};
  };
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9600, .name = "role-names", .state = Zum::State::Active}));
  ZuCheck(runSaga(db, Zum::roleAdd(add(app(9600), 9610, "unique")), ZdbSagaID{96000}));
  auto snapshot = app(9600);
  ZuCheck(!snapshot.owner && snapshot.version == 2 && snapshot.authVersion == 2);
  ZuCheck(!runSaga(db, Zum::roleAdd(add(snapshot, 9611, "unique")), ZdbSagaID{96001}));
  ZuCheck(Zum::SagaImage::save(app(9600)) == Zum::SagaImage::save(snapshot));
  // Disabled and tombstoned identities continue to reserve their names.
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9600, .id = 9612, .name = "disabled", .state = Zum::State::Disabled}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9600, .id = 9613, .name = "retired", .tombstone = true}));
  ZuCheck(!runSaga(db, Zum::roleAdd(add(snapshot, 9614, "disabled")), ZdbSagaID{96002}));
  ZuCheck(!runSaga(db, Zum::roleAdd(add(snapshot, 9615, "retired")), ZdbSagaID{96003}));
  ZuCheck(Zum::SagaImage::save(app(9600)) == Zum::SagaImage::save(snapshot));
  // A catalog must also reject a name occupied by a custom role.
  Zum::CatalogPublishArgs catalog{.before = snapshot, .after = snapshot};
  ++catalog.after.version;
  ++catalog.after.authVersion;
  catalog.roles.add(Zum::Role{.appID = 9600, .id = 9616,
    .name = "unique", .origin = Zum::Origin::Standard});
  ZuCheck(!runSaga(db, Zum::catalogPublish(ZuMv(catalog)), ZdbSagaID{96004}));
  ZuCheck(Zum::SagaImage::save(app(9600)) == Zum::SagaImage::save(snapshot));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9601, .name = "role-name-race", .state = Zum::State::Active}));
  snapshot = app(9601);
  // Submit both creators before waiting; application ownership serializes them.
  ZmSemaphore completed;
  bool first = false, second = false;
  auto submit = [db, &completed](Zum::SagaRef saga, ZdbSagaID id, bool *result) {
    return ZmBlock<bool>{}([db, id, saga = ZuMv(saga), result, &completed](
	auto wake) mutable {
      if (!Zum::sagaSubmit(db, id, ZuMv(saga),
	  [wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	  [result, &completed](bool ok) { *result = ok; completed.post(); }))
	wake(false);
    });
  };
  ZuCheck(submit(Zum::roleAdd(add(snapshot, 9620, "race")), ZdbSagaID{96005}, &first));
  ZuCheck(submit(Zum::roleAdd(add(snapshot, 9621, "race")), ZdbSagaID{96006}, &second));
  completed.wait();
  completed.wait();
  ZuCheck(first != second);
  snapshot = app(9601);
  ZuCheck(!snapshot.owner && snapshot.version == 2 && snapshot.authVersion == 2);
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9602, .name = "role-other-app", .state = Zum::State::Active}));
  ZuCheck(runSaga(db, Zum::roleAdd(add(app(9602), 9630, "unique")), ZdbSagaID{96007}));
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9603, .name = "role-catalog-race", .state = Zum::State::Active}));
  snapshot = app(9603);
  catalog = Zum::CatalogPublishArgs{.before = snapshot, .after = snapshot};
  ++catalog.after.version;
  ++catalog.after.authVersion;
  catalog.roles.add(Zum::Role{.appID = 9603, .id = 9641,
    .name = "race", .origin = Zum::Origin::Standard});
  first = second = false;
  ZuCheck(submit(Zum::roleAdd(add(snapshot, 9640, "race")), ZdbSagaID{96008}, &first));
  ZuCheck(submit(Zum::catalogPublish(ZuMv(catalog)), ZdbSagaID{96009}, &second));
  completed.wait();
  completed.wait();
  ZuCheck(first != second);
  snapshot = app(9603);
  ZuCheck(!snapshot.owner && snapshot.version == 2 && snapshot.authVersion == 2);

}

static void runtimeRoles()
{
  ZuTestScope(runtimeRoles);
  ZiLog::init("ZumRuntimeRoleTest");
  ZiLog::level(0);
  ZiLog::start();
  auto config = dbConfig();
  auto configFields = ZfCf::handler<zdbtest::ConfigFields>(config).ctor();
  ZiMultiplex mx{ZvMxParams("mx", configFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  ZuCheck(db->requests->init(&mx, 5, 64));
  db->init(ZdbCf{configFields.zdb.p<const ZfCf::AnyNode *>()}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store);
  Zum::registerSchema(db);
  ZmRef<Zum::DB> context = db;
  ZuCheck(db->start());
  db->active.wait();
  roleNames(db, context);
  ZmSemaphore requestsDown;
  db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
  requestsDown.wait();
  ZuCheck(db->stop());
  context = {};
  db->final();
  db->requests = {};
  db = {};
  store = {};
  ZuCheck(mx.stop());
  ZiLog::stop();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(runtimeRoles);
  return 0;
}
