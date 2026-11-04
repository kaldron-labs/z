//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZgrpcServer_HH
#define ZgrpcServer_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZgrpcEndpoint.hh>

namespace Zgrpc {

template <typename App, typename Catalog> class Server;
template <typename App, typename Catalog> struct ServerResponse;

template <typename App, typename Catalog>
struct ServerWork : WorkEntry {
  using Response = ServerResponse<App, Catalog>;
  Server<App, Catalog> *const server;
  const uint64_t id;
  const Limits limits;
  const CallRoute rx, tx;
  ZmRef<RxInput> input; // Rx only; detached receive work owns its callbacks.
  void (*startFn)(ServerWork *, Response *);
  void (*queueFn)(ServerWork *);
  void (*failFn)(ServerWork *, Code::T);
  void (*closeFn)(ServerWork *);
  void (*doneFn)(ServerWork *, bool);
  void (*signFn)(ServerWork *, ZuBSpan);

  ServerWork(Server<App, Catalog> *owner, uint64_t value) :
    server{owner}, id{value}, limits{owner->limits()},
    rx{owner->calls().rx()}, tx{owner->calls().tx()} { }
};

template <typename App, typename Catalog>
struct ServerResponse : ZmObject, MResBuilder<ServerResponse<App, Catalog>, Catalog> {
  using Base = MResBuilder<ServerResponse, Catalog>;
  ZmRef<ServerWork<App, Catalog>> work;
  Code::T error = Code::OK;
  using Base::header;

  bool neverIndex(ZuBSpan name) const { return work->server->neverIndex(name); }
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return error == Code::OK ? Base::bodyPolicy() : Zhttp::BodyPolicy::None;
  }
  template <typename Key, typename Value, typename L> void header(L &&l) const {
    if (error == Code::OK) Base::template header<Key, Value>(ZuFwd<L>(l));
    else if constexpr (ZuTypeIn<Value, Zhttp::HeaderValues<Key, ResControl>>{}) l();
  }
  template <typename Key, typename L> void header(L &&l) const {
    if (error == Code::OK) Base::template header<Key>(ZuFwd<L>(l));
    else if constexpr (ZuIsSame<Key, ZuStringT<"grpc-status">>{}) l(ZuBox<int>{int(error)});
  }
  template <typename L> void header(L &&l) const {
    if (error == Code::OK) Base::header(ZuFwd<L>(l));
  }
  template <typename M, typename B> void producerStarted(M *, B *) { work->startFn(work.ptr(), this); }
  template <typename M, typename B> void producerQueued(M *, B *) { work->queueFn(work.ptr()); }
  template <typename M> void productionFailed(M *, Code::T code) { work->failFn(work.ptr(), code); }
  template <typename M> void signMessage(M *, ZuBSpan bytes) { work->signFn(work.ptr(), bytes); }
  void close() { Base::close(); work->closeFn(work.ptr()); }
  void completed(const Zhttp::ResponseResult &result) { work->doneFn(work.ptr(), result.ok()); }
};
template <typename App, typename Catalog>
using ServerResQ = Zhttp::ResponseQ<ServerResponse<App, Catalog>, ZuStringT<"Zgrpc.Server.Response">>;

template <typename App, typename M,
  bool Stream = M::Kind == Kind::ClientStream || M::Kind == Kind::Duplex>
struct ServerObserver { struct T { }; };
template <typename App, typename M>
struct ServerObserver<App, M, true> {
  using T = decltype(ZuDeclVal<App *>()->open(static_cast<M *>(nullptr),
    ZuDeclVal<Context &>(), ZuDeclVal<ZmRef<ServerCall<M>>>()));
};
template <typename O> auto serverCancel(O *o, Code::T code, int)
    -> decltype(o->cancelled(code), void()) { o->cancelled(code); }
template <typename O> void serverCancel(O *, Code::T, long) { }
template <typename App> auto serverSensitive(const App *app, ZuBSpan name, int)
    -> decltype(bool(app->neverIndex(name))) { return app->neverIndex(name); }
