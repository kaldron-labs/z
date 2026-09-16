//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ztchub daemon entry point

#include <stdlib.h>

#include <iostream>

#include <zlib/ZuTuple.hh>
#include <zlib/Zfb.hh>
#include <zlib/ZtcFB.hh>
#include <zlib/ZtcMsg.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTrap.hh>

#include <zlib/ZfCLI.hh>
#include <zlib/ZvCf.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZrestServer.hh>
#include <zlib/Zws.hh>

#include <zlib/ztchub_daemon.hh>
#include <zlib/ZtcDB.hh>

#include "../../zum/example/pinghttp.hh"

namespace ZtcHub_ {

using Ztc::Protocol;

using HubBuf = ZiIOBufAlloc<1024, 1U<<30, "Ztc.Hub.Frame">;

namespace SSF_ {

struct RawData : public ZmObject {
  Zum::String data;
  RawData &operator =(ZuSpan<uint8_t> value) { data = value; return *this; }
};

struct Reply : public ZmObject { };

struct SessionReply : public Reply {
  ZtString<> cookie;
};

template <unsigned Status_>
struct Response : public Zrest::ResBuilder<Response<Status_>, Reply> {
  using Base = Zrest::ResBuilder<Response<Status_>, Reply>;
  enum { Status = Status_, Body = Zrest::BodyPolicy::None };
  using Headers = ZhttpHeaders("cache-control", "content-length");
};
template <unsigned Status_>
struct SessionResponse : public Zrest::ResBuilder<
    SessionResponse<Status_>, SessionReply> {
  using Base = Zrest::ResBuilder<SessionResponse<Status_>, SessionReply>;
  using Base::header;
  enum { Status = Status_, Body = Zrest::BodyPolicy::None };
  using Headers = ZhttpHeaders("cache-control", "content-length",
    "set-cookie");

  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "cache-control") l("no-store");
    else if constexpr (Key{}() == "set-cookie") {
      if (this->object->cookie) l(this->object->cookie);
    } else Base::template header<Key>(ZuFwd<L>(l));
  }
};
using Responses = ZuTypeList<Response<202>, Response<400>, Response<401>,
  Response<503>>;
using SessionResponses = ZuTypeList<SessionResponse<201>, Response<400>,
  Response<401>, Response<503>>;

struct App;
static ZuCSpan browserPath(App *);
static ZuCSpan ssfPath(App *);
template <typename Impl>
struct Request : public Zrest::ReqParser<Impl, RawData> {
  using Base = Zrest::ReqParser<Impl, RawData>;
  enum { Body = Zrest::BodyPolicy::Raw };
  using Base::header;
  static constexpr uint64_t BodyLimit = 64U<<10;
  using Headers = ZhttpHeaders("authorization", "content-type");
  using Responses = SSF_::Responses;
  App *app = nullptr;
  ZtString<> authorization;
  ZtString<> contentType;
  void init() { Base::init(); authorization.null(); contentType.null(); }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
    else if constexpr (Key{}() == "content-type") contentType = value;
  }
  template <typename Link> void complete(Link *, bool);
};
struct SSF : public Request<SSF> {
  enum { Exact = 1, Method = Zhttp::Method::POST };
  using Path = ZuStringT<"/ssf">;
};
struct Session : public Request<Session> {
  enum { Exact = 1, Method = Zhttp::Method::POST, Body = Zrest::BodyPolicy::None };
  using Path = ZuStringT<"/session">;
  using Headers = ZhttpHeaders("authorization", "cookie", "origin");
  using Responses = SSF_::SessionResponses;
  using Request<Session>::header;
  ZtString<> cookie;
  ZtString<> origin;

  void init() {
    Request<Session>::init();
    cookie.null();
    origin.null();
  }
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "authorization") authorization = value;
    else if constexpr (Key{}() == "cookie") cookie = value;
    else if constexpr (Key{}() == "origin") origin = value;
  }
  template <typename Link> void complete(Link *, bool);
};
using Requests = ZuTypeList<SSF, Session>;
ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)

struct Parser : public Zrest::MReqParser<Catalog> {
  using Base = Zrest::MReqParser<Catalog>;
  void init(App &app_) { app = &app_; }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    auto saved = target.path;
    static uint8_t sessionPath[] = "/session";
    static uint8_t ssfPathValue[] = "/ssf";
    if (target.path == browserPath(app))
      target.path = ZuSpan<uint8_t>{sessionPath, sizeof(sessionPath) - 1};
    else if (target.path == ssfPath(app))
      target.path = ZuSpan<uint8_t>{ssfPathValue,
        sizeof(ssfPathValue) - 1};
    bool ok = Base::operation(method, target);
    target.path = saved;
    if (!ok) return false;
    u.dispatch([this](auto, auto &request) { request.app = app; });
    return true;
  }
  App *app = nullptr;
};
struct Builder : public ZmObject, public Zrest::MResBuilder<Catalog> { };
ZmListDerive(BuilderQ, Builder,
  ZmListNode<Builder, ZmListHeapID<"Ztc.Hub.SSFReply">>);

