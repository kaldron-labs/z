//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPrint.hh>
#include <zlib/ZuPolymorph.hh>

#include <zlib/ZmTrap.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZcmdServer.hh>

class CmdTest;

struct Link : public ZcmdSrvLink<CmdTest, Link> {
  using Base = ZcmdSrvLink<CmdTest, Link>;
  Link(CmdTest *app) : Base{app} { }
};

class CmdTest : public ZmObject, public ZcmdServer<CmdTest, Link> {
public:
  void init(const ZfCf::AnyNode *cf, ZiMultiplex *mx, Zdb *db) {
    m_uptime = Zm::now();

    ZcmdServer::init(cf, mx, db);

    addCmd("ackme", "", Zcmd::Fn{this,
      [](CmdTest *this_, ZmRef<Zcmd::Context> ctx) {
	auto link = static_cast<Link *>(ctx->dest.p<void *>());
	if (auto cxn = link->cxn())
	  std::cout << cxn->info().remoteIP << ':'
	    << ZuBoxed(cxn->info().remotePort) << ' ';
	const auto &user = link->session()->user->data();
	ZiLOG(Info, "cmdtest", ([
	  id = user.id, name = user.name, cmd = ctx->args->get("0")
	](auto &s) {
	  s << "user: " << id << ' ' << name << ' '
	    << "cmd: " << cmd;
	}));
	ctx->out << "this is an ack";
	Zcmd::executed(ZuMv(ctx), 0);
      }}, "test ack", "");
    addCmd("nakme", "", Zcmd::Fn{this,
      [](CmdTest *this_, ZmRef<Zcmd::Context> ctx) {
	ctx->out << "this is a nak";
	Zcmd::executed(ZuMv(ctx), 1);
      }}, "test nak", "");
    addCmd("quit", "", Zcmd::Fn{this,
      [](CmdTest *this_, ZmRef<Zcmd::Context> ctx) {
	this_->post();
	ctx->out << "quitting...";
	Zcmd::executed(ZuMv(ctx), 0);
      }}, "quit", "");
  }

  void wait() { m_done.wait(); }
  void post() { m_done.post(); }

  void telemetry(Ztel::App &data) {
    using namespace Ztel;
    data.id = "cmdtest";
    data.version = "1.0";
    data.uptime = m_uptime;
    data.role = AppRole::Dev;
    data.rag = RAG::Green;
  }

private:
  ZuDateTime	m_uptime;
  ZmSemaphore	m_done;
};

ZiMultiplex *mx = nullptr;
ZmRef<CmdTest> server;

void gtfo() {
  if (mx) mx->stop();
  ZiLog::stop();
  Zm::exit(1);
};

struct Options {
  ZuCSpan	certPath;
  ZuCSpan	keyPath;
  ZuCSpan	localIP;
  unsigned	localPort = 0;
  ZuCSpan	module = getenv("ZDB_MODULE");
  ZuCSpan	connect = getenv("ZDB_CONNECT");
  ZuCSpan	caPath = "/etc/ssl/certs";
  unsigned	passLen = 12;
  unsigned	totpRange = 2;
  unsigned	keyInterval = 30;
  ZuCSpan	log = "&2";
  bool		debug = false;
  bool		help = false;
};

ZfStruct((Options, CLI),
  (((certPath),		(CLI::Arg<1>)),			(String)),
  (((keyPath),		(CLI::Arg<2>)),			(String)),
  (((localIP),		(CLI::Arg<3>)),			(String)),
  (((localPort),	(CLI::Arg<4>, (Range<1U, 65535U>))), (UInt32)),
  (((module),		(CLI::Opt<'m'>)),		(String)),
  (((connect),		(CLI::Opt<'c'>)),		(String)),
  (((caPath),		(CLI::Opt<'C'>, CLI::Long<"ca-path">)), (String,
      "/etc/ssl/certs")),
  (((passLen),		(CLI::Long<"pass-len">, (Range<6U, 60U>))),
							(UInt32, 12)),
  (((totpRange),	(CLI::Long<"totp-range">, (Range<0U, 100U>))),
							(UInt32, 2)),
  (((keyInterval),	(CLI::Long<"key-interval">,
      (Range<0U, 36000U>))),				(UInt32, 30)),
  (((log),		(CLI::Opt<'l'>)),		(String, "&2")),
  (((debug),		(CLI::Flag<'d'>)),		(Bool)),
  (((help),		(CLI::Flag<'h'>)),		(Bool)));

void usage()
{
  std::cerr << "Usage: cmdtest CERTPATH KEYPATH IP PORT [OPTION]...\n"
    "  CERTPATH\tTLS/SSL certificate path\n"
    "  KEYPATH\tTLS/SSL private key path\n"
    "  IP\t\tlistener IP address\n"
    "  PORT\t\tlistener port\n\n"
    "Options:\n"
    "  -m, --module=MODULE\tZdb data store module e.g. libZdbPQ.so\n"
    "  -c, --connect=CONNECT\tZdb data store connection string\n"
    "\t\t\te.g. \"dbname=test host=/tmp\"\n"
    "  -C, --ca-path=CAPATH\tset CA path (default: /etc/ssl/certs)\n"
    "      --pass-len=N\tset default password length (default: 12)\n"
    "      --totp-range=N\tset TOTP accepted range (default: 2)\n"
    "      --key-interval=N\tset key refresh interval (default: 30)\n"
    "  -l, --log=FILE\tlog to FILE\n"
    "  -d, --debug\t\tenable Zdb debugging\n"
    "      --help\t\tthis help\n"
    << std::flush;
  gtfo();
}

