//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// authenticated live telemetry hub

#ifndef ztchub_daemon_HH
#define ztchub_daemon_HH

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuDerive.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAssert.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfCf.hh>

#include <zlib/ZumService.hh>

#include <zlib/ZtcMsg.hh>
#include <zlib/ZtcAppTypes.hh>

namespace Ztc {

namespace Hubd_ { struct State; }

namespace HubdState {
  enum T : int8_t { Down, Up };
}

ZuDerive(HubString, (ZtString<ZtStringHeapID<"Ztc.Hub.String">>));
ZuDerive(HubStrings, (ZtArray<HubString, ZtArrayHeapID<"Ztc.Hub.Strings">>));
using HubFrame = ZmRef<ZiIOBuf>;

// A delivery retains actual buffers with their active skip/length.
struct HubPacket {
  HubFrame head;
  HubFrame tail;

  HubPacket() = default;
  explicit HubPacket(HubFrame frame, HubFrame tail_ = {}) :
    head{ZuMv(frame)}, tail{ZuMv(tail_)} { }
  explicit operator bool() const { return bool(head); }
  uint64_t length() const {
    return uint64_t(head ? head->length : 0) + (tail ? tail->length : 0);
  }

  // The outer fields are entirely in head. A segmented packet's Bytes vector
  // points at tail on the wire, so never dereference it through the prefix.
  MsgView message() const {
    return Zfb::view<Msg>(head ? head->cspan() : ZuBSpan{});
  }
  auto body() const {
    auto payload = tail ? tail->cspan() : message().get<ZfField(Msg, data)>();
    return Zfb::view<Payload>(payload).get<ZfField(Payload, body)>();
  }

