//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// PostgreSQL saga journaling/recovery across two distinct shard threads.

#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZtPlatform.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zdb.hh>

#include "ZdbTest.hh"

using namespace ZuTestUtil;

namespace zdbtest {

namespace Fault { enum { None, NoCommit }; }

using PauseFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.PQ.Pause">>;

struct Context : public ZmPolymorph {
  ZdbTable<Order>	*orders = nullptr;
  ZdbTable<Order>	*items = nullptr;
  ZmSemaphore	*paused = nullptr;
  ZmSemaphore	*done = nullptr;
  unsigned		runs = 0;
  unsigned		inserts = 0;
  unsigned		updates = 0;
  unsigned		deletes = 0;
  unsigned		errors = 0;
  unsigned		fault = Fault::None;
  bool		pauseReverse = false;
  uint32_t	pauseAt = UINT32_MAX;
  bool		holdIntent = false;
  ZtArray<int>	dirs;
  PauseFn	pausedComplete;
};

// Reuse the test intent schema; runtime context is deliberately not saved.
struct LiveSaga : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"pqSaga1">;
  enum { NSteps = 3 };
  uint64_t orderID = 0;

  ZdbSagaStep(0, pq_saga_order, Insert) {
    ++context->runs;
	context->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	if constexpr (Fwd) {
	  ZdbRowRef<Order> row = new ZdbRow<Order>{context->orders, 0};
	  saga->insert(context->orders, ZuMv(row), ZuMv(complete),
	  [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++context->inserts;
	    new (row->ptr()) Order{
	      "SAGA", orderID, "PQ", "saga", 0, Side::Buy, {100}, {7}};
	    complete(bool(row->commit()));
	  });
	} else {
	  saga->findDel<0>(context->orders, 0, ZuFwdTuple("SAGA", orderID),
	    ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	      if (context->pauseReverse) {
		context->pausedComplete = PauseFn{
		  [complete = ZuMv(complete)](bool ok) mutable { complete(ok); }};
		context->paused->post();
		return;
	      }
	      complete(bool(row->commit()));
	    });
	}
	});
	return {};
  }

  ZdbSagaStep(1, pq_saga_order, Update) {
	++context->runs;
	context->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	saga->findUpd<0>(context->orders, 0, ZuFwdTuple("SAGA", orderID),
	  ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++context->updates;
	    if constexpr (Fwd)
	      ++row->data().qtys[0]; // UN replay must not increment this twice
	    else
	      --row->data().qtys[0];
	    if (context->fault == Fault::NoCommit) { complete(false); return; }
	    complete(bool(row->commit()));
	  });
	});
	return {};
  }

  ZdbSagaStep(2, pq_saga_item, Delete) {
	ZuAssert(Fwd);
	++context->runs;
	++context->deletes;
	if (context->paused) {
	  context->pausedComplete = PauseFn{
	    [complete = ZuMv(complete)](bool ok) mutable { complete(ok); }};
	  context->paused->post();
	  return {};
	}
	context->items->run(1,
	  [this, complete = ZuMv(complete)]() mutable {
	  saga->findDel<0>(context->items, 1, ZuFwdTuple("SAGA", orderID),
	  ZuMv(complete), [](ZdbRow<Order> *row, auto &&complete) mutable {
	    complete(bool(row->commit()));
	  });
	});
	return {};
  }
};

