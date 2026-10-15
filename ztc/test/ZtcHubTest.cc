//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>

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
    Ztc::Builder builder{new ZiIOBufAlloc<>()};
    auto nested = builder.startNested();
    auto request = Zfb::save(builder, Ztc::Request{
      .seqNo = 9, .interval = 1000, .group = uint8_t(group),
      .subscribe = subscribe});
    auto bytes = builder.finish(Ztc::saveMsg(builder, nested,
      Ztc::Body::Request, request, subID, deviceID, generation));
    return Ztc::Hubd::validFrontMessage(Zfb::view<Ztc::Msg>(bytes));
  };
  ZuCheck(check("device-1", 7, 0, true, 0));
  ZuCheck(check("device-1", 7, 0, false, 0));
  ZuCheck(!check("", 7, 0, true, 0));
  ZuCheck(!check("device-1", 0, 0, true, 0));
  ZuCheck(!check("device-1", 7, 255, true, 0));
  ZuCheck(!check("device-1", 7, 0, true, 3));
  ZuCheck(!Ztc::Hubd::validFrontMessage({}));
  ZuCheck(!Ztc::validMsg({}));

  auto app = [](ZuCSpan deviceID, unsigned group, ZuCSpan filter) {
    Ztc::Builder builder{new ZiIOBufAlloc<>()};
    auto nested = builder.startNested();
    auto request = Zfb::save(builder, Ztc::Request{
      .filter = Ztc::RequestFilter{filter}, .seqNo = 0,
      .group = uint8_t(group), .subscribe = true});
    auto bytes = builder.finish(Ztc::saveMsg(builder, nested,
      Ztc::Body::Request, request, 7, deviceID));
    return Ztc::Hubd::validAppRequest(Zfb::view<Ztc::Msg>(bytes));
  };
  ZuCheck(app({}, unsigned(Ztc::Group::App), {}));
  ZuCheck(app("", unsigned(Ztc::Group::App), "*"));
  ZuCheck(!app("device-1", unsigned(Ztc::Group::App), {}));
  ZuCheck(!app({}, unsigned(Ztc::Group::Heap), {}));
  ZuCheck(!app({}, unsigned(Ztc::Group::App), "app-*"));

  Ztc::Builder builder{new ZiIOBufAlloc<>()};
  auto nested = builder.startNested();
  auto request = Zfb::save(builder, Ztc::Request{
    .filter = "*", .seqNo = 0, .interval = 1000,
    .group = Ztc::Group::App, .subscribe = true});
  using PayloadH = Zfb::As<Ztc::Payload>::Handler<Ztc::Payload>;
  using MsgH = Zfb::As<Ztc::Msg>::Handler<Ztc::Msg>;
  auto payload = builder.startTable();
  PayloadH::fieldUnion<Ztc::ZfField(Payload, body)>(builder, payload,
    Zfb::UnionOffset<Ztc::BodyValue>{request, Ztc::Body::Request});
  auto data = builder.endNested(nested, builder.endTable(payload));
  auto empty = builder.string("");
  auto table = builder.startTable();
  MsgH::fieldOffset<Ztc::ZfField(Msg, data)>(builder, table, data);
  MsgH::field<Ztc::ZfField(Msg, subID)>(builder, table, uint64_t(7));
  MsgH::fieldOffset<Ztc::ZfField(Msg, deviceID)>(builder, table, empty);
  auto bytes = builder.finish(builder.endTable(table));
  auto msg = Ztc::msg(bytes);
  ZuCheck(msg && msg.has<Ztc::ZfField(Msg, deviceID)>());
  ZuCheck(Ztc::Hubd::validAppRequest(msg));
}

