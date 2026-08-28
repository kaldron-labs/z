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
    "  store: {thread: store},\n"
    "  hostID: self,\n"
    "  heartbeatFreq: 101,\n"
    "  heartbeatTimeout: 102,\n"
    "  reconnectFreq: 103,\n"
    "  electionTimeout: 104,\n"
    "  hosts: {self: {standalone: true}},\n"
    "  tables: {o: {shards: 1, cacheMode: All}}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 4,\n"
    "  threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true}\n"
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
  ZiMultiplex mx{ZvMxParams{"mx", config->resolve("mx")}};
  ZuCheck(mx.start());

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
    {&watch, ZmFnPtr<&Watch::addHost>{}},
    {&watch, ZmFnPtr<&Watch::delHost>{}},
    {&watch, ZmFnPtr<&Watch::addTable>{}},
    {&watch, ZmFnPtr<&Watch::delTable>{}});
  Ztc::DBMgr::watch(
    {&watch, ZmFnPtr<&Watch::addDB>{}},
    {&watch, ZmFnPtr<&Watch::delDB>{}});

  ZmRef<zdbtest::Store> store = new zdbtest::Store{};
  ZmRef<Zdb> db = new Zdb{};
  ZmSemaphore active;
  active_ = &active;

  auto init = [&]() {
    db->init(ZdbCf{config->resolve("zdb")}, &mx, ZdbHandler{
      .upFn = up
    }, store);
  };

  init();
  ZuCheck(watch.dbAdds == 1);
  ZuCheck(watch.dbDataOK);
  ZuCheck(watch.hostSeeds == 1);
  ZuCheck(watch.hostAdds == 1);
  ZuCheck(watch.hostDataOK);
  ZuCheck(watch.tableSeeds == 0);
  bool dbFound = false;
  ZuCheck(Ztc::DBMgr::all(
    {[&](Ztc::DB *db_) { dbFound = db_ == db.ptr(); }}) == 1);
  ZuCheck(dbFound);

  auto orders = db->initTable<Order>("o");
  ZuCheck(watch.tableAdds == 1);
  ZuCheck(watch.tableDataOK);
  auto tableKey = orders->telKey();
  ZuCheck(tableKey.p<0>() == "self");
  ZuCheck(tableKey.p<1>() == "o");
  Ztc::DBTableTelemetry tableData;
  orders->telemetry(tableData);
  ZuCheck(tableData.id == "o");
  ZuCheck(tableData.nShards == 1);
  ZuCheck(tableData.cacheMode == Ztc::DBCacheMode::All);
  ZuCheck(!tableData.count);
  ZuCheck(!tableData.cacheLoads);
  ZuCheck(!tableData.cacheMisses);
  ZuCheck(!tableData.cacheEvictions);

  Ztc::DBTelemetry dbData;
  db->telemetry(dbData);
  ZuCheck(dbData.thread == "zdb");
  ZuCheck(dbData.self == "self");
  ZuCheck(dbData.nTables == 1);
  ZuCheck(dbData.nHosts == 1);
  ZuCheck(dbData.heartbeatFreq == 101);
  ZuCheck(dbData.heartbeatTimeout == 102);
  ZuCheck(dbData.reconnectFreq == 103);
  ZuCheck(dbData.electionTimeout == 104);

  ZuCheck(db->start());
  active.wait();
  db->telemetry(dbData);
  ZuCheck(dbData.state == Ztc::DBHostState::Active);
  ZuCheck(dbData.active);
  ZuCheck(db->stop());
  ZuCheck(db->start());
  ZuCheck(db->stop());

  orders = {};
  db->final();
  ZuCheck(watch.tableDels == 1);
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
  ZuCheck(watch.tableAdds == 2);
  ZuCheck(watch.tableDels == 2);
  Ztc::DBMgr::unwatch();

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
