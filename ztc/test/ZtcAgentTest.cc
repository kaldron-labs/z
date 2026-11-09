//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ztcagent_daemon.hh>

#include "../../zum/example/pinghttp.hh"
#include "ZiTestResidue.hh"

using namespace ZuTestUtil;

namespace ZtcAgentTest_ {

static void httpLifecycle()
{
  ZuTestScopeRT(httpLifecycle);
  ZiMxParams params;
  params.scheduler([](auto &scheduler) {
    scheduler.nThreads(2)
      .thread(1, [](auto &thread) { thread.name("rx"); thread.isolated(true); })
      .thread(2, [](auto &thread) { thread.name("tx"); thread.isolated(true); });
  }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  auto init = [&mx](Zum::PingHTTP &http) {
    return http.init(&mx, "https://zum.example/oauth2/7",
      "https://zum.example/oauth2/7", "https://zum.example/manage");
  };
  Zum::PingHTTP http;
  ZuCheckRT(!http.start());
  ZuCheckRT(http.stop());
  http.final();
  // Register the controller's allocators before starting the borrowed multiplexer.
  ZuCheckRT(init(http));
  ZuCheckRT(http.state() == ZmEngineState::Stopped);
  auto retained = http.fn();
  http.final(); // neither the HTTP controller nor resolver has started
  bool rejected = false;
  retained(Zum::ServiceHTTPRequest{}, [&rejected](Zum::ServiceHTTPResponse response) {
    rejected = response.status == 0;
  });
  ZuCheckRT(rejected);
  retained = {};

  ZiResolver::init();
  ZuCheckRT(init(http));
  ZuCheckRT(mx.start());
  ZuCheckRT(!http.start()); // borrowed resolver is stopped
  ZuCheckRT(http.state() == ZmEngineState::Stopped);
  http.final();
  ZuCheckRT(ZiResolver::instance()->initialized());
  ZiResolver::final();
  ZuCheckRT(mx.stop());

  ZuCheckRT(init(http));
  ZuCheckRT(mx.start());
  ZuCheckRT(http.start());
  ZuCheckRT(http.start());
  ZuCheckRT(http.state() == ZmEngineState::Running);
  ZuCheckRT(http.stop());
  ZuCheckRT(http.stop());
  ZuCheckRT(http.state() == ZmEngineState::Stopped);
  ZuCheckRT(http.start());
  bool stopped = ZmBlock<bool>{}([&http](auto wake) {
    http.stop([&http, wake = ZuMv(wake)](bool ok) mutable {
      if (ok) http.final();
      wake(ok);
    });
  });
  ZuCheckRT(stopped);
  ZuCheckRT(mx.stop());
}

static Ztc::AgentCf config()
{
  Ztc::AgentCf cf;
  cf.maxFrame = 1U << 20;
  cf.telBytes = 1U << 24;
  cf.reqBytes = 1U << 22;
  cf.vaultStore = "keyring";
  return cf;
}

static Ztc::AgentEnv env()
{
  return {
    .issuerURL = "https://zum.example/oauth2/7",
    .clientID = "device-client",
    .deviceID = "device-1",
    .wssURL = "wss://ztchub.example/ztc",
    .clientSecret = "client-secret",
    .ring = "ztc-agent-test"
  };
}

static void agentLifecycle()
{
  ZuTestScopeRT(agentLifecycle);
  auto cf = config();
  Ztc::Agent agent;
  ZuCheckRT(!agent.start());
  ZuCheckRT(agent.stop());
  agent.final();
  agent.provisioned(true); // closed admission does not require runtime resources
  ZuCheckRT(agent.init(cf, env()));
  ZuCheckRT(agent.state() == ZmEngineState::Stopped);
  agent.final();

  ZiResolver::init();
  ZuCheckRT(agent.init(cf, env()));
  ZuCheckRT(!agent.start()); // HTTP startup cannot use a stopped borrowed resolver
  ZuCheckRT(agent.state() == ZmEngineState::Stopped);
  ZuCheckRT(!agent.start()); // rollback permits another attempt without init
  ZuCheckRT(agent.stop());
  agent.final();
  ZuCheckRT(ZiResolver::instance()->initialized());
  ZiResolver::final();

  auto runtime = env();
  cf.loopbackTest = true;
  runtime.issuerURL = "http://127.0.0.1:1/oauth2/7";
  runtime.wssURL = "wss://127.0.0.1:1/ztc";
  runtime.ring = ZiTestResidue::uniqueName("agent-telemetry");
  runtime.pidDir = ZiFile::leafname(ZiTestResidue::tmpDir("agent-registry"));
  Ztc::Ring writer{ZiRingParams{runtime.ring, cf.telSize}};
  ZuCheckRT(writer.open(Ztc::Ring::Write) == Zi::OK);
  ZuCheckRT(agent.init(cf, ZuMv(runtime)));
  ZuCheckRT(agent.start());
  ZuCheckRT(agent.start());
  ZuCheckRT(agent.state() == ZmEngineState::Running);
  agent.provisioned(false);
  ZuCheckRT(agent.stop());
  ZuCheckRT(agent.stop());
  ZuCheckRT(agent.state() == ZmEngineState::Stopped);
  ZuCheckRT(agent.start());
  bool stopped = ZmBlock<bool>{}([&agent](auto wake) {
    agent.stop([&agent, wake = ZuMv(wake)](bool ok) mutable {
      if (ok) agent.final();
      wake(ok);
    });
  });
  ZuCheckRT(stopped);
}

static void configValidation()
{
  ZuTestScope(config_validation);
  auto cf = config();
  ZuCheck(cf.pubGCInterval == 1);
  ZuCheck(cf.pubGCBatch == 100);

  auto parsed = ZfCf::scan("vaultStore: keyring");
  Ztc::AgentCf loaded;
  loaded.maxFrame = 65536;
  ZfCf::handler<Ztc::AgentCf>(parsed.p<1>()).update(loaded);
  ZuCheck(loaded.telSpin == cf.telSpin);
  ZuCheck(loaded.maxFrame == 65536);
  ZuCheck(loaded.telLL == cf.telLL);
  ZuCheck(loaded.telTimeout == cf.telTimeout);
  ZuCheck(loaded.closeTimeout == cf.closeTimeout);
  ZuCheck(loaded.pingInterval == cf.pingInterval);
  ZuCheck(loaded.idleTimeout == cf.idleTimeout);

  Ztc::Agent missing;
  auto invalid = env();
  invalid.deviceID.null();
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  invalid = env();
  invalid.clientSecret.null();
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  invalid = env();
  invalid.issuerURL = "https://zum.example/";
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  invalid = env();
  invalid.wssURL = "https://ztchub.example/ztc";
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  invalid = env();
  invalid.wssURL = "wss://ztchub.example/ztc#fragment";
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  ZuCheck(missing.init(cf, env()));
  missing.final();
}

} // ZtcAgentTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZiTestResidue::init("ZtcAgentTest");
  ZiLog::init("ZtcAgentTest");
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  ZuTestMain();
  ZuTestCall(ZtcAgentTest_::httpLifecycle);
  ZuTestCall(ZtcAgentTest_::configValidation);
  ZuTestCall(ZtcAgentTest_::agentLifecycle);
  ZiLog::stop();
  return 0;
}