template <typename App> bool serverSensitive(const App *, ZuBSpan, long) { return false; }
template <typename App, typename M> auto serverSign(App *app, M *m, ZuBSpan bytes, int)
    -> decltype(app->signMessage(m, bytes), void()) { app->signMessage(m, bytes); }
template <typename App, typename M> void serverSign(App *, M *, ZuBSpan, long) { }
template <typename App, typename M> auto serverHeaders(App *app, M *method, Context &ctx, int)
    -> decltype(app->headers(method, ctx)) { return app->headers(method, ctx); }
template <typename App, typename M> Metadata serverHeaders(App *, M *, Context &, long) { return Metadata{}; }

struct ServerStart {
  Zhttp::ChannelRef channel;
  Metadata headers;
};

template <typename App, typename Catalog, typename M>
class ServerWorkT : public ServerWork<App, Catalog>, public ZmNHeap<ServerWorkT<App, Catalog, M>> {
friend inline ZuStringT<"Zgrpc.Server.Work"> ZmNHeapID(ServerWorkT *);
  using Self = ServerWorkT;
  using Base = ServerWork<App, Catalog>;
  using Response = Base::Response;
  using Node = ServerResQ<App, Catalog>::Node;
  using Req = M::Request;
  using Res = M::Response;
  using Token = ServerCall<M>;
  using Messages = ZmList<Message<Res>, ZmListHeapID<"Zgrpc.Server.Messages">>;
  enum { StreamIn = M::Kind == Kind::ClientStream || M::Kind == Kind::Duplex };
  // A synchronous HTTP failure can retire work on Rx before Tx returns.
  static inline const CallOps<Res, Res> ops{
    [](void *ptr, uint64_t id, uint64_t gen, Message<Res> message) {
      auto self = ZmRef<Self>{static_cast<Self *>(ptr)};
      return self->identity_(id, gen) && self->send_(ZuMv(message));
    }, nullptr,
    [](void *ptr, uint64_t id, uint64_t gen, FinishData finish) {
      auto self = ZmRef<Self>{static_cast<Self *>(ptr)};
      return self->identity_(id, gen) && self->finish_(ZuMv(finish));
    },
    [](void *ptr, uint64_t, uint64_t, Reply<Res> reply) {
      auto self = static_cast<Self *>(ptr);
      if (self->terminal) return false;
      using Action = CallAction<Self, Reply<Res>>;
      ZmRef<Action> action = new Action{self, ZuMv(reply)};
      self->tx.run([action = ZuMv(action)]() mutable {
	auto &reply = action->data;
	if (reply.message.template is<Message<Res>>() &&
	    !action->token->send_(ZuMv(reply.message.template p<Message<Res>>()))) return;
	(void)action->token->finish_({ZuMv(reply.status), ZuMv(reply.trailers)});
      });
      return true;
    },
    [](void *ptr, uint64_t, uint64_t) { static_cast<Self *>(ptr)->failRx_(Code::CANCELLED); }
  };

public:
  ServerWorkT(Server<App, Catalog> *owner, uint64_t id, String authority, Code::T refusal) :
    Base{owner, id},
    m_token{new Token{id, 1, this->rx, this, this->tx, this, this->limits.maxQueue, ops}},
    m_authority{ZuMv(authority)}, m_refusal{refusal} {
    this->cancelFn = [](WorkData *ptr, Code::T code) { static_cast<Self *>(ptr)->failRx_(code); };
    this->startFn = [](Base *ptr, Response *response) {
      auto self = static_cast<Self *>(ptr);
      if (self->m_txInvalid) return;
      self->m_response = response;
      self->drainTx_();
    };
    this->queueFn = [](Base *ptr) { static_cast<Self *>(ptr)->queueTx_(); };
    this->failFn = [](Base *ptr, Code::T code) { static_cast<Self *>(ptr)->failTx_(code); };
    this->closeFn = [](Base *ptr) { static_cast<Self *>(ptr)->m_response = nullptr; };
    this->doneFn = [](Base *ptr, bool ok) { static_cast<Self *>(ptr)->doneTx_(ok); };
    this->signFn = [](Base *ptr, ZuBSpan bytes) {
      serverSign(ptr->server->app(), static_cast<M *>(nullptr), bytes, 0);
    };
  }
  void accepted(Zhttp::Channel *channel, RxInput &input) {
    m_channel = channel;
    m_expires = input.meta().timed() ? expiry(input.meta().timeout()) : -1;
    bool expired = m_expires >= 0 && remaining(m_expires) <= 0;
    Metadata headers;
    bool headerError = false;
    if (m_refusal == Code::OK && !expired) {
      try {
	auto ctx = context_(input);
	headers = serverHeaders(this->server->app(), static_cast<M *>(nullptr), ctx, 0);
	headerError = headers.failed();
      } catch (...) { headerError = true; }
    }
    using Start = CallAction<Self, ServerStart>;
    ZmRef<Start> start = new Start{this, {Zhttp::ChannelRef{channel}, ZuMv(headers)}};
    this->tx.run([start = ZuMv(start)]() mutable {
      auto self = start->token.ptr();
      if (self->m_txInvalid) return;
      self->m_txChannel = ZuMv(start->data.channel);
      self->m_headers = ZuMv(start->data.headers);
      self->m_txChannel->responseCancel(Zhttp::StreamCancelFn{
	[self = ZmRef<Self>{self}]() { self->failTx_(Code::CANCELLED); }});
    });
    if (m_refusal != Code::OK) {
      this->tx.run([self = ZmRef<Self>{this}]() { self->rejectTx_(); });
      return;
    }
    // Publish the response route before expiry can select terminal status.
    // Expired calls bypass application callbacks but still return gRPC status.
    if (m_expires >= 0) {
      armRx_();
      if (!liveRx_()) return;
    }
    if (headerError) { errorRx_(Code::INTERNAL); return; }
    if constexpr (StreamIn) {
      try {
	auto ctx = context_(input);
	m_observer = this->server->app()->open(static_cast<M *>(nullptr), ctx, m_token);
	if (!m_observer) { errorRx_(Code::INTERNAL); return; }
      } catch (...) { errorRx_(Code::INTERNAL); return; }
      this->tx.run([self = ZmRef<Self>{this}]() { self->ensureTx_(); });
    }
  }
  bool message(Message<Req> value) {
    if (!liveRx_() || m_refusal != Code::OK) return true;
    if constexpr (StreamIn) {
      if (!m_observer) return false;
      using R = decltype(m_observer->message(ZuMv(value)));
      if constexpr (ZuIsSame<R, bool>{}) return m_observer->message(ZuMv(value));
      else { m_observer->message(ZuMv(value)); return true; }
    } else { m_request = ZuMv(value); return true; }
  }
  void finished(RxInput &input, bool ok) {
    m_rxEnd = true;
    if (!liveRx_()) { releaseRx_(); return; }
    if (!ok) { failRx_(input.code()); return; }
    if (m_refusal != Code::OK) return;
    try {
      if constexpr (StreamIn) {
	if (m_observer) m_observer->peerEnd();
      } else {
	if (!m_request.template is<Message<Req>>()) { failRx_(Code::INTERNAL); return; }
	auto ctx = context_(input);
	this->server->app()->request(static_cast<M *>(nullptr), ctx,
	  ZuMv(m_request.template p<Message<Req>>()), m_token);
      }
    } catch (...) { errorRx_(Code::INTERNAL); }
  }

private:
  bool liveRx_() {
    if (this->terminal) return false;
    if (m_expires >= 0 && remaining(m_expires) <= 0) {
      errorRx_(Code::DEADLINE_EXCEEDED);
      return false;
    }
    return true;
  }
  Context context_(RxInput &input) {
    return {m_channel->session(), &m_channel->remoteIP(), m_channel->remotePort(),
      m_authority, &input.meta().metadata(), m_expires};
  }
  bool identity_(uint64_t id, uint64_t gen) const { return id == this->id && gen == 1 && !m_txInvalid; }
  bool send_(Message<Res> message) {
    ZiAssert(this->tx.invoked(), "Zgrpc", (), "server send outside Tx", return false);
    if (m_txError != Code::OK) return false;
    if (m_txInvalid || m_finishing) return failTx_(Code::INTERNAL);
    if (m_messages.count_() >= this->limits.maxQueue) return failTx_(Code::RESOURCE_EXHAUSTED);
    m_messages.push(ZuMv(message));
    ensureTx_(); drainTx_();
    return !m_txInvalid && m_txError == Code::OK;
  }
  bool finish_(FinishData finish) {
    ZiAssert(this->tx.invoked(), "Zgrpc", (), "server finish outside Tx", return false);
    if (m_txInvalid || m_txError != Code::OK || m_finishing) return false;
    m_finishing = true;
    m_finish = ZuMv(finish);
    ensureTx_(); drainTx_();
    return !m_txInvalid && m_txError == Code::OK;
  }
  void ensureTx_() {
    if (m_txInvalid || m_txError != Code::OK || m_submitted || !m_txChannel) return;
    ZmRef<Node> response = new Node{};
    response->work = this;
    auto &builder = response->template init<M>(this->limits);
    builder.metadata = ZuMv(m_headers);
    if (m_finishing && !m_messages.count_() && m_finish.status.code != Code::OK) {
      if (!builder.trailersOnly(ZuMv(m_finish.status), ZuMv(m_finish.trailers))) {
	failTx_(Code::INTERNAL); return;
      }
      selectTx_();
    }
    m_submitted = true;
    m_txChannel->send(ZuMv(response));
  }
  void rejectTx_() {
    if (m_txInvalid || !m_txChannel) return;
    ZmRef<Node> response = new Node{};
    response->work = this;
    response->error = m_refusal;
    m_submitted = true;
    selectTx_();
    m_txChannel->send(ZuMv(response));
  }
  void drainTx_() {
    if (m_txInvalid || m_txError != Code::OK || m_txFinished || !m_response || m_draining) return;
    m_draining = true;
    unsigned n = 0;
    while (m_response && m_messages.count_() && n++ < this->limits.workBatch) {
      auto message = m_messages.shift();
      // Refusal can synchronously close and retire the HTTP producer.
      ZmRef<Node> response = static_cast<Node *>(m_response);
      if (!response->template send_<M>(ZuMv(message->data()))) break;
    }
    if (m_response && !m_txInvalid && m_txError == Code::OK) {
      if (m_messages.count_()) this->tx.run([self = ZmRef<Self>{this}]() { self->drainTx_(); });
      else if (m_finishing && !m_finishSubmitted) {
	m_finishSubmitted = true;
	ZmRef<Node> response = static_cast<Node *>(m_response);
	if (response->finish_(ZuMv(m_finish.status), ZuMv(m_finish.trailers))) {
	  if (!response->template builder<M>()->queuedMessages()) selectTx_();
	}
	else failTx_(Code::INTERNAL);
      }
    }
    m_draining = false;
  }
  void queueTx_() {
    if (m_framesPosted) return;
    m_framesPosted = true;
    this->tx.run([self = ZmRef<Self>{this}]() {
      self->m_framesPosted = false;
      if (self->m_txInvalid || self->m_txError != Code::OK || !self->m_response) return;
      ZmRef<Node> response = static_cast<Node *>(self->m_response);
      auto builder = response->template builder<M>();
      if (builder->drain_() && self->m_finishSubmitted && !builder->queuedMessages())
	self->selectTx_();
    });
  }
  void selectTx_() {
    if (m_txFinished) return;
    m_txFinished = true;
    m_token->invalidateTx_();
    this->rx.run([self = ZmRef<Self>{this}]() {
      if (!self->terminal) {
	self->terminal = true;
	self->m_token->invalidateRx_();
	self->rx.scheduler->del(&self->m_timer);
      }
      self->releaseRx_();
    });
  }
  void doneTx_(bool ok) {
    if (m_txDone) return;
    m_txDone = true;
    m_txInvalid = true;
    m_token->invalidateTx_();
    m_messages.clean();
    m_response = nullptr;
    if (m_txChannel && !m_submitted) m_txChannel->responseCancel({});
    m_txChannel.reset();
    auto code = m_txError != Code::OK ? m_txError : Code::UNAVAILABLE;
    bool transport = m_txError == Code::OK || m_txError == Code::CANCELLED;
    this->rx.run([self = ZmRef<Self>{this}, ok, code, transport]() {
      self->m_txAck = true;
      if (!ok && !self->terminal) {
	if (transport) self->transportFailRx_(code);
	else self->failRx_(code);
      }
      self->releaseRx_();
    });
  }
  bool failTx_(Code::T code) {
    if (m_txInvalid || m_txError != Code::OK) return false;
    m_txError = code;
    m_messages.clean();
    this->rx.run([self = ZmRef<Self>{this}, code]() {
      if (code == Code::CANCELLED) self->transportFailRx_(code);
      else self->failRx_(code);
    });
    return false;
  }
  void transportFailRx_(Code::T code) {
    if (!liveRx_()) return;
    // Transport completion can precede deferred receive failure delivery.
    if (this->input) {
      if (this->input->failed()) code = this->input->code();
      else if (this->input->meta().failed()) code = this->input->meta().status().code;
    }
    failRx_(code);
  }
  void errorRx_(Code::T code) {
    if (this->terminal) return;
    this->terminal = true;
    m_token->invalidateRx_(code == Code::DEADLINE_EXCEEDED || code == Code::CANCELLED);
    this->rx.scheduler->del(&m_timer);
    if constexpr (StreamIn) {
      if (m_observer) { try { serverCancel(m_observer.ptr(), code, 0); } catch (...) { } }
    }
    using Action = CallAction<Self, FinishData>;
    Status status; status.code = code;
    ZmRef<Action> action = new Action{this, {ZuMv(status), Metadata{}}};
    this->tx.run([action = ZuMv(action)]() mutable {
      auto self = action->token.ptr();
      if (self->m_txInvalid || self->m_txFinished) return;
      self->m_messages.clean();
      self->m_finish = ZuMv(action->data);
      self->m_finishing = true;
      self->ensureTx_(); self->drainTx_();
    });
  }
  void failRx_(Code::T code) {
    if (this->terminal) return;
    this->terminal = true;
    m_token->invalidateRx_(true);
    this->rx.scheduler->del(&m_timer);
    if (this->input) this->input->fail(code);
    else m_rxEnd = true;
    if constexpr (StreamIn) {
      if (m_observer) { try { serverCancel(m_observer.ptr(), code, 0); } catch (...) { } }
    }
    this->tx.run([self = ZmRef<Self>{this}]() {
      if (self->m_txInvalid || self->m_txDone) return;
      self->m_txInvalid = true;
      self->m_token->invalidateTx_();
      self->m_messages.clean();
      if (self->m_txChannel) {
	if (!self->m_submitted) self->m_txChannel->responseCancel({});
	self->m_txChannel->disconnect();
      }
      if (!self->m_submitted) self->doneTx_(false);
    });
    releaseRx_();
  }
  void releaseRx_() {
    if (m_released || !this->terminal || !m_rxEnd || !m_txAck) return;
    m_released = true;
    this->input = nullptr;
    m_channel.reset();
    m_request = {};
    if constexpr (StreamIn) m_observer = nullptr;
    // Drain timer callbacks already queued on Rx before releasing the work.
    if (this->registered) this->rx.run([self = ZmRef<Self>{this}]() {
      self->server->calls().release(self.ptr());
    });
  }
  void armRx_() {
    auto left = remaining(m_expires);
    if (left <= 0) { errorRx_(Code::DEADLINE_EXCEEDED); return; }
    this->rx.scheduler->add(&m_timer, ZuTime{ZuTime::Nano{m_expires}},
      ZmScheduler::Update, [this](auto &&arm) {
	return arm([this]() { if (!this->terminal) this->armRx_(); });
      }, this->rx.thread);
  }

