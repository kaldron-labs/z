//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZiLog.hh>

#include "ZdbMockStore.hh"
#include "ZdbTestCf.hh"
#include "ZdbSagaTestDefs.hh"

using namespace ZuTestUtil;

static ZuPtr<const ZfCf::AnyNode> config()
{
  auto scan = ZfCf::scan(R"(
    zdb: {
      thread: zdb, threads: [shard], store: {thread: store},
      hostID: self, hosts: {self: {standalone: true, priority: 100}},
      tables: {o: {warmup: false}}
    },
    mx: {
      nThreads: 5,
      threads: {
	1: {name: rx, isolated: true}, 2: {name: tx, isolated: true},
	3: {name: zdb, isolated: true}, 4: {name: store, isolated: true},
	5: {name: shard, isolated: true}
      },
      rxThread: rx, txThread: tx
    }
  )");
  return ZuMv(scan.p<1>());
}

static void execute(uint64_t repeats, bool rollback)
{
  ZuTestScopeRT(execute);
  auto cf = config();
  auto fields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", fields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  using DB = zdbtest::LiveDB;
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{fields.zdb.p<const ZfCf::AnyNode *>()}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveDB> context = db;
  context->orders = orders;
  db->sagas({zdbtest::repeatLiveSagaDef()});
  context->failStep = rollback ? 2 : UINT32_MAX;
  ZuCheckRT(db->start());
  auto saga = zdbtest::repeatLiveSaga(1, repeats);
  ZuCheckRT(saga->nPhases() == 3 && saga->nSteps() == repeats + 2);
  ZmSemaphore submitted, completed;
  bool accepted = false, success = false, ordered = false;
  ZuCheckRT(db->saga(0, 1, saga,
    [&accepted, &submitted](bool ok) { accepted = ok; submitted.post(); },
    [&accepted, &success, &ordered, &completed](bool ok) {
      ordered = accepted;
      success = ok;
      completed.post();
    }));
  submitted.wait();
  completed.wait();
  ZuCheckRT(accepted && ordered && success == !rollback);
  ZuCheckRT(saga->db() == context.ptr());
  ZuCheckRT(context->updates == repeats * (rollback ? 2 : 1));
  ZuCheckRT(context->dirs.length() == (rollback ? repeats * 2 + 3 : repeats + 2));
  bool dirs = true;
  for (unsigned i = 0; i < repeats + 2; ++i)
    dirs &= context->dirs[i] == int(i) + 1;
  if (rollback)
    for (unsigned i = 0; i < repeats + 1; ++i)
      dirs &= context->dirs[repeats + 2 + i] == -int(repeats + 1 - i);
  ZuCheckRT(dirs);
  bool clean = ZmBlock<bool>{}([db, orders](auto wake) {
    db->run([db, orders, wake = ZuMv(wake)]() mutable {
      wake(!orders->count() && !db->table("saga")->count() &&
	!db->table("saga_step")->count());
    });
  });
  ZuCheckRT(clean);
  ZuCheckRT(db->stop());
  orders = {};
  context->orders = nullptr;
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

// Admit no effects, then preserve a corrupt/overflowing main intent for replay.
static void invalidRecovery(bool overflow)
{
  ZuTestScopeRT(invalidRecovery);
  auto cf = config();
  auto fields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
  ZiMultiplex mx{ZvMxParams("mx", fields.mx.p<const ZfCf::AnyNode *>())};
  ZuCheckRT(mx.start());
  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  using DB = zdbtest::LiveDB;
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{fields.zdb.p<const ZfCf::AnyNode *>()}, &mx, {}, store);
  auto orders = db->initTable<zdbtest::Order>("o");
  ZmRef<zdbtest::LiveDB> context = db;
  context->orders = orders;
  auto definition = zdbtest::repeatLiveSagaDef();
  db->sagas({definition});
  ZuCheckRT(db->start());
  unsigned completed = 0;
  auto invalid = zdbtest::repeatLiveSaga(1, UINT64_MAX);
  bool admitted = ZmBlock<bool>{}([db, invalid, &completed](auto wake) {
    db->saga(0, 1, invalid,
      [wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
      [&completed](bool) { ++completed; });
  });
  ZuCheckRT(!admitted && !completed && !context->runs);
  Zdb_::SagaPayload data;
  ZuCheckRT(invalid->save(data) == ZiFB::Error::OK);
  if (!overflow) data.length(0);
  auto table = ZmBlock<ZdbTable<Zdb_::SagaData> *>{}([db](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(static_cast<ZdbTable<Zdb_::SagaData> *>(db->table("saga").ptr()));
    });
  });
  bool stored = ZmBlock<bool>{}([table, data = ZuMv(data), definition](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), definition, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<Zdb_::SagaData> row = new ZdbRow<Zdb_::SagaData>{table, 0};
      bool committed = false;
      table->insert(row, [&committed, &data, definition](ZdbRow<Zdb_::SagaData> *row) mutable {
	if (!row) return;
	new (row->ptr()) Zdb_::SagaData{
	  .type = definition.type, .id = 1, .shard = 0, .data = ZuMv(data)
	};
	committed = bool(row->commit());
      });
      wake(committed);
    });
  });
  ZuCheckRT(stored);
  ZuCheckRT(db->stop());
  orders = {};
  context->orders = nullptr;
  db->final();
  db->init(ZdbCf{fields.zdb.p<const ZfCf::AnyNode *>()}, &mx, {}, store);
  orders = db->initTable<zdbtest::Order>("o");
  context->orders = orders;
  db->sagas({definition});
  ZuCheckRT(!db->start());
  ZuCheckRT(!context->runs);
  orders = {};
  context->orders = nullptr;
  db->final();
  db = {};
  store = {};
  ZuCheckRT(mx.stop());
}

int main(int argc, char **argv)
{
  ZiLog::init("ZdbSagaRepeatTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(execute, 0, false);
  ZuTestCall(execute, 4, false);
  ZuTestCall(execute, 0, true);
  ZuTestCall(execute, 4, true);
  ZuTestCall(invalidRecovery, false);
  ZuTestCall(invalidRecovery, true);
  ZiLog::stop();
  return 0;
}
