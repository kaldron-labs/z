//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuPrint.hh>
#include <zlib/ZuPolymorph.hh>

#include <zlib/ZmTrap.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZcmdServer.hh>

class CmdTest;

struct Link : public ZcmdSrvLink<CmdTest, Link> {
  using Base = ZcmdSrvLink<CmdTest, Link>;
  Link(CmdTest *app) : Base{app} { }
};

class CmdTest : public ZmObject, public ZcmdServer<CmdTest, Link> {
public:
  void init(const ZvCf *cf, ZiMultiplex *mx, Zdb *db) {
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
    "      --max-age=N\tset user DB file backups (default: 8)\n"
    "  -l, --log=FILE\tlog to FILE\n"
    "  -d, --debug\t\tenable Zdb debugging\n"
    "      --help\t\tthis help\n"
    << std::flush;
  gtfo();
}

void sigint() { if (server) server->post(); }

int main(int argc, char **argv)
{
  ZmRef<ZvCf> cf;
  mx = new ZiMultiplex();
  ZmRef<Zdb> db = new Zdb();
  server = new CmdTest{};

  try {
    ZmRef<ZvCf> options = new ZvCf{};

    options->fromString(
      "module m m { param zdb.store.module }\n"
      "connect c c { param zdb.store.connection }\n"
      "ca-path C C { param caPath }\n"
      "pass-len { param userDB.passLen }\n"
      "totp-range { param userDB.totpRange }\n"
      "key-interval { param userDB.keyInterval }\n"
      "max-age { param userDB.maxAge }\n"
      "log l l { param log }\n"
      "debug d d { flag zdb.debug }\n"
      "help { flag help }\n");

    ZmRef<ZvCf> cf = new ZvCf{};

    cf->fromString(
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
      "  passLen 12\n"
      "  totpRange 2\n"
      "  keyInterval 30\n"
      "  maxAge 8\n"
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
      "  tables { }\n"
      "}\n"
      "server {\n"
      "  thread app\n"
      "  caPath /etc/ssl/certs\n"
      "}\n");

    if (cf->fromArgs(options, ZvCf::args(argc, argv)) != 5 ||
	cf->getBool("help")) {
      usage();
      gtfo();
    }

    if (!cf->exists("zdb.store.module")) {
      std::cerr << "set ZDB_MODULE or use --module=MODULE\n" << std::flush;
      gtfo();
    }
    if (!cf->exists("zdb.store.connection")) {
      std::cerr << "set ZDB_CONNECT or use --connect=CONNECT\n" << std::flush;
      gtfo();
    }

    {
      ZmRef<ZvCf> srvCf = cf->getCf<true>("server");
      srvCf->set("certPath", cf->get("1"));
      srvCf->set("keyPath", cf->get("2"));
      srvCf->set("localIP", cf->get("3"));
      srvCf->set("localPort", cf->get("4"));
    }

    ZiLog::init("cmdtest");
    ZiLog::level(0);
    ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path(cf->get<true>("log"))));
    ZiLog::start();

    mx = new ZiMultiplex{ZvMxParams{"mx", cf->getCf<true>("mx")}};

    mx->start();

    ZdbCf dbCf{cf->getCf<true>("zdb")};

    CmdTest::dbCf(cf, dbCf);

    db->init(ZuMv(dbCf), mx, ZdbHandler{
      .upFn = [](Zdb *, ZdbHost *) { },
      .downFn = [](Zdb *, bool) { }
    });

    server->init(cf, mx, db);

  } catch (const ZvCfError::Usage &e) {
    usage();
    gtfo();
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
