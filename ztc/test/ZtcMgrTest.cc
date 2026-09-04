//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZtcDB.hh>
#include <zlib/ZtcHub.hh>
#include <zlib/ZtcMx.hh>

using namespace ZuTestUtil;

ZuAssert((ZuIsSame<
  typename Ztc::DBMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::DB::AllDBHostsFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::DB::AllDBTablesFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::HubMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::Hub::AllLinksFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::Hub::AllPoolsFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::DBMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::DBMgr::AddHostFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::HubMgr::AddQueueFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::MxMgr::AllFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::Mx::AllCxnsFn::HeapID,
  typename Ztc::AllFnHeapID::HeapID>{}));
ZuAssert((ZuIsSame<
  typename Ztc::MxMgr::AddFn::HeapID,
  typename Ztc::WatchFnHeapID::HeapID>{}));

struct MockDBHost final : public Ztc::DBHost {
  Ztc::DBHostKey telKey() const override { return {dbID, id}; }
  void telemetry(Ztc::DBHostTelemetry &data) const override {
    data.dbID = dbID;
    data.id = id;
  }

  ZuID	dbID{"db"};
  ZuID	id{"host"};
};

struct MockDBTable final : public Ztc::DBTable {
  Ztc::DBTableKey telKey() const override { return {dbID, id}; }
  void telemetry(Ztc::DBTableTelemetry &data) const override {
    data.dbID = dbID;
    data.id = id;
  }

  ZuID			dbID{"db"};
  Ztc::DBTableID	id{"table_identifier_longer_than_twenty_eight_bytes"};
};

struct MockDB final : public Ztc::DB {
  Ztc::DBKey telKey() const override { return id; }
  void telemetry(Ztc::DBTelemetry &data) const override { data.self = id; }
  bool start() override { return true; }
  bool stop() override { return true; }
  unsigned allDBHosts(AllDBHostsFn fn) const override {
    fn(&host);
    return 1;
  }
  unsigned allDBTables(AllDBTablesFn fn) const override {
    fn(&table);
    return 1;
  }
  void hostAdd() { hostAdded_(&host); }
  void hostDel() { hostDeleted_(&host); }
  void tableAdd() { tableAdded_(&table); }
  void tableDel() { tableDeleted_(&table); }

  ZuID		id{"db"};
  mutable MockDBHost	host;
  mutable MockDBTable	table;
};

struct MockQueue final : public Ztc::Queue {
  MockQueue(ZuCSpan ownerID_, ZuCSpan id_, Ztc::QueueType::T type_) :
    ownerID{ownerID_}, id{id_}, type{type_} { }

  ZuTuple<const ZuID &, const ZuID &, Ztc::QueueType::T>
    telKey() const override {
    return {ownerID, id, type};
  }
  void telemetry(Ztc::QueueTelemetry &data) const override {
    data.ownerID = ownerID;
    data.id = id;
    data.type = type;
  }

  ZuID			ownerID;
  ZuID			id;
  Ztc::QueueType::T	type;
};

struct MockLink final : public Ztc::Link {
  MockLink() :
    rx{"hub", "link", Ztc::QueueType::Rx},
    tx{"hub", "link", Ztc::QueueType::Tx} { }

  ZuTuple<const ZuID &, const ZuID &> telKey() const override {
    return {hubID, id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = "hub";
    data.id = "link";
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    fn(&rx);
    fn(&tx);
    return 2;
  }
  void up() override { }
  void down() override { }

  ZuID			hubID{"hub"};
  ZuID			id{"link"};
  mutable MockQueue	rx;
  mutable MockQueue	tx;
};

struct MockPool final : public Ztc::Pool {
  ZuTuple<const ZuID &, const ZuID &> telKey() const override {
    return {hubID, id};
  }
  void telemetry(Ztc::PoolTelemetry &data) const override {
    data.hubID = "hub";
    data.id = "pool";
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    MockQueue tx{hubID, id, Ztc::QueueType::Tx};
    fn(&tx);
    return 1;
  }

