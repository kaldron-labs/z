//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/ZumDB.hh>
#include <zlib/ZumRequest.hh>

using namespace ZuTestUtil;

struct TestDB : public Zum::DB {
  ZmSemaphore	active;
  ZmRef<Zum::Requests> requests;
};

static ZuPtr<const ZfCf::AnyNode> config()
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
  auto *test = static_cast<TestDB *>(db);
  test->requests->activate();
  test->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<TestDB *>(db)->requests->deactivate();
}

static ZmRef<TestDB> startDB(
    const ZfCf::AnyNode *cf, ZiMultiplex &mx,
    const ZmRef<ZdbMem::Store> &store, ZmRef<Zum::DBContext> &context)
{
  ZmRef<TestDB> db = new TestDB{};
  db->requests = new Zum::Requests{};
  if (!db->requests->init(&mx, 5, 8)) return {};
  db->init(ZdbCf{cf->resolve("zdb")}, &mx,
    ZdbHandler{.upFn = dbUp, .downFn = dbDown}, store.ptr());
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
	context->actions->find<0>(0, ZuFwdTuple(id), [
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
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  store->preserve();

  ZmRef<Zum::DBContext> context;
  auto db = startDB(cf, mx, store, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(insertIssuer(context));
  Zum::ActionID first = 0;
  ZuCheck(createAction(context, "orders.read", first));
  auto state = loadState(context, first);
  ZuCheck(state.issuer.nextActionID == first + 1 &&
    state.action.id == first && state.action.name == "orders.read");
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, store, context);
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

  store = {};
  ZuCheck(mx.stop());
}

static void sagaRecovery()
{
  ZuTestScope(sagaRecovery);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
  ZuCheck(mx.start());
  ZmRef<ZdbMem::Store> store = new ZdbMem::Store{};
  store->preserve();

  ZmRef<Zum::DBContext> context;
  auto db = startDB(cf, mx, store, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(stageEnrollment(db, context));
  ZuCheck(stopDB(db, context));

  db = startDB(cf, mx, store, context);
  ZuCheck(bool(db));
  if (!db) return;
  ZuCheck(db->requests->active());
  ZuCheck(enrollmentRecovered(context));
  ZuCheck(stopDB(db, context));

  store = {};
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(restart);
  ZuTestCall(sagaRecovery);
  return 0;
}