  template <typename Tx>
  void write(Tx &tx, bool writerOwned) && {
    // Only an unpublished allocation from this writer grants mutability.
    // Serialize foreign/replayed storage into the transport's registered allocator.
    if (writerOwned) tx.appendOwned_(ZuMv(head));
    else tx << head->cspan();
    if (tail) tx << tail->cspan();
  }
};

// Build the real destination-owned Msg prefix with its empty Bytes child
// first. Backwards construction leaves that child's length word at the end;
// its aligned payload is the original nested root appended by reference.
inline HubPacket wrapHubFrame(HubFrame frame, HubFrame payload,
    uint64_t subID, ZuCSpan deviceID, uint64_t generation, unsigned tailRoom) {
  Builder builder{ZuMv(frame), Builder::Align, UINT32_MAX, tailRoom};
  auto data = builder.copyRoot({}, Builder::Align);
  builder.finish(saveMsg(builder, data, subID, deviceID, generation));
  auto head = builder.buf();
  if (!head) return {};
  ZmAssert(Zfb::view<Msg>(head->cspan()).get<ZfField(Msg, data)>().data() ==
    head->data() + head->length);
  ZmAssert(!(head->length & (Builder::Align - 1)));
  Zfb::write<Zfb::Offset>(head->data() + head->length - sizeof(Zfb::Offset),
    payload->length);
  return HubPacket{ZuMv(head), ZuMv(payload)};
}

// Builders are consumed synchronously on the sending producer's shard.
using HubBuildFn = ZmFn<HubPacket(HubFrame, unsigned), ZmFnHeapID<"Ztc.Hub.Build">>;
using HubSendFn = ZmFn<void(HubPacket, HubBuildFn), ZmFnHeapID<"Ztc.Hub.Send">>;
using HubCtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;
using HubActivateFn = ZmFn<void(bool, HubCtrlFn),
  ZmFnHeapID<"Ztc.Hub.Activate">>;

struct RouteInfo {
  uint64_t frontEndID = 0;
  uint64_t subID = 0;
  uint64_t agentSessionID = 0;
  uint64_t agentGeneration = 0;
  uint64_t requestSeqNo = 0;
  HubString deviceID;
  bool continuing = false;
  bool fanout = false;
};

using HubRouteFn = ZmFn<void(RouteInfo), ZmFnHeapID<"Ztc.Hub.Route">>;

struct ListenerCf {
  HubString	bind;
  HubString	path;
  HubString	cert;
  HubString	key;
  HubString	browserPath;
  HubStrings	origins;
	uint32_t	port = 443;
	uint32_t	ssfPort = 0;
};

ZfStruct(, (ListenerCf, Cf),
  (bind,		(Mutable, Required),			String),
  (path,		(Mutable, Required),			String),
  (cert,		(Mutable, Required),			String),
  (key,		(Mutable, Required),				String),
  (browserPath,		(Mutable, Required),			String),
  (origins, (Mutable),						StringVec),
  (port,		(Mutable, (Range<1U, 65535U>)),		UInt32),
  (ssfPort,		(Mutable, (Range<0U, 65535U>)),		UInt32));

using ListenerCfs = ZtArray<ListenerCf,
  ZtArrayHeapID<"Ztc.Hub.Listeners">>;
inline ZfCf::AsArray<ZfFieldTC::UDT> ZfCf_Fmt(ListenerCfs *);

struct HubdCf {
	ListenerCfs	listeners;
	HubString	issuerURL;
	HubString	audience;
	HubString	managementIssuer;
	HubString	managementURL;
	HubString	managementClientID;
	HubString	caPath;
	HubString	ssfCallbackPath{"/ssf"};
	HubString	ssfDeliveryURL;
  uint32_t	ssfLease = 300;
  HubStrings	actions;
  HubStrings	roles;
  // Default workload: 2048 agents, 256 publishers, 32 clients x 32 streams.
  // Each stream reserves 1 MiB each for control and telemetry (2 GiB total).
  uint32_t	maxFrame = 1U << 16;
  uint32_t	controlFrames = 256;
  uint32_t	telemetryFrames = 1024;
  uint64_t	controlBytes = 1ULL << 20;
  uint64_t	telemetryBytes = 1ULL << 20;
  uint64_t	queueMem = 1ULL << 32;
  uint32_t	upgradeTimeout = 10;
  uint32_t	idleTimeout = 60;
  uint32_t	pingInterval = 30;
  uint32_t	closeTimeout = 5;
  uint32_t	expectedAgents = 2048;
  uint32_t	publishersPerAgent = 256;
  uint32_t	activeFrontEnds = 32;
  uint32_t	subscriptionsPerFrontEnd = 32;
  uint32_t	minRefreshMS = 1000;
  uint32_t	fanoutSLOMS = 200;
  uint32_t	schedulerTurnWork = 64;
};

ZfStruct(, (HubdCf, Cf),
  (listeners,		(Mutable, Required),					UDT),
  (issuerURL,		(Mutable, JSON::ID<"issuer">, Required),		String),
  (audience,		(Mutable, Required),					String),
  (managementIssuer,	(Mutable, Required),					String),
	(managementURL,	(Mutable, Required),					String),
  (managementClientID,	(Mutable, Required),					String),
  (caPath, (Mutable),								String),
  (ssfDeliveryURL, (Mutable, Required),						String),
  (ssfLease, (Mutable, (Range<2U, 86400U>)),					UInt32),
  (ssfCallbackPath, (Mutable),							String),
  (actions, (Mutable),								StringVec),
  (roles, (Mutable),								StringVec),
  (maxFrame,		(Mutable, (Range<64U, INT_MAX>)),			UInt32),
  (controlFrames,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (telemetryFrames,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (controlBytes,	(Mutable, (Range<64ULL, UINT64_MAX>)),			UInt64),
  (telemetryBytes,	(Mutable, (Range<64ULL, UINT64_MAX>)),			UInt64),
  (queueMem,	(Mutable, (Range<64ULL, UINT64_MAX>)),				UInt64),
  (upgradeTimeout,	(Mutable, (Range<1U, 86400U>)),				UInt32),
  (idleTimeout,	(Mutable, (Range<1U, 86400U>)),					UInt32),
  (pingInterval,	(Mutable, (Range<1U, 86400U>)),				UInt32),
  (closeTimeout,	(Mutable, (Range<1U, 86400U>)),				UInt32),
  (expectedAgents,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (publishersPerAgent,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (activeFrontEnds,	(Mutable, (Range<1U, INT_MAX>)),			UInt32),
  (subscriptionsPerFrontEnd,	(Mutable, (Range<1U, INT_MAX>)),		UInt32),
  (minRefreshMS,	(Mutable, (Range<1000U, 86400000U>)),			UInt32),
  (fanoutSLOMS,	(Mutable, (Range<1U, 86400000U>)),				UInt32),
  (schedulerTurnWork,	(Mutable, (Range<1U, INT_MAX>)),			UInt32));

class Hubd : private ZmEngine<Hubd> {
friend ZmEngine<Hubd>;
friend Hubd_::State;
  using Engine = ZmEngine<Hubd>;
public:
  Hubd();
  ~Hubd();

  Hubd(const Hubd &) = delete;
  Hubd &operator =(const Hubd &) = delete;

  bool init(HubdCf);
  bool init(HubdCf, ZiMultiplex *, Zum::ServiceHTTPFn,
    Zum::Bytes clientSecret, ZuCSpan callbackAuth);
  using Engine::start;
  using Engine::stop;
  using Engine::state;
  using Engine::running;
  using Engine::stopping;
  void final();

  HubdState::T readiness() const;
  int startError() const;
  // Configure before start. DB role callbacks control the listener aggregate;
  // its lifetime extends through Hubd stop and final.
  void activateFn(HubActivateFn);
  const HubdCf &config() const;
  Zum::ServiceManifest manifest() const;

  void verify(Zum::String, Zum::ServiceVerifyFn);
  void setRefreshRevocationFn(Zum::ServiceRefreshFn);
  void receiveSET(Zum::ServiceSETRequest, Zum::ServiceSETDoneFn);

  static bool validFrontMessage(MsgView);
  static bool validAppRequest(MsgView);
  static bool authorized(const Zum::ServicePrincipal &, ZuCSpan action);

  bool addAgent(uint64_t sessionID, ZuCSpan deviceID, uint64_t generation,
    const Zum::ServicePrincipal &, HubError::T &);
  bool addAgent(uint64_t sessionID, ZuCSpan deviceID, uint64_t generation,
    const Zum::ServicePrincipal &, HubSendFn, HubError::T &);
  bool addFrontend(uint64_t sessionID, const Zum::ServicePrincipal &, HubSendFn);
  bool createBrowserSession(const Zum::ServicePrincipal &, HubString &);
  bool browserPrincipal(ZuCSpan, Zum::ServicePrincipal &);
  bool removeAgent(uint64_t sessionID, ZuCSpan deviceID, uint64_t generation);
  bool removeAgent(uint64_t sessionID, ZuCSpan deviceID, uint64_t generation,
    HubRouteFn);
  bool removeSession(uint64_t sessionID);
  bool removeSession(uint64_t sessionID, HubRouteFn);
  bool addSubscription(uint64_t frontEndID, uint64_t subID,
    ZuCSpan deviceID, const Request &request,
    uint64_t &agentSessionID, uint64_t &agentGeneration,
    uint64_t &requestSeqNo, HubError::T &);
  bool removeSubscription(uint64_t frontEndID, uint64_t subID);
  bool removeSubscription(uint64_t frontEndID, uint64_t subID, HubRouteFn);
  bool app(uint64_t agentSessionID, uint64_t agentGeneration,
    TelemetryView);
  bool appRemove(uint64_t agentSessionID, uint64_t agentGeneration,
    ZuCSpan publisherID);
  bool appSubscribe(uint64_t frontEndID, uint64_t subID,
    uint32_t interval, HubError::T &);
  bool appUnsubscribe(uint64_t frontEndID, uint64_t subID);
  bool route(uint64_t agentSessionID, uint64_t agentGeneration,
    uint64_t requestSeqNo, RouteInfo &) const;
  bool sendAgent(uint64_t sessionID, uint64_t generation, HubPacket);
  bool sendFrontend(uint64_t sessionID, HubPacket);
  bool sendAgent(uint64_t sessionID, uint64_t generation, HubBuildFn);
  bool sendFrontend(uint64_t sessionID, HubBuildFn);
  unsigned disconnectAgent(uint64_t sessionID, uint64_t generation);
  unsigned disconnectAgent(uint64_t sessionID, uint64_t generation,
    HubRouteFn);

private:
  void start_();
  void startRx_();
  void startDB_();
  void stop_();
  void stopRx_(bool);
  void stopService_(bool);
  void drain_();
  void clearBatch_();
  ZiMultiplex *mx_() const;
  template <typename L> void post_(L &&fn) {
    if (auto mx = mx_()) mx->rxRun(ZuFwd<L>(fn));
    else ZuFwd<L>(fn)();
  }

private:
  ZmRef<Hubd_::State> m_state;
};

} // Ztc

#endif /* ztchub_daemon_HH */
