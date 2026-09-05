//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

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

// mock data store
ZmRef<zdbtest::Store> store;

// database
ZmRef<Zdb> db;

// table
ZmRef<ZdbTable<Order>> orders;

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
      "zdb: {\n"
      "  thread: zdb,\n"
      "  store: {thread: zdb_mem},\n"
      "  hostID: 0,\n"
      "  hosts: {0: {standalone: true}},\n"
      "  tables: {order: {}},\n"
      "  debug: true\n"
      "},\n"
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

  ZiLog::init("zdbsmoketest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdbsmoketest", "multiplexer start failed");

    store = new zdbtest::Store();
    db = new Zdb();

    db->init(ZdbCf{cf->resolve("zdb")}, mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdbsmoketest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
	done.post();
      },
      .downFn = [](Zdb *, bool) {
	ZiLOG(Info, "zdbsmoketest", "INACTIVE");
      }
    }, store);

    orders = db->initTable<Order>("order"); // might throw

    db->start();
    done.wait(); // ensure active

    uint64_t id;

    // orders->writeCache(false);

    orders->run(0, [&id]{
      ZdbRowRef<Order> o = new ZdbRow<Order>{orders, 0};
      orders->insert(o, [&id](ZdbRow<Order> *o) {
	if (ZuUnlikely(!o)) return;
	new (o->ptr())
	  Order{"IBM", 0, "FIX0", "order0", 0, Side::Buy, {100}, {100}};
	o->data().flags = ZfField(Order, flags)::deflt();
	o->commit();
	id = o->data().orderID;
	ZiLOG(Info, "zdbsmoketest", ([id](auto &s) { s << "orderID=" << id; }));
      });
      o = new ZdbRow<Order>{orders, 0};
      orders->insert(o, [](ZdbRow<Order> *o) {
	if (ZuUnlikely(!o)) return;
	new (o->ptr())
	  Order{"IBM", 1, "FIX0", "order1", 2, Side::Buy, {100}, {100}};
	o->commit();
      });
      o = new ZdbRow<Order>{orders, 0};
      orders->insert(o, [](ZdbRow<Order> *o) {
	if (ZuUnlikely(!o)) { done.post(); return; }
	new (o->ptr())
	  Order{"IBM", 2, "FIX0", "order2", 4, Side::Buy, {100}, {100}};
	o->commit();
	done.post();
      });
    });
    done.wait();

    orders->run(0, [&id]{
      static ZmSemaphore done_;
      orders->find<0>(0, ZuFwdTuple("IBM", id),
	[&id](ZmRef<ZdbRow<Order>> o) {
	  if (!o)
	    ZiLOG(Info, "zdbsmoketest", ([id](auto &s) {
	      s << "find(IBM, " << id << "): (null)";
	    }));
	  else
	    ZiLOG(Info, "zdbsmoketest", ([id, o = ZuMv(o)](auto &s) {
	      s << "find(IBM, " << id << "): " << o->data();
	    }));
	  done.post();
	});
    });
    done.wait();

    orders->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
      using Key = ZuStructKeyT<Order, 2>;
      if (max.template is<Key>())
	ZiLOG(Info, "zdbsmoketest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): " << max.template p<Key>();
	}));
      else {
	ZiLOG(Info, "zdbsmoketest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): EOR";
	}));
	done.post();
      }
    });
    done.wait();

    db->stop(); // closes all tables

    store->preserve();

    orders = {};
    db->final();

    db->init(ZdbCf{cf->resolve("zdb")}, mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdbsmoketest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
      },
      .downFn = [](Zdb *, bool) { ZiLOG(Info, "zdbsmoketest", "INACTIVE"); }
    }, store);

    orders = db->initTable<Order>("order"); // might throw

    db->start();

    ZiLOG(Info, "zdbsmoketest", ([count = orders->count()](auto &s) {
      s << "orders count=" << count;
    }));

    orders->run(0, [&id]{
      static ZmSemaphore done_;
      orders->find<0>(0, ZuFwdTuple("IBM", id),
	[&id](ZmRef<ZdbRow<Order>> o) {
	  if (!o)
	    ZiLOG(Info, "zdbsmoketest", ([id](auto &s) {
	      s << "find(IBM, " << id << "): (null)";
	    }));
	  else
	    ZiLOG(Info, "zdbsmoketest", ([id, o = ZuMv(o)](auto &s) {
	      s << "find(IBM, " << id << "): " << o->data();
	    }));
	  done.post();
	});
    });
    done.wait();

    orders->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
      using Key = ZuStructKeyT<Order, 2>;
      if (max.template is<Key>())
	ZiLOG(Info, "zdbsmoketest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): " << max.template p<Key>();
	}));
      else {
	ZiLOG(Info, "zdbsmoketest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): EOR";
	}));
	done.post();
      }
    });
    done.wait();

    db->stop();

    mx->stop();

    ZiLOG(Debug, "zdbsmoketest", (ZeString{} << '\n' << Ztc::hashCSV()));

    orders = {};
    db->final(); // calls Store::final()
    db = {};
    store = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdbsmoketest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdbsmoketest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  ZuCHECK(true, "in-memory database lifecycle");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(run);
  return 0;
}
