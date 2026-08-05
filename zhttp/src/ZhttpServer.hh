//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP server

#ifndef ZhttpServer_HH
#define ZhttpServer_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZhttpMessage.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZhttpRuntime.hh>

namespace Zhttp {

ZtEnumStruct(ZhttpAPI, RequestDisposition, int8_t,
  Continue, Disconnect);

ZtEnumStruct(ZhttpAPI, RequestPhase, int8_t,
  Receiving, Queued, Sending, Committed, Completing, Done);

template <typename App, typename Profile>
class ProtocolServer :
  public ProfileTraits<Profile>::Transport::template Server<App> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;

public:
  using Base = typename Traits::template Server<App>;
  using Base::init;
  using Base::start;
  enum {
    TLS = Traits::Secure,
    Multiplexed = HTTP::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const HubConfig &hub, const typename Traits::Config &config) {
    return Base::init(Traits::serverParams(hub, config));
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }

  void linkDisconnected_() { }
  void linkDrained_() { Base::linkDisconnected_(); }

  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!impl()->admit(ci)) return nullptr;
    using Link = typename App::Link;
    return new typename Link::Cxn(new Link{impl(), ci}, ci);
  }

  bool admit(const ZiCxnInfo &) { return true; }
  void release() { }
  unsigned idleTimeout() const { return 0; }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

ZuDerive(MessageString, ZtString<ZtStringHeapID<"Zhttp.Message">>);

struct RequestMeta {
  Method::T	method = -1;
  MessageString	target;
  MessageString	pathStorage;
  MessageString	queryStorage;
  MessageString	authority;
  MessageString	protocol;
  ZiIP		remoteIP;
  uint64_t	bodyReceived = 0;
  uint64_t	bodyConsumed = 0;
  uint64_t	bodyReset = 0;
  uint64_t	bodyDiscarded = 0;
  uint32_t	pathOffset = 0;
  uint32_t	pathLength = 0;
  uint32_t	queryOffset = 0;
  uint32_t	queryLength = 0;
  uint16_t	remotePort = 0;
  Scheme::T	scheme = -1;
  TargetForm::T	form = TargetForm::Origin;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  bool		secure = false;
  bool		http10 = false;
  bool		hasQuery = false;
  bool		pathStored = false;
  bool		queryStored = false;

  ZuBSpan path() const {
    if (pathStored) return pathStorage;
    if (!target) return {};
    return
      ZuBSpan{reinterpret_cast<const uint8_t *>(target.data()) + pathOffset,
	pathLength};
  }
  ZuBSpan query() const {
    if (queryStored) return queryStorage;
    if (!target) return {};
    return
      ZuBSpan{reinterpret_cast<const uint8_t *>(target.data()) + queryOffset,
	queryLength};
  }
};

class ServerConfig {
public:
  ServerConfig() { m_tls.policy(H2Policy::Prefer); }

  const ZiIP &localIP() const { return m_localIP; }
  uint16_t port() const { return m_port; }
  unsigned idleTimeout() const { return m_idleTimeout; }
  unsigned maxConnections() const { return m_maxConnections; }
  unsigned maxRequests() const { return m_maxRequests; }
  unsigned altSvcMaxAge() const { return m_altSvcMaxAge; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
  uint64_t retainedBytesMax() const { return m_retainedBytesMax; }
  bool tcpEnabled() const { return m_tcpEnabled; }
  bool tlsEnabled() const { return m_tlsEnabled; }
  bool quicEnabled() const { return m_quicEnabled; }
  const TCPConfig &tcpConfig() const { return m_tcp; }
  const H2Config &tlsConfig() const { return m_tls; }
  const QUICConfig &quicConfig() const { return m_quic; }
  QUICConfig quicHubConfig() const {
    QUICConfig config{m_quic};
    // QUIC transport parameters use milliseconds; server idleTimeout uses
    // seconds.  An explicit QUIC value overrides the transport-neutral default.
    if (!config.maxIdleTimeout() && m_idleTimeout)
      config.maxIdleTimeout(uint64_t(m_idleTimeout) * 1000);
    return config;
  }

  ServerConfig &localIP(ZiIP v) { m_localIP = ZuMv(v); return *this; }
  ServerConfig &port(uint16_t v) { m_port = v; return *this; }
  ServerConfig &idleTimeout(unsigned v) {
    m_idleTimeout = v;
    return *this;
  }
  ServerConfig &maxConnections(unsigned v) {
    m_maxConnections = v;
    return *this;
  }
  ServerConfig &maxRequests(unsigned v) {
    m_maxRequests = v;
    return *this;
  }
  ServerConfig &altSvcMaxAge(unsigned v) {
    m_altSvcMaxAge = v;
    return *this;
  }
  ServerConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ServerConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    return *this;
  }
  ServerConfig &retainedBytesMax(uint64_t v) {
    m_retainedBytesMax = v;
    return *this;
  }
  ServerConfig &tcp(TCPConfig v = {}) {
    m_tcp = ZuMv(v);
    m_tcpEnabled = true;
    return *this;
  }
  ServerConfig &tls(H2Config v) {
    m_tls = ZuMv(v);
    m_tlsEnabled = true;
    return *this;
  }
  ServerConfig &quic(QUICConfig v) {
    m_quic = ZuMv(v);
    m_quicEnabled = true;
    return *this;
  }

private:
  ZiIP		m_localIP;
  TCPConfig	m_tcp;
  H2Config	m_tls;
  QUICConfig	m_quic;
  unsigned	m_idleTimeout = 0;
  unsigned	m_maxConnections = 0;
  unsigned	m_maxRequests = uint32_t(-1);
  unsigned	m_altSvcMaxAge = 86400;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  uint64_t	m_retainedBytesMax = uint64_t(-1);
  uint16_t	m_port = 0;
  bool		m_tcpEnabled = false;
  bool		m_tlsEnabled = false;
  bool		m_quicEnabled = false;
};

// Workload_ is a plain application struct.  Request and each emitted Response
// conform to the application contracts documented in Zhttp.hh.  respond()
// calls emit(response) exactly once.
#if 0
struct Workload {
  using Request = AppRequest;

  Request request();

