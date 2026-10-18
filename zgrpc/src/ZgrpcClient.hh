//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZgrpcClient_HH
#define ZgrpcClient_HH

#ifndef ZgrpcLib_HH
#include <zlib/ZgrpcLib.hh>
#endif

#include <zlib/ZgrpcEndpoint.hh>

namespace Zgrpc {

template <typename App, typename Catalog> class Client;
template <typename App, typename Catalog> struct ClientRequest;
template <typename App, typename Catalog> struct ClientParser;

template <typename App, typename Catalog>
struct ClientWork : WorkEntry {
  using Request = ClientRequest<App, Catalog>;
  const uint64_t id;
  const unsigned pool;
  const int64_t expires;
  const Limits limits;
  const CallRoute rx, tx;
  Client<App, Catalog> *const client;

  void (*parseFn)(ClientWork *, ClientParser<App, Catalog> &);
  void (*startFn)(ClientWork *, Request *);
  void (*queueFn)(ClientWork *);
  void (*txFailFn)(ClientWork *, Code::T);
  void (*closeFn)(ClientWork *);
  void (*signFn)(ClientWork *, ZuBSpan);
  void (*resultFn)(ClientWork *, const Zhttp::Result &);
  void (*retiredFn)(ClientWork *);

  ClientWork(Client<App, Catalog> *owner, uint64_t id_, unsigned pool_,
      int64_t expires_, Limits limits_, CallRoute rx_, CallRoute tx_) :
    id{id_}, pool{pool_}, expires{expires_}, limits{limits_}, rx{rx_}, tx{tx_}, client{owner} { }
};

template <typename App, typename Catalog>
struct ClientRequest : ZmObject, MReqBuilder<ClientRequest<App, Catalog>, Catalog> {
  using Base = MReqBuilder<ClientRequest, Catalog>;
  ZmRef<ClientWork<App, Catalog>> work;

  bool neverIndex(ZuBSpan name) const { return work->client->neverIndex(name); }
  template <typename M, typename B> void producerStarted(M *, B *) { work->startFn(work.ptr(), this); }
  template <typename M, typename B> void producerQueued(M *, B *) { work->queueFn(work.ptr()); }
  template <typename M> void productionFailed(M *, Code::T code) { work->txFailFn(work.ptr(), code); }
  template <typename M> void signMessage(M *, ZuBSpan bytes) { work->signFn(work.ptr(), bytes); }
  template <typename L> void operation(L &&l) const {
    if (work->expires >= 0 && remaining(work->expires) <= 0) {
      work->txFailFn(work.ptr(), Code::DEADLINE_EXCEEDED);
      return;
    }
    Base::operation(ZuFwd<L>(l));
  }
  using Base::header;
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (ZuIsSame<Key, ZuStringT<"grpc-timeout">>{}) {
      if (work->expires >= 0) {
	auto left = remaining(work->expires);
	if (left <= 0) work->txFailFn(work.ptr(), Code::DEADLINE_EXCEEDED);
	l(encodeTimeout(left));
      }
    } else Base::template header<Key>(ZuFwd<L>(l));
  }
  void close() { Base::close(); work->closeFn(work.ptr()); }
  void completed(const Zhttp::Result &result) { work->resultFn(work.ptr(), result); }
  void retired() { work->retiredFn(work.ptr()); work = nullptr; }
};

template <typename App, typename Catalog>
using ClientReqQ = ZmPQueue<ClientRequest<App, Catalog>,
  ZmPQueueOverlap<false, ZmPQueueNode<ClientRequest<App, Catalog>,
    ZmPQueueHeapID<"Zgrpc.Client.Request">>>>;

template <typename App, typename Catalog>
struct ClientParser : MResParser<ClientParser<App, Catalog>, Catalog> {
  using Base = MResParser<ClientParser, Catalog>;
  using Base::init;
  void init(ClientRequest<App, Catalog> &request) { request.work->parseFn(request.work.ptr(), *this); }
};
template <typename App, typename Catalog> class Pool;
template <typename App, typename Catalog>
using ClientTxQ = ZmPQTx<Pool<App, Catalog>, ClientReqQ<App, Catalog>, ZmPQTxOrdered<false>>;
template <typename App, typename Catalog>
class Pool : public ZmNHeap<Pool<App, Catalog>>,
    public Zhttp::Pool<Client<App, Catalog>, ClientTxQ<App, Catalog>,
      ClientParser<App, Catalog>, Selection> {
friend inline ZuStringT<"Zgrpc.Client.Pool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<Client<App, Catalog>, ClientTxQ<App, Catalog>,
    ClientParser<App, Catalog>, Selection>;
public:
  explicit Pool(Client<App, Catalog> *client) : Base{client} { }
  auto txQueue() { return &m_requests; }
  void archive_(typename ClientReqQ<App, Catalog>::Node *) { }
  ZmRef<typename ClientReqQ<App, Catalog>::Node> retrieve_(
      typename ClientReqQ<App, Catalog>::Key, typename ClientReqQ<App, Catalog>::Key) { return {}; }
private:
  ClientReqQ<App, Catalog> m_requests;
};

