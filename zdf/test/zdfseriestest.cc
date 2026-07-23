//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuLib.hh>

#include <zlib/ZmTrap.hh>

#include <zlib/ZfCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/ZdfCompress.hh>
#include <zlib/ZdfSeries.hh>
#include <zlib/ZdfStore.hh>

void print(const char *s) {
  std::cout << s << '\n' << std::flush;
}
void print(const char *s, int64_t i) {
  std::cout << s << ' ' << i << '\n' << std::flush;
}
void ok(const char *s) { print(s); }
void ok(const char *s, int64_t i) { print(s, i); }
void fail(const char *s) { print(s); }
void fail(const char *s, int64_t i) { print(s, i); }
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

using Series = Zdf::Series<Zdf::Decoder>;

struct Test {
  ZmRef<Series>	series;

  void run() {
    store->openSeries<Zdf::Decoder, true>(0, "test",
      [this](ZmRef<Series> series_) {
	series = ZuMv(series_);
	run_opened();
      });
  }
  void run_opened() {
    if (!series) {
      ZiLOG(Fatal, "zdfseriestest", "open failed");
      gtfo();
      return;
    }
    series->write([this](auto w) {
      run_write(ZuMv(w));
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "write1 failed");
      gtfo();
    }, 0);
  }
  void run_write(ZmRef<Series::Writer> w) {
    CHECK(w->write(42));
    CHECK(w->write(42));
    w->stop();
    series->write([this](auto w) {
      run_write2(ZuMv(w));
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "write2 failed");
      gtfo();
    }, 2);
  }
  void run_write2(ZmRef<Series::Writer> w) {
    CHECK(w->write(4301));
    CHECK(w->write(4302));
    w->stop();
    series->write([this](auto w) {
      run_write3(ZuMv(w));
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "write3 failed");
      gtfo();
    }, 3);
  }
  void run_write3(ZmRef<Series::Writer> w) {
    CHECK(w->write(43030));
    CHECK(w->write(43040));
    w->stop();
    series->write([this](auto w) mutable {
      run_write4(ZuMv(w));
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "write4 failed");
      gtfo();
    }, 4);
  }
  void run_write4(ZmRef<Series::Writer> w) {
    CHECK(w->write(430500));
    CHECK(w->write(430600));
    for (unsigned i = 0; i < 300; i++) {
      w->write(430700);
      // CHECK(w->write(430700));
    }
    CHECK(w->series()->blkCount() == 4);
    w->stop();
    run_read();
  }
  void run_read() {
    series->seek(0, [this, i = 0](auto &rc, ZuFixed v) mutable {
      switch (i++) {
	case 0:
	  CHECK(v.mantissa == 42 && !v.ndp);
	  break;
	case 1:
	  CHECK(v.mantissa == 42 && !v.ndp);
	  break;
	case 2:
	  CHECK(v.mantissa == 4301 && v.ndp == 2);
	  break;
	case 3:
	  CHECK(v.mantissa == 4302 && v.ndp == 2);
	  break;
	case 4:
	  CHECK(v.mantissa == 43030 && v.ndp == 3);
	  break;
	case 5:
	  CHECK(v.mantissa == 43040 && v.ndp == 3);
	  break;
	case 6:
	  CHECK(v.mantissa == 430500 && v.ndp == 4);
	  break;
	case 7:
	  CHECK(v.mantissa == 430600 && v.ndp == 4);
	  break;
	default:
	  CHECK(v.mantissa == 430700 && v.ndp == 4);
	  break;
      }
      if (i >= 308) {
	rc.stop([this]() { run_read2(); });
	return false;
      }
      return true;
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "read failed");
      gtfo();
    });
  }
  void run_read2() {
    series->find(ZuFixed{425, 1}, [this](auto &rc, ZuFixed v) {
      CHECK(v.mantissa == 4301 && v.ndp == 2);
      rc.stop([this]() { run_read3(); });
      return false;
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "read2 failed");
      gtfo();
    });
  }
  void run_read3() {
    series->find(ZuFixed{43020, 3}, [this](auto &rc, ZuFixed v) {
      CHECK(v.mantissa == 4302 && v.ndp == 2);
      rc.purge();
      rc.stop([this]() { run_read4(); });
      return false;
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "read3 failed");
      gtfo();
    });
  }
  void run_read4() {
    series->find(ZuFixed{44, 0}, [this](auto &rc, ZuFixed v) {
      CHECK(!*v);
      rc.stop([this]() { run_read5(); });
      return false;
    }, []() {
      ZiLOG(Fatal, "zdfseriestest", "read4 failed");
      gtfo();
    });
  }
  void run_read5() {
    CHECK(series->blkCount() == 4);
    done.post();
  }
};
// FIXME
#if 0
    {
      auto r = series->find<DeltaDecoder<>>(ZuFixed{44, 0});
      ZuFixed v;
      CHECK(!r);
      CHECK(!r.read(v));
    }
    {
      auto r = series->seek<DeltaDecoder<>>();
      ZuFixed v;
      CHECK(r.read(v)); CHECK(v.mantissa == 4301 && v.ndp == 2);
    }
    {
      auto r = series->seek<DeltaDecoder<>>(208);
      ZuFixed v;
      for (unsigned i = 0; i < 50; i++) {
	CHECK(r.read(v));
	CHECK(v.mantissa == 430700 && v.ndp == 4);
      }
      CHECK(r.offset() == 258);
      for (unsigned i = 0; i < 50; i++) {
	CHECK(r.read(v));
	CHECK(v.mantissa == 430700 && v.ndp == 4);
      }
      CHECK(!r.read(v));
    }
#endif

Test test;

int main()
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

  ZiLog::init("zdfstoretest");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2"))); // log to stderr
  ZiLog::start();

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    if (!mx->start()) throw ZeEXCEPT(Fatal, "zdfseriestest", "multiplexer start failed");

    db = new Zdb();

    ZdbCf dbCf{cf->resolve("zdb")};

    Zdf::Store::dbCf(cf, dbCf);

    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *host) {
	ZiLOG(Info, "zdfseriestest", ([id = host ? ZuID{host->id()} : ZuID{"unset"}](auto &s) {
	  s << "ACTIVE (was " << id << ')';
	}));
	done.post();
      },
      .downFn = [](Zdb *, bool) {
	ZiLOG(Info, "zdfseriestest", "INACTIVE");
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

    ZiLOG(Debug, "zdfseriestest", (ZeString{} << '\n' << ZmHashMgr::csv()));

    db = {};
    store = {};

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zdfseriestest", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zdfseriestest", "unknown exception");
    gtfo();
  }

  mx = {};

  ZiLog::stop();

  return 0;
}