  template <typename Emit>
  void respond(const RequestMeta &meta, Request &request, Emit &&emit) {
    if (meta.path() == "/health") {
      emit(EmptyResponse{204});
      return;
    }
    if (meta.path() == "/record") {
      emit(JSONResponse{request.record});
      return;
    }
    emit(FileResponse{lookup(meta.path())});
  }
  // EmptyResponse, JSONResponse, and FileResponse are unrelated concrete
  // types which each conform to Response; emit() consumes the selected value.
};
#endif

template <typename Workload_>
class Server : public ZmEngine<Server<Workload_>> {
public:
  using Engine = ZmEngine<Server<Workload_>>;
  using Workload = Workload_;
  using AppRequest = typename Workload::Request;
  using ReqHeaders = typename AppRequest::Headers;
  using Engine::running;
  using Engine::start;
  using Engine::state;
  using Engine::stop;
  using Engine::stopping;
  static constexpr uint64_t ReqBodyMax =
    ParserBodyMax<AppRequest>::V;

private:
  template <typename Protocol> struct Link;
  struct TLSHub;
  struct TLSH1Link;
  struct TLSH2Link;

  template <typename Profile>
  using ProfileLink = ZuIf<ZuIsSame<Profile, H1TLS>{}, TLSH1Link,
    ZuIf<ZuIsSame<Profile, H2TLS>{}, TLSH2Link, Link<Profile>>>;

public:
  template <typename Profile>
  struct LiveReq_ : public ZmObject {
    ZmRef<ProfileLink<Profile>> link;
    RequestMeta		meta;
    AppRequest		request;
    RequestBody		requestBody;
    ResponseResult	response;
    uint64_t		retainedBytes = 0;
    RequestErrorCode::T	errorCode = -1;
    RequestPhase::T	phase = RequestPhase::Receiving;
    bool		close = false;
    bool		completionPosted = false;
  };
  template <typename Profile>
  using LiveReqQ = ZmList<LiveReq_<Profile>,
    ZmListNode<LiveReq_<Profile>,
      ZmListHeapID<"Zhttp.Server.LiveReq">>>;
  template <typename Profile>
  using LiveReq = typename LiveReqQ<Profile>::Node;

  template <typename Profile>
  struct H1Ready_ : public ZmObject {
    H1Ready_(ProfileLink<Profile> *link_) : link{link_} { }
    ProfileLink<Profile> *link = nullptr;
  };
  template <typename Profile>
  using H1ReadyQ = ZmList<H1Ready_<Profile>,
    ZmListNode<H1Ready_<Profile>,
      ZmListHeapID<"Zhttp.Server.H1Ready">>>;
  template <typename Profile>
  using H1Ready = typename H1ReadyQ<Profile>::Node;

private:
  template <typename Protocol> struct Session;
  template <typename Protocol> struct Hub;
  template <typename Profile> struct Parser;

  template <typename Profile>
  struct ResponseQueue {
    LiveReqQ<Profile> fifo;
    H1ReadyQ<Profile> ready;
    bool posted = false;
  };

  template <typename Profile>
  struct H1ResponseState {
    LiveReqQ<Profile> responses;
    ZmRef<H1Ready<Profile>> readyNode;
    LiveReq<Profile> *activeResponse = nullptr;
    bool responseReady = false;
  };
  struct NoResponseState { };
  template <typename Profile>
  using LinkResponseState = ZuIf<
    MessageTraits<Profile>::ID == Version::H1,
    H1ResponseState<Profile>, NoResponseState>;

  using BodyChunkFn = ZmFn<void(ZmRef<ZiIOBuf>, bool),
    ZmFnHeapID<"Zhttp.Server.BodyChunk">>;
  using BodyCancelFn = ZmFn<void(),
    ZmFnHeapID<"Zhttp.Server.BodyCancel">>;
  struct BodyTask {
    BodyCancelFn cancel;
  };
  using BodyTaskQ = ZmList<BodyTask,
    ZmListNode<BodyTask, ZmListHeapID<"Zhttp.Server.BodyTask">>>;

  struct Stats {
    ZmAtomic<unsigned> activeConnections = 0;
    ZmAtomic<unsigned> activeRequests = 0;
    ZmAtomic<unsigned> queuedResponses = 0;
    ZmAtomic<uint64_t> retainedBytes = 0;
    ZmAtomic<uint64_t> serverFaults = 0;
    ZmAtomic<uint64_t> rejectedRequests = 0;
    ZmAtomic<uint64_t> parseFailures = 0;
    ZmAtomic<uint64_t> responseBuildFailures = 0;
    ZmAtomic<uint64_t> transportFailures = 0;
  };

  template <typename App, typename = void>
  struct HasAsyncBody : public ZuFalse { };
  template <typename App>
  struct HasAsyncBody<App, decltype(
    ZuDeclVal<App &>().next(unsigned{}, ZuDeclVal<BodyChunkFn>()), void())> :
      public ZuTrue { };

  template <typename Profile>
  ResponseQueue<Profile> &responseQueue_() {
    if constexpr (ZuIsSame<Profile, H1TCP>{}) return m_h1TCPResponses;
    else if constexpr (ZuIsSame<Profile, H1TLS>{}) return m_h1TLSResponses;
    else if constexpr (ZuIsSame<Profile, H2TLS>{}) return m_h2TLSResponses;
    else return m_h3QUICResponses;
  }

