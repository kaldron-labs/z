//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>
#include <zlib/Zdb.hh>

#include "ZdbMockStore.hh"
#include "ZdbTest.hh"

namespace zdbtest {

using PauseFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.Saga.Repl.Pause">>;

struct Context : public ZmPolymorph {
  ZdbTable<Order>	*orders = nullptr;
  ZmSemaphore	*paused = nullptr;
  unsigned	runs = 0;
  unsigned	inserts = 0;
  unsigned	updates = 0;
  unsigned	completed = 0;
  unsigned	errors = 0;
  bool		failUpdate = false;
  bool		pauseReverse = false;
  PauseFn	pausedComplete;
};

struct LiveSaga : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"replSaga">;
  enum { NSteps = 2 };

  uint64_t orderID;

  ZdbSagaStep(0, o, Insert) {
    ++context->runs;
	context->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	if constexpr (Fwd) {
	  ZdbRowRef<Order> order = new ZdbRow<Order>{context->orders, 0};
	  saga->insert(context->orders, ZuMv(order), ZuMv(complete),
	  [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++context->inserts;
	    new (row->ptr()) Order{
	      "IBM", orderID, "FIX0", "repl", 0, Side::Buy, {100}, {7}};
	    complete(bool(row->commit()));
	  });
	} else {
	  saga->findDel<0>(context->orders, 0, ZuFwdTuple("IBM", orderID),
	    ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	      if (context->pauseReverse) {
		context->pausedComplete = PauseFn{ZuMv(complete)};
		context->paused->post();
		return;
	      }
	      complete(bool(row->commit()));
	    });
	}
	});
	return {};
  }

  ZdbSagaStep(1, o, Update) {
	ZuAssert(Fwd);
	++context->runs;
	context->orders->run(0,
	  [this, complete = ZuMv(complete)]() mutable {
	saga->findUpd<0>(context->orders, 0, ZuFwdTuple("IBM", orderID),
	  ZuMv(complete), [this](ZdbRow<Order> *row, auto &&complete) mutable {
	    ++context->updates;
	    if (context->failUpdate) { complete(false); return; }
	    ++row->data().qtys[0];
	    bool ok = row->commit();
	    if (context->paused) {
	      context->pausedComplete = PauseFn{ZuMv(complete)};
	      context->paused->post();
	      return;
	    }
	    complete(ok);
	  });
	});
	return {};
  }
};
ZfbStruct(LiveSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

using Sagas = ZuTypeList<LiveSaga>;

struct ReplDB : public ZdbSagaDB<Context, Sagas> {
  Context	*context = nullptr;
  ZmSemaphore	*up = nullptr;
  unsigned	ups = 0;
  unsigned	doneAtUp = 0;
};

} // zdbtest

using namespace ZuTestUtil;

namespace Mode {
  enum { Forward, RetrySuccess, RetryFailure };
}

template <typename T>
static bool empty(Zdb *db, ZuCSpan id)
{
  return ZmBlock<bool>{}([db, id](auto wake) {
    db->run([db, id, wake = ZuMv(wake)]() mutable {
      auto table = static_cast<ZdbTable<T> *>(db->table(id).ptr());
      table->template selectRows<0>({}, 1, [
	  wake = ZuMv(wake), empty = true
      ](auto result, unsigned) mutable {
	using Tuple = typename ZdbTable<T>::Tuple;
	if (result.template is<Tuple>())
	  empty = false;
	else
	  wake(empty);
      });
    });
  });
}

