//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtcAgent.hh>

#include <string.h>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuStringFn.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBackoff.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmRing.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiIP.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiPIDFile.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtlsPK.hh>

#include <zlib/ZhttpClient.hh>

#include <zlib/ZtcRing.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcVer.hh>

namespace Ztc {

static bool validHubHost(ZuCSpan);

namespace Agent_ {

ZuDerive(CSR, (ZtArray<uint8_t, ZtArrayHeapID<"Ztc.Agent.CSR">>));
ZuDerive(Nonce, (ZtArray<uint8_t, ZtArrayHeapID<"Ztc.Agent.Nonce">>));
ZuDerive(EnrollBody,
  (ZtString<ZtStringHeapID<"Ztc.Agent.EnrollBody">>));

struct Phase { enum { Down, Enrolling, Connecting, Up, Stopping }; };

struct PendingEnroll {
  ZmRef<Ztls::PK::AnyPK>	key;
  CSR			csr;
  Nonce			nonce;
  EnrollBody		body;
};

struct Identity {
  Ztls::ClientCreds	creds;
  Ztls::Certs		caCerts;
  Ztls::Host		host;
  ZuID			agentID;
  uint16_t		port = 0;
};

using EnrollState = ZuUnion<void, PendingEnroll, Identity>;

struct EnrollRequest {
  ZuBSpan	csr;
  ZuBSpan	nonce;
  ZuCSpan	deviceID;
  ZuCSpan	agentVersion;
  uint32_t	protocolVersion = 1;
};

ZfStruct((EnrollRequest, JSON),
  (((protocolVersion),
    (JSON::ID<"protocol_version">)),		(UInt32)),
  (((deviceID),
    (JSON::ID<"device_id">, JSON::Opt)),	(String)),
  (((nonce),
    (JSON::ID<"nonce">, JSON::Base64URL)),	(Bytes)),
  (((csr),
    (JSON::ID<"csr">, JSON::Base64)),		(Bytes)),
  (((agentVersion),
    (JSON::ID<"agent_version">)),		(String)));

struct EnrollResponse {
  Ztls::Certs	certificateChain;
  Ztls::Certs	hubCAChain;
  Nonce		nonce;
  Ztls::Host	hubHost;
  ZtString<ZtStringHeapID<"Ztc.Agent.AgentID">> agentID;
  uint32_t	protocolVersion = 0;
  uint16_t	hubPort = 0;
};

ZfStruct((EnrollResponse, JSON),
  (((protocolVersion),
    (JSON::ID<"protocol_version">, Required)),	(UInt32)),
  (((nonce),
    (JSON::ID<"nonce">, JSON::Base64URL, Required)), (Bytes)),
  (((agentID),
    (JSON::ID<"agent_id">, Required)),		(String)),
  (((certificateChain),
    (JSON::ID<"certificate_chain">, JSON::Base64, Required)), (BytesVec)),
  (((hubCAChain),
    (JSON::ID<"hub_ca_chain">, JSON::Base64, Required)), (BytesVec)),
  (((hubHost),
    (JSON::ID<"hub_host">, Required)),		(String)),
  (((hubPort),
    (JSON::ID<"hub_port">, Required)),		(UInt16)));

struct Frame : public ZiIOBufAlloc<0, AgentCf::MaxFrame,
    "Ztc.Agent.Frame"> { };
struct HubRx : public ZiIOBufAlloc<0, AgentCf::MaxFrame,
    "Ztc.Agent.HubRx"> { };

struct App;
struct Req;
struct EnrollClient;

struct Leg {
  Leg(App *app_, Req *req_, uint64_t seqNo_) :
    app{app_}, req{req_}, seqNo{seqNo_} { }