  template <typename Profile>
  struct Parser :
    public MessageTraits<Profile>::template RequestParser<
      Parser<Profile>, ReqHeaders, ReqBodyMax> {
    using Base = typename MessageTraits<Profile>::template RequestParser<
      Parser, ReqHeaders, ReqBodyMax>;
    using State = typename Base::State;

    void reset() { Base::reset(); live = nullptr; }
    void operation(Method::T method, const RequestTarget &target) {
      auto &request = live->meta;
      request.method = method;
      request.target = ZuCSpan{target.raw};
      request.authority = ZuCSpan{target.authority.raw};
      request.protocol = ZuCSpan{target.protocol};
      request.scheme = target.scheme;
      request.form = target.form;
      request.hasQuery = target.hasQuery;
      auto offsets = [&target](ZuBSpan value, uint32_t &offset,
	  uint32_t &length) {
	if (!target.raw || !value) return false;
	uintptr_t raw = reinterpret_cast<uintptr_t>(target.raw.data());
	uintptr_t data = reinterpret_cast<uintptr_t>(value.data());
	if (data < raw || data - raw > target.raw.length() ||
	    value.length() > target.raw.length() - (data - raw))
	  return false;
	offset = uint32_t(data - raw);
	length = uint32_t(value.length());
	return true;
      };
      request.pathStored = !offsets(
	target.path, request.pathOffset, request.pathLength);
      if (request.pathStored) request.pathStorage = ZuCSpan{target.path};
      request.queryStored = !offsets(
	target.query, request.queryOffset, request.queryLength);
      if (request.queryStored) request.queryStorage = ZuCSpan{target.query};
      live->request.operation(method, target);
    }
    void version(ZuBSpan version_) {
      auto &request = live->meta;
      request.http10 = ZuCSpan{version_} == "HTTP/1.0";
      live->request.version(version_);
    }
    template <typename Key>
    void header(ZuBSpan value) {
      live->request.template header<Key>(value);
    }
    void contentLength(uint64_t value) { live->request.contentLength(value); }
    void chunked() { live->request.chunked(); }
    void status(unsigned) { }
    template <typename Rx>
    void body(Rx &rx) {
      auto &request = live->meta;
      auto &requestBody = live->requestBody;
      uint64_t before = rx.length();
      if (before < requestBody.pending) {
	return;
      }
      requestBody.received += before - requestBody.pending;
	live->request.body(rx);
      uint64_t pending = rx.length();
      if (pending > before) return;
      requestBody.consumed += before - pending;
      requestBody.pending = pending;
      request.bodyReceived = requestBody.received;
      request.bodyConsumed = requestBody.consumed;
      request.bodyReset = requestBody.reset;
      request.bodyDiscarded = requestBody.discarded;
    }
    void complete(typename State::T state) {
      auto &request = live->meta;
      auto &requestBody = live->requestBody;
      requestBody.reset += requestBody.pending;
      requestBody.discarded += requestBody.pending;
      requestBody.pending = 0;
      request.bodyReceived = requestBody.received;
      request.bodyConsumed = requestBody.consumed;
      request.bodyReset = requestBody.reset;
      request.bodyDiscarded = requestBody.discarded;
      live->request.complete(state == State::Complete);
    }

    Server			*server = nullptr;
    ZmRef<LiveReq<Profile>>	live;
  };

  struct ErrorResponse {
    using BodyPolicy = Body::None;
    using Headers = ZuTypeList<>;

    unsigned status_ = 400;

    void reset() { }
    unsigned status() const { return status_; }
    template <typename L> void reason(L &&l) const { l(""); }
    template <typename Key, typename L> void header(L &&) const { }
    template <typename L> void header(L &&) const { }
    bool close() const { return false; }
  };

  template <typename Profile, typename AppBuilder>
  struct ResponseOps {
    using Headers = typename AppBuilder::Headers;

    Server	*server = nullptr;
    AppBuilder	app;
    HeaderPatches<Headers> patches;
    uint64_t	produced = 0;

