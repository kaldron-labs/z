//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuBase64.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZumDB.hh>
#include <zlib/ZumRequest.hh>

#include "ZumBootstrap.hh"
#include "ZumDaemon.hh"
#include "ZumUpstream.hh"

struct Options {
  Zum::String	module;
  Zum::String	connect;
  Zum::String	log{"&2"};
  Zum::String	issuer;
  Zum::String	admin;
  Zum::String	bootstrapOutput;
  Zum::String	addr{"127.0.0.1"};
  Zum::String	rpID{"localhost"};
  Zum::String	rpName{"Zum"};
  uint32_t	port = 8080;
  uint32_t	bootstrapTTL = 900;
  bool		debug = false;
  bool		bootstrapReissue = false;
  bool		once = false;
  bool		help = false;
};
ZfStruct(, (Options, CLI),
  (((module),	(CLI::Long<"module">)),		(String)),
  (((connect),	(CLI::Long<"connect">)),	(String)),
  (((log),	(CLI::Long<"log">)),		(String, "&2")),
  (((issuer),	(CLI::Long<"issuer">)),		(String)),
  (((admin),	(CLI::Long<"admin">)),		(String)),
  (((bootstrapOutput), (CLI::Long<"bootstrap-output">)), (String)),
  (((addr),	(CLI::Long<"addr">)),		(String, "127.0.0.1")),
  (((port),	(CLI::Long<"port">, (Range<1, 65535>))), (UInt32, 8080)),
  (((rpID),	(CLI::Long<"rp-id">)),		(String, "localhost")),
  (((rpName),	(CLI::Long<"rp-name">)),	(String, "Zum")),
  (((bootstrapTTL), (CLI::Long<"bootstrap-ttl">,
      (Range<1, 86400>))), (UInt32, 900)),
  (((debug),	(CLI::Flag<'d'>, CLI::Long<"debug">)), (Bool)),
  (((bootstrapReissue), (CLI::Long<"bootstrap-reissue">)), (Bool)),
  (((once),	(CLI::Flag<'o'>, CLI::Long<"once">)), (Bool)),
  (((help),	(CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static void usage(int code)
{
  std::cerr <<
    "Usage: zumd [OPTION]...\n\n"
    "  --module=PATH       Zdb store module (default: $ZDB_MODULE)\n"
    "  --connect=STRING    Zdb connection (default: $ZDB_CONNECT)\n"
    "  --log=FILE          log destination (default: stderr)\n"
    "  --issuer=URL        canonical issuer URL\n"
    "  --admin=LOGIN       initial local administrator login\n"
    "  --bootstrap-output=FILE  owner-only enrollment URL output\n"
    "  --bootstrap-ttl=N   enrollment capability seconds (default: 900)\n"
    "  --addr=IP           HTTP listen address (default: 127.0.0.1)\n"
    "  --port=N            HTTP listen port (default: 8080)\n"
    "  --rp-id=NAME        WebAuthn relying-party ID (default: localhost)\n"
    "  --rp-name=NAME      WebAuthn display name (default: Zum)\n"
    "  -d, --debug         enable Zdb debug logging\n"
    "  -o, --once          stop after database activation\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
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
  ZmSemaphore		active;
  ZmRef<Zum::Requests>	requests;
  bool			activated = false;
};

static ZmSemaphore done;

static void interrupted()
{
  done.post();
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto server = static_cast<DB *>(db);
  server->activated = true;
  server->requests->activate();
  server->active.post();
}

static void dbDown(Zdb *db, bool)
{
  auto server = static_cast<DB *>(db);
  server->requests->deactivate();
  if (!server->activated) server->active.post();
}

int main(int argc, char **argv)
{
  Options options;
  try {
    ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage(1);
  }
  if (options.help) usage(0);
  if (!options.module) options.module = ::getenv("ZDB_MODULE");
  if (!options.connect) options.connect = ::getenv("ZDB_CONNECT");
  ZuCSpan encodedDBKey = ::getenv("ZUM_DB_KEY");
  if (!options.module || !options.connect || !options.issuer ||
      !options.admin || !options.bootstrapOutput || !encodedDBKey) usage(1);
  if (!Zum::loginNormalize(options.admin)) {
    std::cerr << "zumd: invalid administrator login\n";
    return 1;
  }
  Zum::Bytes dbKey;
  dbKey.length(32, false);
  if (ZuBase64::decode(dbKey, encodedDBKey) != dbKey.length()) {
    ZuClear(dbKey.data(), dbKey.length());
    std::cerr << "zumd: ZUM_DB_KEY must be a base64-encoded 256-bit key\n";
    return 1;
  }

  ZiLog::init("zumd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.log)));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  int result = 1;
  try {
    auto cf = config(options);
    ZiMultiplex mx{ZvMxParams{"mx", cf->resolve("mx")}};
    ZmRef<DB> db = new DB{};
    db->requests = new Zum::Requests{};
    if (!db->requests->init(&mx, 5, 64))
      throw ZeEXCEPT(Fatal, "zumd", "request initialization failed");
    db->init(ZdbCf{cf->resolve("zdb")}, &mx,
      ZdbHandler{.upFn = dbUp, .downFn = dbDown});
    ZmRef<Zum::DBContext> context = Zum::registerSchema(db);
    bool mxStarted = false, dbStarted = false, stopped = false;
    auto stop = [&]() {
      if (stopped) return;
      stopped = true;
      if (db->activated) {
        ZmSemaphore requestsDown;
        db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
        requestsDown.wait();
        db->activated = false;
      }
      if (dbStarted) db->stop();
      if (mxStarted) mx.stop();
      db->final();
      context = nullptr;
    };
    try {
      mxStarted = mx.start();
      if (!mxStarted || !(dbStarted = db->start()))
        throw ZeEXCEPT(Fatal, "zumd", "Zdb start failed");
      db->active.wait();
      if (!db->activated)
        throw ZeEXCEPT(Fatal, "zumd", "Zdb activation failed");
      Ztls::Random rng;
      if (!rng.init())
        throw ZeEXCEPT(Fatal, "zumd", "random initialization failed");
      Zum::ServerBootstrapResult bootstrap;
      bool bootstrapOK = false;
      ZmSemaphore bootstrapDone;
      Zum::serverBootstrap(db, db->requests, context, rng,
          Zum::ServerBootstrapConfig{
            .issuer = options.issuer,
            .admin = options.admin,
            .output = options.bootstrapOutput,
            .dbKey = dbKey,
            .now = Zm::now().sec(),
            .ttl = options.bootstrapTTL,
            .reissue = options.bootstrapReissue},
          [&bootstrap, &bootstrapOK, &bootstrapDone](
              bool ok, Zum::ServerBootstrapResult result) mutable {
            bootstrapOK = ok;
            bootstrap = ZuMv(result);
            bootstrapDone.post();
          });
      bootstrapDone.wait();
      if (!bootstrapOK)
        throw ZeEXCEPT(Fatal, "zumd", "bootstrap failed");
      std::cout << "zumd: active" << std::endl;
      if (options.once) {
        ZuClear(dbKey.data(), dbKey.length());
      } else {
        Zum::UpstreamHTTP upstream;
        if (!upstream.init(&mx))
          throw ZeEXCEPT(Fatal, "zumd", "upstream HTTP initialization failed");
        Zum::Daemon daemon;
        bool daemonInited = daemon.init(db, context, db->requests, &mx,
            Zum::DaemonConfig{
              .addr = options.addr, .port = uint16_t(options.port),
              .issuer = options.issuer, .rpID = options.rpID,
              .rpName = options.rpName, .admin = options.admin,
              .dbKey = ZuMv(dbKey), .upstreamHTTP = upstream.fn(),
              .bootstrap = bootstrap});
        if (!daemonInited || !daemon.start()) {
          if (daemonInited) daemon.final();
          upstream.final();
          throw ZeEXCEPT(Fatal, "zumd", "HTTP server start failed");
        }
        std::cout << "zumd: listening" << std::endl;
        done.wait();
        daemon.stop();
        daemon.final();
        upstream.final();
      }
      stop();
    } catch (...) {
      stop();
      throw;
    }
    result = 0;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
  } catch (const ZeError &e) {
    std::cerr << e.message() << '\n';
  }
  if (dbKey && dbKey.mutable_()) ZuClear(dbKey.data(), dbKey.length());
  ZiLog::stop();
  return result;
}
