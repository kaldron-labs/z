//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ztcagent_daemon.hh>

using namespace ZuTestUtil;

namespace ZtcAgentTest_ {

static Ztc::AgentCf config()
{
  Ztc::AgentCf cf;
  cf.maxFrame = 1U << 20;
  cf.telBytes = 1U << 24;
  cf.reqBytes = 1U << 22;
  return cf;
}

static Ztc::AgentEnv env()
{
  return {
    .issuer = "https://zum.example/oauth2/7",
    .clientID = "device-client",
    .deviceID = "device-1",
    .credentialStore = "/run/secrets/ztcagent",
    .wssURL = "wss://ztchub.example/ztc",
    .accessToken = "access-token",
    .ring = "ztc-agent-test"
  };
}

static void configValidation()
{
  ZuTestScope(config_validation);
  auto cf = config();
  ZuCheck(cf.pubGCInterval == 1);
  ZuCheck(cf.pubGCBatch == 100);

  Ztc::Agent missing;
  auto invalid = env();
  invalid.deviceID.null();
  ZuCheck(!missing.init(cf, ZuMv(invalid)));

  invalid = env();
  invalid.issuer = "https://zum.example/";
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

int main()
{
  ZuTestMain();
  ZuTestCall(ZtcAgentTest_::configValidation);
}