    unsigned status() const { return app.status(); }
    template <typename L>
    void reason(L &&l) const { app.reason(ZuFwd<L>(l)); }
    template <typename Key, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (rejectContentLength) return;
      if (suppressPads) {
	unsigned count = 0;
	app.template header<Key>([this, &l, &count]<typename V>(V &&v) {
	  if (++count > 1) {
	    patches.invalidate();
	    return;
	  }
	  if constexpr (!IsHeaderPad<ZuDecay<V>>{})
	    l(ZuFwd<V>(v));
	});
	return;
      }
      patches.template header<
	MessageTraits<Profile>::ID == Version::H1, Key>(
	app, ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      app.header([this, &l]<typename K, typename V>(K &&k, V &&v) {
	ZtString<ZtStringHeapID<"Zhttp.RuntimeHeader.Name">> name;
	name << k;
	if (!validRuntimeHeader<Headers>(ZuCSpan{name}, h1)) {
	  headersOK = false;
	  return;
	}
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
      if (server->m_altSvc) l("alt-svc", server->m_altSvc);
    }
    bool provision() { return patches.provision(app); }
    bool patch(uint64_t n) {
      bool ok = true;
      app.bodyHdrs(
	[this, &ok]<typename Key, typename Patcher>(Patcher &&patcher) {
	  if (!patches.template patch<Key>(ZuFwd<Patcher>(patcher)))
	    ok = false;
	});
      return ok && patches.validate(n);
    }
    uint64_t contentLength() const { return produced; }
    bool headersValid() const { return headersOK && patches.valid(); }
    AppBuilder &appBuilder() { return app; }
    template <typename Emit>
    void emitBody(Emit &&emit) { app.body(ZuFwd<Emit>(emit)); }

    bool	h1 = false;
    bool	headersOK = true;
    bool	suppressPads = false;
    bool	rejectContentLength = false;
  };

  template <
    typename Profile, typename AppBuilder,
    bool HasBody, bool Streaming>
  struct ResponseTx :
    public MessageTraits<Profile>::template Response<
      ResponseTx<Profile, AppBuilder, HasBody, Streaming>,
      typename AppBuilder::Headers,
      typename BuilderTrailers<AppBuilder>::T, HasBody, Streaming>,
    public ResponseOps<Profile, AppBuilder> {
    using Base = typename MessageTraits<Profile>::template Response<
      ResponseTx, typename AppBuilder::Headers,
      typename BuilderTrailers<AppBuilder>::T, HasBody, Streaming>;
    using Ops = ResponseOps<Profile, AppBuilder>;
    enum { Optional = AppBuilder::BodyPolicy::Optional };

    ResponseTx(
      Server *server, AppBuilder app, bool suppressPads = false,
      bool rejectContentLength = false) :
      Ops{server, ZuMv(app)} {
      this->h1 = MessageTraits<Profile>::ID == Version::H1;
      this->suppressPads = suppressPads;
      this->rejectContentLength = rejectContentLength;
    }

    bool streamResponse() const { return false; }
    using Ops::contentLength;
    using Ops::emitBody;
    using Ops::header;
    using Ops::reason;
    using Ops::status;
  };

  template <typename Profile, typename App, typename Heap>
  struct AsyncBody_ : public Heap, public ZmObject {
    using Self = AsyncBody_<Profile, App, Heap>;
    using Tx = ResponseTx<Profile, App, true, true>;

    AsyncBody_(Server *server_, ZmRef<LiveReq<Profile>> live_, App app) :
      server{server_}, live{ZuMv(live_)}, builder{server_, ZuMv(app)} { }

    bool start(ZmRef<Self> self) {
      auto tx = live->link->transmit(builder);
      builder.begin(tx);
      if (!builder.headersValid()) return false;
      task = server->addBodyTask_(BodyCancelFn{
	[self = ZuMv(self)]() mutable { self->cancel_(); }});
      next_();
      return true;
    }

    void next_() {
      if (cancelled) return;
      uint64_t available = server->retainedAvailable_();
      if (available > server->m_config.retainedMessageMax())
	available = server->m_config.retainedMessageMax();
      if (available > server->m_config.retainedBodyMax())
	available = server->m_config.retainedBodyMax();
      if (available > ZiIOBuf_DefltSize) available = ZiIOBuf_DefltSize;
      if (available > uint32_t(-1)) available = uint32_t(-1);
      if (!available) { fail_(ResponseOutcome::BuildFailed); return; }
      reserved = available;
      server->m_stats.retainedBytes += reserved;
      live->retainedBytes += reserved;
      ++server->m_bodyPending;
      builder.app.next(unsigned(available), BodyChunkFn{
	[self = ZmRef<Self>{this}](ZmRef<ZiIOBuf> buf, bool final) mutable {
	  self->chunk_(ZuMv(buf), final);
	}});
    }

    void chunk_(ZmRef<ZiIOBuf> buf, bool final) {
      server->txRun_([
	self = ZmRef<Self>{this}, buf = ZuMv(buf), final]() mutable {
	self->chunkTx_(ZuMv(buf), final);
      });
    }

    void chunkTx_(ZmRef<ZiIOBuf> buf, bool final) {
      server->bodyResolved_();
      if (cancelled) return;
      if (!live->link->active()) {
	fail_(ResponseOutcome::Reset);
	return;
      }
      if (!buf || !buf->length || buf->length > reserved) {
	fail_(ResponseOutcome::BuildFailed);
	return;
      }
      uint64_t unused = reserved - buf->length;
      reserved = 0;
      if (unused) {
	server->m_stats.retainedBytes -= unused;
	live->retainedBytes -= unused;
      }
      auto tx = live->link->transmit(builder);
      auto body = builder.body(tx);
      body << ZuBSpan{buf->data(), buf->length};
      body.flush();
      builder.produced += buf->length;
      if (!body.valid()) { fail_(ResponseOutcome::TxFailed); return; }
      if (final) {
	builder.finish(tx);
	committed_(live->response.body, builder.produced);
	live->response.outcome = ResponseOutcome::Success;
	live->phase = RequestPhase::Committed;
	finishTask_();
	live->link->finish();
	server->template postCommitted_<Profile>(live);
	return;
      }
      if (!live->link->txFence(Transport_::TxCompleteFn{
	  [self = ZmRef<Self>{this}](ResponseOutcome::T outcome) mutable {
	    self->fenced_(outcome);
	  }}))
	fail_(ResponseOutcome::TxFailed);
    }

    void fenced_(ResponseOutcome::T outcome) {
      server->txRun_([self = ZmRef<Self>{this}, outcome]() mutable {
	if (self->cancelled) return;
	self->release_();
	if (outcome == ResponseOutcome::Success) self->next_();
	else self->fail_(outcome);
      });
    }

    void release_() {
      if (!live->retainedBytes) return;
      ZmAssert(
	server->m_stats.retainedBytes.load_() >= live->retainedBytes);
      server->m_stats.retainedBytes -= live->retainedBytes;
      live->retainedBytes = 0;
    }

    void fail_(ResponseOutcome::T outcome) {
      if (cancelled) return;
      cancelled = true;
      finishTask_();
      live->link->txCancel();
      live->response.outcome = outcome;
      live->phase = RequestPhase::Completing;
      server->template postCompleted_<Profile>(ZuMv(live), outcome);
    }

    void cancel_() {
      task = nullptr;
      fail_(ResponseOutcome::Cancelled);
    }

    void finishTask_() {
      if (!task) return;
      server->delBodyTask_(task);
      task = nullptr;
    }

    Server			*server;
    ZmRef<LiveReq<Profile>>	live;
    Tx				builder;
    typename BodyTaskQ::Node	*task = nullptr;
    uint64_t			reserved = 0;
    bool			cancelled = false;
  };

  template <typename Profile, typename App>
  using AsyncBody = AsyncBody_<Profile, App,
    ZmHeap<"Zhttp.Server.AsyncBody", AsyncBody_<Profile, App, ZuVoid>>>;

  static bool bodyAllowed_(Method::T method, unsigned status) {
    if (method == Method::HEAD || (status >= 100 && status < 200) ||
	status == 204 || status == 304)
      return false;
    return method != Method::CONNECT || status < 200 || status >= 300;
  }

  static bool contentLengthForbidden_(Method::T method, unsigned status) {
    return (status >= 100 && status < 200) || status == 204 ||
      (method == Method::CONNECT && status >= 200 && status < 300);
  }

  static void committed_(BodyCommit &commit, uint64_t produced = 0) {
    commit.produced = produced;
    commit.committed = produced;
    commit.headers = true;
    commit.final = true;
  }

  typename BodyTaskQ::Node *addBodyTask_(BodyCancelFn fn) {
    return m_bodyTasks.push(BodyTask{ZuMv(fn)});
  }

  void delBodyTask_(typename BodyTaskQ::Node *task) {
    auto node = m_bodyTasks.delNode(task);
    if (node) node->cancel = {};
  }

  void cancelBodies_() {
    while (auto task = m_bodyTasks.shift()) {
      auto fn = ZuMv(task->cancel);
      task->cancel = {};
      if (fn) fn();
    }
  }

  void bodyResolved_() {
    ZmAssert(m_bodyPending);
    if (--m_bodyPending || !Engine::stopping()) return;
    stopTransports_();
  }

  template <typename Profile>
  void postCommitted_(ZmRef<LiveReq<Profile>> live) {
    auto hub = live->link->app();
    hub->rxRun([this, live = ZuMv(live)]() mutable {
      m_workload->committed(
	live->meta, live->request, live->response.body);
    });
  }

  template <typename Profile, typename App>
  bool startAsyncBody_(ZmRef<LiveReq<Profile>> live, App app) {
    using State = AsyncBody<Profile, App>;
    ZmRef<State> state = new State{this, ZuMv(live), ZuMv(app)};
    return state->start(state);
  }

  template <typename Profile, typename Link_, typename AppBuilder>
  struct ServerTxOps {
    Server	*server;
    Link_	*link_;
    BodyCommit	*commit;
    uint64_t	*retainedBytes;

    Link_ &link() { return *link_; }
    uint64_t fixedBodyMax() const { return server->fixedBodyMax_(); }
    uint64_t retainedMax() const {
      uint64_t n = server->retainedAvailable_();
      if (n > server->m_config.retainedMessageMax())
	n = server->m_config.retainedMessageMax();
      return n;
    }
    void headers() { }
    template <bool> void produced(uint64_t) { }
    bool empty(AppBuilder &app) {
      ResponseTx<Profile, AppBuilder, false, false> empty{
	server, ZuMv(app), true};
      auto tx = link_->transmit(empty);
      empty.begin(tx);
      if (!empty.headersValid()) return false;
      empty.finish(tx);
      committed_(*commit);
      link_->finish();
      return true;
    }
    template <bool> bool fail() { return false; }
    bool complete(uint64_t n) {
      if (!server->retainResponse_(*retainedBytes, n)) return false;
      committed_(*commit, n);
      return true;
    }
  };

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendResponse_(
      Link_ &link, Method::T method, AppBuilder &&app_, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using App = ZuDecay<AppBuilder>;
    using Policy = typename App::BodyPolicy;
    unsigned status = app_.status();
    if constexpr (!Policy::HasBody) {
      ResponseTx<Profile, App, false, false> builder{
	this, ZuFwd<AppBuilder>(app_), true,
	contentLengthForbidden_(method, status)};
      auto tx = link.transmit(builder);
      builder.begin(tx);
      if (!builder.headersValid()) return false;
      builder.finish(tx);
      committed_(commit);
      link.finish();
      return true;
    } else {
      if (!bodyAllowed_(method, status)) {
	ResponseTx<Profile, App, false, false> builder{
	  this, ZuFwd<AppBuilder>(app_), true,
	  contentLengthForbidden_(method, status)};
	auto tx = link.transmit(builder);
	builder.begin(tx);
	if (!builder.headersValid()) return false;
	builder.finish(tx);
	committed_(commit);
	link.finish();
	return true;
      }
      if constexpr (Policy::Streaming)
	return sendStreamingResponse_<Profile>(
	  link, ZuFwd<AppBuilder>(app_), commit, retainedBytes);
      else
	return sendFixedResponse_<Profile>(
	  link, ZuFwd<AppBuilder>(app_), commit, retainedBytes);
    }
  }

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendStreamingResponse_(
      Link_ &link, AppBuilder &&app_, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using App = ZuDecay<AppBuilder>;
    ResponseTx<Profile, App, true, true> builder{
      this, ZuFwd<AppBuilder>(app_)};
    ServerTxOps<Profile, Link_, App> ops{
      this, &link, &commit, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.streaming(builder);
  }

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendFixedResponse_(
      Link_ &link, AppBuilder &&app_, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using App = ZuDecay<AppBuilder>;
    ResponseTx<Profile, App, true, false> builder{
      this, ZuFwd<AppBuilder>(app_)};
    ServerTxOps<Profile, Link_, App> ops{
      this, &link, &commit, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.fixed(builder);
  }

  enum { ResponseDrainBatch = 16 };

  template <typename Profile>
  void completeLiveRx_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome,
      bool disconnect = true) {
    if (live->phase == RequestPhase::Done) return;
    live->response.outcome = outcome;
    switch (outcome) {
      case ResponseOutcome::BuildFailed:
	++m_stats.responseBuildFailures;
	break;
      case ResponseOutcome::TxFailed:
      case ResponseOutcome::Reset:
	++m_stats.transportFailures;
	break;
      default:
	break;
    }
    live->phase = RequestPhase::Completing;
    m_workload->completed(live->meta, live->request, live->response);
    live->phase = RequestPhase::Done;
    if (disconnect &&
	(live->close || live->response.outcome != ResponseOutcome::Success))
      live->link->disconnect();
    --m_stats.activeRequests;
  }

  template <typename Profile>
  void postCompletedTx_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome) {
    if (live->completionPosted) return;
    live->completionPosted = true;
    if (live->retainedBytes) {
      ZmAssert(m_stats.retainedBytes.load_() >= live->retainedBytes);
      m_stats.retainedBytes -= live->retainedBytes;
      live->retainedBytes = 0;
    }
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      if (live->link->activeResponse == live.ptr()) {
	live->link->activeResponse = nullptr;
	readyH1_<Profile>(live->link.ptr());
	scheduleResponses_<Profile>(live->link->app());
      }
    }
    auto hub = live->link->app();
    hub->rxRun([
      this, live = ZuMv(live), outcome]() mutable {
      completeLiveRx_<Profile>(ZuMv(live), outcome);
    });
  }

