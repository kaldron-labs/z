//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZfCf.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zdb.hh>

#include "ZdbMockStore.hh"
#include "ZdbTest.hh"

using namespace ZuTestUtil;
using namespace zdbtest;

ZuPtr<const ZfCf::AnyNode> cf()
{
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb,\n"
    "  shards: 2,\n"
    "  threads: [zdb0, zdb1],\n"
    "  store: {thread: store},\n"
    "  hostID: self,\n"
    "  heartbeatFreq: 101,\n"
    "  heartbeatTimeout: 102,\n"
    "  reconnectFreq: 103,\n"
    "  electionTimeout: 104,\n"
    "  hosts: {self: {standalone: true}},\n"
    "  tables: {o: {cacheMode: All}}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 6,\n"
    "  threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: zdb0, isolated: true},\n"
    "    6: {name: zdb1, isolated: true}\n"
    "  },\n"
    "  rxThread: rx,\n"
    "  txThread: tx\n"
    "}\n");
  return ZuMv(scan.p<1>());
}

struct Watch {
  void addDB(Ztc::DB *db_) {
    db = db_;
    ++dbAdds;
    Ztc::DBTelemetry data;
    db->telemetry(data);
    dbDataOK = data.self == "self" && data.nHosts == 1;
    hostSeeds += db->allDBHosts(
      {[this](Ztc::DBHost *host) {
	Ztc::DBHostTelemetry data;
	host->telemetry(data);
	hostDataOK = data.dbID == "self" && data.id == "self";
	++hostAdds;
      }});
    tableSeeds += db->allDBTables(
      {[this](Ztc::DBTable *) { ++tableAdds; }});
  }

  void delDB(Ztc::DB *db_) {
    dbDelKeyOK = db_ == db;
    ++dbDels;
  }
  void addHost(Ztc::DBHost *host) {
    Ztc::DBHostTelemetry data;
    host->telemetry(data);
    hostDataOK = data.dbID == "self" && data.id == "self";
    ++hostAdds;
  }
  void delHost(Ztc::DBHost *host) {
    hostDelKeyOK = host->telKey().p<0>() == "self";
    ++hostDels;
  }
  void addTable(Ztc::DBTable *table) {
    Ztc::DBTableTelemetry data;
    table->telemetry(data);
    tableDataOK = data.dbID == "self";
    ++tableAdds;
  }
  void delTable(Ztc::DBTable *table) {
    tableDelKeyOK = table->telKey().p<0>() == "self";
    ++tableDels;
  }

  Ztc::DB	*db = nullptr;
  unsigned	dbAdds = 0;
  unsigned	dbDels = 0;
  unsigned	hostAdds = 0;
  unsigned	hostDels = 0;
  unsigned	tableAdds = 0;
  unsigned	tableDels = 0;
  unsigned	hostSeeds = 0;
  unsigned	tableSeeds = 0;
  bool		dbDataOK = false;
  bool		dbDelKeyOK = false;
  bool		hostDataOK = false;
  bool		hostDelKeyOK = false;
  bool		tableDataOK = false;
  bool		tableDelKeyOK = false;
};

ZmSemaphore *active_;

void up(Zdb *, ZdbHost *)
{
  active_->post();
}

