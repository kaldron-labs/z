//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuLib.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_request.hh>

#include "../src/zumd_daemon.hh"

namespace Zum {
ZrestCatalogImpl(PublicCatalog)
ZrestCatalogImpl(HealthCatalog)
ZrestCatalogImpl(BootstrapCatalog)
ZrestCatalogImpl(EnrollCatalog)
ZrestCatalogImpl(WellKnownCatalog)
ZrestCatalogImpl(AdminCatalog)
ZrestCatalogImpl(DaemonCatalog)
ZrestRootCatalogImpl(DaemonRootCatalog)
}

using namespace ZuTestUtil;

static ZiMxParams params()
{
  return ZiMxParams().scheduler([](auto &s) {
    s.nThreads(3)
      .thread(1, [](auto &t) { t.isolated(true).name("rx"); })
      .thread(2, [](auto &t) { t.isolated(true).name("tx"); })
      .thread(3, [](auto &t) { t.isolated(true).name("owner"); });
  }).rxThread(1).txThread(2);
}

static Zum::DaemonConfig config()
{
  // This configured listener is never started or bound in these unit cases.
  return Zum::DaemonConfig{.issuer = "http://localhost", .rpID = "localhost",
    .cleanupInterval = 0};
}

static void initFinal()
{
  ZuTestScopeRT(initFinal);
  ZiMultiplex mx{params()}; // borrowed runtime is independent of daemon startup
  Zum::DB db;
  Zum::Requests requests;
  ZuCheckRT(requests.init(&mx, 3, 1));
  Zum::Daemon daemon;
  ZuGuard cleanup{[&daemon, &mx]() {
    daemon.final();
    ZuCheckRT(mx.stop());
  }};
  ZuCheckRT(daemon.state() == ZmEngineState::Stopped);
  daemon.final(); // valid without even starting the borrowed runtime
  ZuCheckRT(daemon.init(&db, &requests, &mx, config()));
  ZuCheckRT(daemon.state() == ZmEngineState::Stopped);
  daemon.final();
  daemon.final();
  // The same facade can be configured again after stopped finalization.
  ZuCheckRT(daemon.init(&db, &requests, &mx, config()));
  daemon.final();
}

static void partialInit()
{
  ZuTestScopeRT(partialInit);
  ZiMultiplex mx{params()};
  Zum::DB db;
  Zum::Requests requests;
  ZuCheckRT(requests.init(&mx, 3, 1));
  Zum::Daemon daemon;
  ZuGuard cleanup{[&daemon, &mx]() {
    daemon.final();
    ZuCheckRT(mx.stop());
  }};
  auto value = config();
  value.port = 0; // child HTTP init rejects this after provider init succeeds
  ZuCheckRT(!daemon.init(&db, &requests, &mx, ZuMv(value)));
  ZuCheckRT(daemon.state() == ZmEngineState::Stopped);
  daemon.final();
  ZuCheckRT(daemon.init(&db, &requests, &mx, config()));
  daemon.final();
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiLog::init("ZumDaemonTest");
  ZiLog::level(0);
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(initFinal);
  ZuTestCall(partialInit);
  ZiLog::stop();
  return 0;
}
