//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <stdlib.h>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"

#include <zlib/ZvMxParams.hh>

#include <zlib/zumd_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_saga_enrollment.hh>
#include <zlib/zumd_saga_app_enrollment.hh>
#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_saga_secret_rekey.hh>
#include <zlib/zumd_saga_key_binding.hh>
#include <zlib/zumd_saga_user_invite.hh>
#include <zlib/zumd_saga_role_add.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_saga_role_delete.hh>

using namespace ZuTestUtil;

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> config()
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  ZfCf::DefVal module;
  if (auto path = Zt::getpath("ZDB_MODULE")) module = path;
  defines->add(ZfCf::DefKey{"MODULE"}, ZuMv(module));
  defines->add(ZfCf::DefKey{"CONNECT"},
    ZfCf::DefVal{::getenv("ZDB_CONNECT")});
  Zum::String source{
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store, module: ${MODULE}, connect: ${CONNECT}},\n"
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
    const ZfCf::AnyNode *cf, ZiMultiplex &mx, ZmRef<Zum::DB> &context)
{
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  if (!db->requests->init(&mx, 5, 8)) return {};
  db->init(ZdbCf{cfFields.zdb.p<const ZfCf::AnyNode *>()}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown});
  Zum::registerSchema(db);
  context = db;
  if (!db->start()) return {};
  db->active.wait();
  return db;
}

static bool stopDB(ZmRef<TestDB> &db, ZmRef<Zum::DB> &context)
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

static bool insertIssuer(Zum::DB *context)
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

static Zdb_::AnyTable *journalTable(
    const Zum::SagaRef &saga, unsigned step, Zdb_::AnyTable *expected)
{
  ZuCSpan id;
  ZdbSagaOp::T op;
  if (!saga->stepDef(step, id, op) || id != expected->id() ||
      op == ZdbSagaOp::Invalid) return nullptr;
  return expected;
}

static bool stageEnrollment(Zum::DB *db, Zum::DB *context)
{
  Zum::Bytes ceremonyID{ZuBSpan{"enroll-ceremony!"}};
  Zum::Grant ceremony{
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
    };
  if (!insertRecord(context->grants, ceremony)) return false;

  Zum::SagaRef saga = Zum::enrollment(Zum::EnrollmentArgs{
    .ceremonyID = ZuMv(ceremonyID),
    .userID = 42,
    .name = "recovered user",
    .handle = Zum::Bytes{ZuBSpan{"recovered handle"}},
    .credentialID = Zum::Bytes{ZuBSpan{"recovered credential"}},
    .publicKey = Zum::Bytes{ZuBSpan{"public key"}},
    .created = 100,
    .label = "passkey",
    .beforeGrant = ZuMv(ceremony)
  });
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
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
	    .type = Zum::enrollmentDef().type,
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
    Zum::DB *db, Zum::AppID appID,
    ZuCSpan clientID, ZdbSagaID sagaID, Zum::ActionID catalogPublishOp,
    Zum::ActionID operationQueryOp)
{
  Zum::AppEnrollmentArgs enrollment{.appID = appID,
    .appName = Zum::String{clientID}, .appLabel = "Orders",
    .audience = Zum::String{"https://orders.example/"} << appID,
    .clientID = Zum::String{clientID},
    .secretDigest = Zum::Bytes{ZuBSpan{"verifier"}},
    .created = 100, .catalogPublishOp = catalogPublishOp,
    .operationQueryOp = operationQueryOp,
    .request = Zum::IdemRequest{.actorID = "recovery-admin",
      .operation = Zum::MgmtOp::appEnroll,
      .idempotence = Zum::String{clientID},
      .requestDigest = Zum::Bytes{ZuBSpan{"enrollment request"}},
      .expires = 86400, .created = 90, .updated = 90}};
  Zum::SagaRef saga = Zum::appEnrollment(ZuMv(enrollment));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
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
	    .type = Zum::appEnrollmentDef().type,
	    .id = sagaID, .shard = 0, .data = ZuMv(payload)};
	  wake(bool(row->commit()));
	});
      });
    });
  });
}