  template <typename Profile>
  void postCompleted_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome) {
    if (m_mx->invoked(m_txThread)) {
      postCompletedTx_<Profile>(ZuMv(live), outcome);
      return;
    }
    txRun_([this, live = ZuMv(live), outcome]() mutable {
      postCompletedTx_<Profile>(ZuMv(live), outcome);
    });
  }

  uint64_t retainedAvailable_() const {
    uint64_t max = m_config.retainedBytesMax();
    uint64_t used = m_stats.retainedBytes.load_();
    return used < max ? max - used : 0;
  }

  bool retainResponse_(uint64_t &retainedBytes, uint64_t n) {
    if (n > retainedAvailable_()) return false;
    m_stats.retainedBytes += n;
    retainedBytes = n;
    return true;
  }

  template <typename Profile>
  void sendLiveTx_(ZmRef<LiveReq<Profile>> live) {
    auto link = live->link;
    if (!link->active()) {
	live->response.outcome = ResponseOutcome::Reset;
	live->phase = RequestPhase::Completing;
	postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Reset);
	return;
    }
    auto emit = [this, &live, &link]<typename AppResponse>(
	AppResponse &&response) {
	response.reset();
	if (live->phase != RequestPhase::Queued) {
	  live->response.outcome = ResponseOutcome::BuildFailed;
	  live->phase = RequestPhase::Completing;
	  live->close = true;
	  return;
	}
	live->phase = RequestPhase::Sending;
	live->close = live->close || response.close();
	link->txComplete(Transport_::TxCompleteFn{
	  [this, live_ = live](ResponseOutcome::T outcome) mutable {
	    postCompleted_<Profile>(ZuMv(live_), outcome);
	  }});
	using App = ZuDecay<AppResponse>;
	using Policy = typename App::BodyPolicy;
	if constexpr (Policy::Streaming && HasAsyncBody<App>{}) {
	  if (!startAsyncBody_<Profile>(
		live, App{ZuFwd<AppResponse>(response)})) {
	    link->txCancel();
	    live->response.outcome = ResponseOutcome::BuildFailed;
	    live->phase = RequestPhase::Completing;
	  }
	} else {
	  bool sent = sendResponse_<Profile>(
	    *link, live->meta.method, ZuFwd<AppResponse>(response),
	    live->response.body, live->retainedBytes);
	  if (sent) {
	    live->response.outcome = ResponseOutcome::Success;
	    live->phase = RequestPhase::Committed;
	  } else {
	    link->txCancel();
	    live->response.outcome = ResponseOutcome::BuildFailed;
	    live->phase = RequestPhase::Completing;
	  }
	}
      };
    if (live->errorCode >= 0)
      emit(ErrorResponse{requestErrorStatus(live->errorCode)});
    else
      m_workload->respond(live->meta, live->request, emit);
    if (live->phase == RequestPhase::Committed) {
      postCommitted_<Profile>(live);
      return;
    }
    if (live->phase == RequestPhase::Sending) return;
    if (live->phase == RequestPhase::Queued) {
      live->response.outcome = ResponseOutcome::BuildFailed;
      live->phase = RequestPhase::Completing;
    }
    if (!live->response.body.final)
      postCompleted_<Profile>(ZuMv(live), ResponseOutcome::BuildFailed);
  }

  template <typename Profile>
  void drainResponses_() {
    auto &queue = responseQueue_<Profile>();
    unsigned sent = 0;
    while (sent < ResponseDrainBatch) {
      ZmRef<LiveReq<Profile>> live;
      if constexpr (MessageTraits<Profile>::ID == Version::H1) {
	auto ready = queue.ready.shift();
	if (!ready) break;
	auto link = ready->link;
	ZmAssert(link && link->responseReady && !link->activeResponse);
	link->responseReady = false;
	live = link->responses.shift();
	ZmAssert(live);
	link->activeResponse = live.ptr();
      } else {
	live = queue.fifo.shift();
	if (!live) break;
      }
      --m_stats.queuedResponses;
      sendLiveTx_<Profile>(ZuMv(live));
      ++sent;
    }
    bool pending;
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      pending = !queue.ready.empty_();
    else
      pending = !queue.fifo.empty_();
    if (pending) {
      txRun_([this]() { drainResponses_<Profile>(); });
    } else {
      queue.posted = false;
    }
  }

  template <typename Profile, typename Hub_>
  void scheduleResponses_(Hub_ *hub) {
    auto &queue = responseQueue_<Profile>();
    bool pending;
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      pending = !queue.ready.empty_();
    else
      pending = !queue.fifo.empty_();
    if (queue.posted || !pending) return;
    queue.posted = true;
    hub->txRun([this]() { drainResponses_<Profile>(); });
  }

  template <typename Profile>
  void readyH1_(ProfileLink<Profile> *link) {
    auto &queue = responseQueue_<Profile>();
    if (link->activeResponse || link->responseReady ||
	link->responses.empty_()) return;
    if (!link->readyNode)
      link->readyNode = new H1Ready<Profile>{link};
    queue.ready.pushNode(link->readyNode);
    link->responseReady = true;
  }

  template <typename Profile>
  void enqueueResponse_(ZmRef<LiveReq<Profile>> live) {
    auto &queue = responseQueue_<Profile>();
    auto hub = live->link->app();
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      auto link = live->link;
      link->responses.pushNode(ZuMv(live));
      readyH1_<Profile>(link);
    } else {
      queue.fifo.pushNode(ZuMv(live));
    }
    scheduleResponses_<Profile>(hub);
  }

  template <typename Profile>
  void detachH1_(ProfileLink<Profile> *link) {
    if (!link->responseReady) return;
    auto &ready = responseQueue_<Profile>().ready;
    if (ready.headPtr() == link->readyNode.ptr())
      ready.shift();
    else
      ready.delNode(link->readyNode.ptr());
    link->responseReady = false;
  }

  template <typename Profile>
  void cancelH1Tx_(ProfileLink<Profile> *link) {
    detachH1_<Profile>(link);
    while (auto live = link->responses.shift()) {
      --m_stats.queuedResponses;
      postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Cancelled);
    }
  }

  template <typename Profile, typename Link_>
  void disconnectedH1_(Link_ *link) {
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      txRun_([this, link = ZmMkRef(link)]() {
	cancelH1Tx_<Profile>(link);
      });
  }

  template <typename Profile>
  void cancelResponses_() {
    auto &queue = responseQueue_<Profile>();
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      while (auto ready = queue.ready.shift()) {
	auto link = ready->link;
	link->responseReady = false;
	cancelH1Tx_<Profile>(link);
      }
    } else {
      while (auto live = queue.fifo.shift()) {
	--m_stats.queuedResponses;
	postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Cancelled);
      }
    }
    queue.posted = false;
  }

  void cancelResponses_() {
    cancelResponses_<H1TCP>();
    cancelResponses_<H1TLS>();
    cancelResponses_<H2TLS>();
    cancelResponses_<H3QUIC>();
  }

  template <typename Profile>
  struct Session :
    public ServerSession<
      Session<Profile>, Parser<Profile>, MessageTraits<Profile>> {
    using Message = MessageTraits<Profile>;
    using Parser_ = Parser<Profile>;
    using Base = ServerSession<Session, Parser_, Message>;
    using Base::parser;

    template <typename Link_, typename Rx>
    int process(Link_ &link, Rx &rx) {
      if (terminal) return 0;
      if (!parser.server) {
	parser.server = link.app()->server;
      }
      if (!parser.live) {
	if constexpr (Message::ID == Version::H1)
	  if (!rx.length()) return 0;
	if (!parser.server->admitRequest_()) return -1;
	parser.live = new LiveReq<Profile>;
	parser.live->link = ZmMkRef(&link);
	parser.live->request = parser.server->m_workload->request();
      }
      auto &request = parser.live->meta;
      if (!request.remoteIP) {
	request.remoteIP = link.remoteIP();
	request.remotePort = link.remotePort();
	request.transport = Message::Transport::ID;
	request.httpVersion = Message::ID;
	request.secure = Message::Transport::Secure;
      }
      return Base::process(link, rx);
    }

    template <typename Link_>
    void disconnected(Link_ &link, bool) {
      auto server = parser.server ? parser.server : link.app()->server;
      server->template disconnectedH1_<Profile>(&link);
      if (parser.live) {
	auto live = ZuMv(parser.live);
	auto &body = live->requestBody;
	body.reset += body.pending;
	body.discarded += body.pending;
	body.pending = 0;
	live->meta.bodyReceived = body.received;
	live->meta.bodyConsumed = body.consumed;
	live->meta.bodyReset = body.reset;
	live->meta.bodyDiscarded = body.discarded;
	live->request.complete(false);
	live->phase = RequestPhase::Completing;
	server->template completeLiveRx_<Profile>(
	  ZuMv(live), ResponseOutcome::Reset, false);
      }
    }

    template <typename Link_>
    int error(Link_ &link, Parser_ &parser) {
      auto server = link.app()->server;
      const RequestError &error = parser.error();
      if (!parser.live) return -1;
      ++server->m_stats.parseFailures;
      auto live = ZuMv(parser.live);
      if (!error.responsePossible ||
	  (error.scope != RequestErrorScope::Request &&
	   Message::ID != Version::H1)) {
	terminal = true;
	server->txRun_([server, live = ZuMv(live)]() mutable {
	  server->template postCompleted_<Profile>(
	    ZuMv(live), ResponseOutcome::Reset);
	});
	return -1;
      }
      RequestDisposition::T disposition = RequestDisposition::Disconnect;
      if (error.scope == RequestErrorScope::Request)
	disposition = server->m_workload->requestError(
	  live->meta, live->request, error);
      live->close = disposition == RequestDisposition::Disconnect;
      if (live->close || Message::OneMessagePerLink) terminal = true;
      live->errorCode = error.code;
      live->phase = RequestPhase::Queued;
      ++server->m_stats.queuedResponses;
      link.app()->txRun([server, live = ZuMv(live)]() mutable {
	server->template enqueueResponse_<Profile>(ZuMv(live));
      });
      if constexpr (Message::ID == Version::H1) {
	if (disposition == RequestDisposition::Continue) parser.reset();
      }
      return 1;
    }

    template <typename Link_>
    int request(Link_ &link, Parser_ &parser) {
      auto server = link.app()->server;
      ZmAssert(parser.live);
      auto live = ZuMv(parser.live);
      live->phase = RequestPhase::Queued;
      ++server->m_stats.queuedResponses;
      link.app()->txRun([
	server, live = ZuMv(live)]() mutable {
	server->template enqueueResponse_<Profile>(ZuMv(live));
      });
      return 1;
    }

    bool terminal = false;
  };

  template <typename Profile>
  struct Hub : public ProtocolServer<Hub<Profile>, Profile> {
    using HTTP = ProfileTraits<Profile>;
    using Link_ = typename Server::template Link<Profile>;
    using Link = Link_;
    Server *server = nullptr;

    Hub(Server *server_) : server{server_} { }
    ZiIP localIP() const { return server->m_config.localIP(); }
    unsigned localPort() const { return server->m_config.port(); }
    unsigned idleTimeout() const { return server->m_config.idleTimeout(); }
    template <typename Info>
    bool admit(const Info &) { return server->admit(); }
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_workload->connected(
	HTTP::Transport::ID);
    }
    void release() {
      server->release(HTTP::Transport::ID);
    }
    template <typename Info>
    void listening(const Info &info) {
      server->m_workload->listening(
	HTTP::Transport::ID, info.port);
    }
    void listening() {
      server->m_workload->listening(
	HTTP::Transport::ID, server->m_config.port());
    }
    void listenFailed(bool transient) {
      server->m_workload->listenFailed(
	HTTP::Transport::ID, transient);
      server->failServer_();
    }
  };

  template <typename Profile>
  struct Link :
    public ServerLink<
      Hub<Profile>, Link<Profile>, Profile, Session<Profile>>,
    public LinkResponseState<Profile> {
    using Base = ServerLink<
      Hub<Profile>, Link, Profile, Session<Profile>>;
    using Base::Base;
  };

  struct TLSHub : public TLS_::ServerHub<TLSHub> {
    using H1Link = TLSH1Link;
    using H2Link = TLSH2Link;
    Server *server = nullptr;

    TLSHub(Server *server_) : server{server_} { }
    ZiIP localIP() const { return server->m_config.localIP(); }
    unsigned localPort() const { return server->m_config.port(); }
    unsigned idleTimeout() const {
      return server->m_config.idleTimeout();
    }
    bool admit(const ZiCxnInfo &) { return server->admit(); }
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_workload->connected(Transport::TLS);
    }
    template <typename Link_>
    void disconnected(Link_ &, bool) { }
    void release() { server->release(Transport::TLS); }
    void listening(const ZiListenInfo &info) {
      server->m_workload->listening(Transport::TLS, info.port);
    }
    void listenFailed(bool transient) {
      server->m_workload->listenFailed(Transport::TLS, transient);
      server->failServer_();
    }
  };

  struct TLSH1Link :
    public TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>,
    public H1ResponseState<H1TLS> {
    using Base = TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
  };

  struct TLSH2Link :
    public H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>> {
    using Base = H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
  };