static void messages()
{
  ZuTestScope(messages);
  auto verify = [](auto make, unsigned expected) {
    Ztc::Builder builder{new ZiIOBufAlloc<>()};
    auto nested = builder.startNested();
    auto body = make(builder);
    auto bytes = builder.finish(Ztc::saveMsg(
      builder, nested, expected, body, 7, "device-1", 3));
    auto msg = Ztc::msg(bytes);
    return msg &&
      Ztc::msgBody(msg).tag() == expected &&
      msg.template get<Ztc::ZfField(Msg, subID)>() == 7 &&
      msg.template get<Ztc::ZfField(Msg, agentGen)>() == 3 &&
      msg.template get<Ztc::ZfField(Msg, deviceID)>() == "device-1";
  };
  ZuCheck(verify([](auto &builder) {
    return Zfb::save(builder, Ztc::Ack{ZuID{"app"}, 9, 1000});
  }, Ztc::Body::Ack));
  ZuCheck(!verify([](auto &builder) {
    return Zfb::save(builder, Ztc::Ack{ZuID{"app"}, 9, 1000, 255});
  }, Ztc::Body::Ack));
  ZuCheck(verify([](auto &builder) {
    auto id = builder.string("app");
    Ztc::AppTelemetry value;
    auto valueOffset = Zfb::save(builder, value);
    return Ztc::saveTelemetry(
      builder, id, 9, Ztc::TelemetryBody::AppTelemetry,
      valueOffset);
  }, Ztc::Body::Telemetry));
  ZuCheck(verify([](auto &builder) {
    return Zfb::save(builder, Ztc::EOS{ZuID{"app"}, 9});
  }, Ztc::Body::EOS));
  ZuCheck(verify([](auto &builder) {
    return Zfb::save(builder,
      Ztc::Error{Ztc::ErrorMessage{"failed"}, ZuID{"app"}, 9, 1});
  }, Ztc::Body::Error));
}