ZfbStruct(, LiveSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

struct BatchSaga : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"pqBatch1">;
  enum { NSteps = 3 };
  ZtArray<uint64_t> ids;
  uint32_t failAt = UINT32_MAX;

  template <bool Fwd, typename Complete>
  bool begin(Complete &complete) {
    context->dirs.push(Fwd ? int(saga->step() + 1) : -int(saga->step() + 1));
    if constexpr (Fwd) {
      if (saga->step() == failAt) { complete(false); return false; }
      if (!context->holdIntent && hold(complete)) return false;
    }
    return true;
  }

  template <typename Complete>
  bool hold(Complete &complete) {
    if (!context->paused || saga->step() != context->pauseAt) return false;
    context->pausedComplete = PauseFn{
      [complete = ZuMv(complete)](bool ok) mutable { complete(ok); }};
    context->paused->post();
    return true;
  }

  ZdbSagaRepeatStep(0, pq_saga_order, Insert, ids.length()) {
    if (!begin<Fwd>(complete)) return {};
    auto id = ids[saga->iteration()];
    auto shard = ZdbShard(saga->iteration() & 1);
    context->orders->run(shard, [
      this, id, shard, complete = ZuMv(complete)]() mutable {
      if constexpr (Fwd) {
	ZdbRowRef<Order> row = new ZdbRow<Order>{context->orders, shard};
	saga->insert(context->orders, ZuMv(row), ZuMv(complete),
	  [this, id](ZdbRow<Order> *row, auto &&complete) mutable {
	    ZtString name;
	    name << id;
	    new (row->ptr()) Order{
	      "BATCH", id, "BATCH", name, id, Side::Buy, {100}, {7}};
	    if (context->holdIntent && hold(complete)) return;
	    complete(bool(row->commit()));
	  });
      } else {
	saga->findDel<0>(context->orders, shard, ZuFwdTuple("BATCH", id),
	  ZuMv(complete), [](ZdbRow<Order> *row, auto &&complete) mutable {
	    complete(bool(row->commit()));
	  });
      }
    });
    return {};
  }

  ZdbSagaRepeatStep(1, pq_saga_order, Update, ids.length()) {
    if (!begin<Fwd>(complete)) return {};
    auto id = ids[saga->iteration()];
    auto shard = ZdbShard(saga->iteration() & 1);
    context->orders->run(shard, [
      this, id, shard, complete = ZuMv(complete)]() mutable {
      saga->findUpd<0>(context->orders, shard, ZuFwdTuple("BATCH", id),
	ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  if (Fwd && context->holdIntent && hold(complete)) return;
	  row->data().qtys[0] += Fwd ? 1 : -1;
	  complete(bool(row->commit()));
	});
    });
    return {};
  }

  ZdbSagaStep(2, pq_saga_item, Delete) {
    ZuAssert(Fwd);
    if (!begin<Fwd>(complete)) return {};
    context->items->run(1, [this, complete = ZuMv(complete)]() mutable {
      saga->findDel<0>(context->items, 1, ZuFwdTuple("BATCH", UINT64_C(0)),
	ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	  if (context->holdIntent && hold(complete)) return;
	  complete(bool(row->commit()));
	});
    });
    return {};
  }
};
ZfbStruct(, BatchSaga,
  (((ids), (Ctor<0>)), (UInt64Vec)),
  (((failAt), (Ctor<1>)), (UInt32)));

struct SagaCatalog {
  using List = ZuTypeList<LiveSaga, BatchSaga>;
  static int match(ZuCSpan type) {
    struct IDs { using Keys = Zdb_::SagaTypes<List>; };
    static constexpr auto matcher = ZuMatcher<IDs>();
    return matcher.exact(type);
  }
};

} // zdbtest

using DB = ZdbSagaDB<zdbtest::Context, zdbtest::SagaCatalog>;

namespace Mode { enum { Live, Reconnect, Crash, Replay, NoCommit }; }
enum { CrashStatus = 73 }; // distinguishes the deliberate cut from a failed child
enum { FailureStatus = 74 };

struct Options {
  bool crash = false;
  bool uncommitted = false;
};
ZfStruct(, (Options, CLI),
  (((crash), (CLI::Flag<'c'>, CLI::Long<"crash">)), (Bool)),
  (((uncommitted), (CLI::Flag<'u'>, CLI::Long<"uncommitted">)), (Bool)));