// Optional metadata notifications do not create a second ownership model.
template <typename O> auto notifyHeaders(O *o, const Metadata &meta, int)
    -> decltype(o->headers(meta), void()) { o->headers(meta); }
template <typename O> void notifyHeaders(O *, const Metadata &, long) { }
template <typename O> auto notifyTrailers(O *o, const Metadata &meta, int)
    -> decltype(o->trailers(meta), void()) { o->trailers(meta); }
template <typename O> void notifyTrailers(O *, const Metadata &, long) { }
template <typename O> auto notifyFailed(O *o, Failure failure, int)
    -> decltype(o->failed(ZuMv(failure)), void()) { o->failed(ZuMv(failure)); }
template <typename O> void notifyFailed(O *o, Failure, long) { o->failed(); }

template <typename App, typename Catalog, typename M, typename Observer>
class ClientWorkT : public ClientWork<App, Catalog>, public ZmNHeap<ClientWorkT<App, Catalog, M, Observer>> {
friend inline ZuStringT<"Zgrpc.Client.Work"> ZmNHeapID(ClientWorkT *);
  using Self = ClientWorkT;
  using Base = ClientWork<App, Catalog>;
  using Request = Base::Request;
  using Req = M::Request;
  using Res = M::Response;
  using Token = ClientCall<M>;
  using Messages = ZmList<Message<Req>, ZmListHeapID<"Zgrpc.Client.Messages">>;
  enum { StreamOut = M::Kind == Kind::ServerStream || M::Kind == Kind::Duplex };

  // A synchronous HTTP failure can retire work on Rx before Tx returns.
  static inline const CallOps<Req, Res> ops{
    [](void *ptr, uint64_t id, uint64_t gen, Message<Req> message) {
      auto self = ZmRef<Self>{static_cast<Self *>(ptr)};
      return self->identity_(id, gen) && self->send_(ZuMv(message));
    },
    [](void *ptr, uint64_t id, uint64_t gen) {
      auto self = ZmRef<Self>{static_cast<Self *>(ptr)};
      return self->identity_(id, gen) && self->end_();
    }, nullptr, nullptr,
    [](void *ptr, uint64_t, uint64_t) { static_cast<Self *>(ptr)->failRx_(Code::CANCELLED); }
  };

public:
  ClientWorkT(Client<App, Catalog> *owner, uint64_t id, unsigned pool, int64_t expires,
      ZmRef<Observer> observer) :
    Base{owner, id, pool, expires, owner->limits(), owner->calls().rx(), owner->calls().tx()},
    m_observer{ZuMv(observer)},
    m_token{new Token{id, 1, this->rx, this, this->tx, this, this->limits.maxQueue, ops}} {
    this->cancelFn = [](WorkEntry *ptr, Code::T code) { static_cast<Self *>(ptr)->failRx_(code); };
    this->parseFn = [](Base *ptr, ClientParser<App, Catalog> &parser) {
      auto self = static_cast<Self *>(ptr);
      if (!parser.template init<M>(ZmRef<Self>{self}, self->limits)) return;
      self->m_input = parser.input();
      if (self->terminal) self->m_input->fail(Code::CANCELLED);
    };
    this->startFn = [](Base *ptr, Request *request) {
      auto self = static_cast<Self *>(ptr);
      if (self->m_txInvalid) return;
      self->m_request = request;
      self->drainTx_();
    };
    this->queueFn = [](Base *ptr) { static_cast<Self *>(ptr)->queueTx_(); };
    this->txFailFn = [](Base *ptr, Code::T code) { static_cast<Self *>(ptr)->failTx_(code); };
    this->closeFn = [](Base *ptr) {
      auto self = static_cast<Self *>(ptr);
      self->m_request = nullptr;
      self->m_messages.clean();
    };
    this->signFn = [](Base *ptr, ZuBSpan bytes) {
      auto self = static_cast<Self *>(ptr);
      self->client->signPayload(static_cast<M *>(nullptr), bytes);
    };
    this->resultFn = [](Base *ptr, const Zhttp::Result &result) {
      static_cast<Self *>(ptr)->resultRx_(result);
    };
    this->retiredFn = [](Base *ptr) {
      auto self = static_cast<Self *>(ptr);
      self->m_httpRetired = true;
      self->releaseRx_();
    };
  }
  const ZmRef<Token> &token() const { return m_token; }

