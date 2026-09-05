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

struct LiveSaga {
  using Type = ZuStringT<"replSaga">;
  enum { NSteps = 2 };

  uint64_t orderID;

  struct Context {
    ZdbTable<Order>	*orders = nullptr;
    ZmSemaphore	*paused = nullptr;
    unsigned		runs = 0;
    unsigned		inserts = 0;
    unsigned		updates = 0;
    unsigned		completed = 0;
    unsigned		errors = 0;
    Zdb_::SagaStepComplete pausedComplete;
  };

  ZdbSagaStep(0, o, Insert) {
    auto context = static_cast<Context *>(context_);
    ++context->runs;
	context->orders->run(0, [
	  this, context, saga, complete = ZuMv(complete)
	]() mutable {
	ZdbObjRef<Order> order = new ZdbObject<Order>{context->orders, 0};
	saga->insert(context->orders, ZuMv(order),
	  [context, complete = ZuMv(complete), orderID = orderID](
	      ZdbObject<Order> *object) mutable {
	    ++context->inserts;
	    if (!object) { complete(false); return; }
	    new (object->ptr()) Order{
	      "IBM", orderID, "FIX0", "repl", 0, Side::Buy, {100}, {7}};
	    object->commit();
	    complete(true);
	  });
	});
	return {};
  }

  ZdbSagaStep(1, o, Update) {
	auto context = static_cast<Context *>(context_);
	++context->runs;
	context->orders->run(0, [
	  this, context, saga, complete = ZuMv(complete)
	]() mutable {
	saga->findUpd<0>(context->orders, 0, ZuFwdTuple("IBM", orderID),
	  [context, complete = ZuMv(complete)](
	      ZdbObject<Order> *object) mutable {
	    ++context->updates;
	    if (!object) { complete(false); return; }
	    ++object->data().qtys[0];
	    object->commit();
	    if (context->paused) {
	      context->pausedComplete = ZuMv(complete);
	      context->paused->post();
	      return;
	    }
	    complete(true);
	  });
	});
	return {};
  }
};
ZfbStruct(LiveSaga,
  (((orderID), (Ctor<0>)), (UInt64)));

using Sagas = ZuTypeList<LiveSaga>;

struct ReplDB : public ZdbSagaDB<Sagas> {
  LiveSaga::Context context;
  ZmSemaphore	*up = nullptr;
  unsigned	ups = 0;
  unsigned	doneAtUp = 0;
};

} // zdbtest

using namespace ZuTestUtil;

static void recovery()
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
	db->doneAtUp = db->context.completed;
	db->up->post();
      }
    }, store);
    db->sagas(ZdbSagaHandler{
      .context = &db->context,
      .doneFn = [](void *context_, ZuCSpan, ZdbSagaID) {
	++static_cast<zdbtest::LiveSaga::Context *>(context_)->completed;
      },
      .errorFn = [](void *context_, ZuCSpan, ZdbSagaID, ZeException) {
	++static_cast<zdbtest::LiveSaga::Context *>(context_)->errors;
      }
    });
  };
  init(leader, firstStore, "0");
  init(follower, secondStore, "1");
  auto firstOrders = leader->initTable<zdbtest::Order>("o");
  auto secondOrders = follower->initTable<zdbtest::Order>("o");
  leader->context.orders = firstOrders;
  follower->context.orders = secondOrders;
  ZmSemaphore firstUp, secondUp, paused, replicated, started;
  leader->up = &firstUp;
  follower->up = &secondUp;
  leader->context.paused = &paused;
  unsigned writes = 0; // committing-shard-owned; observed through replicated
  secondStore->writeFn([&writes, &replicated](ZuCSpan id) {
    if (id == "o" && ++writes == 2) replicated.post();
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
      wake(!follower->active() && !follower->ups && !follower->context.runs);
    });
  });
  ZuCheck(standby);

  using M = ZdbMSaga<zdbtest::Sagas>;
  ZmRef<M> saga = new M{};
  saga->init(zdbtest::LiveSaga{42});
  bool admitted = ZmBlock<bool>{}([
      leader = leader.ptr(), saga = ZuMv(saga)](auto wake) mutable {
    leader->saga(0, 1, ZuMv(saga),
      [wake = ZuMv(wake)](ZdbSagaSubmitResult result) mutable {
	wake(result.template is<void>());
      });
  });
  ZuCheck(admitted);
  paused.wait();
  replicated.wait();
  // Test coordination only: finish the observed follower store write.
  secondStore->sync();
  ZuCheck(leader->context.inserts == 1 && leader->context.updates == 1);
  leader->run([leader = leader.ptr()]() {
    leader->fail();
    leader->context.pausedComplete(false);
  });
  ZuCheck(leader->stop());
  secondUp.wait();
  ZuCheck(follower->ups == 1 && follower->doneAtUp == 1);
  ZuCheck(follower->context.runs == 2);
  ZuCheck(!follower->context.inserts && !follower->context.updates);
  ZuCheck(!leader->context.errors && !follower->context.errors);

  bool once = ZmBlock<bool>{}([
      orders = secondOrders.ptr()](auto wake) {
    orders->run(0, [orders, wake = ZuMv(wake)]() mutable {
      orders->find<0>(0, ZuFwdTuple("IBM", UINT64_C(42)),
	[wake = ZuMv(wake)](ZdbObjRef<zdbtest::Order> object) mutable {
	  wake(object && object->data().qtys[0] == 8);
	});
    });
  });
  ZuCheck(once);
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
  ZuTestCall(recovery);
  ZiLog::stop();
  return 0;
}
