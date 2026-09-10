//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <stdlib.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZumDB.hh>
#include <zlib/ZumRequest.hh>

using namespace ZuTestUtil;

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> config()
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"},
    ZfCf::DefVal{::getenv("ZUM_TEST_MODULE")});
  defines->add(ZfCf::DefKey{"CONNECT"},
    ZfCf::DefVal{::getenv("ZUM_TEST_CONNECT")});
  Zum::String source{
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
    "}\n"};
  auto scan = ZfCf::scan(source, {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto *test = static_cast<TestDB *>(db);
  test->requests->activate();
  test->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<TestDB *>(db)->requests->deactivate();
}

static ZmRef<TestDB> startDB(
    const ZfCf::AnyNode *cf, ZiMultiplex &mx, ZmRef<Zum::DBContext> &context)
{
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  if (!db->requests->init(&mx, 5, 8)) return {};
  db->init(ZdbCf{cf->resolve("zdb")}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown});
  context = Zum::registerSchema(db);
  if (!db->start()) return {};
  db->active.wait();
  return db;
}

static bool stopDB(ZmRef<TestDB> &db, ZmRef<Zum::DBContext> &context)
{
  ZmSemaphore drained;
  db->requests->deactivate([&drained]() { drained.post(); });
  drained.wait();
  // stop() waits for store queue/pipeline completion and shard callback drain.
  // Only after it returns may final() release tables and a new DB be opened.
  bool ok = db->stop();
  context = {};
  db->final();
  db = {};
  return ok;
}

static bool insertIssuer(Zum::DBContext *context)
{
  return ZmBlock<bool>{}([context](auto wake) mutable {
    context->issuers->run(0, [context, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<Zum::Issuer> row =
	new ZdbRow<Zum::Issuer>{context->issuers, ZdbShard{0}};
      context->issuers->insert(row, [wake = ZuMv(wake)](
	  ZdbRow<Zum::Issuer> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) Zum::Issuer{.id = Zum::String{"issuer"}};
	wake(row->commit());
      });
    });
  });
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{

  return ZmBlock<bool>{}([table, data = ZuMv(data)](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [data = ZuMv(data), wake = ZuMv(wake)](
	  ZdbRow<T> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) T{ZuMv(data)};
	wake(bool(row->commit()));
      });
    });
  });
}

static bool stageEnrollment(Zum::DB *db, Zum::DBContext *context)
{
  Zum::Bytes ceremonyID{ZuBSpan{"enroll-ceremony!"}};
  if (!insertRecord(context->grants, Zum::Grant{
      .id = ceremonyID,
      .created = 100,
      .expires = 200,
      .kind = Zum::GrantKind::Ceremony,
      .purpose = Zum::GrantPurpose::Enrollment,
      .state = Zum::State::Active,
      .issuer = "issuer",
      .challenge = Zum::Bytes{ZuBSpan{"challenge"}},
      .bindingDigest = Zum::Bytes{ZuBSpan{"binding"}},
      .userName = "recovered user",
      .userHandle = Zum::Bytes{ZuBSpan{"recovered handle"}}
    })) return false;

  using M = Zum::MSaga;
  ZmRef<M> saga = new M{};
  saga->init(Zum::Enrollment{
    .ceremonyID = ZuMv(ceremonyID),
    .userID = 42,
    .name = "recovered user",
    .handle = Zum::Bytes{ZuBSpan{"recovered handle"}},
    .credentialID = Zum::Bytes{ZuBSpan{"recovered credential"}},
    .publicKey = Zum::Bytes{ZuBSpan{"public key"}},
    .created = 100,
    .label = "passkey"
  });
  Zdb_::SagaPayload payload;
  M::save(saga, payload);
  return ZmBlock<bool>{}([
    db, payload = ZuMv(payload)
  ](auto wake) mutable {
    db->run([db, payload = ZuMv(payload), wake = ZuMv(wake)]() mutable {
      auto table = static_cast<ZdbTable<Zdb_::SagaData> *>(
	db->table("saga").ptr());
      table->run(0, [table, payload = ZuMv(payload),
	  wake = ZuMv(wake)]() mutable {
	ZdbRowRef<Zdb_::SagaData> row =
	  new ZdbRow<Zdb_::SagaData>{table, ZdbShard{0}};
	table->insert(row, [payload = ZuMv(payload), wake = ZuMv(wake)](
	    ZdbRow<Zdb_::SagaData> *row) mutable {
	  if (!row) { wake(false); return; }
	  new (row->ptr()) Zdb_::SagaData{
	    .type = Zum::Enrollment::Type{}(),
	    .id = ZdbSagaID{42},
	    .shard = 0,
	    .data = ZuMv(payload)
	  };
	  wake(bool(row->commit()));
	});
      });
    });
  });
}

