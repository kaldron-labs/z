//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// OAuth/WSS agent transport and local publisher routing

#include <zlib/ztcagent_daemon.hh>

#include <limits.h>
#include <string.h>

#include <zlib/ZuCmp.hh>
#include <zlib/ZuBase64.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuID.hh>

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZfJSON.hh>

#include <zlib/ZtcAppTypes.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZumURI.hh>

#include "../../zum/example/pinghttp.hh"

namespace Ztc {
namespace Agent_ {

struct Req;
struct Pub_;

struct Discovery {
  Zum::String issuerURL;
  Zum::String tokenEndpoint;
};
ZfStruct(, (Discovery, JSON),
  (((issuerURL), (JSON::ID<"issuer">, Required)), (String)),
  (((tokenEndpoint), (JSON::ID<"token_endpoint">, Required)), (String)));

struct Token {
  Zum::String accessToken;
  Zum::String tokenType;
  uint64_t expiresIn = 0;
};
ZfStruct(, (Token, JSON),
  (((accessToken), (JSON::ID<"access_token">, Required)), (String)),
  (((tokenType), (JSON::ID<"token_type">, Required)), (String)),
  (((expiresIn), (JSON::ID<"expires_in">, Required)), (UInt64)));

struct Pub {
  Pub(ZuID id_, const AgentCf &cf) :
    id{ZuMv(id_)}, ring{ZiRingParams{id, 0}.timeout(cf.reqTimeout)}
  {
    ready = ring.open(Ring::Write) == Zu::OK &&
      ring.writeStatus() >= 0;
  }
  ~Pub() { ring.close(); }

  ZuID	id;
  Ring	ring;
  bool	ready = false;
};

struct Leg {
  Leg(Pub_ *pub_, Req *req_, uint64_t seqNo_) :
    pub{pub_}, req{req_}, seqNo{seqNo_} { }

