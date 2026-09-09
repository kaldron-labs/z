//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// offline first-user enrollment capability issuer

#include <iostream>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZumPasskey.hh>

#include <zlib/ZtlsRandom.hh>

struct Options {
  Zum::String	issuer;
  Zum::IDVec	roles;
  Zum::String	module;
  Zum::String	connect;
  Zum::String	log;
  Zum::String	actor;
  Zum::UserID	recover = 0;
  uint32_t	ttl = 900;
  bool		debug = false;
  bool		help = false;
};
ZfStruct(, (Options, CLI),
  (((issuer),	(CLI::Arg<1>)),			(String)),
  (((roles),	(CLI::Args<2>)),			(UInt64Vec)),
  (((module),	(CLI::Opt<'m'>)),			(String)),
  (((connect),	(CLI::Opt<'c'>)),			(String)),
  (((log),	(CLI::Opt<'l'>)),			(String, "&2")),
  (((actor),	(CLI::Opt<'a'>)),			(String)),
  (((recover),	(CLI::Opt<'r'>)),			(UInt64)),
  (((ttl),	(CLI::Opt<'t'>, (Range<1, 86400>))),	(UInt32, 900)),
  (((debug),	(CLI::Flag<'d'>)),			(Bool)),
  (((help),	(CLI::Flag<'h'>)),			(Bool)));

static void usage()
{
  std::cerr <<
    "Usage: zum ISSUER ROLE_ID... [OPTION]...\n"
    "       zum ISSUER -r USER_ID -a ACTOR [OPTION]...\n\n"
    "Issue a one-shot browser enrollment or account-recovery capability.\n"
    "Run with all service writers quiesced. For first-user enrollment,\n"
    "ROLE_IDs must name the ordinary administrative roles already seeded\n"
    "for ISSUER. Recovery suspends USER_ID and invalidates its credentials\n"
    "and refresh families before issuing the capability.\n\n"
    "Options:\n"
    "  -m, --module=MODULE\tZdb store module (default: $ZDB_MODULE)\n"
    "  -c, --connect=CONNECT\tZdb connection (default: $ZDB_CONNECT)\n"
    "  -r, --recover=USER_ID\trecover an existing user\n"
    "  -a, --actor=ACTOR\trecovery audit actor\n"
    "  -t, --ttl=SECONDS\tcapability lifetime (default: 900)\n"
    "  -l, --log=FILE\tlog destination (default: stderr)\n"
    "  -d, --debug\t\tenable Zdb debug logging\n"
    "  -h, --help\t\tthis help\n" << std::flush;
}

static ZuPtr<const ZfCf::AnyNode> config(const Options &options)
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"}, ZfCf::DefVal{options.module});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{options.connect});
  defines->add(ZfCf::DefKey{"DEBUG"},
    ZfCf::DefVal{options.debug ? "true" : "false"});
  auto scan = ZfCf::scan(
    "zdb: {\n"
    "  thread: zdb, shards: 1, threads: [shard],\n"
    "  store: {thread: store, module: ${MODULE},\n"
    "    connection: ${CONNECT}},\n"
    "  hostID: self, hosts: {self: {standalone: true}},\n"
    "  tables: {}, debug: ${DEBUG}\n"
    "},\n"
    "mx: {\n"
    "  nThreads: 5, rxThread: rx, txThread: tx, threads: {\n"
    "    1: {name: rx, isolated: true},\n"
    "    2: {name: tx, isolated: true},\n"
    "    3: {name: zdb, isolated: true},\n"
    "    4: {name: store, isolated: true},\n"
    "    5: {name: shard, isolated: true}\n"
    "  }\n"
    "}\n", {}, ZuMv(defines));
  return ZuMv(scan.p<1>());
}

struct DB : public Zum::DB {
  ZmSemaphore active;
  ZmRef<Zum::Requests> requests;
};

static void dbUp(Zdb *db, ZdbHost *)
{
  auto app = static_cast<DB *>(db);
  app->requests->activate();
  app->active.post();
}

static void dbDown(Zdb *db, bool)
{
  static_cast<DB *>(db)->requests->deactivate();
}

int main(int argc, char **argv)
{
  Options options;
  options.module = getenv("ZDB_MODULE");
  options.connect = getenv("ZDB_CONNECT");
  try {
    ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
    return 1;
  }
  if (options.help) {
    usage();
    return 0;
  }
  if (!options.issuer || !options.module || !options.connect ||
      (options.recover ? (!options.actor || options.roles) : !options.roles)) {
    usage();
    return 1;
  }

  ZiLog::init("zum");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.log)));
  ZiLog::start();

  int result = 1;
  try {
    auto cf = config(options);
    ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
    ZmRef<DB> db = new DB{};
    db->requests = new Zum::Requests{};
    if (!db->requests->init(&mx, 5, 1))
      throw ZeEXCEPT(Fatal, "zum", "request initialization failed");
    db->init(ZdbCf{cf->resolve("zdb")}, &mx,
      ZdbHandler{.upFn = dbUp, .downFn = dbDown});
    ZmRef<Zum::DBContext> context = Zum::registerSchema(db);
    if (!mx.start() || !db->start())
      throw ZeEXCEPT(Fatal, "zum", "Zdb start failed");
    db->active.wait();

    Ztls::Random rng;
    if (!rng.init())
      throw ZeEXCEPT(Fatal, "zum", "random initialization failed");
    Zum::String capability;
    int64_t now = Zm::now().sec();
    bool ok;
    if (options.recover) {
      ok = ZmBlock<bool>{}([db, context, &rng, &options,
          now, &capability](auto wake) mutable {
	if (!Zum::recoveryIssue(db->requests, Zm::now() + ZuTime{30},
	  db, context, rng, Zum::RecoveryIssueConfig{
	  .issuer = ZuMv(options.issuer),
	  .actor = ZuMv(options.actor),
	  .userID = options.recover,
	  .now = now,
	  .expires = now + options.ttl
	}, [&capability, wake](
	    bool ok, Zum::String value) mutable {
	  capability = ZuMv(value);
	  wake(ok);
	})) wake(false);
      });
    } else {
      ok = ZmBlock<bool>{}([context, db, &rng, &options,
          now, &capability](auto wake) mutable {
	if (!Zum::bootstrapIssue(db->requests, Zm::now() + ZuTime{30},
	  context, rng, Zum::BootstrapConfig{
	  .issuer = ZuMv(options.issuer),
	  .roleIDs = ZuMv(options.roles),
	  .now = now,
	  .expires = now + options.ttl
	}, [&capability, wake](
	    bool ok, Zum::String value) mutable {
	  capability = ZuMv(value);
	  wake(ok);
	})) wake(false);
      });
    }
    if (ok) {
      std::cout << capability << '\n' << std::flush;
      ZuClear(capability.data(), capability.length());
      result = 0;
    } else {
      std::cerr << (options.recover ?
	"recovery capability unavailable\n" :
	"bootstrap capability already issued or unavailable\n");
    }
    ZmSemaphore requestsDown;
    db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
    requestsDown.wait();
    db->stop();
    mx.stop();
    db->final();
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
  } catch (const ZeError &e) {
    std::cerr << e.message() << '\n';
  }
  ZiLog::stop();
  return result;
}
