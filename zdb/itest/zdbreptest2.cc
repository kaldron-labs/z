//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <limits.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/Zdb.hh>

#include "ZdbMockStore.hh"
#include "ZdbTest.hh"

using namespace zdbtest;
using namespace ZuTestUtil;

// mock data stores
ZmRef<zdbtest::Store> store[2];

// databases
ZmRef<Zdb> db[2];

// tables
ZmRef<ZdbTable<Order>> orders[2];

// multiplexer
ZuPtr<ZiMultiplex> mx;

ZmSemaphore done;

void sigint()
{
  std::cerr << "SIGINT\n" << std::flush;
  done.post();
}

ZuPtr<const ZfCf::AnyNode> inlineCf(ZuCSpan s)
{
  auto scan = ZfCf::scan(s);
  return ZuMv(scan.p<1>());
}

void gtfo()
{
  if (mx) mx->stop();
  ZiLog::stop();
  Zm::exit(1);
}

static void run()
{
  ZuTestScope(run);
  ZuPtr<const ZfCf::AnyNode> cf;

  try {
    cf = inlineCf(
      "thread: zdb,\n"
      "store: {thread: zdb_mem},\n"
      "hostID: 0,\n"
      "hosts: {\n"
      "  0: {priority: 100, ip: 127.0.0.1, port: 9943},\n"
      "  1: {priority: 80, ip: 127.0.0.1, port: 9944}\n"
      "},\n"
      "tables: {order: {}},\n"
      "debug: true,\n"
      "mx: {\n"
      "  nThreads: 4,\n"
      "  threads: {\n"
      "    1: {name: rx, isolated: true},\n"
      "    2: {name: tx, isolated: true},\n"
      "    3: {name: zdb, isolated: true},\n"
      "    4: {name: zdb_mem, isolated: true}\n"
      "  },\n"
      "  rxThread: rx,\n"
      "  txThread: tx\n"
      "}\n"
    );

  } catch (const ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (const ZeError &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (...) {
    Zm::exit(1);
  }

  ZiLog::init("zdbreptest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdbreptest2", "multiplexer start failed");

    ZmAtomic<unsigned> ok = 0;

    for (unsigned i = 0; i < 2; i++) {
      store[i] = new zdbtest::Store();
      db[i] = new Zdb();

      ZdbCf dbCf{cf};

      dbCf.hostID = (ZuCArray<16>{} << i);

      db[i]->init(ZuMv(dbCf), mx, ZdbHandler{
	.upFn = [](Zdb *db_, ZdbHost *host) {
	  ZiLOG(Info, "zdbreptest2", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	    s << "ACTIVE (was " << id << ')';
	  }));
	  if (db_ == db[1]) done.post();
	},
	.downFn = [](Zdb *, bool) { ZiLOG(Info, "zdbreptest2", "INACTIVE"); }
      }, store[i]);

      orders[i] = db[i]->initTable<Order>("order"); // might throw

      db[i]->start([&ok](bool ok_) { if (ok_) ++ok; done.post(); });
    }

    for (unsigned i = 0; i < 2; i++) done.wait();

    if (ok >= 2) {
      ZmAssert(db[0]->active());
      ZmAssert(!db[1]->active());

      uint64_t id;

      orders[0]->writeCache(true); // change to false to cause find() to fail

      store[0]->deferWork(true);
      store[0]->deferCallbacks(true);

      orders[0]->run(0, [&id]{
	ZdbRowRef<Order> o = new ZdbRow<Order>{orders[0], 0};
	orders[0]->insert(o, [](ZdbRow<Order> *o) {
	  if (ZuUnlikely(!o)) return;
	  new (o->ptr())
	    Order{"IBM", 0, "FIX0", "order0", 0, Side::Buy, {100}, {100}};
	  o->commit();
	});
	o = new ZdbRow<Order>{orders[0], 0};
	orders[0]->insert(o, [&id](ZdbRow<Order> *o) {
	  if (ZuUnlikely(!o)) return;
	  new (o->ptr())
	    Order{"IBM", 1, "FIX0", "order1", 2, Side::Buy, {100}, {100}};
	  id = o->data().orderID;
	  ZiLOG(Info, "zdbreptest2", ([id](auto &s) { s << "orderID=" << id; }));
	  o->commit();
	});
	o = new ZdbRow<Order>{orders[0], 0};
	orders[0]->insert(o, [](ZdbRow<Order> *o) {
	  if (ZuUnlikely(!o)) return;
	  new (o->ptr())
	    Order{"IBM", 2, "FIX0", "order2", 4, Side::Buy, {100}, {100}};
	  o->commit();
	  done.post(); // #1
	});
      });
      ZmBlock<>{}([](auto wake) {
	orders[0]->run(0, [wake = ZuMv(wake)]() mutable { wake(); });
      });

      orders[0]->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (max.template is<Key>())
	  ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	    s << "#1 maximum(FIX0): " << max.template p<Key>();
	  }));
	else {
	  ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	    s << "#1 maximum(FIX0): EOR";
	  }));
	  done.post(); // #2
	}
      });

      store[0]->performWork();
      store[0]->performCallbacks();

      done.wait(); // #1
      done.wait(); // #2

      orders[0]->run(0, [&id]{
	static ZmSemaphore done_;
	orders[0]->find<0>(0, ZuFwdTuple("IBM", id),
	  [&id](ZmRef<ZdbRow<Order>> o) {
	    if (!o)
	      ZiLOG(Info, "zdbreptest2", ([id](auto &s) {
		s << "find(IBM, " << id << "): (null)";
	      }));
	    else
	      ZiLOG(Info, "zdbreptest2", ([id, o = ZuMv(o)](auto &s) {
		s << "find(IBM, " << id << "): " << o->data();
	      }));
	    done.post(); // #3
	  });
      });
      ZmBlock<>{}([](auto wake) {
	orders[0]->run(0, [wake = ZuMv(wake)]() mutable { wake(); });
      });

      orders[0]->selectKeys<2>(ZuFwdTuple("FIX0"), 1,
	[](auto max, unsigned) {
	  using Key = ZuStructKeyT<Order, 2>;
	  if (max.template is<Key>())
	    ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	      s << "#2 maximum(FIX0): " << max.template p<Key>();
	    }));
	  else {
	    ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	      s << "#2 maximum(FIX0): EOR";
	    }));
	    done.post(); // #4
	  }
	});

      store[0]->performWork();
      store[0]->deferWork(false);
      store[0]->performCallbacks();
      store[0]->deferCallbacks(false);

      done.wait(); // #3
      done.wait(); // #4

      ZiLOG(Debug, "zdbreptest2", "ENV 0 STOPPING");

      db[0]->stop();

      ZiLOG(Debug, "zdbreptest2", "ENV 0 STOPPED");

      done.wait(); // wait for db[1] to become active

      orders[1]->run(0, [&id]{
	static ZmSemaphore done_;
	orders[1]->find<0>(0, ZuFwdTuple("IBM", id),
	  [&id](ZmRef<ZdbRow<Order>> o) {
	    if (!o)
	      ZiLOG(Info, "zdbreptest2", ([id](auto &s) {
		s << "find(IBM, " << id << "): (null)";
	      }));
	    else
	      ZiLOG(Info, "zdbreptest2", ([id, o = ZuMv(o)](auto &s) {
		s << "find(IBM, " << id << "): " << o->data();
	      }));
	    done.post(); // #5
	  });
      });
      done.wait(); // #5

      orders[1]->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (max.template is<Key>())
	  ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	    s << "#3 maximum(FIX0): " << max.template p<Key>();
	  }));
	else {
	  ZiLOG(Info, "zdbreptest2", ([max = ZuMv(max)](auto &s) {
	    s << "#3 maximum(FIX0): EOR";
	  }));
	  done.post(); // #6
	}
      });
      done.wait(); // #6
    }

    for (unsigned i = 0; i < 2; i++)
      db[i]->stop();

    mx->stop();

    // ZiLOG(Debug, "zdbreptest2", (ZeString{} << '\n' << Ztc::hashCSV()));

    for (unsigned i = 0; i < 2; i++) {
      orders[i] = {};
      db[i]->final(); // calls Store::final()
      db[i] = {};
      store[i] = {};
    }

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdbreptest2", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdbreptest2", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  ZuCHECK(true, "replication restart lifecycle");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(run);
  return 0;
}