struct App {
  using Parser = SSF_::Parser;
  using ResBuilderQ = SSF_::BuilderQ;
  Ztc::Hubd *hub = nullptr;
  Ztc::HubString browserPath;
  Ztc::HubString ssfPath;
  Ztc::HubStrings origins;

  static bool bearer(ZuBSpan authorization) {
    return authorization.length() > 7 &&
      authorization.prefix("Bearer ") == 7;
  }

  void listening(int8_t, uint16_t) { }
  void listenFailed(int8_t, bool) { }
  void connected(int8_t) { }
  void disconnected(int8_t) { }

  template <typename Req, typename Link>
  static void reply(ZmRef<Link> link, unsigned status) {
    ZmRef<Reply> object = new Reply{};
    ZmRef<BuilderQ::Node> response = new BuilderQ::Node{};
    switch (status) {
#define ZTC_SSF_RESPONSE(N) case N: response->template init<Response<N>, Req>(object.ptr()); break
      ZTC_SSF_RESPONSE(202);
      ZTC_SSF_RESPONSE(400);
      ZTC_SSF_RESPONSE(401);
      default: response->template init<Response<503>, Req>(object.ptr()); break;
#undef ZTC_SSF_RESPONSE
    }
    link->send(ZuMv(response));
  }

  template <typename Req, typename Link>
  static void sessionReply(ZmRef<Link> link, unsigned status,
      ZtString<> cookie = {}) {
    auto object = ZmRef<SessionReply>{new SessionReply};
    object->cookie = ZuMv(cookie);
    ZmRef<BuilderQ::Node> response = new BuilderQ::Node{};
    switch (status) {
#define ZTC_SESSION_RESPONSE(N) case N: response->template init<SessionResponse<N>, Req>(object.ptr()); break
      ZTC_SESSION_RESPONSE(201);
      case 400: response->template init<Response<400>, Req>(object.ptr()); break;
      case 401: response->template init<Response<401>, Req>(object.ptr()); break;
      default: response->template init<Response<503>, Req>(object.ptr()); break;
#undef ZTC_SESSION_RESPONSE
    }
    link->send(ZuMv(response));
  }

  template <typename Req, typename Link>
  void request(Link *link, const Req &request, bool ok) {
    if (!ok) return reply<Req>(ZmRef<Link>{link}, 400);
    hub->receiveSET(Zum::ServiceSETRequest{
      .authorization = request.authorization,
      .contentType = request.contentType,
      .body = ZuMv(request.object->data)},
      [hold = ZmRef<Link>{link}](int error) mutable {
        unsigned status = error == Zum::ServiceError::OK ? 202 :
          error == Zum::ServiceError::Unauthorized ? 401 :
          error == Zum::ServiceError::Unavailable ? 503 : 400;
        reply<Req>(ZuMv(hold), status);
      });
  }

  template <typename Link>
  void session(Link *link, const SSF_::Session &request, bool ok) {
    if (!ok) return sessionReply<SSF_::Session>(ZmRef<Link>{link}, 400);
    if (!bearer(request.authorization))
      return sessionReply<SSF_::Session>(ZmRef<Link>{link}, 401);
    bool originOK = false;
    for (auto &origin: origins)
      if (request.origin == origin) { originOK = true; break; }
    if (!originOK)
      return sessionReply<SSF_::Session>(ZmRef<Link>{link}, 401);
    ZtString<> token;
    token << ZuCSpan{request.authorization}.offset(7);
    auto hold = ZmRef<Link>{link};
    hub->verify(ZuMv(token), [this, hold = ZuMv(hold)](
        int error, Zum::ServicePrincipal principal) mutable {
      auto server = hold->app();
      server->rxRun([this, hold = ZuMv(hold), error,
          principal = ZuMv(principal)]() mutable {
        if (error != Zum::ServiceError::OK) {
          sessionReply<SSF_::Session>(ZuMv(hold), 401);
          return;
        }
        Ztc::HubString cookie;
        if (!hub->createBrowserSession(principal, cookie)) {
          sessionReply<SSF_::Session>(ZuMv(hold), 503);
          return;
        }
        ZtString<> header;
        header << "__Host-ztc_session=" << cookie <<
          "; Path=/; Secure; HttpOnly; SameSite=Strict";
        sessionReply<SSF_::Session>(ZuMv(hold), 201, ZuMv(header));
      });
    });
  }
};

static ZuCSpan browserPath(App *app) { return app->browserPath; }
static ZuCSpan ssfPath(App *app) { return app->ssfPath; }

template <typename Impl>
template <typename Link>
void Request<Impl>::complete(Link *link, bool ok) {
  app->request(link, static_cast<const Impl &>(*this), ok);
}

template <typename Link>
void Session::complete(Link *link, bool ok) {
  app->session(link, *this, ok);
}

} // SSF_