void sigint() { if (server) server->post(); }

int main(int argc_, char **argv)
{
  mx = new ZiMultiplex();
  ZmRef<Zdb> db = new Zdb();
  server = new CmdTest{};

  try {
    Options options;
    int argc = ZfCLI::load(options, argc_, argv);
    if (argc != 5 || options.help) usage();
    if (!options.module) {
      std::cerr << "set ZDB_MODULE or use --module=MODULE\n" << std::flush;
      gtfo();
    }
    if (!options.connect) {
      std::cerr << "set ZDB_CONNECT or use --connect=CONNECT\n" << std::flush;
      gtfo();
    }

    ZmRef<ZfCf::Defines> defines = new ZfCf::Defines();
    auto define = [&defines](ZuCSpan key, auto value) {
      defines->add(ZfCf::DefKey{key}, ZfCf::DefVal{value});
    };
    define("MODULE", options.module);
    define("CONNECT", options.connect);
    define("CAPATH", options.caPath);
    define("CERTPATH", options.certPath);
    define("KEYPATH", options.keyPath);
    define("LOCALIP", options.localIP);
    define("LOCALPORT", ZuBoxed(options.localPort));
    define("PASSLEN", ZuBoxed(options.passLen));
    define("TOTPRANGE", ZuBoxed(options.totpRange));
    define("KEYINTERVAL", ZuBoxed(options.keyInterval));
    define("DEBUG", options.debug ? "true" : "false");
    auto scan = ZfCf::scan(
      "mx: {\n"
      "  nThreads: 5,\n"
      "  threads: {\n"
      "    1: {name: rx, isolated: true},\n"
      "    2: {name: tx, isolated: true},\n"
      "    3: {name: zdb, isolated: true},\n"
      "    4: {name: zdb_store, isolated: true},\n"
      "    5: {name: app}\n"
      "  },\n"
      "  rxThread: rx,\n"
      "  txThread: tx\n"
      "},\n"
      "userdb: {\n"
      "  thread: app,\n"
      "  passLen: ${PASSLEN},\n"
      "  totpRange: ${TOTPRANGE},\n"
      "  keyInterval: ${KEYINTERVAL}\n"
      "},\n"
      "zdb: {\n"
      "  thread: zdb,\n"
      "  debug: ${DEBUG},\n"
      "  hostID: 0,\n"
      "  hosts: {0: {standalone: true}},\n"
      "  store: {\n"
      "    module: ${MODULE},\n"
      "    connection: ${CONNECT},\n"
      "    thread: zdb_store,\n"
      "    replicated: true\n"
      "  },\n"
      "  tables: {}\n"
      "},\n"
      "server: {\n"
      "  thread: app,\n"
      "  caPath: ${CAPATH},\n"
      "  certPath: ${CERTPATH},\n"
      "  keyPath: ${KEYPATH},\n"
      "  localIP: ${LOCALIP},\n"
      "  localPort: ${LOCALPORT}\n"
      "}\n",
      {}, ZuMv(defines));
    auto cf = ZuMv(scan.p<1>());

    ZiLog::init("cmdtest");
    ZiLog::level(0);
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(options.log)));
    ZiLog::start();

    mx = new ZiMultiplex{ZvMxParams{"mx", cf->resolve("mx")}};

    mx->start();

    ZdbCf dbCf{cf->resolve("zdb")};

    CmdTest::dbCf(cf, dbCf);

    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *) { },
      .downFn = [](Zdb *, bool) { }
    });

    server->init(cf, mx, db);

  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    usage();
  } catch (const ZvError &e) {
    std::cerr << e << '\n' << std::flush;
    gtfo();
  } catch (const ZtString<> &e) {
    std::cerr << e << '\n' << std::flush;
    gtfo();
  } catch (...) {
    std::cerr << "unknown exception\n" << std::flush;
    gtfo();
  }

  ZmTrap::sigintFn(sigint);
  ZmTrap::trap();

  if (!db->start()) {
    ZiLOG(Fatal, "cmdtest", "Zdb start failed");
    gtfo();
  }

  if (!ZmBlock<bool>{}([](auto wake) {
    server->open({}, [wake = ZuMv(wake)](bool ok, ZtArray<unsigned>) mutable {
      wake(ok);
    });
  })) {
    ZiLOG(Fatal, "cmdtest", "UserDB open failed");
    db->stop();
    gtfo();
  }

  server->start();

  server->wait();

  server->stop();
  db->stop();
  mx->stop();

  server->final();
  server = {};

  db->final();
  db = {};

  delete mx;

  ZiLog::stop();

  return 0;
}