  Pub_	*pub;
  Req	*req;
  uint64_t	seqNo;
  bool	active = false;
};

ZmListDerive(LegList, Leg, ZmListNode<Leg, ZmListShadow<>>);
inline uint64_t Leg_SeqAxor(const LegList::Node &leg) {
  return leg.val().seqNo;
}
ZmHashDerive(LegHash, typename LegList::Node,
  (ZmHashNode<typename LegList::Node,
    ZmHashKey<Leg_SeqAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Ztc.Agent.PubLeg">>>>));

struct Pub_ : public Pub {
  using Pub::Pub;
  LegHash legs;
  ZmRef<ZiIOBuf> app;
  uint64_t appSeqNo = 0;
};

inline const ZuID &Pub_IDAxor(const Pub_ &pub) { return pub.id; }
ZmHashDerive(Pubs, Pub_,
  (ZmHashNode<Pub_,
    ZmHashKey<Pub_IDAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Ztc.Agent.Pub">>>>));
using PubIdx = ZmRBTreeKV<ZuID, Pub_ *,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Ztc.Agent.PubIdx">>>>;

struct Req {
  Req(Request request_, uint64_t cxnGen_) :
    request{ZuMv(request_)}, cxnGen{cxnGen_} { }

  Request	request;
  LegList	pending;
  LegList	legs;
  uint64_t	cxnGen;
};

inline uint64_t Req_SeqAxor(const Req &req) { return req.request.seqNo; }
ZmHashDerive(Reqs, Req,
  (ZmHashNode<Req,
    ZmHashKey<Req_SeqAxor,
      ZmHashLock<ZmNoLock,
        ZmHashHeapID<"Ztc.Agent.PubReq">>>>));

using Frame = ZiIOBufAlloc<1024, AgentCf::MaxFrame,
  "Ztc.Agent.Frame">;

struct StateData {
  StateData(Agent *agent_, const AgentCf &cf_, AgentEnv env_) :
    agent{agent_}, cf{cf_}, env{ZuMv(env_)}, client{agent_} { }
  ~StateData() {
    if (env.clientSecret.mutable_()) ZuClear(env.clientSecret);
    if (token.mutable_()) ZuClear(token);
  }

  Agent	*agent;
  AgentCf	cf;
  AgentEnv	env;
  ZiMultiplex	*mx = nullptr;
  Zws::URI	uri;
  Zws::Client<Agent, Zhttp::H1TLS>	client;
  Zum::PingHTTP	http;
  Zum::ServiceHTTPFn httpFn;
  AgentString metadataURL;
  AgentString tokenURL;
  AgentSecret token;
  int64_t tokenExpires = 0;
  uint64_t authGeneration = 0;
  bool tokenPending = false;
  bool awaitingProvision = false;
  ZmRef<Agent::Link>	link;
  Ring	telRing;
  ZmThread	telThread;
  ZmAtomic<unsigned>	stop = 0;
  ZmAtomic<uint64_t>	currentGeneration = 0;
  ZmAtomic<unsigned>	pendingTelFrames = 0;
  ZmAtomic<uint64_t>	pendingTelBytes = 0;
  uint64_t	nextGeneration = 1;
  uint64_t	routeGeneration = 0;
  uint64_t	nextAppSeqNo = 1;
  ZuID	pubGCID;
  ZmScheduler::Timer	reconnectTimer;
  ZmScheduler::Timer	pubGCTimer;
  unsigned	reconnectDelay = 1;
  ZtArray<uint8_t, ZtArrayHeapID<"Ztc.Agent.Rx">> rxFrame;
  Pubs	pubs;
  PubIdx	pubIdx;
  Reqs	reqs;
  bool	started = false;
};

template <typename Heap = ZuVoid>
struct State_ : public Heap, public ZmObject, public StateData {
  using StateData::StateData;
};
ZuDerive(StateHeap, (ZmHeap<"Ztc.Agent.State", State_<>>));

} // Agent_

struct Agent::State final : public Agent_::State_<Agent_::StateHeap> {
  using Base = Agent_::State_<Agent_::StateHeap>;
  using Base::Base;
};

namespace Agent_ {

static bool rewriteSeq(uint8_t *data, uint64_t seqNo)
{
  auto message = flatbuffers::GetMutableRoot<fbs::Msg>(data);
  if (!message) return false;
  switch (message->body_type()) {
    case fbs::Body::Telemetry:
      return message->mutable_body_as_Telemetry()->mutate_seqNo(seqNo);
    case fbs::Body::Ack:
      return message->mutable_body_as_Ack()->mutate_seqNo(seqNo);
    case fbs::Body::Error: {
      auto error = message->mutable_body_as_Error();
      return error && error->seqNo() != ZuCmp<uint64_t>::null() &&
        error->mutate_seqNo(seqNo);
    }
    case fbs::Body::EOS:
      return message->mutable_body_as_EOS()->mutate_seqNo(seqNo);
    default:
      return false;
  }
}

static ZmRef<ZiIOBuf> requestFrame(const Request &request)
{
  Zfb::IOBuilder builder{
    frameBuf(ZmRef<ZiIOBuf>{new Frame})};
  auto value = ZfbStruct::save(builder, request);
  builder.Finish(saveMsg(builder, fbs::Body::Request,
    value.Union()));
  return saveHdr(builder);
}

static ZmRef<ZiIOBuf> hubErrorFrame(
    const ZuID &id, uint64_t seqNo, int32_t code, ZuCSpan message)
{
  Zfb::IOBuilder builder{ZmRef<ZiIOBuf>{new Frame}};
  auto error = ZfbStruct::save(builder, Error{
    ErrorMessage{message}, id ? id : ZuID{"ztcagent"}, seqNo, code});
  builder.Finish(saveMsg(builder, fbs::Body::Error,
    error.Union()));
  return builder.buf();
}

static bool writeRequest(
    Agent::State *state, Ring &ring, ZmRef<ZiIOBuf> frame)
{
  if (!frame) return false;
  for (;;) {
    if (void *ptr = ring.push(frame->length)) {
      memcpy(ptr, frame->data(), frame->length);
      ring.push2(ptr, frame->length);
      return true;
    }
    if (ring.writeStatus() < 0 || state->stop.load_()) return false;
    ring.kill();
  }
}

static void transmit(
    Agent::State *state, ZmRef<ZiIOBuf> frame, uint64_t generation)
{
  if (!frame || !generation) return;
  auto link = state->link;
  state->client.rxRun([
      state, link = ZuMv(link), frame = ZuMv(frame), generation]() mutable {
    ZmAssert(state->client.rxInvoked());
    if (state->currentGeneration.load_() != generation || !link)
      return;
    link->txStream([frame = ZuMv(frame)](auto &tx) mutable {
      tx << ZuBSpan{frame->data(), frame->length};
      tx.flush();
    }, Zws::Opcode::Binary);
  });
}

static void inventory(Agent::State *state, Pub_ *pub,
    ZmRef<ZiIOBuf> frame, uint64_t generation)
{
  if (!pub || !frame) return;
  pub->app = frame;
  transmit(state, ZuMv(frame), generation);
}

static bool requestApp(Agent::State *state, Pub_ *pub)
{
  auto seqNo = state->nextAppSeqNo++;
  if (!seqNo || seqNo == ZuCmp<uint64_t>::null()) return false;
  pub->appSeqNo = seqNo;
  Request request{
    .filter = "*",
    .id = pub->id,
    .seqNo = seqNo,
    .group = uint8_t(fbs::Group::App),
    .subscribe = true};
  if (!writeRequest(state, pub->ring, requestFrame(request))) {
    pub->appSeqNo = 0;
    return false;
  }
  return true;
}

static void inventoryAll(Agent::State *state, uint64_t generation)
{
  if (state->routeGeneration != generation) return;
  auto i = state->pubs.iter();
  while (auto pub = i())
    if (pub->val().app)
      transmit(state, pub->val().app, generation);
    else
      (void)requestApp(state, &pub->val());
}

static void shutdown(Agent::State *state, Pub_ *pub,
    uint64_t generation)
{
  if (!pub) return;
  Zfb::IOBuilder builder{ZmRef<ZiIOBuf>{new Frame}};
  auto id = builder.CreateString(pub->id.data(), pub->id.length());
  auto value = fbs::CreateShutdown(builder);
  auto telemetry = saveTelemetry(builder, id, 0,
    fbs::TelemetryBody::Shutdown, value.Union());
  builder.Finish(saveMsg(builder, fbs::Body::Telemetry, telemetry.Union()));
  transmit(state, builder.buf(), generation);
}

static void unlinkLeg(Leg *leg)
{
  auto req = leg->req;
  if (leg->active) {
    req->legs.delNode(static_cast<LegList::Node *>(leg));
  } else {
    auto i = req->pending.iter();
    while (auto node = i()) {
      if (&node->val() == leg) { i.del(); break; }
    }
  }
  leg->pub->legs.delNode(static_cast<LegHash::Node *>(leg));
}

static void delReq(Agent::State *state, Req *req)
{
  while (auto node = req->pending.shift()) {
    auto leg = &node->val();
    leg->active = false;
    unlinkLeg(leg);
  }
  while (auto node = req->legs.shift()) {
    auto leg = &node->val();
    Request request = req->request;
    request.id = leg->pub->id;
    request.seqNo = leg->seqNo;
    request.subscribe = false;
    (void)writeRequest(state, leg->pub->ring, requestFrame(request));
    leg->active = false;
    unlinkLeg(leg);
  }
  state->reqs.delNode(static_cast<Reqs::Node *>(req));
}

static void delPub(Agent::State *state, Pub_ *pub)
{
  while (pub->legs.count_()) {
    auto i = pub->legs.iter();
    auto node = i();
    if (!node) break;
    auto leg = &node->val();
    auto req = leg->req;
    unlinkLeg(leg);
    if (!req->pending.count_() && !req->legs.count_()) delReq(state, req);
  }
  if (auto prior = state->pubIdx.delVal(pub->id)) ZmAssert(prior == pub);
  state->pubs.delNode(static_cast<Pubs::Node *>(pub));
}

static bool addPub(Agent::State *state, ZuID id)
{
  if (!id || state->pubs.findPtr(id)) return true;
  auto pub = new Pubs::Node{ZuMv(id), state->cf};
  if (!pub->val().ready) { delete pub; return false; }
  state->pubs.addNode(pub);
  state->pubIdx.add(pub->val().id, &pub->val());
  return requestApp(state, &pub->val());
}

static bool scanPubs(Agent::State *state)
{
  ZiDir dir;
  if (dir.open(ZiFile::append(ZiFile::tmpDir(), state->env.pidDir)) !=
      Zi::OK) return true;
  Zi::Path name;
  for (;;) {
    int result = dir.read(name);
    if (result == Zi::EndOfFile) break;
    if (result != Zi::OK) return false;
    unsigned length = name.length();
    if (name == "." || name == ".." || length <= 4 ||
        name.rfind(".pid") != int64_t(length - 4)) continue;
    ZuCSpan stem{name.data(), length - 4};
    if (stem.length() > ZuIDSize) continue;
    ZuID id{stem};
    if (id != stem) continue;
    (void)addPub(state, ZuMv(id));
  }
  return true;
}

static void drainReq(Agent::State *, uint64_t, uint64_t);

static bool addLeg(Agent::State *state, Req *req, Pub_ *pub)
{
  uint64_t seqNo = state->nextAppSeqNo++;
  if (!seqNo || seqNo == ZuCmp<uint64_t>::null()) return false;
  auto leg = new LegHash::Node{pub, req, seqNo};
  pub->legs.addNode(leg);
  req->pending.pushNode(leg);
  return true;
}

static void drainReq(Agent::State *state,
    uint64_t hubSeqNo, uint64_t generation)
{
  if (state->routeGeneration != generation) return;
  auto req = state->reqs.findPtr(hubSeqNo);
  if (!req || req->cxnGen != generation) return;
  unsigned n = 0;
  while (n < state->cf.fanoutBatch) {
    auto node = req->pending.shift();
    if (!node) break;
    auto leg = &node->val();
    Request request = req->request;
    request.id = leg->pub->id;
    request.seqNo = leg->seqNo;
    request.subscribe = true;
    if (writeRequest(state, leg->pub->ring, requestFrame(request))) {
      req->legs.pushNode(static_cast<LegList::Node *>(leg));
      leg->active = true;
    } else {
      leg->active = false;
      auto frame = hubErrorFrame(leg->pub->id, req->request.seqNo,
        1, "publisher request failed");
      unlinkLeg(leg);
      transmit(state, ZuMv(frame), generation);
    }
    ++n;
  }
  if (req->pending.count_()) {
    state->mx->run([state, hubSeqNo, generation]() {
      drainReq(state, hubSeqNo, generation);
    }, 1);
  } else if (!req->legs.count_()) {
    delReq(state, req);
  }
}

static void admitRequest(Agent::State *state,
    ZmRef<ZiIOBuf> frame, uint64_t generation)
{
  if (state->routeGeneration != generation || !frame) return;
  auto root = Zfb::GetRoot<fbs::Msg>(frame->data());
  auto value = root && root->body_type() == fbs::Body::Request ?
    root->body_as_Request() : nullptr;
  if (!value) return;
  auto request = ZfbStruct::ctor<Ztc::Request>(value);
  if (!request.subscribe) {
    if (auto req = state->reqs.findPtr(request.seqNo)) delReq(state, req);
    return;
  }
  if (!request.seqNo || request.seqNo == ZuCmp<uint64_t>::null() ||
      state->reqs.findPtr(request.seqNo)) {
    transmit(state, hubErrorFrame(request.id, request.seqNo, 1,
      "duplicate or invalid request"), generation);
    return;
  }
  auto req = new Reqs::Node{ZuMv(request), generation};
  state->reqs.addNode(req);
  if (req->val().request.id) {
    auto pub = state->pubs.findPtr(req->val().request.id);
    if (pub) (void)addLeg(state, &req->val(), &pub->val());
  } else {
    auto i = state->pubs.iter();
    while (auto pub = i()) (void)addLeg(state, &req->val(), &pub->val());
  }
  if (!req->val().pending.count_() && !req->val().legs.count_()) {
    transmit(state, hubErrorFrame(req->val().request.id,
      req->val().request.seqNo, 1, "publisher not running"), generation);
    delReq(state, &req->val());
    return;
  }
  drainReq(state, req->val().request.seqNo, generation);
}

static void routeTelemetry(Agent::State *state,
    ZmRef<ZiIOBuf> frame, uint64_t generation)
{
  if (state->routeGeneration != generation || !frame) return;
  auto message = Zfb::GetRoot<fbs::Msg>(frame->data());
  if (!message) return;
  auto body = message->body();
  if (!body) return;
  ZuCSpan id;
  uint64_t seqNo = ZuCmp<uint64_t>::null();
  bool terminal = false;
  switch (message->body_type()) {
    case fbs::Body::Telemetry: {
      auto value = message->body_as_Telemetry();
      if (!value) return;
      id = Zfb::Load::str(value->id());
      if (value->value_type() == fbs::TelemetryBody::AppTelemetry && id) {
	ZuID pubID{id};
	if (pubID != id) return;
	if (!addPub(state, ZuMv(pubID))) return;
	auto pub = state->pubs.findPtr(id);
	if (!pub) return;
	seqNo = value->seqNo();
	if (!seqNo || seqNo == pub->val().appSeqNo) {
	  if (seqNo) {
	    if (pub->val().app) {
	      pub->val().appSeqNo = 0;
	      return;
	    }
	    if (!rewriteSeq(frame->data(), 0)) return;
	    pub->val().appSeqNo = 0;
	  }
	  inventory(state, &pub->val(), ZuMv(frame), generation);
	  return;
	}
      }
      if (value->value_type() == fbs::TelemetryBody::Shutdown && id &&
          !value->seqNo()) {
        ZuID pubID{id};
        auto pub = pubID == id ? state->pubs.findPtr(pubID) : nullptr;
        if (!pub) return;
        transmit(state, ZuMv(frame), generation);
        delPub(state, &pub->val());
        return;
      }
      seqNo = value->seqNo();
      if (!seqNo) return;
    } break;
    case fbs::Body::Ack: {
      auto value = message->body_as_Ack();
      if (!value) return;
      id = Zfb::Load::str(value->id());
      seqNo = value->seqNo();
	ZuID pubID{id};
	auto pub = pubID == id ? state->pubs.findPtr(pubID) : nullptr;
	if (pub && seqNo == pub->val().appSeqNo) {
	  if (value->status() != fbs::AckStatus::OK)
	    pub->val().appSeqNo = 0;
	  return;
	}
      terminal = value->status() == fbs::AckStatus::Invalid ||
	value->status() == fbs::AckStatus::Failed;
    } break;
    case fbs::Body::EOS: {
      auto value = message->body_as_EOS();
      if (!value) return;
      id = Zfb::Load::str(value->id());
      seqNo = value->seqNo();
	ZuID pubID{id};
	auto pub = pubID == id ? state->pubs.findPtr(pubID) : nullptr;
	if (pub && seqNo && seqNo == pub->val().appSeqNo) {
	  pub->val().appSeqNo = 0;
	  return;
	}
      if (!seqNo) {
	if (!pub) return;
	pub->val().app = nullptr;
	transmit(state, ZuMv(frame), generation);
	delPub(state, &pub->val());
	return;
      }
      terminal = true;
    } break;
    case fbs::Body::Error: {
      auto value = message->body_as_Error();
      if (!value) return;
      id = Zfb::Load::str(value->id());
      seqNo = value->seqNo();
      terminal = true;
    } break;
    default: return;
  }
  ZuID pubID{id};
  if (pubID != id || seqNo == ZuCmp<uint64_t>::null()) return;
  auto pub = state->pubs.findPtr(pubID);
  auto leg = pub ? pub->val().legs.findPtr(seqNo) : nullptr;
  if (!leg || leg->val().req->cxnGen != generation) return;
  auto req = leg->val().req;
  if (!req->request.interval && message->body_type() == fbs::Body::Ack)
    terminal = true;
  if (!rewriteSeq(frame->data(), req->request.seqNo)) return;
  transmit(state, ZuMv(frame), generation);
  if (terminal) {
    unlinkLeg(&leg->val());
    if (!req->pending.count_() && !req->legs.count_()) delReq(state, req);
  }
}

static bool reserveTelemetry(Agent::State *state, unsigned size)
{
  if (!size || uint64_t(size) > state->cf.telBytes) return false;
  for (;;) {
    auto bytes = state->pendingTelBytes.load_();
    if (bytes > state->cf.telBytes - size) return false;
    if (state->pendingTelBytes.cmpXch(bytes + size, bytes) == bytes)
      break;
  }
  auto frames = state->pendingTelFrames++;
  if (frames < state->cf.telFrames) return true;
  --state->pendingTelFrames;
  state->pendingTelBytes -= size;
  return false;
}

static void releaseTelemetry(Agent::State *state, unsigned size)
{
  --state->pendingTelFrames;
  state->pendingTelBytes -= size;
}

static void telemetry(Agent::State *state)
{
  while (!state->stop.load_()) {
    auto ptr = state->telRing.shift();
    if (!ptr) {
      if (state->stop.load_()) break;
      if (state->telRing.readStatus() >= 0) continue;
      break;
    }
    auto hdr = static_cast<const Hdr *>(ptr);
    uint64_t body = uint32_t(hdr->length);
    uint64_t total = uint64_t(sizeof(Hdr)) + body;
    if (total > UINT_MAX) {
      state->stop = 1;
      break;
    }
    unsigned size = unsigned(body);
    unsigned recordSize = unsigned(total);
    if (size <= state->cf.maxFrame && Ztc::msg(hdr)) {
      if (!reserveTelemetry(state, size)) {
        state->stop = 1;
        state->client.rxRun([state]() {
          if (state->link) state->link->close(Zws::CloseCode::Policy);
        });
        state->telRing.shift2(recordSize);
        break;
      }
      ZmRef<ZiIOBuf> frame = new Frame;
      if (frame->alloc(size)) {
        memcpy(frame->data(), hdr->data(), size);
        frame->length = size;
        auto generation = state->currentGeneration.load_();
        state->mx->run([state, frame = ZuMv(frame), generation, size]() mutable {
          Agent_::releaseTelemetry(state, size);
          Agent_::routeTelemetry(state, ZuMv(frame), generation);
        }, 1);
      } else releaseTelemetry(state, size);
    }
    state->telRing.shift2(recordSize);
  }
}

static void pubGC(Agent::State *state)
{
  unsigned n = 0;
  while (n++ < state->cf.pubGCBatch && state->pubs.count_()) {
    auto next = state->pubGCID ?
      state->pubIdx.citer<ZmRBTreeGreater>(state->pubGCID)() :
      state->pubIdx.minimum();
    if (!next) {
      state->pubGCID.null();
      break;
    }
    Pub_ *pub = next->val();
    state->pubGCID = pub->id;
    Zi::Path path{ZiFile::append(ZiFile::tmpDir(), state->env.pidDir)};
    path << '/' << pub->id << ".pid";
    if (ZiStat{path}.exists()) continue;
    shutdown(state, pub, state->routeGeneration);
    delPub(state, pub);
  }
  if (!state->stop.load_()) state->mx->add(&state->pubGCTimer,
    Zm::now(state->cf.pubGCInterval), ZmScheduler::Update,
    [state](auto &&arm) { return arm([state]() { pubGC(state); }); }, 1);
}

static void fence(Agent::State *state, uint64_t generation)
{
  if (generation && state->routeGeneration != generation) return;
  state->routeGeneration = 0;
  while (state->reqs.count_()) {
    auto i = state->reqs.iter();
    auto node = i();
    if (!node) break;
    delReq(state, &node->val());
  }
}

static void requestToken(Agent::State *);

template <typename T>
static bool jsonLoad(Zum::String &json, T &value)
{
  if (!json || json.length() > 64U<<10) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1) return false;
  auto handler = ZfJSON::handler<T>(roots[0]);
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

static bool tokenURL(Agent::State *state, ZuCSpan value)
{
  Zhttp::URL parsed{value};
  if (!parsed.ok()) return false;
  auto url = parsed.url();
  Zhttp::URL issuer{state->env.issuerURL};
  if (url.origin() != issuer.url().origin() || url.hasQuery ||
      url.hasFragment || !url.path) return false;
  Zum::AppEndpointPath path;
  AgentString source{url.path};
  Zum::AppIssuerPath issuerPath;
  AgentString issuerSource{issuer.url().path};
  return ZfURI::loadPath(path, source) &&
    ZfURI::loadPath(issuerPath, issuerSource) &&
    path.oauth2 == "oauth2" && path.appID == issuerPath.appID &&
    path.group == "v1" && path.endpoint == "token";
}

static Zum::String basicAuth(ZuCSpan clientID, ZuBSpan secret)
{
  AgentSecret plain;
  ZfURI::PathQuote::quote(plain, clientID);
  plain << ':';
  ZfURI::PathQuote::quote(plain, secret);
  AgentSecret encoded;
  encoded.length(ZuBase64::enclen(plain.length()));
  encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
  ZuClear(plain);
  Zum::String authorization{"Basic "};
  authorization << encoded;
  ZuClear(encoded);
  return authorization;
}

static void retry(Agent::State *state)
{
  if (!state->started || state->stop.load_()) return;
  auto delay = state->reconnectDelay;
  auto next = unsigned(double(delay) * state->cf.reconnBackoff);
  if (next <= delay || next > state->cf.reconnMax) next = state->cf.reconnMax;
  state->reconnectDelay = next;
  state->mx->add(&state->reconnectTimer, Zm::now(delay),
    ZmScheduler::Update, [state](auto &&arm) {
      return arm([state]() { requestToken(state); });
    }, 1);
}

static void connectToken(Agent::State *state)
{
  if (state->stop.load_()) return;
  if (!state->token || state->tokenExpires <=
      Zm::now().sec() + int64_t(state->cf.upgradeTimeout) + 1) {
    if (state->token.mutable_()) ZuClear(state->token);
    state->token.null();
    retry(state);
    return;
  }
  AgentSecret authorization{"Bearer "};
  authorization << state->token;
  state->link = new Agent::Link{&state->client, state->uri,
    ZuBSpan{authorization}};
  ZuClear(authorization);
  ZuClear(state->token);
  state->token.null();
  state->link->connect();
}

static void tokenResult(Agent::State *state, uint64_t generation,
    Zum::ServiceHTTPResponse response)
{
  if (state->stop.load_() || generation != state->authGeneration) {
    if (response.body.mutable_()) ZuClear(response.body);
    return;
  }
  state->tokenPending = false;
  Token wire;
  bool ok = response.status == 200 && jsonLoad(response.body, wire) &&
    wire.accessToken && wire.tokenType == "Bearer" && wire.expiresIn > 1 &&
    wire.expiresIn <= uint64_t(INT64_MAX - Zm::now().sec());
  if (ok) {
    state->token.length(wire.accessToken.length());
    memcpy(state->token.data(), wire.accessToken.data(), wire.accessToken.length());
    state->tokenExpires = Zm::now().sec() + int64_t(wire.expiresIn);
  }
  if (wire.accessToken.mutable_()) ZuClear(wire.accessToken);
  if (response.body.mutable_()) ZuClear(response.body);
  if (!ok) { retry(state); return; }
  if (state->env.provision) {
    state->awaitingProvision = true;
    state->env.onProvision();
    return;
  }
  connectToken(state);
}

static void metadataResult(Agent::State *state, uint64_t generation,
    Zum::ServiceHTTPResponse response)
{
  if (state->stop.load_() || generation != state->authGeneration) return;
  Discovery wire;
  bool ok = response.status == 200 && jsonLoad(response.body, wire) &&
    wire.issuerURL == state->env.issuerURL &&
    tokenURL(state, wire.tokenEndpoint);
  if (response.body.mutable_()) ZuClear(response.body);
  if (!ok) {
    state->tokenPending = false;
    retry(state);
    return;
  }
  state->tokenURL = ZuMv(wire.tokenEndpoint);
  state->httpFn(Zum::ServiceHTTPRequest{
    .method = Zum::ServiceMethod::POST,
    .url = state->tokenURL,
    .authorization = basicAuth(state->env.clientID,
      ZuBSpan{state->env.clientSecret}),
    .contentType = "application/x-www-form-urlencoded",
    .body = "grant_type=client_credentials&scope=Agent"},
    [state, generation](Zum::ServiceHTTPResponse response) mutable {
      state->mx->run([state, generation, response = ZuMv(response)]() mutable {
        tokenResult(state, generation, ZuMv(response));
      }, 1);
    });
}

static void requestToken(Agent::State *state)
{
  if (state->stop.load_() || !state->started || state->tokenPending ||
      state->awaitingProvision || state->link) return;
  state->tokenPending = true;
  auto generation = ++state->authGeneration;
  state->httpFn(Zum::ServiceHTTPRequest{.url = state->metadataURL},
    [state, generation](Zum::ServiceHTTPResponse response) mutable {
      state->mx->run([state, generation, response = ZuMv(response)]() mutable {
        metadataResult(state, generation, ZuMv(response));
      }, 1);
    });
}

} // Agent_

Agent::~Agent() { final(); }

bool Agent::init(const AgentCf &cf, AgentEnv env)
{
  if (m_state || !env.issuerURL || !env.clientID || !env.deviceID ||
      !env.wssURL || !env.clientSecret ||
      (env.provision && !env.onProvision) || !cf.reconnMin ||
      cf.reconnMin > cf.reconnMax || !cf.fanoutBatch ||
      cf.telBytes < cf.maxFrame || cf.reqBytes < cf.maxFrame ||
      !cf.idleTimeout || !cf.pingInterval || !cf.closeTimeout)
    return false;
  Zhttp::URL issuerURL{env.issuerURL};
  auto issuerView = issuerURL.url();
  bool loopback = cf.loopbackTest && issuerView.scheme == Zhttp::Scheme::http &&
    (issuerView.host == "localhost" || issuerView.host == "127.0.0.1" ||
      issuerView.host == "::1");
  if (!issuerURL.ok() || (!loopback && issuerView.scheme != Zhttp::Scheme::https) ||
      !issuerView.host || issuerView.hasQuery || issuerView.hasFragment ||
      !issuerView.path) return false;
  Zum::AppIssuerPath issuerPath;
  AgentString issuerSource{issuerView.path};
  if (!ZfURI::loadPath(issuerPath, issuerSource) ||
      issuerPath.oauth2 != "oauth2" || !issuerPath.appID) return false;
  auto state = new State{this, cf, ZuMv(env)};
  state->metadataURL << issuerView.origin();
  ZfURI::savePath(state->metadataURL, Zum::AppOAuthMetadataPath{
    .wellKnown = ".well-known",
    .endpoint = "oauth-authorization-server",
    .oauth2 = "oauth2", .appID = issuerPath.appID});
  if (!Zws::URI::parse(state->uri, state->env.wssURL).ok() ||
      !state->uri.secure() || !state->uri.host || !state->uri.port ||
      !state->uri.target) {
    delete state;
    return false;
  }
  try {
    state->mx = new ZiMultiplex{
      ZiMxParams{}.scheduler([](auto &s) {
        s.nThreads(2)
          .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
          .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); });
      }).rxThread(1).txThread(2)};
  } catch (...) {
    delete state;
    return false;
  }
  m_state = state;
  return true;
}

bool Agent::start()
{
  if (!m_state || m_state->started || !m_state->mx->start()) return false;
  Zws::Config ws;
  ws.maxMessage = m_state->cf.maxFrame;
  ws.maxQueuedInput = m_state->cf.reqBytes;
  ws.handshakeTimeout = m_state->cf.upgradeTimeout;
  ws.closeTimeout = m_state->cf.closeTimeout;
  ws.pingInterval = m_state->cf.pingInterval;
  ws.pongTimeout = m_state->cf.idleTimeout;
  Zhttp::H2Config tls;
  if (m_state->env.caPath) tls.caPath(m_state->env.caPath);
  if (!m_state->client.init(
      Zhttp::HubConfig{m_state->mx, "1", "2"}, tls, ws) ||
      !m_state->client.start()) {
    m_state->mx->stop();
    return false;
  }
  if (!m_state->http.init(m_state->mx, m_state->env.issuerURL,
        m_state->env.issuerURL, m_state->env.issuerURL,
        m_state->env.caPath)) {
    m_state->client.final();
    m_state->mx->stop();
    return false;
  }
  m_state->httpFn = m_state->http.fn();
  m_state->telRing.init(ZiRingParams{m_state->env.ring, m_state->cf.telSize}
    .ll(m_state->cf.telLL).spin(m_state->cf.telSpin)
    .timeout(m_state->cf.telTimeout));
  if (m_state->telRing.open(Ring::Read) != Zu::OK ||
      m_state->telRing.attach() != Zu::OK) {
    m_state->telRing.close();
    m_state->http.final();
    m_state->client.final();
    m_state->mx->stop();
    return false;
  }
  m_state->stop = 0;
  m_state->reconnectDelay = m_state->cf.reconnMin;
  if (m_state->telThread.run([state = m_state]() {
      Agent_::telemetry(state);
    }, ZmThreadParams{}.name("ztcTel")) < 0) {
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->http.final();
    m_state->client.final();
    m_state->mx->stop();
    return false;
  }
  ZmSemaphore scanDone;
  bool scanOK = false;
  m_state->mx->run([state = m_state, &scanDone, &scanOK]() {
    scanOK = Agent_::scanPubs(state);
    scanDone.post();
  }, 1);
  scanDone.wait();
  if (!scanOK) {
    m_state->stop = 1;
    m_state->telThread.join();
    m_state->telRing.detach();
    m_state->telRing.close();
    m_state->http.final();
    m_state->client.final();
    m_state->mx->stop();
    return false;
  }
  m_state->mx->run([state = m_state]() { Agent_::pubGC(state); }, 1);
  m_state->started = true;
  m_state->mx->run([state = m_state]() {
    Agent_::requestToken(state);
  }, 1);
  return true;
}

bool Agent::stop()
{
  if (!m_state || !m_state->started) return true;
  m_state->stop = 1;
  m_state->mx->del(&m_state->reconnectTimer);
  m_state->mx->del(&m_state->pubGCTimer);
  m_state->client.rxRun([state = m_state]() {
    if (state->link) state->link->close();
  });
  if (m_state->telThread.tid()) m_state->telThread.join();
  ZmSemaphore routeDone;
  m_state->mx->run([state = m_state, &routeDone]() {
    Agent_::fence(state, 0);
    while (state->pubs.count_()) {
      auto i = state->pubs.iter();
      auto node = i();
      if (!node) break;
      Agent_::delPub(state, &node->val());
    }
    routeDone.post();
  }, 1);
  routeDone.wait();
  if (m_state->telRing.rdrID() >= 0) m_state->telRing.detach();
  m_state->telRing.close();
  m_state->http.final();
  m_state->httpFn = {};
  bool ok = m_state->client.stop();
  m_state->link = nullptr;
  m_state->client.final();
  m_state->mx->stop();
  m_state->started = false;
  if (m_state->token.mutable_()) ZuClear(m_state->token);
  m_state->token.null();
  return ok;
}

ZuBSpan Agent::clientSecret() const
{
  return m_state && m_state->awaitingProvision ?
    ZuBSpan{m_state->env.clientSecret} : ZuBSpan{};
}

void Agent::provisioned(bool ok)
{
  if (!m_state) return;
  m_state->mx->run([state = m_state, ok]() {
    if (state->stop.load_() || !state->awaitingProvision) return;
    state->awaitingProvision = false;
    if (!ok) return;
    state->env.provision = false;
    Agent_::connectToken(state);
  }, 1);
}

void Agent::final()
{
  if (!m_state) return;
  stop();
  delete m_state->mx;
  delete m_state;
  m_state = nullptr;
}

template <typename Link>
void Agent::connected(Link &link, const Zhttp::ConnectedInfo &)
{
  if (&link != m_state->link.ptr() || m_state->stop.load_()) return;
  uint64_t generation = m_state->nextGeneration++;
  if (!generation) generation = m_state->nextGeneration++;
  m_state->currentGeneration = generation;
  m_state->mx->run([state = m_state, generation]() {
    Agent_::fence(state, 0);
    state->routeGeneration = generation;
    Agent_::inventoryAll(state, generation);
  }, 1);
  m_state->reconnectDelay = m_state->cf.reconnMin;
  m_state->mx->del(&m_state->reconnectTimer);
}

template <typename Link>
void Agent::disconnected(Link &link, bool)
{
  if (&link != m_state->link.ptr()) return;
  auto generation = m_state->currentGeneration.load_();
  if (m_state->currentGeneration.load_() == generation)
    m_state->currentGeneration = 0;
  m_state->mx->run([state = m_state, generation]() {
    Agent_::fence(state, generation);
  }, 1);
  m_state->link = nullptr;
  Agent_::retry(m_state);
}

template <typename Link>
void Agent::connectFailed(Link &link, bool)
{
  if (&link != m_state->link.ptr()) return;
  m_state->link = nullptr;
  Agent_::retry(m_state);
}

template <typename Link>
int Agent::messageStart(Link &link, Zws::Opcode::T opcode)
{
  if (&link != m_state->link.ptr()) return -1;
  if (opcode != Zws::Opcode::Binary) return -1;
  m_state->rxFrame.length(0);
  return 1;
}

template <typename Link_, typename Rx>
int Agent::process(Link_ &link, Rx &rx)
{
  if (&link != m_state->link.ptr()) return -1;
  return Zhttp::bodyEach(rx, [this](ZuSpan<uint8_t> span) {
    if (span.length() > m_state->cf.maxFrame -
        m_state->rxFrame.length())
      return false;
    m_state->rxFrame << span;
    return true;
  }) ? 1 : -1;
}

template <typename Link>
int Agent::messageEnd(Link &link)
{
  if (&link != m_state->link.ptr()) return -1;
  auto msg = Ztc::msg(ZuBSpan{
    m_state->rxFrame.data(), m_state->rxFrame.length()});
  if (!msg || msg->body_type() != fbs::Body::Request ||
      !m_state->currentGeneration.load_()) {
    link.close(Zws::CloseCode::Protocol);
    return -1;
  }
  ZmRef<ZiIOBuf> frame = new Agent_::Frame;
  if (!frame->alloc(m_state->rxFrame.length())) {
    link.close(Zws::CloseCode::TooLarge);
    return -1;
  }
  memcpy(frame->data(), m_state->rxFrame.data(), m_state->rxFrame.length());
  frame->length = m_state->rxFrame.length();
  auto generation = m_state->currentGeneration.load_();
  m_state->mx->run([state = m_state, frame = ZuMv(frame), generation]() mutable {
    Agent_::admitRequest(state, ZuMv(frame), generation);
  }, 1);
  return 1;
}

} // Ztc