void run()
{
  ZuTestScope(run);
  auto config = cf();

  {
    auto scan = ZfCf::scan("cacheMode: All, shards: 2");
    bool failed = false;
    try {
      ZdbTableCf tableCf{"o", scan.p<1>()};
    } catch (const ZeException &) {
      failed = true;
    }
    ZuCheck(failed);
  }
  {
    auto scan = ZfCf::scan("cacheMode: All, threads: [zdb0]");
    bool failed = false;
    try {
      ZdbTableCf tableCf{"o", scan.p<1>()};
    } catch (const ZeException &) {
      failed = true;
    }
    ZuCheck(failed);
  }
  for (auto source: {
    ZuCSpan{"thread: zdb, shards: 3"},
    ZuCSpan{"thread: zdb, shards: 65"},
    ZuCSpan{"thread: zdb, shards: 4, threads: [a, b, c]"},
    ZuCSpan{"thread: zdb, shards: 1, threads: [a, b]"}
  }) {
    auto scan = ZfCf::scan(source);
    bool failed = false;
    try {
      ZdbCf badCf{scan.p<1>()};
    } catch (const ZeException &) {
      failed = true;
    }
    ZuCheck(failed);
  }
  {
    Zdb_::DBState state{2};
    state.update("o", ZdbShard{1}, 42);
    Zfb::Builder fbb;
    fbb.Finish(state.save(fbb));
    auto fbo = flatbuffers::GetRoot<Zdb_::fbs::DBState>(
      fbb.GetBufferPointer());
    Zdb_::DBState loaded{fbo};
    auto found = loaded.find("o", ZdbShard{1});
    ZuCheck(found);
    ZuCheck(found->p<1>() == 42);
    ZuCheck(!loaded.find("o", ZdbShard{0}));
  }

  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());

  {
    Zdb empty;
    auto emptyCf = ZdbCf{config->resolve("zdb")};
    emptyCf.nShards = 1;
    emptyCf.threads.length(0);
    empty.init(ZuMv(emptyCf), &mx, {}, new zdbtest::Store{});
    ZuCheck(empty.start());
    ZuCheck(empty.stop());
    empty.final();
  }

  {
    Zdb other;
    auto otherCf = ZdbCf{config->resolve("zdb")};
    otherCf.nShards = 1;
    otherCf.threads.length(0);
    other.init(ZuMv(otherCf), &mx, {}, new zdbtest::Store{});
    ZuCheck(other.nShards() == 1);
    auto otherOrders = other.initTable<Order>("o");
    otherOrders = {};
    other.final();
  }

  {
    Zdb bad;
    auto badCf = ZdbCf{config->resolve("zdb")};
    badCf.thread = "missing";
    bool failed = false;
    try {
      bad.init(ZuMv(badCf), &mx, {}, new zdbtest::Store{});
    } catch (const ZeException &) {
      failed = true;
    }
    ZuCheck(failed);
    ZuCheck(Ztc::DBMgr::all({[](Ztc::DB *) { }}) == 0);
  }

  Watch watch;
  Ztc::DBMgr::watch(
    {&watch, ZmFnPtr<&Watch::addDB>{}},
    {&watch, ZmFnPtr<&Watch::delDB>{}},
    {&watch, ZmFnPtr<&Watch::addHost>{}},
    {&watch, ZmFnPtr<&Watch::delHost>{}},
    {&watch, ZmFnPtr<&Watch::addTable>{}},
    {&watch, ZmFnPtr<&Watch::delTable>{}});

  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  store->preserve();
  ZmRef<Zdb> db = new Zdb{};
  ZmSemaphore active;
  active_ = &active;

  auto init = [
    config = config.ptr(), db = db.ptr(), mx = &mx, store
  ]() mutable {
    auto dbCf = ZdbCf{config->resolve("zdb")};
    dbCf.tableCf("p");
    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = up
    }, store);
  };

  init();
  ZuCheck(db->nShards() == 2);
  ZuCheck(db->shardSID(0) == mx.sid("zdb0"));
  ZuCheck(db->shardSID(1) == mx.sid("zdb1"));
  ZtArray<unsigned> shardOK;
  shardOK.length(2);
  ZmSemaphore shardDone;
  for (ZdbShard shard = 0; shard < db->nShards(); ++shard)
    db->shardRun(shard, [
      db = db.ptr(), shard, shardOK = shardOK.data(), shardDone = &shardDone
    ]() {
      bool runOK = db->shardInvoked(shard) &&
	db->mx()->invoked(db->shardSID(shard));
      db->shardInvoke(shard, [
	db, shard, runOK, shardOK, shardDone
      ]() {
	shardOK[shard] = runOK && db->shardInvoked(shard);
	shardDone->post();
      });
    });
  for (unsigned i = 0; i < db->nShards(); ++i) shardDone.wait();
  ZuCheck(shardOK[0]);
  ZuCheck(shardOK[1]);
  ZuCheck(watch.dbAdds == 1);
  ZuCheck(watch.dbDataOK);
  ZuCheck(watch.hostSeeds == 1);
  ZuCheck(watch.hostAdds == 1);
  ZuCheck(watch.hostDataOK);
  ZuCheck(watch.tableSeeds == 0);
  bool dbFound = false;
  ZuCheck(Ztc::DBMgr::all(
    {[db = db.ptr(), dbFound = &dbFound](Ztc::DB *db_) {
      *dbFound = db_ == db;
    }}) == 1);
  ZuCheck(dbFound);

  auto orders = db->initTable<Order>("o");
  auto payments = db->initTable<Order>("p");
  ZuCheck(watch.tableAdds == 2);
  ZuCheck(watch.tableDataOK);
  auto tableKey = orders->telKey();
  ZuCheck(tableKey.p<0>() == "self");
  ZuCheck(tableKey.p<1>() == "o");
  Ztc::DBTableTelemetry tableData;
  orders->telemetry(tableData);
  ZuCheck(tableData.id == "o");
  ZuCheck(tableData.cacheMode == Ztc::DBCacheMode::All);
  ZuCheck(!tableData.count);
  ZuCheck(!tableData.cacheLoads);
  ZuCheck(!tableData.cacheMisses);
  ZuCheck(!tableData.cacheEvictions);

  Ztc::DBTelemetry dbData;
  db->telemetry(dbData);
  ZuCheck(dbData.thread == "zdb");
  ZuCheck(dbData.nShards == 2);
  ZuCheck(dbData.threads.length() == 2);
  ZuCheck(dbData.self == "self");
  ZuCheck(dbData.nTables == 2);
  ZuCheck(dbData.nHosts == 1);
  ZuCheck(dbData.heartbeatFreq == 101);
  ZuCheck(dbData.heartbeatTimeout == 102);
  ZuCheck(dbData.reconnectFreq == 103);
  ZuCheck(dbData.electionTimeout == 104);

  ZuCheck(db->start());
  active.wait();

  ZtArray<ZtString<>> writes;
  store->writeFn({[&writes](ZuCSpan id) { writes.push(id); }});
  ZmSemaphore commitsDone;
  ZmSemaphore callbackDone;
  orders->run(0, [
    orders = orders.ptr(), payments = payments.ptr(),
    writes = &writes, commitsDone = &commitsDone, callbackDone = &callbackDone
  ]() {
    ZdbRowRef<Order> order = new ZdbRow<Order>{orders, 0};
    orders->insert(order, [](ZdbRow<Order> *row) {
      if (!row) return;
      new (row->ptr()) Order{
	"IBM", 1, "FIX0", "order1", 1, Side::Buy, {100}, {1}};
      row->commit();
    });
    orders->run(0, [writes, callbackDone]() {
      writes->push("callback");
      callbackDone->post();
    });
    ZdbRowRef<Order> payment = new ZdbRow<Order>{payments, 0};
    payments->insert(payment, [](ZdbRow<Order> *row) {
      if (!row) return;
      new (row->ptr()) Order{
	"GBP", 1, "FIX0", "payment1", 1, Side::Buy, {100}, {1}};
      row->commit();
    });
    commitsDone->post();
  });
  commitsDone.wait();
  callbackDone.wait();
  ZuCheck(writes.length() == 3);
  ZuCheck(writes[0] == "o");
  ZuCheck(writes[1] == "p");
  ZuCheck(writes[2] == "callback");
  store->writeFn({});

  bool exactUNOK = true;
  unsigned exactUNCallbacks = 0;
  unsigned exactUNFinds = 0;
  ZmSemaphore exactUNDone;
  ZmSemaphore exactUNMissing;
  ZmSemaphore exactUNStable;
  store->findFn({[finds = &exactUNFinds](ZuCSpan id) {
    if (id == "o") ++*finds;
  }});
  store->deferWork(true);
  orders->run(0, [
    orders = orders.ptr(), ok = &exactUNOK,
    callbacks = &exactUNCallbacks, done = &exactUNDone,
    missing = &exactUNMissing
  ]() {
    ZdbRowRef<Order> gap = new ZdbRow<Order>{orders, 0};
    orders->insert(2, gap, [&gap, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (gap->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::NotReady && !row && next == 1;
    });

    ZdbRowRef<Order> null = new ZdbRow<Order>{orders, 0};
    orders->insert(ZdbNullUN(), null, [&null, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (null->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Invalid && !row && next == 1;
    });

    orders->find<0>(0, ZuFwdTuple("IBM", UINT64_C(1)),
	[orders, ok, callbacks](ZdbRowRef<Order> row) {
      *ok &= bool(row);
      orders->insert(1, row, [ok, callbacks](
	  ZdbOpResult::T result, ZdbRow<Order> *row_, ZdbUN next) {
	++*callbacks;
	*ok &= result == ZdbOpResult::Invalid && !row_ && next == 1;
      });
      orders->insert(ZuMv(row), [ok, callbacks](
	  ZdbRow<Order> *row_) {
	++*callbacks;
	*ok &= !row_;
      });
    });

    ZdbRowRef<Order> invalidUpdate = new ZdbRow<Order>{orders, 0};
    orders->update(invalidUpdate, 1, [&invalidUpdate, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (invalidUpdate->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Invalid && !row && next == 1;
    });
    *ok &= invalidUpdate->state() == ZdbRowState::Undefined;

    ZdbRowRef<Order> invalidDel = new ZdbRow<Order>{orders, 0};
    orders->del(invalidDel, 1, [&invalidDel, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (invalidDel->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Invalid && !row && next == 1;
    });
    *ok &= invalidDel->state() == ZdbRowState::Undefined;

    ZdbRowRef<Order> insert = new ZdbRow<Order>{orders, 0};
    orders->insert(1, ZuMv(insert), [orders, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 1;
      *ok &= orders->invoked(0);
      if (!row) return;
      new (row->ptr()) Order{
	"MSFT", 2, "FIX0", "order2", 2, Side::Buy, {101}, {2}};
      *ok &= bool(row->commit());
    });

    ZdbRowRef<Order> replay = new ZdbRow<Order>{orders, 0};
    orders->insert(1, replay, [&replay, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (replay->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Skipped && !row && next == 2;
    });

    orders->findUpd<0>(0, ZuFwdTuple("MISSING", UINT64_C(99)), 2,
	[ok, callbacks, missing](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Skipped && !row && next == 7;
      missing->post();
    });

    orders->findUpd<0>(0, ZuFwdTuple("IBM", UINT64_C(1)), 3,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::NotReady && !row && next == 2;
    });

    orders->findUpd<0, ZuSeq<2>>(0, ZuFwdTuple("IBM", UINT64_C(1)), 2,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 2;
      if (!row) return;
      row->data().seqNo = 2;
      *ok &= bool(row->commit());
    });

    orders->findUpd<0>(0, ZuFwdTuple("IBM", UINT64_C(1)), 2,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Skipped && !row && next == 3;
    });

    orders->findDel<0>(0, ZuFwdTuple("IBM", UINT64_C(1)), 3,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 3;
      if (row) *ok &= bool(row->commit());
    });

    orders->findDel<0>(0, ZuFwdTuple("IBM", UINT64_C(1)), 3,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Skipped && !row && next == 4;
    });
    orders->findDel<0>(0, ZuFwdTuple("IBM", UINT64_C(1)), 5,
	[ok, callbacks](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::NotReady && !row && next == 4;
    });

    ZdbRowRef<Order> nested = new ZdbRow<Order>{orders, 0};
    orders->insert(4, nested, [orders, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 4;
      ZdbRowRef<Order> intervening = new ZdbRow<Order>{orders, 0};
      orders->insert(ZuMv(intervening), [ok, callbacks](
	  ZdbRow<Order> *row_) {
	++*callbacks;
	*ok &= bool(row_);
	if (!row_) return;
	new (row_->ptr()) Order{
	  "ORCL", 3, "FIX0", "order3", 3, Side::Buy, {102}, {3}};
	*ok &= bool(row_->commit());
      });
      if (!row) return;
      new (row->ptr()) Order{
	"META", 4, "FIX0", "order4", 4, Side::Buy, {103}, {4}};
      *ok &= !row->commit();
    });
    *ok &= nested->state() == ZdbRowState::Undefined;

    ZdbRowRef<Order> nestedReplay = new ZdbRow<Order>{orders, 0};
    orders->insert(4, nestedReplay, [&nestedReplay, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (nestedReplay->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Skipped && !row && next == 5;
    });

    ZdbRowRef<Order> aborted = new ZdbRow<Order>{orders, 0};
    orders->insert(5, aborted, [&aborted, ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      new (aborted->ptr()) Order{};
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 5;
    });
    *ok &= aborted->state() == ZdbRowState::Undefined &&
	orders->nextUN(0) == 5;

    ZdbRowRef<Order> parked = new ZdbRow<Order>{orders, 0};
    orders->insert(6, parked, [ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::NotReady && !row && next == 5;
    });

    ZdbRowRef<Order> predecessor = new ZdbRow<Order>{orders, 0};
    orders->insert(5, ZuMv(predecessor), [ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 5;
      if (!row) return;
      new (row->ptr()) Order{
	"AMD", 5, "FIX0", "order5", 5, Side::Buy, {104}, {5}};
      *ok &= bool(row->commit());
    });
    orders->insert(6, ZuMv(parked), [ok, callbacks](
	ZdbOpResult::T result, ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Executed && row && next == 6;
      if (!row) return;
      new (row->ptr()) Order{
	"NVDA", 6, "FIX0", "order6", 6, Side::Buy, {105}, {6}};
      *ok &= bool(row->commit());
    });
    *ok &= orders->nextUN(0) == 7;
    done->post();
  });
  exactUNDone.wait();
  store->performWork();
  exactUNMissing.wait();
  store->deferWork(false);
  orders->run(0, [
    orders = orders.ptr(), ok = &exactUNOK,
    callbacks = &exactUNCallbacks, stable = &exactUNStable
  ]() {
    orders->findUpd<0>(0, ZuFwdTuple("MISSING", UINT64_C(100)), 7,
	[orders, ok, callbacks, stable](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Missing && !row && next == 7;
      *ok &= orders->invoked(0);
      stable->post();
    });
    orders->findDel<0>(0, ZuFwdTuple("MISSING", UINT64_C(101)), 7,
	[orders, ok, callbacks, stable](ZdbOpResult::T result,
	  ZdbRow<Order> *row, ZdbUN next) {
      ++*callbacks;
      *ok &= result == ZdbOpResult::Missing && !row && next == 7;
      *ok &= orders->invoked(0);
      stable->post();
    });
  });
  exactUNStable.wait();
  exactUNStable.wait();
  store->findFn({});
  ZuCheck(exactUNOK);
  ZuCheck(exactUNCallbacks == 24);
  ZuCheck(exactUNFinds == 3);

  ZtArray<ZtString<>> asyncOrder;
  ZmSemaphore lookupStarted;
  ZmSemaphore markerDone;
  ZmSemaphore lookupDone;
  store->deferWork(true);
  orders->run(0, [
    orders = orders.ptr(), asyncOrder = &asyncOrder,
    lookupStarted = &lookupStarted, markerDone = &markerDone,
    lookupDone = &lookupDone
  ]() {
    asyncOrder->push("before");
    orders->find<0>(0, ZuFwdTuple("MISSING", UINT64_C(99)), [
      asyncOrder, lookupDone
    ](ZdbRowRef<Order>) {
      asyncOrder->push("lookup");
      lookupDone->post();
    });
    asyncOrder->push("after");
    orders->run(0, [asyncOrder, markerDone]() {
      asyncOrder->push("marker");
      markerDone->post();
    });
    lookupStarted->post();
  });
  lookupStarted.wait();
  markerDone.wait();
  store->performWork();
  lookupDone.wait();
  store->deferWork(false);
  ZuCheck(asyncOrder.length() == 4);
  ZuCheck(asyncOrder[0] == "before");
  ZuCheck(asyncOrder[1] == "after");
  ZuCheck(asyncOrder[2] == "marker");
  ZuCheck(asyncOrder[3] == "lookup");

  db->telemetry(dbData);
  ZuCheck(dbData.state == Ztc::DBHostState::Active);
  ZuCheck(dbData.active);
  ZuCheck(db->stop());
  ZuCheck(db->start());
  ZuCheck(db->stop());

  orders = {};
  payments = {};
  db->final();
  ZuCheck(watch.tableDels == 2);
  ZuCheck(watch.hostDels == 1);
  ZuCheck(watch.dbDels == 1);
  ZuCheck(watch.tableDelKeyOK);
  ZuCheck(watch.hostDelKeyOK);
  ZuCheck(watch.dbDelKeyOK);
  ZuCheck(Ztc::DBMgr::all({[](Ztc::DB *) { }}) == 0);

  init();
  orders = db->initTable<Order>("o");
  orders = {};
  db->final();

  ZuCheck(watch.dbAdds == 2);
  ZuCheck(watch.dbDels == 2);
  ZuCheck(watch.hostSeeds == 2);
  ZuCheck(watch.hostAdds == 2);
  ZuCheck(watch.hostDels == 2);
  ZuCheck(watch.tableAdds == 3);
  ZuCheck(watch.tableDels == 3);
  Ztc::DBMgr::unwatch();

  {
    Zdb bad;
    auto badCf = ZdbCf{config->resolve("zdb")};
    badCf.nShards = 1;
    badCf.threads.length(0);
    bool failed = false;
    try {
      bad.init(ZuMv(badCf), &mx, {}, store);
    } catch (const ZeException &) {
      failed = true;
    }
    ZuCheck(failed);
  }

  db = {};
  store = {};
  ZuCheck(mx.stop());
}

int main()
{
  ZiLog::init("ZdbTelTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(run);
  ZiLog::stop();
}
