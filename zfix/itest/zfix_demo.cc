//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include <zlib/ZmRef.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfJSON.hh>
#include "zfix_demo_io.hh"

namespace ZfixDemo {

  ZtEnumImplNS(Scenario);
  ZtEnumImplNS(Scenario, CLI);
}

using namespace ZfixDemo;
template <typename Net> static int run(Options options) {
  State state{options};
  Hub<Net, false> server{&state}; Hub<Net, true> client{&state};
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); });
  }).rxThread(1).txThread(2)};
  if (!mx.start()) return 1;
  typename Net::ServerParams sp{&mx, "3", "4"};
  typename Net::ClientParams cp{&mx, "3", "4"};
  if constexpr (Net::Encrypted) {
    sp.certPath(options.cert).keyPath(options.key); cp.caPath(options.ca);
  }
  bool si = server.init(ZuMv(sp)), ci = client.init(ZuMv(cp));
  bool ss = si && server.start(), cs = ci && client.start();
  ZmRef<Link<Net, true>> link;
  if (!ss || !cs) state.fail();
  else {
    server.listen();
    if (state.listening.timedwait(Zm::now(5)) || !state.port) state.fail();
    else {
      link = new Link<Net, true>{&client};
      if constexpr (Net::Encrypted) link->connect("localhost", state.port, ZiIP{"127.0.0.1"});
      else link->connect("127.0.0.1", state.port);
      if (state.done.timedwait(Zm::now(5))) state.fail();
      if (options.badAuth || state.errors.load_()) link->disconnect();
      else link->logout();
      if (state.stopped.timedwait(Zm::now(5))) { state.fail(); link->disconnect(); }
    }
  }
  if (cs && !client.stop()) state.fail();
  if (ss && !server.stop()) state.fail();
  if (ci) client.final();
  if (si) server.final();
  mx.stop(); link = nullptr; server.connection = nullptr;
  auto &s = state.summary;
  if (options.badAuth) {
    if (!s.authRefused || s.registered || s.complete || s.fresh) state.fail();
  } else {
    if (s.authAccepted != 2 || s.authRefused || s.registered != options.calls() ||
	s.complete != options.calls()) state.fail();
    switch (options.scenario) {
      case Scenario::RPC: if (s.fresh != 1) state.fail(); break;
      case Scenario::Groups: if (s.fresh != 1 || s.groups != 2) state.fail(); break;
      case Scenario::Events: if (s.events != 1 || s.fresh) state.fail(); break;
      case Scenario::Reject: if (s.rejected != 1 || s.seqRejects != 1 || s.fresh) state.fail();
	break;
      case Scenario::Policy:
	if (s.fresh != 2 || s.cached != 1 || s.duplicate != 4 || s.unknown != 1 ||
	    s.firstComplete != 1002) state.fail();
	break;
    }
  }
  s.scenario = Scenario::CLI::v2s(options.scenario); s.tls = options.tls;
  s.errors = state.errors.load_();
  ZtString<ZtStringHeapID<"Zfix.Demo.JSON">> json;
  ZfJSON::save(json, s); json << '\n'; fwrite(json.data(), 1, json.length(), stdout);
  return s.errors ? 1 : 0;
}
int main(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (!strcmp(a, "--tls")) o.tls = true;
    else if (!strcmp(a, "--bad-auth")) o.badAuth = true;
    else if (!strcmp(a, "--auth") && i + 1 < argc) o.token = argv[++i];
    else if (!strcmp(a, "--cert") && i + 1 < argc) o.cert = argv[++i];
    else if (!strcmp(a, "--key") && i + 1 < argc) o.key = argv[++i];
    else if (!strcmp(a, "--ca") && i + 1 < argc) o.ca = argv[++i];
    else if (!strcmp(a, "--mode") && i + 1 < argc) {
      a = argv[++i];
      if (!strcmp(a, "4")) o.mode = Zfix::Transport::FIX4;
      else if (!strcmp(a, "44A")) o.mode = Zfix::Transport::FIX44A;
      else if (!strcmp(a, "T11")) o.mode = Zfix::Transport::FIXT11;
      else goto usage;
    } else {
      auto scenario = Scenario::CLI::s2v(a);
      if (unsigned(scenario) >= Scenario::N) goto usage;
      o.scenario = scenario;
    }
  }
  if (o.badAuth && !o.token.length()) goto usage;
  if (o.tls) {
    if (!o.cert) o.cert = getenv("ZFIX_TEST_CERT");
    if (!o.key) o.key = getenv("ZFIX_TEST_KEY");
    if (!o.ca) o.ca = o.cert;
    if (!o.cert || !o.key || !o.ca) goto usage;
    return run<Zfix::TLS>(o);
  }
  return run<Zfix::TCP>(o);
usage:
  fputs("usage: zfix_demo [rpc|groups|events|reject|policy] [--mode 4|44A|T11]\n"
    "  [--auth TOKEN [--bad-auth]] [--tls --cert FILE --key FILE --ca FILE]\n", stderr);
  return 2;
}