static flatbuffers::Offset<Ztc::fbs::Telemetry> copyTelemetry(
    Zfb::IOBuilder &builder, const Ztc::fbs::Telemetry *source)
{
  if (!source || !source->id()) return {};
  auto id = builder.CreateString(source->id()->data(), source->id()->size());
#define ZTC_COPY_TEL(Name, Type) \
  case Ztc::fbs::TelemetryBody::Name: { \
    auto copy = ZfbStruct::save<ZuFacet::Core, ZfFieldFilter::All>( \
      builder, *source->value_as_##Name()); \
    return Ztc::saveTelemetry(builder, id, source->seqNo(), \
      Ztc::fbs::TelemetryBody::Name, copy.Union()); \
  }
  switch (source->value_type()) {
    ZTC_COPY_TEL(HeapTelemetry, HeapTelemetry)
    ZTC_COPY_TEL(HashTelemetry, HashTelemetry)
    ZTC_COPY_TEL(ThreadTelemetry, ThreadTelemetry)
    ZTC_COPY_TEL(CxnTelemetry, CxnTelemetry)
    ZTC_COPY_TEL(MxTelemetry, MxTelemetry)
    ZTC_COPY_TEL(QueueTelemetry, QueueTelemetry)
    ZTC_COPY_TEL(HubTelemetry, HubTelemetry)
    ZTC_COPY_TEL(LinkTelemetry, LinkTelemetry)
    ZTC_COPY_TEL(PoolTelemetry, PoolTelemetry)
    ZTC_COPY_TEL(AppTelemetry, AppTelemetry)
    ZTC_COPY_TEL(AlertTelemetry, AlertTelemetry)
    ZTC_COPY_TEL(DBTelemetry, DBTelemetry)
    ZTC_COPY_TEL(DBHostTelemetry, DBHostTelemetry)
    ZTC_COPY_TEL(DBTableTelemetry, DBTableTelemetry)
    default: return {};
  }
#undef ZTC_COPY_TEL
}

static Ztc::HubFrame agentFrame(
    const Ztc::RouteInfo &route, const Ztc::fbs::Msg *message)
{
  if (!message) return {};
  Ztc::HubFrame frame = new HubBuf;
  frame->skip = Zfb::IOBuilder::Align;
  Zfb::IOBuilder builder{ZuMv(frame)};
  auto device = builder.CreateString(route.deviceID.data(),
    route.deviceID.length());
  flatbuffers::Offset<void> body;
  Ztc::fbs::Body type;
  switch (message->body_type()) {
    case Ztc::fbs::Body::Ack:
      body = ZfbStruct::save(builder, *message->body_as_Ack()).Union();
      break;
    case Ztc::fbs::Body::Telemetry:
      body = copyTelemetry(builder, message->body_as_Telemetry()).Union();
      break;
    case Ztc::fbs::Body::EOS:
      body = ZfbStruct::save(builder, *message->body_as_EOS()).Union();
      break;
    case Ztc::fbs::Body::Error:
      body = ZfbStruct::save(builder, *message->body_as_Error()).Union();
      break;
    default: return {};
  }
  if (body.IsNull()) return {};
  type = message->body_type();
  builder.Finish(Ztc::saveMsg(builder, type, body,
    route.subID, device, route.agentGeneration));
  return builder.buf();
}

static Ztc::HubFrame errorFrame(uint64_t subID, ZuCSpan deviceID,
    uint64_t agentGeneration, Ztc::HubError::T code, ZuCSpan message)
{
  Ztc::HubFrame frame = new HubBuf;
  frame->skip = Zfb::IOBuilder::Align;
  Zfb::IOBuilder builder{ZuMv(frame)};
  auto device = builder.CreateString(deviceID.data(), deviceID.length());
  auto error = ZfbStruct::save(builder, Ztc::Error{
    Ztc::ErrorMessage{message}, ZuID{deviceID}, 0, int(code)});
  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::Error,
    error.Union(), subID, device, agentGeneration));
  return builder.buf();
}

static Ztc::HubFrame eosFrame(const Ztc::RouteInfo &route)
{
  Ztc::HubFrame frame = new HubBuf;
  frame->skip = Zfb::IOBuilder::Align;
  Zfb::IOBuilder builder{ZuMv(frame)};
  auto device = builder.CreateString(route.deviceID.data(),
    route.deviceID.length());
  auto eos = ZfbStruct::save(builder,
    Ztc::EOS{ZuID{route.deviceID}, route.requestSeqNo});
  builder.Finish(Ztc::saveMsg(builder, Ztc::fbs::Body::EOS,
    eos.Union(), route.subID, device, route.agentGeneration));
  return builder.buf();
}

static Ztc::HubFrame unsubscribeFrame(const Ztc::RouteInfo &route)
{
  Ztc::HubFrame frame = new HubBuf;
  frame->skip = Zfb::IOBuilder::Align;
  Zfb::IOBuilder builder{ZuMv(frame)};
  auto request = ZfbStruct::save(builder,
    Ztc::Request{.seqNo = route.requestSeqNo});
  builder.Finish(Ztc::saveMsg(builder,
    Ztc::fbs::Body::Request, request.Union()));
  return builder.buf();
}

struct App {
  template <typename Heap>
  struct Egress_ : public Heap, public ZmObject {
    Egress_(uint64_t id_) : id{id_} { }

