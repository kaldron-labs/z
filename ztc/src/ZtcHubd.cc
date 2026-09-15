//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcHubd.hh>

#include <limits.h>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZtlsRandom.hh>

#include <zlib/Zdb.hh>
#include <zlib/ZdbMemStore.hh>

#include <zlib/ZtcMsg.hh>

namespace Ztc {
namespace Hubd_ {

struct Agent {
  Agent(uint64_t sessionID_, ZuCSpan deviceID_, uint64_t generation_) :
    sessionID{sessionID_}, deviceID{deviceID_}, generation{generation_} { }

  uint64_t	sessionID;
  HubString	deviceID;
  uint64_t	generation;
};

template <typename Heap>
struct BrowserSession_ : public Heap, public ZmObject {
  Zum::ServicePrincipal principal;
  HubString cookie;
  ZmScheduler::Timer expiry;
};
using BrowserSessionHeap = ZmHeap<"Ztc.Hub.BrowserSession",
  BrowserSession_<ZuVoid>>;
struct BrowserSession final : public BrowserSession_<BrowserSessionHeap> { };

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

struct Route {
  uint64_t	frontEndID;
  uint64_t	subID;
  uint64_t	agentSessionID;
  uint64_t	agentGeneration;
	uint64_t	requestSeqNo;
	HubString	deviceID;
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
using BrowserSessions = ZmHashKV<HubString, ZmRef<BrowserSession>,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.BrowserSession">>>;
struct TokenSession {
  uint64_t sessionID = 0;
};
using TokenSessions = ZtArray<TokenSession,
  ZtArrayHeapID<"Ztc.Hub.TokenSessions">>;
using TokenIdx = ZmHashKV<Zum::TokenID, TokenSessions,
  ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.TokenIdx">>>;

struct StateData {
	HubdCf	cf;
	HubdState::T	state = HubdState::Down;
	Agents	agents;
	Subs	subs;
	AgentReqs	agentReqs;
	AgentReqIdx	agentReqIdx;
	SubIdx	subIdx;
	AgentSessions	agentSessions;
	Sessions	sessions;
	TokenIdx	tokenIdx;
	BrowserSessions	browserSessions;
	ZmRef<Zdb>	db;
	ZuPtr<const ZfCf::AnyNode>	dbSource;
	uint64_t	nextRequestSeqNo = 1;
	uint32_t	agentCount = 0;
	uint32_t	frontEndCount = 0;
	ZiMultiplex	*multiplex = nullptr;
	Ztls::Random	random;
	Zum::Service	service;
	bool	serviceReady = false;
	bool	serviceStarted = false;
};

template <typename Heap>
struct State_ : public Heap, public ZmObject, public StateData { };
using StateHeap = ZmHeap<"Ztc.Hub.State", State_<ZuVoid>>;
struct State final : public State_<StateHeap> { };

static void clearBrowsers(StateData &state)
{
  if (state.multiplex) {
    auto i = state.browserSessions.iter();
    while (auto node = i()) state.multiplex->del(&node->val()->expiry);
  }
  state.browserSessions.clean();
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
  if (!tokenID.issuer || !tokenID.jti || !sessionID) return false;
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
  if (!tokenID.issuer || !tokenID.jti || !sessionID) return;
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
    route.agentGeneration, route.requestSeqNo, route.deviceID};
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
      state.multiplex->rxRun([hold = ZmRef<State>{&state}, sessionID,
          generation, routeFn = ZuMv(routeFn)]() mutable {
        disconnectAgent(*hold, sessionID, generation, ZuMv(routeFn));
      });
      return n;
    }
    auto seq = index->val()[index->val().length() - 1];
    auto route = state.agentReqs.findVal(RouteKey{sessionID, seq});
    if (route) delRoute(state, Route_SubKeyAxor(*route), routeFn);
    else removeAgentReq(state, sessionID, seq);
    ++n;
  }
  state.agents.del(agent->val().deviceID);
  state.agentSessions.delNode(agent);
  if (state.agentCount) --state.agentCount;
  return n;
}

} // Hubd_

Hubd::Hubd() = default;

