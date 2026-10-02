//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Native OAuth and protected HTTP example

#include "native.hh"

using namespace ZumNative;

struct Options {
  String	config;
  bool		noBrowser = false;
  bool		help = false;
};

ZfStruct(, (Options, CLI),
  (((config), (CLI::Long<"config">)),			String),
  (((noBrowser), (CLI::Long<"no-browser">)),		Bool),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)),	Bool));

static void usage(int code = 1)
{
  std::cerr <<
    "Usage: zumping --config FILE [--no-browser]\n";
  ::exit(code);
}

int main(int argc, char **argv)
{
  Options options;
  try { argc = ZfCLI::load(options, argc, argv); }
  catch (const ZeException &e) { std::cerr << e << '\n'; usage(); }
  if (options.help) usage(0);
  if (argc != 1 || !options.config) usage();
  Config config;
  try {
    if (!loadConfig(options.config, config)) {
      std::cerr << "zumping: invalid configuration\n"; return 1;
    }
  } catch (const ZeException &e) {
    std::cerr << "zumping: invalid configuration: " << e << '\n';
    return 1;
  }

  Zhttp::URL service;
  if (!endpointURL(config.serviceURL, config.loopbackTest, service) ||
      service.url().hasQuery || service.url().hasFragment ||
      (service.url().path && service.url().path != "/")) {
    std::cerr << "zumping: service URL must be an HTTP(S) origin\n";
    return 1;
  }
  String resourceURL{config.serviceURL};
  while (resourceURL && resourceURL[resourceURL.length() - 1] == '/')
    resourceURL.length(resourceURL.length() - 1);
  resourceURL << "/ping";
  return run(config, options.noBrowser,
    [&config, &service, &resourceURL](ZiMultiplex &mx, Clients &clients,
        ZuCSpan token) {
      if (!clients.add(&mx, service.url(), config.caPath)) return false;
      Result result;
      String bearer{"Bearer "};
      bearer << token;
      bool ok = clients.perform<ResourceBuilder>(result, resourceURL,
        {}, ZuMv(bearer)) && result.status == 200;
      if (ok) std::cout << result.body << '\n';
      return ok;
    }) ? 0 : 1;
}
