//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>
#include <libpq-fe.h>
#include <string.h>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZfCf.hh>

#include "../../zdb/test/ZdbTestCf.hh"
#include <zlib/ZvMxParams.hh>

#include <zlib/Zdb.hh>

#include "ZdbTest.hh"
#include "ZdbRepairTest.hh"

using namespace zdbtest;
using namespace ZuTestUtil;

// command line options

struct Options {
  ZuCSpan	module;
  ZuCSpan	connect;
  bool		debug = false;
  bool		hashTel = false;
  bool		heapTel = false;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (module,  (CLI::Opt<'m'>),					String),
  (connect, (CLI::Opt<'c'>),					String),
  (debug,   (CLI::Flag<'d'>),					Bool),
  (hashTel, (CLI::Flag<'t'>, CLI::Long<"hash-tel">),		Bool),
  (heapTel, (CLI::Flag<'T'>, CLI::Long<"heap-tel">),		Bool),
  (help,    (CLI::Flag<'h'>),					Bool));

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
ZmRef<ZdbTable<Order>> payments;

// multiplexer
ZuPtr<ZiMultiplex> mx;

ZmSemaphore done;

struct DiagOne { int64_t a; };
ZfStruct(, (DiagOne, FB), (a, (Ctor<0>, Keys<0>), Int64));
ZiFB::AsTable<"test.DiagOne"> ZiFB_Fmt(DiagOne *);
struct DiagTwo { int64_t a; int64_t b; };
ZfStruct(, (DiagTwo, FB),
  (a, (Ctor<0>, Keys<0>), Int64),
  (b, (Ctor<1>), Int64));
ZiFB::AsTable<"test.DiagTwo"> ZiFB_Fmt(DiagTwo *);

template <typename T>
static void mismatch(
  PGconn *conn, ZuCSpan id, ZuCSpan columns, const char *expected)
{
  ZuTestScope(mismatch);
  using namespace Zdb_;
  auto exec = [conn](const ZtString<> &sql) {
    auto result = PQexec(conn, sql.data());
    bool ok = result && PQresultStatus(result) == PGRES_COMMAND_OK;
    if (!ok) log(PQerrorMessage(conn));
    if (result) PQclear(result);
    ZuCHECK(ok, "diagnostic fixture DDL");
    return ok;
  };
  ZtString<> sql;
  sql << "DROP TABLE IF EXISTS public.\"" << id << '"';
  if (!exec(sql)) return;
  sql.null();
  sql << "CREATE TABLE public.\"" << id << "\" (" << columns << ')';
  if (!exec(sql)) return;
  SchemaBuf bytes(ZiFB::SchemaOut::Schema<T>::Size);
  ZiFB::saveSchema<T>(bytes);
  auto schema = Schema::from<T>(bytes);
  auto result = ZmBlock<OpenResult>{}([id, schema](auto wake) {
    db->store()->open(false, IDString{id}, ZfVFields<T>(), ZfVKeyFields<T>(),
      schema, Table<T>::allocBuf, ZuMv(wake));
  });
  ZuCHECK(result.template is<Event>(), "OID mismatch completes open with error");
  if (result.template is<Event>()) {
    ZtString<> text;
    text << result.template p<Event>();
    ZuCHECK(strstr(text.data(), expected), "diagnostic identifies failing field or missing-column count");
    if (!strstr(expected, "field["))
      ZuCHECK(!strstr(text.data(), "field["), "general diagnostic has no invented field index");
  } else if (result.template is<OpenData>()) {
    auto table = result.template p<OpenData>().storeTbl;
    ZmBlock<>{}([table](auto wake) { table->close(ZuMv(wake)); });
  }
  sql.null();
  sql << "DROP TABLE public.\"" << id << '"';
  exec(sql);
}

static void repairColumns(const char *connect)
{
  ZuTestScopeRT(repairColumns);
  auto conn = PQconnectdb(connect);
  bool ok = conn && PQstatus(conn) == CONNECTION_OK;
  ZuCHECK(ok, "conversion fixture connection");
  if (!ok) { if (conn) PQfinish(conn); return; }
  // Reset this fixture's durable stream position before starting at UN zero.
  auto result = PQexec(conn,
    "DROP TABLE IF EXISTS public.repair; DELETE FROM zdb.mrd WHERE tbl='arepair'");
  ok = result && PQresultStatus(result) == PGRES_COMMAND_OK;
  if (!ok) log(PQerrorMessage(conn));
  if (result) PQclear(result);
  PQfinish(conn);
  ZuCHECK(ok, "conversion fixture reset");
  if (ok) ZuTestCallRT(zdbtest::repairBindings, db->store());
}

static void schemaMismatch(const char *connect)
{
  ZuTestScope(schemaMismatch);
  auto conn = PQconnectdb(connect);
  bool ok = conn && PQstatus(conn) == CONNECTION_OK;
  ZuCHECK(ok, "diagnostic fixture connection");
  if (!ok) { if (conn) PQfinish(conn); return; }
  const char *reserved = "\"_shard\" uint1, \"_un\" uint8, \"_sn\" uint16, \"_vn\" int8, ";
  ZtString<> columns;
  columns << reserved << "a int4";
  mismatch<DiagOne>(conn, "repair_diag_one", columns, "field[0]={id=a");
  // Physical/catalog order differs from metadata; both names are counted before a fails.
  columns.null(); columns << reserved << "b int8, a int4";
  mismatch<DiagTwo>(conn, "repair_diag_shuffled", columns, "field[0]={id=a");
  columns.null(); columns << reserved << "a int8, b int4";
  mismatch<DiagTwo>(conn, "repair_diag_last", columns, "field[1]={id=b");
  columns.null(); columns << reserved << "a int8";
  mismatch<DiagTwo>(conn, "repair_diag_missing", columns, "matched=1 nFields=2");
  mismatch<DiagOne>(conn, "repair_diag_reserved",
    "\"_shard\" int4, \"_un\" uint8, \"_sn\" uint16, \"_vn\" int8, a int8",
    "failed=1 matched=0 nFields=1");
  PQfinish(conn);
}

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

