//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// server-side user DB bootstrap tool

#include <iostream>

#include <zlib/ZuBase32.hh>
#include <zlib/ZuBase64.hh>

#include <zlib/ZtCLI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvCf.hh>
#include <zlib/ZvMxParams.hh>

#include <zlib/ZtlsTOTP.hh>

#include <zlib/Zdb.hh>

#include <zlib/ZumServer.hh>

// command line options

struct Options {
  ZuCSpan		user;
  unsigned		passLen = 0;
  Zum::StringVec	perms;
  ZuCSpan		module;
  ZuCSpan		connect;
  ZuCSpan		log;
  bool			debug = false;
  bool			help = false;
};
ZtStruct(Options,
  (((user),    (Ctor<0>, CLI::Arg<1>)),    (String)),
  (((passLen), (Ctor<1>, CLI::Arg<2>, (Range<6, 60>))), (UInt8, 20)),
  (((perms),   (Ctor<2>, CLI::Args<3>)),   (StringVec)),
  (((module),  (Ctor<3>, CLI::Opt<'m'>)),  (String)),
  (((connect), (Ctor<4>, CLI::Opt<'c'>)),  (String)),
  (((log),     (Ctor<5>, CLI::Opt<'l'>)),  (String)),
  (((debug),   (Ctor<6>, CLI::Flag<'d'>)), (Bool)),
  (((help),    (Ctor<7>, CLI::Flag<'h'>)), (Bool)));

void usage()
{
  std::cerr <<
    "Usage: zuserdb USER PASSLEN [OPTION]... [PERM]...\n"
    "  Bootstrap user database with admin super-user USER,\n"
    "  generating a random initial password of PASSLEN characters,\n"
    "  optionally adding permissions PERM...\n\n"
    "Options:\n"
    "  -m, --module=MODULE\tZdb data store module e.g. libZdbPQ.so\n"
    "  -c, --connect=CONNECT\tZdb data store connection string\n"
    "\t\t\te.g. \"dbname=test host=/tmp\"\n"
    "  -l, --log=FILE\tlog to FILE\n"
    "  -d, --debug\t\tenable Zdb debugging\n"
    "  -h, --help\t\tthis help\n"
    << std::flush;
  exit(1);
}

ZmRef<ZvCf> inlineCf(ZuCSpan s)
{
  ZmRef<ZvCf> cf = new ZvCf{};
  cf->fromString(s);
  return cf;
}

int main(int argc_, char **argv)
{
  Options options;
  options.module = getenv("ZDB_MODULE");
  options.connect = getenv("ZDB_CONNECT");
  int argc = ZtCLI::load(options, argc_, argv);
  if (argc < 3) usage();
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
    "log \"&2\"\n"	// default - stderr
    "mx {\n"
    "  nThreads 5\n"
    "  threads {\n"
    "    1 { name rx isolated true }\n"
    "    2 { name tx isolated true }\n"
    "    3 { name zdb isolated true }\n"
    "    4 { name zdb_store isolated true }\n"
    "    5 { name app }\n"
    "  }\n"
    "  rxThread rx\n"
    "  txThread tx\n"
    "}\n"
    "userdb {\n"
    "  thread app\n"
    "}\n"
    "zdb {\n"
    "  thread zdb\n"
    "  hostID 0\n"
    "  hosts { 0 { standalone 1 } }\n"
    "  store {\n"
    "    module ${ZDB_MODULE}\n"
    "    connection ${ZDB_CONNECT}\n"
    "    thread zdb_store\n"
    "    replicated true\n"
    "  }\n"
    "  tables {\n"
    "    \"zum.user\" { }\n"
    "    \"zum.role\" { }\n"
    "    \"zum.key\" { }\n"
    "    \"zum.perm\" { }\n"
    "  }\n"
    "}\n");

  cf->set("zdb.store.module", options.module);
  cf->set("zdb.store.connection", options.connect);
  cf->set("zdb.debug", options.debug ? "1" : "0");
  cf->set("userDB.passLen", ZvCfString{} << options.passLen);

  ZiLog::init("zuserdb");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(cf->get<true>("log"))));
  ZiLog::start();

  ZiMultiplex *mx = nullptr;
  ZmRef<Zdb> db = new Zdb();

  auto gtfo = [&mx]() {
    if (mx) mx->stop();
    ZiLog::stop();
    Zm::exit(1);
  };

  Ztls::Random rng;

  rng.init();

  Zum::Server::UserDB userDB(&rng);

  try {
    mx = new ZiMultiplex{ZvMxParams{"mx", cf->getCf<true>("mx")}};

    db->init(ZdbCf(cf->getCf<true>("zdb")), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *) { },
      .downFn = [](Zdb *, bool) { }
    });

    userDB.init(cf->getCf<true>("userdb"), db);

    mx->start();
    if (!db->start()) throw ZeEXCEPT(Fatal, "zuserdb", "Zdb start failed");

  } catch (ZeException &e) {
    ZiLogEvent(ZuMv(e));
    gtfo();
  } catch (const ZeError &e) {
    ZiLOG(Fatal, "zuserdb", e.message());
    gtfo();
  } catch (...) {
    ZiLOG(Fatal, "zuserdb", "unknown exception");
    gtfo();
  }

  ZmBlock<>{}([&userDB, &gtfo, perms = ZuMv(options.perms)](auto wake) {
    userDB.open(ZuMv(perms), [
      wake = ZuMv(wake), &gtfo, &perms
    ](bool ok, ZtArray<unsigned> permIDs) mutable {
      if (!ok) {
	ZiLOG(Fatal, "zuserdb", "userDB open failed");
	gtfo();
      } else {
	for (unsigned i = 0, n = perms.length(); i < n; i++)
	  std::cout << permIDs[i] << ' ' << perms[i] << '\n';
      }
      wake();
    });
  });

  Zum::String passwd, secret;

  ZmBlock<>{}([
    user = ZuMv(options.user), &userDB, &gtfo, &passwd, &secret
  ](auto wake) {
    userDB.bootstrap(ZuMv(user), [
      &gtfo, &passwd, &secret, wake = ZuMv(wake)
    ](Zum::Server::BootstrapResult result) mutable {
      using Data = Zum::Server::BootstrapData;
      if (result.is<bool>()) {
	if (result.p<bool>()) {
	  std::cout << "userDB already initialized\n" << std::flush;
	} else {
	  std::cerr << "userDB bootstrap failed\n" << std::flush;
	  gtfo();
	}
      } else if (result.is<Data>()) {
	auto &data = result.p<Data>();
	passwd = ZuMv(data.passwd);
	secret = ZuMv(data.secret);
	std::cout
	  << "passwd: " << passwd
	  << "\nsecret: " << secret << '\n' << std::flush;
      }
      wake();
    });
  });

  db->stop();
  mx->stop();

  userDB.final();

  db->final();
  db = {};

  delete mx;

  ZiLog::stop();

  return 0;
}
