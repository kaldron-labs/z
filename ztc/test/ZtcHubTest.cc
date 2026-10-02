//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ztchub_daemon.hh>

using namespace ZuTestUtil;

namespace ZtcHubTest_ {

static Ztc::HubdCf config()
{
  Ztc::HubdCf cf;
  cf.listeners.push(Ztc::ListenerCf{
    "127.0.0.1", "/ztc", "server.crt", "server.key", "/session", {}});
  cf.issuerURL = "https://zum.example/oauth2/7";
  cf.audience = "https://ztchub.example";
  cf.managementIssuer = "https://zum.example";
  cf.managementClientID = "ztchub-manager";
  cf.actions.push("Request");
  cf.actions.push("Telemetry");
  cf.roles.push("Client");
  cf.roles.push("Agent");
  cf.expectedAgents = 2;
  cf.activeFrontEnds = 2;
  cf.subscriptionsPerFrontEnd = 4;
  return cf;
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

static void protocol()
{
  ZuTestScope(protocol);
  auto check = [](ZuCSpan deviceID, uint64_t subID, unsigned group,
      bool subscribe, uint64_t generation) {
    Zfb::Builder builder;
    auto device = builder.CreateString(deviceID.data(), deviceID.length());
    auto request = Ztc::fbs::CreateRequest(builder, 9,
      Ztc::fbs::Group(group), 0, 1000, subscribe);
    builder.Finish(Ztc::saveMsg(builder,
      Ztc::fbs::Body::Request, request.Union(), subID, device, generation));
    return Ztc::Hubd::validFrontMessage(
      Ztc::fbs::GetMsg(builder.GetBufferPointer()));
  };
  ZuCheck(check("device-1", 7, 0, true, 0));
  ZuCheck(check("device-1", 7, 0, false, 0));
  ZuCheck(!check("", 7, 0, true, 0));
  ZuCheck(!check("device-1", 0, 0, true, 0));
  ZuCheck(!check("device-1", 7, 255, true, 0));
  ZuCheck(!check("device-1", 7, 0, true, 3));
  ZuCheck(!Ztc::Hubd::validFrontMessage(nullptr));
  ZuCheck(!Ztc::Hubd::validAgentMessage(nullptr));

  auto app = [](ZuCSpan deviceID, unsigned group, ZuCSpan filter) {
    Zfb::Builder builder;
    auto device = deviceID ?
      builder.CreateString(deviceID.data(), deviceID.length()) :
      flatbuffers::Offset<flatbuffers::String>{};
    auto filter_ = filter ?
      builder.CreateString(filter.data(), filter.length()) :
      flatbuffers::Offset<flatbuffers::String>{};
    auto request = Ztc::fbs::CreateRequest(builder, 0,
      Ztc::fbs::Group(group), filter_, 0, true);
    builder.Finish(Ztc::saveMsg(builder,
      Ztc::fbs::Body::Request, request.Union(), 7, device));
    return Ztc::Hubd::validAppRequest(
      Ztc::fbs::GetMsg(builder.GetBufferPointer()));
  };
  ZuCheck(app({}, unsigned(Ztc::fbs::Group::App), {}));
  ZuCheck(app("", unsigned(Ztc::fbs::Group::App), "*"));
  ZuCheck(!app("device-1", unsigned(Ztc::fbs::Group::App), {}));
  ZuCheck(!app({}, unsigned(Ztc::fbs::Group::Heap), {}));
  ZuCheck(!app({}, unsigned(Ztc::fbs::Group::App), "app-*"));

  Zfb::Builder inventoryBuilder;
  auto inventoryDevice = inventoryBuilder.CreateString("", 0);
  auto inventoryFilter = inventoryBuilder.CreateString("*");
  auto inventoryRequest = Ztc::fbs::CreateRequest(inventoryBuilder, 0,
    Ztc::fbs::Group::App, inventoryFilter, 1000, true);
  inventoryBuilder.Finish(Ztc::saveMsg(inventoryBuilder,
    Ztc::fbs::Body::Request, inventoryRequest.Union(), 7, inventoryDevice));
  ZuCheck(Ztc::Hubd::validAppRequest(
    Ztc::fbs::GetMsg(inventoryBuilder.GetBufferPointer())));
}

static void messages()
{
  ZuTestScope(messages);
  auto verify = [](auto make, Ztc::fbs::Body expected) {
    Zfb::Builder builder;
    auto body = make(builder);
    auto device = builder.CreateString("device-1");
    builder.Finish(Ztc::saveMsg(
      builder, expected, body.Union(), 7, device, 3));
    if (!Zfb::Verifier{builder.GetBufferPointer(), builder.GetSize()}.
          VerifyBuffer<Ztc::fbs::Msg>()) return false;
    auto msg = Ztc::fbs::GetMsg(builder.GetBufferPointer());
    return Ztc::Hubd::validAgentMessage(msg) &&
      msg->body_type() == expected && msg->subId() == 7 &&
      msg->agentGen() == 3 && msg->deviceId()->string_view() == "device-1";
  };
  ZuCheck(verify([](auto &builder) {
    return ZfbStruct::save(builder, Ztc::Ack{ZuID{"app"}, 9, 1000});
  }, Ztc::fbs::Body::Ack));
  ZuCheck(!verify([](auto &builder) {
    return ZfbStruct::save(builder, Ztc::Ack{ZuID{"app"}, 9, 1000, 255});
  }, Ztc::fbs::Body::Ack));
  ZuCheck(verify([](auto &builder) {
    auto id = builder.CreateString("app");
    Ztc::AppTelemetry value;
    auto valueOffset = ZfbStruct::save(builder, value);
    return Ztc::saveTelemetry(
      builder, id, 9, Ztc::fbs::TelemetryBody::AppTelemetry,
      valueOffset.Union());
  }, Ztc::fbs::Body::Telemetry));
  ZuCheck(verify([](auto &builder) {
    return ZfbStruct::save(builder, Ztc::EOS{ZuID{"app"}, 9});
  }, Ztc::fbs::Body::EOS));
  ZuCheck(verify([](auto &builder) {
    return ZfbStruct::save(builder,
      Ztc::Error{Ztc::ErrorMessage{"failed"}, ZuID{"app"}, 9, 1});
  }, Ztc::fbs::Body::Error));
}

static void routing()
{
  ZuTestScope(routing);
  Ztc::Hubd hub;
  ZuCheck(hub.init(config()));
  ZuCheck(hub.state() == Ztc::HubdState::Starting);
  ZuCheck(hub.start());
  ZuCheck(hub.state() == Ztc::HubdState::Up);

  auto agentPrincipal = principal("device-1", "Telemetry");
  auto agentPrincipal2 = principal("device-2", "Telemetry");
  auto browser = principal("user-1", "Request");
  auto wrongKind = browser;
  wrongKind.actions[0] = "Telemetry";
  ZuCheck(!Ztc::Hubd::authorized(wrongKind, "Telemetry"));
  wrongKind = agentPrincipal;
  wrongKind.actions[0] = "Request";
  ZuCheck(!Ztc::Hubd::authorized(wrongKind, "Request"));
  wrongKind.authMethod.null();
  ZuCheck(!Ztc::Hubd::authorized(wrongKind, "Request"));
  auto wrongAudience = agentPrincipal;
  wrongAudience.audience = "https://other.example";
  auto wrongIssuer = agentPrincipal;
  wrongIssuer.tokenID.issuerURL = "https://zum.example/oauth2/8";
  auto wrongDevice = agentPrincipal;
  wrongDevice.subject = "other-device";
  auto expired = agentPrincipal;
  expired.expires = Zm::now().sec();
  Ztc::HubString cookie;
  ZuCheck(hub.createBrowserSession(browser, cookie));
  ZuCheck(cookie);
  Zum::ServicePrincipal browserPrincipal;
  ZuCheck(hub.browserPrincipal(cookie, browserPrincipal));
  ZuCheck(browserPrincipal.subject == "user-1" &&
    browserPrincipal.tokenID.issuerURL == "https://zum.example/oauth2/7" &&
    browserPrincipal.tokenID.jti == "user-1" &&
    browserPrincipal.actions.length() == 1 &&
    browserPrincipal.actions[0] == "Request");
  ZuCheck(!hub.browserPrincipal("missing", browserPrincipal));

  auto combined = browser;
  combined.actions.push("Telemetry");
  ZuCheck(Ztc::Hubd::authorized(combined, "Request"));
  ZuCheck(!Ztc::Hubd::authorized(combined, "Telemetry"));

  ZuCheck(hub.addFrontend(20, browser, Ztc::HubSendFn{
    [](Ztc::HubFrame) { }}));
  ZuCheck(hub.addFrontend(22, browser, Ztc::HubSendFn{
    [](Ztc::HubFrame) { }}));

  Ztc::HubError::T error = Ztc::HubError::Unauthorized;
  ZuCheck(hub.addAgent(10, "device-1", 3, agentPrincipal, error));
  ZuCheck(hub.addAgent(12, "device-2", 3, agentPrincipal2, error));
  ZuCheck(!hub.addAgent(12, "device-2", 3, wrongAudience, error));
  ZuCheck(!hub.addAgent(12, "device-2", 3, wrongIssuer, error));
  ZuCheck(!hub.addAgent(13, "device-3", 3, wrongDevice, error));
  ZuCheck(!hub.addAgent(14, "device-3", 3, expired, error));
  ZuCheck(!hub.addAgent(11, "device-1", 4, agentPrincipal, error));
  ZuCheck(error == Ztc::HubError::DuplicateSub);

  uint64_t agent = 0, generation = 0, seq = 0;
  Ztc::Request snapshot;
  Ztc::Request continuing{.id = "publisher-1", .interval = 1000};
  ZuCheck(hub.addSubscription(20, 7, "device-1", snapshot, agent, generation, seq,
    error));
  ZuCheck(agent == 10 && generation == 3 && seq != 0);
  uint64_t unusedAgent = 0, unusedGeneration = 0, unusedSeq = 0;
  ZuCheck(!hub.addSubscription(20, 7, "device-2", snapshot, unusedAgent,
    unusedGeneration, unusedSeq, error));
  ZuCheck(error == Ztc::HubError::DuplicateSub);
  Ztc::RouteInfo route;
  ZuCheck(hub.route(10, 3, seq, route));
  ZuCheck(!route.continuing && route.fanout);
  ZuCheck(!hub.route(10, 4, seq, route));
  uint64_t agent2 = 0, generation2 = 0, seq2 = 0;
  ZuCheck(hub.addSubscription(22, 8, "device-1", continuing, agent2, generation2, seq2,
    error));
  ZuCheck(agent2 == 10 && generation2 == 3 && seq2 != seq &&
    hub.route(10, 3, seq2, route));
  ZuCheck(route.continuing && !route.fanout);
  ZuCheck(!hub.addSubscription(20, 7, "device-1", snapshot, agent, generation, seq,
    error));
  ZuCheck(error == Ztc::HubError::DuplicateSub);
  ZuCheck(hub.removeSession(20));
  ZuCheck(hub.removeSubscription(20, 7));
  ZuCheck(!hub.route(10, 3, seq, route) && hub.route(10, 3, seq2, route));
  bool completed = false;
  ZuCheck(hub.removeAgent(10, "device-1", 3,
    [&hub, &completed](Ztc::RouteInfo route) {
      completed = hub.removeSubscription(route.frontEndID, route.subID);
    }));
  ZuCheck(completed);
  ZuCheck(!hub.route(10, 3, 1, route));
  ZuCheck(!hub.route(10, 3, seq, route) && !hub.route(10, 3, seq2, route));
  ZuCheck(hub.stop());
  ZuCheck(hub.state() == Ztc::HubdState::Down);
}

static bool app(Ztc::Hubd &hub, uint64_t agent, uint64_t generation,
    ZuCSpan publisher, ZuCSpan version, uint64_t seqNo = 0)
{
  Zfb::Builder builder;
  auto id = builder.CreateString(publisher.data(), publisher.length());
  Ztc::AppTelemetry value;
  value.version = version;
  auto app_ = ZfbStruct::save(builder, value);
  auto telemetry = Ztc::saveTelemetry(builder, id, seqNo,
    Ztc::fbs::TelemetryBody::AppTelemetry, app_.Union());
  builder.Finish(Ztc::saveMsg(builder,
    Ztc::fbs::Body::Telemetry, telemetry.Union()));
  return hub.app(agent, generation,
    Ztc::fbs::GetMsg(builder.GetBufferPointer())->body_as_Telemetry());
}

static void appInventoryLimits()
{
  ZuTestScope(appInventoryLimits);
  auto cf = config();
  cf.publishersPerAgent = 1;
  Ztc::Hubd hub;
  ZuCheck(hub.init(cf) && hub.start());
  auto agentPrincipal = principal("device-1", "Telemetry");
  Ztc::HubError::T error = Ztc::HubError::BadReq;
  ZuCheck(hub.addAgent(10, "device-1", 3, agentPrincipal, error));
  ZuCheck(!app(hub, 10, 3, "publisher-1", "invalid", 1));
  ZuCheck(!app(hub, 10, 4, "publisher-1", "invalid"));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v1"));
  ZuCheck(!app(hub, 10, 3, "publisher-2", "v1"));
  ZuCheck(hub.removeAgent(10, "device-1", 3));
  ZuCheck(hub.addAgent(11, "device-1", 4, agentPrincipal, error));
  ZuCheck(app(hub, 11, 4, "publisher-2", "v2"));
  ZuCheck(hub.stop());
}

static void appInventory()
{
  ZuTestScope(appInventory);
  Ztc::Hubd hub;
  ZuCheck(hub.init(config()) && hub.start());
  auto agentPrincipal = principal("device-1", "Telemetry");
  auto browser = principal("user-1", "Request");
  Ztc::HubError::T error = Ztc::HubError::BadReq;
  ZuCheck(hub.addAgent(10, "device-1", 3, agentPrincipal, error));

  struct Frames {
    ZtArray<uint8_t> types;
    bool attributed = true;
    bool sourceGone = false;
    bool sourceShutdown = false;
    void operator ()(Ztc::HubFrame frame) {
      auto msg = Ztc::msg(ZuBSpan{frame->data(), frame->length});
      if (!msg) { attributed = false; return; }
      types.push(uint8_t(msg->body_type()));
      if (msg->body_type() == Ztc::fbs::Body::Telemetry) {
        auto telemetry = msg->body_as_Telemetry();
        attributed = attributed && msg->deviceId() &&
          msg->deviceId()->string_view() == "device-1" &&
          msg->agentGen() == 3 && telemetry && telemetry->id() &&
          telemetry->id()->string_view() == "publisher-1" &&
          !telemetry->seqNo() &&
          (telemetry->value_type() == Ztc::fbs::TelemetryBody::AppTelemetry ||
           telemetry->value_type() == Ztc::fbs::TelemetryBody::Shutdown);
        sourceShutdown |= telemetry && telemetry->value_type() ==
          Ztc::fbs::TelemetryBody::Shutdown;
      } else if (msg->body_type() == Ztc::fbs::Body::Error) {
        auto error = msg->body_as_Error();
        sourceGone = error && msg->deviceId() &&
          msg->deviceId()->string_view() == "device-1" &&
          msg->agentGen() == 3 && error->id() &&
          error->id()->string_view() == "publisher-1" &&
          error->code() == Ztc::HubError::AgentGone;
      } else if (msg->body_type() == Ztc::fbs::Body::EOS) {
        attributed &= !msg->deviceId() && !msg->agentGen() &&
          msg->body_as_EOS()->id()->string_view() == "inventory";
      }
    }
  } frames;
  ZuCheck(hub.addFrontend(20, browser, Ztc::HubSendFn{
    [&frames](Ztc::HubFrame frame) { frames(ZuMv(frame)); }}));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v1"));
  ZuCheck(hub.appSubscribe(20, 7, 0, error));
  ZuCheck(frames.attributed && frames.types.length() == 3 &&
    frames.types[0] == uint8_t(Ztc::fbs::Body::Telemetry) &&
    frames.types[1] == uint8_t(Ztc::fbs::Body::Ack) &&
    frames.types[2] == uint8_t(Ztc::fbs::Body::EOS));

  frames.types.length(0);
  ZuCheck(hub.appSubscribe(20, 8, 1000, error));
  ZuCheck(frames.types.length() == 3 &&
    frames.types[0] == uint8_t(Ztc::fbs::Body::Ack) &&
    frames.types[1] == uint8_t(Ztc::fbs::Body::Telemetry) &&
    frames.types[2] == uint8_t(Ztc::fbs::Body::EOS));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v2"));
  ZuCheck(frames.types.length() == 4 &&
    frames.types[3] == uint8_t(Ztc::fbs::Body::Telemetry));
  ZuCheck(hub.appRemove(10, 3, "publisher-1"));
  ZuCheck(frames.sourceShutdown && !frames.sourceGone && frames.attributed &&
    frames.types.length() == 5 &&
    frames.types[4] == uint8_t(Ztc::fbs::Body::Telemetry));
  ZuCheck(hub.appRemove(10, 3, "publisher-1"));
  ZuCheck(hub.appUnsubscribe(20, 8));
  ZuCheck(hub.appUnsubscribe(20, 8));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v3"));
  ZuCheck(hub.appSubscribe(20, 9, 1000, error));
  ZuCheck(hub.removeAgent(10, "device-1", 3));
  ZuCheck(frames.sourceGone && frames.attributed && frames.types.length() == 9 &&
    frames.types[8] == uint8_t(Ztc::fbs::Body::Error));
  ZuCheck(hub.stop());
}

static void configValidation()
{
  ZuTestScope(configValidation);
  auto cf = config();
  auto duplicate = cf.listeners[0];
  cf.listeners.push(ZuMv(duplicate));
  ZuCheck(!Ztc::Hubd{}.init(ZuMv(cf)));

  cf = config();
  cf.listeners[0].ssfPort = cf.listeners[0].port;
  ZuCheck(!Ztc::Hubd{}.init(ZuMv(cf)));


  cf = config();
  cf.controlBytes = cf.maxFrame;
  cf.telemetryBytes = cf.maxFrame;
  cf.queueMem = cf.maxFrame;
  ZuCheck(!Ztc::Hubd{}.init(ZuMv(cf)));
}

} // ZtcHubTest_

int main()
{
  ZuTestMain();
  ZuTestCall(ZtcHubTest_::protocol);
  ZuTestCall(ZtcHubTest_::messages);
  ZuTestCall(ZtcHubTest_::routing);
  ZuTestCall(ZtcHubTest_::appInventory);
  ZuTestCall(ZtcHubTest_::appInventoryLimits);
  ZuTestCall(ZtcHubTest_::configValidation);
}