static void cursors()
{
  ZuTestScope(cursors);
  for (unsigned i = 0; i < 3; ++i) {
    bool committed = false;
    orders->run(0, [i, &committed]() {
      ZdbRowRef<Order> row = new ZdbRow<Order>{orders, 0};
      orders->insert(ZuMv(row), [i, &committed](ZdbRow<Order> *row) {
	if (row) {
	  new (row->ptr()) Order{};
	  auto &data = row->data();
	  data.symbol = "CURSOR";
	  data.orderID = i + 1;
	  data.link = i < 2 ? "scan-a" : "scan-b";
	  data.clOrdID = i == 1 ? "b" : "a";
	  data.seqNo = 102 - i;
	  committed = row->commit();
	}
	done.post();
      });
    });
    done.wait();
    ZuCheck(committed);
  }
  for (bool inclusive: {false, true}) {
    unsigned count = 0;
    bool matched = true;
    orders->nextKeys<1>(ZuFwdTuple("scan-a", "a"), inclusive, 3,
      [inclusive, &count, &matched](auto result, unsigned) {
	using Key = ZuStructKeyT<Order, 1>;
	if (!result.template is<Key>()) { done.post(); return; }
	auto key = ZuMv(result).template p<Key>();
	unsigned index = count++ + !inclusive;
	matched &= index < 3 &&
	  key.template p<0>() == (index < 2 ? "scan-a" : "scan-b") &&
	  key.template p<1>() == (index == 1 ? "b" : "a");
      });
    done.wait();
    ZuCheck(matched);
    ZuCheck(count == (inclusive ? 3 : 2));
    count = 0;
    matched = true;
    orders->nextKeys<2>(ZuFwdTuple("scan-a", uint64_t{102}), inclusive, 3,
      [inclusive, &count, &matched](auto result, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (!result.template is<Key>()) { done.post(); return; }
	auto key = ZuMv(result).template p<Key>();
	unsigned index = count++ + !inclusive;
	matched &= index < 2 && key.template p<0>() == "scan-a" &&
	  key.template p<1>() == 102 - index;
      });
    done.wait();
    ZuCheck(matched);
    ZuCheck(count == (inclusive ? 2 : 1));
  }
}

int main(int argc_, char **argv)
{
  ZuTestMain();
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
  if (!options.connect.data()) options.connect = getenv("ZDB_CONNECT");
  ZtString<> moduleEnv;
  if (!options.module)
    if (auto path = Zt::getpath("ZDB_MODULE"))
      options.module = moduleEnv = path;
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
    "shards: 4,\n"
    "hostID: 0,\n"
    "hosts: {0: {standalone: true}},\n"
    "store: {\n"
    "  thread: zdb_pq,\n"
    "  replicated: true,\n"
    "  module: ${MODULE},\n"
    "  connect: ${CONNECT}\n"
    "},\n"
    "tables: {order: {warmup: true}, payment: {}},\n"
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
    auto cfFields = ZfCf::handler<zdbtest::ConfigFields>(cf).ctor();
    mx = new ZiMultiplex{ZvMxParams("mx", cfFields.mx.p<const ZfCf::AnyNode *>())};

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
    payments = db->initTable<Order>("payment");

    if (!db->start()) throw ZeEXCEPT(Fatal, "zdbpqtest", "Zdb start failed");

    payments->run(1, [] {
      payments->find<0>(1, ZuFwdTuple("MISSING", UINT64_C(0)),
	[](ZmRef<ZdbRow<Order>>) { done.post(); });
    });
    done.wait();

    if (options.hashTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << Ztc::hashCSV()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << Ztc::heapCSV()));

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
	  [seqNo, &id](ZmRef<ZdbRow<Order>> o) {
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
      ZdbRowRef<Order> o = new ZdbRow<Order>{orders, 0};
      orders->insert(o, [&id, &seqNo](ZdbRow<Order> *o) {
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
      });
      // Repeating an insert must not fail the DB or add a durable row.
      ZdbRowRef<Order> duplicate = new ZdbRow<Order>{orders, 0};
      orders->insert(duplicate, [o](ZdbRow<Order> *row) {
	if (row) {
	  new (row->ptr()) Order{o->data()};
	  row->commit();
	}
	done.post();
      });
    });
    done.wait();

    orders->run(0, [&id]{
      orders->find<0>(0, ZuFwdTuple("IBM", id),
	[&id](ZmRef<ZdbRow<Order>> o) {
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
	  [id](ZmRef<ZdbRow<Order>> o) {
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
	  [id](ZmRef<ZdbRow<Order>> o) {
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
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << Ztc::hashCSV()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbpqtest", (ZeString{} << '\n' << Ztc::heapCSV()));

    bool active = false;
    ZmBlock<>{}([&active](auto wake) {
      db->run([&active, wake = ZuMv(wake)]() mutable {
	active = db->active();
	wake();
      });
    });
    ZuCHECK(active, "DB remains active after repeated inserts");

    ZuTestCall(repairColumns, options.connect.data());
    ZuTestCall(cursors);
    ZuTestCall(schemaMismatch, options.connect.data());

    db->stop(); // closes all tables

    mx->stop();

    orders = {};
    payments = {};
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

  ZuCHECK(true, "PostgreSQL database lifecycle");
  return 0;
}