static bool initDB(Hubd_::State &state, ZiMultiplex *mx)
{
  auto source = ZfCf::scan(
    "thread: hubdb, shards: 1, store: {thread: hubstore}, "
    "hostID: ztchub, hosts: {ztchub: {standalone: true}}, "
    "tables: {}\n");
  if (!source) return false;
  state.dbSource = ZuMv(source.p<1>());
  try {
    auto config = ZdbCf{state.dbSource};
    state.db = new Zdb;
    state.db->init(ZuMv(config), mx, ZdbHandler{},
      ZmRef<Zdb_::Store>{new ZdbMem::Store});
  } catch (...) {
    state.db = nullptr;
    state.dbSource = nullptr;
    return false;
  }
  return true;
}

static uint32_t ssfPort(const ListenerCf &listener)
{
  return listener.ssfPort ? listener.ssfPort : listener.port + 1;
}

Hubd::~Hubd() { final(); }

bool Hubd::init(HubdCf cf)
{
  if (m_state || !cf.listeners.length() || !cf.issuer || !cf.audience ||
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
  state->state = HubdState::Starting;
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
    .issuerURL = state.cf.issuer,
    .managementIssuerURL = state.cf.managementIssuer,
    .managementURL = state.cf.managementURL,
    .clientID = state.cf.managementClientID,
    .clientSecret = ZuMv(clientSecret),
    .audience = state.cf.audience,
    .responseMax = state.cf.maxFrame,
    .ssf = Zum::ServiceSSFConfig{
      .enabled = true,
      .receiverID = state.cf.managementClientID,
      .callbackPath = state.cf.ssfCallbackPath,
      .callbackAuth = callbackAuth,
      .transmitterIssuer = state.cf.issuer,
      .audience = state.cf.audience,
      .maxBytes = state.cf.maxFrame}}
  ;
  if (!state.service.init(ZuMv(serviceCf), ZuMv(http))) {
    final();
    return false;
  }
  state.serviceReady = true;
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

bool Hubd::start()
{
  if (!m_state || m_state->state != HubdState::Starting) return false;
  if (!validManifest(*m_state)) {
    m_state->state = HubdState::Down;
    return false;
  }
  if (m_state->serviceReady) return false;
  m_state->state = HubdState::Publishing;
  m_state->state = HubdState::Listening;
  m_state->state = HubdState::Up;
  return true;
}

void Hubd::start(ZmFn<void(int), ZmFnHeapID<"Ztc.Hub.Start">> complete)
{
  if (m_state && m_state->multiplex) {
    m_state->multiplex->rxInvoke([this, complete = ZuMv(complete)]() mutable {
      start_(ZuMv(complete));
    });
  } else start_(ZuMv(complete));
}

void Hubd::start_(ZmFn<void(int), ZmFnHeapID<"Ztc.Hub.Start">> complete)
{
  if (!m_state || !complete || m_state->state != HubdState::Starting) {
    if (complete) complete(Zum::ServiceError::Invalid);
    return;
  }
  if (!validManifest(*m_state)) {
    m_state->state = HubdState::Down;
    complete(Zum::ServiceError::Invalid);
    return;
  }
  if (!m_state->serviceReady) {
    complete(start() ? Zum::ServiceError::OK : Zum::ServiceError::Invalid);
    return;
  }
  m_state->serviceStarted = true;
  m_state->state = HubdState::Publishing;
  auto state = ZmRef<Hubd_::State>{m_state};
    state->service.start([this, state = ZuMv(state),
      complete = ZuMv(complete)](int error) mutable {
    if (error != Zum::ServiceError::OK) {
      state->serviceStarted = false;
      state->service.final();
      state->serviceReady = false;
      state->state = HubdState::Down;
      complete(error);
      return;
    }
    state->service.publish(manifest(),
      [state = ZuMv(state), complete = ZuMv(complete)](
          Zum::ServiceProtocolResult result) mutable {
        if (result.error != Zum::ServiceError::OK) {
          state->service.stop([state = ZuMv(state), complete = ZuMv(complete)](
              int) mutable {
            state->service.final();
            state->serviceStarted = false;
            state->serviceReady = false;
            state->state = HubdState::Down;
            complete(Zum::ServiceError::Unavailable); });
          return;
        }
        if (!state->db) {
          state->state = HubdState::Listening;
          state->state = HubdState::Up;
          complete(Zum::ServiceError::OK);
          return;
        }
        state->db->start([state = ZuMv(state),
            complete = ZuMv(complete)](bool active) mutable {
          state->multiplex->rxRun([state = ZuMv(state),
              complete = ZuMv(complete), active]() mutable {
            if (active) {
              state->state = HubdState::Listening;
              state->state = HubdState::Up;
              complete(Zum::ServiceError::OK);
              return;
            }
            state->service.stop([state = ZuMv(state),
                complete = ZuMv(complete)](int) mutable {
              state->service.final();
              state->serviceStarted = false;
              state->serviceReady = false;
              state->state = HubdState::Down;
              complete(Zum::ServiceError::Unavailable);
            });
          });
        });
      });
  });
}

bool Hubd::stop()
{
  if (!m_state) return true;
  if (m_state->multiplex)
    return ZmBlock<bool>{}([this](auto wake) {
      stop([wake = ZuMv(wake)](int error) mutable {
        wake(error == Zum::ServiceError::OK);
      });
    });
  m_state->state = HubdState::Stopping;
  m_state->state = HubdState::Draining;
  m_state->agents.clean();
  m_state->agentReqs.clean();
  m_state->agentReqIdx.clean();
  m_state->subs.clean();
  m_state->subIdx.clean();
  m_state->agentSessions.clean();
  m_state->sessions.clean();
  m_state->tokenIdx.clean();
  Hubd_::clearBrowsers(*m_state);
  m_state->state = HubdState::Down;
  return true;
}

void Hubd::stop(ZmFn<void(int), ZmFnHeapID<"Ztc.Hub.Stop">> complete)
{
  if (m_state && m_state->multiplex) {
    m_state->multiplex->rxInvoke([this, complete = ZuMv(complete)]() mutable {
      stop_(ZuMv(complete));
    });
  } else stop_(ZuMv(complete));
}

void Hubd::stop_(ZmFn<void(int), ZmFnHeapID<"Ztc.Hub.Stop">> complete)
{
  if (!complete) return;
  if (!m_state || m_state->state == HubdState::Down) {
    complete(Zum::ServiceError::OK);
    return;
  }
  auto state = ZmRef<Hubd_::State>{m_state};
  state->state = HubdState::Stopping;
  state->state = HubdState::Draining;
  state->agents.clean();
  state->agentReqs.clean();
  state->agentReqIdx.clean();
  state->subs.clean();
  state->subIdx.clean();
  state->agentSessions.clean();
  state->sessions.clean();
  state->tokenIdx.clean();
  Hubd_::clearBrowsers(*state);
  if (!state->serviceReady) {
    state->state = HubdState::Down;
    complete(Zum::ServiceError::OK);
    return;
  }
  if (!state->serviceStarted) {
    state->service.final();
    state->serviceReady = false;
    state->state = HubdState::Down;
    complete(Zum::ServiceError::OK);
    return;
  }
  bool dbRunning = state->db && state->db->running();
  auto stopService = [state, complete = ZuMv(complete)](
      bool dbOK) mutable {
    state->service.stop([state = ZuMv(state), complete = ZuMv(complete),
        dbOK](int error) mutable {
      state->service.final();
      state->serviceStarted = false;
      state->serviceReady = false;
      state->state = HubdState::Down;
      complete(!dbOK ? Zum::ServiceError::Unavailable :
        error == Zum::ServiceError::OK ? Zum::ServiceError::OK : error);
    });
  };
  if (dbRunning)
    state->db->stop([stopService = ZuMv(stopService)](bool ok) mutable {
      stopService(ok);
    });
  else
    stopService(true);
}

void Hubd::final()
{
  if (!m_state) return;
  stop();
  if (m_state->db) {
    m_state->db->final();
    m_state->db = nullptr;
  }
  m_state = nullptr;
}

void Hubd::verify(Zum::String token, Zum::ServiceVerifyFn complete)
{
  if (!m_state || !m_state->serviceReady || !complete) return;
  m_state->service.verify(ZuMv(token), ZuMv(complete));
}

void Hubd::setRefreshRevocationFn(Zum::ServiceRefreshFn fn)
{
  if (!m_state || !m_state->serviceReady) return;
  m_state->service.setRefreshRevocationFn(ZuMv(fn));
}

void Hubd::receiveSET(Zum::ServiceSETRequest request,
    Zum::ServiceSETDoneFn complete)
{
  if (!m_state || !m_state->serviceReady || !complete) return;
  m_state->service.receiveSET(ZuMv(request), ZuMv(complete));
}

HubdState::T Hubd::state() const
{
  return m_state ? m_state->state : HubdState::Down;
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
    if (name == "Client") actions.push("Request");
    else if (name == "Agent") actions.push("Telemetry");
    value.catalog.roles.push(Zum::CatalogRole{
      ZuMv(actions), {}, name});
  }
  return value;
}