  // Admission is already reserved. The request owns field storage before HTTP
  // publishes it to Tx; the ingress action owns the initial message until post.
  void admit(Metadata metadata, Decoded<Req> initial = {}) {
    this->client->calls().insert(ZmRef<WorkEntry>{this});
    if (!this->client->up()) { failRx_(Code::CANCELLED); return; }
    if (this->expires >= 0) {
      if (remaining(this->expires) <= 0) { failRx_(Code::DEADLINE_EXCEEDED); return; }
      armRx_();
      if (this->terminal) return;
    }
    ZmRef<typename ClientReqQ<App, Catalog>::Node> request =
      new typename ClientReqQ<App, Catalog>::Node{};
    request->work = this;
    request->sequence = this->id;
    request->template init<M>(this->limits).metadata = ZuMv(metadata);
    if (initial.template is<Message<Req>>()) {
      using Action = CallAction<Self, Message<Req>>;
      ZmRef<Action> action = new Action{this, ZuMv(initial.template p<Message<Req>>())};
      this->tx.run([action = ZuMv(action)]() mutable {
	if (action->token->send_(ZuMv(action->data))) action->token->end_();
      });
    }
    m_httpSubmitted = true;
    if (!this->client->submit(this->pool, ZuMv(request))) {
      m_httpSubmitted = false;
      failRx_(Code::UNAVAILABLE);
    }
  }

