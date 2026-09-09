//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// offline system bootstrap tool

#include <iostream>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvMxParams.hh>

#include <zlib/ZumPasskey.hh>
#include <zlib/ZumMgmt.hh>

#include <zlib/ZtlsRandom.hh>

struct Options {
  Zum::String	admin;
  Zum::String	issuer;
  Zum::String	module;
  Zum::String	connect;
  Zum::String	log;
  uint32_t	ttl = 900;
  bool		debug = false;
  bool		help = false;
};
ZfStruct(, (Options, CLI),
  (((admin),	(CLI::Arg<1>)),			(String)),
  (((issuer),	(CLI::Opt<'i'>)),			(String)),
  (((module),	(CLI::Opt<'m'>)),			(String)),
  (((connect),	(CLI::Opt<'c'>)),			(String)),
  (((log),	(CLI::Opt<'l'>)),			(String, "&2")),
  (((ttl),	(CLI::Opt<'t'>, (Range<1, 86400>))),	(UInt32, 900)),
  (((debug),	(CLI::Flag<'d'>)),			(Bool)),
  (((help),	(CLI::Flag<'h'>)),			(Bool)));

static void usage()
{
  std::cerr <<
    "Usage: zum ADMIN_EMAIL [OPTION]...\n\n"
    "Initialize an empty Zum store and issue a one-shot browser enrollment\n"
    "capability for its first administrative user. The bootstrap creates\n"
    "the built-in Zum management actions and the superuser role.\n\n"
    "Options:\n"
    "  -m, --module=MODULE\tZdb store module (default: $ZDB_MODULE)\n"
    "  -c, --connect=CONNECT\tZdb connection (default: $ZDB_CONNECT)\n"
    "  -i, --issuer=ISSUER\tissuer URL (default: https:// email domain)\n"
    "  -t, --ttl=SECONDS\tcapability lifetime (default: 900)\n"
    "  -l, --log=FILE\tlog destination (default: stderr)\n"
    "  -d, --debug\t\tenable Zdb debug logging\n"
    "  -h, --help\t\tthis help\n" << std::flush;
}

static bool defaultIssuer(Zum::String &issuer, ZuCSpan admin)
{
  unsigned at = 0;
  for (unsigned i = 0, n = admin.length(); i < n; i++) {
    if (admin[i] != '@') continue;
    if (at || !i || i + 1 == n) return false;
    at = i;
  }
  if (!at) return false;
  issuer << "https://" << admin.offset(at + 1);
  return true;
}

template <typename T>
static bool insertRecord(ZdbTable<T> *table, T data)
{
  return ZmBlock<bool>{}([
    table, data = ZuMv(data)
  ](auto wake) mutable {
    table->run(0, [table, data = ZuMv(data), wake = ZuMv(wake)]() mutable {
      ZdbRowRef<T> row = new ZdbRow<T>{table, ZdbShard{0}};
      table->insert(row, [
	data = ZuMv(data), wake = ZuMv(wake)
      ](ZdbRow<T> *row) mutable {
	if (!row) { wake(false); return; }
	new (row->ptr()) T{ZuMv(data)};
	wake(row->commit());
      });
    });
  });
}

static bool bootstrap(
    Zum::Requests *requests, Zum::DBContext *context, Ztls::Random &rng,
    Options &options, int64_t now, Zum::String &capability)
{
  unsigned n = Zum::MgmtOp::N;
  if (!insertRecord(context->issuers, Zum::Issuer{
      .id = options.issuer, .nextActionID = n, .authVersion = 1}))
    return false;
  for (unsigned i = 0; i < n; i++) {
    if (!insertRecord(context->actions, Zum::Action{
	.id = i, .name = Zum::managementAction(i),
	.state = Zum::State::Active}))
      return false;
  }
  ZtBitmap permitted{n};
  for (unsigned i = 0; i < n; i++) permitted.set(i);
  if (!insertRecord(context->roles, Zum::Role{
      .id = 1, .name = "superuser", .actions = ZuMv(permitted),
      .state = Zum::State::Active}))
    return false;
  Zum::IDVec roleIDs;
  roleIDs.push(1);
  return ZmBlock<bool>{}([
    requests, context, &rng, &options, now, &capability, roleIDs = ZuMv(roleIDs)
  ](auto wake) mutable {
    if (!Zum::bootstrapIssue(requests, Zm::now() + ZuTime{30},
	context, rng, Zum::BootstrapConfig{
	.issuer = options.issuer, .roleIDs = ZuMv(roleIDs),
	.userName = options.admin, .label = "bootstrap passkey", .userID = 1,
	.now = now, .expires = now + options.ttl
	}, [&capability, wake = ZuMv(wake)](bool ok, Zum::String value) mutable {
	capability = ZuMv(value);
	wake(ok);
	})) wake(false);
  });
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
  if (!options.admin || !options.module || !options.connect ||
      (!options.issuer && !defaultIssuer(options.issuer, options.admin))) {
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
    bool ok = bootstrap(db->requests, context, rng, options, now, capability);
    if (ok) {
      std::cout << capability << '\n' << std::flush;
      ZuClear(capability.data(), capability.length());
      result = 0;
    } else {
      std::cerr << "Zum store already initialized or bootstrap failed\n";
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