static bool stageAppEnrollment(
    Zum::DB *db, Zum::AppID appID, Zum::AudienceID audienceID,
    ZuCSpan clientID, ZdbSagaID sagaID, Zum::ActionID catalogPublishOp,
    Zum::ActionID operationQueryOp)
{
  Zum::AppEnrollment enrollment{.coreAppID = 1, .appID = appID,
    .appName = Zum::String{clientID}, .appLabel = "Orders",
    .audienceID = audienceID,
    .audienceURI = Zum::String{"https://orders.example/"} << appID,
    .clientID = Zum::String{clientID},
    .secretDigest = Zum::Bytes{ZuBSpan{"verifier"}},
    .clientType = Zum::ClientType::Confidential, .nativeService = true,
    .created = 100, .catalogPublishOp = catalogPublishOp,
    .operationQueryOp = operationQueryOp};
  ZmRef<Zum::MSaga> saga = new Zum::MSaga{};
  saga->init(ZuMv(enrollment));
  Zdb_::SagaPayload payload;
  Zum::MSaga::save(saga, payload);
  return ZmBlock<bool>{}([
    db, sagaID, payload = ZuMv(payload)
  ](auto wake) mutable {
    db->run([db, sagaID, payload = ZuMv(payload),
        wake = ZuMv(wake)]() mutable {
      auto table = static_cast<ZdbTable<Zdb_::SagaData> *>(
	db->table("saga").ptr());
      table->run(0, [table, sagaID, payload = ZuMv(payload),
	  wake = ZuMv(wake)]() mutable {
	ZdbRowRef<Zdb_::SagaData> row =
	  new ZdbRow<Zdb_::SagaData>{table, ZdbShard{0}};
	table->insert(row, [sagaID, payload = ZuMv(payload), wake = ZuMv(wake)](
	    ZdbRow<Zdb_::SagaData> *row) mutable {
	  if (!row) { wake(false); return; }
	  new (row->ptr()) Zdb_::SagaData{
	    .type = Zum::AppEnrollment::Type{}(),
	    .id = sagaID, .shard = 0, .data = ZuMv(payload)};
	  wake(bool(row->commit()));
	});
      });
    });
  });
}

struct AppRollbackState {
  Zum::App		app;
  Zum::Audience	audience;
  Zum::Client		client;
};

// Test-only durable images at each intent/effect boundary. No production
// callbacks are bypassed during recovery: startup runs the real AppActionAdd.
template <typename T>
static ZmRef<ZdbTable<T>> internalTable(Zum::DB *db, ZuCSpan name)
{
  return ZmBlock<ZmRef<ZdbTable<T>>>{}([db, name](auto wake) mutable {
    db->run([db, name, wake = ZuMv(wake)]() mutable {
      wake(static_cast<ZdbTable<T> *>(db->table(name).ptr()));
    });
  });
}

template <typename Table, typename Key>
static typename Table::T record(Table *table, Key key)
{
  using T = typename Table::T;
  return ZmBlock<T>{}([table, key = ZuMv(key)](auto wake) mutable {
    table->run(0, [table, key = ZuMv(key), wake = ZuMv(wake)]() mutable {
      table->template find<0>(0, ZuMv(key), [wake = ZuMv(wake)](
	  ZdbRowRef<T> row) mutable { wake(row ? T{row->data()} : T{}); });
    });
  });
}