  ZmRef<Token> m_token;
  alignas(Zm::CacheLineSize)
  Zhttp::ChannelRef m_channel;
  String m_authority;
  const Code::T m_refusal;
  int64_t m_expires = -1;
  Decoded<Req> m_request;
  typename ServerObserver<App, M>::T m_observer;
  ZmScheduler::Timer m_timer;
  bool m_rxEnd = false, m_txAck = false, m_released = false;

  alignas(Zm::CacheLineSize)
  Zhttp::ChannelRef m_txChannel;
  Response *m_response = nullptr;
  Metadata m_headers;
  Messages m_messages;
  FinishData m_finish;
  Code::T m_txError = Code::OK;
  bool m_finishSubmitted = false, m_framesPosted = false;
  bool m_submitted = false, m_finishing = false, m_draining = false;
  bool m_txFinished = false, m_txInvalid = false, m_txDone = false;
};

template <typename App, typename Catalog>
struct ServerParser : MReqParser<ServerParser<App, Catalog>, Catalog> {
  using Base = MReqParser<ServerParser, Catalog>;
  void init(Server<App, Catalog> &value) { server = &value; }
  Limits limits() const { return server->limits(); }
  unsigned headerMax() const { return server->headerMax(); }
  bool operation(Zhttp::Method::T method, Zhttp::Target &target) {
    authority = target.authority.raw;
    bool ok = Base::operation(method, target);
    if (work) work->input = this->input();
    return ok;
  }
  template <typename M> auto receiver(M *) {
    auto result = server->template admit<M>(ZuMv(authority));
    work = result;
    return result;
  }
  bool unknownMethod(Zhttp::Method::T method, Zhttp::Target &) {
    if (method != Zhttp::Method::POST) return false;
    using M = ZuType<0, typename Catalog::List>;
    auto result = server->template admit<M>(ZuMv(authority), Code::UNIMPLEMENTED);
    work = result;
    return this->template init<M>(ZuMv(result), limits(), headerMax());
  }
  using Base::init;
  void reset() { Base::reset(); work = nullptr; authority.null(); }
  Server<App, Catalog> *server = nullptr;
  ZmRef<ServerWork<App, Catalog>> work;
  String authority;
};