static void recovery(unsigned mode)
{
  ZuTestScope(recovery);
  auto config = ZfCf::scan(
    "thread: zdb, store: {thread: store}, hostID: 0,\n"
    "hosts: {\n"
    "  0: {priority: 100, ip: 127.0.0.1, port: 9945},\n"
    "  1: {priority: 80, ip: 127.0.0.1, port: 9946}\n"
    "}, tables: {o: {}},\n"
    "mx: {nThreads: 4, threads: {\n"
    "  1: {name: rx, isolated: true},\n"
    "  2: {name: tx, isolated: true},\n"
    "  3: {name: zdb, isolated: true},\n"
    "  4: {name: store, isolated: true}\n"
    "}, rxThread: rx, txThread: tx}\n").p<1>();
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());

  ZmRef<zdbtest::ReplDB> leader = new zdbtest::ReplDB{};
  ZmRef<zdbtest::ReplDB> follower = new zdbtest::ReplDB{};
  ZmRef<zdbtest::Store> firstStore = new zdbtest::Store{};
  ZmRef<zdbtest::Store> secondStore = new zdbtest::Store{};
  auto init = [&config, &mx](zdbtest::ReplDB *db,
      zdbtest::Store *store, ZuCSpan id) {
    ZdbCf cf{config};
    cf.hostID = id;
    db->init(ZuMv(cf), &mx, ZdbHandler{
      .upFn = [](Zdb *db_, ZdbHost *) {
	auto db = static_cast<zdbtest::ReplDB *>(db_);
	++db->ups;
	db->doneAtUp = db->context->completed;
	db->up->post();
      }
    }, store);
    ZmRef<zdbtest::Context> context = new zdbtest::Context{};
    db->context = context;
    db->sagas(ZuMv(context));
  };
  init(leader, firstStore, "0");
  init(follower, secondStore, "1");
  auto firstOrders = leader->initTable<zdbtest::Order>("o");
  auto secondOrders = follower->initTable<zdbtest::Order>("o");
  leader->context->orders = firstOrders;
  follower->context->orders = secondOrders;
  if (mode != Mode::Forward) {
    leader->context->failUpdate = true;
    leader->context->pauseReverse = true;
    follower->context->failUpdate = mode == Mode::RetryFailure;
  }
  ZmSemaphore firstUp, secondUp, paused, replicated, started;
  leader->up = &firstUp;
  follower->up = &secondUp;
  leader->context->paused = &paused;
  unsigned writes = 0; // committing-shard-owned; observed through replicated
  secondStore->writeFn([mode, &writes, &replicated](ZuCSpan id) {
    if (mode == Mode::Forward) {
      if (id == "o" && ++writes == 2) replicated.post();
    } else if (id == "saga_step" && ++writes == 3)
      replicated.post();
  });
  bool firstOK = false, secondOK = false;
  leader->start([&firstOK, &started](bool ok) {
    firstOK = ok;
    started.post();
  });
  follower->start([&secondOK, &started](bool ok) {
    secondOK = ok;
    started.post();
  });
  started.wait();
  started.wait();
  ZuCheck(firstOK && secondOK);
  firstUp.wait();
  bool standby = ZmBlock<bool>{}([follower = follower.ptr()](auto wake) {
    follower->run([follower, wake = ZuMv(wake)]() mutable {
      wake(!follower->active() && !follower->ups && !follower->context->runs);
    });
  });
  ZuCheck(standby);

  using M = ZdbMSaga<zdbtest::Sagas>;
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::LiveSaga{{}, 42});
  bool admitted = ZmBlock<bool>{}([
      leader = leader.ptr(), saga = ZuMv(saga)](auto wake) mutable {
    leader->saga(0, 1, ZuMv(saga),
      [wake = ZuMv(wake)](bool ok) mutable { wake(ok); },
      [context = leader->context](bool ok) {
	if (ok) ++context->completed; else ++context->errors;
      });
  });
  ZuCheck(admitted);
  paused.wait();
  replicated.wait();
  // Test coordination only: finish the observed follower store write.
  secondStore->sync();
  ZuCheck(leader->context->inserts == 1 && leader->context->updates == 1);
  leader->run([leader = leader.ptr()]() {
    leader->fail();
    leader->context->pausedComplete(false);
  });
  ZuCheck(leader->stop());
  secondUp.wait();
  ZuCheck(follower->ups == 1 && !follower->doneAtUp);
  ZuCheck(follower->context->runs ==
    unsigned(mode == Mode::RetryFailure ? 3 : 2));
  ZuCheck(!follower->context->inserts);
  ZuCheck(follower->context->updates == unsigned(mode != Mode::Forward));
  ZuCheck(!leader->context->errors && !follower->context->errors);

  bool once = ZmBlock<bool>{}([
      orders = secondOrders.ptr(), mode](auto wake) {
    orders->run(0, [orders, mode, wake = ZuMv(wake)]() mutable {
      orders->find<0>(0, ZuFwdTuple("IBM", UINT64_C(42)),
	[mode, wake = ZuMv(wake)](ZdbRowRef<zdbtest::Order> row) mutable {
	  wake(mode == Mode::RetryFailure ? !row :
	    row && row->data().qtys[0] == 8);
	});
    });
  });
  ZuCheck(once);
  ZuCheck(empty<Zdb_::SagaData>(follower, "saga"));
  ZuCheck(empty<Zdb_::SagaStep>(follower, "saga_step"));
  ZuCheck(follower->stop());
  firstOrders = {};
  secondOrders = {};
  leader->final();
  follower->final();
  leader = {};
  follower = {};
  firstStore = {};
  secondStore = {};
  ZuCheck(mx.stop());
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("zdbsagareptest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(recovery, Mode::Forward);
  ZuTestCall(recovery, Mode::RetrySuccess);
  ZuTestCall(recovery, Mode::RetryFailure);
  ZiLog::stop();
  return 0;
}