static ZuPtr<const ZfCf::AnyNode> config()
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  ZfCf::DefVal module;
  if (auto path = Zt::getpath("ZDB_MODULE")) module = path;
  defines->add(ZfCf::DefKey{"MODULE"}, ZuMv(module));
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{getenv("ZDB_CONNECT")});
  auto scan = ZfCf::scan(
    "thread: zdb, shards: 4, threads: [saga0, saga1, saga2, saga3],\n"
    "hostID: self, hosts: {self: {standalone: true}},\n"
    "store: {thread: store, replicated: true,\n"
    "  module: ${MODULE}, connection: ${CONNECT}},\n"
    "tables: {pq_saga_order: {cacheMode: All},\n"
    "  pq_saga_item: {cacheMode: All}},\n"
    "mx: {nThreads: 8, rxThread: rx, txThread: tx, threads: {\n"
    "  1: {name: rx, isolated: true},\n"
    "  2: {name: tx, isolated: true},\n"
    "  3: {name: zdb, isolated: true},\n"
    "  4: {name: store, isolated: true},\n"
    "  5: {name: saga0, isolated: true},\n"
    "  6: {name: saga1, isolated: true},\n"
    "  7: {name: saga2, isolated: true},\n"
    "  8: {name: saga3, isolated: true}}}\n", {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

static bool erase(ZdbTable<zdbtest::Order> *table, ZdbShard shard)
{
  return ZmBlock<bool>{}([table, shard](auto wake) {
    table->run(shard, [table, shard, wake = ZuMv(wake)]() mutable {
      table->findDel<0>(shard, ZuFwdTuple("SAGA", UINT64_C(1)),
	[wake = ZuMv(wake)](ZdbRow<zdbtest::Order> *row) mutable {
	  wake(!row || row->commit());
	});
    });
  });
}

static bool seed(ZdbTable<zdbtest::Order> *table)
{
  return ZmBlock<bool>{}([table](auto wake) {
    table->run(1, [table, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<zdbtest::Order> row = new ZdbRow<zdbtest::Order>{table, 1};
      table->insert(row,
	[wake = ZuMv(wake)](ZdbRow<zdbtest::Order> *row) mutable {
	  if (!row) { wake(false); return; }
	  new (row->ptr()) zdbtest::Order{
	    "SAGA", 1, "PQ", "saga", 0, zdbtest::Side::Buy, {100}, {1}};
	  wake(row->commit());
	});
    });
  });
}

static void exercise(unsigned mode)
{
  ZuTestScopeRT(exercise);
  auto cf = config();
  ZiMultiplex mx{ZvMxParams("mx", cf->resolve("mx"))};
  ZuCheckRT(mx.start());
  ZmRef<DB> db = new DB{};
  db->init(ZdbCf{cf}, &mx, {});
  auto orders = db->initTable<zdbtest::Order>("pq_saga_order");
  auto items = db->initTable<zdbtest::Order>("pq_saga_item");
  ZmRef<zdbtest::Context> context = new zdbtest::Context{};
  context->orders = orders;
  context->items = items;
  auto contextPtr = context.ptr();
  db->sagas(ZuMv(context));
  bool started = db->start();
  ZuCheckRT(started);
  if (!started) {
    orders = {}; items = {};
    db->final();
    mx.stop();
    return;
  }

  ZmSemaphore paused;
  ZmSemaphore done;
  if (mode != Mode::Replay) {
    // Remove only this fixture's rows; repeated test runs share the test DB.
    ZuCheckRT(erase(orders, 0));
    ZuCheckRT(erase(items, 1));
    ZuCheckRT(seed(items));
    contextPtr->paused = mode != Mode::Live ? &paused : nullptr;
    contextPtr->done = &done;
    contextPtr->fault =
      mode == Mode::NoCommit || mode == Mode::Crash ?
      zdbtest::Fault::NoCommit : zdbtest::Fault::None;
    contextPtr->pauseReverse = mode == Mode::Crash;
    ZmRef<ZdbMSaga<zdbtest::SagaCatalog>> saga =
      new ZdbMSaga<zdbtest::SagaCatalog>{};
    saga->init(zdbtest::LiveSaga{{}, 1});
    bool admitted = ZmBlock<bool>{}([
      db = db.ptr(), saga = ZuMv(saga), context = contextPtr
    ](auto wake) mutable {
      // The main row belongs to neither of the target shards (0 and 1).
      db->saga(2, 1, ZuMv(saga),
	[wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
	[context](bool ok) {
	  if (!ok) ++context->errors;
	  if (context->done) context->done->post();
	});
    });
    ZuCheckRT(admitted);
    if (mode != Mode::Live) {
      if (mode == Mode::NoCommit) {
	done.wait();
	bool stopped = db->stop();
	_Exit(stopped && contextPtr->errors == 1 && contextPtr->runs == 3 &&
	  contextPtr->inserts == 1 &&
	  contextPtr->updates == 1 ? FailureStatus : 1);
      }
      paused.wait();
      ZuCheckRT(contextPtr->inserts == 1);
      ZuCheckRT(contextPtr->updates == 1);
      if (mode == Mode::Crash) {
	// A backend SELECT establishes this exact durable cut. Do not call
	// stop/final or run destructors: all DB state is lost with the process.
	bool durable = false;
	ZmBlock<>{}([table = orders.ptr(), &durable](auto wake) {
	  table->selectRows<0>({}, 1,
	    [&durable, wake = ZuMv(wake)](auto result, unsigned) mutable {
	      using Tuple = ZdbTable<zdbtest::Order>::Tuple;
	      if (result.template is<Tuple>()) {
		auto &row = result.template p<Tuple>();
		durable = row.template p<1>() == 1 &&
		  row.template p<7>().length() == 1 && row.template p<7>()[0] == 7;
	      } else wake();
	    });
	});
	_Exit(admitted && durable && contextPtr->runs == 3 &&
	  contextPtr->inserts == 1 &&
	  contextPtr->updates == 1 ? CrashStatus : 1);
      }
      db->run([db = db.ptr(), context = contextPtr]() {
	db->fail();
	context->pausedComplete(false);
      });
      // This intentionally interrupted saga is preserved; stop flushes the
      // driver before a fresh connection reopens it. This is not a hard-crash test.
      ZuCheckRT(db->stop());
      orders = {}; items = {};
      db->final();
      db->init(ZdbCf{cf}, &mx, {});
      orders = db->initTable<zdbtest::Order>("pq_saga_order");
      items = db->initTable<zdbtest::Order>("pq_saga_item");
      context = new zdbtest::Context{};
      context->orders = orders;
      context->items = items;
      context->done = &done;
      contextPtr = context.ptr();
      db->sagas(ZuMv(context));
      started = db->start();
      ZuCheckRT(started);
      if (!started) {
	orders = {}; items = {};
	db->final();
	mx.stop();
	return;
      }
    }
    // A reconstructed saga has no callback from the old live submission.
    // start() has already waited for replay and activation in reconnect mode.
    if (mode != Mode::Reconnect) done.wait();
  }
  ZuCheckRT(contextPtr->runs == 3);
  ZuCheckRT(contextPtr->inserts == unsigned(mode == Mode::Live));
  ZuCheckRT(contextPtr->updates ==
    unsigned(mode == Mode::Live || mode == Mode::Replay));
  ZuCheckRT(contextPtr->deletes == 1);

  bool updated = ZmBlock<bool>{}([table = orders.ptr()](auto wake) {
    table->run(0, [table, wake = ZuMv(wake)]() mutable {
      table->find<0>(0, ZuFwdTuple("SAGA", UINT64_C(1)),
	[wake = ZuMv(wake)](ZdbRowRef<zdbtest::Order> row) mutable {
	  wake(row && row->data().qtys[0] == 8);
	});
    });
  });
  ZuCheckRT(updated);
  bool clean = ZmBlock<bool>{}([db = db.ptr()](auto wake) {
    db->run([db, wake = ZuMv(wake)]() mutable {
      wake(db->table("saga")->count() == 0 &&
	db->table("saga_step")->count() == 0 &&
	db->table("pq_saga_item")->count() == 0);
    });
  });
  ZuCheckRT(clean);
  ZuCheckRT(db->stop());
  orders = {}; items = {};
  db->final();
  db = {};
  ZuCheckRT(mx.stop());
}

static void batches(
    unsigned count, bool recover, bool fail = false, bool expired = false)
{
  ZuTestScopeRT(batches);
  using M = ZdbMSaga<zdbtest::SagaCatalog>;
  auto cf = config();
  ZiMultiplex mx{ZvMxParams("mx", cf->resolve("mx"))};
  ZuCheckRT(mx.start());
  auto cuts = recover ? (fail ? 2 : 2 * (2 * count + 1)) : 1;
  for (unsigned cut = 0; cut < cuts; ++cut) {
    ZmRef<DB> db = new DB{};
    ZdbTblRef<zdbtest::Order> orders, items;
    ZmRef<zdbtest::Context> context;
    auto start = [&db, &orders, &items, &context, &cf, &mx]() {
      db->init(ZdbCf{cf}, &mx, {});
      orders = db->initTable<zdbtest::Order>("pq_saga_order");
      items = db->initTable<zdbtest::Order>("pq_saga_item");
      context = new zdbtest::Context{};
      context->orders = orders;
      context->items = items;
      db->sagas(context);
      return db->start();
    };
    auto stop = [&db, &orders, &items, &context]() {
      bool ok = db->stop(); // drains PostgreSQL and shard callbacks
      orders = {}; items = {}; context = {};
      db->final();
      return ok;
    };
    ZuCheckRT(start());
    ZuCheckRT(ZmBlock<bool>{}([items = items.ptr()](auto wake) {
      items->run(1, [items, wake = ZuMv(wake)]() mutable {
	ZdbRowRef<zdbtest::Order> row = new ZdbRow<zdbtest::Order>{items, 1};
	items->insert(row, [wake = ZuMv(wake)](ZdbRow<zdbtest::Order> *row) mutable {
	  if (!row) { wake(false); return; }
	  new (row->ptr()) zdbtest::Order{
	    "BATCH", 0, "BATCH", "marker", 0, zdbtest::Side::Buy, {100}, {1}};
	  wake(bool(row->commit()));
	});
      });
    }));
    zdbtest::BatchSaga def;
    for (unsigned i = 0; i < count; ++i) def.ids.push(100 + i);
    if (fail) def.failAt = count + 1;
    ZmRef<M> saga = new M{};
    saga->init(ZuMv(def));
    ZuCheckRT(M::stepCount(saga.ptr()) == 2 * count + 1);
    ZmSemaphore submitted, done, paused;
    bool admitted = false, outcome = true;
    if (recover) {
      context->paused = &paused;
      context->pauseAt = fail ? count : cut / 2;
      context->holdIntent = cut & 1;
    }
    ZuCheckRT(db->saga(2, 400 + cut, ZuMv(saga),
      [&admitted, &submitted](bool ok) { admitted = ok; submitted.post(); },
      [&outcome, &done](bool ok) { outcome = ok; done.post(); },
      expired ? ZuTime{0} : ZuTime{}));
    submitted.wait();
    ZuCheckRT(admitted);
    if (recover) {
      paused.wait();
      db->run([db = db.ptr(), context = context.ptr()]() {
	db->fail();
	context->pausedComplete(false);
      });
      ZuCheckRT(stop());
      ZuCheckRT(start());
      // Recovered work has no callback from the interrupted live instance.
    } else {
      done.wait();
      ZuCheckRT(outcome == !fail);
    }
    if (expired) {
      auto reverses = cut / 2;
      ZuCheckRT(context->dirs.length() == reverses);
      for (unsigned i = 0; i < reverses; ++i)
	ZuCheckRT(context->dirs[i] == -int(reverses - i));
    } else {
      auto forwards = fail ? count + 2 : 2 * count + 1;
      auto reverses = fail ? count + 1 : 0;
      ZuCheckRT(context->dirs.length() == forwards + reverses);
      for (unsigned i = 0; i < forwards; ++i)
	ZuCheckRT(context->dirs[i] == int(i + 1));
      for (unsigned i = 0; i < reverses; ++i)
	ZuCheckRT(context->dirs[forwards + i] == -int(reverses - i));
    }
    for (unsigned i = 0; i < count; ++i) {
      ZuCheckRT(ZmBlock<bool>{}([
	orders = orders.ptr(), id = uint64_t(100 + i),
	shard = ZdbShard(i & 1), fail, expired
      ](auto wake) {
	orders->run(shard, [
	  orders, id, shard, fail, expired, wake = ZuMv(wake)]() mutable {
	  orders->find<0>(shard, ZuFwdTuple("BATCH", id), [
	    orders, id, shard, fail, expired, wake = ZuMv(wake)
	  ](ZdbRowRef<zdbtest::Order> row) mutable {
	    bool valid = fail || expired ? !row :
	      row && row->data().qtys[0] == 8;
	    orders->findDel<0>(shard, ZuFwdTuple("BATCH", id),
	      [valid, wake = ZuMv(wake)](ZdbRow<zdbtest::Order> *row) mutable {
		wake(valid && (!row || row->commit()));
	      });
	  });
	});
      }));
    }
    ZuCheckRT(ZmBlock<bool>{}([items = items.ptr(), fail, expired](auto wake) {
      items->run(1, [items, fail, expired, wake = ZuMv(wake)]() mutable {
	items->findDel<0>(1, ZuFwdTuple("BATCH", UINT64_C(0)),
	  [fail, expired, wake = ZuMv(wake)](
	      ZdbRow<zdbtest::Order> *row) mutable {
	    bool valid = bool(row) == (fail || expired);
	    wake(valid && (!row || row->commit()));
	  });
      });
    }));
    ZuCheckRT(ZmBlock<bool>{}([db = db.ptr()](auto wake) {
      db->run([db, wake = ZuMv(wake)]() mutable {
	wake(!db->table("saga")->count() && !db->table("saga_step")->count());
      });
    }));
    ZuCheckRT(stop());
  }
  ZuCheckRT(mx.stop());
}

static Zdb_::OpenResult open(
    Zdb_::Store *store, bool internal, Zdb_::IDString id)
{
  using namespace Zdb_;
  using zdbtest::Order;
  return ZmBlock<OpenResult>{}([store, internal, id = ZuMv(id)](auto wake) mutable {
    store->open(internal, ZuMv(id), ZfVFields<Order>(), ZfVKeyFields<Order>(),
      reflection::GetSchema(ZfbSchema<Order>::data()), Table<Order>::allocBuf,
      [wake = ZuMv(wake)](OpenResult result) mutable {
	if (result.is<ZeException>()) ZiLogEvent(result.p<ZeException>());
	wake(ZuMv(result));
      });
  });
}

static void close(Zdb_::StoreTbl *table)
{
  ZmBlock<>{}([table](auto wake) {
    table->close([wake = ZuMv(wake)]() mutable { wake(); });
  });
}

static bool write(Zdb_::OpenData &state, ZuCSpan id, int vn, int qty)
{
  using namespace Zdb_;
  using zdbtest::Order;
  UN un = state.un[0] == nullUN() ? 0 : state.un[0] + 1;
  SN sn = state.sn + 1;
  Order row{"NS", 1, "NS", "probe", 0, zdbtest::Side::Buy, {100}, {qty}};
  Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
  auto data = Zfb::Save::nest(fbb, [&row](Zfb::Builder &fbb) {
    return ZfbStruct::save(fbb, row).Union();
  });
  auto sn_ = ZfbTransform::UInt128::save(sn);
  fbb.Finish(fbs::CreateMsg(fbb, fbs::Body::Replication,
    fbs::CreateRecord(fbb, Zfb::Save::str(fbb, id),
      un, &sn_, vn, 0, data).Union()));
  bool ok = ZmBlock<bool>{}([table = state.storeTbl, buf = saveHdr(fbb)](auto wake) mutable {
    table->write(ZuMv(buf),
      [wake = ZuMv(wake)](ZmRef<IOBuf>, CommitResult result) mutable {
	wake(result.is<void>());
      });
  });
  if (ok) { state.un[0] = un; state.sn = sn; }
  return ok;
}

static int read(Zdb_::StoreTbl *table)
{
  using namespace Zdb_;
  using zdbtest::Order;
  Table<Order>::Key<0> key{"NS", 1};
  Zfb::IOBuilder fbb{Table<Order>::allocBuf()};
  fbb.Finish(ZfbStruct::save(fbb, key));
  return ZmBlock<int>{}([table, buf = fbb.buf()](auto wake) mutable {
    table->find(0, ZuMv(buf), [wake = ZuMv(wake)](RowResult result) mutable {
      if (result.is<void>()) { wake(-1); return; }
      if (!result.is<RowData>()) { wake(-2); return; }
      auto buf = ZuMv(result).p<RowData>().buf;
      auto rec = record(msg(buf->hdr()));
      auto fbo = rec ? ZfbStruct::verify<Order>(Zfb::Load::bytes(rec->data())) : nullptr;
      if (!fbo) { wake(-2); return; }
      auto row = ZfbStruct::ctor<Order>(fbo);
      wake(row.qtys.length() == 1 ? row.qtys[0] : -2);
    });
  });
}

static void namespaces(bool maxID)
{
  ZuTestScopeRT(namespaces);
  using namespace Zdb_;
  IDString id{"saga_ns"};
  if (maxID)
    while (id.length() < IDSize_ - 1) id << 'n';
  auto cf = config();
  ZiMultiplex mx{ZvMxParams("mx", cf->resolve("mx"))};
  ZuCheckRT(mx.start());
  ZmRef<Zdb> db = new Zdb{};
  db->init(ZdbCf{cf}, &mx, {});
  auto orders = db->initTable<zdbtest::Order>("pq_saga_order");
  bool started = db->start();
  ZuCheckRT(started);
  if (!started) {
    orders = {};
    db->final();
    db = {};
    ZuCheckRT(mx.stop());
    return;
  }
  auto app = open(db->store(), false, id);
  auto internal = open(db->store(), true, id);
  bool opened = app.is<OpenData>() && internal.is<OpenData>();
  ZuCheckRT(opened);
  if (opened) {
    auto &a = app.p<OpenData>();
    auto &b = internal.p<OpenData>();
    ZuCheckRT(a.storeTbl != b.storeTbl);
    // Clean only this fixture's key if an earlier failed run left it behind.
    if (a.count) ZuCheckRT(write(a, id, -1, 0));
    if (b.count) ZuCheckRT(write(b, id, -1, 0));
    ZuCheckRT(write(a, id, 0, 11));
    ZuCheckRT(write(b, id, 0, 22));
    ZuCheckRT(read(a.storeTbl) == 11 && read(b.storeTbl) == 22);
    ZuCheckRT(write(a, id, 1, 12));
    ZuCheckRT(write(b, id, 1, 23));
    ZuCheckRT(write(a, id, 2, 13)); // deliberately different UN/SN histories
    ZuCheckRT(read(a.storeTbl) == 13 && read(b.storeTbl) == 23);
    close(a.storeTbl);
    close(b.storeTbl);
    auto app2 = open(db->store(), false, id);
    auto internal2 = open(db->store(), true, id);
    bool reopened = app2.is<OpenData>() && internal2.is<OpenData>();
    ZuCheckRT(reopened);
    if (reopened) {
      auto &a2 = app2.p<OpenData>();
      auto &b2 = internal2.p<OpenData>();
      ZuCheckRT(a2.count == 1 && a2.un[0] == a.un[0] && a2.sn == a.sn);
      ZuCheckRT(b2.count == 1 && b2.un[0] == b.un[0] && b2.sn == b.sn);
      ZuCheckRT(read(a2.storeTbl) == 13 && read(b2.storeTbl) == 23);
      ZuCheckRT(write(a2, id, -3, 0));
      ZuCheckRT(write(b2, id, -2, 0));
      ZuCheckRT(read(a2.storeTbl) == -1 && read(b2.storeTbl) == -1);
      close(a2.storeTbl);
      close(b2.storeTbl);
      // Reconnect with retained handles, then repeat with an entirely new store.
      for (unsigned restart = 0; restart < 2; ++restart) {
	ZuCheckRT(db->stop());
	if (restart) {
	  orders = {};
	  db->final();
	  db->init(ZdbCf{cf}, &mx, {});
	  orders = db->initTable<zdbtest::Order>("pq_saga_order");
	}
	bool started = db->start();
	ZuCheckRT(started);
	if (!started) break;
	auto app3 = open(db->store(), false, id);
	auto internal3 = open(db->store(), true, id);
	bool deleted = app3.is<OpenData>() && internal3.is<OpenData>();
	ZuCheckRT(deleted);
	if (deleted) {
	  auto &a3 = app3.p<OpenData>();
	  auto &b3 = internal3.p<OpenData>();
	  // With no rows left, only MRD can restore these positions.
	  ZuCheckRT(a3.count == 0 && a3.un[0] == a2.un[0] && a3.sn == a2.sn);
	  ZuCheckRT(b3.count == 0 && b3.un[0] == b2.un[0] && b3.sn == b2.sn);
	}
	if (app3.is<OpenData>()) close(app3.p<OpenData>().storeTbl);
	if (internal3.is<OpenData>()) close(internal3.p<OpenData>().storeTbl);
      }
    } else {
      if (app2.is<OpenData>()) close(app2.p<OpenData>().storeTbl);
      if (internal2.is<OpenData>()) close(internal2.p<OpenData>().storeTbl);
    }
  } else {
    if (app.is<OpenData>()) close(app.p<OpenData>().storeTbl);
    if (internal.is<OpenData>()) close(internal.p<OpenData>().storeTbl);
  }
  ZuCheckRT(db->stop());
  orders = {};
  db->final();
  db = {};
  ZuCheckRT(mx.stop());
}

static int child(const char *self, const char *option)
{
#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (!pid) {
    // Keep child TAP out of the parent's TAP stream. After fork, only use
    // async-signal-safe operations until exec replaces the threaded process.
    if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(127);
    execl(self, self, option, static_cast<char *>(nullptr));
    _exit(127);
  }
  int status;
  pid_t result;
  do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
  if (result != pid) return -1;
  if (WIFSIGNALED(status)) return -WTERMSIG(status);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
  ZtString<> command;
  ZfCLI::CmdQuote::quote(command, self);
  command << ' ' << option;
  STARTUPINFOA start{};
  start.cb = sizeof(start);
  start.dwFlags = STARTF_USESTDHANDLES;
  start.hStdOutput = start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0,
      nullptr, nullptr, &start, &process)) return -1;
  DWORD status = DWORD(-1);
  if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &status);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return int(status);
