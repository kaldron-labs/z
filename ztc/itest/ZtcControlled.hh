//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZtcControlled_HH
#define ZtcControlled_HH

#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcDB.hh>
#include <zlib/ZtcHub.hh>

// Controlled telemetry sources, enumerated by the production publisher.
namespace ZtcControlled {

struct Host : public Ztc::DBHost {
  ZuID db;
  TelKey telKey() const override { return {db, "host"}; }
  void telemetry(Ztc::DBHostTelemetry &data) const override {
    data.dbID = db;
    data.id = "host";
  }
};
struct Table : public Ztc::DBTable {
  ZuID db;
  TelKey telKey() const override { return {db, "table"}; }
  void telemetry(Ztc::DBTableTelemetry &data) const override {
    data.dbID = db;
    data.id = "table";
  }
};
struct DB : public Ztc::DB {
  DB(ZuCSpan id_) : id{id_} { host.db = table.db = id; }
  TelKey telKey() const override { return {id}; }
  void telemetry(Ztc::DBTelemetry &data) const override {
    data.self = id;
    data.state = Ztc::DBHostState::Active;
  }
  bool start() override { return true; }
  bool stop() override { return true; }
  unsigned allDBHosts(AllDBHostsFn fn) const override { fn(&host); return 1; }
  unsigned allDBTables(AllDBTablesFn fn) const override { fn(&table); return 1; }
  ZuID id;
  mutable Host host;
  mutable Table table;
};
struct Link : public Ztc::Link {
  TelKey telKey() const override { return {"fixture-hub", "link"}; }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = "fixture-hub";
    data.id = "link";
    data.type = Ztc::LinkType::TCP;
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
  void up() override { }
  void down() override { }
};
struct Pool : public Ztc::Pool {
  TelKey telKey() const override { return {"fixture-hub", "pool"}; }
  void telemetry(Ztc::PoolTelemetry &data) const override {
    data.hubID = "fixture-hub";
    data.id = "pool";
    data.type = Ztc::LinkType::TCP;
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn) const override { return 0; }
};
struct Hub : public Ztc::Hub {
  TelKey telKey() const override { return {Ztc::LinkType::TCP, "fixture-hub"}; }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = "fixture-hub";
    data.linkType = Ztc::LinkType::TCP;
    data.state = Ztc::HubState::Running;
  }
  bool start() override { return true; }
  bool stop() override { return true; }
  unsigned allLinks(AllLinksFn fn) const override { fn(&link); return 1; }
  unsigned allPools(AllPoolsFn fn) const override { fn(&pool); return 1; }
  mutable Link link;
  mutable Pool pool;
};
struct Cxn : public Ztc::Connection {
  TelKey telKey() const override {
    return {"fixture-mx", ZiIP{"192.0.2.1"}, 443, ZiIP{"192.0.2.2"}, 54321};
  }
  void telemetry(Ztc::CxnTelemetry &data) const override {
    data.mxID = "fixture-mx";
    data.remoteIP = "192.0.2.1";
    data.remotePort = 443;
    data.localIP = "192.0.2.2";
    data.localPort = 54321;
    data.rxBufSize = data.txBufSize = 1024;
  }
};
class Mx : public ZiMultiplex {
public:
  Mx() : ZiMultiplex{ZiMxParams{}.scheduler(
      [](auto &s) { s.id("fixture-mx"); })} { }
  unsigned allCxns(Ztc::Mx::AllCxnsFn fn) const override { fn(&m_cxn); return 1; }
private:
  mutable Cxn m_cxn;
};
struct Sources {
  Sources(bool enabled_) : enabled{enabled_} {
    if (!enabled) return;
    Ztc::DBMgr::add(&db);
    Ztc::DBMgr::add(&db2);
    Ztc::HubMgr::add(&hub);
  }
  ~Sources() {
    if (!enabled) return;
    Ztc::HubMgr::del(&hub);
    Ztc::DBMgr::del(&db2);
    Ztc::DBMgr::del(&db);
  }
  DB db{"fixture-db"};
  DB db2{"fixture-db2"};
  Hub hub;
  Mx mx;
  bool enabled;
};
} // ZtcControlled

#endif