struct ServerEvents {
  template <typename A, typename E> static auto connected(A *app, E event, int)
    -> decltype(app->connected(event), void()) { app->connected(event); }
  template <typename A, typename E> static void connected(A *, E event, long) { }
  template <typename A, typename E> static auto disconnected(A *app, E event, int)
    -> decltype(app->disconnected(event), void()) { app->disconnected(event); }
  template <typename A, typename E> static void disconnected(A *, E event, long) { }
  template <typename A, typename E> static auto listening(A *app, E event, unsigned port, int)
    -> decltype(app->listening(event, port), void()) { app->listening(event, port); }
  template <typename A, typename E> static void listening(A *, E event, unsigned port, long) { }
  template <typename A, typename E> static auto listenFailed(A *app, E event, bool transient, int)
    -> decltype(app->listenFailed(event, transient), void()) { app->listenFailed(event, transient); }
  template <typename A, typename E> static void listenFailed(A *, E event, bool transient, long) { }
};

template <typename App, typename Catalog>
class Server : public Zhttp::Server<Server<App, Catalog>, ServerParser<App, Catalog>,
    ServerResQ<App, Catalog>, Selection> {
  using HTTP = Zhttp::Server<Server, ServerParser<App, Catalog>, ServerResQ<App, Catalog>, Selection>;
public:
  ~Server() { final(); }
  bool init(const Zhttp::HubConfig &hub, ServerConfig config, App *app) {
    if (m_initialized || !app || !valid(config.limits())) return false;
    if (config.tlsEnabled() && config.tlsConfig().policy() == Zhttp::H2Policy::Prefer) {
      auto tls = config.tlsConfig();
      tls.policy(Zhttp::H2Policy::Force);
      config.tls(ZuMv(tls));
    }
    config.bodyMax(uint64_t(-1));
    if (config.retainedBodyMax() > config.limits().maxQueueBytes)
      config.retainedBodyMax(config.limits().maxQueueBytes);
    if (!HTTP::init(hub, config)) return false;
    if (!m_calls.init(hub, config.limits(), EndpointCalls::StartFn{
	[this](EndpointCalls::DoneFn done) {
	  HTTP::start([done = ZuMv(done)](bool ok) mutable { done(ok); });
	}}, EndpointCalls::StopFn{
	[this](EndpointCalls::DoneFn done) {
	  HTTP::stop([done = ZuMv(done)](bool ok) mutable { done(ok); });
	}})) { HTTP::final(); return false; }
    m_app = app;
    m_headerMax = config.tlsConfig().maxHeaderListSize();
    m_initialized = true;
    return true;
  }
  bool start() { return m_initialized && m_calls.start(); }
  template <typename Done> void start(Done &&done) {
    if (!m_initialized) { done(false); return; }
    m_calls.start(EndpointCalls::DoneFn{ZuFwd<Done>(done)});
  }

  bool stop() { return ZmBlock<bool>{}([this](auto done) { this->stop(ZuMv(done)); }); }
  template <typename Done> void stop(Done &&done) { m_calls.stop(EndpointCalls::DoneFn{ZuFwd<Done>(done)}); }
  void final() {
    if (!m_initialized) return;
    m_calls.final(); HTTP::final(); m_initialized = false; m_app = nullptr;
  }
  bool up() const { return m_calls.up(); }
  unsigned pending() const { return m_calls.pending(); }
  const Limits &limits() const { return m_calls.limits(); }
  unsigned headerMax() const { return m_headerMax; }
  EndpointCalls &calls() { return m_calls; }
  App *app() const { return m_app; }
  bool neverIndex(ZuBSpan name) const { return serverSensitive(m_app, name, 0); }
  template <typename M> ZmRef<ServerWorkT<App, Catalog, M>> admit(String authority, Code::T refusal = Code::OK) {
    bool reserved = refusal == Code::OK && m_calls.reserve();
    if (!reserved && refusal == Code::OK) refusal = up() ? Code::RESOURCE_EXHAUSTED : Code::CANCELLED;
    ZmRef<ServerWorkT<App, Catalog, M>> work = new ServerWorkT<App, Catalog, M>{
      this, ++m_sequence, ZuMv(authority), refusal};
    if (reserved) m_calls.insert(work);
    return work;
  }

  void connected(Zhttp::Transport::T event) { ServerEvents::connected(m_app, event, 0); }
  void disconnected(Zhttp::Transport::T event) { ServerEvents::disconnected(m_app, event, 0); }
  void connected(Zhttp::Session event) { ServerEvents::connected(m_app, event, 0); }
  void disconnected(Zhttp::Session event) { ServerEvents::disconnected(m_app, event, 0); }
  void listening(Zhttp::Transport::T event, unsigned port) { ServerEvents::listening(m_app, event, port, 0); }
  void listenFailed(Zhttp::Transport::T event, bool transient) { ServerEvents::listenFailed(m_app, event, transient, 0); }

private:
  EndpointCalls m_calls;
  App *m_app = nullptr;
  uint64_t m_sequence = 0; // Rx-owned request identity, distinct from HTTP IDs.
  unsigned m_headerMax = Zhttp::DefltMaxHeaderSection;
  bool m_initialized = false;
};

} // Zgrpc

#endif /* ZgrpcServer_HH */
