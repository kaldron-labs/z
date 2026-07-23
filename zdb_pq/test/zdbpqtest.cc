//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/Zdb.hh>

#include "zdbtest.hh"

using namespace zdbtest;

// command line options

struct Options {
  ZuCSpan	module;
  ZuCSpan	connect;
  bool		debug = false;
  bool		hashTel = false;
  bool		heapTel = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((module),  (CLI::Opt<'m'>)),  (String, getenv("ZDB_MODULE"))),
  (((connect), (CLI::Opt<'c'>)),  (String, getenv("ZDB_CONNECT"))),
  (((debug),   (CLI::Flag<'d'>)),                        (Bool)),
  (((hashTel), (CLI::Flag<'t'>, CLI::Long<"hash-tel">)), (Bool)),
  (((heapTel), (CLI::Flag<'T'>, CLI::Long<"heap-tel">)), (Bool)),
  (((help),    (CLI::Flag<'h'>)),                        (Bool)));

void usage()
{
  static const char *help =
    "Usage: zdbpqtest [OPTION]...\n\n"
    "Options:\n"
    "      --help\t\tthis help\n"
    "  -m, --module=MODULE\tspecify data store module (default: $ZDB_MODULE)\n"
    "  -c, --connect=CONNECT\t"
      "specify data store connection (default: $ZDB_CONNECT)\n"
    "  -d, --debug\t\tenable Zdb debug logging\n"
    "  -t, --hash-tel\toutput hash table telemetry CSV at exit\n"
    "  -T, --heap-tel\toutput heap telemetry CSV at exit\n"
    ;

  std::cerr << help << std::flush;
  Zm::exit(1);
}

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