public:
  Server() : m_tcp{this}, m_tls{this}, m_quic{this} { }

  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  // Request metadata callbacks precede request-body input.  Body input is a
  // concrete bounded stream on the Rx shard and is valid only for the
  // synchronous workload callback.  Message completion follows validated,
  // fully consumed input; no workload callback follows terminal completion.
  bool init(
    const HubConfig &hub, ServerConfig config, Workload *workload) {
    return Engine::lock(ZmEngineState::Stopped, [&]() {
      return init_(hub, ZuMv(config), workload);
    });
  }

private:
  friend Engine;

  bool init_(
    const HubConfig &hub, ServerConfig config, Workload *workload) {
    if (!workload || !config.port()) return false;
    if (!hub.mx()) return false;
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = ZuMv(config);
    m_workload = workload;
    m_failed = false;
    m_altSvc.null();
    if (m_config.tlsEnabled() && m_config.quicEnabled() &&
	m_config.altSvcMaxAge())
      m_altSvc << "h3=\":" << m_config.port() << "\"; ma=" <<
	m_config.altSvcMaxAge();
    if (!m_runtime.init()) return false;
    if (m_config.tcpEnabled() &&
	!m_hubs.init(m_tcp, hub, m_config.tcpConfig()))
      return false;
    if (m_config.tlsEnabled() &&
	!m_hubs.init(m_tls, hub, m_config.tlsConfig()))
      return false;
    if (m_config.quicEnabled() &&
	!m_hubs.init(m_quic, hub, m_config.quicHubConfig()))
      return false;
    return m_hubs.count();
  }

  void start_() {
    m_admitRequests = true;
    m_hubs.start([this](bool ok) {
      if (!ok) m_admitRequests = false;
      Engine::started(ok);
    });
  }
  void stop_() {
    m_admitRequests = false;
    // Order every pre-stop Rx admission/enqueue post before Tx cancellation.
    rxRun_([this]() {
      txRun_([this]() {
	cancelBodies_();
	if (!m_bodyPending) stopTransports_();
      });
    });
  }

  void stopTransports_() {
    cancelResponses_();
    m_hubs.stop([this](bool ok) {
      // Native hub teardown can enqueue terminal Tx and link Rx callbacks.
      // Drain both owners before allowing finalization to release them.
      txRun_([this, ok]() {
	rxRun_([this, ok]() {
	  ZmAssert(!m_stats.activeRequests.load_());
	  ZmAssert(!m_stats.queuedResponses.load_());
	  ZmAssert(!m_stats.retainedBytes.load_());
	  Engine::stopped(ok);
	});
      });
    });
  }