static bool stageActionAdd(
    Zum::DB *db, Zum::DBContext *context, const Zum::App &app,
    ZdbSagaID sagaID, ZuCSpan name, unsigned cut)
{
  Zum::AppActionAdd add{.appID = app.id, .actionID = app.nextActionID,
    .name = Zum::String{name}, .label = "Recovered action", .created = 200,
    .oldAppVersion = app.version, .oldAuthVersion = app.authVersion,
    .oldUpdated = app.updated};
  ZmRef<Zum::MSaga> saga = new Zum::MSaga{};
  saga->init(ZuMv(add));
  Zdb_::SagaPayload payload;
  Zum::MSaga::save(saga, payload);
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::AppActionAdd::Type{}(), .id = sagaID,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < Zum::AppActionAdd::NSteps; ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *table = step == 0 || step == 3 ?
      static_cast<Zdb_::AnyTable *>(context->apps) :
      static_cast<Zdb_::AnyTable *>(context->actions);
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
	.type = Zum::AppActionAdd::Type{}(), .id = sagaID,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    if (step == 0 || step == 3) {
      if (!ZmBlock<bool>{}([context, &app, sagaID, step](auto wake) mutable {
	context->apps->run(0, [context, &app, sagaID, step,
	    wake = ZuMv(wake)]() mutable {
	  context->apps->findUpd<0>(0, ZuFwdTuple(app.id), [
	      &app, sagaID, step, wake = ZuMv(wake)](
	      ZdbRow<Zum::App> *row) mutable {
	    if (!row) { wake(false); return; }
	    if (!step) {
	      row->data().nextActionID = app.nextActionID + 1;
	      row->data().version = app.version + 1;
	      row->data().authVersion = app.authVersion + 1;
	      row->data().updated = 200;
	      row->data().owner = sagaID;
	    } else row->data().owner = 0;
	    wake(bool(row->commit()));
	  });
	});
      })) return false;
    } else if (step == 1) {
      if (!insertRecord(context->actions, Zum::Action{
	  .appID = app.id, .id = app.nextActionID, .name = Zum::String{name},
	  .label = "Recovered action", .state = Zum::State::Active,
	  .origin = Zum::Origin::Custom, .version = 1,
	  .created = 200, .updated = 200, .owner = sagaID})) return false;
    } else {
      if (!ZmBlock<bool>{}([context, &app](auto wake) mutable {
	context->actions->run(0, [context, &app, wake = ZuMv(wake)]() mutable {
	  context->actions->findUpd<0>(0, ZuFwdTuple(app.id, app.nextActionID),
	    [wake = ZuMv(wake)](ZdbRow<Zum::Action> *row) mutable {
	      if (!row) { wake(false); return; }
	      row->data().owner = 0;
	      wake(bool(row->commit()));
	    });
	});
      })) return false;
    }
  }
  return true;
}

static bool sagaEmpty(Zum::DB *db)
{
  return ZmBlock<bool>{}([db](auto wake) mutable {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(!db->table("saga")->count() &&
	!db->table("saga_step")->count());
    });
  });
}

static AppRollbackState appRollbackState(Zum::DBContext *context)
{
  return ZmBlock<AppRollbackState>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{19}), [context,
	  wake = ZuMv(wake)](ZdbRowRef<Zum::App> app) mutable {
	context->audiences->find<0>(0, ZuFwdTuple(Zum::AudienceID{20}), [
	  context, app = ZuMv(app), wake = ZuMv(wake)
	](ZdbRowRef<Zum::Audience> audience) mutable {
	  context->clients->find<0>(0, ZuFwdTuple(ZuCSpan{"svc_failed"}), [
	    app = ZuMv(app), audience = ZuMv(audience), wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Client> client) mutable {
	    wake(AppRollbackState{
	      app ? Zum::App{app->data()} : Zum::App{},
	      audience ? Zum::Audience{audience->data()} : Zum::Audience{},
	      client ? Zum::Client{client->data()} : Zum::Client{}});
	  });
	});
      });
    });
  });
}

static bool enrollmentRecovered(Zum::DBContext *context)
{
  return ZmBlock<bool>{}([context](auto wake) mutable {
    context->users->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(Zum::UserID{42}), [
	context, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::User> user) mutable {
	context->creds->find<0>(0,
	  ZuFwdTuple(ZuBSpan{"recovered credential"}), [
	    user = ZuMv(user), wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Cred> cred) mutable {
	    wake(user && user->data().state == Zum::State::Active &&
	      !user->data().owner && cred &&
	      cred->data().state == Zum::State::Active &&
	      !cred->data().owner && cred->data().userID == 42);
	  });
      });
    });
  });
}