ZuPtr<const ZfCf::AnyNode> inlineCf(
    ZuCSpan s, ZmRef<ZfCf::Defines> defines = new ZfCf::Defines())
{
  auto scan = ZfCf::scan(s, {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

void gtfo()
{
  if (mx) mx->stop();
  ZiLog::stop();
  Zm::exit(1);
}

int main(int argc_, char **argv)
{
  Options options;
  int argc;
  try {
    argc = ZfCLI::load(options, argc_, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  }
  if (argc != 1) usage();
  if (options.help) usage();
  if (!options.module) {
    std::cerr << "set ZDB_MODULE or use --module=MODULE\n" << std::flush;
    Zm::exit(1);
  }
  if (!options.connect) {
    std::cerr << "set ZDB_CONNECT or use --connect=CONNECT\n" << std::flush;
    Zm::exit(1);
  }

  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines();
  defines->add(ZfCf::DefKey{"MODULE"}, ZfCf::DefVal{options.module});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{options.connect});
  defines->add(
    ZfCf::DefKey{"DEBUG"}, ZfCf::DefVal{options.debug ? "true" : "false"});
  auto cf = inlineCf(
    "thread: zdb,\n"
    "hostID: 0,\n"
    "hosts: {0: {standalone: true}},\n"
    "store: {\n"
    "  thread: zdb_pq,\n"
    "  replicated: true,\n"
    "  module: ${MODULE},\n"
    "  connection: ${CONNECT}\n"
    "},\n"
    "tables: {order: {warmup: true}},\n"
    "debug: ${DEBUG},\n"
    "mx: {\n"
    "  nThreads: 4,\n"
    "  threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: zdb_pq, isolated: true}\n"
    "  },\n"
    "  rxThread: rx,\n"
    "  txThread: tx\n"
    "}\n", defines);

  ZiLog::init("zdbpqtest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdbpqtest", "multiplexer start failed");

    db = new Zdb();

    db->init(ZdbCf(cf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdbpqtest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
      },
      .downFn = [](Zdb *, bool) { ZiLOG(Info, "zdbpqtest", "INACTIVE"); }
    });

    orders = db->initTable<Order>("order"); // might throw

    if (!db->start()) throw ZeEXCEPT(Fatal, "zdbpqtest", "Zdb start failed");

    if (options.hashTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << ZmHashMgr::csv()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << ZmHeapMgr::csv()));

    ZuNBox<uint64_t> seqNo;

    orders->selectKeys<2>(
      ZuFwdTuple("FIX0"), 1, [&seqNo](auto max, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (max.template is<Key>()) {
	  seqNo = max.template p<Key>().template p<1>();
	  ZiLOG(Info, "zdbpqtest", ([max = ZuMv(max)](auto &s) {
	    s << "maximum(FIX0): " << max.template p<Key>();
	  }));
	} else {
	  ZiLOG(Info, "zdbpqtest", ([max = ZuMv(max)](auto &s) {
	    s << "maximum(FIX0): EOR";
	  }));
	  done.post();
	}
      });
    done.wait();

    ZuNBox<uint64_t> id;

    if (*seqNo) {
      orders->run(0, [seqNo, &id]{
	orders->find<2>(0, ZuFwdTuple("FIX0", seqNo),
	  [seqNo, &id](ZmRef<ZdbObject<Order>> o) {
	    if (!o) {
	      id = {};
	      ZiLOG(Info, "zdbpqtest", ([seqNo](auto &s) {
		s << "find(FIX0, " << seqNo << "): (null)";
	      }));
	    } else {
	      id = o->data().orderID;
	      ZiLOG(Info, "zdbpqtest", ([seqNo, o = ZuMv(o)](auto &s) {
		s << "find(FIX0, " << seqNo
		  << "): refCount=" << o->refCount()
		  << ' ' << *o;
	      }));
	    }
	    done.post();
	  });
      });
      done.wait();

      ++seqNo;
    } else
      seqNo = 0;

    if (*id)
      ++id;
    else
      id = 0;

    orders->run(0, [&id, &seqNo]{
      ZdbObjRef<Order> o = new ZdbObject<Order>{orders, 0};
      orders->insert(o, [&id, &seqNo](ZdbObject<Order> *o) {
	if (ZuUnlikely(!o)) { done.post(); return; }
	ZuCArray<32> clOrdID;
	clOrdID << "order" << id;
	new (o->ptr())
	  Order{"IBM", id, "FIX0", clOrdID, seqNo, Side::Buy, {100}, {100}};
	o->data().flags.set(42);
	o->commit();
	id = o->data().orderID;
	seqNo = o->data().seqNo;
	ZiLOG(Info, "zdbpqtest", ([id, seqNo](auto &s) {
	  s << "orderID=" << id << " seqNo=" << seqNo;
	}));
	done.post();
      });
    });
    done.wait();

    orders->run(0, [&id]{
      orders->find<0>(0, ZuFwdTuple("IBM", id),
	[&id](ZmRef<ZdbObject<Order>> o) {
	  if (!o)
	    ZiLOG(Info, "zdbpqtest", ([id](auto &s) {
	      s << "find(IBM, " << id << "): (null)";
	    }));
	  else
	    ZiLOG(Info, "zdbpqtest", ([id, o = ZuMv(o)](auto &s) {
	      s << "find(IBM, " << id << "): " << *o;
	    }));
	  done.post();
	});
    });
    done.wait();

    orders->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
      using Key = ZuStructKeyT<Order, 2>;
      if (max.template is<Key>()) {
	ZiLOG(Info, "zdbpqtest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): " << max.template p<Key>();
	}));
      } else {
	ZiLOG(Info, "zdbpqtest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): EOR";
	}));
	done.post();
      }
    });
    done.wait();

    if (id > 0) {
      orders->run(0, [id = id - 1]{
	orders->findUpd<0, ZuSeq<1>>(0, ZuFwdTuple("IBM", id),
	  [id](ZmRef<ZdbObject<Order>> o) {
	    if (!o) {
	      ZiLOG(Info, "zdbpqtest", ([id](auto &s) {
		s << "findUpd(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZiLOG(Info, "zdbpqtest", ([id, data = o->data()](auto &s) {
	      s << "findUpd(IBM, " << id << "): " << data;
	    }));
	    ZuCArray<32> clOrdID;
	    clOrdID << "order" << id << "_1";
	    o->data().prices[0] = o->data().prices[0] + 42;
	    o->data().clOrdID = clOrdID;
	    o->commit();
	  });
	done.post();
      });
      done.wait();
    }

    if (id > 3) {
      orders->run(0, [id = id - 3]{
	orders->findDel<0>(0, ZuFwdTuple("IBM", id),
	  [id](ZmRef<ZdbObject<Order>> o) {
	    if (!o) {
	      ZiLOG(Info, "zdbpqtest", ([id](auto &s) {
		s << "findDel(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZiLOG(Info, "zdbpqtest", ([id, o](auto &s) {
	      s << "findDel(IBM, " << id << "): " << *o;
	    }));
	    o->commit();
	    done.post();
	  });
      });
      done.wait();
    }

    if (options.hashTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << ZmHashMgr::csv()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << ZmHeapMgr::csv()));

    db->stop(); // closes all tables

    mx->stop();

    orders = {};
    db->final(); // calls Store::final()
    db = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdbpqtest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdbpqtest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  return 0;
}