  void accepted(Zhttp::Channel *, RxInput &input) {
    if (!liveRx_()) return;
    notifyHeaders(m_observer.ptr(), input.meta().metadata(), 0);
  }
  bool message(Message<Res> value) {
    if (!liveRx_()) return true;
    if constexpr (StreamOut) {
      using R = decltype(m_observer->message(ZuMv(value)));
      if constexpr (ZuIsSame<R, bool>{}) return m_observer->message(ZuMv(value));
      else { m_observer->message(ZuMv(value)); return true; }
    } else { m_value = ZuMv(value); return true; }
  }
  void finished(RxInput &input, bool ok) {
    m_rxEnd = true;
    if (!liveRx_()) { releaseRx_(); return; }
    if (!ok) {
      // A generic receive abort precedes HTTP's Tx-drained result. Retain the
      // RPC deadline while waiting for that result's native refusal details.
      if (input.httpFailed() && !input.meta().failed() &&
	  m_httpSubmitted && !m_httpReady) return;
      failRx_(input.code());
      return;
    }
    this->terminal = true;
    m_protocolOK = true;
    this->rx.scheduler->del(&m_timer);
    m_token->invalidateRx_();
    m_status = input.meta().takeStatus();
    m_metadata = input.meta().takeMetadata();
    m_trailers = input.meta().takeTrailers();
    invalidateTx_();
    notifyRx_();
    releaseRx_();
  }

private:
  bool liveRx_() {
    if (this->terminal) return false;
    if (this->expires >= 0 && remaining(this->expires) <= 0) {
      failRx_(Code::DEADLINE_EXCEEDED);
      return false;
    }
    return true;
  }
  bool identity_(uint64_t id, uint64_t gen) const { return id == this->id && gen == 1 && !m_txInvalid; }
  bool send_(Message<Req> value) {
    ZiAssert(this->tx.invoked(), "Zgrpc", (), "send outside Tx", return false);
    if (m_txError != Code::OK) return false;
    if (m_txInvalid || m_end) return failTx_(Code::INTERNAL);
    if (m_messages.count_() >= this->limits.maxQueue) return failTx_(Code::RESOURCE_EXHAUSTED);
    m_messages.push(ZuMv(value));
    drainTx_();
    return m_txError == Code::OK;
  }
  bool end_() {
    ZiAssert(this->tx.invoked(), "Zgrpc", (), "end outside Tx", return false);
    if (m_txError != Code::OK) return false;
    if (m_txInvalid || m_end) return failTx_(Code::INTERNAL);
    m_end = true;
    drainTx_();
    return m_txError == Code::OK;
  }
  void drainTx_() {
    if (m_txInvalid || !m_request || m_draining || m_txError != Code::OK) return;
    m_draining = true;
    unsigned n = 0;
    while (m_request && m_messages.count_() && n++ < this->limits.workBatch) {
      auto message = m_messages.shift();
      // Refusal can synchronously close and retire the HTTP producer.
      ZmRef<typename ClientReqQ<App, Catalog>::Node> request =
	static_cast<typename ClientReqQ<App, Catalog>::Node *>(m_request);
      if (!request->template send_<M>(ZuMv(message->data()))) break;
    }
    if (m_request && m_txError == Code::OK) {
      if (m_messages.count_()) {
	this->tx.run([self = ZmRef<Self>{this}]() { self->drainTx_(); });
      } else if (m_end && !m_endSubmitted) {
	m_endSubmitted = true;
	ZmRef<typename ClientReqQ<App, Catalog>::Node> request =
	  static_cast<typename ClientReqQ<App, Catalog>::Node *>(m_request);
	(void)request->end_();
      }
    }
    m_draining = false;
  }
  void queueTx_() {
    if (m_framesPosted) return;
    m_framesPosted = true;
    this->tx.run([self = ZmRef<Self>{this}]() {
      self->m_framesPosted = false;
      if (self->m_txInvalid || self->m_txError != Code::OK || !self->m_request) return;
      ZmRef<typename ClientReqQ<App, Catalog>::Node> request =
	static_cast<typename ClientReqQ<App, Catalog>::Node *>(self->m_request);
      (void)request->template builder<M>()->drain_();
    });
  }
  bool failTx_(Code::T code) {
    if (m_txInvalid || m_txError != Code::OK) return false;
    m_txError = code;
    m_messages.clean();
    this->rx.run([self = ZmRef<Self>{this}, code]() { self->failRx_(code); });
    return false;
  }
  void invalidateTx_() {
    this->tx.run([self = ZmRef<Self>{this}]() {
      if (self->m_txInvalid) return;
      self->m_txInvalid = true;
      self->m_token->invalidateTx_();
      self->m_messages.clean();
      self->m_request = nullptr;
      self->rx.run([self]() { self->m_txDone = true; self->releaseRx_(); });
    });
  }
  void failRx_(Code::T code) {
    if (this->terminal) return;
    ZiAssert(this->rx.invoked(), "Zgrpc", (), "failure outside Rx", return);
    this->terminal = true;
    this->rx.scheduler->del(&m_timer);
    m_token->invalidateRx_(code == Code::CANCELLED || code == Code::DEADLINE_EXCEEDED);
    m_status.code = code;
    if (m_input) m_input->fail(code);
    else m_rxEnd = true;
    if (m_httpSubmitted) this->client->cancelHTTP(this->pool, this->id);
    else m_httpRetired = true;
    invalidateTx_();
    notifyRx_();
    releaseRx_();
  }
  void resultRx_(const Zhttp::Result &result) {
    m_http = result;
    m_httpReady = true;
    if (!liveRx_()) { notifyRx_(); return; }
    if (!result.ok()) {
      Code::T code = httpCode(result);
      // HTTP reset completion can overtake deferred receive notification.
      // Keep the original local failure; liveRx_ already arbitrated expiry.
      if (m_input) {
	if (m_input->failed() && !m_input->httpFailed()) code = m_input->code();
	else if (m_input->meta().failed()) code = m_input->meta().status().code;
      }
      failRx_(code);
    } else if (m_rxEnd && m_input && m_input->failed())
      failRx_(m_input->code());
    notifyRx_();
  }
  void notifyRx_() {
    if (m_notified || !this->terminal || (m_protocolOK && !StreamOut && !m_httpReady)) return;
    m_notified = true;
    try {
      if constexpr (StreamOut) {
	notifyTrailers(m_observer.ptr(), m_trailers, 0);
	m_observer->finished(ZuMv(m_status));
      } else if (m_protocolOK) {
	m_observer->process(Result<Res>{ZuMv(m_status), ZuMv(m_value),
	  ZuMv(m_metadata), ZuMv(m_trailers), m_http});
      } else {
	MetaError::T metadata = MetaError::OK;
	if (m_input) {
	  metadata = m_input->meta().error();
	  // Receive failure can precede HTTP completion; retain a parsed final
	  // status without postponing notification for the transport drain.
	  unsigned status = m_input->meta().httpStatus();
	  if (!m_http.status && status >= 200) m_http.status = status;
	}
	notifyFailed(m_observer.ptr(), Failure{m_status.code, m_http, metadata}, 0);
      }
    } catch (...) { }
    m_observer = nullptr;
  }
  void releaseRx_() {
    if (m_released || !this->registered || !this->terminal || !m_notified ||
	!m_rxEnd || !m_txDone || !m_httpRetired) return;
    m_released = true;
    m_input = nullptr;
    // Drain timer callbacks already queued on Rx before releasing the work.
    this->rx.run([self = ZmRef<Self>{this}]() {
      self->client->calls().release(self.ptr());
    });
  }
  void armRx_() {
    auto left = remaining(this->expires);
    if (left <= 0) { failRx_(Code::DEADLINE_EXCEEDED); return; }
    this->rx.scheduler->add(&m_timer, ZuTime{ZuTime::Nano{this->expires}},
      ZmScheduler::Update, [this](auto &&arm) {
	return arm([this]() { if (!this->terminal) this->armRx_(); });
      }, this->rx.thread);
  }