    using FrameQ = ZmQueue<ZuTuple<uint64_t, Ztc::HubFrame>,
      ZmQueueHeapID<"Ztc.Hub.Egress">>;
    uint64_t id;
    uint64_t next = 1;
    FrameQ control;
    FrameQ telemetry;
    uint64_t controlBytes = 0;
    uint64_t telemetryBytes = 0;
    bool queued = false;
  };
  using EgressHeap = ZmHeap<"Ztc.Hub.Egress", Egress_<ZuVoid>>;
  struct Egress final : public Egress_<EgressHeap> {
    using Base = Egress_<EgressHeap>;
    using Base::Base;
  };
  using Egresses = ZmHashKV<uint64_t, ZmRef<Egress>,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Ztc.Hub.EgressIdx">>>;

  struct LinkState {
    Ztc::HubFrame frame;
    Egresses egresses;
    ZmQueue<ZmRef<Egress>, ZmQueueHeapID<"Ztc.Hub.Ready">> ready;
    ZmScheduler::Timer expiry;
    bool sending = false;
    Zws::Opcode::T opcode = Zws::Opcode::Binary;
    bool agent = false;
    bool authenticated = false;
    bool pending = false;
    bool closed = false;
    uint64_t sessionID = 0;
    uint64_t generation = 0;
    Ztc::HubString deviceID;
  };

  Ztc::Hubd *hub;
  Ztc::ListenerCf cf;
  bool *failed;
  ZmSemaphore *done = nullptr;

  static uint64_t nextSessionID() {
    static ZmAtomic<uint64_t> next = 1;
    return next.xchAdd(1);
  }

  // Routing queues are Rx-owned. Only one bounded frame per connection is
  // handed to Tx; its transport fence resumes the Rx pump.
  template <typename Link>
  static void pump(ZmRef<Link> hold) {
    auto &state = hold->state();
    if (state.closed || state.sending || !state.ready.length()) return;
    auto egress = state.ready.shift();
    egress->queued = false;
    if (!egress->control.length() && !egress->telemetry.length()) {
      state.egresses.del(egress->id);
      pump(ZuMv(hold));
      return;
    }
    bool control = egress->control.length() &&
      (!egress->telemetry.length() || egress->control.head().template p<0>() <
        egress->telemetry.head().template p<0>());
    auto &queue = control ? egress->control : egress->telemetry;
    auto packet = queue.shift();
    auto frame = ZuMv(packet.template p<1>());
    if (control) egress->controlBytes -= frame->length;
    else egress->telemetryBytes -= frame->length;
    if (egress->control.length() || egress->telemetry.length()) {
      egress->queued = true;
      state.ready.push(egress);
    } else state.egresses.del(egress->id);
    state.sending = true;
    auto server = hold->app();
    server->txRun([hold = ZuMv(hold), frame = ZuMv(frame)]() mutable {
      hold->txStream_([frame = ZuMv(frame)](auto &tx) mutable {
        tx << ZuBSpan{frame->data(), frame->length};
        tx.flush();
      }, Zws::Opcode::Binary);
      auto complete = [hold](bool ok) mutable {
        auto server = hold->app();
        server->rxRun([hold = ZuMv(hold), ok]() mutable {
          auto &state = hold->state();
          state.sending = false;
          if (!ok) {
            if (!state.closed)
              ZiLOG(Warning, "Ztc.Hub", "WSS transmit completion failed");
            state.closed = true;
            hold->close(Zws::CloseCode::Policy);
            return;
          }
          pump(ZuMv(hold));
        });
      };
      if (!hold->txFence(Zhttp::Transport_::TxCompleteFn{complete}))
        complete(false);
    });
  }

  template <typename Link>
  static void enqueue(ZmRef<Link> hold, Ztc::HubFrame frame) {
    if (!frame) return;
    // All hub routes and listeners share the multiplex Rx owner.
    auto &state = hold->state();
    if (state.closed) return;
    auto msg = Ztc::fbs::GetMsg(frame->data());
    bool telemetry = msg->body_type() == Ztc::fbs::Body::Telemetry;
    bool terminal = msg->body_type() == Ztc::fbs::Body::EOS ||
      msg->body_type() == Ztc::fbs::Body::Error;
    auto id = msg->subId();
    auto &cf = hold->app()->app()->hub->config();
    auto egress = state.egresses.findVal(id);
    if (!egress) {
      if (state.egresses.count_() >= cf.subscriptionsPerFrontEnd + 1) {
        state.closed = true;
        hold->close(Zws::CloseCode::Policy);
        return;
      }
      egress = new Egress{id};
      state.egresses.add(id, egress);
    }
    auto &queue = telemetry ? egress->telemetry : egress->control;
    auto &bytes = telemetry ? egress->telemetryBytes : egress->controlBytes;
    auto maxFrames = telemetry ? cf.telemetryFrames : cf.controlFrames;
    auto maxBytes = telemetry ? cf.telemetryBytes : cf.controlBytes;
    // Reserve one maximum-sized control frame for terminal delivery.
    if (!telemetry && !terminal) { --maxFrames; maxBytes -= cf.maxFrame; }
    if (queue.length() >= maxFrames || frame->length > maxBytes ||
        bytes > maxBytes - frame->length) {
      if (telemetry && id) {
        queue.clean();
        bytes = 0;
        auto app = hold->app()->app();
        app->hub->removeSubscription(state.sessionID, id,
          [hold](Ztc::RouteInfo route) mutable {
            auto app = hold->app()->app();
            app->hub->sendAgent(route.agentSessionID,
              route.agentGeneration, unsubscribeFrame(route));
            enqueue(hold, errorFrame(route.subID, route.deviceID,
              route.agentGeneration, Ztc::HubError::Overflow,
              "telemetry queue overflow"));
          });
        return;
      }
      state.closed = true;
      hold->close(Zws::CloseCode::Policy);
      return;
    }
    bytes += frame->length;
    queue.push(ZuTuple{egress->next++, ZuMv(frame)});
    if (!egress->queued) {
      egress->queued = true;
      state.ready.push(egress);
    }
    pump(ZuMv(hold));
  }