  ZuID			hubID{"hub"};
  ZuID			id{"pool"};
};

struct MockHub final : public Ztc::Hub {
  ZuTuple<Ztc::LinkType::T, const ZuID &> telKey() const override {
    return {Ztc::LinkType::TCP, id};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = id;
    data.linkType = Ztc::LinkType::TCP;
  }
  bool start() override { return true; }
  bool stop() override { return true; }
  unsigned allLinks(AllLinksFn fn) const override {
    fn(&link);
    return 1;
  }
  unsigned allPools(AllPoolsFn fn) const override {
    fn(&pool);
    return 1;
  }
  void linkAdd() { linkAdded_(&link); }
  void linkDel() { linkDeleted_(&link); }
  void poolAdd() { poolAdded_(&pool); }
  void poolDel() { poolDeleted_(&pool); }

  ZuID			id{"hub"};
  mutable MockLink	link;
  mutable MockPool	pool;
};

struct WatchState {
  enum {
    DBAdd = 1, DBDel,
    HubAdd, HubDel,
    LinkAdd, RxAdd, TxAdd, TxDel, RxDel, LinkDel,
    PoolAdd, PoolTxAdd, PoolTxDel, PoolDel
  };

  void push(unsigned event) { events.push(event); }

  ZtArray<unsigned>	events;
};

void managerGuards()
{
  ZuTestScope(managerGuards);
  unsigned guarded = 0;
  Ztc::DBMgr::guard([&guarded]() { ++guarded; });
  Ztc::HubMgr::guard([&guarded]() { ++guarded; });
  Ztc::MxMgr::guard([&guarded]() { ++guarded; });
  ZuCheck(guarded == 3);
}

void dbRoot()
{
  ZuTestScope(dbRoot);
  WatchState state;
  MockDB db;
  Ztc::DBMgr::watch(
    {[&state](Ztc::DB *db_) {
      Ztc::DBTelemetry data;
      db_->telemetry(data);
      ZuCheck(data.self == "db");
      state.push(WatchState::DBAdd);
    }},
    {[&state](Ztc::DB *db_) {
      Ztc::DBTelemetry data;
      db_->telemetry(data);
      ZuCheck(data.self == "db");
      state.push(WatchState::DBDel);
    }},
    {}, {}, {}, {});
  Ztc::DBMgr::add(&db);
  unsigned seen = 0;
  unsigned count = Ztc::DBMgr::all({[&db, &seen](Ztc::DB *db_) {
    ZuCheck(db_ == &db);
    ++seen;
  }});
  ZuCheck(count == 1);
  ZuCheck(seen == 1);
  Ztc::DBMgr::capture([](const auto &captures) {
    unsigned guarded = 0;
    Ztc::DBMgr::guard([&guarded]() { ++guarded; });
    ZuCheck(guarded == 1);
    ZuCheck(captures.length() == 1);
    ZuCheck(captures[0].self == "db");
  });
  Ztc::DBMgr::del(&db);
  Ztc::DBMgr::unwatch();
  ZuCheck(state.events.length() == 2);
  ZuCheck(state.events[0] == WatchState::DBAdd);
  ZuCheck(state.events[1] == WatchState::DBDel);
}

void hubChildren()
{
  ZuTestScope(hubChildren);
  WatchState state;
  MockHub hub;
  auto &link = hub.link;
  Ztc::HubMgr::watch(
    {[&state](Ztc::Hub *) { state.push(WatchState::HubAdd); }},
    {[&state](Ztc::Hub *) { state.push(WatchState::HubDel); }},
    {[&state](Ztc::Link *) { state.push(WatchState::LinkAdd); }},
    {[&state](Ztc::Link *) { state.push(WatchState::LinkDel); }},
    {[&state](Ztc::Pool *) { state.push(WatchState::PoolAdd); }},
    {[&state](Ztc::Pool *) { state.push(WatchState::PoolDel); }},
    {[&state, &link](Ztc::Queue *queue) {
      auto key = queue->telKey();
      if (key.p<1>() == link.rx.id &&
	  key.p<2>() == Ztc::QueueType::Rx)
	state.push(WatchState::RxAdd);
      else if (key.p<1>() == link.tx.id)
	state.push(WatchState::TxAdd);
      else
	state.push(WatchState::PoolTxAdd);
    }},
    {[&state, &link](Ztc::Queue *queue) {
      auto key = queue->telKey();
      if (key.p<1>() == link.rx.id &&
	  key.p<2>() == Ztc::QueueType::Rx)
	state.push(WatchState::RxDel);
      else if (key.p<1>() == link.tx.id)
	state.push(WatchState::TxDel);
      else
	state.push(WatchState::PoolTxDel);
    }});
  Ztc::HubMgr::add(&hub);
  Ztc::HubMgr::capture([](const auto &captures) {
    unsigned guarded = 0;
    Ztc::HubMgr::guard([&guarded]() { ++guarded; });
    ZuCheck(guarded == 1);
    ZuCheck(captures.length() == 1);
    ZuCheck(captures[0].id == "hub");
  });
  hub.linkAdd();
  hub.linkDel();
  hub.poolAdd();
  hub.poolDel();
  Ztc::HubMgr::del(&hub);
  Ztc::HubMgr::unwatch();

  ZuCheck(state.events.length() == 12);
  ZuCheck(state.events[0] == WatchState::HubAdd);
  ZuCheck(state.events[1] == WatchState::LinkAdd);
  ZuCheck(state.events[2] == WatchState::RxAdd);
  ZuCheck(state.events[3] == WatchState::TxAdd);
  ZuCheck(state.events[4] == WatchState::RxDel);
  ZuCheck(state.events[5] == WatchState::TxDel);
  ZuCheck(state.events[6] == WatchState::LinkDel);
  ZuCheck(state.events[7] == WatchState::PoolAdd);
  ZuCheck(state.events[8] == WatchState::PoolTxAdd);
  ZuCheck(state.events[9] == WatchState::PoolTxDel);
  ZuCheck(state.events[10] == WatchState::PoolDel);
  ZuCheck(state.events[11] == WatchState::HubDel);
}

void dbChildren()
{
  ZuTestScope(dbChildren);
  MockDB db;
  unsigned hostAdds = 0, hostDels = 0;
  unsigned tableAdds = 0, tableDels = 0;
  Ztc::DBMgr::watch(
    {},
    {},
    {[&hostAdds](Ztc::DBHost *host) {
      ZuCheck(host->telKey().p<1>() == "host");
      ++hostAdds;
    }},
    {[&hostDels](Ztc::DBHost *host) {
      ZuCheck(host->telKey().p<1>() == "host");
      ++hostDels;
    }},
    {[&tableAdds](Ztc::DBTable *table) {
      ZuCheck(
	table->telKey().p<1>() ==
	"table_identifier_longer_than_twenty_eight_bytes");
      ++tableAdds;
    }},
    {[&tableDels](Ztc::DBTable *table) {
      ZuCheck(
	table->telKey().p<1>() ==
	"table_identifier_longer_than_twenty_eight_bytes");
      ++tableDels;
    }});
  ZuCheck(db.allDBHosts({[&hostAdds](Ztc::DBHost *) {
    ++hostAdds;
  }}) == 1);
  ZuCheck(db.allDBTables({[&tableAdds](Ztc::DBTable *) {
    ++tableAdds;
  }}) == 1);
  db.hostAdd();
  db.tableAdd();
  db.tableDel();
  db.hostDel();
  Ztc::DBMgr::unwatch();
  ZuCheck(hostAdds == 2);
  ZuCheck(hostDels == 1);
  ZuCheck(tableAdds == 2);
  ZuCheck(tableDels == 1);
}

void deleteBlocksOnConsumer()
{
  ZuTestScope(deleteBlocksOnConsumer);
  MockDB db;
  ZmPLock indexLock;
  ZmSemaphore entered;
  ZmSemaphore removed;
  Ztc::DBMgr::watch(
    {},
    {[&db, &entered, &indexLock, &removed](Ztc::DB *db_) {
      ZuCheck(db_ == &db);
      entered.post();
      ZmGuard<ZmPLock> guard(indexLock);
      removed.post();
    }},
    {}, {}, {}, {});
  Ztc::DBMgr::add(&db);
  indexLock.lock();
  ZmThread deleter{[&db]() { Ztc::DBMgr::del(&db); }};
  entered.wait();
  ZuCheck(removed.trywait() != 0);
  indexLock.unlock();
  removed.wait();
  deleter.join();
  Ztc::DBMgr::unwatch();
}

int main()
{
  ZuTestMain();
  ZuTestCall(managerGuards);
  ZuTestCall(dbRoot);
  ZuTestCall(dbChildren);
  ZuTestCall(hubChildren);
  ZuTestCall(deleteBlocksOnConsumer);
}
