//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuBox.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/ztchub_daemon.hh>

using namespace ZuTestUtil;

static Ztc::HubdCf config()
{
  Ztc::HubdCf cf;
  cf.listeners.push(Ztc::ListenerCf{
    "127.0.0.1", "/ztc", "server.crt", "server.key", "/session", {}});
  cf.issuerURL = "https://zum.example/oauth2/7";
  cf.audience = "https://ztchub.example";
  cf.managementIssuer = "https://zum.example/oauth2/1";
  cf.managementClientID = "ztchub-manager";
  cf.actions.push("Request");
  cf.actions.push("Telemetry");
  cf.roles.push("Client");
  cf.roles.push("Agent");
  return cf;
}

static Ztc::HubString numbered(ZuCSpan prefix, unsigned value)
{
  Ztc::HubString result{prefix};
  result << ZuBox<unsigned>{value};
  return result;
}

static Zum::ServicePrincipal principal(ZuCSpan subject, ZuCSpan action)
{
  Zum::ServicePrincipal value;
  value.tokenID.issuerURL = "https://zum.example/oauth2/7";
  value.tokenID.jti = subject;
  value.subject = subject;
  value.audience = "https://ztchub.example";
  value.expires = Zm::now().sec() + 60;
  value.actions.push(action);
  value.authMethod = action == "Telemetry" ? "client_credentials" : "passkey";
  return value;
}

static void lifecycle()
{
  ZuTestScope(lifecycle);
  Ztc::Hubd hub;
  ZuCheck(hub.init(config()));
  auto manifest = hub.manifest();
  ZuCheck(manifest.revision == 1 && manifest.catalog.actions.length() == 2 &&
    manifest.catalog.roles.length() == 2);
  ZuCheck(hub.start() && hub.state() == Ztc::HubdState::Up);
  ZuCheck(hub.stop() && hub.state() == Ztc::HubdState::Down);
}

static void productionInit()
{
  ZuTestScope(productionInit);
  ZiMxParams params;
  params.scheduler([](auto &s) {
    s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); t.name("hubdb"); })
      .thread(4, [](auto &t) { t.isolated(1); t.name("hubstore"); });
  }).rxThread(1).txThread(2);
  ZiMultiplex mx{ZuMv(params)};
  ZuCheck(mx.start());
  auto cf = config();
  cf.managementURL = "https://zum.example";
  Zum::ServiceHTTPFn http{[](Zum::ServiceHTTPRequest,
      Zum::ServiceHTTPDoneFn) { }};
  Ztc::Hubd hub;
  ZuCheck(hub.init(ZuMv(cf), &mx, ZuMv(http), Zum::Bytes{"secret"},
    "Bearer callback"));
  ZuCheck(hub.state() == Ztc::HubdState::Starting);
  hub.final();
  ZuCheck(mx.stop());
}

// Cardinality and routing only; ztchubloadtest measures the running WSS hub.
static void routingScale()
{
  ZuTestScope(routingScale);
  auto cf = config();
  cf.maxFrame = 1U << 16;
  cf.controlBytes = 1U << 20;
  cf.telemetryBytes = 1U << 20;
  cf.queueMem = 1ULL << 32;
  cf.expectedAgents = 2048;
  cf.publishersPerAgent = 256;
  cf.activeFrontEnds = 32;
  cf.subscriptionsPerFrontEnd = 32;
  cf.schedulerTurnWork = 64;

  Ztc::Hubd hub;
  ZuCheck(hub.init(ZuMv(cf)) && hub.start());

  Ztc::HubError::T error = Ztc::HubError::Unauthorized;
  bool agentsOK = true;
  for (unsigned i = 0; i < cf.expectedAgents; ++i) {
    auto device = numbered("device-", i);
    auto value = principal(device, "Telemetry");
    if (!hub.addAgent(1000 + i, device, i + 1, value, error))
      agentsOK = false;
  }
  ZuCheck(agentsOK);

  bool frontEndsOK = true;
  for (unsigned i = 0; i < cf.activeFrontEnds; ++i) {
    auto value = principal(numbered("user-", i), "Request");
    if (!hub.addFrontend(10000 + i, value, Ztc::HubSendFn{
        [](Ztc::HubFrame) { }})) frontEndsOK = false;
  }
  ZuCheck(frontEndsOK);

  bool routesOK = true;
  for (unsigned i = 0; i < cf.activeFrontEnds && routesOK; ++i)
    for (unsigned j = 0; j < cf.subscriptionsPerFrontEnd; ++j) {
      uint64_t agent = 0, generation = 0, seqNo = 0;
      auto device = numbered("device-", (i * 32) + j);
      if (!hub.addSubscription(10000 + i, j + 1, device, agent,
          generation, seqNo, error)) {
        routesOK = false;
        break;
      }
    }
  ZuCheck(routesOK);

  for (unsigned i = 0; i < cf.activeFrontEnds; ++i)
    for (unsigned j = 0; j < cf.subscriptionsPerFrontEnd; ++j)
      ZuCheck(hub.removeSubscription(10000 + i, j + 1));

  Ztc::HubFrame frame = new ZiIOBufAlloc<1024, 1U << 30,
    "Ztc.Hub.Frame">;
  frame->length = 1;
  bool telemetryOK = true;
  uint64_t sent = 0;
  for (unsigned i = 0; i < cf.activeFrontEnds && telemetryOK; ++i)
    for (unsigned j = 0; j < cf.subscriptionsPerFrontEnd && telemetryOK; ++j) {
      uint64_t agent = 0, generation = 0, seqNo = 0;
      auto device = numbered("device-", (i * cf.subscriptionsPerFrontEnd) + j);
      if (!hub.addSubscription(10000 + i, 100000 + j, device, agent,
          generation, seqNo, error)) {
        telemetryOK = false;
        break;
      }
      Ztc::RouteInfo route;
      if (!hub.route(agent, generation, seqNo, route)) {
        telemetryOK = false;
        break;
      }
      for (unsigned publisher = 0; publisher < cf.publishersPerAgent;
          ++publisher) {
        if (!hub.sendFrontend(route.frontEndID, Ztc::HubFrame{frame})) {
          telemetryOK = false;
          break;
        }
        ++sent;
      }
      if (!hub.removeSubscription(10000 + i, 100000 + j))
        telemetryOK = false;
    }
  ZuCheck(telemetryOK && sent == uint64_t(cf.activeFrontEnds) *
    cf.subscriptionsPerFrontEnd * cf.publishersPerAgent);
  ZuCheck(hub.stop());
}

int main()
{
  ZuTestMain();
  ZuTestCall(lifecycle);
  ZuTestCall(productionInit);
  ZuTestCall(routingScale);
}
