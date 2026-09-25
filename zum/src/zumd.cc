//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/zumd_db.hh>
#include <zlib/zumd_request.hh>

#include "zumd_bootstrap.hh"
#include "zumd_daemon.hh"
#include "zumd_oidc.hh"
#include "zumd_rekey.hh"

namespace Zum {

namespace DaemonCapacity {
  enum { RequestAdmission = 64, BootstrapAdmission = 1 };
}

ZrestCatalogImpl(PublicCatalog)
ZrestCatalogImpl(HealthCatalog)
ZrestCatalogImpl(BootstrapCatalog)
ZrestCatalogImpl(EnrollCatalog)
ZrestCatalogImpl(WellKnownCatalog)
ZrestCatalogImpl(AdminCatalog)
ZrestCatalogImpl(DaemonCatalog)
ZrestRootCatalogImpl(DaemonRootCatalog)

} // Zum

struct Options {
  Zum::String	config;
  Zum::String	module;
  Zum::String	connect;
  Zum::String	log{"&2"};
  Zum::String	issuer;
  Zum::String	ssfIssuer;
  Zum::String	admin;
  Zum::String	bootstrapOutput;
  Zum::String	addr{"127.0.0.1"};
  Zum::String	rpID{"localhost"};
  Zum::String	rpName{"Zum"};
  uint32_t	port = 8080;
  uint32_t	bootstrapTTL = 900;
  uint32_t	cleanupInterval = 300;
  uint32_t	oidcOrigins = Zum::OIDCHTTP::DefaultOrigins;
  bool		debug = false;
  bool		bootstrapReissue = false;
  bool		once = false;
  bool		rekey = false;
  bool		help = false;
};
ZfStruct(, (Options, CLI),
  (((config),	(CLI::Long<"config">)),		(String)),
  (((module),	(CLI::Long<"module">)),		(String)),
  (((connect),	(CLI::Long<"connect">)),	(String)),
  (((log),	(CLI::Long<"log">)),		(String, "&2")),
  (((issuer),	(CLI::Long<"issuer">)),		(String)),
  (((ssfIssuer),	(CLI::Long<"ssf-issuer">)),	(String)),
  (((admin),	(CLI::Long<"admin">)),		(String)),
  (((bootstrapOutput), (CLI::Long<"bootstrap-output">)), (String)),
  (((addr),	(CLI::Long<"addr">)),		(String, "127.0.0.1")),
  (((port),	(CLI::Long<"port">, (Range<1, 65535>))), (UInt32, 8080)),
  (((rpID),	(CLI::Long<"rp-id">)),		(String, "localhost")),
  (((rpName),	(CLI::Long<"rp-name">)),	(String, "Zum")),
  (((bootstrapTTL), (CLI::Long<"bootstrap-ttl">,
      (Range<1, 86400>))), (UInt32, 900)),
  (((cleanupInterval), (CLI::Long<"cleanup-interval">,
      (Range<0, 86400>))), (UInt32, 300)),
  (((oidcOrigins), (CLI::Long<"oidc-origins">)),
      (UInt32, Zum::OIDCHTTP::DefaultOrigins)),
  (((debug),	(CLI::Flag<'d'>, CLI::Long<"debug">)), (Bool)),
  (((bootstrapReissue), (CLI::Long<"bootstrap-reissue">)), (Bool)),
  (((once),	(CLI::Flag<'o'>, CLI::Long<"once">)), (Bool)),
  (((rekey),	(CLI::Long<"rekey">)), (Bool)),
  (((help),	(CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static void usage(int code)
{
  std::cerr <<
    "Usage: zumd [OPTION]...\n\n"
    "  --config=FILE       node configuration containing native zdb/mx sections\n"
    "  --module=PATH       Zdb store module (default: $ZDB_MODULE)\n"
    "  --connect=STRING    Zdb connection (default: $ZDB_CONNECT)\n"
    "  --log=FILE          log destination (default: stderr)\n"
    "  --issuer=URL        public authorization base URL\n"
    "  --ssf-issuer=URL    application-scoped issuer used in SSF SETs\n"
    "  --admin=LOGIN       initial local administrator login\n"
    "  --bootstrap-output=FILE  owner-only enrollment URL output\n"
    "  --bootstrap-ttl=N   enrollment capability seconds (default: 900)\n"
    "  --cleanup-interval=N  cleanup period seconds, 0 disables (default: 300)\n"
    "  --oidc-origins=N    maximum cached OIDC origins (default: 32)\n"
    "  --addr=IP           HTTP listen address (default: 127.0.0.1)\n"
    "  --port=N            HTTP listen port (default: 8080)\n"
    "  --rp-id=NAME        WebAuthn relying-party ID (default: localhost)\n"
    "  --rp-name=NAME      WebAuthn display name (default: Zum)\n"
    "  -d, --debug         enable Zdb debug logging\n"
    "  -o, --once          stop after database activation\n"
    "  --rekey             offline DB-secret rotation; stop all writers first\n"
    "                      old key: ZUM_DB_KEY; new key: ZUM_DB_NEW_KEY\n"
    "  -h, --help          show help\n" << std::flush;
  ::exit(code);
}

struct OIDCConfig {
  Zum::String caPath;
};
ZfStruct(, (OIDCConfig, Cf),
  (((caPath)), (String)));

struct SSFReceiverCf {
  Zum::String receiverID;
  Zum::AppID appID = 0;
  Zum::String audience;
  Zum::String deliveryURL;
  // This is an environment-variable name, not the callback credential.
  Zum::String secretRef;
  uint64_t revision = 0;
};
ZfStruct(, (SSFReceiverCf, Cf),
  (((receiverID), (Required)), (String)),
  (((appID), (Required)), (UInt64)),
  (((audience), (Required)), (String)),
  (((deliveryURL), (Required)), (String)),
  (((secretRef), (Required)), (String)),
  (((revision), (Required)), (UInt64)));

struct SSFReceiverVecCf : public ZtArray<SSFReceiverCf> {
  using Base = ZtArray<SSFReceiverCf>;
  using Base::Base;
  using Base::operator =;
  friend ZfCf::AsArray<ZfFieldTC::UDT> ZfCf_Fmt(SSFReceiverVecCf *);
};

struct SSFCf {
  Zum::String issuer;
  SSFReceiverVecCf receivers;
};
ZfStruct(, (SSFCf, Cf),
  (((issuer)), (String)),
  (((receivers)), (UDT)));

static ZuPtr<const ZfCf::AnyNode> config(
    const Options &options, Zum::String &source)
{
  ZmRef<ZfCf::Defines> defines = new ZfCf::Defines{};
  defines->add(ZfCf::DefKey{"MODULE"}, ZfCf::DefVal{options.module});
  defines->add(ZfCf::DefKey{"CONNECT"}, ZfCf::DefVal{options.connect});
  defines->add(ZfCf::DefKey{"DEBUG"},
    ZfCf::DefVal{options.debug ? "true" : "false"});
  if (options.config) {
    ZiFile file;
    if (file.open(Zi::Path{options.config}, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      return {};
    auto size = file.size();
    if (size <= 0 || uint64_t(size) > INT_MAX) return {};
    source.length(unsigned(size));
    if (file.read(source.data(), unsigned(size)) != int(size)) return {};
    auto scan = ZfCf::scan(source.span(), {}, ZuMv(defines));
    if (scan.p<0>() < 0) return {};
    return ZuMv(scan.p<1>());
  }
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
  ZmRef<Zum::Requests>	requests;
  ZmRef<Zum::Requests>	bootstrapRequests;
  // Zdb-thread owned; used only to reject stale preparation completion.
  uint64_t		generation = 0;
  bool			failed = false;
};

static ZmSemaphore done;
static ZmAtomic<unsigned> stopping = 0;

static void interrupted()
{
  stopping = 1;
  done.post();
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto server = static_cast<DB *>(db);
  ++server->generation;
  done.post();
}

static void dbDown(Zdb *db, bool failed)
{
  auto server = static_cast<DB *>(db);
  server->requests->deactivate();
  server->bootstrapRequests->deactivate();
  server->failed |= failed;
  done.post();
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
  if (!options.module)
    if (auto path = Zt::getpath("ZDB_MODULE")) options.module = path;
  if (!options.connect) options.connect = ::getenv("ZDB_CONNECT");
  ZuCSpan encodedDBKey = ::getenv("ZUM_DB_KEY");
  if (!encodedDBKey) usage(1);
  if ((!options.config && (!options.module || !options.connect)) ||
      !options.issuer || (!options.rekey &&
        (!options.admin || !options.bootstrapOutput)) ||
      (options.rekey && (options.once || options.bootstrapReissue))) {
    usage(1);
  }
  if (!options.rekey && !Zum::loginNormalize(options.admin)) {
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
  Zum::Bytes newDBKey;
  if (options.rekey) {
    ZuCSpan encodedNewKey = ::getenv("ZUM_DB_NEW_KEY");
    newDBKey.length(32, false);
    if (!encodedNewKey || ZuBase64::decode(newDBKey, encodedNewKey) != 32) {
      ZuClear(dbKey.data(), dbKey.length());
      ZuClear(newDBKey.data(), newDBKey.length());
      std::cerr << "zumd: ZUM_DB_NEW_KEY must be a base64-encoded 256-bit key\n";
      return 1;
    }
  }

  ZiLog::init("zumd");
  ZiLog::level(0);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.log)));
  ZiLog::start();
  ZmTrap::sigintFn(interrupted);
  ZmTrap::trap();

  int result = 1;
  try {
    Zum::String configSource;
    auto cf = config(options, configSource);
    if (!cf || !cf->resolve("mx") || !cf->resolve("zdb"))
      throw ZeEXCEPT(Fatal, "zumd", "invalid node configuration");
    SSFCf ssfConfig;
    if (auto node = cf->resolve("ssf"))
      ssfConfig = ZfCf::handler<SSFCf>(node).ctor();
    Zum::String ssfIssuer = options.ssfIssuer;
    if (!ssfIssuer) ssfIssuer = ssfConfig.issuer;
    if (!ssfIssuer) ssfIssuer = options.issuer;
    Zum::SSFReceiverVec ssfReceivers;
    for (const auto &receiver: ssfConfig.receivers) {
      if (!receiver.receiverID || !receiver.appID || !receiver.audience ||
          !receiver.deliveryURL || !receiver.secretRef || !receiver.revision)
        throw ZeEXCEPT(Fatal, "zumd", "invalid SSF receiver configuration");
      ssfReceivers.push(Zum::SSFRx{
        .receiverID = receiver.receiverID, .appID = receiver.appID,
        .audience = receiver.audience, .deliveryURL = receiver.deliveryURL,
        .secretRef = receiver.secretRef, .revision = receiver.revision});
    }
    Zum::SSFSecretFn ssfSecret;
    if (ssfReceivers) {
      ssfSecret = Zum::SSFSecretFn{[](Zum::String secretRef) {
        auto value = ::getenv(secretRef);
        return value ? Zum::String{value} : Zum::String{};
      }};
    }
    ZiMultiplex mx{ZvMxParams("mx", cf->resolve("mx"))};
    ZmRef<DB> db = new DB{};
    db->requests = new Zum::Requests{};
    db->bootstrapRequests = new Zum::Requests{};
    if (!db->requests->init(&mx, mx.sid("shard"),
          Zum::DaemonCapacity::RequestAdmission) ||
	!db->bootstrapRequests->init(&mx, mx.sid("shard"),
          Zum::DaemonCapacity::BootstrapAdmission))
      throw ZeEXCEPT(Fatal, "zumd", "request initialization failed");
    db->init(ZdbCf{cf->resolve("zdb")}, &mx,
      ZdbHandler{.upFn = dbUp, .downFn = dbDown});
    ZmRef<Zum::DBContext> context = Zum::registerSchema(db);
    Zum::OIDCHTTP oidcHTTP;
    Zum::Daemon daemon;
    bool oidcHTTPInited = false, daemonInited = false;
    bool mxStarted = false, dbStarted = false, stopped = false;
    bool storeStopped = true;
    auto stopDB = [&dbStarted, &storeStopped, db]() {
      if (!dbStarted) return;
      storeStopped = db->stop();
      dbStarted = false;
    };
    auto stop = [
      &stopped, &mxStarted, db, &daemon, &oidcHTTPInited, &oidcHTTP,
      &stopDB, &mx, &context
    ]() {
      if (stopped) return;
      stopped = true;
      if (mxStarted) {
        ZmSemaphore requestsDown;
        db->requests->deactivate([&requestsDown]() { requestsDown.post(); });
        requestsDown.wait();
        db->bootstrapRequests->deactivate([&requestsDown]() { requestsDown.post(); });
        requestsDown.wait();
      }
      daemon.stop();
      if (oidcHTTPInited) oidcHTTP.final();
      stopDB();
      daemon.final();
      if (mxStarted) mx.stop();
      db->final();
      context = nullptr;
    };
    try {
      mxStarted = mx.start();
      if (!mxStarted || !(dbStarted = db->start()))
        throw ZeEXCEPT(Fatal, "zumd", "Zdb start failed");
      Ztls::Random rng;
      if (!rng.init())
        throw ZeEXCEPT(Fatal, "zumd", "random initialization failed");
      if (!options.once && !options.rekey) {
        OIDCConfig oidcConfig;
        if (auto node = cf->resolve("oidc"))
          oidcConfig = ZfCf::handler<OIDCConfig>(node).ctor();
        oidcHTTPInited = oidcHTTP.init(&mx, db->requests->sid(),
          options.oidcOrigins, oidcConfig.caPath);
        if (!oidcHTTPInited)
          throw ZeEXCEPT(Fatal, "zumd", "OIDC HTTP initialization failed");
        daemonInited = daemon.init(db, context, db->requests, &mx,
            Zum::DaemonConfig{
              .addr = options.addr, .port = uint16_t(options.port),
              .issuer = options.issuer, .ssfIssuer = ZuMv(ssfIssuer),
              .rpID = options.rpID,
              .rpName = options.rpName, .admin = options.admin,
              .dbKey = dbKey, .oidcHTTP = oidcHTTP.fn(),
              .ssfSecret = ZuMv(ssfSecret),
              .ssfReceivers = ZuMv(ssfReceivers),
              .cleanupInterval = options.cleanupInterval});
        if (!daemonInited || !daemon.start())
          throw ZeEXCEPT(Fatal, "zumd", "HTTP server start failed");
      }
      uint64_t prepared = 0;
      bool reissue = options.bootstrapReissue;
      while (!stopping) {
        done.wait();
        if (stopping) break;
        auto activation = ZmBlock<ZuTuple<bool, uint64_t>>{}([db](auto wake) {
          db->run([db, wake = ZuMv(wake)]() mutable {
            wake({db->failed, db->active() ? db->generation : 0});
          });
        });
        if (activation.p<0>())
          throw ZeEXCEPT(Fatal, "zumd", "Zdb activation failed");
        auto generation = activation.p<1>();
        if (!generation || generation == prepared) continue;
        ZmSemaphore drained;
        db->requests->deactivate([&drained]() { drained.post(); });
        drained.wait();
        db->bootstrapRequests->deactivate([&drained]() { drained.post(); });
        drained.wait();
        bool current = ZmBlock<bool>{}([db, generation](auto wake) {
          db->run([db, generation, wake = ZuMv(wake)]() mutable {
            bool current = !stopping && db->active() && db->generation == generation;
            if (current) db->bootstrapRequests->activate();
            wake(current);
          });
        });
        if (!current) continue;
        if (options.rekey) {
          bool rotated = ZmBlock<bool>{}([
            db, context, &rng, &options, &dbKey, &newDBKey
          ](auto wake) mutable {
            Zum::serverRekey(db, context, rng, options.issuer, dbKey, newDBKey,
              [wake = ZuMv(wake)](bool ok) mutable { wake(ok); });
          });
          current = ZmBlock<bool>{}([db, generation](auto wake) {
            db->run([db, generation, wake = ZuMv(wake)]() mutable {
              wake(!db->failed && db->active() && db->generation == generation);
            });
          });
          if (!rotated || !current)
            throw ZeEXCEPT(Fatal, "zumd", "offline secret-key rotation failed; retain both keys");
          // stop() drains persistence before reporting durable completion.
          stop();
          if (!storeStopped)
            throw ZeEXCEPT(Fatal, "zumd", "offline secret-key rotation store drain failed");
          std::cout << "zumd: secret-key rotation complete" << std::endl;
          break;
        }
        Zum::ServerBootstrapResult bootstrap;
        bool bootstrapOK = false;
        ZmSemaphore bootstrapDone;
        Zum::serverBootstrap(db, db->bootstrapRequests, context, rng,
          Zum::ServerBootstrapConfig{
            .issuer = options.issuer,
            .admin = options.admin,
            .output = options.bootstrapOutput,
            .dbKey = dbKey,
            .now = Zm::now().sec(),
            .ttl = options.bootstrapTTL,
            .reissue = reissue},
          [&bootstrap, &bootstrapOK, &bootstrapDone](
              bool ok, Zum::ServerBootstrapResult result) mutable {
            bootstrapOK = ok;
            bootstrap = ZuMv(result);
            bootstrapDone.post();
          });
        bootstrapDone.wait();
        if (bootstrapOK) reissue = false;
        if (stopping) break;
        current = ZmBlock<bool>{}([db, generation](auto wake) {
          db->run([db, generation, wake = ZuMv(wake)]() mutable {
            wake(db->active() && db->generation == generation);
          });
        });
        if (!current) continue;
        bool ready = bootstrapOK && (!daemonInited || daemon.prepare(ZuMv(bootstrap)));
        current = ZmBlock<bool>{}([db, generation, ready](auto wake) {
          db->run([db, generation, ready, wake = ZuMv(wake)]() mutable {
            bool current = !stopping && db->active() && db->generation == generation;
            if (current && ready) db->requests->activate();
            wake(current);
          });
        });
        if (!current) continue;
        if (!ready)
          throw ZeEXCEPT(Fatal, "zumd", "bootstrap/signing preparation failed");
        prepared = generation;
        std::cout << "zumd: active" << std::endl;
        if (options.once) break;
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
  if (newDBKey && newDBKey.mutable_()) ZuClear(newDBKey.data(), newDBKey.length());
  ZiLog::stop();
  return result;
}