  ZmRef<Observer> m_observer;
  ZmRef<Token> m_token;

  alignas(Zm::CacheLineSize)
  ZmRef<RxInput> m_input;
  ZmScheduler::Timer m_timer;
  Decoded<Res> m_value;
  Status m_status;
  Metadata m_metadata, m_trailers;
  Zhttp::Result m_http;
  bool m_httpSubmitted = false, m_httpReady = false, m_httpRetired = false;
  bool m_rxEnd = false, m_txDone = false, m_protocolOK = false;
  bool m_notified = false, m_released = false;

  alignas(Zm::CacheLineSize)
  Request *m_request = nullptr;
  Messages m_messages;
  Code::T m_txError = Code::OK;
  bool m_endSubmitted = false, m_framesPosted = false;
  bool m_end = false, m_txInvalid = false, m_draining = false;
};

template <typename Work, typename Req>
struct ClientIngress : ZmObject, ZmNHeap<ClientIngress<Work, Req>> {
friend inline ZuStringT<"Zgrpc.Client.Ingress"> ZmNHeapID(ClientIngress *);
  ZmRef<Work> work;
  Metadata metadata;
  Decoded<Req> initial;
  ClientIngress(ZmRef<Work> work_, Metadata metadata_, Decoded<Req> initial_) :
    work{ZuMv(work_)}, metadata{ZuMv(metadata_)}, initial{ZuMv(initial_)} { }
};

template <typename App, typename M>
auto clientSign(App *app, M *method, ZuBSpan bytes, int)
    -> decltype(app->signMessage(method, bytes), void()) { app->signMessage(method, bytes); }
template <typename App, typename M>
void clientSign(App *, M *, ZuBSpan, long) { }
template <typename App>
auto clientSensitive(const App *app, ZuBSpan name, int)
    -> decltype(bool(app->neverIndex(name))) { return app->neverIndex(name); }
template <typename App> bool clientSensitive(const App *, ZuBSpan, long) { return false; }

template <typename App, typename Catalog>
class Client : public Zhttp::Client<Client<App, Catalog>, Pool<App, Catalog>> {
  using HTTP = Zhttp::Client<Client, Pool<App, Catalog>>;
public:
  ~Client() { final(); }
  bool init(const Zhttp::HubConfig &hub, unsigned pools, ClientConfig config, App *app,
      const Zhttp::TCPConfig &tcp = {}, const Zhttp::H2Config &h2 = {},
      const Zhttp::QUICConfig &quic = {}) {
    if (m_initialized || !app || !valid(config.limits())) return false;
    config.maxRetries(0).maxRedirects(0).bodyMax(uint64_t(-1));
    if (config.retainedBodyMax() > config.limits().maxQueueBytes)
      config.retainedBodyMax(config.limits().maxQueueBytes);
    auto tls = h2;
    if (tls.policy() == Zhttp::H2Policy::Prefer) tls.policy(Zhttp::H2Policy::Force);
    if (!HTTP::init(hub, pools, config, tcp, tls, quic)) return false;
    if (!m_calls.init(hub, config.limits(), EndpointCalls::StartFn{
	[this](EndpointCalls::DoneFn done) {
	  HTTP::start([done = ZuMv(done)](bool ok) mutable { done(ok); });
	}}, EndpointCalls::StopFn{
	[this](EndpointCalls::DoneFn done) {
	  HTTP::stop([done = ZuMv(done)](bool ok) mutable { done(ok); });
	}})) { HTTP::final(); return false; }
    m_app = app;
    m_httpConfig = config;
    m_initialized = true;
    return true;
  }
  bool start() { return m_initialized && m_calls.start(); }
  template <typename Done> void start(Done &&done) {
    if (!m_initialized) { done(false); return; }
    m_calls.start(EndpointCalls::DoneFn{ZuFwd<Done>(done)});
  }