  template <typename Link>
  static Ztc::HubSendFn sender(ZmRef<Link> hold) {
    return Ztc::HubSendFn{[hold = ZuMv(hold)](Ztc::HubFrame frame) mutable {
      enqueue(ZmRef<Link>{hold}, ZuMv(frame));
    }};
  }

  static bool bearer(ZuBSpan authorization) {
    return authorization.length() > 7 && authorization.prefix("Bearer ") == 7;
  }

  void listening(const ZiListenInfo &info) {
    ZiLOG(Info, "Ztc.Hub", ([ip = info.ip, port = info.port](auto &s) {
      s << "listening on (" << ip << ':' << port << ')';
    }));
  }
  void listening() { ZiLOG(Info, "Ztc.Hub", "listening"); }
  void listenFailed(bool) {
    ZiLOG(Error, "Ztc.Hub", "listener failed");
    *failed = true;
    if (done) done->post();
  }

  static bool cookieValue(ZuBSpan cookies, ZtString<> &value) {
    static constexpr ZuCSpan name{"__Host-ztc_session="};
    while (cookies) {
      while (cookies && cookies[0] == ' ') cookies.offset(1);
      int end = cookies.find([](char c) { return c == ';'; });
      auto item = cookies;
      if (end >= 0) item.trunc(unsigned(end));
      if (item.length() > name.length() &&
          item.prefix(name) == int(name.length())) {
        value = item.offset(name.length());
        return bool(value);
      }
      if (end < 0) break;
      cookies.offset(unsigned(end) + 1);
    }
    return false;
  }

  template <typename Link>
  bool accept(Link &link, ZuBSpan, ZuBSpan target, ZuBSpan offered,
      Zws::HandshakeString &selected) {
    if (target != cf.path) return false;
    if (!Zws::subprotocol(offered, Protocol)) return false;
    selected = Protocol;
    if (!bearer(link.authorization()) && !link.cookie()) return false;
    if (!bearer(link.authorization()) && !link.origin()) return false;
    if (link.origin()) {
      for (auto &origin: cf.origins)
        if (link.origin() == origin) return true;
      return false;
    }
    return true;
  }