static bool appEnrollmentRecovered(Zum::DBContext *context)
{
  return ZmBlock<bool>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9}), [context,
	  wake = ZuMv(wake)](ZdbRowRef<Zum::App> app) mutable {
	context->audiences->find<0>(0, ZuFwdTuple(Zum::AudienceID{10}), [
	  context, app = ZuMv(app), wake = ZuMv(wake)
	](ZdbRowRef<Zum::Audience> audience) mutable {
	  context->clients->find<0>(0, ZuFwdTuple(ZuCSpan{"svc_orders"}), [
	    context, app = ZuMv(app), audience = ZuMv(audience),
	    wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Client> client) mutable {
	    context->clientAccess->find<0>(0,
	      ZuFwdTuple(ZuCSpan{"svc_orders"}, Zum::AppID{1}), [context,
	      app = ZuMv(app), audience = ZuMv(audience),
	      client = ZuMv(client), wake = ZuMv(wake)
	    ](ZdbRowRef<Zum::ClientAccess> clientAccess) mutable {
	      context->adminAccess->find<0>(0,
		ZuFwdTuple(Zum::ActorKind::Client,
		  ZuCSpan{"svc_orders"}, Zum::AppID{9}), [context,
		app = ZuMv(app), audience = ZuMv(audience),
		client = ZuMv(client), clientAccess = ZuMv(clientAccess),
		wake = ZuMv(wake)
	      ](ZdbRowRef<Zum::AdminAccess> adminAccess) mutable {
		context->authPolicies->find<0>(0, ZuFwdTuple(Zum::AppID{9}), [
		  app = ZuMv(app), audience = ZuMv(audience),
		  client = ZuMv(client), clientAccess = ZuMv(clientAccess),
		  adminAccess = ZuMv(adminAccess), wake = ZuMv(wake)
		](ZdbRowRef<Zum::AuthPolicy> policy) mutable {
		  wake(app && app->data().state == Zum::State::Active &&
		    app->data().version == 2 && !app->data().owner &&
		    audience && audience->data().appID == 9 &&
		    !audience->data().owner && client &&
		    client->data().appID == 9 && !client->data().owner &&
		    client->data().secretDigest == ZuBSpan{"verifier"} &&
		    clientAccess && !clientAccess->data().owner &&
		    clientAccess->data().appID == 1 && adminAccess &&
		    !adminAccess->data().owner && adminAccess->data().appID == 9 &&
		    policy && !policy->data().owner && policy->data().appID == 9);
		});
	      });
	    });
	  });
	});
      });
    });
  });
}

static bool createAction(
    Zum::DBContext *context, ZuCSpan name, Zum::ActionID &id)
{
  return ZmBlock<bool>{}([context, name, &id](auto wake) mutable {
    Zum::actionCreate(context, Zum::String{"issuer"}, Zum::String{name}, [
      &id, wake = ZuMv(wake)
    ](bool ok, Zum::ActionID value) mutable {
      if (ok) id = value;
      wake(ok);
    });
  });
}

struct State {
  Zum::Issuer	issuer;
  Zum::Action	action;
};

static State loadState(Zum::DBContext *context, Zum::ActionID id)
{
  return ZmBlock<State>{}([context, id](auto wake) mutable {
    context->issuers->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->issuers->find<0>(0, ZuFwdTuple(ZuCSpan{"issuer"}), [
	context, id, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Issuer> issuer) mutable {
	context->actions->find<0>(0, ZuFwdTuple(Zum::AppID{0}, id), [
	  issuer = ZuMv(issuer), wake = ZuMv(wake)
	](ZdbRowRef<Zum::Action> action) mutable {
	  wake(State{
	    issuer ? Zum::Issuer{issuer->data()} : Zum::Issuer{},
	    action ? Zum::Action{action->data()} : Zum::Action{}});
	});
      });
    });
  });
}

static void restart()
{
  ZuTestScope(restart);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<Zum::DBContext> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(insertIssuer(context));
  Zum::ActionID first = 0;
  ZuCheck(createAction(context, "orders.read", first));
  auto state = loadState(context, first);
  ZuCheck(state.issuer.nextActionID == first + 1 &&
    state.action.id == first && state.action.name == "orders.read");
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  state = loadState(context, first);
  ZuCheck(state.issuer.nextActionID == first + 1 &&
    state.action.id == first && state.action.name == "orders.read");
  Zum::ActionID second = 0;
  ZuCheck(createAction(context, "orders.write", second));
  ZuCheck(second == first + 1 &&
    loadState(context, second).issuer.nextActionID == second + 1);
  ZuCheck(stopDB(db, context));

  ZuCheck(mx.stop());
}

static void grantUpdate()
{
  ZuTestScope(grantUpdate);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<Zum::DBContext> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  constexpr ZuCSpan id{"grant-update-001"};
  ZuCheck(insertRecord(context->grants, Zum::Grant{
    .id = Zum::Bytes{ZuBSpan{id}}, .roleIDs = Zum::IDVec{1},
    .scope = "before"}));
  ZuCheck(ZmBlock<bool>{}([context, id](auto wake) mutable {
    auto grants = context->grants;
    grants->run(0, [grants, id, wake = ZuMv(wake)]() mutable {
      grants->findUpd<0>(0, ZuFwdTuple(ZuBSpan{id}),
        [wake = ZuMv(wake)](ZdbRow<Zum::Grant> *row) mutable {
        if (!row) { wake(false); return; }
        row->data().roleIDs = Zum::IDVec{2, 3};
        row->data().scope = "after";
        row->data().authoritySource = Zum::UserSource::External;
        row->data().authorityProviderID = 8;
        wake(row->commit());
      });
    });
  }));
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto grant = record(context->grants, ZuFwdTuple(ZuBSpan{id}));
  ZuCheck(grant.id == ZuBSpan{id} && grant.scope == "after" &&
    grant.roleIDs.length() == 2 && grant.roleIDs[0] == 2 &&
    grant.roleIDs[1] == 3 &&
    grant.authoritySource == Zum::UserSource::External &&
    grant.authorityProviderID == 8);
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
}