public:
  void diagnostic(unsigned seconds, DiagnosticFn fn) {
    m_runtime.add(seconds, ZuMv(fn));
  }
  void wait() { m_runtime.wait(); }
  bool wait(unsigned timeout) { return m_runtime.wait(timeout); }
  void final() {
    if (m_mx) (void)Engine::stop();
    ZmAssert(!m_stats.activeConnections.load_());
    ZmAssert(!m_stats.activeRequests.load_());
    ZmAssert(!m_stats.queuedResponses.load_());
    ZmAssert(!m_stats.retainedBytes.load_());
    ZmAssert(m_h1TCPResponses.fifo.empty_());
    ZmAssert(m_h1TLSResponses.fifo.empty_());
    ZmAssert(m_h1TCPResponses.ready.empty_());
    ZmAssert(m_h1TLSResponses.ready.empty_());
    ZmAssert(m_h2TLSResponses.fifo.empty_());
    ZmAssert(m_h3QUICResponses.fifo.empty_());
    ZmAssert(m_bodyTasks.empty_());
    ZmAssert(!m_bodyPending);
    m_hubs.final();
    m_runtime.final();
    m_altSvc.null();
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
    m_workload = nullptr;
    m_config = {};
  }

  bool failServer_() {
    ++m_stats.serverFaults;
    m_failed = true;
    m_runtime.stop();
    Engine::stop({});
    return false;
  }
  bool ok() const { return !m_failed.load_(); }
  unsigned activeConnections() const {
    return m_stats.activeConnections.load_();
  }
  unsigned activeRequests() const { return m_stats.activeRequests.load_(); }
  unsigned queuedResponses() const {
    return m_stats.queuedResponses.load_();
  }
  uint64_t retainedBytes() const { return m_stats.retainedBytes.load_(); }
  uint64_t serverFaults() const { return m_stats.serverFaults.load_(); }
  uint64_t rejectedRequests() const {
    return m_stats.rejectedRequests.load_();
  }
  uint64_t parseFailures() const { return m_stats.parseFailures.load_(); }
  uint64_t responseBuildFailures() const {
    return m_stats.responseBuildFailures.load_();
  }
  uint64_t transportFailures() const {
    return m_stats.transportFailures.load_();
  }
  unsigned hubCount() const { return m_hubs.count(); }