  App		*app;
  Req		*req;
  uint64_t	seqNo;
};

inline uint64_t Leg_SeqAxor(const Leg &leg) { return leg.seqNo; }

ZmListDerive(LegList, Leg, ZmListNode<Leg, ZmListShadow<>>);
ZmHashDerive(LegHash, typename LegList::Node,
  (ZmHashNode<typename LegList::Node,
    ZmHashKey<Leg_SeqAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Ztc.Agent.Leg">>>>));

struct App {
  App(ZuID id_, uint32_t ztcver_, const AgentCf &cf) :
    id{ZuMv(id_)},
    ring{ZiRingParams{id, 0}.timeout(cf.reqTimeout)
      .killWait(cf.reqKillWait).coredump(cf.reqCoredump)},
    ztcver{ztcver_}
  {
    ready = ring.open(Ring::Write) == Zu::OK && ring.writeStatus() >= 0;
  }

  ZuID		id;
  Ring		ring;
  LegHash	legs;
  uint32_t	ztcver = 0;
  bool		ready = false;
};

inline const ZuID &App_IDAxor(const App &app) { return app.id; }

ZmHashDerive(Apps, App,
  (ZmHashNode<App,
    ZmHashKey<App_IDAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Ztc.Agent.App">>>>));

ZmHashDerive(AppIDs, ZuID,
  (ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Agent.ReqExcl">>));

struct Req {
  Req(Request request_, uint64_t cxnGen_) :
    request{ZuMv(request_)}, cxnGen{cxnGen_} { }

  Request	request;
  LegList	pending;
  LegList	legs;
  AppIDs	excl;
  uint64_t	cxnGen;
};

inline uint64_t Req_SeqAxor(const Req &req) { return req.request.seqNo; }

ZmHashDerive(Reqs, Req,
  (ZmHashNode<Req,
    ZmHashKey<Req_SeqAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Ztc.Agent.Req">>>>));

ZuDerive(Credits, (ZmRing<ZmRingT<unsigned>>));

class State {
public:
  State(Agent *agent_, const AgentCf &cf_, AgentEnv env_) :
    agent{agent_}, cf{cf_}, env{ZuMv(env_)},
    telCredits{ZmRingParams{
      (uint64_t(cf.telFrames) + 1) * sizeof(unsigned)}},
    reconn{cf.reconnMin, cf.reconnMax,
	cf.reconnBackoff, cf.reconnRandom},
    reconnInterval{reconn.initial()}
  {
    telCredits.open(Credits::Read | Credits::Write);
  }
  ~State() {
    unsigned n = env.token.length();
    if (n) memset(env.token.ensure(n + 1), 0, n);
  }

  Agent		*agent;
  AgentCf	cf;
  AgentEnv	env;
  ZiMultiplex	*mx = nullptr;
  Ring		telRing;
  ZiPIDFile	pidFile;
  ZmThread	telThread;
  ZmAtomic<unsigned> stop{0};

  alignas(Zm::CacheLineSize) Apps apps;
  Reqs		reqs;
  uint64_t	nextSeqNo = 1;
  uint64_t	routeCxnGen = 0;
  uint64_t	minHubSeqNo = 0;

  alignas(Zm::CacheLineSize) Credits telCredits;
  unsigned	telFrames = 0;
  uint64_t	telBytes = 0;
  uint64_t	telLostFrames = 0;
  uint64_t	telLostBytes = 0;
  bool		telOverflowPending = false;

  alignas(Zm::CacheLineSize) uint64_t nextCxnGen = 1;
  uint64_t	currentCxnGen = 0;
  Agent::Link	*currentLink = nullptr;
  unsigned	reqPendingFrames = 0;
  uint64_t	reqPendingBytes = 0;
  ZmBackoff	reconn;
  ZuTime	reconnInterval;

  alignas(Zm::CacheLineSize) EnrollState enrollment;
  ZmRef<Agent::Link> hubLink;
  EnrollClient	*enrollClient = nullptr;
  ZmPLock	ctrlLock;
  Agent::CtrlFn	startFn;
  bool		baseInit = false;
  bool		running = false;
  bool		startPending = false;
  int8_t	phase = Phase::Down;
};

using EnrollReqHeaders = ZhttpHeaders(
  "content-type", "accept", "authorization", "cache-control",
  "content-length");
using EnrollResHeaders = ZhttpHeaders("content-type", "cache-control");

struct EnrollReq_;
struct EnrollRes;
struct EnrollPool;
template <typename Heap = ZuVoid> struct EnrollPool_;

struct Authorization {
  State *state;
  template <typename S> void print(S &s) const {
    s << "Bearer " << state->env.token;
  }
  friend ZuPrintFn ZuPrintType(Authorization *);
};

struct EnrollReq_ : public ZmObject, public Zhttp::ReqBuilder {
  using Headers = EnrollReqHeaders;
  using ContentLength = ZuStringT<"content-length">;

  constexpr Zhttp::BodyPolicy::T bodyPolicy() const {
    return Zhttp::BodyPolicy::Fixed;
  }
  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::POST, [this](auto &&emit) {
      emit([this](auto &tx) { tx << target; });
    });
  }
  template <typename L> void protocol(L &&) const { }
  template <typename Key, typename L>
  void header(L &&l) const {
    if constexpr (Key{}() == "content-type" || Key{}() == "accept")
      l("application/json");
    else if constexpr (Key{}() == "authorization")
      l(Authorization{state});
    else if constexpr (Key{}() == "cache-control")
      l("no-store");
    else if constexpr (ZuIsSame<Key, ContentLength>{})
      l(Zhttp::contentLengthPad());
  }
  template <typename L> void header(L &&) const { }
  template <typename Emit>
  void body(Emit &&emit) {
    emit([this](auto &tx) {
      const auto &pending = state->enrollment.p<PendingEnroll>();
      tx << pending.body;
      bodyLength = pending.body.length();
      return Zhttp::WriteOutcome::End;
    });
  }
  template <typename L>
  void bodyHdrs(L &&l) const { Zhttp::contentLengthSet(l, bodyLength); }

  void connected(const Zhttp::ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }
  void completed(const Zhttp::Result &);

  uint64_t key() const { return key_; }
  uint64_t length() const { return 1; }

  State		*state = nullptr;
  EnrollClient	*client = nullptr;
  Zhttp::URLString target;
  EnrollBody	response;
  uint64_t	key_ = 0;
  uint64_t	bodyLength = 0;
  unsigned	status = 0;
  bool		contentType = false;
  bool		cacheControl = false;
  bool		overflow = false;
};

struct EnrollRes : public Zhttp::Parser {
  using Headers = EnrollResHeaders;

  bool enable1xx() const { return false; }
  void init(const EnrollReq_ &req_) {
    req = const_cast<EnrollReq_ *>(&req_);
    req->response.length(0);
    req->status = 0;
    req->contentType = false;
    req->cacheControl = false;
    req->overflow = false;
  }
  void reset() { req = nullptr; }
  void status(unsigned value) { req->status = value; }
  bool bodyInfo(Zhttp::BodyType::T, uint64_t length) {
    if (length > req->state->cf.maxFrame) req->overflow = true;
    return !req->overflow;
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "content-type") {
      ZuBSpan json{"application/json"};
      req->contentType = value.length() >= json.length() &&
	!Zu::stricmp_(reinterpret_cast<const char *>(value.data()),
	  reinterpret_cast<const char *>(json.data()), json.length());
      if (req->contentType && value.length() != json.length()) {
	unsigned i = json.length();
	while (i < value.length() && (value[i] == ' ' || value[i] == '\t')) ++i;
	req->contentType = i < value.length() && value[i] == ';';
      }
    } else if constexpr (Key{}() == "cache-control") {
      req->cacheControl = ZuBSpan{value} == ZuBSpan{"no-store"};
    }
  }
  template <typename Rx>
  bool body(Rx &rx) {
    return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> value) {
      if (req->overflow || value.length() >
	  req->state->cf.maxFrame - req->response.length()) {
	req->overflow = true;
	return;
      }
      req->response << value;
    }) && !req->overflow;
  }
  template <typename Link> void complete(Link *, bool) { }

  EnrollReq_	*req = nullptr;
};

ZmPQueueDerive(EnrollReqQ, EnrollReq_,
  ZmPQueueOverlap<false, ZmPQueueNode<EnrollReq_,
    ZmPQueueHeapID<"Ztc.Agent.EnrollReq">>>);
using EnrollReq = EnrollReqQ::Node;
using EnrollTxQ =
  ZmPQTx<EnrollPool, EnrollReqQ, ZmPQTxOrdered<false>>;

template <typename Heap>
struct EnrollPool_ : public Heap,
    public Zhttp::Pool<EnrollClient, EnrollTxQ, EnrollRes> {
  using Base = Zhttp::Pool<EnrollClient, EnrollTxQ, EnrollRes>;
  EnrollPool_(EnrollClient *client) : Base{client} { }
  EnrollReqQ *txQueue() { return &requests; }
  void archive_(EnrollReq *) { }
  ZmRef<EnrollReq> retrieve_(EnrollReqQ::Key, EnrollReqQ::Key) {
    return {};
  }
  EnrollReqQ requests;
};
using EnrollPoolHeap =
  ZmHeap<"Ztc.Agent.EnrollPool", EnrollPool_<>>;
struct EnrollPool : public EnrollPool_<EnrollPoolHeap> {
  using EnrollPool_<EnrollPoolHeap>::EnrollPool_;
};

struct EnrollClient : public Zhttp::Client<EnrollClient, EnrollPool> {
  using Base = Zhttp::Client<EnrollClient, EnrollPool>;

  EnrollClient(State *state_) : state{state_} { }
  void idle() { }
  bool begin();
  void completed(EnrollReq_ *, const Zhttp::Result &);
  void retry();
  void cancelRetry() {
    state->mx->del(&retryTimer);
  }

  State		*state;
  Zhttp::URL	url;
  ZmScheduler::Timer retryTimer;
  uint64_t	nextKey = 1;
};

static void startDone(State *state, bool ok)
{
  Agent::CtrlFn fn;
  {
    ZmGuard guard{state->ctrlLock};
    if (!state->startPending) return;
    state->startPending = false;
    fn = ZuMv(state->startFn);
  }
  if (fn) fn(ok);
}

void EnrollReq_::completed(const Zhttp::Result &result)
{
  client->completed(this, result);
}

static bool startHub(State *state)
{
  auto &identity = state->enrollment.p<Identity>();
  if (!state->agent->Ztls::Client<Agent>::init(Ztls::ClientParams{
	state->mx, state->cf.mx.rxThread, state->cf.mx.txThread}
      .creds(ZuMv(identity.creds)).caCerts(ZuMv(identity.caCerts)))) {
    ZiLOG(Error, "Ztc.Agent", "could not initialize hub TLS client");
    return false;
  }
  state->baseInit = true;
  if (!state->agent->Ztls::Client<Agent>::start()) {
    ZiLOG(Error, "Ztc.Agent", "could not start hub TLS client");
    state->agent->Ztls::Client<Agent>::final();
    state->baseInit = false;
    return false;
  }
  state->hubLink = new Agent::Link{state->agent};
  state->hubLink->connect(identity.host, identity.port);
  return true;
}

bool EnrollClient::begin()
{
  if (!url.assign(state->env.enrollURL).ok()) return false;
  auto view = url.url();
  if (!view.ok() || view.scheme != Zhttp::Scheme::https ||
      view.hasFragment || !view.host || !view.port)
    return false;
  auto config = Zhttp::Config{}.links(1).concurrency(1).linkMax(1)
    .requestTimeout(0).maxRedirects(0).maxRetries(0)
    .retainedBodyMax(state->cf.maxFrame)
    .retainedMessageMax(state->cf.maxFrame)
    .protocol(Zhttp::ProtoPolicy::DisableH3)
    .h2Policy(Zhttp::H2Policy::Prefer)
    .secure(true).tcp(false).tls(true).quic(false);
  if (!Base::init(
      Zhttp::HubConfig{state->mx,
	state->cf.mx.rxThread, state->cf.mx.txThread},
      1, config, Zhttp::TCPConfig{}, Zhttp::H2Config{}) ||
      !pool(0, Zhttp::Destination{
	view.host, view.port, view.ipv6Literal}) || !Base::start())
    return false;
  state->mx->run([state = state]() {
    if (auto client = state->enrollClient) client->retry();
  }, state->cf.routeThread);
  return true;
}

void EnrollClient::retry()
{
  if (state->phase != Phase::Enrolling ||
      !state->enrollment.is<PendingEnroll>())
    return;
  auto request = ZmRef<EnrollReq>{new EnrollReq};
  request->state = state;
  request->client = this;
  request->key_ = nextKey++;
  url.url().writeTarget(request->target);
  if (!send(0, ZuMv(request))) {
    State *state_ = state;
    state->mx->run([state_]() {
      if (state_->phase != Phase::Enrolling) return;
      ZiLOG(Error, "Ztc.Agent", "could not dispatch enrollment request");
      state_->phase = Phase::Down;
      startDone(state_, false);
    }, state->cf.routeThread);
  }
}

void EnrollClient::completed(
    EnrollReq_ *request, const Zhttp::Result &result)
{
  EnrollBody body = ZuMv(request->response);
  unsigned status = request->status;
  bool contentType = request->contentType;
  bool cacheControl = request->cacheControl;
  bool overflow = request->overflow;
  State *state_ = state;
  state->mx->run([
    state_, result, body = ZuMv(body), status,
    contentType, cacheControl, overflow
  ]() mutable {
    if (state_->phase != Phase::Enrolling) return;
    if (result.code == Zhttp::ResultCode::Indeterminate) {
      auto client = state_->enrollClient;
      if (!client) return;
      state_->mx->add(&client->retryTimer,
	Zm::now(int(state_->cf.enrollRetry)), ZmScheduler::Update,
	[client](auto &&arm) {
	  return arm([client]() { client->retry(); });
	}, state_->cf.routeThread);
      return;
    }
    if (!result.ok() || status != 201 || !contentType ||
	!cacheControl || overflow) {
      ZiLOG(Error, "Ztc.Agent", "enrollment rejected");
      state_->phase = Phase::Down;
      startDone(state_, false);
      return;
    }
    try {
      unsigned bodyLength = body.length();
      auto scan = ZfJSON::scan(body);
      if (scan.p<0>() != int(bodyLength) || !scan.p<1>())
	throw ZeEXCEPT(Error, "Ztc.Agent", "invalid enrollment JSON");
      EnrollResponse response =
	ZfJSON::handler<EnrollResponse>((*scan.p<1>())[0]).ctor();
      auto &pending = state_->enrollment.p<PendingEnroll>();
      ZuID agentID{response.agentID};
      if (response.protocolVersion != 1 || response.nonce != pending.nonce ||
	  agentID != response.agentID || !response.certificateChain ||
	  !validHubHost(response.hubHost) || !response.hubPort)
	throw ZeEXCEPT(Error, "Ztc.Agent", "invalid enrollment response");
      Identity identity{
	.creds = Ztls::ClientCreds{
	  ZuMv(pending.key), ZuMv(response.certificateChain)},
	.caCerts = ZuMv(response.hubCAChain),
	.host = ZuMv(response.hubHost),
	.agentID = ZuMv(agentID),
	.port = response.hubPort
      };
      state_->enrollment.p<Identity>(ZuMv(identity));
      unsigned tokenLength = state_->env.token.length();
      if (tokenLength)
	memset(state_->env.token.ensure(tokenLength + 1), 0, tokenLength);
      state_->env.token.length(0);
      state_->phase = Phase::Connecting;
      if (state_->enrollClient) {
	state_->enrollClient->cancelRetry();
	state_->enrollClient->stop();
	state_->enrollClient->final();
	delete state_->enrollClient;
	state_->enrollClient = nullptr;
      }
      if (!startHub(state_)) state_->phase = Phase::Down;
      if (state_->phase == Phase::Down) startDone(state_, false);
    } catch (const ZeException &e) {
      ZiLog::log(e);
      state_->phase = Phase::Down;
      startDone(state_, false);
    }
  }, state->cf.routeThread);
}

} // Agent_

static bool rewriteSeq(Hdr *hdr, uint64_t seqNo)
{
  auto message = flatbuffers::GetMutableRoot<fbs::Msg>(
    const_cast<uint8_t *>(hdr->data()));
  switch (message->body_type()) {
    case fbs::Body::Telemetry:
      return message->mutable_body_as_Telemetry()->mutate_seqNo(seqNo);
    case fbs::Body::Ack:
      return message->mutable_body_as_Ack()->mutate_seqNo(seqNo);
    case fbs::Body::Error: {
      auto error = message->mutable_body_as_Error();
      return error->seqNo() != ZuCmp<uint64_t>::null() &&
	error->mutate_seqNo(seqNo);
    }
    case fbs::Body::EOS:
      return message->mutable_body_as_EOS()->mutate_seqNo(seqNo);
    default:
      return false;
  }
}

static bool validCf(const AgentCf &cf)
{
  uint64_t frame = cf.maxFrame;
  return cf.maxFrame >= sizeof(Hdr) && cf.telFrames && cf.reqFrames &&
    cf.telFrames < UINT_MAX && cf.fanoutBatch &&
    uint64_t(cf.telFrames) + 1 <= SIZE_MAX / sizeof(unsigned) &&
    cf.telBytes >= frame && cf.reqBytes >= frame &&
    cf.telSize >= frame + Zm::CacheLineSize && cf.reconnMin &&
    cf.reconnMin <= cf.reconnMax && cf.reconnBackoff >= 1 &&
    cf.routeThread > 2 && cf.routeThread <= cf.mx.nThreads &&
    cf.mx.rxThread && cf.mx.txThread;
}

static bool validEnrollURL(ZuBSpan value)
{
  Zhttp::URL url;
  if (!url.assign(value).ok()) return false;
  auto view = url.url();
  return view.ok() && view.scheme == Zhttp::Scheme::https &&
    !view.hasFragment && view.host && view.port;
}

static bool validHubHost(ZuCSpan host)
{
  unsigned length = host.length();
  // RFC 1035 DNS presentation-name and label bounds.
  if (!length || length > 253) return false;
  ZiIP ip;
  if (ZiIP::parse(ip, host)) return false;
  unsigned label = 0;
  for (unsigned i = 0; i < length; ++i) {
    uint8_t c = uint8_t(host[i]);
    if (c == '.') {
      if (!label || host[i - 1] == '-') return false;
      label = 0;
      continue;
    }
    if ((!label && c == '-') ||
	!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	  (c >= '0' && c <= '9') || c == '-') || ++label > 63)
      return false;
  }
  return label && host[length - 1] != '-';
}

static ZiMxParams mxParams(const AgentCf &cf)
{
  ZiMxParams params;
  params.scheduler([cf](auto &sched) {
    sched.nThreads(cf.mx.nThreads)
      .thread(1, [name = cf.mx.rxThread](auto &thread) {
	thread.name(name);
	thread.isolated(true);
      })
      .thread(2, [name = cf.mx.txThread](auto &thread) {
	thread.name(name);
	thread.isolated(true);
      })
      .thread(cf.routeThread, [](auto &thread) {
	thread.name("route");
	thread.isolated(true);
      });
    if (cf.mx.stackSize) sched.stackSize(cf.mx.stackSize);
    if (cf.mx.quantum > 0) sched.quantum(cf.mx.quantum);
    if (cf.mx.queueSize) sched.queueSize(cf.mx.queueSize);
  }).rxThread(1).txThread(2)
    .rxBufSize(cf.mx.rcvBufSize).txBufSize(cf.mx.sndBufSize);
  return params;
}

Agent::~Agent()
{
  final();
}

bool Agent::init(const AgentCf &cf, AgentEnv env)
{
  if (m_state || !validCf(cf) || !env.token || !env.ring ||
      !env.pidDir || !env.enrollURL || !validEnrollURL(env.enrollURL))
    return false;
  auto state = new Agent_::State{this, cf, ZuMv(env)};
  try {
    state->mx = new ZiMultiplex{mxParams(cf)};
  } catch (...) {
    delete state->mx;
    delete state;
    return false;
  }
  m_state = state;
  return true;
}

static void returnTelCredit(Agent_::State *state, unsigned size)
{
  auto credit = static_cast<unsigned *>(state->telCredits.tryPush());
  ZiAssert(credit, "Ztc.Agent", (), "telemetry credit ring overflow", return);
  *credit = size;
  state->telCredits.push2();
}

static void delReq(Agent_::State *, Agent_::Req *);
static void delLeg(Agent_::LegList::Node *);
static void delApp(Agent_::State *, Agent_::App *);
static void attachPeriodic(Agent_::State *, Agent_::App *);
static void fenceRoute(Agent_::State *, uint64_t);

static void discoverApp(Agent_::State *state, const fbs::Telemetry *telemetry)
{
  ZiAssert(state->mx->invoked(state->cf.routeThread), "Ztc.Agent", (),
    "application discovery outside route thread", return);
  auto value = telemetry->value_as_AppTelemetry();
  if (!value) return;
  ZuCSpan id_ = Zfb::Load::str(telemetry->id());
  if (!id_) return;
  ZuID id{id_};
  if (id != id_) return;

  auto existing = state->apps.findPtr(id);
  if (!telemetry->seqNo() && existing) {
    delApp(state, existing);
    existing = nullptr;
  }
  uint32_t ztcver = value->ztcver();
  if (!Ver::compatible(Z_VERSION, ztcver)) return;
  if (existing) {
    existing->ztcver = ztcver;
    return;
  }
  auto app = new Agent_::Apps::Node{id, ztcver, state->cf};
  if (!app->ready) {
    delete app;
    return;
  }
  state->apps.addNode(app);
  attachPeriodic(state, app);
}

static void hubSend(
    Agent_::State *state, ZmRef<ZiIOBuf> buf, uint64_t cxnGen)
{
  state->agent->rxRun([state, buf = ZuMv(buf), cxnGen]() mutable {
    if (cxnGen != state->currentCxnGen || !state->currentLink) return;
    if (!state->currentLink->send(ZuMv(buf))) {
      ZiLOG(Warning, "Ztc.Agent", "hub transmit backpressure overflow");
      state->currentLink->disconnect();
    }
  });
}

static void hubClose(Agent_::State *state, uint64_t cxnGen)
{
  state->agent->rxRun([state, cxnGen]() {
    if (cxnGen == state->currentCxnGen && state->currentLink)
      state->currentLink->disconnect();
  });
}

static void routeTel(Agent_::State *state, ZmRef<ZiIOBuf> buf)
{
  ZiAssert(state->mx->invoked(state->cf.routeThread), "Ztc.Agent", (),
    "telemetry routing outside route thread", return);
  unsigned size = buf->length;
  auto message = msg(buf->ptr<Hdr>());
  if (!message) {
    returnTelCredit(state, size);
    return;
  }
  ZuCSpan id_;
  uint64_t seqNo = ZuCmp<uint64_t>::null();
  bool terminal = false;
  switch (message->body_type()) {
    case fbs::Body::Telemetry: {
      auto telemetry = message->body_as_Telemetry();
      if (!telemetry) break;
      if (telemetry->value_type() == fbs::TelemetryBody::AppTelemetry)
	discoverApp(state, telemetry);
      if (!telemetry->seqNo()) break;
      id_ = Zfb::Load::str(telemetry->id());
      seqNo = telemetry->seqNo();
      break;
    }
    case fbs::Body::Ack: {
      auto ack = message->body_as_Ack();
      if (!ack) break;
      id_ = Zfb::Load::str(ack->id());
      seqNo = ack->seqNo();
      terminal = ack->status() == fbs::AckStatus::Invalid ||
	ack->status() == fbs::AckStatus::Failed;
      break;
    }
    case fbs::Body::EOS: {
      auto eos = message->body_as_EOS();
      if (!eos) break;
      id_ = Zfb::Load::str(eos->id());
      seqNo = eos->seqNo();
      terminal = true;
      break;
    }
    case fbs::Body::Error: {
      auto error = message->body_as_Error();
      if (!error) break;
      id_ = Zfb::Load::str(error->id());
      seqNo = error->seqNo();
      if (seqNo == ZuCmp<uint64_t>::null()) {
	ZuID id{id_};
	if (id == id_ && state->apps.findPtr(id) && state->routeCxnGen)
	  hubSend(state, ZuMv(buf), state->routeCxnGen);
	returnTelCredit(state, size);
	return;
      }
      terminal = true;
      break;
    }
    default:
      break;
  }
  if (seqNo != ZuCmp<uint64_t>::null() && id_) {
    ZuID id{id_};
    auto app = id == id_ ? state->apps.findPtr(id) : nullptr;
    auto leg = app ? app->legs.findPtr(seqNo) : nullptr;
    if (leg && leg->req->cxnGen == state->routeCxnGen &&
	rewriteSeq(buf->ptr<Hdr>(), leg->req->request.seqNo)) {
      auto req = leg->req;
      uint64_t cxnGen = req->cxnGen;
      bool del = terminal &&
	(message->body_type() != fbs::Body::EOS || !req->request.interval);
      hubSend(state, ZuMv(buf), cxnGen);
      if (del) {
	delLeg(leg);
	if (!req->request.interval && !req->pending.count_() &&
	    !req->legs.count_())
	  delReq(state, req);
      }
    }
  }
  returnTelCredit(state, size);
}

static void drainTelCredits(Agent_::State *state)
{
  while (auto credit = state->telCredits.tryShift()) {
    if (!*credit) {
      state->telOverflowPending = false;
      state->telCredits.shift2();
      continue;
    }
    ZiAssert(state->telFrames && state->telBytes >= *credit,
	"Ztc.Agent", (), "invalid telemetry credit", return);
    --state->telFrames;
    state->telBytes -= *credit;
    state->telCredits.shift2();
  }
}

static ZmRef<ZiIOBuf> requestFrame(const Request &request)
{
  Zfb::IOBuilder fbb{frameBuf(ZmRef<ZiIOBuf>{new Agent_::Frame})};
  auto request_ = ZfbStruct::save(fbb, request);
  fbb.Finish(fbs::CreateMsg(
    fbb, fbs::Body::Request, request_.Union()));
  return saveHdr(fbb);
}

static ZmRef<ZiIOBuf> appRequestFrame(
    const ZuID &id, uint64_t seqNo, bool subscribe)
{
  Request request;
  request.id = id;
  request.seqNo = seqNo;
  request.group = uint8_t(fbs::Group::App);
  request.subscribe = subscribe;
  return requestFrame(request);
}

static ZmRef<ZiIOBuf> errorFrame(
    const ZuID &id, uint64_t seqNo, int32_t code, ZuCSpan message)
{
  Zfb::IOBuilder fbb{frameBuf(ZmRef<ZiIOBuf>{new Agent_::Frame})};
  Error native{
    .message = message,
    .id = id,
    .seqNo = seqNo,
    .code = code
  };
  auto error = ZfbStruct::save(fbb, native);
  fbb.Finish(fbs::CreateMsg(
    fbb, fbs::Body::Error, error.Union()));
  return saveHdr(fbb);
}

static bool writeRequest(
    Agent_::State *state, Ring &ring, ZmRef<ZiIOBuf> buf)
{
  if (!buf) return false;
  for (;;) {
    if (void *ptr = ring.push(buf->length)) {
      memcpy(ptr, buf->data(), buf->length);
      ring.push2(ptr, buf->length);
      return true;
    }
    int status = ring.writeStatus();
    if (status < 0) return false;
    ring.kill();
    if (state->stop.load_()) return false;
  }
}

static bool scanApps(Agent_::State *state)
{
  ZiAssert(state->mx->invoked(state->cf.routeThread), "Ztc.Agent", (),
    "application scan outside route thread", return false);
  ZiDir dir;
  if (dir.open(ZiFile::append(ZiFile::tmpDir(), state->env.pidDir)) !=
      Zi::OK) return false;
  Zi::Path name;
  for (;;) {
    int result = dir.read(name);
    if (result == Zi::EndOfFile) break;
    if (result != Zi::OK) return false;
    unsigned length = name.length();
    if (name == "." || name == ".." || name == "ztcagent.pid" ||
	length <= 4 || name.rfind(".pid") != int64_t(length - 4))
      continue;
    ZuCSpan stem{name.data(), length - 4};
    if (stem.length() > ZuIDSize) continue;
    ZuID id{stem};
    if (id != stem) continue;
    Ring ring{ZiRingParams{id, 0}.timeout(state->cf.reqTimeout)
      .killWait(state->cf.reqKillWait).coredump(state->cf.reqCoredump)};
    if (ring.open(Ring::Write) != Zu::OK || ring.writeStatus() < 0)
      continue;

    if (!writeRequest(state, ring, appRequestFrame(
	  id, ZuCmp<uint64_t>::null(), false)))
      continue;
    uint64_t seqNo = state->nextSeqNo;
    if (seqNo == ZuCmp<uint64_t>::null()) return false;
    ++state->nextSeqNo;
    (void)writeRequest(state, ring, appRequestFrame(id, seqNo, true));
  }
  return true;
}

static void returnReqCredit(
    Agent_::State *state, unsigned size, uint64_t cxnGen)
{
  state->agent->rxRun([state, size, cxnGen]() {
    if (cxnGen != state->currentCxnGen) return;
    ZiAssert(state->reqPendingFrames && state->reqPendingBytes >= size,
	"Ztc.Agent", (), "invalid request credit", return);
    --state->reqPendingFrames;
    state->reqPendingBytes -= size;
  });
}

static void delLeg(Agent_::LegList::Node *leg)
{
  auto node = static_cast<Agent_::LegHash::Node *>(leg);
  auto req = leg->req;
  bool pending = false;
  {
    auto i = req->pending.iter();
    while (auto candidate = i()) {
      if (candidate == leg) {
	pending = true;
	break;
      }
    }
  }
  if (pending)
    req->pending.delNode(leg);
  else
    req->legs.delNode(leg);
  leg->app->legs.delNode(node);
}

static void delReq(Agent_::State *state, Agent_::Req *req)
{
  while (auto leg = req->pending.shift()) delLeg(leg);
  while (auto leg = req->legs.shift()) {
    Request local = req->request;
    local.id = leg->app->id;
    local.seqNo = leg->seqNo;
    local.subscribe = false;
    (void)writeRequest(state, leg->app->ring, requestFrame(local));
    delLeg(leg);
  }
  state->reqs.delNode(static_cast<Agent_::Reqs::Node *>(req));
}

static void delApp(Agent_::State *state, Agent_::App *app)
{
  while (app->legs.count_()) {
    Agent_::LegList::Node *leg = nullptr;
    {
      auto i = app->legs.iter();
      leg = i();
    }
    if (!leg) break;
    auto req = leg->req;
    delLeg(leg);
    if (!req->request.interval && !req->pending.count_() &&
	!req->legs.count_())
      delReq(state, req);
  }
  state->apps.delNode(static_cast<Agent_::Apps::Node *>(app));
}

static void exhaustSeq(Agent_::State *state)
{
  uint64_t cxnGen = state->routeCxnGen;
  if (!cxnGen) return;
  hubClose(state, cxnGen);
  state->mx->run([state, cxnGen]() { fenceRoute(state, cxnGen); },
    state->cf.routeThread);
}

static bool addLeg(
    Agent_::State *state, Agent_::Req *req, Agent_::App *app)
{
  uint64_t seqNo = state->nextSeqNo;
  if (seqNo == ZuCmp<uint64_t>::null()) {
    exhaustSeq(state);
    return false;
  }
  ++state->nextSeqNo;
  auto leg = new Agent_::LegHash::Node{app, req, seqNo};
  app->legs.addNode(leg);
  req->pending.pushNode(leg);
  if (state->nextSeqNo == ZuCmp<uint64_t>::null()) exhaustSeq(state);
  return true;
}

static void drainReq(
    Agent_::State *state, uint64_t hubSeqNo, uint64_t cxnGen)
{
  if (cxnGen != state->routeCxnGen) return;
  auto req = state->reqs.findPtr(hubSeqNo);
  if (!req || req->cxnGen != cxnGen) return;
  unsigned n = 0;
  while (n < state->cf.fanoutBatch) {
    auto leg = req->pending.shift();
    if (!leg) break;
    Request request = req->request;
    request.seqNo = leg->seqNo;
    if (writeRequest(state, leg->app->ring, requestFrame(request)))
      req->legs.pushNode(leg);
    else {
      auto app = leg->app;
      delLeg(leg);
      delApp(state, app);
    }
    ++n;
  }
  if (req->pending.count_()) {
    state->mx->run([state, hubSeqNo, cxnGen]() {
      drainReq(state, hubSeqNo, cxnGen);
    }, state->cf.routeThread);
    return;
  }
  if (!req->legs.count_() && !req->request.interval) delReq(state, req);
}

static bool hasLeg(Agent_::Req *req, Agent_::App *app)
{
  {
    auto i = req->pending.iter();
    while (auto leg = i()) if (leg->app == app) return true;
  }
  auto i = req->legs.iter();
  while (auto leg = i()) if (leg->app == app) return true;
  return false;
}

static void attachPeriodic(Agent_::State *state, Agent_::App *app)
{
  auto i = state->reqs.iter();
  while (auto req = i()) {
    if (!req->request.interval ||
	(req->request.id && req->request.id != app->id) ||
	req->excl.findPtr(app->id) || hasLeg(req, app))
      continue;
    bool empty = !req->pending.count_();
    if (!addLeg(state, req, app)) continue;
    if (empty) {
      uint64_t seqNo = req->request.seqNo;
      uint64_t cxnGen = req->cxnGen;
      state->mx->run([state, seqNo, cxnGen]() {
	drainReq(state, seqNo, cxnGen);
      }, state->cf.routeThread);
    }
  }
}

static Agent_::LegList::Node *findLeg(
    Agent_::LegList &legs, const ZuID &id)
{
  auto i = legs.iter();
  while (auto leg = i()) if (leg->app->id == id) return leg;
  return nullptr;
}

static void unsubscribe(
    Agent_::State *state, Agent_::Req *req, const Request &request)
{
  if (!request.id) {
    delReq(state, req);
    return;
  }
  ZuID id{request.id};
  if (id != request.id) return;
  if (req->request.interval && !req->request.id &&
      !req->excl.findPtr(id))
    req->excl.add(id);
  if (auto leg = findLeg(req->pending, id)) {
    delLeg(leg);
  } else if (auto leg = findLeg(req->legs, id)) {
    Request local = req->request;
    local.id = id;
    local.seqNo = leg->seqNo;
    local.subscribe = false;
    (void)writeRequest(state, leg->app->ring, requestFrame(local));
    delLeg(leg);
  }
  if (req->request.id == id ||
      (!req->request.interval && !req->pending.count_() &&
	!req->legs.count_()))
    delReq(state, req);
}

static void admitReq(
    Agent_::State *state, const ZiIOBuf *buf, uint64_t cxnGen)
{
  if (cxnGen == state->routeCxnGen) {
    auto message = msg(buf->ptr<Hdr>());
    auto request_ = message && message->body_type() == fbs::Body::Request ?
      message->body_as_Request() : nullptr;
    if (request_) {
      Request request = ZfbStruct::ctor<Request>(request_);
      uint64_t seqNo = request.seqNo;
      if (seqNo != ZuCmp<uint64_t>::null()) {
	if (!request.subscribe) {
	  if (auto req = state->reqs.findPtr(seqNo))
	    unsubscribe(state, req, request);
	} else if (seqNo >= state->minHubSeqNo) {
	  state->minHubSeqNo = seqNo + 1;
	  if (state->minHubSeqNo == ZuCmp<uint64_t>::null()) {
	    fenceRoute(state, cxnGen);
	    hubClose(state, cxnGen);
	  } else if (!state->reqs.findPtr(seqNo)) {
	    if (request.id && !state->apps.findPtr(request.id)) {
	      hubSend(state, errorFrame(request.id, seqNo, 1,
		  "app not running"), cxnGen);
	      return;
	    }
	    auto req = new Agent_::Reqs::Node{ZuMv(request), cxnGen};
	    state->reqs.addNode(req);
	    if (req->request.id) {
	      (void)addLeg(state, req,
		state->apps.findPtr(req->request.id));
	    } else {
	      auto i = state->apps.iter();
	      while (auto app = i()) (void)addLeg(state, req, app);
	    }
	    if (req->pending.count_())
	      drainReq(state, seqNo, cxnGen);
	    else if (!req->request.interval)
	      delReq(state, req);
	  }
	}
      }
    }
  }
}

static void routeReq(
    Agent_::State *state, ZmRef<ZiIOBuf> buf, uint64_t cxnGen)
{
  ZiAssert(state->mx->invoked(state->cf.routeThread), "Ztc.Agent", (),
    "request routing outside route thread", return);
  unsigned size = buf->length;
  admitReq(state, buf, cxnGen);
  returnReqCredit(state, size, cxnGen);
}

static void fenceRoute(Agent_::State *state, uint64_t cxnGen)
{
  ZiAssert(state->mx->invoked(state->cf.routeThread), "Ztc.Agent", (),
    "generation fence outside route thread", return);
  if (cxnGen && state->routeCxnGen != cxnGen) return;
  state->routeCxnGen = 0;
  while (state->reqs.count_()) {
    Agent_::Req *req = nullptr;
    {
      auto i = state->reqs.iter();
      req = i();
    }
    if (!req) break;
    delReq(state, req);
  }
}

static void telOverflow(
    Agent_::State *state, uint64_t lostFrames, uint64_t lostBytes)
{
  ZiLOG(Warning, "Ztc.Agent", ([lostFrames, lostBytes](auto &s) {
    s << "telemetry handoff overflow (lost_frames=" << lostFrames <<
      ", lost_bytes=" << lostBytes << ')';
  }));
  uint64_t cxnGen = state->routeCxnGen;
  fenceRoute(state, cxnGen);
  hubClose(state, cxnGen);
  returnTelCredit(state, 0);
}

static void telRun(Agent_::State *state)
{
  ZiAssert(state->telThread.tid() == Zm::getTID(), "Ztc.Agent", (),
    "telemetry reader outside telemetry thread", return);
  while (!state->stop.load_()) {
    drainTelCredits(state);
    auto ptr = state->telRing.shift();
    if (!ptr) {
      if (state->stop.load_()) break;
      int status = state->telRing.readStatus();
      if (status >= 0 || status == Zu::NotReady) continue;
      break;
    }
    unsigned size = ringSize(ptr);
    auto hdr = static_cast<const Hdr *>(ptr);
    bool valid = size <= state->cf.maxFrame && msg(hdr);
    bool admitted = false;
    if (valid && state->telFrames < state->cf.telFrames &&
	state->telBytes <= state->cf.telBytes - size) {
      ZmRef<ZiIOBuf> buf = new Agent_::Frame;
      if (buf->alloc(size)) {
	memcpy(buf->data(), ptr, size);
	buf->length = size;
	++state->telFrames;
	state->telBytes += size;
	state->mx->run([
	  state, buf = ZuMv(buf)
	]() mutable { routeTel(state, ZuMv(buf)); }, state->cf.routeThread);
	admitted = true;
      }
    }
    if (!admitted) {
      ++state->telLostFrames;
      state->telLostBytes += size;
      if (valid && !state->telOverflowPending) {
	state->telOverflowPending = true;
	uint64_t lostFrames = state->telLostFrames;
	uint64_t lostBytes = state->telLostBytes;
	state->mx->run([state, lostFrames, lostBytes]() {
	  telOverflow(state, lostFrames, lostBytes);
	},
	  state->cf.routeThread);
      }
    }
    state->telRing.shift2(size);
  }
  drainTelCredits(state);
}

static bool prepareEnrollment(Agent_::State *state)
{
  if (state->phase != Agent_::Phase::Down ||
      state->enrollment.type() != 0)
    return false;
  try {
    Ztls::Random rng;
    if (!rng.init()) return false;
    ZmRef<Ztls::PK::SK_ED25519> key =
      new Ztls::PK::SK_ED25519{rng};
    Agent_::CSR csr;
    auto result = key->saveCSR(csr);
    if (result.template is<ZeException>()) {
      ZiLog::log(ZuMv(result.template p<ZeException>()));
      return false;
    }
    Agent_::Nonce nonce;
    nonce.length(32);
    if (!rng.random(nonce)) return false;

    ZtString<ZtStringHeapID<"Ztc.Agent.Version">> agentVersion;
    agentVersion << ZuBoxed(Z_VMAJOR) << '.' <<
      ZuBoxed(Z_VMINOR) << '.' << ZuBoxed(Z_VPATCH);
    Agent_::EnrollRequest request{
      .csr = csr,
      .nonce = nonce,
      .deviceID = state->env.deviceID,
      .agentVersion = agentVersion
    };
    Agent_::EnrollBody body;
    ZfJSON::save(body, request);
    if (!body || body.length() > state->cf.maxFrame) return false;

    auto pending = state->enrollment.new_<Agent_::PendingEnroll>();
    new (pending) Agent_::PendingEnroll{
      .key = ZuMv(key),
      .csr = ZuMv(csr),
      .nonce = ZuMv(nonce),
      .body = ZuMv(body)
    };
    state->phase = Agent_::Phase::Enrolling;
    return true;
  } catch (const ZeException &e) {
    ZiLog::log(e);
    return false;
  }
}

bool Agent::startAgent_()
{
  if (!m_state) return false;
  if (m_state->running) return true;

  Zi::Path pidName = ZiFile::append(
    m_state->env.pidDir, Zi::Path{"ztcagent.pid"});
  if (m_state->pidFile.init(ZiFile::tmpDir(), pidName) != ZiPIDFile::OK) {
    ZiLOG(Error, "Ztc.Agent", "could not acquire PID file");
    return false;
  }
  if (!m_state->mx->start()) {
    ZiLOG(Error, "Ztc.Agent", "could not start multiplexer");
    m_state->pidFile.final();
    return false;
  }
  m_state->telRing.init(
    ZiRingParams{m_state->env.ring, m_state->cf.telSize}
      .ll(m_state->cf.telLL).spin(m_state->cf.telSpin)
      .timeout(m_state->cf.telTimeout));
  if (m_state->telRing.open(Ring::Read) != Zu::OK ||
      m_state->telRing.attach() != Zu::OK) {
    ZiLOG(Error, "Ztc.Agent", "could not open telemetry ring");
    m_state->telRing.close();
    m_state->mx->stop();
    m_state->pidFile.final();
    return false;
  }
  m_state->stop = 0;
  if (m_state->telThread.run(
      [state = m_state]() { telRun(state); },
      ZmThreadParams{}.name("ztcTel")) < 0) {
    ZiLOG(Error, "Ztc.Agent", "could not start telemetry reader");
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->mx->stop();
    m_state->pidFile.final();
    return false;
  }
  ZmSemaphore scanDone;
  bool scanOK = false;
  m_state->mx->run([state = m_state, &scanDone, &scanOK]() {
    scanOK = scanApps(state);
    scanDone.post();
  }, m_state->cf.routeThread);
  scanDone.wait();
  if (!scanOK) {
    ZiLOG(Error, "Ztc.Agent", "could not scan publisher registry");
    m_state->stop = 1;
    m_state->telThread.join();
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->mx->stop();
    m_state->pidFile.final();
    return false;
  }
  if (!prepareEnrollment(m_state)) {
    ZiLOG(Error, "Ztc.Agent", "could not prepare enrollment");
    m_state->stop = 1;
    m_state->telThread.join();
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->mx->stop();
    m_state->pidFile.final();
    return false;
  }
  m_state->enrollClient = new Agent_::EnrollClient{m_state};
  if (!m_state->enrollClient->begin()) {
    ZiLOG(Error, "Ztc.Agent", "could not start enrollment client");
    m_state->enrollClient->final();
    delete m_state->enrollClient;
    m_state->enrollClient = nullptr;
    m_state->enrollment.null();
    m_state->phase = Agent_::Phase::Down;
    m_state->stop = 1;
    m_state->telThread.join();
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->mx->stop();
    m_state->pidFile.final();
    return false;
  }
  m_state->running = true;
  return true;
}

void Agent::start(CtrlFn fn)
{
  if (!m_state) {
    if (fn) fn(false);
    return;
  }
  {
    ZmGuard guard{m_state->ctrlLock};
    if (m_state->running) {
      bool up = m_state->phase == Agent_::Phase::Up;
      guard.unlock();
      if (fn) fn(up);
      return;
    }
    if (m_state->startPending) {
      guard.unlock();
      if (fn) fn(false);
      return;
    }
    m_state->startPending = true;
    m_state->startFn = ZuMv(fn);
  }
  if (!startAgent_()) Agent_::startDone(m_state, false);
}

bool Agent::start()
{
  if (!m_state) return false;
  if (m_state->mx && m_state->mx->running()) {
    auto tid = Zm::getTID();
    if (m_state->mx->invoked_(tid, 1) ||
	m_state->mx->invoked_(tid, 2) ||
	m_state->mx->invoked_(tid, m_state->cf.routeThread))
      return false;
  }
  bool ok = ZmBlock<bool>{}(
    [this](auto wake) { start(CtrlFn{ZuMv(wake)}); });
  if (!ok) stop();
  return ok;
}

bool Agent::stop()
{
  if (!m_state || !m_state->running) return true;
  auto tid = Zm::getTID();
  if (m_state->telThread.tid() == tid ||
      m_state->mx->invoked_(tid, 1) ||
      m_state->mx->invoked_(tid, 2) ||
      m_state->mx->invoked_(tid, m_state->cf.routeThread))
    return false;
  Agent_::startDone(m_state, false);
  m_state->stop = 1;
  m_state->phase = Agent_::Phase::Stopping;
  m_state->telThread.join();
  ZmSemaphore routeDone;
  m_state->mx->run([state = m_state, &routeDone]() {
    fenceRoute(state, 0);
    while (state->apps.count_()) {
      Agent_::App *app = nullptr;
      {
	auto i = state->apps.iter();
	app = i();
      }
      if (!app) break;
      delApp(state, app);
    }
    routeDone.post();
  }, m_state->cf.routeThread);
  routeDone.wait();
  drainTelCredits(m_state);
  if (m_state->enrollClient) {
    m_state->enrollClient->cancelRetry();
    m_state->enrollClient->stop();
    m_state->enrollClient->final();
    delete m_state->enrollClient;
    m_state->enrollClient = nullptr;
  }
  if (m_state->telRing.rdrID() >= 0) m_state->telRing.detach();
  m_state->telRing.close();
  if (m_state->baseInit) {
    (void)Ztls::Client<Agent>::stop();
    m_state->hubLink = nullptr;
    Ztls::Client<Agent>::final();
    m_state->baseInit = false;
  }
  m_state->mx->stop();
  m_state->pidFile.final();
  m_state->enrollment.null();
  m_state->phase = Agent_::Phase::Down;
  m_state->running = false;
  return true;
}

void Agent::stop(CtrlFn fn)
{
  bool ok = stop();
  if (fn) fn(ok);
}

void Agent::final()
{
  if (!m_state) return;
  if (!stop()) return;
  delete m_state->mx;
  delete m_state;
  m_state = nullptr;
}

unsigned Agent::reconnFreq()
{
  if (!m_state) return 0;
  auto interval = m_state->reconnInterval;
  m_state->reconnInterval = m_state->reconn.backoff(interval);
  return unsigned(interval.sec());
}

void Agent::Link::connected(Ztls::Connected)
{
  auto state = app()->m_state;
  uint64_t cxnGen = state->nextCxnGen++;
  if (!cxnGen) cxnGen = state->nextCxnGen++;
  m_cxnGen = state->currentCxnGen = cxnGen;
  state->currentLink = this;
  state->reqPendingFrames = 0;
  state->reqPendingBytes = 0;
  state->reconnInterval = state->reconn.initial();
  state->mx->run([state, cxnGen]() {
    fenceRoute(state, 0);
    state->routeCxnGen = cxnGen;
    state->minHubSeqNo = 0;
    if (state->phase == Agent_::Phase::Connecting) {
      state->phase = Agent_::Phase::Up;
      Agent_::startDone(state, true);
    }
  }, state->cf.routeThread);
}

void Agent::Link::disconnected(bool)
{
  auto state = app()->m_state;
  uint64_t cxnGen = m_cxnGen;
  m_cxnGen = 0;
  state->currentCxnGen = 0;
  if (state->currentLink == this) state->currentLink = nullptr;
  state->reqPendingFrames = 0;
  state->reqPendingBytes = 0;
  state->mx->run([state, cxnGen]() {
    fenceRoute(state, cxnGen);
    if (state->phase == Agent_::Phase::Up)
      state->phase = Agent_::Phase::Connecting;
  }, state->cf.routeThread);
  Base::connectFailed(true);
}

void Agent::Link::connectFailed(bool transient)
{
  auto state = app()->m_state;
  ZiLOG(Warning, "Ztc.Agent", ([transient](auto &s) {
    s << "hub connection failed (transient=" << transient << ')';
  }));
  state->mx->run([state]() {
    if (state->phase == Agent_::Phase::Up)
      state->phase = Agent_::Phase::Connecting;
  }, state->cf.routeThread);
  Base::connectFailed(true);
}

int Agent::Link::process(Ztls::RxStream &rx)
{
  while (!rx.empty()) {
    int result = 0;
    int64_t consumed = rx.consume(
      [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      [this, &result](ZuSpan<uint8_t> span) { result = process_(span); });
    if (result < 0 || consumed < 0) return -1;
    if (!consumed) return 0;
  }
  return 1;
}

static bool admitHubFrame(Agent_::State *state, unsigned size)
{
  if (!state->currentCxnGen ||
      state->reqPendingFrames >= state->cf.reqFrames ||
      state->reqPendingBytes > state->cf.reqBytes - size)
    return false;
  ++state->reqPendingFrames;
  state->reqPendingBytes += size;
  return true;
}

int Agent::Link::process_(ZuSpan<uint8_t> span)
{
  unsigned consumed = 0;
  while (consumed < span.length()) {
    if (!m_frame) {
      m_frame = new Agent_::HubRx;
      if (!m_frame->alloc(sizeof(Hdr))) return -1;
      m_size = 0;
    }
    unsigned needed = m_size < sizeof(Hdr) ? sizeof(Hdr) : m_size;
    unsigned have = m_frame->length;
    unsigned n = needed - have;
    if (n > span.length() - consumed) n = span.length() - consumed;
    memcpy(m_frame->data() + have, span.data() + consumed, n);
    m_frame->length += n;
    consumed += n;
    if (m_frame->length < sizeof(Hdr)) continue;
    if (!m_size) {
      int size = loadHdr(m_frame.ptr(), app()->m_state->cf.maxFrame);
      if (size < 0 || size == INT_MAX) return -1;
      m_size = unsigned(size);
      if (!m_frame->ensure(m_size)) return -1;
    }
    if (m_frame->length < m_size) continue;
    auto message = msg(m_frame->ptr<Hdr>());
    if (!message || message->body_type() != fbs::Body::Request) return -1;
    auto state = app()->m_state;
    if (!m_cxnGen || !admitHubFrame(state, m_size)) {
      ZiLOG(Warning, "Ztc.Agent", ([size = m_size](auto &s) {
	s << "hub request handoff overflow (frame_bytes=" << size << ')';
      }));
      return -1;
    }
    auto frame = ZuMv(m_frame);
    uint64_t cxnGen = m_cxnGen;
    m_size = 0;
    state->mx->run([
      state, frame = ZuMv(frame), cxnGen
    ]() mutable { routeReq(state, ZuMv(frame), cxnGen); },
      state->cf.routeThread);
  }
  return int(consumed);
}

} // Ztc