static void payloadCopy()
{
  ZuTestScope(payloadCopy);
  Ztc::Builder routed{new ZiIOBufAlloc<>()};
  ZuBSpan bytes;
  {
    Ztc::Builder source{new ZiIOBufAlloc<>()};
    auto nested = source.startNested();
    auto id = source.string("publisher");
    auto value = Zfb::save(source, Ztc::AppTelemetry{});
    auto telemetry = Ztc::saveTelemetry<true>(source, id, 0,
      Ztc::TelemetryBody::AppTelemetry, value);
    auto original = source.finish(Ztc::saveMsg(source, nested,
      Ztc::Body::Telemetry, telemetry));
    auto msg = Ztc::msg(original);
    ZuCheck(msg);
    auto payload = msg.get<Ztc::ZfField(Msg, data)>();
    auto copied = routed.copyRoot(payload, Ztc::Builder::Align);
    bytes = routed.finish(Ztc::saveMsg(routed, copied, 27, "device", 4));
    auto output = Ztc::msg(bytes);
    ZuCheck(output);
    auto emitted = output.get<Ztc::ZfField(Msg, data)>();
    ZuCheck(emitted.length() == payload.length() &&
      !memcmp(emitted.data(), payload.data(), payload.length()));
  }
  // The nested span remains independently usable after its source is freed.
  auto output = Ztc::msg(bytes);
  ZuCheck(output && output.get<Ztc::ZfField(Msg, subID)>() == 27);
  auto payload = output.get<Ztc::ZfField(Msg, data)>();
  ZuCheck(Zfb::verify<Ztc::Payload>(payload));
  auto telemetry = Ztc::msgBody(output).get<Ztc::Body::Telemetry>();
  ZuCheck(telemetry.has<Ztc::ZfField(Telemetry, seqNo)>() &&
    telemetry.get<Ztc::ZfField(Telemetry, seqNo)>() == 0);
  Zfb::write<Zfb::Offset>(const_cast<uint8_t *>(payload.data()), UINT32_MAX);
  ZuCheck(Zfb::verify<Ztc::Msg>(bytes));
  ZuCheck(!Ztc::msg(bytes));
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
  ZuCheck(!hub.browserPrincipal(cookie, browserPrincipal));
  Ztc::HubString unusedCookie;
  ZuCheck(hub.createBrowserSession(browser, unusedCookie));

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
  Ztc::Builder builder{new ZiIOBufAlloc<>()};
  auto nested = builder.startNested();
  auto id = builder.string(publisher);
  Ztc::AppTelemetry value;
  value.version = version;
  auto app_ = Zfb::save(builder, value);
  auto telemetry = Ztc::saveTelemetry(builder, id, seqNo,
    Ztc::TelemetryBody::AppTelemetry, app_);
  auto bytes = builder.finish(Ztc::saveMsg(builder, nested,
    Ztc::Body::Telemetry, telemetry));
  return hub.app(agent, generation,
    Ztc::msgBody(Zfb::view<Ztc::Msg>(bytes)).get<Ztc::Body::Telemetry>());
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
      types.push(uint8_t(Ztc::msgBody(msg).tag()));
      if (Ztc::msgBody(msg).tag() == Ztc::Body::Telemetry) {
	auto telemetry = Ztc::msgBody(msg).get<Ztc::Body::Telemetry>();
	attributed = attributed && msg.get<Ztc::ZfField(Msg, deviceID)>() &&
	  msg.get<Ztc::ZfField(Msg, deviceID)>() == "device-1" &&
	  msg.get<Ztc::ZfField(Msg, agentGen)>() == 3 && telemetry &&
	    telemetry.get<Ztc::ZfField(Telemetry, id)>() &&
	  telemetry.get<Ztc::ZfField(Telemetry, id)>() == "publisher-1" &&
	  !telemetry.get<Ztc::ZfField(Telemetry, seqNo)>() &&
	  (telemetry
	    .get<Ztc::ZfField(Telemetry, value)>().tag() == Ztc::TelemetryBody::AppTelemetry ||
	   telemetry.get<Ztc::ZfField(Telemetry, value)>().tag() == Ztc::TelemetryBody::Shutdown);
	sourceShutdown |= telemetry && telemetry.get<Ztc::ZfField(Telemetry, value)>().tag() ==
	  Ztc::TelemetryBody::Shutdown;
      } else if (Ztc::msgBody(msg).tag() == Ztc::Body::Error) {
	auto error = Ztc::msgBody(msg).get<Ztc::Body::Error>();
	sourceGone = error && msg.get<Ztc::ZfField(Msg, deviceID)>() &&
	  msg.get<Ztc::ZfField(Msg, deviceID)>() == "device-1" &&
	  msg.get<Ztc::ZfField(Msg, agentGen)>() == 3 && error.get<Ztc::ZfField(Error, id)>() &&
	  error.get<Ztc::ZfField(Error, id)>() == "publisher-1" &&
	  error.get<Ztc::ZfField(Error, code)>() == Ztc::HubError::AgentGone;
      } else if (Ztc::msgBody(msg).tag() == Ztc::Body::EOS) {
	attributed &= !msg.get<Ztc::ZfField(Msg, deviceID)>() &&
	  !msg.get<Ztc::ZfField(Msg, agentGen)>() &&
	  Ztc::msgBody(msg).get<Ztc::Body::EOS>().get<Ztc::ZfField(EOS, id)>() == "inventory";
      }
    }
  } frames;
  ZuCheck(hub.addFrontend(20, browser, Ztc::HubSendFn{
    [&frames](Ztc::HubFrame frame) { frames(ZuMv(frame)); }}));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v1"));
  ZuCheck(hub.appSubscribe(20, 7, 0, error));
  ZuCheck(frames.attributed && frames.types.length() == 3 &&
    frames.types[0] == uint8_t(Ztc::Body::Telemetry) &&
    frames.types[1] == uint8_t(Ztc::Body::Ack) &&
    frames.types[2] == uint8_t(Ztc::Body::EOS));

  frames.types.length(0);
  ZuCheck(hub.appSubscribe(20, 8, 1000, error));
  ZuCheck(frames.types.length() == 3 &&
    frames.types[0] == uint8_t(Ztc::Body::Ack) &&
    frames.types[1] == uint8_t(Ztc::Body::Telemetry) &&
    frames.types[2] == uint8_t(Ztc::Body::EOS));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v2"));
  ZuCheck(frames.types.length() == 4 &&
    frames.types[3] == uint8_t(Ztc::Body::Telemetry));
  ZuCheck(hub.appRemove(10, 3, "publisher-1"));
  ZuCheck(frames.sourceShutdown && !frames.sourceGone && frames.attributed &&
    frames.types.length() == 5 &&
    frames.types[4] == uint8_t(Ztc::Body::Telemetry));
  ZuCheck(hub.appRemove(10, 3, "publisher-1"));
  ZuCheck(hub.appUnsubscribe(20, 8));
  ZuCheck(hub.appUnsubscribe(20, 8));
  ZuCheck(app(hub, 10, 3, "publisher-1", "v3"));
  ZuCheck(hub.appSubscribe(20, 9, 1000, error));
  ZuCheck(hub.removeAgent(10, "device-1", 3));
  ZuCheck(frames.sourceGone && frames.attributed && frames.types.length() == 9 &&
    frames.types[8] == uint8_t(Ztc::Body::Error));
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
  ZuTestCall(ZtcHubTest_::payloadCopy);
  ZuTestCall(ZtcHubTest_::routing);
  ZuTestCall(ZtcHubTest_::appInventory);
  ZuTestCall(ZtcHubTest_::appInventoryLimits);
  ZuTestCall(ZtcHubTest_::configValidation);
}
