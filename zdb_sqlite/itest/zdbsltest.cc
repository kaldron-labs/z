//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <math.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/Zdb.hh>

#include "ZdbTest.hh"
#include "ZdbSLTypes.hh"

using namespace zdbtest;
using namespace zdbsltest;
using namespace ZuTestUtil;

// command line options

struct Options {
  ZuCSpan	module;
  ZuCSpan	connect;
  ZuCSpan	synchronous;
  bool		debug = false;
  bool		hashTel = false;
  bool		heapTel = false;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (module,  (CLI::Opt<'m'>),					String),
  (connect, (CLI::Opt<'c'>),					String),
  (synchronous, (CLI::Opt<'s'>),				String),
  (debug,   (CLI::Flag<'d'>),					Bool),
  (hashTel, (CLI::Flag<'t'>, CLI::Long<"hash-tel">),		Bool),
  (heapTel, (CLI::Flag<'T'>, CLI::Long<"heap-tel">),		Bool),
  (help,    (CLI::Flag<'h'>),					Bool));

void usage()
{
  static const char *help =
    "Usage: zdbsltest [OPTION]...\n\n"
    "Options:\n"
    "      --help\t\tthis help\n"
    "  -m, --module=MODULE\tspecify data store module (default: $ZDB_MODULE)\n"
    "  -c, --connect=CONNECT\t"
      "specify data store connection (default: $ZDB_CONNECT)\n"
    "  -s, --synchronous=MODE\tSQLite NORMAL, FULL or OFF\n"
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
ZmRef<ZdbTable<AllTypes>> allTypes;

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

static void nonUnique()
{
  ZuTestScope(nonUnique);
  for (uint64_t id: {UINT64_C(900001), UINT64_C(900002)}) {
    orders->run(0, [id]() {
      orders->findDel<0>(0, ZuFwdTuple("DUPSK", id),
	[](ZmRef<ZdbRow<Order>> row) {
	  if (row) row->commit();
	  done.post();
	});
    });
    done.wait();
  }
  for (uint64_t id: {UINT64_C(900001), UINT64_C(900002)}) {
    bool committed = false;
    orders->run(0, [id, &committed]() {
      ZdbRowRef<Order> row = new ZdbRow<Order>{orders, 0};
      orders->insert(ZuMv(row), [id, &committed](ZdbRow<Order> *row) {
	if (row) {
	  new (row->ptr()) Order{
	    "DUPSK", id, "same", "same", id, Side::Buy, {100}, {100}};
	  committed = row->commit();
	}
	done.post();
      });
    });
    done.wait();
    ZuCHECK(committed, "insert duplicate secondary key");
  }

  bool updated = false;
  orders->run(0, [&updated]() {
    orders->findUpd<1>(0, ZuFwdTuple("same", "same"),
      [&updated](ZmRef<ZdbRow<Order>> row) {
	if (row) {
	  row->data().prices[0] = 777;
	  updated = row->commit();
	}
	done.post();
      });
  });
  done.wait();
  ZuCHECK(updated, "update one duplicate secondary-key match");

  unsigned updatedRows = 0;
  for (uint64_t id: {UINT64_C(900001), UINT64_C(900002)}) {
    orders->run(0, [id, &updatedRows]() {
      orders->find<0>(0, ZuFwdTuple("DUPSK", id),
	[&updatedRows](ZmRef<ZdbRow<Order>> row) {
	  if (row && row->data().prices[0] == 777) ++updatedRows;
	  done.post();
	});
    });
    done.wait();
  }
  ZuCHECK(updatedRows == 1, "secondary-key update changes one row");

  bool removed = false;
  orders->run(0, [&removed]() {
    orders->findDel<1>(0, ZuFwdTuple("same", "same"),
      [&removed](ZmRef<ZdbRow<Order>> row) {
	if (row) removed = row->commit();
	done.post();
      });
  });
  done.wait();
  ZuCHECK(removed, "delete one duplicate secondary-key match");

  unsigned remaining = 0;
  for (uint64_t id: {UINT64_C(900001), UINT64_C(900002)}) {
    orders->run(0, [id, &remaining]() {
      orders->find<0>(0, ZuFwdTuple("DUPSK", id),
	[&remaining](ZmRef<ZdbRow<Order>> row) {
	  if (row) ++remaining;
	  done.post();
	});
    });
    done.wait();
  }
  ZuCHECK(remaining == 1, "secondary-key delete removes one row");
}

static void setTypes(AllTypes &v)
{
  v.id = 1;
  v.stringValue = ZuCSpan{"A\0B", 3};
  v.bytesValue = {0, 1, 255};
  v.boolValue = true;
  v.int8Value = INT8_MIN;
  v.uint8Value = UINT8_MAX;
  v.int16Value = INT16_MIN;
  v.uint16Value = UINT16_MAX;
  v.int32Value = INT32_MIN;
  v.uint32Value = UINT32_MAX;
  v.int64Value = INT64_MIN;
  v.uint64Value = UINT64_MAX;
  v.int128Value = -(int128_t(1)<<100) + 7;
  v.uint128Value = (uint128_t(1)<<100) + 9;
  v.floatValue = -1.25;
  v.fixedValue = ZuFixed{"-12.3400", 4};
  v.decimalValue = ZuDecimal{"123.000000000000000001"};
  v.timeValue = ZuTime{-1, 999999999};
  v.dateTimeValue = ZuDateTime{
    ZuDateTime::Julian{-1}, 86399, 999999999};
  v.bitmapValue = ZtBitmap{"0,63,64,130"};
  v.ipValue = ZiIP{"2001:db8::1"};
  v.stringVec = {"", "alpha", ZuCSpan{"X\0Y", 3}};
  v.bytesVec = {{}, {0, 255}, {1, 2, 3}};
  v.int8Vec = {INT8_MIN, -1, 0, INT8_MAX};
  v.uint8Vec = {0, 1, UINT8_MAX};
  v.int16Vec = {INT16_MIN, -1, INT16_MAX};
  v.uint16Vec = {0, 1, UINT16_MAX};
  v.int32Vec = {INT32_MIN, -1, INT32_MAX};
  v.uint32Vec = {0, 1, UINT32_MAX};
  v.int64Vec = {INT64_MIN, -1, INT64_MAX};
  v.uint64Vec = {0, 1, UINT64_MAX};
  v.int128Vec = {-(int128_t(1)<<100), -1, (int128_t(1)<<100)};
  v.uint128Vec = {0, 1, (uint128_t(1)<<100)};
  v.floatVec = {-INFINITY, -0.0, 1.5, INFINITY, NAN};
  v.fixedVec = {ZuFixed{}, ZuFixed{"0", 2}, ZuFixed{"-1.25", 2}};
  v.decimalVec = {ZuDecimal{}, ZuDecimal{"0"}, ZuDecimal{"1.000000000000000001"}};
  v.timeVec = {ZuTime{}, ZuTime{-1, 999999999}, ZuTime{0, 0}};
  v.dateTimeVec = {
    ZuDateTime{},
    ZuDateTime{ZuDateTime::Julian{-1}, 86399, 999999999},
    ZuDateTime{ZuDateTime::Julian{0}, 0, 0}};
}

static bool checkTypes(const AllTypes &v)
{
  return v.id == 1 && v.stringValue == ZuCSpan{"A\0B", 3} &&
    v.bytesValue == TestBytes{0, 1, 255} && v.boolValue &&
    v.int8Value == INT8_MIN && v.uint8Value == UINT8_MAX &&
    v.int16Value == INT16_MIN && v.uint16Value == UINT16_MAX &&
    v.int32Value == INT32_MIN && v.uint32Value == UINT32_MAX &&
    v.int64Value == INT64_MIN && v.uint64Value == UINT64_MAX &&
    v.int128Value == -(int128_t(1)<<100) + 7 &&
    v.uint128Value == (uint128_t(1)<<100) + 9 &&
    v.floatValue == -1.25 && v.fixedValue == ZuFixed{"-12.34", 2} &&
    v.decimalValue == ZuDecimal{"123.000000000000000001"} &&
    v.timeValue == ZuTime{-1, 999999999} &&
    v.dateTimeValue == ZuDateTime{
      ZuDateTime::Julian{-1}, 86399, 999999999} &&
    v.bitmapValue == ZtBitmap{"0,63,64,130"} &&
    v.ipValue == ZiIP{"2001:db8::1"} &&
    v.stringVec.length() == 3 && !v.stringVec[0] &&
    v.stringVec[1] == "alpha" && v.stringVec[2] == ZuCSpan{"X\0Y", 3} &&
    v.bytesVec == TestBytesVec{{}, {0, 255}, {1, 2, 3}} &&
    v.int8Vec == ZtArray<int8_t>{INT8_MIN, -1, 0, INT8_MAX} &&
    v.uint8Vec == ZtArray<uint8_t>{0, 1, UINT8_MAX} &&
    v.int16Vec == ZtArray<int16_t>{INT16_MIN, -1, INT16_MAX} &&
    v.uint16Vec == ZtArray<uint16_t>{0, 1, UINT16_MAX} &&
    v.int32Vec == ZtArray<int32_t>{INT32_MIN, -1, INT32_MAX} &&
    v.uint32Vec == ZtArray<uint32_t>{0, 1, UINT32_MAX} &&
    v.int64Vec == ZtArray<int64_t>{INT64_MIN, -1, INT64_MAX} &&
    v.uint64Vec == ZtArray<uint64_t>{0, 1, UINT64_MAX} &&
    v.int128Vec == ZtArray<int128_t>{
      -(int128_t(1)<<100), -1, (int128_t(1)<<100)} &&
    v.uint128Vec == ZtArray<uint128_t>{0, 1, (uint128_t(1)<<100)} &&
    v.floatVec.length() == 5 && isinf(v.floatVec[0]) &&
    v.floatVec[0] < 0 && v.floatVec[1] == 0 && !signbit(v.floatVec[1]) &&
    v.floatVec[2] == 1.5 && isinf(v.floatVec[3]) &&
    v.floatVec[3] > 0 && isnan(v.floatVec[4]) &&
    v.fixedVec.length() == 3 && !*v.fixedVec[0] && *v.fixedVec[1] &&
    !v.fixedVec[1] && v.fixedVec[2] == ZuFixed{"-1.25", 2} &&
    v.decimalVec == ZtArray<ZuDecimal>{
      ZuDecimal{}, ZuDecimal{"0"}, ZuDecimal{"1.000000000000000001"}} &&
    v.timeVec == ZtArray<ZuTime>{
      ZuTime{}, ZuTime{-1, 999999999}, ZuTime{0, 0}} &&
    v.dateTimeVec == ZtArray<ZuDateTime>{
      ZuDateTime{},
      ZuDateTime{ZuDateTime::Julian{-1}, 86399, 999999999},
      ZuDateTime{ZuDateTime::Julian{0}, 0, 0}};
}

static void types()
{
  ZuTestScope(types);
  for (uint64_t id: {uint64_t{1}, uint64_t{2}}) {
    allTypes->run(0, [id] {
      allTypes->findDel<0>(0, ZuFwdTuple(id),
	[](ZmRef<ZdbRow<AllTypes>> row) {
	  if (row) row->commit();
	  done.post();
	});
    });
    done.wait();
  }

  bool committed = false;
  allTypes->run(0, [&committed] {
    ZdbRowRef<AllTypes> row = new ZdbRow<AllTypes>{allTypes, 0};
    allTypes->insert(ZuMv(row), [&committed](ZdbRow<AllTypes> *row) {
      if (row) {
	new (row->ptr()) AllTypes{};
	setTypes(row->data());
	committed = row->commit();
      }
      done.post();
    });
  });
  done.wait();
  ZuCHECK(committed, "all type families insert");

  bool emptyCommitted = false;
  allTypes->run(0, [&emptyCommitted] {
    ZdbRowRef<AllTypes> row = new ZdbRow<AllTypes>{allTypes, 0};
    allTypes->insert(ZuMv(row), [&emptyCommitted](ZdbRow<AllTypes> *row) {
      if (row) {
	new (row->ptr()) AllTypes{.id = 2};
	emptyCommitted = row->commit();
      }
      done.post();
    });
  });
  done.wait();
  ZuCHECK(emptyCommitted, "empty type families insert");

  if (!db->stop() || !db->start())
    throw ZeEXCEPT(Fatal, "zdbsltest", "all-types restart failed");

  bool matched = false;
  allTypes->run(0, [&matched] {
    allTypes->evict<0>(0, ZuFwdTuple(uint64_t{1}));
    allTypes->find<0>(0, ZuFwdTuple(uint64_t{1}),
      [&matched](ZmRef<ZdbRow<AllTypes>> row) {
	matched = row && checkTypes(row->data());
	done.post();
      });
  });
  done.wait();
  ZuCHECK(matched, "all type families round trip");

  bool emptyMatched = false;
  allTypes->run(0, [&emptyMatched] {
    allTypes->evict<0>(0, ZuFwdTuple(uint64_t{2}));
    allTypes->find<0>(0, ZuFwdTuple(uint64_t{2}),
      [&emptyMatched](ZmRef<ZdbRow<AllTypes>> row) {
	if (row) {
	  auto &v = row->data();
	  emptyMatched = v.id == 2 && !v.stringValue && !v.bytesValue &&
	    !v.bitmapValue && !v.ipValue && !v.stringVec && !v.bytesVec &&
	    !v.int8Vec && !v.uint8Vec && !v.int16Vec && !v.uint16Vec &&
	    !v.int32Vec && !v.uint32Vec && !v.int64Vec && !v.uint64Vec &&
	    !v.int128Vec && !v.uint128Vec && !v.floatVec && !v.fixedVec &&
	    !v.decimalVec && !v.timeVec && !v.dateTimeVec;
	}
	done.post();
      });
  });
  done.wait();
  ZuCHECK(emptyMatched, "empty type families round trip");
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
  if (!options.synchronous.data()) {
    auto mode = getenv("ZDB_SYNCHRONOUS");
    options.synchronous = mode ? mode : "NORMAL";
  }
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
    ZfCf::DefKey{"SYNCHRONOUS"}, ZfCf::DefVal{options.synchronous});
  defines->add(
    ZfCf::DefKey{"DEBUG"}, ZfCf::DefVal{options.debug ? "true" : "false"});
  auto cf = inlineCf(
    "thread: zdb,\n"
    "shards: 4,\n"
    "hostID: 0,\n"
    "hosts: {0: {standalone: true}},\n"
    "store: {\n"
    "  thread: zdb_sl,\n"
    "  module: ${MODULE},\n"
    "  connect: ${CONNECT},\n"
    "  synchronous: ${SYNCHRONOUS}\n"
    "},\n"
    "tables: {order: {warmup: true}, payment: {}, all_types: {}},\n"
    "debug: ${DEBUG},\n"
    "mx: {\n"
    "  nThreads: 4,\n"
    "  threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: zdb_sl, isolated: true}\n"
    "  },\n"
    "  rxThread: rx,\n"
    "  txThread: tx\n"
    "}\n", defines);

  ZiLog::init("zdbsltest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams("mx", cf->resolve("mx"))};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdbsltest", "multiplexer start failed");

    db = new Zdb();

    db->init(ZdbCf(cf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdbsltest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
      },
      .downFn = [](Zdb *, bool) { ZiLOG(Info, "zdbsltest", "INACTIVE"); }
    });

    orders = db->initTable<Order>("order"); // might throw
    payments = db->initTable<Order>("payment");
    allTypes = db->initTable<AllTypes>("all_types");

    if (!db->start()) throw ZeEXCEPT(Fatal, "zdbsltest", "Zdb start failed");

    payments->run(1, [] {
      payments->find<0>(1, ZuFwdTuple("MISSING", UINT64_C(0)),
	[](ZmRef<ZdbRow<Order>>) { done.post(); });
    });
    done.wait();

    if (options.hashTel)
      ZiLOG(Debug, "zdbsltest", (ZeString{} << '\n' << Ztc::hashCSV()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbsltest", (ZeString{} << '\n' << Ztc::heapCSV()));

    ZuNBox<uint64_t> seqNo;

    orders->selectKeys<2>(
      ZuFwdTuple("FIX0"), 1, [&seqNo](auto max, unsigned) {
	using Key = ZuStructKeyT<Order, 2>;
	if (max.template is<Key>()) {
	  seqNo = max.template p<Key>().template p<1>();
	  ZiLOG(Info, "zdbsltest", ([max = ZuMv(max)](auto &s) {
	    s << "maximum(FIX0): " << max.template p<Key>();
	  }));
	} else {
	  ZiLOG(Info, "zdbsltest", ([max = ZuMv(max)](auto &s) {
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
	      ZiLOG(Info, "zdbsltest", ([seqNo](auto &s) {
		s << "find(FIX0, " << seqNo << "): (null)";
	      }));
	    } else {
	      id = o->data().orderID;
	      ZiLOG(Info, "zdbsltest", ([seqNo, o = ZuMv(o)](auto &s) {
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
	ZiLOG(Info, "zdbsltest", ([id, seqNo](auto &s) {
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
	    ZiLOG(Info, "zdbsltest", ([id](auto &s) {
	      s << "find(IBM, " << id << "): (null)";
	    }));
	  else
	    ZiLOG(Info, "zdbsltest", ([id, o = ZuMv(o)](auto &s) {
	      s << "find(IBM, " << id << "): " << *o;
	    }));
	  done.post();
	});
    });
    done.wait();

    orders->selectKeys<2>(ZuFwdTuple("FIX0"), 1, [](auto max, unsigned) {
      using Key = ZuStructKeyT<Order, 2>;
      if (max.template is<Key>()) {
	ZiLOG(Info, "zdbsltest", ([max = ZuMv(max)](auto &s) {
	  s << "maximum(FIX0): " << max.template p<Key>();
	}));
      } else {
	ZiLOG(Info, "zdbsltest", ([max = ZuMv(max)](auto &s) {
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
	      ZiLOG(Info, "zdbsltest", ([id](auto &s) {
		s << "findUpd(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZiLOG(Info, "zdbsltest", ([id, data = o->data()](auto &s) {
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
	      ZiLOG(Info, "zdbsltest", ([id](auto &s) {
		s << "findDel(IBM, " << id << "): (null)";
	      }));
	      return;
	    }
	    ZiLOG(Info, "zdbsltest", ([id, o](auto &s) {
	      s << "findDel(IBM, " << id << "): " << *o;
	    }));
	    o->commit();
	    done.post();
	  });
      });
      done.wait();
    }

    if (options.hashTel)
      ZiLOG(Debug, "zdbsltest", (ZeString{} << '\n' << Ztc::hashCSV()));

    if (options.heapTel)
      ZiLOG(Debug, "zdbsltest", (ZeString{} << '\n' << Ztc::heapCSV()));

    bool active = false;
    ZmBlock<>{}([&active](auto wake) {
      db->run([&active, wake = ZuMv(wake)]() mutable {
	active = db->active();
	wake();
      });
    });
    ZuCHECK(active, "DB remains active after repeated inserts");

    ZuTestCall(cursors);
    ZuTestCall(nonUnique);
    ZuTestCall(types);

    db->stop(); // closes all tables

    mx->stop();

    orders = {};
    payments = {};
    allTypes = {};
    db->final(); // calls Store::final()
    db = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdbsltest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdbsltest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  ZuCHECK(true, "SQLite database lifecycle");
  return 0;
}