  bool pool(unsigned slot, Zhttp::Destination destination, Zhttp::Config config = {}) {
    config = m_httpConfig.overlay(config);
    config.maxRetries(0).maxRedirects(0).bodyMax(uint64_t(-1));
    if (config.retainedBodyMax() > limits().maxQueueBytes) config.retainedBodyMax(limits().maxQueueBytes);
    return HTTP::pool(slot, ZuMv(destination), config);
  }
  bool up() const { return m_calls.up(); }
  unsigned pending() const { return m_calls.pending(); }
  const Limits &limits() const { return m_calls.limits(); }
  EndpointCalls &calls() { return m_calls; }
  App *app() const { return m_app; }
  bool neverIndex(ZuBSpan name) const { return clientSensitive(m_app, name, 0); }
  template <typename M> void signPayload(M *method, ZuBSpan bytes) { clientSign(m_app, method, bytes, 0); }
  bool stop() { return ZmBlock<bool>{}([this](auto done) { this->stop(ZuMv(done)); }); }
  template <typename Done> void stop(Done &&done) { m_calls.stop(EndpointCalls::DoneFn{ZuFwd<Done>(done)}); }
  void final() {
    if (!m_initialized) return;
    m_calls.final(); HTTP::final(); m_initialized = false; m_app = nullptr;
  }

  template <typename M, typename Observer>
  ZmRef<ClientCall<M>> open(unsigned pool, ZmRef<Observer> observer, CallOptions options = {}) {
    return open_<M>(pool, ZuMv(observer), ZuMv(options), {});
  }
  template <typename M, typename Observer>
  bool call(unsigned pool, Message<typename M::Request> request,
      ZmRef<Observer> observer, CallOptions options = {}) {
    ZuAssert(M::Kind == Kind::Unary);
    return bool(open_<M>(pool, ZuMv(observer), ZuMv(options), Decoded<typename M::Request>{ZuMv(request)}));
  }

  // Internal work/HTTP bridge; endpoint admission remains the public entry.
  bool submit(unsigned pool, ZmRef<typename ClientReqQ<App, Catalog>::Node> request) {
    return HTTP::send(pool, ZuMv(request));
  }
  void cancelHTTP(unsigned pool, uint64_t id) { (void)HTTP::cancel(pool, id); }

private:
  template <typename M, typename Observer>
  ZmRef<ClientCall<M>> open_(unsigned pool, ZmRef<Observer> observer,
      CallOptions options, Decoded<typename M::Request> initial) {
    ZuAssert((ZuTypeIn<M, typename Catalog::List>{}));
    auto expires = expiry(options.timeout);
    if (!observer || options.metadata.failed() || !HTTP::config(pool) || !m_calls.reserve()) return {};
    using Work = ClientWorkT<App, Catalog, M, Observer>;
    auto id = ++m_next;
    if (!id) id = ++m_next;
    ZmRef<Work> work = new Work{this, id, pool, expires, ZuMv(observer)};
    auto token = work->token();
    using Ingress = ClientIngress<Work, typename M::Request>;
    ZmRef<Ingress> ingress = new Ingress{ZuMv(work), ZuMv(options.metadata), ZuMv(initial)};
    m_calls.rx().run([ingress = ZuMv(ingress)]() mutable {
      ingress->work->admit(ZuMv(ingress->metadata), ZuMv(ingress->initial));
    });
    return token;
  }

  EndpointCalls m_calls;
  Zhttp::Config m_httpConfig;
  ZmAtomic<uint64_t> m_next = 0;
  App *m_app = nullptr;
  bool m_initialized = false;
};

} // Zgrpc

#endif /* ZgrpcClient_HH */
