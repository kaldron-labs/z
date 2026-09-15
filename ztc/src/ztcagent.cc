//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>

#include <zlib/ZvCf.hh>

#include <zlib/ZtcAgent.hh>

struct Options {
  ZtString<>	config{"ztcagent.conf"};
  bool		help = false;
  bool		version = false;
};

ZfStruct(, (Options, CLI),
  (((config),	(CLI::Opt<'c'>, CLI::Long<"config">)),	(String,
	"ztcagent.conf")),
  (((help),	(CLI::Flag<'h'>, CLI::Long<"help">)),	(Bool)),
  (((version),	(CLI::Flag<'V'>, CLI::Long<"version">)),	(Bool)));

static ZmSemaphore done;

static void trapped()
{
  done.post();
}

static void usage(int code)
{
  std::cerr <<
    "Usage: ztcagent [OPTION]...\n\n"
    "Options:\n"
    "  -c, --config=PATH  configuration file (default ztcagent.conf)\n"
    "  -V, --version      print version\n"
    "  -h, --help         print this help\n" << std::flush;
  ::exit(code);
}

static Ztc::AgentEnv environment()
{
  const char *issuer = ::getenv("ZTC_ISSUER");
  const char *clientID = ::getenv("ZTC_CLIENT_ID");
  const char *credentialStore = ::getenv("ZTC_CREDENTIAL_STORE");
  const char *wssURL = ::getenv("ZTC_WSS_URL");
  const char *accessToken = ::getenv("ZTC_ACCESS_TOKEN");
  const char *pidDir = ::getenv("ZTC_DIR");
  const char *ring = ::getenv("ZTC_RING");
  return {
    .issuer = issuer ? issuer : "",
    .clientID = clientID ? clientID : "",
    .deviceID = ::getenv("ZTC_DEVICE_ID") ?
      ::getenv("ZTC_DEVICE_ID") : "",
    .credentialStore = credentialStore ? credentialStore : "",
    .caPath = ::getenv("ZTC_CA_PATH") ? ::getenv("ZTC_CA_PATH") : "",
    .wssURL = wssURL ? wssURL : "",
    .accessToken = accessToken ? accessToken : "",
    .pidDir = pidDir ? pidDir : "ztc",
    .ring = ring ? ring : "ztc"
  };
}

int main(int argc, char **argv)
{
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
    if (options.help) usage(0);
    if (argc != 1) usage(1);
    if (options.version) {
      std::cout << Z_VERSION << '\n';
      return 0;
    }

    auto loaded = ZvCf::load(options.config);
    Ztc::AgentCf cf = ZfCf::handler<Ztc::AgentCf>(loaded.p<1>()).ctor();
    auto env = environment();
    if (!env.issuer || !env.clientID || !env.deviceID ||
        !env.credentialStore || !env.wssURL || !env.accessToken) {
      std::cerr << "ZTC_ISSUER, ZTC_CLIENT_ID, ZTC_CREDENTIAL_STORE, "
        "ZTC_DEVICE_ID, ZTC_WSS_URL, and ZTC_ACCESS_TOKEN are required\n";
      return 1;
    }

    ZiLog::init("ztcagent");
    ZiLog::sink(ZiLog::sysSink());
    ZiLog::start();

    ZmTrap::sigintFn(trapped);
    ZmTrap::trap();

    Ztc::Agent agent;
    bool ok = agent.init(cf, ZuMv(env));
    if (!ok)
      std::cerr << "ztcagent initialization failed\n";
    else if (!(ok = agent.start()))
      std::cerr << "ztcagent startup failed\n";
    if (ok) done.wait();
    ok = agent.stop() && ok;
    agent.final();

    ZmTrap::sigintFn(nullptr);
    ZiLog::stop();
    return ok ? 0 : 1;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
}