static void sagaRecovery()
{
  ZuTestScope(sagaRecovery);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<Zum::DBContext> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(stageEnrollment(db, context));
  ZuCheck(stageAppEnrollment(
    db, 9, 10, "svc_orders", ZdbSagaID{43}, 69, 1));
  // Equal operation IDs are a corrupt enrollment payload. It fails after the
  // app, audience, client, and client-access inserts and must roll them back.
  ZuCheck(stageAppEnrollment(
    db, 19, 20, "svc_failed", ZdbSagaID{44}, 1, 1));
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(db->requests->active());
  ZuCheck(enrollmentRecovered(context));
  ZuCheck(appEnrollmentRecovered(context));
  auto rollback = appRollbackState(context);
  ZuCheck(!rollback.app.id);
  ZuCheck(!rollback.audience.id);
  ZuCheck(!rollback.client.id);
  ZuCheck(sagaEmpty(db));

  // Every step has an intent-only image and an effect-committed image, plus
  // the initial durable payload. Reconstruct the DB at each boundary.
  for (unsigned cut = 0; cut <= 2 * Zum::AppActionAdd::NSteps; ++cut) {
    ZuTestRepeat(actionRecovery, 2 * Zum::AppActionAdd::NSteps + 1);
    auto app = record(context->apps, ZuFwdTuple(Zum::AppID{9}));
    Zum::String name{"cut"};
    name << cut;
    ZuCheck(stageActionAdd(db, context, app, ZdbSagaID{100 + cut}, name, cut));
    ZuCheck(stopDB(db, context));
    db = startDB(cf, mx, context);
    ZuCheck(bool(db));
    if (!db) return;
    auto next = record(context->apps, ZuFwdTuple(app.id));
    auto action = record(context->actions,
      ZuFwdTuple(app.id, app.nextActionID));
    ZuCheck(db->requests->active() && !next.owner &&
      next.nextActionID == app.nextActionID + 1 &&
      next.version == app.version + 1 && next.authVersion == app.authVersion + 1 &&
      next.updated == 200);
    ZuCheck(action.appID == app.id && action.id == app.nextActionID &&
      action.name == name && action.label == "Recovered action" &&
      action.state == Zum::State::Active && action.origin == Zum::Origin::Custom &&
      !action.owner && action.version == 1 && !action.tombstone);
    ZuCheck(sagaEmpty(db));
  }

  // A duplicate name after a committed app reservation must compensate the
  // allocation without changing the existing action or leaving an owned app.
  auto app = record(context->apps, ZuFwdTuple(Zum::AppID{9}));
  ZuCheck(stageActionAdd(db, context, app, ZdbSagaID{200}, "cut0", 2));
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto next = record(context->apps, ZuFwdTuple(app.id));
  auto existing = record(context->actions,
    ZuFwdTuple(app.id, Zum::ActionID{0}));
  auto absent = record(context->actions, ZuFwdTuple(app.id, app.nextActionID));
  ZuCheck(!next.owner && next.nextActionID == app.nextActionID &&
    next.version == app.version && next.authVersion == app.authVersion &&
    next.updated == app.updated);
  ZuCheck(existing.name == "cut0" && !existing.owner && existing.version == 1 &&
    existing.state == Zum::State::Active && !absent.appID);
  ZuCheck(sagaEmpty(db));
  ZuCheck(stopDB(db, context));

  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  auto module = ::getenv("ZUM_TEST_MODULE");
  auto connect = ::getenv("ZUM_TEST_CONNECT");
  if (!module || !*module || !connect || !*connect) {
    std::cerr << "zumrestarttest: set ZUM_TEST_MODULE and ZUM_TEST_CONNECT "
      "for a fresh disposable PostgreSQL database\n";
    return 1;
  }
  ZuTestMain();
  ZuTestCall(restart);
  ZuTestCall(grantUpdate);
  ZuTestCall(sagaRecovery);
  return 0;
}
