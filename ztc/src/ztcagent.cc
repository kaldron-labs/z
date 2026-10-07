//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>

#include <iostream>

#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZtPlatform.hh>

#include <zlib/ZfCLI.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZtlsVault.hh>

#include <zlib/ZvCf.hh>

#include <zlib/ztcagent_daemon.hh>

struct Options {
  Ztc::AgentString	config{"ztcagent.conf"};
  bool		help = false;
  bool		version = false;
};

ZfStruct(, (Options, CLI),
  (config,	(CLI::Opt<'c'>, CLI::Long<"config">),		String),
  (help,	(CLI::Flag<'h'>, CLI::Long<"help">),		Bool),
  (version,	(CLI::Flag<'V'>, CLI::Long<"version">),		Bool));

static ZmSemaphore done;
static ZmAtomic<unsigned> interrupted = 0;
static ZmAtomic<unsigned> provisionReady = 0;

static void trapped()
{
  interrupted = 1;
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
  const char *issuerURL = ::getenv("ZTC_ISSUER");
  const char *clientID = ::getenv("ZTC_CLIENT_ID");
  const char *wssURL = ::getenv("ZTC_WSS_URL");
  const char *clientSecret = ::getenv("ZTC_CLIENT_SECRET");
  auto pidDir = Zt::getpath("ZTC_DIR");
  auto caPath = Zt::getpath("ZTC_CA_PATH");
  const char *ring = ::getenv("ZTC_RING");
  return {
    .issuerURL = issuerURL ? issuerURL : "",
    .clientID = clientID ? clientID : "",
    .deviceID = ::getenv("ZTC_DEVICE_ID") ?
      ::getenv("ZTC_DEVICE_ID") : "",
    .caPath = caPath ? caPath : "",
    .wssURL = wssURL ? wssURL : "",
    .clientSecret = clientSecret ? clientSecret : "",
    .provision = clientSecret != nullptr,
    .pidDir = pidDir ? pidDir : "ztc",
    .ring = ring ? ring : "ztc"
  };
}

static Ztls::VaultConfig vaultConfig(const Ztc::AgentCf &cf,
    const Ztc::AgentEnv &env)
{
  Ztls::VaultConfig result;
  result.program = "ztcagent";
  result.account << env.issuerURL << '\n' << env.clientID;
  result.variant = Ztls::VaultVariant::Direct;
  if (cf.vaultStore == "keyring")
    result.store = Ztls::VaultStore::KeyRing;
  else if (cf.vaultStore == "module" && cf.vaultModule) {
    result.store = Ztls::VaultStore::Module;
    result.module = cf.vaultModule;
  } else if ((cf.vaultStore == "file" || cf.vaultStore == "ephemeral") &&
      cf.vaultTestStore && ::getenv("ZTCAGENT_HOME") &&
      *::getenv("ZTCAGENT_HOME"))
    result.store = cf.vaultStore == "file" ?
      Ztls::VaultStore::File : Ztls::VaultStore::Ephemeral;
  else
    throw ZeEXCEPT(Fatal, "ztcagent", "invalid Vault store configuration");
  return result;
}

template <typename Fn>
static Ztls::VaultResult withVault(const Ztls::VaultConfig &cf, Fn &&fn)
{
  Ztls::Vault vault;
  auto result = vault.init(cf);
  if (result.is<ZeException>()) return result;
  result = vault.open();
  if (result.is<ZeException>()) {
    vault.final();
    return result;
  }
  ZuGuard close{[&vault]() { vault.close(); vault.final(); }};
  return ZuFwd<Fn>(fn)(vault);
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
    Ztc::AgentCf cf;
    ZfCf::handler<Ztc::AgentCf>(loaded.p<1>()).update(cf);
    auto env = environment();
    if (!env.issuerURL || !env.clientID || !env.deviceID ||
        !env.wssURL) {
      std::cerr << "ZTC_ISSUER, ZTC_CLIENT_ID, ZTC_DEVICE_ID, "
        "and ZTC_WSS_URL are required\n";
      return 1;
    }
    auto vaultCf = vaultConfig(cf, env);
    if (!env.provision) {
      auto loadedSecret = withVault(vaultCf, [&env](Ztls::Vault &vault) {
        return vault.load(Ztls::Scopes::Global{}, "clientSecret",
          [&env](ZuSpan<uint8_t> value) {
            env.clientSecret = ZuCSpan{value};
          });
      });
      if (loadedSecret.is<ZeException>())
        throw ZuMv(loadedSecret).p<ZeException>();
    }
    if (!env.clientSecret) {
      std::cerr << "ztcagent client secret unavailable\n";
      return 1;
    }
    env.onProvision = []() { provisionReady = 1; done.post(); };

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
    while (ok && !interrupted.load_()) {
      done.wait();
      if (interrupted.load_()) break;
      if (!provisionReady.load_()) continue;
      provisionReady = 0;
      auto published = withVault(vaultCf, [&agent](Ztls::Vault &vault) {
        return vault.save(Ztls::Scopes::Global{}, "clientSecret",
          agent.clientSecret());
      });
      if (published.is<ZeException>()) {
        std::cerr << ZuMv(published).p<ZeException>() << '\n';
        ok = false;
      }
      agent.provisioned(ok);
    }
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