  template <typename Link>
  int messageStart(Link &link, Zws::Opcode::T opcode) {
    if (opcode != Zws::Opcode::Binary) {
      link.close(Zws::CloseCode::Unsupported);
      return -1;
    }
    auto &state = link.state();
    if (!state.authenticated && state.pending) {
      link.close(Zws::CloseCode::Policy);
      state.closed = true;
      return -1;
    }
    if (!state.frame) state.frame = new HubBuf;
    state.frame->length = 0;
    state.opcode = opcode;
    return 1;
  }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    auto &state = link.state();
    return Zhttp::bodyEach(rx, [this, &state](ZuSpan<uint8_t> span) {
      if (span.length() > hub->config().maxFrame - state.frame->length)
        return false;
      auto size = state.frame->length + span.length();
      state.frame->append(span);
      return state.frame->length == size;
    }) ? 1 : -1;
  }

  template <typename Link>
  int messageEnd(Link &link) {
    if (!link.state().authenticated) {
      link.state().pending = true;
      return 1;
    }
    return processFrame(link);
  }

  template <typename Link>
  int processFrame(Link &link) {
    auto &state = link.state();
    auto msg = Ztc::msg(ZuBSpan{state.frame->data(), state.frame->length});
    if (!msg) {
      ZiLOG(Warning, "Ztc.Hub", "invalid Ztc message");
      link.close(Zws::CloseCode::InvalidData);
      return -1;
    }
    if (state.agent) {
      if (msg->body_type() == Ztc::fbs::Body::Request) {
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      // Agent output is correlated by the forwarded sequence number.  The
      // payload is routed below while this link still owns the receive turn.
      auto body = msg->body();
      uint64_t seqNo = 0;
      switch (msg->body_type()) {
        case Ztc::fbs::Body::Ack: seqNo = msg->body_as_Ack()->seqNo(); break;
        case Ztc::fbs::Body::Telemetry: seqNo = msg->body_as_Telemetry()->seqNo(); break;
        case Ztc::fbs::Body::EOS: seqNo = msg->body_as_EOS()->seqNo(); break;
        case Ztc::fbs::Body::Error: seqNo = msg->body_as_Error()->seqNo(); break;
        default: break;
      }
      if (!body || !seqNo ||
          (msg->deviceId() && Zfb::Load::str(msg->deviceId()) != state.deviceID) ||
          (msg->agentGen() && msg->agentGen() != state.generation)) {
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      Ztc::RouteInfo route;
      // Replies already in flight may arrive after unsubscribe or overflow.
      // Removing one route must not terminate the agent's other streams.
      if (!hub->route(state.sessionID, state.generation, seqNo, route)) return 1;
      if (msg->subId() && msg->subId() != route.subID) {
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      auto output = agentFrame(route, msg);
      if (!output) {
        link.close(Zws::CloseCode::InvalidData);
        return -1;
      }
      if (output->length > hub->config().maxFrame) {
        link.close(Zws::CloseCode::TooLarge);
        return -1;
      }
      bool terminal = msg->body_type() == Ztc::fbs::Body::EOS ||
        msg->body_type() == Ztc::fbs::Body::Error ||
        (msg->body_type() == Ztc::fbs::Body::Ack &&
          msg->body_as_Ack()->status() != Ztc::fbs::AckStatus::OK);
      if (!hub->sendFrontend(route.frontEndID, ZuMv(output)) || terminal)
        hub->removeSubscription(route.frontEndID, route.subID,
          [this](Ztc::RouteInfo route) {
            hub->sendAgent(route.agentSessionID, route.agentGeneration,
              unsubscribeFrame(route));
          });
    } else {
      if (!Ztc::Hubd::validFrontMessage(msg) ||
          (msg->body_as_Request()->interval() &&
            msg->body_as_Request()->interval() < hub->config().minRefreshMS)) {
        link.close(Zws::CloseCode::Protocol);
        return -1;
      }
      auto sub = msg;
      if (!sub->body_as_Request()->subscribe()) {
        hub->removeSubscription(state.sessionID,
          sub->subId(), [this](Ztc::RouteInfo route) {
            hub->sendAgent(route.agentSessionID, route.agentGeneration,
              unsubscribeFrame(route));
          });
        return 1;
      }
      uint64_t agent = 0, generation = 0, seqNo = 0;
      Ztc::HubError::T error = Ztc::HubError::BadReq;
      if (!hub->addSubscription(state.sessionID, sub->subId(),
          sub->deviceId()->string_view(), agent, generation, seqNo, error)) {
        auto message = error == Ztc::HubError::NoAgent ? "no agent" :
          error == Ztc::HubError::DuplicateSub ? "duplicate subscription" :
          "invalid subscription";
        hub->sendFrontend(state.sessionID, errorFrame(sub->subId(),
          sub->deviceId()->string_view(), generation, error, message));
        return 1;
      }
      Zfb::IOBuilder builder{ZmRef<ZiIOBuf>{new ZiIOBufAlloc<1024, 1U<<30,
        "Ztc.Hub.Frame">}};
      auto requestData = ZfbStruct::ctor<Ztc::Request>(sub->body_as_Request());
      requestData.seqNo = seqNo;
      auto request = ZfbStruct::save(builder, requestData);
      builder.Finish(Ztc::saveMsg(builder,
        Ztc::fbs::Body::Request, request.Union()));
      if (!hub->sendAgent(agent, generation, builder.buf())) {
        hub->removeSubscription(state.sessionID, sub->subId());
        hub->sendFrontend(state.sessionID, errorFrame(sub->subId(),
          sub->deviceId()->string_view(), generation,
          Ztc::HubError::AgentGone, "agent disconnected"));
      }
    }
    return 1;
  }

  template <typename Link>
  void authorize(ZmRef<Link> hold, int error,
      Zum::ServicePrincipal principal) {
    auto server = hold->app();
    server->rxRun([this, hold = ZuMv(hold), error,
        principal = ZuMv(principal)]() mutable {
      auto &state = hold->state();
      if (state.closed || error != Zum::ServiceError::OK) {
        ZiLOG(Warning, "Ztc.Hub", ([error](auto &s) {
          s << "WSS authentication failed error=" << int(error);
        }));
        hold->close(Zws::CloseCode::Policy);
        return;
      }
      state.authenticated = true;
      state.agent = Ztc::Hubd::authorized(principal, "Telemetry");
      if (state.agent) {
        state.deviceID = principal.subject;
        Ztc::HubError::T error = Ztc::HubError::Unauthorized;
        if (!hub->addAgent(state.sessionID, state.deviceID,
            state.generation, principal, sender(hold), error)) {
          hold->close(Zws::CloseCode::Policy);
          state.closed = true;
        } else {
          std::cout << "agent accepted " << state.deviceID << '\n' <<
            std::flush;
        }
      } else {
        if (principal.audience != hub->config().audience ||
            !Ztc::Hubd::authorized(principal, "Request") ||
            !hub->addFrontend(state.sessionID, principal, sender(hold))) {
          ZiLOG(Warning, "Ztc.Hub", "front-end authorization failed");
          hold->close(Zws::CloseCode::Policy);
          state.closed = true;
        }
      }
      if (!state.closed) {
        auto server = hold->app();
        server->mx()->add(&state.expiry, ZuTime{principal.expires},
          ZmScheduler::Update, [hold](auto &&arm) mutable {
            return arm([hold]() mutable {
              hold->state().closed = true;
              hold->close(Zws::CloseCode::Policy);
            });
          }, server->rxThread());
      }
      if (!state.closed && state.pending) {
        state.pending = false;
        if (processFrame(*hold) < 0) state.closed = true;
      }
    });
  }

  template <typename Link>
  void connected(Link &link, const Zhttp::ConnectedInfo &) {
    auto &state = link.state();
    state.sessionID = nextSessionID();
    state.generation = state.sessionID;
    auto hold = ZmRef<Link>{&link};
    ZuBSpan authorization = link.authorization();
    if (!state.agent && !bearer(authorization)) {
      ZtString<> cookie;
      Zum::ServicePrincipal principal;
      if (!cookieValue(link.cookie(), cookie) ||
          !hub->browserPrincipal(cookie, principal)) {
        link.close(Zws::CloseCode::Policy);
        state.closed = true;
        return;
      }
      authorize(ZuMv(hold), Zum::ServiceError::OK, ZuMv(principal));
      return;
    }
    if (!bearer(authorization)) {
      link.close(Zws::CloseCode::Policy);
      state.closed = true;
      return;
    }
    ZtString<> token;
    token << authorization.offset(7);
    hub->verify(ZuMv(token), [this, hold = ZuMv(hold)](
        int error, Zum::ServicePrincipal principal) mutable {
      authorize(ZuMv(hold), error, ZuMv(principal));
    });
  }

  template <typename Link>
  void disconnected(Link &link, bool) {
    auto &state = link.state();
    ZiLOG(Info, "Ztc.Hub", ([agent = state.agent](auto &s) {
      s << (agent ? "agent disconnected" : "client disconnected");
    }));
    state.closed = true;
    link.app()->mx()->del(&state.expiry);
    state.ready.clean();
    state.egresses.clean();
    if (!state.authenticated) return;
    if (state.agent) {
      hub->removeAgent(state.sessionID, state.deviceID, state.generation,
        [this](Ztc::RouteInfo route) {
          if (hub->sendFrontend(route.frontEndID, eosFrame(route)))
            hub->completeRoute(route.agentSessionID, route.agentGeneration,
              route.requestSeqNo);
        });
    } else
      hub->removeSession(state.sessionID, [this](Ztc::RouteInfo route) {
        hub->sendAgent(route.agentSessionID, route.agentGeneration,
          unsubscribeFrame(route));
      });
  }
};

class Listener : public ZmObject {
public:
  Listener(Ztc::Hubd *hub_, Ztc::ListenerCf cf_, bool *failed_,
      ZmSemaphore *done_) :
    hub{hub_}, cf{ZuMv(cf_)}, failed{failed_}, done{done_},
    app{hub, cf, failed, done},
    ssfApp{hub_, cf.browserPath, hub->config().ssfCallbackPath, cf.origins},
    server{&app, ZiIP{cf.bind}, cf.port} { }

  bool init(ZiMultiplex *mx) {
    Zws::Config ws;
    ws.maxMessage = hub->config().maxFrame;
    ws.maxQueuedInput = hub->config().telemetryBytes;
    ws.handshakeTimeout = hub->config().upgradeTimeout;
    ws.closeTimeout = hub->config().closeTimeout;
    ws.pingInterval = hub->config().pingInterval;
    ws.pongTimeout = hub->config().idleTimeout;
    auto config = Zhttp::H2Config{}.certPath(cf.cert).keyPath(cf.key);
    if (!server.init(Zhttp::HubConfig{mx, "1", "2"}, config, ws)) return false;
    initialized = true;
    auto ssfConfig = Zhttp::ServerConfig{}.localIP(ZiIP{cf.bind})
      .port(cf.ssfPort ? cf.ssfPort : uint16_t(cf.port + 1))
      .idleTimeout(hub->config().idleTimeout).retainedBodyMax(hub->config().maxFrame)
      .tls(Zhttp::H2Config{}.policy(Zhttp::H2Policy::Disable)
        .certPath(cf.cert).keyPath(cf.key));
    if (ssf.init(Zhttp::HubConfig{mx, "1", "2"}, ZuMv(ssfConfig), &ssfApp))
      return true;
    server.final();
    initialized = false;
    return false;
  }
  bool start() {
    if (!server.start()) return false;
    serverStarted = true;
    if (ssf.start()) {
      ssfStarted = true;
      return true;
    }
    stop();
    return false;
  }
  bool stop() {
    bool ok = true;
    if (ssfStarted) {
      ok = ssf.stop();
      ssfStarted = false;
    }
    if (serverStarted) {
      server.stopAccepting();
      ok = ZmBlock<bool>{}([this](auto wake) {
        server.stop([wake = ZuMv(wake)](bool value) mutable { wake(value); });
      }) && ok;
      serverStarted = false;
    }
    if (initialized) {
      ssf.final();
      server.final();
      initialized = false;
    }
    return ok;
  }

private:
  Ztc::Hubd *hub;
  Ztc::ListenerCf cf;
  bool *failed;
  ZmSemaphore *done;
  App app;
  SSF_::App ssfApp;
  Zws::Server<App, Zhttp::H1TLS> server;
  Zhttp::Server<SSF_::App> ssf;
  bool initialized = false;
  bool serverStarted = false;
  bool ssfStarted = false;
};

} // namespace ZtcHub_

struct Options {
  ZtString<> config{"ztchub.conf"};
  bool help = false;
};

ZfStruct(, (Options, CLI),
  (((config), (CLI::Opt<'c'>, CLI::Long<"config">)),
    (String, "ztchub.conf")),
  (((help), (CLI::Flag<'h'>, CLI::Long<"help">)), (Bool)));

static ZmSemaphore done;
static void trapped() { done.post(); }

int main(int argc, char **argv)
{
  Options options;
  try {
    argc = ZfCLI::load(options, argc, argv);
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
  if (options.help) {
    std::cout << "Usage: ztchub [-c|--config=PATH]\n";
    return 0;
  }
  if (argc != 1) return 1;

  ZiLog::init("ztchub");
  ZiLog::level(Ze::Info);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();
  struct LogStop { ~LogStop() { ZiLog::stop(); } } logStop;

  try {
    auto loaded = ZvCf::load(options.config);
    auto cf = ZfCf::handler<Ztc::HubdCf>(loaded.p<1>()).ctor();
    const char *secret = ::getenv("ZUM_CLIENT_SECRET");
    const char *callbackAuth = ::getenv("ZUM_SSF_CALLBACK_AUTH");
    if (!secret || !*secret || !callbackAuth || !*callbackAuth) {
      std::cerr << "ZUM_CLIENT_SECRET and ZUM_SSF_CALLBACK_AUTH are required\n";
      return 1;
    }
    ZiMxParams params;
    params.scheduler([](auto &s) {
      s.nThreads(4)
        .thread(1, [](auto &t) { t.name("rx"); t.isolated(1); })
        .thread(2, [](auto &t) { t.name("tx"); t.isolated(1); })
        .thread(3, [](auto &t) { t.isolated(1); t.name("hubdb"); })
        .thread(4, [](auto &t) { t.isolated(1); t.name("hubstore"); });
    }).rxThread(1).txThread(2);
    ZiMultiplex mx{ZuMv(params)};
    if (!mx.start()) return 1;
    Ztc::Hubd hub;
    Zum::PingHTTP transport;
    if (!transport.init(&mx, cf.issuer, cf.managementIssuer,
        cf.managementURL, cf.caPath) ||
        !hub.init(ZuMv(cf), &mx, transport.fn(),
          Zum::Bytes{ZuCSpan{secret}}, callbackAuth)) {
      std::cerr << "ztchub initialization/startup failed\n";
      hub.final();
      transport.final();
      mx.stop();
      return 1;
    }
    hub.setRefreshRevocationFn([](Zum::RefreshID, int64_t) {
      ZiLOG(Info, "Ztc.Hub", "refresh family revocation received");
      std::cout << "refresh family revocation received\n" << std::flush;
    });
    bool ready = false;
    ZmSemaphore started;
    hub.start([&](int error) { ready = error == Zum::ServiceError::OK;
      started.post(); });
    started.wait();
    if (!ready) {
      std::cerr << "ztchub Zum service startup/publication failed\n";
      hub.final();
      transport.final();
      mx.stop();
      return 1;
    }
    bool failed = false;
    ZtArray<ZmRef<ZtcHub_::Listener>,
      ZtArrayHeapID<"Ztc.Hub.Listeners">> listeners;
    for (auto &listenerCf: hub.config().listeners) {
      ZmRef<ZtcHub_::Listener> listener =
        new ZtcHub_::Listener{&hub, listenerCf, &failed, &done};
      if (!listener->init(&mx) || !listener->start()) {
        listener->stop();
        failed = true;
        break;
      }
      listeners.push(ZuMv(listener));
    }
    if (failed) {
      for (auto &listener: listeners) listener->stop();
      hub.stop();
      hub.final();
      transport.final();
      mx.stop();
      return 1;
    }
    std::cout << "ztchub ready\n" << std::flush;
    ZmTrap::sigintFn(trapped);
    ZmTrap::trap();
    done.wait();
    ZmTrap::sigintFn(nullptr);
    for (auto &listener: listeners) listener->stop();
    bool ok = hub.stop();
    hub.final();
    transport.final();
    mx.stop();
    return ok && !failed ? 0 : 1;
  } catch (const ZeException &e) {
    std::cerr << e << '\n';
    return 1;
  }
}
