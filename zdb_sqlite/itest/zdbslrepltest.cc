//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/ZiLog.hh>

#include <zlib/Zdb.hh>
#include <zlib/ZdbSL.hh>

#include "ZdbTest.hh"

using namespace ZuTestUtil;

struct ReplDB : public Zdb {
  ZmSemaphore *up = nullptr;
};

static ZuPtr<const ZfCf::AnyNode> config(
    ZuCSpan connection, ZuCSpan hostID)
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{connection});
  defines->add(ZfCf::DefKey{"HOST"}, ZfCf::DefVal{hostID});
  return ZfCf::scan(
    "thread: zdb, shards: 2, hostID: ${HOST},\n"
    "hosts: {\n"
    "  0: {priority: 100, ip: 127.0.0.1, port: 19945},\n"
    "  1: {priority: 80, ip: 127.0.0.1, port: 19946}\n"
    "},\n"
    "store: {thread: store, connection: ${CONNECT}},\n"
    "tables: {order: {}},\n"
    "mx: {nThreads: 4, threads: {\n"
    "  1: {name: rx, isolated: true},\n"
    "  2: {name: tx, isolated: true},\n"
    "  3: {name: zdb, isolated: true},\n"
    "  4: {name: store, isolated: true}\n"
    "}, rxThread: rx, txThread: tx}\n", {}, ZuMv(defines)).p<1>();
}

int main()
{
  ZuTestMain();
  ZiLog::init("zdbslrepltest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  auto base = ZuCSpan{getenv("ZDB_CONNECT")};
  ZuCheck(bool(base));
  ZtString leaderPath;
  ZtString followerPath;
  leaderPath << base << ".leader";
  followerPath << base << ".follower";
  auto leaderCf = config(leaderPath, "0");
  auto followerCf = config(followerPath, "1");

  ZiMultiplex mx{ZvMxParams{"mx", leaderCf->resolve("mx")}};
  ZuCheck(mx.start());

  ZmSemaphore leaderUp;
  ZmSemaphore followerUp;
  ZmRef<ReplDB> leader = new ReplDB{};
  ZmRef<ReplDB> follower = new ReplDB{};
  leader->up = &leaderUp;
  follower->up = &followerUp;
  leader->init(ZdbCf{leaderCf}, &mx, ZdbHandler{
    .upFn = [](Zdb *db, ZdbHost *) { static_cast<ReplDB *>(db)->up->post(); }
  }, new ZdbSL::Store{});
  follower->init(ZdbCf{followerCf}, &mx, ZdbHandler{
    .upFn = [](Zdb *db, ZdbHost *) { static_cast<ReplDB *>(db)->up->post(); }
  }, new ZdbSL::Store{});
  auto leaderOrders = leader->initTable<zdbtest::Order>("order");
  auto followerOrders = follower->initTable<zdbtest::Order>("order");

  ZmSemaphore started;
  bool leaderStarted = false;
  bool followerStarted = false;
  leader->start([&](bool ok) { leaderStarted = ok; started.post(); });
  follower->start([&](bool ok) { followerStarted = ok; started.post(); });
  started.wait();
  started.wait();
  ZuCheck(leaderStarted && followerStarted);
  leaderUp.wait();

  bool committed = ZmBlock<bool>{}([orders = leaderOrders.ptr()](auto wake) {
    orders->run(0, [orders, wake = ZuMv(wake)]() mutable {
      ZdbRowRef<zdbtest::Order> row = new ZdbRow<zdbtest::Order>{orders, 0};
      orders->insert(ZuMv(row), [wake = ZuMv(wake)](
          ZdbRow<zdbtest::Order> *row) mutable {
        if (!row) { wake(false); return; }
        new (row->ptr()) zdbtest::Order{
          "REPL", 1, "SL", "sqlite", 1,
          zdbtest::Side::Buy, {100}, {7}};
        wake(bool(row->commit()));
      });
    });
  });
  ZuCheck(committed);

  ZuCheck(leader->stop());
  followerUp.wait();
  bool replicated = ZmBlock<bool>{}([orders = followerOrders.ptr()](auto wake) {
    orders->run(0, [orders, wake = ZuMv(wake)]() mutable {
      orders->find<0>(0, ZuFwdTuple("REPL", UINT64_C(1)),
        [wake = ZuMv(wake)](ZdbRowRef<zdbtest::Order> row) mutable {
          wake(row && row->data().clOrdID == "sqlite");
        });
    });
  });
  ZuCheck(replicated);
  ZuCheck(follower->stop());

  leaderOrders = {};
  followerOrders = {};
  leader->final();
  follower->final();
  leader = {};
  follower = {};
  ZuCheck(mx.stop());
  ZiLog::stop();
  return 0;
}
