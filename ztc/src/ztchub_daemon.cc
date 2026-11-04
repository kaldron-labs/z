//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/ztchub_daemon.hh>

#include <limits.h>
#include <stdlib.h>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/Zdb.hh>
#include <zlib/ZdbMemStore.hh>

#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>

namespace Ztc {
namespace Hubd_ {

struct RoleKeys {
  using Keys = ZuStringTL<"Client", "Agent">;
};

static constexpr auto roleMatcher = ZuMatcher<RoleKeys>();

static bool userAuthMethod(ZuCSpan method)
{
  switch (method.length()) {
    case 7: return method == "passkey";
    case 4: return method == "oidc";
    default: return false;
  }
}

struct Agent {
  Agent(uint64_t sessionID_, ZuCSpan deviceID_, uint64_t generation_) :
    sessionID{sessionID_}, deviceID{deviceID_}, generation{generation_} { }

  uint64_t	sessionID;
  HubString	deviceID;
  uint64_t	generation;
  uint32_t	appCount = 0;
};

struct BrowserSession_ : public ZmObject {
  Zum::ServicePrincipal principal;
  HubString cookie;
  ZmScheduler::Timer expiry;
};
inline const HubString &BrowserCookieAxor(const BrowserSession_ &session) {
  return session.cookie;
}
ZmHashDerive(BrowserSessions, BrowserSession_,
  (ZmHashNode<BrowserSession_,
    ZmHashKey<BrowserCookieAxor,
      ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.BrowserSession">>>>));
using BrowserSession = BrowserSessions::Node;

inline ZuCSpan Agent_KeyAxor(const Agent &agent) { return agent.deviceID; }
ZmHashDerive(Agents, Agent,
  (ZmHashNode<Agent,
    ZmHashKey<Agent_KeyAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Ztc.Hub.Agent">>>>));

struct AgentSession {
  HubString deviceID;
  uint64_t generation = 0;
};
using AgentSessions = ZmHashKV<uint64_t, AgentSession,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.AgentSess">>>;

struct Inventory {
  HubString	deviceID;
  HubString	publisherID;
  AppTelemetry	app;
  uint64_t	generation = 0;
};
inline auto Inventory_KeyAxor(const Inventory &value) {
  return ZuFwdTuple(value.deviceID, value.generation, value.publisherID);
}
ZmHashDerive(Inventories, Inventory,
  (ZmHashNode<Inventory,
    ZmHashKey<Inventory_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Ztc.Hub.Inventory">>>>));

struct AppSub {
  uint64_t	frontEndID = 0;
  uint64_t	subID = 0;
};
inline auto AppSub_KeyAxor(const AppSub &value) {
  return ZuFwdTuple(value.frontEndID, value.subID);
}
ZmHashDerive(AppSubs, AppSub,
  (ZmHashNode<AppSub,
    ZmHashKey<AppSub_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Ztc.Hub.AppSub">>>>));

struct Route {
  uint64_t	frontEndID;
  uint64_t	subID;
  uint64_t	agentSessionID;
  uint64_t	agentGeneration;
	uint64_t	requestSeqNo;
	HubString	deviceID;
  bool		continuing;
  bool		fanout;
};

using RouteKey = ZuTuple<uint64_t, uint64_t>;

inline RouteKey Route_SubKeyAxor(const Route &route) {
  return RouteKey{route.frontEndID, route.subID};
}
inline RouteKey Route_AgentKeyAxor(const Route &route) {
  return RouteKey{route.agentSessionID, route.requestSeqNo};
}
ZmHashDerive(Subs, Route,
  (ZmHashNode<Route,
    ZmHashKey<Route_SubKeyAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Ztc.Hub.Sub">>>>));

using AgentReqs = ZmHashKV<RouteKey, Route *,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.AgentReq">>>;
using AgentReqVec = ZtArray<uint64_t,
  ZtArrayHeapID<"Ztc.Hub.AgentReqVec">>;
using AgentReqIdx = ZmHashKV<uint64_t, AgentReqVec,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.AgentReqIdx">>>;
using SubVec = ZtArray<uint64_t,
  ZtArrayHeapID<"Ztc.Hub.SubVec">>;
using SubIdx = ZmHashKV<uint64_t, SubVec,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.SubIdx">>>;

struct Session {
  HubSendFn send;
  Zum::TokenID tokenID;
  bool agent = false;
};
using Sessions = ZmHashKV<uint64_t, Session,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.Session">>>;
struct TokenSession {
  uint64_t sessionID = 0;
};
using TokenSessions = ZtArray<TokenSession,
  ZtArrayHeapID<"Ztc.Hub.TokenSessions">>;
using TokenIdx = ZmHashKV<Zum::TokenID, TokenSessions,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.TokenIdx">>>;

struct StateData {
  Hubd			*owner = nullptr;
	HubdCf		cf;
  HubdState::T		state = HubdState::Down;
  Agents		agents;
  Inventories		inventories;
  AppSubs	appSubs;
  Subs			subs;
  AgentReqs		agentReqs;
  AgentReqIdx		agentReqIdx;
  SubIdx		subIdx;
  AgentSessions		agentSessions;
  Sessions		sessions;
  TokenIdx		tokenIdx;
  BrowserSessions	browserSessions;
  ZmRef<Zdb>		db;
  ZuPtr<const ZfCf::AnyNode>	dbCf;
  uint64_t		nextRequestSeqNo = 1;
  uint32_t		agentCount = 0;
  uint32_t		frontEndCount = 0;
  ZiMultiplex		*multiplex = nullptr;
  Ztls::Random		random;
  Zum::Service		service;
  HubActivateFn		activateFn;
  bool			serviceInit = false;
  bool			rollback = false;
  bool			draining = false;
  bool			drainPosted = false;
  bool			stopOK = true;
  bool			roleUp = false;
  bool			roleDraining = false;
  int			startError = Zum::ServiceError::OK;
  ZmAtomic<unsigned>	posts = 0;
};

struct State : public ZmNHeap<State>, public ZmObject, public StateData {
friend inline ZuStringT<"Ztc.Hub.State"> ZmNHeapID(State *);

  void activate(bool active, HubCtrlFn complete = {}) {
    if (!activateFn) { if (complete) complete(true); return; }
    ++posts; // includes the external hub transition through its owner reply
    activateFn(active, [this, active, complete = ZuMv(complete)](bool ok) mutable {
      post([this, active, ok, complete = ZuMv(complete)]() mutable {
        --posts;
        if (!ok) state = HubdState::Down;
        if (!active && !ok) stopOK = false;
        if (complete) complete(ok);
      });
    });
  }

  void clearRole();

  template <typename L> void post(L &&fn) {
    ++posts;
    auto run = [this, fn = ZuFwd<L>(fn)]() mutable {
      { auto complete = ZuMv(fn); complete(); }
      --posts;
      owner->drain_();
    };
    if (multiplex) multiplex->rxRun(ZuMv(run));
    else run();
  }
};

struct HubDB : public Zdb {
  HubDB(State *state_) : state{state_} { }

  State *state;
};

// Remove at most budget owning nodes per mixed-worker turn. Hash iterators
// end before each deletion; AgentReqs aliases Routes and retires before Subs.
static bool clearRouting(StateData &state, unsigned budget)
{
  auto clear = [&budget](auto &hash) {
    while (budget && hash.count_()) {
      typename ZuDecay<decltype(hash)>::Node *node = nullptr;
      { auto i = hash.iter(); node = i(); }
      ZmAssert_(node);
      hash.delNode(node);
      --budget;
    }
    return !hash.count_();
  };
  while (budget && state.browserSessions.count_()) {
    BrowserSession *node = nullptr;
    { auto i = state.browserSessions.iter(); node = i(); }
    if (state.multiplex) state.multiplex->del(&node->expiry);
    state.browserSessions.delNode(node);
    --budget;
  }
  bool done = !state.browserSessions.count_() &&
    clear(state.agentReqs) && clear(state.agentReqIdx) &&
    clear(state.subIdx) && clear(state.subs) &&
    clear(state.appSubs) && clear(state.inventories) &&
    clear(state.agentSessions) && clear(state.agents) &&
    clear(state.tokenIdx) && clear(state.sessions);
  if (done) state.agentCount = state.frontEndCount = 0;
  return done;
}

void State::clearRole()
{
  if (!clearRouting(*this, cf.schedulerTurnWork)) {
    post([this]() { clearRole(); });
    return;
  }
  // Start a new role only after the previous listener generation and all its
  // routing state have retired. Runtime stop joins these counted posts.
  roleDraining = false;
  if (roleUp && owner->state() == ZmEngineState::Running) {
    state = HubdState::Up;
    activate(true);
  }
}


static HubBuildFn inventoryFrame(const Inventory &value, uint64_t subID)
{
  return HubBuildFn{[&value, subID](HubFrame frame, unsigned tailRoom) {
    Ztc::Builder builder{ZuMv(frame), Ztc::Builder::Align, UINT32_MAX, tailRoom};
    auto nested = builder.startNested();
    auto publisher = builder.string(value.publisherID);
    auto app = Zfb::save(builder, value.app);
    auto telemetry = saveTelemetry(builder, publisher, 0,
      TelemetryBody::AppTelemetry, app);
    builder.finish(saveMsg(builder, nested, Body::Telemetry, telemetry,
      subID, value.deviceID, value.generation));
    return HubPacket{builder.buf()};
  }};
}

static HubBuildFn inventoryAck(uint64_t subID, uint32_t interval)
{
  return HubBuildFn{[subID, interval](HubFrame frame, unsigned tailRoom) {
    Ztc::Builder builder{ZuMv(frame), Ztc::Builder::Align, UINT32_MAX, tailRoom};
    auto nested = builder.startNested();
    auto ack = Zfb::save(builder, Ack{
      .id = "inventory", .seqNo = 0, .interval = interval,
      .status = uint8_t(AckStatus::OK)});
    builder.finish(saveMsg(builder, nested, Body::Ack, ack, subID));
    return HubPacket{builder.buf()};
  }};
}

static HubBuildFn inventoryEOS(uint64_t subID)
{
  return HubBuildFn{[subID](HubFrame frame, unsigned tailRoom) {
    Ztc::Builder builder{ZuMv(frame), Ztc::Builder::Align, UINT32_MAX, tailRoom};
    auto nested = builder.startNested();
    auto eos = Zfb::save(builder, EOS{ZuID{"inventory"}, 0});
    builder.finish(saveMsg(builder, nested, Body::EOS, eos, subID));
    return HubPacket{builder.buf()};
  }};
}

static HubBuildFn inventoryGone(const Inventory &value, uint64_t subID)
{
  return HubBuildFn{[&value, subID](HubFrame frame, unsigned tailRoom) {
    Ztc::Builder builder{ZuMv(frame), Ztc::Builder::Align, UINT32_MAX, tailRoom};
    auto nested = builder.startNested();
    auto error = Zfb::save(builder, Error{
      ErrorMessage{"agent disconnected"}, ZuID{value.publisherID},
      0, int(HubError::AgentGone)});
    builder.finish(saveMsg(builder, nested, Body::Error, error,
      subID, value.deviceID, value.generation));
    return HubPacket{builder.buf()};
  }};
}

// A confirmed publisher removal is Shutdown, never snapshot completion.
static HubBuildFn inventoryShutdown(const Inventory &value, uint64_t subID)
{
  return HubBuildFn{[&value, subID](HubFrame frame, unsigned tailRoom) {
    Ztc::Builder builder{ZuMv(frame), Ztc::Builder::Align, UINT32_MAX, tailRoom};
    auto nested = builder.startNested();
    auto publisher = builder.string(value.publisherID);
    auto shutdown = Zfb::save(builder, Shutdown{});
    auto telemetry = saveTelemetry(builder, publisher, 0,
      TelemetryBody::Shutdown, shutdown);
    builder.finish(saveMsg(builder, nested, Body::Telemetry, telemetry,
      subID, value.deviceID, value.generation));
    return HubPacket{builder.buf()};
  }};
}

static void inventorySend(StateData &state, const Inventory &value)
{
  auto i = state.appSubs.iter();
  while (auto sub = i()) {
    auto session = state.sessions.findPtr(sub->val().frontEndID);
    if (session && !session->val().agent && session->val().send)
      session->val().send(HubPacket{}, inventoryFrame(value, sub->val().subID));
  }
}

static bool inventoryDel(StateData &state, ZuCSpan deviceID,
    uint64_t generation, ZuCSpan publisherID = {})
{
  bool removed = false;
  auto i = state.inventories.iter();
  while (auto node = i()) {
    auto &value = node->val();
    if (value.deviceID != deviceID || value.generation != generation ||
        (publisherID && value.publisherID != publisherID)) continue;
    auto subs = state.appSubs.iter();
    while (auto sub = subs()) {
      auto session = state.sessions.findPtr(sub->val().frontEndID);
      if (session && !session->val().agent && session->val().send)
	session->val().send(HubPacket{}, publisherID ?
	  inventoryShutdown(value, sub->val().subID) :
	  inventoryGone(value, sub->val().subID));
    }
    i.del();
    removed = true;
    if (publisherID) break;
  }
  if (removed) {
    auto agent = state.agents.findPtr(deviceID);
    if (agent && agent->val().generation == generation &&
        agent->val().appCount) --agent->val().appCount;
  }
  return removed;
}

static bool checkedCapacity(const HubdCf &cf)
{
  if (!cf.expectedAgents || !cf.publishersPerAgent || !cf.activeFrontEnds ||
      !cf.subscriptionsPerFrontEnd || cf.minRefreshMS < 1000 ||
      !cf.fanoutSLOMS || !cf.schedulerTurnWork || cf.controlFrames < 2 ||
      !cf.telemetryFrames || !cf.maxFrame ||
      cf.controlBytes < uint64_t(cf.maxFrame) * 2 ||
      cf.telemetryBytes < cf.maxFrame) return false;
  uint64_t routes = uint64_t(cf.activeFrontEnds) * cf.subscriptionsPerFrontEnd;
  if (cf.controlBytes > UINT64_MAX - cf.telemetryBytes) return false;
  auto bytes = cf.controlBytes + cf.telemetryBytes;
  return routes <= cf.queueMem / bytes;
}

static bool hasAction(const Zum::ServicePrincipal &principal, ZuCSpan action)
{
  for (auto &value: principal.actions) if (value == action) return true;
  return false;
}

static bool browserCookie(Ztls::Random &random, HubString &cookie)
{
  ZuBArray<32> raw(32, false);
  if (!random.random(raw)) return false;
  cookie.length(ZuBase64URL::enclen(raw.length()));
  cookie.length(ZuBase64URL::encode(cookie.span(), raw));
  return true;
}

static bool addTokenSession(
    StateData &state, const Zum::TokenID &tokenID, uint64_t sessionID)
{
  if (!tokenID.issuerURL || !tokenID.jti || !sessionID) return false;
  auto node = state.tokenIdx.findPtr(tokenID);
  if (!node) {
    TokenSessions sessions;
    sessions.push(TokenSession{sessionID});
    state.tokenIdx.add(tokenID, ZuMv(sessions));
    return true;
  }
  auto &sessions = node->val();
  for (auto &value: sessions) if (value.sessionID == sessionID) return true;
  sessions.push(TokenSession{sessionID});
  return true;
}

static void removeTokenSession(
    StateData &state, const Zum::TokenID &tokenID, uint64_t sessionID)
{
  if (!tokenID.issuerURL || !tokenID.jti || !sessionID) return;
  auto node = state.tokenIdx.findPtr(tokenID);
  if (!node) return;
  auto &sessions = node->val();
  for (unsigned i = 0; i < sessions.length(); ++i) {
    if (sessions[i].sessionID != sessionID) continue;
    sessions[i] = sessions[sessions.length() - 1];
    sessions.length(sessions.length() - 1);
    break;
  }
  if (!sessions.length()) state.tokenIdx.delNode(node);
}

static bool addSession(
    StateData &state, uint64_t sessionID, const Zum::TokenID &tokenID,
    HubSendFn send, bool agent = false)
{
  if (!sessionID || !addTokenSession(state, tokenID, sessionID))
    return false;
  state.sessions.add(sessionID,
    Session{ZuMv(send), tokenID, agent});
  return true;
}

static void removeSession(StateData &state, uint64_t sessionID)
{
  auto node = state.sessions.findPtr(sessionID);
  if (!node) return;
  if (!node->val().agent && state.frontEndCount) --state.frontEndCount;
  removeTokenSession(state, node->val().tokenID, sessionID);
  state.sessions.delNode(node);
}

static bool addAgentReq(StateData &shard, uint64_t sessionID,
    uint64_t requestSeqNo)
{
  auto node = shard.agentReqIdx.findPtr(sessionID);
  if (!node) {
    AgentReqVec requests;
    requests.push(requestSeqNo);
    shard.agentReqIdx.add(sessionID, ZuMv(requests));
    return true;
  }
  node->val().push(requestSeqNo);
  return true;
}

static void removeAgentReq(StateData &shard, uint64_t sessionID,
    uint64_t requestSeqNo)
{
  auto node = shard.agentReqIdx.findPtr(sessionID);
  if (!node) return;
  auto &requests = node->val();
  for (unsigned i = 0; i < requests.length(); ++i) {
    if (requests[i] != requestSeqNo) continue;
    requests[i] = requests[requests.length() - 1];
    requests.length(requests.length() - 1);
    break;
  }
  if (!requests.length()) shard.agentReqIdx.delNode(node);
}

static void addSubIndex(StateData &shard, uint64_t sessionID,
    uint64_t subID)
{
  auto node = shard.subIdx.findPtr(sessionID);
  if (!node) {
    SubVec subscriptions;
    subscriptions.push(subID);
    shard.subIdx.add(sessionID, ZuMv(subscriptions));
    return;
  }
  node->val().push(subID);
}

static void removeSubIndex(StateData &shard, uint64_t sessionID,
    uint64_t subID)
{
  auto node = shard.subIdx.findPtr(sessionID);
  if (!node) return;
  auto &subscriptions = node->val();
  for (unsigned i = 0; i < subscriptions.length(); ++i) {
    if (subscriptions[i] != subID) continue;
    subscriptions[i] = subscriptions[subscriptions.length() - 1];
    subscriptions.length(subscriptions.length() - 1);
    break;
  }
  if (!subscriptions.length()) shard.subIdx.delNode(node);
}

static RouteInfo routeInfo(const Route &route)
{
  return {route.frontEndID, route.subID, route.agentSessionID,
    route.agentGeneration, route.requestSeqNo, route.deviceID,
    route.continuing, route.fanout};
}

static bool delRoute(StateData &state, RouteKey key, HubRouteFn routeFn = {})
{
  auto route = state.subs.findPtr(key);
  if (!route) return true;
  if (routeFn) routeFn(routeInfo(route->val()));
  // A callback may complete the route synchronously.
  route = state.subs.findPtr(key);
  if (!route) return true;
  auto &value = route->val();
  state.agentReqs.del(RouteKey{value.agentSessionID, value.requestSeqNo});
  removeAgentReq(state, value.agentSessionID, value.requestSeqNo);
  removeSubIndex(state, value.frontEndID, value.subID);
  state.subs.delNode(route);
  return true;
}

static unsigned disconnectAgent(State &state, uint64_t sessionID,
    uint64_t generation, HubRouteFn routeFn)
{
  auto agent = state.agentSessions.findPtr(sessionID);
  if (!agent || agent->val().generation != generation) return 0;
  unsigned n = 0;
  auto limit = state.multiplex ? state.cf.schedulerTurnWork : UINT_MAX;
  for (;;) {
    auto index = state.agentReqIdx.findPtr(sessionID);
    if (!index || !index->val().length()) break;
    if (n == limit) {
      state.post([state = &state, sessionID,
          generation, routeFn = ZuMv(routeFn)]() mutable {
        if (state->owner->state() != ZmEngineState::Running) return;
        disconnectAgent(*state, sessionID, generation, ZuMv(routeFn));
      });
      return n;
    }
    auto seq = index->val()[index->val().length() - 1];
    auto route = state.agentReqs.findVal(RouteKey{sessionID, seq});
    if (route) delRoute(state, Route_SubKeyAxor(*route), routeFn);
    else removeAgentReq(state, sessionID, seq);
    ++n;
  }
  inventoryDel(state, agent->val().deviceID, generation);
  state.agents.del(agent->val().deviceID);
  state.agentSessions.delNode(agent);
  if (state.agentCount) --state.agentCount;
  return n;
}

} // Hubd_

Hubd::Hubd() = default;

static void dbUp(Zdb *, ZdbHost *);
static void dbDown(Zdb *, bool);

static bool initDB(Hubd_::State &state, ZiMultiplex *mx)
{
  const char *hosts = ::getenv("ZTCHUB_HOSTS");
  const char *hostID = ::getenv("ZTCHUB_HOSTID");
  if (bool(hosts) != bool(hostID) ||
      (hosts && (!*hosts || !*hostID))) return false;
  ZtString<> text;
  text << "thread: hubdb, shards: 1, store: {thread: hubstore}, hostID: "
    << (hostID ? hostID : "ztchub") << ", hosts: "
    << (hosts ? hosts : "{ztchub: {standalone: true}}")
    << ", tables: {}\n";
  auto source = ZfCf::scan(text);
  if (!source) return false;
  state.dbCf = ZuMv(source.p<1>());
  try {
    auto config = ZdbCf{state.dbCf};
    state.db = new Hubd_::HubDB{&state};
    state.db->init(ZuMv(config), mx,
      ZdbHandler{.upFn = dbUp, .downFn = dbDown},
      ZmRef<Zdb_::Store>{new ZdbMem::Store});
  } catch (...) {
    state.db = nullptr;
    state.dbCf = nullptr;
    return false;
  }
  return true;
}

static uint32_t ssfPort(const ListenerCf &listener)
{
  return listener.ssfPort ? listener.ssfPort : listener.port + 1;
}

static void dbUp(Zdb *db, ZdbHost *)
{
  auto state = static_cast<Hubd_::HubDB *>(db)->state;
  state->post([state]() {
    state->roleUp = true;
    if (state->roleDraining) return;
    switch (state->owner->state()) {
      case ZmEngineState::Starting:
      case ZmEngineState::StopPending:
        state->state = HubdState::Up;
        break;
      case ZmEngineState::Running:
        state->state = HubdState::Up;
        state->activate(true);
        break;
    }
  });
}

static void dbDown(Zdb *db, bool)
{
  auto state = static_cast<Hubd_::HubDB *>(db)->state;
  state->post([state]() {
    state->roleUp = false;
    state->state = HubdState::Down;
    if (state->roleDraining) return;
    state->roleDraining = true;
    state->activate(false, [state](bool) { state->clearRole(); });
  });
}

Hubd::~Hubd() { final(); }

bool Hubd::init(HubdCf cf)
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  if (m_state || !cf.listeners.length() || !cf.issuerURL || !cf.audience ||
      !cf.managementIssuer || !cf.managementClientID ||
      !cf.ssfCallbackPath || cf.ssfCallbackPath[0] != '/' ||
      !cf.schedulerTurnWork ||
      !Hubd_::checkedCapacity(cf)) return false;
  for (auto &listener: cf.listeners)
    if (!listener.bind || !listener.path || !listener.cert || !listener.key ||
        !listener.browserPath || (listener.port == 65535 && !listener.ssfPort))
      return false;
  for (auto &listener: cf.listeners)
    if (listener.path[0] != '/' || listener.browserPath[0] != '/') return false;
  for (auto &listener: cf.listeners)
    if (listener.ssfPort == listener.port) return false;
  for (unsigned i = 0; i < cf.listeners.length(); ++i)
    for (unsigned j = i + 1; j < cf.listeners.length(); ++j) {
      auto &a = cf.listeners[i];
      auto &b = cf.listeners[j];
      if (a.bind != b.bind) continue;
      auto aSSF = ssfPort(a);
      auto bSSF = ssfPort(b);
      if (a.port == b.port || a.port == bSSF || aSSF == b.port ||
          aSSF == bSSF) return false;
    }
  auto state = new Hubd_::State;
  if (!state->random.init()) {
    delete state;
    return false;
  }
  state->cf = ZuMv(cf);
  state->owner = this;
  m_state = state;
  return true;
}

bool Hubd::init(HubdCf cf, ZiMultiplex *scheduler, Zum::ServiceHTTPFn http,
    Zum::Bytes clientSecret, ZuCSpan callbackAuth)
{
  if (!scheduler || !http || !clientSecret || !callbackAuth ||
      !cf.managementURL) return false;
  if (!init(ZuMv(cf))) return false;
  auto &state = *m_state;
  if (!initDB(state, scheduler)) {
    final();
    return false;
  }
  state.multiplex = scheduler;
  Zum::ServiceConfig serviceCf{
    .scheduler = scheduler,
    .sid = scheduler->rxThread(),
    .issuerURL = state.cf.issuerURL,
    .managementIssuerURL = state.cf.managementIssuer,
    .managementURL = state.cf.managementURL,
    .clientID = state.cf.managementClientID,
    .clientSecret = ZuMv(clientSecret),
    .audience = state.cf.audience,
    .responseMax = state.cf.maxFrame,
    .ssf = Zum::ServiceSSFConfig{
      .enabled = true,
      .receiverID = state.cf.ssfDeliveryURL,
      .deliveryURL = state.cf.ssfDeliveryURL,
      .callbackAuth = callbackAuth,
      .transmitterIssuer = state.cf.issuerURL,
      .audience = state.cf.audience,
      .lease = state.cf.ssfLease,
      .maxBytes = state.cf.maxFrame}}
  ;
  if (!state.service.init(ZuMv(serviceCf), ZuMv(http))) {
    final();
    return false;
  }
  state.serviceInit = true;
  return true;
}

static bool validManifest(const Hubd_::State &state)
{
  if (state.cf.actions.length() != 2 || state.cf.roles.length() != 2) return false;
  auto action0 = state.cf.actions[0];
  auto action1 = state.cf.actions[1];
  auto role0 = state.cf.roles[0];
  auto role1 = state.cf.roles[1];
  return action0 != action1 && role0 != role1 &&
    ((action0 == "Request" && action1 == "Telemetry") ||
      (action0 == "Telemetry" && action1 == "Request")) &&
    ((role0 == "Client" && role1 == "Agent") ||
      (role0 == "Agent" && role1 == "Client"));
}

ZiMultiplex *Hubd::mx_() const
{
  return m_state ? m_state->multiplex : nullptr;
}

void Hubd::start_()
{
  if (!m_state) { Engine::started(false); return; }
  post_([this]() { startRx_(); });
}

void Hubd::startRx_()
{
  m_state->startError = Zum::ServiceError::OK;
  if (!validManifest(*m_state)) {
    m_state->startError = Zum::ServiceError::Invalid;
    Engine::started(false);
    return;
  }
  if (!m_state->serviceInit) {
    m_state->state = HubdState::Up;
    Engine::started(true);
    return;
  }
  m_state->service.start([this](bool ok) {
    post_([this, ok]() {
      if (!ok) {
        m_state->startError = m_state->service.startError();
        stopRx_(true);
        return;
      }
      m_state->service.publish(manifest(), [this](Zum::ServiceProtocolResult result) {
        int error = result.error;
        post_([this, error]() {
          if (error != Zum::ServiceError::OK) {
            m_state->startError = Zum::ServiceError::Unavailable;
            stopRx_(true);
            return;
          }
          startDB_();
        });
      });
    });
  });
}

void Hubd::startDB_()
{
  if (!m_state->db) {
    m_state->state = HubdState::Up;
    Engine::started(true);
    return;
  }
  m_state->db->start([this](bool ok) {
    post_([this, ok]() {
      if (!ok) {
        m_state->startError = Zum::ServiceError::Unavailable;
        stopRx_(true);
        return;
      }
      // DB startup and role activation differ. dbUp owns readiness; a standby
      // is Running with readiness Down.
      // Queue activation before publishing completion, so callbacks can stop
      // safely without any owner access after started(). The queued role check
      // suppresses activation if stop/down wins before this continuation.
      auto state = m_state.ptr();
      state->post([state]() {
        if (state->owner->state() == ZmEngineState::Running &&
            state->roleUp && !state->roleDraining) {
          state->state = HubdState::Up;
          state->activate(true);
        }
      });
      Engine::started(true);
    });
  });
}

void Hubd::stop_()
{
  post_([this]() { stopRx_(false); });
}

void Hubd::stopRx_(bool failed)
{
  m_state->stopOK = true;
  if (!m_state->db) { stopService_(failed); return; }
  // A failure is diagnostic; all remaining shutdown steps must still run.
  m_state->db->stop([this, failed](bool ok) {
    post_([this, failed, ok]() {
      if (!ok) m_state->stopOK = false;
      stopService_(failed);
    });
  });
}

void Hubd::stopService_(bool failed)
{
  // Also joins a role transition when no dbDown callback was needed (e.g.
  // failed startup or an already inactive DB). Engine stop is idempotent.
  m_state->activate(false);
  auto drained = [this, failed]() {
    m_state->state = HubdState::Down;
    m_state->rollback = failed;
    m_state->draining = true;
    drain_();
  };
  if (!m_state->serviceInit) { drained(); return; }
  m_state->service.stop([this, drained = ZuMv(drained)](bool ok) mutable {
    post_([this, ok, drained = ZuMv(drained)]() mutable {
      if (!ok) m_state->stopOK = false;
      drained();
    });
  });
}

void Hubd::drain_()
{
  if (!m_state || !m_state->draining || m_state->drainPosted ||
      m_state->posts.load_()) return;
  m_state->drainPosted = true;
  clearBatch_();
}

void Hubd::clearBatch_()
{
  unsigned budget = mx_() ? m_state->cf.schedulerTurnWork : UINT_MAX;
  if (!Hubd_::clearRouting(*m_state, budget)) {
    post_([this]() { clearBatch_(); });
    return;
  }
  auto finish = [this]() {
    bool failed = m_state->rollback;
    bool ok = m_state->stopOK;
    m_state->stopOK = true;
    m_state->rollback = false;
    m_state->draining = false;
    m_state->drainPosted = false;
    if (failed) Engine::started(false);
    else Engine::stopped(ok);
  };
  if (auto mx = mx_()) {
    mx->rxRun([this, finish = ZuMv(finish)]() mutable {
      mx_()->txRun([this, finish = ZuMv(finish)]() mutable {
        mx_()->rxRun(ZuMv(finish));
      });
    });
  } else finish();
}

void Hubd::final()
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  if (!m_state) return;
  ZmAssert_(!m_state->posts.load_() && !m_state->draining && !m_state->drainPosted);
  ZmAssert_(!m_state->roleDraining);
  ZmAssert_(!m_state->sessions.count_() && !m_state->browserSessions.count_());
  if (m_state->serviceInit) m_state->service.final();
  if (m_state->db) {
    m_state->db->final();
    m_state->db = nullptr;
  }
  m_state = nullptr;
}

void Hubd::verify(Zum::String token, Zum::ServiceVerifyFn complete)
{
  if (!m_state || !m_state->serviceInit || !complete) return;
  m_state->service.verify(ZuMv(token), ZuMv(complete));
}

void Hubd::setRefreshRevocationFn(Zum::ServiceRefreshFn fn)
{
  if (!m_state || !m_state->serviceInit) return;
  m_state->service.setRefreshRevocationFn(ZuMv(fn));
}

void Hubd::receiveSET(Zum::ServiceSETRequest request,
    Zum::ServiceSETDoneFn complete)
{
  if (!m_state || !m_state->serviceInit || !complete) return;
  m_state->service.receiveSET(ZuMv(request), ZuMv(complete));
}

HubdState::T Hubd::readiness() const
{
  return m_state ? m_state->state : HubdState::Down;
}

void Hubd::activateFn(HubActivateFn fn)
{
  ZmAssert_(Engine::state() == ZmEngineState::Stopped);
  ZmAssert_(m_state);
  m_state->activateFn = ZuMv(fn);
}

int Hubd::startError() const
{
  return m_state ? m_state->startError : Zum::ServiceError::Invalid;
}

const HubdCf &Hubd::config() const
{
  static const HubdCf empty;
  return m_state ? m_state->cf : empty;
}

Zum::ServiceManifest Hubd::manifest() const
{
  Zum::ServiceManifest value;
  if (!m_state) return value;
  value.revision = 1;
  for (auto &name: m_state->cf.actions)
    value.catalog.actions.push(Zum::CatalogAction{{}, name});
  for (auto &name: m_state->cf.roles) {
    Zum::StringVec actions;
    switch (Hubd_::roleMatcher.exact(name)) {
      case 0: actions.push("Request"); break;
      case 1: actions.push("Telemetry"); break;
    }
    value.catalog.roles.push(Zum::CatalogRole{
      ZuMv(actions), {}, name});
  }
  return value;
}

bool Hubd::validFrontMessage(MsgView msg)
{
  return Ztc::validMsg(msg) &&
    msgBody(msg).tag() == Body::Request &&
    msg.get<ZfField(Msg, subID)>() && msg.get<ZfField(Msg, deviceID)>() &&
    !msg.get<ZfField(Msg, agentGen)>();
}

bool Hubd::validAppRequest(MsgView msg)
{
  if (!Ztc::validMsg(msg) ||
      msgBody(msg).tag() != Body::Request ||
      !msg.get<ZfField(Msg, subID)>() || msg.get<ZfField(Msg, agentGen)>() ||
      msg.get<ZfField(Msg, deviceID)>()) return false;
  auto request = msgBody(msg).get<Body::Request>();
  auto filter = request.get<ZfField(Request, filter)>();
  return request.get<ZfField(Request, group)>() == Group::App &&
    !request.get<ZfField(Request, id)>() && (!filter || filter == "*") &&
    !request.get<ZfField(Request, alertDate)>() &&
    !request.get<ZfField(Request, alertSeqNo)>();
}

bool Hubd::authorized(
    const Zum::ServicePrincipal &principal, ZuCSpan action)
{
  return principal.audience && principal.expires > Zm::now().sec() &&
    (action == "Telemetry" ? principal.authMethod == "client_credentials" :
      action == "Request" && Hubd_::userAuthMethod(principal.authMethod)) &&
    Hubd_::hasAction(principal, action);
}

bool Hubd::addAgent(
    uint64_t sessionID, ZuCSpan deviceID, uint64_t generation,
    const Zum::ServicePrincipal &principal, HubError::T &error)
{
  return addAgent(sessionID, deviceID, generation, principal, {}, error);
}

bool Hubd::addAgent(
    uint64_t sessionID, ZuCSpan deviceID, uint64_t generation,
    const Zum::ServicePrincipal &principal, HubSendFn send, HubError::T &error)
{
  error = HubError::Unauthorized;
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !sessionID || !deviceID ||
      !generation || principal.subject != deviceID ||
      principal.tokenID.issuerURL != m_state->cf.issuerURL ||
      principal.audience != m_state->cf.audience ||
      !authorized(principal, "Telemetry")) return false;
  error = HubError::DuplicateSub;
  if (m_state->agents.findPtr(deviceID) ||
      m_state->sessions.findPtr(sessionID)) return false;
  error = HubError::BadReq;
  if (m_state->agentCount >= m_state->cf.expectedAgents ||
      !Hubd_::addSession(*m_state, sessionID, principal.tokenID,
        ZuMv(send), true)) return false;
  m_state->agents.addNode(new Hubd_::Agents::Node{
    sessionID, deviceID, generation});
  m_state->agentSessions.add(sessionID, Hubd_::AgentSession{deviceID, generation});
  ++m_state->agentCount;
  return true;
}

bool Hubd::addFrontend(uint64_t sessionID,
    const Zum::ServicePrincipal &principal, HubSendFn send)
{
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !sessionID || !send ||
      m_state->sessions.findPtr(sessionID) ||
      m_state->frontEndCount >= m_state->cf.activeFrontEnds ||
      principal.tokenID.issuerURL != m_state->cf.issuerURL ||
      principal.audience != m_state->cf.audience ||
      !authorized(principal, "Request") ||
      !Hubd_::addSession(*m_state, sessionID, principal.tokenID, ZuMv(send)))
    return false;
  ++m_state->frontEndCount;
  return true;
}

bool Hubd::createBrowserSession(
    const Zum::ServicePrincipal &principal, HubString &cookie)
{
  cookie.null();
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !principal.subject ||
      principal.tokenID.issuerURL != m_state->cf.issuerURL ||
      principal.audience != m_state->cf.audience ||
      !authorized(principal, "Request") || !principal.tokenID.jti ||
      m_state->browserSessions.count_() >= m_state->cf.activeFrontEnds ||
      !Hubd_::browserCookie(m_state->random, cookie)) return false;
  auto session = ZmRef<Hubd_::BrowserSession>{new Hubd_::BrowserSession};
  session->principal = principal;
  session->cookie = cookie;
  if (m_state->multiplex) m_state->multiplex->add(&session->expiry,
    ZuTime{principal.expires}, ZmScheduler::Update,
    [state = m_state.ptr(), session](auto &&arm) mutable {
      return arm([state, session = ZuMv(session)]() {
        state->browserSessions.del(session->cookie);
      });
    }, m_state->multiplex->rxThread());
  m_state->browserSessions.addNode(ZuMv(session));
  return true;
}

bool Hubd::browserPrincipal(ZuCSpan cookie, Zum::ServicePrincipal &principal)
{
  principal = {};
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !cookie) return false;
  auto node = m_state->browserSessions.del(cookie);
  if (!node) return false;
  if (m_state->multiplex) m_state->multiplex->del(&node->expiry);
  if (node->principal.expires <= Zm::now().sec()) return false;
  principal = ZuMv(node->principal);
  return true;
}

bool Hubd::removeAgent(uint64_t sessionID, ZuCSpan deviceID, uint64_t generation)
{
  return removeAgent(sessionID, deviceID, generation, {});
}

bool Hubd::removeAgent(uint64_t sessionID, ZuCSpan deviceID,
    uint64_t generation, HubRouteFn routeFn)
{
  if (!m_state) return false;
  auto agent = m_state->agents.findPtr(deviceID);
  if (!agent || agent->val().sessionID != sessionID ||
      agent->val().generation != generation) return false;
  Hubd_::removeSession(*m_state, sessionID);
  disconnectAgent(sessionID, generation, ZuMv(routeFn));
  return true;
}

bool Hubd::removeSession(uint64_t sessionID)
{
  return removeSession(sessionID, {});
}

bool Hubd::removeSession(uint64_t sessionID, HubRouteFn routeFn)
{
  if (!m_state || !sessionID) return false;
  bool removed = bool(m_state->sessions.findPtr(sessionID));
  Hubd_::removeSession(*m_state, sessionID);
  {
    auto i = m_state->appSubs.iter();
    while (auto node = i())
      if (node->val().frontEndID == sessionID) i.del();
  }
  for (;;) {
    auto index = m_state->subIdx.findPtr(sessionID);
    if (!index || !index->val().length()) break;
    Hubd_::delRoute(*m_state,
      Hubd_::RouteKey{sessionID, index->val()[index->val().length() - 1]}, routeFn);
  }
  return removed;
}

bool Hubd::addSubscription(uint64_t frontEndID, uint64_t subID,
    ZuCSpan deviceID, const Request &request,
    uint64_t &agentSessionID, uint64_t &agentGeneration,
    uint64_t &requestSeqNo, HubError::T &error)
{
  error = HubError::BadReq;
  agentSessionID = agentGeneration = requestSeqNo = 0;
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !frontEndID || !subID ||
      !deviceID) return false;
  auto session = m_state->sessions.findPtr(frontEndID);
  if (!session || session->val().agent) return false;
  error = HubError::DuplicateSub;
  if (m_state->subs.findPtr(Hubd_::RouteKey{frontEndID, subID})) return false;
  error = HubError::NoAgent;
  auto agent = m_state->agents.findPtr(deviceID);
  if (!agent || !m_state->sessions.findPtr(agent->val().sessionID)) return false;
  error = HubError::BadReq;
  auto index = m_state->subIdx.findPtr(frontEndID);
  if (index && index->val().length() >= m_state->cf.subscriptionsPerFrontEnd)
    return false;
  auto seq = m_state->nextRequestSeqNo++;
  if (!seq || seq == UINT64_MAX) return false;
  auto route = new Hubd_::Subs::Node{frontEndID, subID,
    agent->val().sessionID, agent->val().generation, seq, deviceID,
    bool(request.interval), !request.id};
  m_state->subs.addNode(route);
  m_state->agentReqs.add(Hubd_::RouteKey{route->val().agentSessionID, seq},
    &route->val());
  Hubd_::addAgentReq(*m_state, route->val().agentSessionID, seq);
  Hubd_::addSubIndex(*m_state, frontEndID, subID);
  agentSessionID = route->val().agentSessionID;
  agentGeneration = route->val().agentGeneration;
  requestSeqNo = seq;
  return true;
}

bool Hubd::app(uint64_t agentSessionID, uint64_t agentGeneration,
    TelemetryView telemetry)
{
  if (!m_state || !telemetry || telemetry.get<ZfField(Telemetry, seqNo)>() ||
      telemetry
        .get<ZfField(Telemetry, value)>().tag() != TelemetryBody::AppTelemetry) return false;
  auto session = m_state->agentSessions.findPtr(agentSessionID);
  if (!session || session->val().generation != agentGeneration) return false;
  auto agent = m_state->agents.findPtr(session->val().deviceID);
  if (!agent || agent->val().sessionID != agentSessionID ||
      agent->val().generation != agentGeneration) return false;
  auto publisherID = telemetry.get<ZfField(Telemetry, id)>();
  auto app_ = telemetry.get<ZfField(Telemetry, value)>().get<TelemetryBody::AppTelemetry>();
  if (!publisherID || !app_) return false;
  auto key = ZuFwdTuple(session->val().deviceID, agentGeneration, publisherID);
  auto node = m_state->inventories.findPtr(key);
  if (!node) {
    if (agent->val().appCount >= m_state->cf.publishersPerAgent) return false;
    node = new Hubd_::Inventories::Node{Hubd_::Inventory{
      session->val().deviceID, publisherID, Zfb::handler<AppTelemetry>(app_).ctor(),
      agentGeneration}};
    m_state->inventories.addNode(node);
    ++agent->val().appCount;
  } else
    node->val().app = Zfb::handler<AppTelemetry>(app_).ctor();
  Hubd_::inventorySend(*m_state, node->val());
  return true;
}

bool Hubd::appRemove(uint64_t agentSessionID, uint64_t agentGeneration,
    ZuCSpan publisherID)
{
  if (!m_state || !publisherID) return false;
  auto session = m_state->agentSessions.findPtr(agentSessionID);
  if (!session || session->val().generation != agentGeneration) return false;
  auto agent = m_state->agents.findPtr(session->val().deviceID);
  if (!agent || agent->val().sessionID != agentSessionID ||
      agent->val().generation != agentGeneration) return false;
  (void)Hubd_::inventoryDel(*m_state, session->val().deviceID,
    agentGeneration, publisherID);
  return true;
}

bool Hubd::appSubscribe(uint64_t frontEndID, uint64_t subID,
    uint32_t interval, HubError::T &error)
{
  error = HubError::BadReq;
  if (Engine::state() != ZmEngineState::Running ||
      !m_state || m_state->state != HubdState::Up || !frontEndID || !subID)
    return false;
  auto session = m_state->sessions.findPtr(frontEndID);
  if (!session || session->val().agent ||
      m_state->subs.findPtr(Hubd_::RouteKey{frontEndID, subID}) ||
      m_state->appSubs.findPtr(Hubd_::RouteKey{frontEndID, subID})) {
    error = HubError::DuplicateSub;
    return false;
  }
  unsigned count = 0;
  if (auto index = m_state->subIdx.findPtr(frontEndID))
    count = index->val().length();
  auto i = m_state->appSubs.iter();
  while (auto node = i()) if (node->val().frontEndID == frontEndID) ++count;
  if (count >= m_state->cf.subscriptionsPerFrontEnd) return false;
  if (interval) {
    m_state->appSubs.addNode(new Hubd_::AppSubs::Node{
      Hubd_::AppSub{frontEndID, subID}});
    session->val().send(HubPacket{}, Hubd_::inventoryAck(subID, interval));
  }
  auto apps = m_state->inventories.iter();
  while (auto node = apps())
    session->val().send(HubPacket{}, Hubd_::inventoryFrame(node->val(), subID));
  if (!interval) session->val().send(HubPacket{}, Hubd_::inventoryAck(subID, interval));
  // Every initial inventory snapshot completes, even a continuing stream.
  session->val().send(HubPacket{}, Hubd_::inventoryEOS(subID));
  return true;
}

bool Hubd::appUnsubscribe(uint64_t frontEndID, uint64_t subID)
{
  if (!m_state || !frontEndID || !subID) return false;
  auto session = m_state->sessions.findPtr(frontEndID);
  if (!session || session->val().agent) return false;
  (void)m_state->appSubs.del(Hubd_::RouteKey{frontEndID, subID});
  return true;
}

bool Hubd::removeSubscription(uint64_t frontEndID, uint64_t subID)
{
  return removeSubscription(frontEndID, subID, {});
}

bool Hubd::removeSubscription(uint64_t frontEndID, uint64_t subID,
    HubRouteFn routeFn)
{
  return m_state && Hubd_::delRoute(*m_state,
    Hubd_::RouteKey{frontEndID, subID}, ZuMv(routeFn));
}

bool Hubd::route(uint64_t sessionID, uint64_t generation, uint64_t seq,
    RouteInfo &info) const
{
  if (!m_state) return false;
  auto route = m_state->agentReqs.findVal(Hubd_::RouteKey{sessionID, seq});
  if (!route || route->agentGeneration != generation) return false;
  info = Hubd_::routeInfo(*route);
  return true;
}

bool Hubd::sendAgent(uint64_t sessionID, uint64_t generation, HubPacket frame)
{
  if (!m_state || !frame) return false;
  auto agent = m_state->agentSessions.findPtr(sessionID);
  if (!agent || agent->val().generation != generation) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || !session->val().send) return false;
  session->val().send(ZuMv(frame), HubBuildFn{});
  return true;
}

bool Hubd::sendFrontend(uint64_t sessionID, HubPacket frame)
{
  if (!m_state || !frame) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || session->val().agent || !session->val().send) return false;
  session->val().send(ZuMv(frame), HubBuildFn{});
  return true;
}

bool Hubd::sendAgent(uint64_t sessionID, uint64_t generation, HubBuildFn build)
{
  if (!m_state || !build) return false;
  auto agent = m_state->agentSessions.findPtr(sessionID);
  if (!agent || agent->val().generation != generation) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || !session->val().send) return false;
  session->val().send(HubPacket{}, ZuMv(build));
  return true;
}

bool Hubd::sendFrontend(uint64_t sessionID, HubBuildFn build)
{
  if (!m_state || !build) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || session->val().agent || !session->val().send) return false;
  session->val().send(HubPacket{}, ZuMv(build));
  return true;
}

unsigned Hubd::disconnectAgent(uint64_t sessionID, uint64_t generation)
{
  return disconnectAgent(sessionID, generation, {});
}

unsigned Hubd::disconnectAgent(uint64_t sessionID, uint64_t generation,
    HubRouteFn routeFn)
{
  return m_state ? Hubd_::disconnectAgent(*m_state, sessionID, generation,
    ZuMv(routeFn)) : 0;
}

} // Ztc