#ifdef Zquic_DEBUG
  void printQUICDiag() { m_quic.printDiag(); }
#endif

private:
  template <typename Link>
  void registerTxError_(Link &link) {
    link.txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
      if (!m_txErrorFn) return true;
      return m_txErrorFn(e);
    }});
  }

  template <typename L>
  void rxRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_rxThread);
  }
  template <typename L>
  void txRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_txThread);
  }

  uint64_t fixedBodyMax_() const {
    uint64_t n = m_config.retainedBodyMax();
    if (n > m_config.retainedMessageMax())
      n = m_config.retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
  }

  bool admit() {
    unsigned active = ++m_stats.activeConnections;
    if (!m_config.maxConnections() ||
	active <= m_config.maxConnections())
      return true;
    --m_stats.activeConnections;
    ++m_stats.rejectedRequests;
    return false;
  }
  bool admitRequest_() {
    if (!m_admitRequests.load_()) {
      ++m_stats.rejectedRequests;
      return false;
    }
    unsigned active = ++m_stats.activeRequests;
    if (active <= m_config.maxRequests()) return true;
    --m_stats.activeRequests;
    ++m_stats.rejectedRequests;
    return false;
  }
  void release(Transport::T transport) {
    --m_stats.activeConnections;
    m_workload->disconnected(transport);
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  ServerConfig	m_config;
  MessageString	m_altSvc;
  Workload	*m_workload = nullptr;
  Hub<H1TCP>	m_tcp;
  TLSHub	m_tls;
  Hub<H3QUIC>	m_quic;
  ResponseQueue<H1TCP> m_h1TCPResponses;
  ResponseQueue<H1TLS> m_h1TLSResponses;
  ResponseQueue<H2TLS> m_h2TLSResponses;
  ResponseQueue<H3QUIC> m_h3QUICResponses;
  BodyTaskQ	m_bodyTasks;
  Hubs	m_hubs;
  Runtime	m_runtime;
  ZiTxErrorFn	m_txErrorFn;
  Stats		m_stats;
  unsigned	m_bodyPending = 0;
  ZmAtomic<unsigned>	m_admitRequests = 0;
  ZmAtomic<unsigned> m_failed = 0;
};

} // namespace Zhttp

#endif /* ZhttpServer_HH */
