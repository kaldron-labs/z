//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>
#include <atomic>

#include <zlib/ZuLib.hh>
#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSeries.hh>
#include <zlib/ZdfStore.hh>
#include <zlib/ZdfStats.hh>

using namespace ZuTestUtil;

static std::atomic<bool> passed{true};
#define CHECK(x) do { if (!(x)) passed.store(false); } while (0)
#define CHECK2(x, y) CHECK((x) == (y))

// database
ZmRef<Zdb> db;

// dataframe store
ZuPtr<Zdf::Store> store;

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

struct Frame {
  uint64_t	seqNo;
  ZuTime	time;
  double	price;
};
ZfStruct(Frame,
  (((seqNo),	(Ctor<0>, Series, Index, Delta)),	(UInt64)),
  (((time),	(Ctor<1>, Series, Index, Delta)),	(Time, "2020/01/01")),
  (((price),	(Ctor<2>, Series, NDP<9>)),		(Float)));

void usage()
{
  static const char *help =
    "Usage: zdffpftest [OPTION]...\n\n"
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

using DF = Zdf::DataFrame<Frame, false>;
using DFWriter = DF::Writer;

struct Test {
  ZmRef<DF>		df;
  ZmQueue<double>	queue{ZmQueueParams{}.initial(100)};
  Zdf::StatsTree<>	stats;

  static double price(double i) {
    return (double(i) * 42) * .000000001;
  }

  void run() {
    store->openDF<Frame, false, true>(
      0, "frame", {this, ZmFnPtr<&Test::run_opened>{}});
  }
  void run_opened(ZmRef<DF> df_) {
    if (!df_) {
      ZiLOG(Fatal, "zdffptest", "data frame open failed");
      done.post();
      return;
    }
    df = ZuMv(df_);
    auto count = df->series<ZfField(Frame, seqNo)>()->count();
    if (count) {
      df->run([this]() { run_read1(); });
    } else
      df->write({this, ZmFnPtr<&Test::run_write>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame write failed");
	done.post();
      });
  }
  void run_write(ZmRef<DFWriter> w) {
    Frame frame;
    for (int64_t i = 0; i < 100000; i++) { // 10000; i++) {
      frame.seqNo = i;
      frame.time = Zm::now();
      frame.price = price(i);
      w->write(frame);
    }
    df->run([this]() { run_read1(); });
  }
  void run_read1() {
    using Field = ZfField(Frame, seqNo);
    using Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->find<Field>(
      ZuFixed{20, 0}, {this, ZmFnPtr<&Test::run_read2<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame read2 failed");
	done.post();
      });
  }
  template <typename Ctrl>
  bool run_read2(Ctrl &rc, ZuFixed) {
    using Field = ZfField(Frame, price);
    using V2Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->seek<Field>(
      rc.stop() - 1, {this, ZmFnPtr<&Test::run_read3<V2Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame read3 failed");
	done.post();
      });
    return false;
  }
  template <typename Ctrl>
  bool run_read3(Ctrl &rc, double v) {
    CHECK(ZuBoxed(v).feq(0.00000084));
    rc.fn({this, ZmFnPtr<&Test::run_read4<Ctrl>>{}});
    rc.findFwd(0.0000084);
    return false;
  }
  template <typename Ctrl>
  bool run_read4(Ctrl &rc, double v) {
    CHECK(ZuBoxed(v).feq(0.0000084));
    using Field = ZfField(Frame, seqNo);
    using V1Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->seek<Field>(
      rc.stop() - 1, {this, ZmFnPtr<&Test::run_read5<V1Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame read5 failed");
	done.post();
      });
    return false;
  }
  template <typename Ctrl>
  bool run_read5(Ctrl &rc, ZuFixed) {
    rc.fn({this, ZmFnPtr<&Test::run_read6<Ctrl>>{}});
    rc.findRev(ZuFixed{100, 0});
    return false;
  }
  template <typename Ctrl>
  bool run_read6(Ctrl &rc, ZuFixed) {
    using Field = ZfField(Frame, price);
    using V2Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->seek<Field>(
      rc.stop() - 1, {this, ZmFnPtr<&Test::run_read7<V2Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame read7 failed");
	done.post();
      });
    return false;
  }
  template <typename Ctrl>
  bool run_read7(Ctrl &rc, double v) {
    CHECK(ZuBoxed(v).feq(0.0000042));
    rc.fn({this, ZmFnPtr<&Test::run_read8<Ctrl>>{}});
    rc.seekRev(0);
    return false;
  }
  template <typename Ctrl>
  bool run_read8(Ctrl &rc, double v) {
    queue.push(v);
    stats.add(v);

    if (queue.count_() < 100) return true;

    v = queue.shift();
    stats.del(v);
    std::cerr << "min=" << ZuBoxed(stats.minimum()) <<
      " max=" << ZuBoxed(stats.maximum()) <<
      " mean=" << ZuBoxed(stats.mean()) <<
      " stdev=" << ZuBoxed(stats.std()) <<
      " median=" << ZuBoxed(stats.median()) <<
      " 95%=" << ZuBoxed(stats.rank(0.95)) << '\n';

    if (rc.reader.offset() < 110) return true;
    df->run([this]() { run_read9(); });
    return false;
  }
  void run_read9() {
    using Field = ZfField(Frame, price);
    using Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->seek<Field>(
      Zdf::MaxOffset,
      {this, ZmFnPtr<&Test::run_read10<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdffptest", "data frame read10 failed");
      });
  }
  template <typename Ctrl>
  bool run_read10(Ctrl &rc, double v) {
    auto j = rc.reader.offset() - 1;
    if (ZuCmp<double>::null(v)) {
      df->run([this]() { run_live_write(); });
    } else {
      CHECK(ZuBoxed(v).feq(price(j)));
    }
    return true;
  }
  void run_live_write() {
    df->write({this, ZmFnPtr<&Test::run_live_write2>{}}, []{
      ZiLOG(Fatal, "zdffptest", "data frame live_write2 failed");
      done.post();
    });
  }
  void run_live_write2(ZmRef<DFWriter> w) {
    auto end = df->count();
    Frame frame;
    for (uint64_t i = 0; i < 10; i++) {
      auto j = i + end;
      frame.seqNo = j;
      frame.time = Zm::now();
      frame.price = price(j);
      w->write(frame);
    }
    df->stopWriting([]{ done.post(); });
    df->stopReading();
  }
};

Test test;

// command line options
struct Options {
  ZuCSpan	module;
  ZuCSpan	connect;
  bool		debug;
  bool		hashTel;
  bool		heapTel;
  bool		help;
};
ZfStruct(Options,
  (((module),    (Ctor<0>, CLI::Opt<'m'>)),  (String, getenv("ZDB_MODULE"))),
  (((connect),   (Ctor<1>, CLI::Opt<'c'>)),  (String, getenv("ZDB_CONNECT"))),
  (((debug),     (Ctor<2>, CLI::Flag<'d'>)), (Bool)),
  (((hashTel),   (Ctor<3>, CLI::Flag<'t'>)), (Bool)),
  (((heapTel),   (Ctor<4>, CLI::Flag<'T'>)), (Bool)),
  (((help),      (Ctor<5>, CLI::Flag<'h'>)), (Bool)));

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

  ZuPtr<const ZfCf::AnyNode> cf;

  try {
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
    cf = inlineCf(
      "zdb: {\n"
      "  thread: zdb,\n"
      "  hostID: 0,\n"
      "  hosts: {0: {standalone: true}},\n"
      "  store: {\n"
      "    thread: zdb_pq,\n"
      "    replicated: true,\n"
      "    module: ${MODULE},\n"
      "    connection: ${CONNECT}\n"
      "  },\n"
      "  tables: {},\n"
      "  debug: ${DEBUG}\n"
      "},\n"
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

    // "  module ../src/.libs/libZdbPQ.so\n"
    // "  connection \"dbname=test host=/tmp\"\n"

  } catch (const ZeException &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (const ZeError &e) {
    std::cerr << e << '\n' << std::flush;
    Zm::exit(1);
  } catch (...) {
    Zm::exit(1);
  }

  ZiLog::init("zdffptest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdffptest", "multiplexer start failed");

    db = new Zdb();

    ZdbCf dbCf{cf->resolve("zdb")};

    Zdf::Store::dbCf(cf, dbCf);

    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdffptest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "Zdb ACTIVE (was " << id << ')';
	}));
	done.post();
      },
      .downFn = [](Zdb *, bool) {
	ZiLOG(Info, "zdffptest", "Zdb INACTIVE");
      }
    });

    store = new Zdf::Store{};
    store->init(db);

    if (!db->start()) gtfo();
    done.wait(); // ensure active

    store->run(0, []() {
      store->open([](bool ok) {
	ZiLOG(Info, "zdffptest", ([ok](auto &s) {
	  s << "Zdf::Store open: " << (ok ? "OK" : "NOT OK");
	}));
	if (ok)
	  test.run();
	else
	  done.post();
      });
    });

    done.wait();

    if (options.hashTel)
      ZiLOG(Debug, "zdffptest", ([](auto &s) { s << '\n' << Ztc::hashCSV(); }));

    if (options.heapTel)
      ZiLOG(Debug, "zdffptest", ([](auto &s) { s << '\n' << Ztc::heapCSV(); }));

    db->stop(); // closes all tables

    mx->stop();

    // ZiLOG(Debug, "zdffptest", (ZeString{} << '\n' << Ztc::hashCSV()));
    // ZiLOG(Debug, "zdffptest", (ZeString{} << '\n' << Ztc::heapCSV()));

    db->final();
    db = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdffptest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdffptest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  ZuCHECK(passed.load(), "external dataframe lifecycle and queries");
  return 0;
}
