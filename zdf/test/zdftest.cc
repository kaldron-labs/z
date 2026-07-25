//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZtcHash.hh>
#include <zlib/ZtcHeap.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/ZfbStruct.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSeries.hh>
#include <zlib/ZdfStore.hh>

void print(const char *msg) {
  std::cout << msg << '\n';
}
void print(const char *msg, double i) {
  std::cout << msg << ' ' << ZuBoxed(i) << '\n';
}
void ok(const char *msg) { print(msg); }
void ok(const char *msg, double i) { print(msg, i); }
void fail(const char *msg) { print(msg); }
void fail(const char *msg, double i) { print(msg, i); }
#define CHECK(x) ((x) ? ok("OK  " #x) : fail("NOK " #x))
#define CHECK2(x, y) ((x == y) ? ok("OK  " #x, x) : fail("NOK " #x, x))

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

struct Frame {
  uint64_t	v1;
  int64_t	v2_;

  ZuFixed v2() const { return ZuFixed{v2_, 9}; }
  void v2(ZuFixed v) { v2_ = v.adjust(9); }
};
ZfStruct(Frame,
  (((v1),	(Ctor<0>, Series, Index, Delta)),	(UInt64)),
  (((v2, Fn),	(Series, Delta, NDP<9>)),		(Fixed)));

void usage() {
  std::cerr << "Usage: zdftest\n" << std::flush;
  ::exit(1);
}

using DF = Zdf::DataFrame<Frame, false>;
using DFWriter = DF::Writer;

struct Test {
  ZmRef<DF>	df;

  void run() {
    store->openDF<Frame, false, true>(
      0, "frame", {this, ZmFnPtr<&Test::run_opened>{}});
  }
  void run_opened(ZmRef<DF> df_) {
    if (!df_) {
      ZiLOG(Fatal, "zdftest", "data frame open failed");
      done.post();
      return;
    }
    df = ZuMv(df_);
    df->write({this, ZmFnPtr<&Test::run_write>{}}, []{
      ZiLOG(Fatal, "zdftest", "data frame write failed");
      done.post();
    });
  }
  void run_write(ZmRef<DFWriter> w) {
    Frame frame;
    for (uint64_t i = 0; i < 300; i++) { // 1000
      frame.v1 = i;
      frame.v2_ = i * 42;
      w->write(frame);
    }
    df->run([this]() { run_read1(); });
  }
  void run_read1() {
    using Field = ZfField(Frame, v1);
    using Ctrl = Zdf::FieldRdrCtrl<Field>;
    df->find<Field>(
      ZuFixed{20, 0}, {this, ZmFnPtr<&Test::run_read2<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdftest", "data frame read1 failed");
	done.post();
      });
  }
  template <typename Ctrl>
  bool run_read2(Ctrl &rc, ZuFixed) {
    df->seek<ZfField(Frame, v2)>(
      rc.stop(), {this, ZmFnPtr<&Test::run_read3<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdftest", "data frame read2 failed");
	done.post();
      });
    return false;
  }
  template <typename Ctrl>
  bool run_read3(Ctrl &rc, ZuFixed v) {
    ZiLOG(Debug, "zdftest", ([v](auto &s) { s << "v=" << v; }));
    CHECK(v.mantissa == 20 * 42);
    CHECK(v.ndp == 9);
    rc.fn({this, ZmFnPtr<&Test::run_read4<Ctrl>>{}});
    rc.findFwd(ZuFixed{200 * 42, 9});
    return false;
  }
  template <typename Ctrl>
  bool run_read4(Ctrl &rc, ZuFixed v) {
    ZiLOG(Debug, "zdftest", ([v](auto &s) { s << "v=" << v; }));
    CHECK(v.mantissa == 200 * 42);
    CHECK(v.ndp == 9);
    df->seek<ZfField(Frame, v1)>(
      rc.stop(), {this, ZmFnPtr<&Test::run_read5<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdftest", "data frame read4 failed");
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
    df->seek<ZfField(Frame, v2)>(
      rc.stop(), {this, ZmFnPtr<&Test::run_read7<Ctrl>>{}}, []{
	ZiLOG(Fatal, "zdftest", "data frame read6 failed");
	done.post();
      });
    return false;
  }
  template <typename Ctrl>
  bool run_read7(Ctrl &rc, ZuFixed v) {
    ZiLOG(Debug, "zdftest", ([v](auto &s) { s << "v=" << v; }));
    CHECK(v.mantissa == 100 * 42);
    CHECK(v.ndp == 9);
    rc.stop();
    done.post();
    return false;
  }
};

#if 0
    reader.seekRev(index.offset());
    AnyReader cleaner;
    {
      auto offset = reader.offset();
      offset = offset < 100 ? 0 : offset - 100;
      df.seek(cleaner, 1, offset);
    }
    Zdf::StatsTree<> w;
    while (reader.read(v)) {
      w.add(v);
      if (cleaner.read(v)) w.del(v);
      std::cout << "min=" << ZuBoxed(w.minimum()) <<
	" max=" << ZuBoxed(w.maximum()) <<
	" mean=" << ZuBoxed(w.mean()) <<
	" stddev=" << ZuBoxed(w.std()) <<
	" median=" << ZuBoxed(w.median()) <<
	" 95%=" << ZuBoxed(w.rank(0.95)) << '\n';
    }
    // for (auto k = w.begin(); k != w.end(); ++k) std::cout << *k << '\n';
    // for (auto k: w) std::cout << k.first << '\n';
    // std::cout << "stddev=" << w.std() << '\n';
#endif

Test test;

int main(int argc, char **argv)
{
  ZuPtr<const ZfCf::AnyNode> cf;

  try {
    cf = inlineCf(
      "zdb: {\n"
      "  thread: zdb,\n"
      "  store: {thread: zdb_mem},\n"
      "  hostID: 0,\n"
      "  hosts: {0: {standalone: true}},\n"
      "  tables: {},\n"
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

  ZiLog::init("zdftest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdftest", "multiplexer start failed");

    db = new Zdb();

    ZdbCf dbCf{cf->resolve("zdb")};

    Zdf::Store::dbCf(cf, dbCf);

    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdftest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
	done.post();
      },
      .downFn = [](Zdb *, bool) {
	ZiLOG(Info, "zdftest", "INACTIVE");
      }
    }, new ZdbMem::Store());

    store = new Zdf::Store{};
    store->init(db);

    db->start();
    done.wait(); // ensure active

    store->run(0, []() {
      store->open([](bool ok) {
	std::cout << "open(): " << (ok ? "OK" : "NOT OK") << '\n';
	if (ok)
	  test.run();
	else
	  done.post();
      });
    });

    done.wait();

    db->stop(); // closes all tables

    db->final();

    mx->stop();

    // ZiLOG(Debug, "zdftest", (ZeString{} << '\n' << Ztc::hashCSV()));
    // ZiLOG(Debug, "zdftest", (ZeString{} << '\n' << Ztc::heapCSV()));

    db = {};
    store = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdftest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdftest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  return 0;
}