struct AppRollbackState {
  Zum::App		app;
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

static Zum::IdemRequest actionRequest(ZdbSagaID id)
{
  return Zum::IdemRequest{.actorKind = Zum::ActorKind::User,
    .actorID = "recovery-admin", .operation = Zum::MgmtOp::actionAdd,
    .idempotence = Zum::String{} << id,
    .requestDigest = Zum::Bytes{ZuBSpan{"action request"}},
    .status = Zum::RequestStatus::Pending, .expires = 86400,
    .version = 1, .created = 190, .updated = 190};
}

static bool stageActionAdd(
    Zum::DB *db, const Zum::App &app,
    ZdbSagaID sagaID, ZuCSpan name, unsigned cut)
{
  Zum::AppActionAddArgs add{.appID = app.id, .actionID = app.nextActionID,
    .name = Zum::String{name}, .label = "Recovered action", .created = 200,
    .oldAppVersion = app.version, .oldAuthVersion = app.authVersion,
    .oldUpdated = app.updated, .request = actionRequest(sagaID)};
  Zum::SagaRef saga = Zum::appActionAdd(ZuMv(add));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::appActionAddDef().type, .id = sagaID,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < saga->nSteps(); ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *expected;
    switch (step) {
      case 0: case 5: expected = db->requests; break;
      case 1: case 4: expected = db->apps; break;
      default: expected = db->actions; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
	.type = Zum::appActionAddDef().type, .id = sagaID,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    if (step == 0) {
      auto request = actionRequest(sagaID);
      request.sagaID = sagaID;
      request.owner = sagaID;
      if (!insertRecord(db->requests, ZuMv(request))) return false;
    } else if (step == 5) {
      if (!ZmBlock<bool>{}([db, sagaID, actionID = app.nextActionID](
          auto wake) mutable {
        auto requests = db->requests;
        requests->run(0, [requests, sagaID, actionID, wake = ZuMv(wake)]() mutable {
          auto request = actionRequest(sagaID);
          requests->findUpd<0>(0, ZuFwdTuple(request.actorKind,
            request.actorID, request.operation, request.idempotence),
            [actionID, wake = ZuMv(wake)](ZdbRow<Zum::IdemRequest> *row) mutable {
              if (!row) { wake(false); return; }
              row->data().status = Zum::RequestStatus::Complete;
              row->data().resultIDs.push(Zum::String{} << actionID);
              row->data().version = 2;
              row->data().updated = 200;
              row->data().owner = 0;
              wake(bool(row->commit()));
            });
        });
      })) return false;
    } else if (step == 1 || step == 4) {
      if (!ZmBlock<bool>{}([db, &app, sagaID, step](auto wake) mutable {
	db->apps->run(0, [db, &app, sagaID, step,
	    wake = ZuMv(wake)]() mutable {
	  db->apps->findUpd<0>(0, ZuFwdTuple(app.id), [
	      &app, sagaID, step, wake = ZuMv(wake)](
	      ZdbRow<Zum::App> *row) mutable {
	    if (!row) { wake(false); return; }
	    if (step == 1) {
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
    } else if (step == 2) {
      if (!insertRecord(db->actions, Zum::Action{
	  .appID = app.id, .id = app.nextActionID, .name = Zum::String{name},
	  .label = "Recovered action", .state = Zum::State::Active,
	  .origin = Zum::Origin::Custom, .version = 1,
	  .created = 200, .updated = 200, .owner = sagaID})) return false;
    } else {
      if (!ZmBlock<bool>{}([db, &app](auto wake) mutable {
	db->actions->run(0, [db, &app, wake = ZuMv(wake)]() mutable {
	  db->actions->findUpd<0>(0, ZuFwdTuple(app.id, app.nextActionID),
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

static AppRollbackState appRollbackState(Zum::DB *context)
{
  return ZmBlock<AppRollbackState>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{19}), [context,
	  wake = ZuMv(wake)](ZdbRowRef<Zum::App> app) mutable {
	  context->clients->find<0>(0, ZuFwdTuple(ZuCSpan{"svc_failed"}), [
	    app = ZuMv(app), wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Client> client) mutable {
	    wake(AppRollbackState{
	      app ? Zum::App{app->data()} : Zum::App{},
	      client ? Zum::Client{client->data()} : Zum::Client{}});
	    });
	  });
	});
  });
}

static bool enrollmentRecovered(Zum::DB *context)
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

static bool appEnrollmentRecovered(Zum::DB *context)
{
  return ZmBlock<bool>{}([context](auto wake) mutable {
    context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9}), [context,
	  wake = ZuMv(wake)](ZdbRowRef<Zum::App> app) mutable {
	  context->clients->find<0>(0, ZuFwdTuple(ZuCSpan{"svc_orders"}), [
	    context, app = ZuMv(app),
	    wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Client> client) mutable {
	    context->adminAccess->find<0>(0,
		ZuFwdTuple(Zum::ActorKind::Client,
		  ZuCSpan{"svc_orders"}, Zum::AppID{9}), [context,
		app = ZuMv(app),
		client = ZuMv(client),
		wake = ZuMv(wake)
	      ](ZdbRowRef<Zum::AdminAccess> adminAccess) mutable {
		context->authPolicies->find<0>(0, ZuFwdTuple(Zum::AppID{9}), [
		  app = ZuMv(app),
		  client = ZuMv(client),
		  adminAccess = ZuMv(adminAccess), wake = ZuMv(wake)
		](ZdbRowRef<Zum::AuthPolicy> policy) mutable {
		  wake(app && app->data().state == Zum::State::Active &&
		    app->data().version == 2 && !app->data().owner &&
		    app->data().audience == "https://orders.example/9" && client &&
		    client->data().appID == 9 && !client->data().owner &&
		    client->data().secretDigest == ZuBSpan{"verifier"} &&
		    adminAccess &&
		    !adminAccess->data().owner && adminAccess->data().appID == 9 &&
		    policy && !policy->data().owner && policy->data().appID == 9);
		});
	      });
	    });
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
    if (!db->saga(0, id, ZuMv(saga),
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

static bool createAction(
    TestDB *db, ZuCSpan name, Zum::ActionID &id)
{
  auto app = record(db->apps, ZuFwdTuple(Zum::AppID{1}));
  if (!app.id) return false;
  id = app.nextActionID;
  return runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{
    .appID = app.id, .actionID = id, .name = Zum::String{name},
    .label = Zum::String{name}, .created = 100,
    .oldAppVersion = app.version, .oldAuthVersion = app.authVersion,
    .oldUpdated = app.updated,
    .request = Zum::IdemRequest{
	.actorKind = Zum::ActorKind::User, .actorID = "restart-admin",
	.operation = Zum::MgmtOp::actionAdd,
	.idempotence = Zum::String{name},
	.requestDigest = Zum::Bytes{ZuBSpan{name}}, .expires = 86400,
	.created = 100, .updated = 100}}), ZdbSagaID{1000 + id});
}

struct State {
  Zum::Issuer	issuer;
  Zum::App	app;
  Zum::Action	action;
};

static State loadState(Zum::DB *context, Zum::ActionID id)
{
  return ZmBlock<State>{}([context, id](auto wake) mutable {
    context->issuers->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->issuers->find<0>(0, ZuFwdTuple(ZuCSpan{"issuer"}), [
	context, id, wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Issuer> issuer) mutable {
	context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{1}), [
	  context, id, issuer = ZuMv(issuer), wake = ZuMv(wake)
	](ZdbRowRef<Zum::App> app) mutable {
	  context->actions->find<0>(0, ZuFwdTuple(Zum::AppID{1}, id), [
	    issuer = ZuMv(issuer), app = ZuMv(app), wake = ZuMv(wake)
	  ](ZdbRowRef<Zum::Action> action) mutable {
	    wake(State{
	      issuer ? Zum::Issuer{issuer->data()} : Zum::Issuer{},
	      app ? Zum::App{app->data()} : Zum::App{},
	      action ? Zum::Action{action->data()} : Zum::Action{}});
	  });
	});
      });
    });
  });
}

static bool stageMemberChange(
    Zum::DB *db, const Zum::App &app,
    const Zum::Assignment &member, ZdbSagaID sagaID, unsigned cut,
    bool stale = false)
{
  Zum::IdemRequest request{.actorID = "member-admin",
    .operation = Zum::MgmtOp::assignmentRoles,
    .idempotence = Zum::String{} << sagaID,
    .requestDigest = Zum::Bytes{ZuBSpan{"assignment change"}}, .expires = 86400,
    .version = 1, .created = 300, .updated = 300};
  Zum::AssignmentChangeArgs change{.appID = app.id, .userID = member.userID,
    .oldRoles = member.roleIDs, .newRoles = {member.roleIDs[0] + 1},
    .oldState = member.state,
    .newState = Zum::State::T(member.state == Zum::State::Active ?
      Zum::State::Suspended : Zum::State::Active),
    .version = member.version + stale, .authVersion = member.authVersion,
    .oldUpdated = member.updated, .updated = 300,
    .appVersion = app.version, .appAuthVersion = app.authVersion,
    .appUpdated = app.updated, .request = request};
  Zum::SagaRef saga = Zum::assignmentChange(ZuMv(change));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::assignmentChangeDef().type, .id = sagaID,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < saga->nSteps(); ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *expected;
    switch (step) {
      case 0: case 5: expected = db->requests; break;
      case 1: case 4: expected = db->apps; break;
      default: expected = db->assignments; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
	.type = Zum::assignmentChangeDef().type, .id = sagaID,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    if (step == 0) {
      auto pending = request;
      pending.sagaID = pending.owner = sagaID;
      if (!insertRecord(db->requests, ZuMv(pending))) return false;
    } else if (step == 5) {
      if (!ZmBlock<bool>{}([db, &request](auto wake) mutable {
        db->requests->run(0, [db, &request, wake = ZuMv(wake)]() mutable {
          db->requests->findUpd<0>(0, ZuFwdTuple(request.actorKind,
            request.actorID, request.operation, request.idempotence),
            [wake = ZuMv(wake)](ZdbRow<Zum::IdemRequest> *row) mutable {
              if (!row) { wake(false); return; }
              row->data().status = Zum::RequestStatus::Complete;
              row->data().version = 2;
              row->data().owner = 0;
              wake(bool(row->commit()));
            });
        });
      })) return false;
    } else if (step == 1 || step == 4) {
      if (!ZmBlock<bool>{}([db, &app, sagaID, step](auto wake) mutable {
	db->apps->run(0, [db, &app, sagaID, step,
	    wake = ZuMv(wake)]() mutable {
	  db->apps->findUpd<0>(0, ZuFwdTuple(app.id), [
	      sagaID, step, wake = ZuMv(wake)](ZdbRow<Zum::App> *row) mutable {
	    if (!row) { wake(false); return; }
	    if (step == 1) row->data().owner = sagaID;
	    else {
	      row->data().owner = 0;
	      ++row->data().version;
	      ++row->data().authVersion;
	      row->data().updated = 300;
	    }
	    wake(bool(row->commit()));
	  });
	});
      })) return false;
    } else {
      if (!ZmBlock<bool>{}([db, &member, sagaID, step](auto wake) mutable {
	db->assignments->run(0, [db, &member, sagaID, step,
	    wake = ZuMv(wake)]() mutable {
	  db->assignments->findUpd<0>(0, ZuFwdTuple(member.appID, member.userID),
	    [&member, sagaID, step, wake = ZuMv(wake)](
		ZdbRow<Zum::Assignment> *row) mutable {
	      if (!row) { wake(false); return; }
	      if (step == 2) {
		row->data().roleIDs = {member.roleIDs[0] + 1};
		row->data().state = member.state == Zum::State::Active ?
		  Zum::State::Suspended : Zum::State::Active;
		++row->data().version;
		++row->data().authVersion;
		row->data().updated = 300;
		row->data().owner = sagaID;
	      } else row->data().owner = 0;
	      wake(bool(row->commit()));
	    });
	});
      })) return false;
    }
  }
  return true;
}

static void memberRecovery()
{
  ZuTestScope(memberRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(insertRecord(context->apps, Zum::App{.id = 30, .name = "member-recovery",
    .state = Zum::State::Active, .created = 100, .updated = 100, .audience = "https://member-recovery.example"}));
  ZuCheck(insertRecord(context->users, Zum::User{.id = 31,
    .name = "member-recovery", .handle = Zum::Bytes{ZuBSpan{"member-recovery"}},
    .state = Zum::State::Active}));
  ZuCheck(insertRecord(context->assignments, Zum::Assignment{
    .appID = 30, .userID = 31, .roleIDs = {101}, .state = Zum::State::Active,
    .created = 100, .updated = 100}));
  for (unsigned cut = 0; cut <= 2 * 6U; ++cut) {
    ZuTestRepeat(memberRecovery, 2 * 6U + 1);
    auto app = record(context->apps, ZuFwdTuple(Zum::AppID{30}));
    auto member = record(context->assignments,
      ZuFwdTuple(Zum::AppID{30}, Zum::UserID{31}));
    ZuCheck(stageMemberChange(db, app, member, ZdbSagaID{300 + cut}, cut));
    ZuCheck(stopDB(db, context));
    db = startDB(cf, mx, context);
    ZuCheck(bool(db));
    if (!db) return;
    auto nextApp = record(context->apps, ZuFwdTuple(app.id));
    auto next = record(context->assignments, ZuFwdTuple(app.id, member.userID));
    ZuCheck(!nextApp.owner && nextApp.version == app.version + 1 &&
      nextApp.authVersion == app.authVersion + 1 && nextApp.updated == 300);
    ZuCheck(!next.owner && next.version == member.version + 1 &&
      next.authVersion == member.authVersion + 1 && next.updated == 300 &&
      next.roleIDs.length() == 1 && next.roleIDs[0] == member.roleIDs[0] + 1 &&
      next.state != member.state);
    auto request = record(context->requests, ZuFwdTuple(Zum::ActorKind::User,
      Zum::String{"member-admin"}, Zum::ActionID(Zum::MgmtOp::assignmentRoles),
      Zum::String{} << ZdbSagaID{300 + cut}));
    ZuCheck(request.status == Zum::RequestStatus::Complete &&
      request.version == 2 && !request.owner && !request.resultIDs &&
      request.sagaID == ZdbSagaID{300 + cut});
    ZuCheck(sagaEmpty(db));
  }
  auto app = record(context->apps, ZuFwdTuple(Zum::AppID{30}));
  auto member = record(context->assignments,
    ZuFwdTuple(Zum::AppID{30}, Zum::UserID{31}));
  ZuCheck(stageMemberChange(db, app, member, ZdbSagaID{400}, 4, true));
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto nextApp = record(context->apps, ZuFwdTuple(app.id));
  auto next = record(context->assignments, ZuFwdTuple(app.id, member.userID));
  ZuCheck(!nextApp.owner && nextApp.version == app.version &&
    nextApp.authVersion == app.authVersion && nextApp.updated == app.updated);
  ZuCheck(!next.owner && next.version == member.version &&
    next.authVersion == member.authVersion && next.updated == member.updated &&
    next.roleIDs == member.roleIDs && next.state == member.state);
  auto failed = record(context->requests, ZuFwdTuple(Zum::ActorKind::User,
    Zum::String{"member-admin"}, Zum::ActionID(Zum::MgmtOp::assignmentRoles),
    Zum::String{} << ZdbSagaID{400}));
  ZuCheck(!failed.idempotence);
  ZuCheck(sagaEmpty(db));
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

template <typename Table>
static bool replaceRecord(Table *table, typename Table::T item)
{
  using T = typename Table::T;
  return ZmBlock<bool>{}([table, item = ZuMv(item)](auto wake) mutable {
    table->run(0, [table, item = ZuMv(item), wake = ZuMv(wake)]() mutable {
      ZuStructKeyT<T, 0> key{ZuStructKey<0>(item)};
      table->template findUpd<0>(0, ZuMv(key),
	[item = ZuMv(item), wake = ZuMv(wake)](ZdbRow<T> *row) mutable {
	  if (!row) { wake(false); return; }
	  row->data() = ZuMv(item);
	  wake(bool(row->commit()));
	});
    });
  });
}

static Zum::SagaRef rekeySaga(Zum::SecretRekeyArgs change)
{ return Zum::secretRekey(ZuMv(change)); }
static Zum::SagaRef rekeySaga(Zum::KeyBindingArgs change)
{ return Zum::keyBinding(ZuMv(change)); }
static auto rekeyDef(const Zum::SecretRekeyArgs &)
{ return Zum::secretRekeyDef(); }
static auto rekeyDef(const Zum::KeyBindingArgs &)
{ return Zum::keyBindingDef(); }

template <typename Table, typename Def>
static bool stageRekey(Zum::DB *db, Table *table, Def change,
    typename Table::T after, unsigned step, unsigned cut, ZdbSagaID id)
{
  auto type = rekeyDef(change).type;
  Zum::SagaRef saga = rekeySaga(ZuMv(change));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = type, .id = id, .shard = 0,
      .data = ZuMv(payload)})) return false;
  if (!cut) return true;
  if (journalTable(saga, step, table) != table) return false;
  auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      wake(table->nextUN(0));
    });
  });
  if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
      .type = type, .id = id, .step = step,
      .shard = 0, .un = un})) return false;
  return cut == 1 || replaceRecord(table, ZuMv(after));
}

static void rekeyRecovery()
{
  ZuTestScope(rekeyRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  // Stage durable images immediately before the step UN, after its UN, and
  // after its mutation. Skipped branches have no step journal of their own.
  for (unsigned field = 0; field < 4; ++field) {
    for (unsigned cut = 0; cut < 3; ++cut) {
      unsigned id = 9000 + field * 3 + cut;
      Zum::String name;
      name << "rekey-" << id;
      Zum::SecretRekeyArgs change{.field = field,
        .providerID = id, .appID = id, .userID = id, .keyID = name,
        .before = Zum::Bytes{ZuBSpan{"old-ciphertext"}},
        .after = Zum::Bytes{ZuBSpan{"new-ciphertext"}}};
      auto stage = [db, field, cut, id, &change](auto table, auto before,
          auto member) {
        before.*member = change.before;
        ZuCheck(insertRecord(table, before));
        before.*member = change.after;
        ZuCheck(stageRekey(db, table, change, ZuMv(before), field, cut, id));
      };
      switch (field) {
        case Zum::SecretRekeyArgs::ProviderField:
          stage(context->providers, Zum::Provider{.id = id,
            .version = 7, .created = 100}, &Zum::Provider::clientSecret);
          break;
        case Zum::SecretRekeyArgs::EvidenceField:
          stage(context->evidence, Zum::Evidence{.appID = id, .userID = id,
            .providerID = id, .version = 7, .created = 100},
            &Zum::Evidence::protectedRefreshToken);
          break;
        case Zum::SecretRekeyArgs::SignKeyField:
          stage(context->signKeys, Zum::SignKey{.id = name, .issuer = "issuer",
            .version = 7, .created = 100}, &Zum::SignKey::privateMaterial);
          break;
        default: {
          Zum::Issuer issuer{.id = name, .schemaVersion = Zum::SchemaVersion,
            .keyCheck = change.before, .pendingKeyCheck = change.after};
          ZuCheck(insertRecord(context->issuers, issuer));
          Zum::KeyBindingArgs binding{.issuer = name, .beforeCheck = change.before,
            .afterCheck = change.after, .beforePending = change.after};
          issuer.keyCheck = change.after;
          issuer.pendingKeyCheck.null();
          ZuCheck(stageRekey(db, context->issuers, ZuMv(binding), ZuMv(issuer),
            0, cut, id));
        } break;
      }
      ZuCheck(stopDB(db, context));
      db = startDB(cf, mx, context);
      ZuCheck(bool(db));
      if (!db) return;
      auto check = [&change](const auto &row, auto member) {
        ZuCheck(row.*member == change.after && row.version == 7 &&
          row.created == 100 && !row.updated);
      };
      switch (field) {
        case Zum::SecretRekeyArgs::ProviderField:
          check(record(context->providers, ZuFwdTuple(Zum::ProviderID{id})),
            &Zum::Provider::clientSecret);
          break;
        case Zum::SecretRekeyArgs::EvidenceField:
          check(record(context->evidence, ZuFwdTuple(Zum::AppID{id},
            Zum::UserID{id}, Zum::ProviderID{id})), &Zum::Evidence::protectedRefreshToken);
          break;
        case Zum::SecretRekeyArgs::SignKeyField:
          check(record(context->signKeys, ZuFwdTuple(name)), &Zum::SignKey::privateMaterial);
          break;
        default: {
          auto issuer = record(context->issuers, ZuFwdTuple(name));
          ZuCheck(issuer.keyCheck == change.after && !issuer.pendingKeyCheck &&
            issuer.schemaVersion == Zum::SchemaVersion);
        } break;
      }
      ZuCheck(sagaEmpty(db));
    }
  }
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

static bool stageInvitation(Zum::DB *db,
    const Zum::UserInviteArgs &change, unsigned cut)
{
  ZdbSagaID id{change.values.id};
  auto copy = change;
  Zum::SagaRef saga = Zum::userInvite(ZuMv(copy));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::userInviteDef().type, .id = id,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < saga->nSteps(); ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *expected;
    switch (step) {
      case 0: case 7: expected = db->requests; break;
      case 2: case 6: expected = db->grants; break;
      default: expected = db->users; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
        .type = Zum::userInviteDef().type, .id = id,
        .step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    bool ok;
    switch (step) {
      case 0: case 7: {
	auto request = change.request;
	request.sagaID = id;
	if (!step) {
	  request.owner = id;
	  ok = insertRecord(db->requests, ZuMv(request));
	} else {
	  request.status = Zum::RequestStatus::Complete;
	  request.resultIDs.push(Zum::String{} << change.values.id);
	  ++request.version;
	  request.updated = change.updated;
	  ok = replaceRecord(db->requests, ZuMv(request));
	}
      } break;
      case 1: case 5: {
        Zum::User user{.id = change.values.id,
          .source = Zum::UserSource::Local, .name = change.values.name,
          .profile = change.values.profile, .email = change.values.email,
          .created = change.updated, .updated = change.updated,
          .state = Zum::State::Pending, .version = 1};
	user.owner = step == 1 ? id : ZdbSagaID{0};
	ok = step == 1 ? insertRecord(db->users, ZuMv(user)) :
	  replaceRecord(db->users, ZuMv(user));
      } break;
      case 2: case 6: {
	auto grant = change.grant;
	grant.owner = step == 2 ? id : ZdbSagaID{0};
	ok = step == 2 ? insertRecord(db->grants, ZuMv(grant)) :
	  replaceRecord(db->grants, ZuMv(grant));
      } break;
      default: {
	auto user = change.external;
	++user.authVersion;
	++user.version;
	user.updated = change.updated;
	user.owner = step == 3 ? id : ZdbSagaID{0};
	ok = replaceRecord(db->users, ZuMv(user));
      } break;
    }
    if (!ok) return false;
  }
  return true;
}

static void invitationRecovery()
{
  ZuTestScope(invitationRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  unsigned boundaries = 2 * 8U + 1;
  for (unsigned cut = 0; cut <= boundaries; ++cut) {
    bool stale = cut == boundaries;
    Zum::User external{.id = Zum::UserID{8000 + 2 * cut},
      .source = Zum::UserSource::External,
      .name = Zum::String{} << "recovery-invite-" << cut,
      .created = 100, .updated = 100, .state = Zum::State::Active,
      .authVersion = 3, .version = 5};
    ZuCheck(insertRecord(context->users, external));
    Zum::UserInviteArgs change{
      .before = Zum::User{.id = external.id + 1, .version = 0},
      .values = Zum::User{.id = external.id + 1, .name = external.name},
      .grant = Zum::Grant{.id = Zum::Bytes{ZuBSpan{external.name}},
	.userID = external.id + 1, .created = 200, .expires = 300,
	.kind = Zum::GrantKind::Capability, .purpose = Zum::GrantPurpose::Enrollment,
	.state = Zum::State::Active, .issuer = "issuer",
	.digest = Zum::Bytes{ZuBSpan{external.name}},
	.userName = external.name, .actor = "precreated"},
      .updated = 200, .request = Zum::IdemRequest{.actorID = "invite-admin",
	.operation = Zum::MgmtOp::userInvite, .idempotence = external.name,
	.requestDigest = Zum::Bytes{ZuBSpan{"invite"}}, .expires = 86400,
	.created = 200, .updated = 200}, .external = external};
    if (stale) --change.external.version;
    ZuCheck(stageInvitation(db, change, stale ? 6 : cut));
    ZuCheck(stopDB(db, context));
    db = startDB(cf, mx, context);
    ZuCheck(bool(db));
    if (!db) return;
    auto next = record(context->users, ZuFwdTuple(external.id));
    ZuCheck(!next.owner && next.authVersion == external.authVersion + !stale &&
      next.version == external.version + !stale && next.name == external.name &&
      next.updated == (stale ? 100 : 200));
    auto local = record(context->users, ZuFwdTuple(change.values.id));
    auto grant = record(context->grants, ZuFwdTuple(change.grant.id));
    auto request = record(context->requests, ZuFwdTuple(change.request.actorKind,
      change.request.actorID, change.request.operation, change.request.idempotence));
    if (stale) {
      ZuCheck(!local.id && !grant.id && !request.idempotence);
    } else {
      ZuCheck(local.id == change.values.id && !local.owner &&
	local.source == Zum::UserSource::Local && local.state == Zum::State::Pending);
      ZuCheck(grant.userID == local.id && !grant.owner);
      ZuCheck(request.status == Zum::RequestStatus::Complete &&
	request.version == 2 && !request.owner);
    }
    ZuCheck(sagaEmpty(db));
  }
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

static bool stageRoleCreation(Zum::DB *db,
    const Zum::RoleAddArgs &change, unsigned cut)
{
  ZdbSagaID id{change.values.id};
  auto copy = change;
  Zum::SagaRef saga = Zum::roleAdd(ZuMv(copy));
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::roleAddDef().type, .id = id,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < saga->nSteps(); ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *expected;
    switch (step) {
      case 0: case 5: expected = db->requests; break;
      case 1: case 4: expected = db->apps; break;
      default: expected = db->roles; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable { wake(table->nextUN(0)); });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
	.type = Zum::roleAddDef().type, .id = id,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    bool ok;
    switch (step) {
      case 0: case 5: {
	auto request = change.request;
	request.sagaID = id;
	if (!step) {
	  request.owner = id;
	  ok = insertRecord(db->requests, ZuMv(request));
	} else {
	  request.status = Zum::RequestStatus::Complete;
	  request.resultIDs.push(Zum::String{} << change.values.id);
	  ++request.version;
	  request.updated = change.updated;
	  ok = replaceRecord(db->requests, ZuMv(request));
	}
      } break;
      case 1: case 4: {
	auto app = change.app;
	if (step == 1) app.owner = id;
	else {
	  ++app.version;
	  ++app.authVersion;
	  app.updated = change.updated;
	}
	ok = replaceRecord(db->apps, ZuMv(app));
      } break;
      default: {
	Zum::Role role{.appID = change.values.appID, .id = change.values.id,
	  .name = change.values.name, .label = change.values.label,
	  .state = Zum::State::Active, .origin = Zum::Origin::Custom,
	  .version = 1, .created = change.updated, .updated = change.updated};
	role.owner = step == 2 ? id : ZdbSagaID{0};
	ok = step == 2 ? insertRecord(db->roles, ZuMv(role)) :
	  replaceRecord(db->roles, ZuMv(role));
      } break;
    }
    if (!ok) return false;
  }
  return true;
}

static void roleCreationRecovery()
{
  ZuTestScope(roleCreationRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  unsigned boundaries = 2 * 6U + 1;
  for (unsigned cut = 0; cut <= boundaries; ++cut) {
    bool conflict = cut == boundaries;
    Zum::App app{.id = Zum::AppID{27000 + cut},
      .name = Zum::String{} << "role-recovery-" << cut,
      .state = Zum::State::Active, .created = 100, .updated = 100};
    ZuCheck(insertRecord(context->apps, app));
    Zum::RoleAddArgs change{.app = app,
      .before = Zum::Role{.appID = app.id, .id = app.id, .version = 0},
      .values = Zum::Role{.appID = app.id, .id = app.id, .name = "recovery-role"},
      .updated = 200,
      .request = Zum::IdemRequest{.actorID = "role-admin",
	.operation = Zum::MgmtOp::roleAdd, .idempotence = app.name,
	.requestDigest = Zum::Bytes{ZuBSpan{"role creation"}}, .expires = 86400,
	.created = 200, .updated = 200}};
    if (conflict)
      ZuCheck(insertRecord(context->roles, Zum::Role{
	.appID = app.id, .id = app.id + 100, .name = change.values.name,
	.state = Zum::State::Disabled, .tombstone = true}));
    ZuCheck(stageRoleCreation(db, change, conflict ? 4 : cut));
    ZuCheck(stopDB(db, context));
    db = startDB(cf, mx, context);
    ZuCheck(bool(db));
    if (!db) return;
    auto savedApp = record(context->apps, ZuFwdTuple(app.id));
    auto role = record(context->roles, ZuFwdTuple(app.id, change.values.id));
    auto request = record(context->requests, ZuFwdTuple(change.request.actorKind,
      change.request.actorID, change.request.operation, change.request.idempotence));
    ZuCheck(!savedApp.owner && savedApp.version == app.version + !conflict &&
      savedApp.authVersion == app.authVersion + !conflict);
    if (conflict) {
      ZuCheck(!role.id && !request.idempotence);
      auto reserved = record(context->roles, ZuFwdTuple(app.id, app.id + 100));
      ZuCheck(reserved.id == app.id + 100 && reserved.tombstone && !reserved.owner);
    } else {
      ZuCheck(role.id == change.values.id && role.name == change.values.name &&
	!role.owner && role.version == 1);
      ZuCheck(request.status == Zum::RequestStatus::Complete && !request.owner &&
	request.resultIDs.length() == 1 &&
	request.resultIDs[0] == (Zum::String{} << role.id));
    }
    ZuCheck(sagaEmpty(db));
  }
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

template <typename Table>
static bool stageCatalogRow(Table *table, const Zum::CatalogRows &rows,
    bool added, bool release, ZdbSagaID id)
{
  typename Table::T item;
  if (added) {
    if (!Zum::SagaImage::load(rows.added[0], item)) return false;
  } else {
    Zum::CatalogEdit edit;
    if (!Zum::SagaImage::load(rows.changed[0], edit) ||
	!Zum::SagaImage::load(edit.after, item)) return false;
  }
  if (release) {
    using T = typename Table::T;
    return ZmBlock<bool>{}([table, item = ZuMv(item), id](auto wake) mutable {
      table->run(0, [table, item = ZuMv(item), id,
	  wake = ZuMv(wake)]() mutable {
	ZuStructKeyT<T, 0> key{ZuStructKey<0>(item)};
	table->template findUpd<0>(0, ZuMv(key),
	  [id, wake = ZuMv(wake)](ZdbRow<T> *row) mutable {
	    if (!row || row->data().owner != id) { wake(false); return; }
	    row->data().owner = ZdbSagaID{0};
	    wake(bool(row->commit()));
	  });
      });
    });
  }
  item.owner = id;
  if (added) return insertRecord(table, ZuMv(item));
  return replaceRecord(table, ZuMv(item));
}

// One insert and one replacement per definition table: each declared repeat
// phase has one physical step. Persist both sides of every intent/effect cut.
static bool stageCatalog(Zum::DB *db,
    const Zum::CatalogPublishArgs &change, unsigned cut)
{
  ZdbSagaID id{change.before.id};
  auto copy = change;
  Zum::SagaRef saga = Zum::catalogPublish(ZuMv(copy));
  unsigned nSteps = saga->nSteps();
  if (nSteps != 12) return false;
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto steps = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::catalogPublishDef().type, .id = id,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < nSteps; ++step) {
    if (cut == 2 * step) return true;
    Zdb_::AnyTable *expected;
    unsigned phase = step - 1;
    if (step == 0 || step == 11) expected = db->requests;
    else switch (phase) {
      case 0: case 9: expected = db->apps; break;
      case 1: case 2: case 5: case 6: expected = db->actions; break;
      default: expected = db->roles; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(steps.ptr(), Zdb_::SagaStep{
	.type = Zum::catalogPublishDef().type, .id = id,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    bool ok;
    if (step == 0 || step == 11) {
      auto request = change.request;
      request.sagaID = id;
      if (!step) {
        request.owner = id;
        ok = insertRecord(db->requests, ZuMv(request));
      } else {
        request.status = Zum::RequestStatus::Complete;
        request.owner = ZdbSagaID{0};
        request.resultIDs.push(Zum::String{} << change.before.id);
        request.version = change.request.version + 1;
        request.updated = change.after.updated;
        ok = replaceRecord(db->requests, ZuMv(request));
      }
    } else switch (phase) {
      case 0: case 9: {
	auto app = phase ? change.after : change.before;
	app.owner = phase ? ZdbSagaID{0} : id;
	ok = replaceRecord(db->apps, ZuMv(app));
      } break;
      case 1: case 2: case 5: case 6:
	ok = stageCatalogRow(db->actions, change.actions, phase & 1, phase > 4, id);
	break;
      case 3: case 4: case 7: case 8:
	ok = stageCatalogRow(db->roles, change.roles, phase & 1, phase > 4, id);
	break;
    }
    if (!ok) return false;
  }
  return true;
}

static void catalogRecovery(bool fail, unsigned cut = 0)
{
  ZuTestScope(catalogRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  Zum::String name{fail ? "catalog-fail-" : "catalog-ok-"};
  name << cut;
  Zum::App app{.id = Zum::AppID(50 + fail + 2 * cut), .name = ZuMv(name),
    .state = Zum::State::Active, .nextActionID = 1, .catalogRevision = 1,
    .created = 100, .updated = 100};
  Zum::Action action{.appID = app.id, .id = 0, .name = "existing",
    .label = "before", .origin = Zum::Origin::Standard, .catalogRevision = 1,
    .created = 100, .updated = 100};
  Zum::Role role{.appID = app.id, .id = 600, .name = "existing",
    .state = Zum::State::Suspended, .origin = Zum::Origin::Standard,
    .catalogRevision = 1, .created = 100, .updated = 100};
  role.actions.set(0);
  app.audience = Zum::String{"https://"} << app.name << ".example";
  ZuCheck(insertRecord(context->apps, app));
  ZuCheck(insertRecord(context->actions, action));
  ZuCheck(insertRecord(context->roles, role));
  Zum::CatalogPublishArgs change{.before = app, .after = app,
    .request = Zum::IdemRequest{.actorKind = Zum::ActorKind::Client,
      .actorID = "publisher", .operation = Zum::MgmtOp::catalogPublish,
      .idempotence = app.name,
      .requestDigest = Zum::Bytes{ZuBSpan{"manifest request"}},
      .expires = 86400, .created = 190, .updated = 190}};
  change.after.catalogRevision = 2;
  change.after.nextActionID = 2;
  change.after.updated = 200;
  ++change.after.version;
  ++change.after.authVersion;
  auto nextAction = action;
  nextAction.label = "after";
  nextAction.catalogRevision = 2;
  ++nextAction.version;
  nextAction.updated = 200;
  change.actions.change(action, nextAction);
  nextAction.id = 1;
  nextAction.name = "added";
  nextAction.version = 1;
  change.actions.add(nextAction);
  auto nextRole = role;
  nextRole.actions.set(1);
  nextRole.catalogRevision = 2;
  ++nextRole.version;
  nextRole.updated = 200;
  auto oldRole = role;
  if (fail) { ++oldRole.version; ++nextRole.version; }
  change.roles.change(oldRole, nextRole);
  nextRole.id = 602;
  nextRole.name = "added";
  nextRole.version = 1;
  nextRole.state = Zum::State::Active;
  change.roles.add(nextRole);
  ZuCheck(stageCatalog(db, change, cut));
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto published = record(context->apps, ZuFwdTuple(app.id));
  auto request = record(context->requests, ZuFwdTuple(Zum::ActorKind::Client,
    Zum::String{"publisher"}, Zum::ActionID(Zum::MgmtOp::catalogPublish), app.name));
  if (fail) {
    ZuCheck(!request.idempotence);
  } else {
    ZuCheck(request.status == Zum::RequestStatus::Complete && !request.owner &&
      request.sagaID == ZdbSagaID{app.id} && request.resultIDs.length() == 1 &&
      request.resultIDs[0] == (Zum::String{} << app.id));
  }
  ZuCheck(!published.owner && published.version == 1 + !fail &&
    published.authVersion == 1 + !fail && published.catalogRevision == 1 + !fail &&
    published.nextActionID == 1 + !fail);
  auto savedAction = record(context->actions, ZuFwdTuple(app.id, Zum::ActionID{0}));
  auto savedRole = record(context->roles, ZuFwdTuple(app.id, role.id));
  ZuCheck(!savedAction.owner && savedAction.version == 1 + !fail &&
    savedAction.label == (fail ? "before" : "after"));
  ZuCheck(!savedRole.owner && savedRole.version == 1 + !fail &&
    savedRole.state == Zum::State::Suspended && savedRole.actions.get(1) == !fail);
  auto addedAction = record(context->actions, ZuFwdTuple(app.id, Zum::ActionID{1}));
  auto addedRole = record(context->roles, ZuFwdTuple(app.id, Zum::RoleID{602}));
  ZuCheck(bool(addedAction.appID) == !fail && !addedAction.owner);
  ZuCheck(bool(addedRole.appID) == !fail && !addedRole.owner);
  ZuCheck(sagaEmpty(db));
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

static void catalogBoundaries()
{
  ZuTestScope(catalogBoundaries);
  enum { Steps = 12, Cuts = 2 * Steps };
  for (unsigned cut = 1; cut <= Cuts; ++cut) {
    ZuTestRepeat(catalogBoundaries, Cuts);
    ZuTestCall(catalogRecovery, false, cut);
  }
}

template <typename Table, typename Key>
static bool deleteRecord(Table *table, Key key)
{
  return ZmBlock<bool>{}([table, key = ZuMv(key)](auto wake) mutable {
    table->run(0, [table, key = ZuMv(key), wake = ZuMv(wake)]() mutable {
      table->template findDel<0>(0, ZuMv(key), [wake = ZuMv(wake)](
	  ZdbRow<typename Table::T> *row) mutable {
	wake(row && row->commit());
      });
    });
  });
}

template <typename Table>
static bool stageRoleRef(Table *table, const Zum::Bytes &image,
    ZdbSagaID id, Zum::RoleID roleID, int64_t updated, bool release)
{
  using T = typename Table::T;
  T item;
  if (!Zum::SagaImage::load(image, item)) return false;
  Zum::IDVec roles;
  roles.size(item.roleIDs.length());
  for (auto role: item.roleIDs) if (role != roleID) roles.push(role);
  item.roleIDs = ZuMv(roles);
  ++item.version;
  if constexpr (ZuIsSame<T, Zum::Assignment>{} ||
      ZuIsSame<T, Zum::ClientAccess>{}) ++item.authVersion;
  item.updated = updated;
  item.owner = release ? ZdbSagaID{0} : id;
  return replaceRecord(table, ZuMv(item));
}

static unsigned roleDeletePhase(const Zum::RoleDeleteArgs &change,
    unsigned step, unsigned &iteration)
{
  for (unsigned phase = 0; phase < 13; ++phase) {
    unsigned count;
    switch (phase) {
      case 3: case 7: count = change.members.length(); break;
      case 4: case 8: count = change.clients.length(); break;
      case 5: case 9: count = change.admins.length(); break;
      case 6: count = change.maps.length(); break;
      default: count = 1; break;
    }
    if (step < count) { iteration = step; return phase; }
    step -= count;
  }
  return 13;
}

static bool stageRoleDelete(Zum::DB *db,
    const Zum::RoleDeleteArgs &change, unsigned cut)
{
  ZdbSagaID id{change.app.id};
  auto copy = change;
  Zum::SagaRef saga = Zum::roleDelete(ZuMv(copy));
  unsigned steps = saga->nSteps();
  Zdb_::SagaPayload payload;
  if (saga->save(payload)) return false;
  auto data = internalTable<Zdb_::SagaData>(db, "saga");
  auto journals = internalTable<Zdb_::SagaStep>(db, "saga_step");
  if (!insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::roleDeleteDef().type, .id = id,
      .shard = 0, .data = ZuMv(payload)})) return false;
  for (unsigned step = 0; step < steps; ++step) {
    if (cut == 2 * step) return true;
    unsigned iteration;
    unsigned phase = roleDeletePhase(change, step, iteration);
    Zdb_::AnyTable *expected;
    switch (phase) {
      case 0: case 12: expected = db->requests; break;
      case 1: case 11: expected = db->apps; break;
      case 2: case 10: expected = db->roles; break;
      case 3: case 7: expected = db->assignments; break;
      case 4: case 8: expected = db->clientAccess; break;
      case 5: case 9: expected = db->adminAccess; break;
      default: expected = db->roleMaps; break;
    }
    auto table = journalTable(saga, step, expected);
    if (!table) return false;
    auto un = ZmBlock<Zdb_::UN>{}([table](auto wake) mutable {
      table->run(0, [table, wake = ZuMv(wake)]() mutable {
	wake(table->nextUN(0));
      });
    });
    if (!insertRecord(journals.ptr(), Zdb_::SagaStep{
	.type = Zum::roleDeleteDef().type, .id = id,
	.step = step, .shard = 0, .un = un})) return false;
    if (cut == 2 * step + 1) return true;
    bool ok = false;
    switch (phase) {
      case 0: case 12: {
	auto request = change.request;
	request.sagaID = id;
	if (!phase) {
	  request.owner = id;
	  ok = insertRecord(db->requests, ZuMv(request));
	} else {
	  request.status = Zum::RequestStatus::Complete;
	  ++request.version;
	  request.updated = change.updated;
	  ok = replaceRecord(db->requests, ZuMv(request));
	}
      } break;
      case 1: case 11: {
	auto app = change.app;
	if (phase == 1)
	  app.owner = id;
	else {
	  ++app.version;
	  ++app.authVersion;
	  app.updated = change.updated;
	}
	ok = replaceRecord(db->apps, ZuMv(app));
      } break;
      case 2: case 10: {
	auto role = change.role;
	role.actions = ZtBitmap{};
	role.state = Zum::State::Revoked;
	role.tombstone = true;
	++role.version;
	role.updated = change.updated;
	role.owner = phase == 2 ? id : ZdbSagaID{0};
	ok = replaceRecord(db->roles, ZuMv(role));
      } break;
      case 3: case 7:
	ok = stageRoleRef(db->assignments, change.members[iteration], id,
	  change.role.id, change.updated, phase == 7);
	break;
      case 4: case 8:
	ok = stageRoleRef(db->clientAccess, change.clients[iteration], id,
	  change.role.id, change.updated, phase == 8);
	break;
      case 5: case 9:
	ok = stageRoleRef(db->adminAccess, change.admins[iteration], id,
	  change.role.id, change.updated, phase == 9);
	break;
      default: {
	Zum::RoleMap map;
	if (Zum::SagaImage::load(change.maps[iteration], map))
	  ok = deleteRecord(db->roleMaps, ZuStructKey<0>(map));
      } break;
    }
    if (!ok) return false;
  }
  return true;
}

static void roleRemoval(bool fail, bool empty, unsigned cut = UINT_MAX)
{
  ZuTestScope(roleRemoval);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  Zum::String appName{fail ? "remove-fail" :
    empty ? "remove-empty" : "remove-ok"};
  if (cut != UINT_MAX) appName << '-' << cut;
  Zum::App app{.id = Zum::AppID(cut == UINT_MAX ?
	40 + fail + 2 * empty : 10000 + cut),
    .name = ZuMv(appName),
    .state = Zum::State::Active, .created = 100, .updated = 100};
  Zum::Role role{.appID = app.id, .id = 400, .name = "remove",
    .label = "Remove", .created = 100, .updated = 100};
  role.actions.set(7);
  app.audience = Zum::String{"https://"} << app.name << ".example";
  ZuCheck(insertRecord(context->apps, app));
  ZuCheck(insertRecord(context->roles, role));
  Zum::RoleDeleteArgs change{.app = app, .role = role, .updated = 200,
    .request = Zum::IdemRequest{.actorID = "recovery-admin",
      .operation = Zum::MgmtOp::roleDelete, .idempotence = app.name,
      .requestDigest = Zum::Bytes{ZuBSpan{"role removal"}},
      .expires = 86400, .created = 190, .updated = 190}};
  auto add = [](auto table, auto item, auto &images) {
    if (!insertRecord(table, item)) return false;
    images.push(Zum::SagaImage::save(item));
    return true;
  };
  if (!empty) {
    for (unsigned i = 0; i < 2; ++i) {
      Zum::String value;
      value << i;
      ZuCheck(add(context->assignments, Zum::Assignment{
	.appID = app.id, .userID = 500 + i, .roleIDs = {399, 400, 401},
	.state = Zum::State::Active, .created = 100, .updated = 100}, change.members));
      ZuCheck(add(context->clientAccess, Zum::ClientAccess{
	.clientID = value, .appID = app.id,
	.roleIDs = {400, 401}, .state = Zum::State::Active,
	.created = 100, .updated = 100}, change.clients));
      ZuCheck(add(context->adminAccess, Zum::AdminAccess{
	.actorKind = Zum::ActorKind::User, .actorID = value, .appID = app.id,
	.operationIDs = {7}, .roleIDs = {400}, .state = Zum::State::Active,
	.created = 100, .updated = 100}, change.admins));
      Zum::RoleMap map{.appID = app.id, .providerID = 700 + i,
	.value = value, .roleID = 400, .state = Zum::State::Active,
	.created = 100, .updated = 100};
      ZuCheck(insertRecord(context->roleMaps, map));
      if (fail && i == 1) ++map.version; // fail after the first map deletion
      change.maps.push(Zum::SagaImage::save(map));
    }
  }
  if (cut == UINT_MAX) {
    Zum::SagaRef saga = Zum::roleDelete(change);
    Zdb_::SagaPayload payload;
    ZuCheck(saga->save(payload) == ZiFB::Error::OK);
    auto data = internalTable<Zdb_::SagaData>(db, "saga");
    ZuCheck(insertRecord(data.ptr(), Zdb_::SagaData{
      .type = Zum::roleDeleteDef().type, .id = ZdbSagaID{app.id},
      .shard = 0, .data = ZuMv(payload)}));
  } else {
    ZuCheck(stageRoleDelete(db, change, cut));
  }
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto nextApp = record(context->apps, ZuFwdTuple(app.id));
  auto nextRole = record(context->roles, ZuFwdTuple(app.id, role.id));
  auto request = record(context->requests, ZuFwdTuple(Zum::ActorKind::User,
    Zum::String{"recovery-admin"}, Zum::ActionID(Zum::MgmtOp::roleDelete), app.name));
  if (fail) {
    ZuCheck(!request.idempotence);
  } else {
    ZuCheck(request.idempotence == app.name && !request.owner &&
      request.status == Zum::RequestStatus::Complete && request.version == 2 &&
      request.updated == 200 && !request.resultIDs &&
      request.sagaID == ZdbSagaID{app.id});
  }
  ZuCheck(!nextApp.owner && nextApp.version == 1 + !fail &&
    nextApp.authVersion == 1 + !fail && nextApp.updated == (fail ? 100 : 200));
  ZuCheck(!nextRole.owner && nextRole.version == 1 + !fail &&
    nextRole.tombstone == !fail &&
    nextRole.state == (fail ? Zum::State::Active : Zum::State::Revoked) &&
    nextRole.actions.get(7) == fail && nextRole.name == role.name);
  if (!empty) {
    for (unsigned i = 0; i < 2; ++i) {
      Zum::String value;
      value << i;
      auto member = record(context->assignments, ZuFwdTuple(app.id, Zum::UserID{500 + i}));
      auto client = record(context->clientAccess, ZuFwdTuple(value, app.id));
      auto admin = record(context->adminAccess,
	ZuFwdTuple(Zum::ActorKind::T(Zum::ActorKind::User), value, app.id));
      auto map = record(context->roleMaps, ZuFwdTuple(app.id, Zum::ProviderID{700 + i}, value));
      auto check = [fail](const auto &item) {
	bool found = false;
	for (auto id: item.roleIDs) if (id == 400) found = true;
	return !item.owner && item.version == 1 + !fail && found == fail &&
	  item.updated == (fail ? 100 : 200);
      };
      ZuCheck(check(member) && member.authVersion == 1 + !fail &&
	member.roleIDs.length() == 2 + fail && member.roleIDs[0] == 399);
      ZuCheck(check(client) && client.authVersion == 1 + !fail);
      ZuCheck(check(admin) && admin.operationIDs == Zum::ActionIDVec{7});
      ZuCheck(fail ? map.roleID == 400 && map.version == 1 && !map.owner : !map.appID);
    }
  }
  ZuCheck(sagaEmpty(db));
  ZuCheck(stopDB(db, context));
  ZuCheck(mx.stop());
  stopMx.cancel();
}

static void roleBoundaries()
{
  ZuTestScope(roleBoundaries);
  enum {
    Steps = 13 + 7,
    Cuts = 2 * Steps + 1
  };
  Zum::RoleDeleteArgs change;
  for (unsigned i = 0; i < 2; ++i) {
    change.members.push(Zum::Bytes{});
    change.clients.push(Zum::Bytes{});
    change.admins.push(Zum::Bytes{});
    change.maps.push(Zum::Bytes{});
  }
  Zum::SagaRef saga = Zum::roleDelete(ZuMv(change));
  unsigned steps = saga->nSteps();
  ZuCheck(steps == Steps);
  for (unsigned cut = 0; cut <= 2 * steps; ++cut) {
    ZuTestRepeat(roleBoundaries, Cuts);
    ZuTestCall(roleRemoval, false, false, cut);
  }
}

static void restart()
{
  ZuTestScope(restart);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(insertIssuer(context));
  ZuCheck(insertRecord(context->apps, Zum::App{.id = 1, .name = "restart",
    .state = Zum::State::Active, .created = 100, .updated = 100, .audience = "https://restart.example"}));
  Zum::ActionID first = 0;
  ZuCheck(createAction(db, "orders.read", first));
  auto state = loadState(context, first);
  ZuCheck(state.issuer.id == "issuer" && state.app.nextActionID == first + 1 &&
    state.action.id == first && state.action.name == "orders.read");
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  state = loadState(context, first);
  ZuCheck(state.issuer.id == "issuer" && state.app.nextActionID == first + 1 &&
    state.action.id == first && state.action.name == "orders.read");
  Zum::ActionID second = 0;
  ZuCheck(createAction(db, "orders.write", second));
  ZuCheck(second == first + 1 &&
    loadState(context, second).app.nextActionID == second + 1);
  ZuCheck(stopDB(db, context));

  ZuCheck(mx.stop());
  stopMx.cancel();
}

static void grantUpdate()
{
  ZuTestScope(grantUpdate);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  constexpr auto id = "grant-update-001"_z;
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
  stopMx.cancel();
}

static void sagaRecovery()
{
  ZuTestScope(sagaRecovery);
  auto cf = config();
  auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheck(mx.start());
  ZuGuard stopMx{[&mx] { mx.stop(); }};
  ZmRef<Zum::DB> context;
  auto db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(stageEnrollment(db, context));
  ZuCheck(stageAppEnrollment(
    db, 9, "svc_orders", ZdbSagaID{43}, Zum::MgmtOp::catalogPublish, Zum::MgmtOp::operationQuery));
  // Equal operation IDs are a corrupt enrollment payload. It fails after the
  // app and client inserts and must roll them back.
  ZuCheck(stageAppEnrollment(
    db, 19, "svc_failed", ZdbSagaID{44}, 1, 1));
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(db->requests->active());
  ZuCheck(enrollmentRecovered(context));
  ZuCheck(appEnrollmentRecovered(context));
  auto enrollmentRequest = record(context->requests,
    ZuFwdTuple(Zum::ActorKind::User, Zum::String{"recovery-admin"},
      Zum::ActionID(Zum::MgmtOp::appEnroll), Zum::String{"svc_orders"}));
  ZuCheck(enrollmentRequest.status == Zum::RequestStatus::Complete &&
    !enrollmentRequest.owner && enrollmentRequest.sagaID == ZdbSagaID{43} &&
    enrollmentRequest.resultIDs.length() == 2 &&
    enrollmentRequest.resultIDs[0] == "9" &&
    enrollmentRequest.resultIDs[1] == "svc_orders");
  auto failedRequest = record(context->requests,
    ZuFwdTuple(Zum::ActorKind::User, Zum::String{"recovery-admin"},
      Zum::ActionID(Zum::MgmtOp::appEnroll), Zum::String{"svc_failed"}));
  ZuCheck(!failedRequest.idempotence);
  auto rollback = appRollbackState(context);
  ZuCheck(!rollback.app.id);
  ZuCheck(!rollback.client.id);
  ZuCheck(sagaEmpty(db));

  // Every step has an intent-only image and an effect-committed image, plus
  // the initial durable payload. Reconstruct the DB at each boundary.
  for (unsigned cut = 0; cut <= 2 * 6U; ++cut) {
    ZuTestRepeat(actionRecovery, 2 * 6U + 1);
    auto app = record(context->apps, ZuFwdTuple(Zum::AppID{9}));
    Zum::String name{"cut"};
    name << cut;
    ZuCheck(stageActionAdd(db, app, ZdbSagaID{100 + cut}, name, cut));
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
    auto key = actionRequest(ZdbSagaID{100 + cut});
    auto request = record(context->requests, ZuFwdTuple(key.actorKind,
      key.actorID, key.operation, key.idempotence));
    ZuCheck(request.status == Zum::RequestStatus::Complete && !request.owner &&
      request.sagaID == ZdbSagaID{100 + cut} && request.version == 2 &&
      request.resultIDs.length() == 1 &&
      request.resultIDs[0] == (Zum::String{} << app.nextActionID));
    ZuCheck(sagaEmpty(db));
  }

  // A duplicate name after a committed app reservation must compensate the
  // allocation without changing the existing action or leaving an owned app.
  auto app = record(context->apps, ZuFwdTuple(Zum::AppID{9}));
  ZuCheck(stageActionAdd(db, app, ZdbSagaID{200}, "cut0", 4));
  ZuCheck(stopDB(db, context));
  db = startDB(cf, mx, context);
  ZuCheck(bool(db));
  if (!db) return;
  auto next = record(context->apps, ZuFwdTuple(app.id));
  auto existing = record(context->actions,
    ZuFwdTuple(app.id, Zum::ActionID{0}));
  auto absent = record(context->actions, ZuFwdTuple(app.id, app.nextActionID));
  auto requestKey = actionRequest(ZdbSagaID{200});
  auto absentRequest = record(context->requests, ZuFwdTuple(requestKey.actorKind,
    requestKey.actorID, requestKey.operation, requestKey.idempotence));
  ZuCheck(!absentRequest.idempotence);
  ZuCheck(!next.owner && next.nextActionID == app.nextActionID &&
    next.version == app.version && next.authVersion == app.authVersion &&
    next.updated == app.updated);
  ZuCheck(existing.name == "cut0" && !existing.owner && existing.version == 1 &&
    existing.state == Zum::State::Active && !absent.appID);
  ZuCheck(sagaEmpty(db));
  ZuCheck(stopDB(db, context));

  ZuCheck(mx.stop());
  stopMx.cancel();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  auto module = Zt::getpath("ZDB_MODULE");
  auto connect = ::getenv("ZDB_CONNECT");
  if (!module || !*module || !connect || !*connect) {
    std::cerr << "zumrestarttest: set ZDB_MODULE and ZDB_CONNECT "
      "for a fresh disposable SQLite database\n";
    return 1;
  }
  ZuTestMain();
  ZuTestCall(restart);
  ZuTestCall(grantUpdate);
  ZuTestCall(sagaRecovery);
  ZuTestCall(memberRecovery);
  ZuTestCall(invitationRecovery);
  ZuTestCall(roleCreationRecovery);
  ZuTestCall(rekeyRecovery);
  ZuTestCall(roleRemoval, false, false);
  ZuTestCall(roleRemoval, true, false);
  ZuTestCall(roleRemoval, false, true);
  ZuTestCall(roleBoundaries);
  ZuTestCall(catalogRecovery, false);
  ZuTestCall(catalogRecovery, true);
  ZuTestCall(catalogRecovery, true, 10);
  ZuTestCall(catalogBoundaries);
  return 0;
}