bool Hubd::validAgentMessage(const fbs::Msg *msg)
{
  return Ztc::validMsg(msg);
}

bool Hubd::validFrontMessage(const fbs::Msg *msg)
{
  return Ztc::validMsg(msg) && msg->body_type() == fbs::Body::Request &&
    msg->subId() && msg->deviceId() && msg->deviceId()->size() &&
    !msg->agentGen();
}

bool Hubd::authorized(
    const Zum::ServicePrincipal &principal, ZuCSpan action)
{
  return principal.audience && principal.expires > Zm::now().sec() &&
    (action == "Telemetry" ? principal.authMethod == "client_credentials" :
      action == "Request" &&
        (principal.authMethod == "passkey" || principal.authMethod == "oidc")) &&
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
  if (!m_state || m_state->state != HubdState::Up || !sessionID || !deviceID ||
      !generation || principal.subject != deviceID ||
      principal.tokenID.issuer != m_state->cf.issuer ||
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
  if (!m_state || m_state->state != HubdState::Up || !sessionID || !send ||
      m_state->sessions.findPtr(sessionID) ||
      m_state->frontEndCount >= m_state->cf.activeFrontEnds ||
      principal.tokenID.issuer != m_state->cf.issuer ||
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
  if (!m_state || m_state->state != HubdState::Up || !principal.subject ||
      principal.tokenID.issuer != m_state->cf.issuer ||
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
      return arm([state, session]() mutable {
        state->browserSessions.del(session->cookie);
      });
    }, m_state->multiplex->rxThread());
  m_state->browserSessions.add(cookie, ZuMv(session));
  return true;
}

bool Hubd::browserPrincipal(ZuCSpan cookie, Zum::ServicePrincipal &principal)
{
  principal = {};
  if (!m_state || !cookie) return false;
  auto node = m_state->browserSessions.del(cookie);
  if (!node) return false;
  if (m_state->multiplex) m_state->multiplex->del(&node->val()->expiry);
  if (node->val()->principal.expires <= Zm::now().sec()) return false;
  principal = ZuMv(node->val()->principal);
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
  for (;;) {
    auto index = m_state->subIdx.findPtr(sessionID);
    if (!index || !index->val().length()) break;
    Hubd_::delRoute(*m_state,
      Hubd_::RouteKey{sessionID, index->val()[index->val().length() - 1]}, routeFn);
  }
  return removed;
}

bool Hubd::addSubscription(uint64_t frontEndID, uint64_t subID,
    ZuCSpan deviceID, uint64_t &agentSessionID, uint64_t &agentGeneration,
    uint64_t &requestSeqNo, HubError::T &error)
{
  error = HubError::BadReq;
  agentSessionID = agentGeneration = requestSeqNo = 0;
  if (!m_state || m_state->state != HubdState::Up || !frontEndID || !subID ||
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
    agent->val().sessionID, agent->val().generation, seq, deviceID};
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

bool Hubd::completeRoute(uint64_t sessionID, uint64_t generation, uint64_t seq)
{
  if (!m_state) return false;
  auto route = m_state->agentReqs.findVal(Hubd_::RouteKey{sessionID, seq});
  return route && route->agentGeneration == generation &&
    Hubd_::delRoute(*m_state, Hubd_::Route_SubKeyAxor(*route));
}

bool Hubd::sendAgent(uint64_t sessionID, uint64_t generation, HubFrame frame)
{
  if (!m_state || !frame) return false;
  auto agent = m_state->agentSessions.findPtr(sessionID);
  if (!agent || agent->val().generation != generation) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || !session->val().send) return false;
  session->val().send(ZuMv(frame));
  return true;
}

bool Hubd::sendFrontend(uint64_t sessionID, HubFrame frame)
{
  if (!m_state || !frame) return false;
  auto session = m_state->sessions.findPtr(sessionID);
  if (!session || session->val().agent || !session->val().send) return false;
  session->val().send(ZuMv(frame));
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
