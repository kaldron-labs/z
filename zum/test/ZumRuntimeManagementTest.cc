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
#include <zlib/ZumMgmt.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_request_db.hh>
#include <zlib/zumd_db_ops.hh>
#include <zlib/zumd_request.hh>
#include <zlib/zumd_webauthn.hh>
#include <zlib/zumd_saga_app_action_add.hh>
#include <zlib/zumd_saga_admin_access_put.hh>
#include <zlib/zumd_saga_catalog_publish.hh>
#include <zlib/zumd_saga_assignment_add.hh>
#include <zlib/zumd_saga_assignment_change.hh>
#include <zlib/zumd_saga_role_edit.hh>
#include <zlib/zumd_saga_action_edit.hh>
#include <zlib/zumd_saga_app_change.hh>
#include <zlib/zumd_saga_user_edit.hh>
#include <zlib/zumd_saga_cred_edit.hh>

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

static int missingAssertion(Zum::DB *context)
{
  return ZmBlock<int>{}([context](auto wake) mutable {
    Zum::assertionVerify(context,
      Zum::Bytes{ZuBSpan{"missing ceremony"}},
      Zum::Bytes{ZuBSpan{"browser binding"}}, Zum::AssertionInput{},
      Zum::String{"issuer"}, Zum::AppID{8},
      Zum::String{"https://example.com"}, Zum::String{"example.com"}, 100,
      [wake = ZuMv(wake)](int error, Zum::User, Zum::Cred, Zum::Grant,
          Zum::AssertionResult) mutable { wake(error); });
  });
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


static Zum::User loadUser(Zum::DB *context, Zum::UserID id)
{
  return ZmBlock<Zum::User>{}([context, id](auto wake) mutable {
    context->users->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->users->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::User> row) mutable {
	wake(row ? Zum::User{row->data()} : Zum::User{});
      });
    });
  });
}


static Zum::Action loadAction(
    Zum::DB *context, Zum::ActionID id, Zum::AppID appID = 0)
{
  return ZmBlock<Zum::Action>{}([context, id, appID](auto wake) mutable {
    context->actions->run(0, [context, id, appID, wake = ZuMv(wake)]() mutable {
      context->actions->find<0>(0, ZuFwdTuple(appID, id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Action> row) mutable {
	wake(row ? Zum::Action{row->data()} : Zum::Action{});
      });
    });
  });
}

static Zum::Cred loadCred(Zum::DB *context, ZuBSpan id)
{
  return ZmBlock<Zum::Cred>{}([context, id](auto wake) mutable {
    context->creds->run(0, [context, id, wake = ZuMv(wake)]() mutable {
      context->creds->find<0>(0, ZuFwdTuple(id), [
	wake = ZuMv(wake)
      ](ZdbRowRef<Zum::Cred> row) mutable {
	wake(row ? Zum::Cred{row->data()} : Zum::Cred{});
      });
    });
  });
}

// Only the logger thread accesses these rows while logging is running.

