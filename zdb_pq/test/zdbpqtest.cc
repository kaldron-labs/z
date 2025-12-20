//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtCLI.hh>

#include <zlib/ZeLog.hh>

#include <zlib/ZvCf.hh>
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

ZtStruct((Options, CLI),
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

ZmRef<ZvCf> inlineCf(ZuCSpan s)
{
  ZmRef<ZvCf> cf = new ZvCf{};
  cf->fromString(s);
  return cf;
}

void gtfo()
{
  if (mx) mx->stop();
  ZeLog::stop();
  Zm::exit(1);
}

int main(int argc_, char **argv)
{
  Options options;
  int argc = ZtCLI::load(options, argc_, argv);
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

  ZmRef<ZvCf> cf = inlineCf(
    "thread zdb\n"
    "hostID 0\n"
    "hosts {\n"
    "  0 { standalone 1 }\n"
    "}\n"
    "store {\n"
    "  thread zdb_pq\n"
    "  replicated true\n"
    "}\n"
    "tables {\n"
    "  order { warmup 1 }\n"
    "}\n"
    "mx {\n"
    "  nThreads 4\n"
    "  threads {\n"
    "    1 { name rx isolated true }\n"
    "    2 { name tx isolated true }\n"
    "    3 { name zdb isolated true }\n"
    "    4 { name zdb_pq isolated true }\n"
    "  }\n"
    "  rxThread rx\n"
    "  txThread tx\n"
    "}\n");

  cf->set("store.module", options.module);
  cf->set("store.connection", options.connect);
  cf->set("debug", options.debug ? "1" : "0");

  ZeLog::init("zdbpqtest");
  ZeLog::level(0);
  ZeLog::sink(ZeLog::fileSink(ZeSinkOptions{}.path("&2"))); // log to stderr
  ZeLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->getCf<true>("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "multiplexer start failed");

    db = new Zdb();

    db->init(ZdbCf(cf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZeLOG(Info, ([id = host ? host->id() : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
      },
      .downFn = [](Zdb *, bool) { ZeLOG(Info, "INACTIVE"); }
    });

    orders = db->initTable<Order>("order"); // might throw

    if (!db->start()) throw ZeEXCEPT(Fatal, "Zdb start failed");

    if (options.hashTel)
      ZeLOG(Debug, (ZeString{} << '\n' << ZmHashMgr::csv()));

    if (options.heapTel)
      ZeLOG(Debug, (ZeString{} << '\n' << ZmHeapMgr::csv()));

    ZuNBox<uint64_t> seqNo;

    orders->selectKeys<2>(
      ZuFwdTuple("FIX0"), 1, [&seqNo](auto max, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (max.template is<Key>()) {
	  seqNo = max.template p<Key>().template p<1>();
	  ZeLOG(Info, ([max = ZuMv(max)](auto &s) {
	    s << "maximum(FIX0): " << max.template p<Key>();
	  }));
	} else {
	  ZeLOG(Info, ([max = ZuMv(max)](auto &s) {
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
	      ZeLOG(Info, ([seqNo](auto &s) {
		s << "find(FIX0, " << seqNo << "): (null)";
	      }));
	    } else {
	      id = o->data().orderID;
	      ZeLOG(Info, ([seqNo, o = ZuMv(o)](auto &s) {
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
	ZeLOG(Info, ([id, seqNo](auto &s) {
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
	    ZeLOG(Info, ([id](auto &s) {
	      s << "find(IBM, " << id << "): (null)";
	    }));
	  else
	    ZeLOG(Info, ([id, o = ZuMv(o)](auto &s) {
	      s << "find(IBM, " << id << "): " << *o;
	    }));
	  done.post();
	});
    });
    done.wait();

    orders->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
      using Key = ZuStructKeyT<Order, 2>;
      if (max.template is<Key>()) {
	ZeLOG(Info, ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): " << max.template p<Key>();
	}));
      } else {
	ZeLOG(Info, ([max = ZuMv(max)](auto &s) {
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
	      ZeLOG(Info, ([id](auto &s) {
		s << "findUpd(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZeLOG(Info, ([id, data = o->data()](auto &s) {
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
	      ZeLOG(Info, ([id](auto &s) {
		s << "findDel(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZeLOG(Info, ([id, o](auto &s) {
	      s << "findDel(IBM, " << id << "): " << *o;
	    }));
	    o->commit();
	    done.post();
	  });
      });
      done.wait();
    }

    if (options.hashTel)
      ZeLOG(Debug, (ZeString{} << '\n' << ZmHashMgr::csv()));

    if (options.heapTel)
      ZeLOG(Debug, (ZeString{} << '\n' << ZmHeapMgr::csv()));

    db->stop(); // closes all tables

    mx->stop();

    orders = {};
    db->final(); // calls Store::final()
    db = {};

  } catch (const ZeException &e) {
    ZeLOG(Fatal, e);
    gtfo();
  } catch (const ZeError &e) {
    ZeLOG(Fatal, e.message());
    gtfo();
  } catch (...) {
    ZeLOG(Fatal, "unknown exception");
    gtfo();
  }

  mx = {};

  ZeLog::stop();

  return 0;
}