#endif
}

static void crash(const char *self)
{
  ZuTestScopeRT(crash);
  bool cut = child(self, "--crash") == CrashStatus;
  ZuCheckRT(cut);
  if (cut) ZuTestCallRT(exercise, Mode::Replay);
}

static void failure(const char *self, const char *option)
{
  ZuTestScopeRT(failure);
  bool failed = child(self, option) == FailureStatus;
  ZuCheckRT(failed);
  if (failed) ZuTestCallRT(exercise, Mode::Live);
}

int main(int argc, char **argv)
{
  Options options;
  if (ZfCLI::load(options, argc, argv) != 1) return 1;
  ZiLog::init("zdbsagatest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  if (options.crash || options.uncommitted) {
    ZuTestMain();
    ZuTestCall(exercise, options.crash ? Mode::Crash : Mode::NoCommit);
    return 1; // a successful cut never returns
  }
  ZuTestMain();
  ZuTestCall(namespaces, false);
  ZuTestCall(namespaces, true);
  ZuTestCall(exercise, Mode::Live);
  ZuTestCall(exercise, Mode::Reconnect);
  ZuTestCall(crash, argv[0]);
  ZuTestCall(failure, argv[0], "--uncommitted");
  ZuTestCall(batches, 0, false);
  ZuTestCall(batches, 3, false);
  ZuTestCall(batches, 3, false, true);
  ZuTestCall(batches, 3, true);
  ZuTestCall(batches, 3, true, true);
  ZuTestCall(batches, 3, true, false, true);
  ZiLog::stop();
}