static void runtimeManagement()
{
  ZuTestScope(runtimeManagement);
  ZiLog::init("ZumRuntimeManagementTest");
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
  ZuCheck(insertRecord(context->apps, Zum::App{
    .id = 9003, .name = "action-saga", .state = Zum::State::Active,
    .nextActionID = 0, .authVersion = 7, .version = 3,
    .created = 90, .updated = 100, .audience = "https://action-saga.example"}));
  auto actionAddState = [context = context.ptr()](unsigned count, uint64_t version,
      uint64_t authVersion, int64_t updated) {
    return ZmBlock<bool>{}([context, count, version, authVersion, updated](
        auto wake) mutable {
      context->apps->run(0, [context, count, version, authVersion, updated,
          wake = ZuMv(wake)]() mutable {
        context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9003}),
          [count, version, authVersion, updated, wake = ZuMv(wake)](
              ZdbRowRef<Zum::App> row) mutable {
            wake(row && !row->data().owner &&
              row->data().nextActionID == count &&
              row->data().version == version &&
              row->data().authVersion == authVersion &&
              row->data().updated == updated);
          });
      });
    });
  };
  ZuCheck(runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{.appID = 9003, .actionID = 0,
    .name = "first", .label = "First action", .created = 101,
    .oldAppVersion = 3, .oldAuthVersion = 7, .oldUpdated = 100}),
    ZdbSagaID{9003}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  auto firstAction = loadAction(context, 0, 9003);
  ZuCheck(firstAction.appID == 9003 && firstAction.name == "first" &&
    firstAction.label == "First action" && !firstAction.owner &&
    firstAction.state == Zum::State::Active &&
    firstAction.origin == Zum::Origin::Custom);
  // A stale app snapshot cannot allocate another position.
  ZuCheck(!runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{.appID = 9003, .actionID = 0,
    .name = "stale", .created = 102, .oldAppVersion = 3,
    .oldAuthVersion = 7, .oldUpdated = 100}), ZdbSagaID{9004}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  // Duplicate names fail after reserving the position; compensation must
  // restore every app field, preserving the previously published action.
  ZuCheck(!runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{.appID = 9003, .actionID = 1,
    .name = "first", .created = 102, .oldAppVersion = 4,
    .oldAuthVersion = 8, .oldUpdated = 101}), ZdbSagaID{9005}));
  ZuCheck(actionAddState(1, 4, 8, 101));
  ZuCheck(!loadAction(context, 1, 9003).appID);
  firstAction = loadAction(context, 0, 9003);
  ZuCheck(firstAction.name == "first" && !firstAction.owner &&
    firstAction.label == "First action");
  ZuCheck(runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{.appID = 9003, .actionID = 1,
    .name = "second", .created = 103, .oldAppVersion = 4,
    .oldAuthVersion = 8, .oldUpdated = 101}), ZdbSagaID{9006}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  // A missing application is a business rejection inside the saga. Its pending
  // request must disappear on rollback, without deactivating the database.
  ZuCheck(!runSaga(db, Zum::appActionAdd(Zum::AppActionAddArgs{.appID = 9999,
    .actionID = UINT32_MAX, .name = "missing-app", .created = 104,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::actionAdd, .idempotence = "missing-app",
      .requestDigest = Zum::Bytes{ZuBSpan{"request"}}, .expires = 86400,
      .created = 104, .updated = 104}}), ZdbSagaID{9007}));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    auto requests = context->requests;
    requests->run(0, [requests, wake = ZuMv(wake)]() mutable {
      requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
        Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::actionAdd),
        Zum::String{"missing-app"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable { wake(!row); });
    });
  }));
  ZuCheck(actionAddState(2, 5, 9, 103));
  auto secondAction = loadAction(context, 1, 9003);
  // An unchanged catalog still completes its request while skipping business
  // writes; no application version or authority change is warranted.
  Zum::App unchanged{.id = 9003, .state = Zum::State::Active,
    .nextActionID = 2, .authVersion = 9, .version = 5, .updated = 103};
  auto invalidAdminActor = [&db, &unchanged](Zum::ActorKind::T kind,
      Zum::String actorID, ZdbSagaID id) {
    return !runSaga(db, Zum::adminAccessPut(Zum::AdminAccessPutArgs{.app = unchanged,
      .before = Zum::AdminAccess{.actorKind = kind, .actorID = actorID,
	.appID = 9003, .version = 0},
      .values = Zum::AdminAccess{.actorKind = kind, .actorID = ZuMv(actorID),
	.appID = 9003, .operationIDs = {Zum::MgmtOp::roleQuery}},
      .ifNoneMatch = "*", .updated = 104}), id);
  };
  ZuCheck(invalidAdminActor(Zum::ActorKind::User, "99999999", ZdbSagaID{12016}));
  ZuCheck(invalidAdminActor(Zum::ActorKind::User, "41junk", ZdbSagaID{12017}));
  ZuCheck(invalidAdminActor(Zum::ActorKind::Client, "missing-admin-client", ZdbSagaID{12018}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(runSaga(db, Zum::catalogPublish(Zum::CatalogPublishArgs{.before = unchanged, .after = unchanged,
    .request = Zum::IdemRequest{.actorKind = Zum::ActorKind::Client,
      .actorID = "publisher", .operation = Zum::MgmtOp::catalogPublish,
      .idempotence = "unchanged", .requestDigest = Zum::Bytes{ZuBSpan{"manifest"}},
      .expires = 86400, .created = 105, .updated = 105}}), ZdbSagaID{9008}));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    auto requests = context->requests;
    requests->run(0, [requests, wake = ZuMv(wake)]() mutable {
      requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::Client,
        Zum::String{"publisher"}, Zum::ActionID(Zum::MgmtOp::catalogPublish),
        Zum::String{"unchanged"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable {
          wake(row && row->data().status == Zum::RequestStatus::Complete &&
            !row->data().owner && row->data().updated == 105 &&
            row->data().resultIDs.length() == 1 && row->data().resultIDs[0] == "9003");
        });
    });
  }));
  ZuCheck(secondAction.name == "second" && !secondAction.owner &&
    secondAction.state == Zum::State::Active);
  // Assignment validation and its request outcome belong to the same saga.
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 9100, .name = "assignment local",
    .handle = Zum::Bytes{ZuBSpan{"assignment local"}}}));
  ZuCheck(insertRecord(context->users, Zum::User{
    .id = 9101, .source = Zum::UserSource::External, .name = "assignment external",
    .handle = Zum::Bytes{ZuBSpan{"assignment external"}},
    .state = Zum::State::Active}));
  auto addAssignment = [&db](Zum::AppID appID, Zum::UserID userID,
      ZuCSpan key, unsigned sagaID) {
    return runSaga(db, Zum::assignmentAdd(Zum::AssignmentAddArgs{.appID = appID,
      .userID = userID, .created = 106,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::assignmentAdd, .idempotence = key,
        .requestDigest = Zum::Bytes{ZuBSpan{"assignment"}}, .expires = 86400,
        .version = 1, .created = 106, .updated = 106}}), ZdbSagaID{sagaID});
  };
  auto assignmentReq = [context = context.ptr()](ZuCSpan key, bool exists) {
    return ZmBlock<bool>{}([context, key, exists](auto wake) mutable {
      context->requests->run(0, [context, key, exists, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::assignmentAdd), key),
          [exists, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(exists ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              row->data().resultIDs.length() == 1 &&
              row->data().resultIDs[0] == "9003:9100" : !row);
          });
      });
    });
  };
  auto assignmentState = [context = context.ptr()](Zum::AppID appID,
      Zum::UserID userID, bool exists) {
    return ZmBlock<bool>{}([context, appID, userID, exists](auto wake) mutable {
      context->assignments->run(0, [context, appID, userID, exists,
          wake = ZuMv(wake)]() mutable {
        context->assignments->find<0>(0, ZuFwdTuple(appID, userID),
          [exists, wake = ZuMv(wake)](ZdbRowRef<Zum::Assignment> row) mutable {
            wake(exists ? row && row->data().state == Zum::State::Active &&
              !row->data().owner && !row->data().roleIDs &&
              row->data().version == 1 && row->data().created == 106 &&
              row->data().updated == 106 : !row);
          });
      });
    });
  };
  ZuCheck(addAssignment(9003, 9100, "assignment-created", 9100));
  ZuCheck(assignmentReq("assignment-created", true));
  ZuCheck(assignmentState(9003, 9100, true));
  ZuCheck(!addAssignment(9003, 9100, "assignment-duplicate", 9101));
  ZuCheck(assignmentReq("assignment-duplicate", false));
  ZuCheck(assignmentState(9003, 9100, true));
  ZuCheck(!addAssignment(9999, 9100, "assignment-no-app", 9102));
  ZuCheck(assignmentReq("assignment-no-app", false));
  ZuCheck(assignmentState(9999, 9100, false));
  ZuCheck(!addAssignment(9003, 9999, "assignment-no-user", 9103));
  ZuCheck(assignmentReq("assignment-no-user", false));
  ZuCheck(assignmentState(9003, 9999, false));
  ZuCheck(!addAssignment(9003, 9101, "assignment-external", 9104));
  ZuCheck(assignmentReq("assignment-external", false));
  ZuCheck(assignmentState(9003, 9101, false));
  // Replacing an assignment with itself persists its request result without
  // advancing either assignment or application authority/version.
  ZuCheck(runSaga(db, Zum::assignmentChange(Zum::AssignmentChangeArgs{.appID = 9003, .userID = 9100,
    .oldState = Zum::State::Active, .newState = Zum::State::Active,
    .version = 1, .authVersion = 1, .oldUpdated = 106, .updated = 107,
    .appVersion = 5, .appAuthVersion = 9, .appUpdated = 103,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::assignmentRoles, .idempotence = "same-roles",
      .expires = 86400, .version = 1, .created = 107, .updated = 107},
    .assignRoles = true, .ifMatch = "\"v1\""}),
    ZdbSagaID{9105}));
  ZuCheck(assignmentState(9003, 9100, true));
  ZuCheck(actionAddState(2, 5, 9, 103));
  ZuCheck(ZmBlock<bool>{}([context = context.ptr()](auto wake) mutable {
    context->requests->run(0, [context, wake = ZuMv(wake)]() mutable {
      context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
        Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::assignmentRoles),
        Zum::String{"same-roles"}), [wake = ZuMv(wake)](
          ZdbRowRef<Zum::IdemRequest> row) mutable {
          wake(row && row->data().status == Zum::RequestStatus::Complete &&
            !row->data().owner && row->data().version == 2 && !row->data().resultIDs);
        });
    });
  }));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 500, .name = "assignable", .state = Zum::State::Active}));
  // An unchanged request still obeys its ETag and application preconditions.
  for (unsigned test = 0; test < 4; ++test) {
    Zum::String key;
    key << "bad-member-state-" << test;
    ZuCheck(!runSaga(db, Zum::assignmentChange(Zum::AssignmentChangeArgs{
      .appID = Zum::AppID(test == 3 ? 9999 : 9003), .userID = 9100,
      .oldState = Zum::State::Active,
      .newState = Zum::State::T(test == 1 ? Zum::State::Suspended : Zum::State::Active),
      .version = 1, .authVersion = 1, .oldUpdated = 106, .updated = 108,
      .appVersion = uint64_t(test == 2 ? 4 : 5), .appAuthVersion = 9, .appUpdated = 103,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::assignmentState, .idempotence = key,
        .expires = 86400, .version = 1, .created = 108, .updated = 108},
      .ifMatch = test < 2 ? "\"v99\"" : "\"v1\""}), ZdbSagaID{9300 + test}));
    ZuCheck(assignmentState(9003, 9100, true));
    ZuCheck(actionAddState(2, 5, 9, 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key](auto wake) mutable {
      context->requests->run(0, [context, key, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::assignmentState), key),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(!row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 501, .name = "disabled", .state = Zum::State::Disabled}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9003, .id = 502, .name = "deleted", .state = Zum::State::Active,
    .tombstone = true}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9002, .id = 503, .name = "other-app", .state = Zum::State::Active}));
  // Existing projections/orphan records must not make an external or absent
  // user eligible for local role assignments.
  for (auto userID : Zum::IDVec{9101, 9999})
    ZuCheck(insertRecord(context->assignments, Zum::Assignment{
      .appID = 9003, .userID = userID, .state = Zum::State::Active,
      .version = 1, .created = 106, .updated = 106}));
  for (unsigned test = 0; test < 7; ++test) {
    Zum::IDVec roles;
    switch (test) {
      case 0: roles = {500, 500}; break;
      case 1: roles = {501}; break;
      case 2: roles = {502}; break;
      case 3: roles = {503}; break;
      case 4: roles = {0}; break;
      default: roles = {500}; break;
    }
    Zum::UserID userID = test < 5 ? 9100 : test == 5 ? 9101 : 9999;
    Zum::String key;
    key << "bad-role-" << test;
    ZuCheck(!runSaga(db, Zum::assignmentChange(Zum::AssignmentChangeArgs{.appID = 9003, .userID = userID,
      .newRoles = ZuMv(roles), .oldState = Zum::State::Active,
      .newState = Zum::State::Active, .version = 1, .authVersion = 1,
      .oldUpdated = 106, .updated = 108, .appVersion = 5,
      .appAuthVersion = 9, .appUpdated = 103,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::assignmentRoles, .idempotence = key,
        .expires = 86400, .version = 1, .created = 108, .updated = 108},
      .assignRoles = true}), ZdbSagaID{9200 + test}));
    ZuCheck(assignmentState(9003, userID, true));
    ZuCheck(actionAddState(2, 5, 9, 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key](auto wake) mutable {
      context->requests->run(0, [context, key, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::assignmentRoles), key),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(!row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9001, .id = 5, .name = "shared"}));
  for (unsigned test = 0; test < 4; ++test) {
    Zum::String key;
    key << "role-actions-" << test;
    Zum::RoleEditArgs change{
      .app = Zum::App{.id = 9003, .state = Zum::State::Active,
        .nextActionID = 2, .authVersion = 9, .version = 5, .updated = 103},
      .before = Zum::Role{.appID = 9003, .id = 500, .name = "assignable"},
      .actionIDs = test == 0 ? Zum::ActionIDVec{0, 0} : Zum::ActionIDVec{0, 1},
      .ifMatch = test == 1 ? "\"v99\"" : "\"v1\"", .updated = 109,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::roleActions, .idempotence = key,
        .expires = 86400, .version = 1, .created = 109, .updated = 109}};
    if (test == 3) {
      change.app.version = 6;
      change.app.authVersion = 10;
      change.app.updated = 109;
      change.before.version = 2;
      change.before.updated = 109;
      change.before.actions.length(2);
      change.before.actions.set(0).set(1);
      change.ifMatch = "\"v2\"";
      change.kind = Zum::RoleEditArgs::Label;
      change.label = "Edited label";
      change.request.operation = Zum::MgmtOp::roleUpdate;
    }
    ZuCheck(runSaga(db, Zum::roleEdit(ZuMv(change)), ZdbSagaID{9400 + test}) == (test >= 2));
    ZuCheck(actionAddState(2, test >= 2 ? 4 + test : 5, test >= 2 ? 10 : 9,
      test >= 2 ? 109 : 103));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), test](auto wake) mutable {
      context->roles->run(0, [context, test, wake = ZuMv(wake)]() mutable {
        context->roles->find<0>(0, ZuFwdTuple(Zum::AppID{9003}, Zum::RoleID{500}),
          [test, wake = ZuMv(wake)](ZdbRowRef<Zum::Role> row) mutable {
            bool valid = row && !row->data().owner && row->data().name == "assignable" &&
              row->data().version == (test >= 2 ? test : 1);
            if (valid && test >= 2)
              valid = row->data().actions[0] && row->data().actions[1];
            else if (valid) valid = !row->data().actions.length();
            if (valid && test == 3) valid = row->data().label == "Edited label";
            wake(valid);
          });
      });
    }));
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, test](auto wake) mutable {
      context->requests->run(0, [context, key, test, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(test == 3 ?
            Zum::MgmtOp::roleUpdate : Zum::MgmtOp::roleActions), key),
          [test, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(test >= 2 ? row && !row->data().owner &&
              row->data().status == Zum::RequestStatus::Complete &&
              row->data().version == 2 && !row->data().resultIDs : !row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->actions, Zum::Action{
    .appID = 9002, .id = 5, .name = "shared"}));
  Zum::App stateApp{.id = 9003, .state = Zum::State::Active, .nextActionID = 2,
    .authVersion = 10, .version = 7, .updated = 109};
  Zum::Role stateRole{.appID = 9003, .id = 500, .name = "assignable",
    .label = "Edited label", .version = 3, .updated = 109};
  stateRole.actions.length(2);
  stateRole.actions.set(0).set(1);
  for (unsigned test = 0; test < 6; ++test) {
    bool valid = test != 0 && test != 4;
    Zum::State::T state = test < 2 || test >= 4 ?
      Zum::State::Active : Zum::State::Disabled;
    Zum::String key, etag;
    key << "role-state-" << test;
    etag << "\"v" << (valid ? stateRole.version : 99) << '"';
    Zum::RoleEditArgs change{.app = stateApp, .before = stateRole,
      .ifMatch = ZuMv(etag), .updated = 110,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::roleState, .idempotence = key,
        .expires = 86400, .version = 1, .created = 110, .updated = 110},
      .kind = Zum::RoleEditArgs::Status, .state = state};
    ZuCheck(runSaga(db, Zum::roleEdit(ZuMv(change)), ZdbSagaID{9500 + test}) == valid);
    if (valid && state != stateRole.state) {
      stateRole.state = state;
      ++stateRole.version;
      stateRole.updated = 110;
      ++stateApp.version;
      ++stateApp.authVersion;
      stateApp.updated = 110;
    }
    ZuCheck(actionAddState(2, stateApp.version, stateApp.authVersion, stateApp.updated));
    auto role = ZmBlock<Zum::Role>{}([context = context.ptr()](auto wake) mutable {
      context->roles->run(0, [context, wake = ZuMv(wake)]() mutable {
        context->roles->find<0>(0, ZuFwdTuple(Zum::AppID{9003}, Zum::RoleID{500}),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::Role> row) mutable {
            wake(row ? Zum::Role{row->data()} : Zum::Role{});
          });
      });
    });
    ZuCheck(role.id == 500 && !role.owner && role.version == stateRole.version &&
      role.state == stateRole.state && role.updated == stateRole.updated &&
      role.label == stateRole.label && role.actions == stateRole.actions);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, valid](auto wake) mutable {
      context->requests->run(0, [context, key, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::roleState), key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && !row->data().resultIDs : !row);
          });
      });
    }));
  }
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9001, .id = 5, .name = "shared"}));
  ZuCheck(insertRecord(context->roles, Zum::Role{
    .appID = 9002, .id = 5, .name = "shared"}));
  ZuCheck(missingAssertion(context) == Zum::WebAuthnError::Ceremony);

  auto stateAction = loadAction(context, 0, 9003);
  for (unsigned test = 0; test < 6; ++test) {
    bool valid = test != 0 && test != 4;
    Zum::State::T state = test < 2 || test >= 4 ?
      Zum::State::Active : Zum::State::Disabled;
    Zum::String key, etag;
    key << "action-state-" << test;
    etag << "\"v" << (valid ? stateAction.version : 99) << '"';
    Zum::ActionEditArgs change{.app = stateApp, .before = stateAction,
      .ifMatch = ZuMv(etag), .updated = 113,
      .request = Zum::IdemRequest{.actorID = "admin",
        .operation = Zum::MgmtOp::actionState, .idempotence = key,
        .expires = 86400, .version = 1, .created = 113, .updated = 113},
      .state = state};
    ZuCheck(runSaga(db, Zum::actionEdit(ZuMv(change)), ZdbSagaID{9700 + test}) == valid);
    if (valid && state != stateAction.state) {
      stateAction.state = state;
      ++stateAction.version;
      stateAction.updated = 113;
      ++stateApp.version;
      ++stateApp.authVersion;
      stateApp.updated = 113;
    }
    ZuCheck(actionAddState(2, stateApp.version, stateApp.authVersion, stateApp.updated));
    auto action = loadAction(context, 0, 9003);
    ZuCheck(action.appID == 9003 && !action.id && !action.owner &&
      action.version == stateAction.version && action.state == stateAction.state &&
      action.updated == stateAction.updated && action.created == stateAction.created &&
      action.name == stateAction.name && action.label == stateAction.label &&
      action.origin == stateAction.origin && !action.tombstone);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, valid](auto wake) mutable {
      context->requests->run(0, [context, key, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, Zum::ActionID(Zum::MgmtOp::actionState), key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

  auto appSnapshot = [context = context.ptr()]() {
    return ZmBlock<Zum::App>{}([context](auto wake) mutable {
      context->apps->run(0, [context, wake = ZuMv(wake)]() mutable {
        context->apps->find<0>(0, ZuFwdTuple(Zum::AppID{9003}),
          [wake = ZuMv(wake)](ZdbRowRef<Zum::App> row) mutable {
            wake(row ? Zum::App{row->data()} : Zum::App{});
          });
      });
    });
  };
  auto expectedApp = appSnapshot();
  ZuCheck(expectedApp.id == 9003);
  for (unsigned test = 0; test < 7; ++test) {
    bool valid = test != 0 && test != 5;
    bool stateOnly = test != 2;
    Zum::State::T state = test == 3 || test == 4 ?
      Zum::State::Disabled : Zum::State::Active;
    Zum::ActionID op = stateOnly ? Zum::MgmtOp::appState : Zum::MgmtOp::appUpdate;
    Zum::String key, etag;
    key << "app-change-" << test;
    etag << "\"v" << (valid ? expectedApp.version : 99) << '"';
    Zum::AppChangeArgs change{.before = expectedApp, .ifMatch = ZuMv(etag),
      .label = "Updated application", .updated = 114,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotence = key, .expires = 86400, .version = 1,
        .created = 114, .updated = 114},
      .stateOnly = stateOnly, .state = state};
    ZuCheck(runSaga(db, Zum::appChange(ZuMv(change)), ZdbSagaID{9800 + test}) == valid);
    if (valid && (!stateOnly || expectedApp.state != state)) {
      if (stateOnly) {
        expectedApp.state = state;
        ++expectedApp.authVersion;
      } else expectedApp.label = "Updated application";
      ++expectedApp.version;
      expectedApp.updated = 114;
    }
    auto app = appSnapshot();
    ZuCheck(app.id == expectedApp.id && !app.owner && app.name == expectedApp.name &&
      app.label == expectedApp.label && app.state == expectedApp.state &&
      app.version == expectedApp.version && app.authVersion == expectedApp.authVersion &&
      app.created == expectedApp.created && app.updated == expectedApp.updated &&
      app.nextActionID == expectedApp.nextActionID &&
      app.catalogRevision == expectedApp.catalogRevision &&
      app.catalogDigest == expectedApp.catalogDigest);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

  auto expectedUser = loadUser(context, 9100);
  ZuCheck(expectedUser.id == 9100 && expectedUser.state == Zum::State::Pending);
  for (unsigned test = 0; test < 7; ++test) {
    bool stateOnly = test >= 4;
    bool valid = test != 3 && test != 4;
    uint8_t fields = test == 0 ? 3 : test == 1 ? 2 : 1;
    Zum::State::T state = test == 4 ? Zum::State::Active : Zum::State::Suspended;
    Zum::ActionID op = stateOnly ? Zum::MgmtOp::userState : Zum::MgmtOp::userUpdate;
    Zum::String key, etag;
    key << "user-edit-" << test;
    etag << "\"v" << (test == 3 ? 99 : expectedUser.version) << '"';
    Zum::UserEditArgs change{.before = expectedUser, .ifMatch = ZuMv(etag),
      .profile = test == 2 ? "" : "Updated profile", .updated = 115,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotence = key, .expires = 86400, .version = 1,
        .created = 115, .updated = 115},
      .stateOnly = stateOnly, .state = state, .email = "updated@example.test",
      .fields = fields};
    ZuCheck(runSaga(db, Zum::userEdit(ZuMv(change)), ZdbSagaID{9900 + test}) == valid);
    if (valid && (!stateOnly || expectedUser.state != state)) {
      if (stateOnly) {
        expectedUser.state = state;
        ++expectedUser.authVersion;
      } else {
        if (fields & 1U) expectedUser.profile = test == 2 ? "" : "Updated profile";
        if (fields & 2U) expectedUser.email = "updated@example.test";
      }
      ++expectedUser.version;
      expectedUser.updated = 115;
    }
    auto user = loadUser(context, 9100);
    ZuCheck(user.id == 9100 && !user.owner && user.name == expectedUser.name &&
      user.profile == expectedUser.profile && user.email == expectedUser.email &&
      user.state == expectedUser.state && user.version == expectedUser.version &&
      user.authVersion == expectedUser.authVersion && user.updated == expectedUser.updated &&
      user.source == expectedUser.source && user.handle == expectedUser.handle &&
      user.version == expectedUser.version);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }
  auto externalUser = loadUser(context, 9101);
  Zum::String externalETag;
  externalETag << "\"v" << externalUser.version << '"';
  ZuCheck(!runSaga(db, Zum::userEdit(Zum::UserEditArgs{.before = externalUser,
    .ifMatch = ZuMv(externalETag), .updated = 116,
    .request = Zum::IdemRequest{.actorID = "admin",
      .operation = Zum::MgmtOp::userState, .idempotence = "external-state",
      .expires = 86400, .version = 1, .created = 116, .updated = 116},
    .stateOnly = true, .state = Zum::State::Disabled}), ZdbSagaID{9910}));
  auto stillExternal = loadUser(context, 9101);
  ZuCheck(stillExternal.state == externalUser.state &&
    stillExternal.version == externalUser.version &&
    stillExternal.authVersion == externalUser.authVersion && !stillExternal.owner);
  Zum::User pendingUser{.id = 9911, .name = "pending-no-handle",
    .state = Zum::State::Suspended, .version = 1, .authVersion = 1};
  ZuCheck(insertRecord(context->users, pendingUser));
  Zum::UserEditArgs activation{.before = pendingUser, .ifMatch = "\"v1\"",
    .updated = 116, .stateOnly = true, .state = Zum::State::Active};
  ZuCheck(!runSaga(db, Zum::userEdit(ZuMv(activation)), ZdbSagaID{9911}));
  ZuCheck(loadUser(context, 9911).state == Zum::State::Suspended);

  Zum::Cred expectedCred{.id = Zum::Bytes{ZuBSpan{"admin-edit"}},
    .userID = 9100, .publicKey = Zum::Bytes{ZuBSpan{"public-key"}},
    .signCount = 8, .created = 115, .updated = 115,
    .state = Zum::State::Active, .backupEligible = true, .label = "Original"};
  ZuCheck(insertRecord(context->creds, expectedCred));
  for (unsigned test = 0; test < 7; ++test) {
    bool valid = test != 1 && test != 4 && test != 6;
    bool stateOnly = test >= 2 && test <= 5;
    Zum::State::T state = test == 2 || test == 3 ?
      Zum::State::Disabled : Zum::State::Active;
    Zum::ActionID op = stateOnly ?
      Zum::MgmtOp::credentialState : Zum::MgmtOp::credentialUpdate;
    Zum::String key, etag;
    key << "credential-edit-" << test;
    etag << "\"v" << (test == 1 || test == 4 ? 99 : expectedCred.version) << '"';
    Zum::CredEditArgs change{.before = expectedCred, .ifMatch = ZuMv(etag),
      .label = "Renamed credential", .updated = 116,
      .request = Zum::IdemRequest{.actorID = "admin", .operation = op,
        .idempotence = key, .expires = 86400, .version = 1,
        .created = 116, .updated = 116},
      .stateOnly = stateOnly, .state = state};
    // Authentication counters may have advanced within the same timestamp.
    // Administrative edits must not replace those unrelated fields.
    change.before.signCount = 7;
    change.before.backedUp = true;
    if (test == 6) --change.before.updated;
    ZuCheck(runSaga(db, Zum::credEdit(ZuMv(change)), ZdbSagaID{9920 + test}) == valid);
    if (valid && (!stateOnly || expectedCred.state != state)) {
      if (stateOnly) expectedCred.state = state;
      else expectedCred.label = "Renamed credential";
      ++expectedCred.version;
      expectedCred.updated = 116;
    }
    auto cred = loadCred(context, expectedCred.id);
    ZuCheck(cred.id == expectedCred.id && !cred.owner &&
      cred.userID == expectedCred.userID && cred.publicKey == expectedCred.publicKey &&
      cred.label == expectedCred.label && cred.state == expectedCred.state &&
      cred.version == expectedCred.version && cred.updated == expectedCred.updated &&
      cred.created == expectedCred.created && cred.signCount == 8 &&
      cred.backupEligible && !cred.backedUp && cred.userVersion == expectedCred.userVersion);
    ZuCheck(ZmBlock<bool>{}([context = context.ptr(), key, op, valid](auto wake) mutable {
      context->requests->run(0, [context, key, op, valid, wake = ZuMv(wake)]() mutable {
        context->requests->find<0>(0, ZuFwdTuple(Zum::ActorKind::User,
          Zum::String{"admin"}, op, key),
          [valid, wake = ZuMv(wake)](ZdbRowRef<Zum::IdemRequest> row) mutable {
            wake(valid ? row && row->data().status == Zum::RequestStatus::Complete &&
              !row->data().owner && row->data().version == 2 &&
              !row->data().resultIDs : !row);
          });
      });
    }));
  }

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
  ZuTestCall(runtimeManagement);
  return 0;
}
