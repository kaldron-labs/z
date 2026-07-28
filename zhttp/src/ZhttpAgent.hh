//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - multi-protocol client request coordinator

#ifndef ZhttpAgent_HH
#define ZhttpAgent_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClientPool.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZhttpEngines.hh>
#include <zlib/ZhttpRuntime.hh>
#include <zlib/ZhttpTLSClientPool.hh>
#include <zlib/ZhttpURL.hh>

namespace Zhttp {

template <
  typename Owner, typename Profile, typename Request,
  typename ReqHeaders, typename RespHeaders, uint64_t RespBodyMax>
class ClientPool;

template <
  typename App_, typename Request_,
  typename ReqHeaders_, typename RespHeaders_, uint64_t RespBodyMax_>
class Agent {
public:
  using App = App_;
  using Request = Request_;
  using ReqHeaders = ReqHeaders_;
  using RespHeaders = RespHeaders_;
  using Self = Agent;
  static constexpr uint64_t RespBodyMax = RespBodyMax_;

  struct Attempt {
    Request		*request = nullptr;
    URL			url;
    Endpoint		endpoint;
    Endpoints		endpoints;
    DiscoveryRequestRef	discovery;
    URLString		location;
    uint64_t		requestID = 0;
    uint64_t		id = 0;
    uint64_t		bodyBytes = 0;
    unsigned		slot = 0;
    unsigned		redirects = 0;
    unsigned		retries = 0;
    unsigned		endpointIndex = 0;
    unsigned		status = 0;
    int8_t		transport = Transport::TCP;
    int8_t		httpVersion = Version::H1;
    bool		endpointSet = false;
    bool		headersDone = false;
    bool		responseStarted = false;
    bool		responseDone = false;
    bool		selectionObserved = false;
    bool		failed = false;
    bool		connectFailed = false;
    bool		failureObserved = false;
    bool		transient = false;
    int8_t		terminal = -1;
    bool		connectionClose = false;
    bool		connectionKeepAlive = false;
    bool		http10 = false;
    bool		closeDelimited = false;
  };

  using TCPPool = ClientPool<
    Self, H1TCP, Attempt, ReqHeaders, RespHeaders, RespBodyMax>;
  using TLSPool = TLSClientPool<
    Self, Attempt, ReqHeaders, RespHeaders, RespBodyMax>;
  using QUICPool = ClientPool<
    Self, H3QUIC, Attempt, ReqHeaders, RespHeaders, RespBodyMax>;

  using Requests =
    ZtArray<Request *, ZtArrayHeapID<"Zhttp.Agent.Requests">>;
  using Attempts =
    ZtArray<Attempt, ZtArrayHeapID<"Zhttp.Agent.Attempts">>;
  using Free =
    ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Agent.Free">>;

private:
  template <typename Heap>
  struct DiscoveryPost_ : public Heap, public ZmObject {
    DiscoveryPost_(DiscoveryError error_, Endpoints endpoints_) :
      endpoints{ZuMv(endpoints_)}, error{error_} { }

    Endpoints		endpoints;
    DiscoveryError	error;
  };
  using DiscoveryPost = DiscoveryPost_<
    ZmHeap<"Zhttp.Agent.Discovery",
      DiscoveryPost_<ZuVoid>>>;
  using DiscoveryPostRef = ZmRef<DiscoveryPost>;

  template <typename Heap>
  struct RequestTimer_ : public Heap, public ZmObject {
    RequestTimer_(Self *agent_, unsigned slot_) :
      agent{agent_}, slot{slot_} { }

    void fire() {
      if (!armed) return;
      armed = false;
      agent->timeout_(slot, request);
    }

    Self		*agent;
    ZmScheduler::Timer	timer;
    uint64_t		request = 0;
    unsigned		slot;
    bool		armed = false;
  };
  using RequestTimer = RequestTimer_<
    ZmHeap<"Zhttp.Agent.Timer", RequestTimer_<ZuVoid>>>;
  using RequestTimerRef = ZmRef<RequestTimer>;
  using RequestTimers =
    ZtArray<RequestTimerRef, ZtArrayHeapID<"Zhttp.Agent.Timers">>;

public:
  Agent() :
    m_tcp{this}, m_tls{this}, m_quic{this}, m_altSvc{1} { }

  App *impl() { return static_cast<App *>(this); }
  const App *impl() const { return static_cast<const App *>(this); }

  bool init(
    const EngineConfig &engine, const AgentConfig &config,
    const TCPConfig &tcp, H2Config tls, const QUICConfig &quic)
  {
    if (!engine.mx() || !config.concurrency() || !config.maxPending() ||
	!config.admissionBatch() ||
	config.protocol() < ProtocolPolicy::ForceH3 ||
	config.protocol() > ProtocolPolicy::DisableH3 ||
	!TLS_::valid(tls) ||
	config.h2Policy() < H2Policy::Force ||
	config.h2Policy() > H2Policy::Disable ||
	!config.tcp() ||
	(config.protocol() == ProtocolPolicy::ForceH3 && !config.quic()) ||
	(config.protocol() == ProtocolPolicy::PreferH3 &&
	  (!config.tls() || !config.quic())) ||
	(config.protocol() == ProtocolPolicy::DisableH3 && !config.tls()))
      return false;
    tls.policy(config.h2Policy());
    m_mx = engine.mx();
    m_rxThread = engine.rxThread() ?
      m_mx->sid(engine.rxThread()) : m_mx->rxThread();
    m_config = config;
    m_altSvc = AltSvcCache{config.maxOrigins()};
    m_pending.length(config.maxPending());
    m_attempts.length(config.concurrency());
    m_timers.size(config.concurrency());
    m_free.size(config.concurrency());
    for (unsigned i = config.concurrency(); i; --i) {
      m_attempts[i - 1].slot = i - 1;
      RequestTimerRef timer = new RequestTimer{this, i - 1};
#ifdef ZmObject_DEBUG
      timer->ZmObject::debug();
#endif
      m_timers.push(ZuMv(timer));
      m_free.push(i - 1);
    }
    if (!m_runtime.init()) return false;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (config.tcp() && !m_engines.init(m_tcp, engine, tcp)) return false;
    if (config.tls() && !m_engines.init(m_tls, engine, tls)) return false;
    if (config.quic() && !m_engines.init(m_quic, engine, quic)) return false;
    return m_engines.count();
  }

  bool start() { return m_engines.start(); }

  // Submit and seal one finite contiguous workload.  Its storage must remain
  // valid through the final completed() callback.
  void submit(Request *requests, unsigned count) {
    rxRun_([this, requests, count]() {
      submit_(requests, count);
    });
  }
  void cancel(Request &request) {
    rxRun_([this, request = &request]() { cancel_(request); });
  }
  // Optional resolver seam; set before submitting requests.  Null selects
  // ZiResolver through the DiscoveryRequest default.
  void discoveryResolver(const DiscoveryResolver *resolver) {
    m_resolverOps = resolver;
  }

  void diagnostic(unsigned seconds, DiagnosticFn fn) {
    m_runtime.add(seconds, ZuMv(fn));
  }
  bool wait(unsigned timeout = 0) {
    return timeout ? m_runtime.wait(timeout) : (m_runtime.wait(), true);
  }

  void stop() {
    (void)ZmBlock<bool>{}(
      [this](auto wake) { stop(Engines::DoneFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    Engines::DoneFn done_{ZuFwd<Done>(done)};
    if (!m_mx) {
      m_engines.stop(ZuMv(done_));
      return;
    }
    rxRun_([this, done = ZuMv(done_)]() mutable {
      stopIngress_();
      // Drain Rx continuations queued before engine ingress was disabled.
      rxRun_([this, done = ZuMv(done)]() mutable {
	m_engines.stop(ZuMv(done));
      });
    });
  }

  void final() {
    if (m_mx) stop();
    m_engines.final();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_runtime.final();
    m_pending.length(0);
    m_attempts.length(0);
    m_timers.length(0);
    m_free.length(0);
    m_mx = nullptr;
    m_rxThread = 0;
  }

  unsigned completed() const { return m_completed; }
  unsigned failed() const { return m_failed; }
  unsigned pending() const { return m_count; }
  unsigned active() const { return m_active; }

  void printQUICDiag() { m_quic.printDiag(); }

  // Side-effect-safe application defaults.
  URL requestURL(const Request &request) const { return request.url; }
  bool requestReplayable(const Request &) const { return true; }
  template <typename L>
  void requestOperation(const Request &, const URL &url, L &&l) {
    ZuCSpan path, query;
    url.pathQuery(path, query);
    l(Method::GET, path, query);
  }
  template <typename L>
  void requestHost(const Request &, const URL &url, L &&l) {
    l(ZuCSpan{url.host});
  }
  template <typename Key, typename L>
  void requestHeader(const Request &, L &&l) { l(""); }
  void responseStatus(Request &, unsigned) { }
  void responseContentLength(Request &, uint64_t) { }
  void responseChunked(Request &) { }
  void responseVersion(Request &, ZuBSpan) { }
  template <typename Key>
  void responseHeader(Request &, ZuBSpan) { }
  void responseBody(Request &, ZuBSpan) { }
  void responseEnd(Request &, bool) { }
  void connected(Request &, const ConnectedInfo &) { }
  void disconnected(Request *, bool) { }
  void connectFailed(Request *, bool) { }
  void selected(Request &, const Endpoint &) { }
  void redirected(Request &, const URL &) { }
  void observed(Request *, const AgentEvent &) { }
  void completed(Request &, const Result &) { }

  template <typename Link>
  void poolConnect(Link &link, Attempt &attempt) {
    if constexpr (ZuIsSame<typename Link::Protocol, QUIC>{}) {
      if (attempt.endpointSet)
	link.connectEndpoint(attempt.endpoint);
      else
	link.connect(attempt.url.host, attempt.url.port);
    } else
      link.connect(attempt.url.host, attempt.url.port);
  }
  template <typename Link>
  void poolSend(Link &, Attempt &, int) { }
  template <typename Link>
  void poolConnected(
    Link &, Attempt &attempt, const ConnectedInfo &info) {
    attempt.transport = info.transport;
    attempt.httpVersion = info.httpVersion;
    if (!attempt.selectionObserved) {
      attempt.selectionObserved = true;
      observe_(
	attempt.request, event_(attempt, AgentEventType::Selected));
    }
    impl()->connected(*attempt.request, info);
  }
  template <typename Link>
  void poolDisconnected(Link &, Attempt *attempt, bool peer) {
    impl()->disconnected(attempt ? attempt->request : nullptr, peer);
  }
  template <typename Link>
  void poolConnectFailed(Link &, Attempt *attempt, bool transient) {
    if (attempt) {
      attempt->failed = true;
      attempt->connectFailed = true;
      attempt->transient = transient;
      attempt->failureObserved = true;
      observe_(
	attempt->request, event_(*attempt, AgentEventType::AttemptFailed,
	  ResultCode::Failed, transient));
    }
    impl()->connectFailed(attempt ? attempt->request : nullptr, transient);
  }
  void poolCloseDelimited(Attempt &attempt) {
    attempt.closeDelimited = true;
  }
  bool poolReusable(const Attempt &attempt) const {
    return !attempt.connectionClose && !attempt.closeDelimited &&
      (!attempt.http10 || attempt.connectionKeepAlive) && !attempt.failed;
  }

  template <typename Link>
  void poolComplete(
    Link &link, Attempt &attempt, bool ok, bool reuse) {
    if (m_stopping || attempt.terminal >= 0) {
      finish_(link, attempt,
	attempt.terminal >= 0 ? attempt.terminal : ResultCode::Cancelled,
	false);
      return;
    }
    ok = ok && !attempt.failed;
    if (!ok && !attempt.failureObserved) {
      attempt.failureObserved = true;
      observe_(
	attempt.request, event_(attempt, AgentEventType::AttemptFailed,
	  ResultCode::Failed));
    }
    if (ok && redirectStatus_(attempt.status) && attempt.location) {
      URL next;
      if (!impl()->requestReplayable(*attempt.request)) {
	finish_(link, attempt, ResultCode::ReplayUnsafe, reuse);
	return;
      }
      if (attempt.redirects >= m_config.maxRedirects()) {
	finish_(link, attempt, ResultCode::RedirectLimit, reuse);
	return;
      }
      if (!URL::resolve(next, attempt.url, attempt.location).ok()) {
	finish_(link, attempt, ResultCode::InvalidRedirect, reuse);
	return;
      }
      bool same = attempt.url.origin() == next.origin();
      ++attempt.redirects;
      attempt.url = ZuMv(next);
      uint64_t previous = attempt.id;
      nextAttempt_(attempt, true);
      auto event = event_(attempt, AgentEventType::Redirected);
      event.previousAttempt = previous;
      observe_(attempt.request, event);
      impl()->redirected(*attempt.request, attempt.url);
      if (reuse && same && direct_<Link>(attempt)) {
	attempt.selectionObserved = true;
	observe_(
	  attempt.request, event_(attempt, AgentEventType::Selected));
	link.assign(&attempt);
	link.sendRequest();
      } else {
	route_(attempt);
	link.retire();
      }
      return;
    }
    if constexpr (ZuIsSame<typename Link::Protocol, TLS>{})
      if (!ok) {
	switch (link.result()) {
	  case ResultCode::Cancelled:
	    finish_(link, attempt, ResultCode::Cancelled, false);
	    return;
	  case ResultCode::Indeterminate:
	    finish_(link, attempt, ResultCode::Indeterminate, false);
	    return;
	  case ResultCode::Unprocessed:
	    if (attempt.responseStarted ||
		!impl()->requestReplayable(*attempt.request)) {
	      finish_(link, attempt, ResultCode::ReplayUnsafe, false);
	      return;
	    }
	    {
	      uint64_t previous = attempt.id;
	      nextAttempt_(attempt, false);
	      auto event = event_(attempt, AgentEventType::Retried);
	      event.previousAttempt = previous;
	      observe_(attempt.request, event);
	      startTLS_(attempt);
	      link.retire();
	    }
	    return;
	}
      }
    if (!ok && retry_(link, attempt)) return;
    if (!ok && attempt.transport == Transport::QUIC &&
	m_config.protocol() == ProtocolPolicy::PreferH3) {
      if (!attempt.responseStarted &&
	  impl()->requestReplayable(*attempt.request)) {
	uint64_t previous = attempt.id;
	nextAttempt_(attempt, false);
	auto event = event_(attempt, AgentEventType::Fallback);
	event.previousAttempt = previous;
	observe_(attempt.request, event);
	startTLS_(attempt);
	link.retire();
	return;
      }
      finish_(link, attempt,
	attempt.responseStarted ? ResultCode::Indeterminate :
	  ResultCode::ReplayUnsafe, false);
      return;
    }
    finish_(link, attempt,
      ok ? ResultCode::OK :
	(attempt.responseStarted &&
	  !impl()->requestReplayable(*attempt.request) ?
	  ResultCode::Indeterminate : ResultCode::Failed),
      reuse);
  }

  template <typename Link>
  void poolStopped(Link &) {
    idle_();
  }

  template <typename L>
  void requestOperation(const Attempt &attempt, L &&l) {
    impl()->requestOperation(
      *attempt.request, attempt.url, ZuFwd<L>(l));
  }
  template <typename L>
  void requestHost(const Attempt &attempt, L &&l) {
    impl()->requestHost(*attempt.request, attempt.url, ZuFwd<L>(l));
  }
  template <typename Key, typename L>
  void requestHeader(const Attempt &attempt, L &&l) {
    impl()->template requestHeader<Key>(*attempt.request, ZuFwd<L>(l));
  }

  template <typename Link>
  void responseStatus(Link &, Attempt &attempt, unsigned status) {
    attempt.status = status;
    attempt.responseStarted = true;
    impl()->responseStatus(*attempt.request, status);
  }
  template <typename Link>
  void responseContentLength(Link &, Attempt &attempt, uint64_t value) {
    impl()->responseContentLength(*attempt.request, value);
  }
  template <typename Link>
  void responseChunked(Link &, Attempt &attempt) {
    impl()->responseChunked(*attempt.request);
  }
  template <typename Link>
  void responseVersion(Link &, Attempt &attempt, ZuBSpan value) {
    attempt.http10 = ZuCSpan(value) == "HTTP/1.0";
    impl()->responseVersion(*attempt.request, value);
  }
  template <typename Key, typename Link>
  void responseHeader(Link &, Attempt &attempt, ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc") {
      AltSvc altSvc;
      if (AltSvc::parse(
	  altSvc, ZuCSpan(value), attempt.url.origin(),
	  m_config.maxAltSvc()).ok())
	m_altSvc.update(attempt.url.origin(), altSvc, Zm::now());
    } else if constexpr (Key{}() == "connection") {
      if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "close"))
	attempt.connectionClose = true;
      else if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "keep-alive"))
	attempt.connectionKeepAlive = true;
    } else if constexpr (Key{}() == "location")
      attempt.location = ZuCSpan(value);
    impl()->template responseHeader<Key>(*attempt.request, value);
  }
  template <typename Link>
  void responseBody(Link &link, Attempt &attempt, ZuBSpan value) {
    headersDone_(link, attempt);
    attempt.bodyBytes += value.length();
    link.responseBodyBytes(&attempt);
    impl()->responseBody(*attempt.request, value);
  }
  template <typename ParserState, typename Link>
  void responseComplete(
    Link &link, Attempt &attempt, typename ParserState::T state) {
    if (attempt.responseDone) return;
    headersDone_(link, attempt);
    attempt.responseDone = true;
    bool ok = state == ParserState::Complete;
    if (!ok) attempt.failed = true;
    impl()->responseEnd(*attempt.request, ok);
    link.complete(ok);
  }
  bool responseFailed(const Attempt &attempt) const {
    return attempt.responseDone;
  }

private:
  template <typename L>
  void rxRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_rxThread);
  }

  static bool redirectStatus_(unsigned status) {
    return status == 301 || status == 302 || status == 303 ||
      status == 307 || status == 308;
  }

  AgentEvent event_(
    const Attempt &attempt, int8_t type,
    int8_t result = ResultCode::OK, bool transient = false) const {
    return {
      .request = attempt.requestID,
      .attempt = attempt.id,
      .status = attempt.status,
      .redirects = uint16_t(attempt.redirects),
      .retries = uint16_t(attempt.retries),
      .type = type,
      .result = result,
      .transport = attempt.transport,
      .httpVersion = attempt.httpVersion,
      .endpointSource = int8_t(attempt.endpointSet ?
	attempt.endpoint.source : EndpointSource::Origin),
      .transient = transient,
      .responseStarted = attempt.responseStarted
    };
  }

  static AgentEvent event_(const Result &result, int8_t type) {
    return {
      .request = result.request,
      .attempt = result.attempt,
      .status = result.status,
      .redirects = result.redirects,
      .retries = result.retries,
      .type = type,
      .result = result.code,
      .transport = result.transport,
      .httpVersion = result.httpVersion
    };
  }

  void observe_(Request *request, const AgentEvent &event) {
    impl()->observed(request, event);
  }

  Request *shift_() {
    if (!m_count) return nullptr;
    auto request = m_pending[m_head];
    m_pending[m_head] = nullptr;
    if (++m_head == m_pending.length()) m_head = 0;
    --m_count;
    return request;
  }

  bool cancelPending_(Request *request) {
    bool found = false;
    unsigned count = m_count;
    for (unsigned i = 0; i < count; ++i) {
      Request *pending = shift_();
      if (!found && pending == request) {
	found = true;
	continue;
      }
      m_pending[m_tail] = pending;
      if (++m_tail == m_pending.length()) m_tail = 0;
      ++m_count;
    }
    return found;
  }

  void cancel_(Request *request) {
    if (!request) return;
    if (cancelPending_(request)) {
      complete_(*request, {}, ResultCode::Cancelled);
      idle_();
      return;
    }
    for (unsigned i = 0; i < m_attempts.length(); ++i) {
      auto &attempt = m_attempts[i];
      if (attempt.request != request) continue;
      if (attempt.discovery) {
	auto discovery = ZuMv(attempt.discovery);
	completeAttempt_(attempt, ResultCode::Cancelled);
	discovery->cancel();
	return;
      }
      cancelTimer_(attempt);
      attempt.terminal = ResultCode::Cancelled;
      switch (attempt.transport) {
	case Transport::TCP: (void)m_tcp.cancel(&attempt); break;
	case Transport::TLS: (void)m_tls.cancel(&attempt); break;
	case Transport::QUIC: (void)m_quic.cancel(&attempt); break;
      }
      return;
    }
  }

  void request_(Request *request) {
    if (m_stopping || m_sealed || !request) {
      if (request) complete_(*request, {}, ResultCode::Cancelled);
      return;
    }
    if (m_free) {
      unsigned slot = m_free.pop();
      ++m_active;
      begin_(m_attempts[slot], request);
      return;
    }
    if (m_count >= m_pending.length()) {
      complete_(*request, {}, ResultCode::Failed);
      return;
    }
    m_pending[m_tail] = request;
    if (++m_tail == m_pending.length()) m_tail = 0;
    ++m_count;
  }

  void submit_(Request *requests, unsigned count) {
    unsigned n = count;
    if (n > m_config.admissionBatch()) n = m_config.admissionBatch();
    for (unsigned i = 0; i < n; ++i) request_(&requests[i]);
    requests += n;
    count -= n;
    if (count) {
      rxRun_([this, requests, count]() {
	submit_(requests, count);
      });
      return;
    }
    m_sealed = true;
    idle_();
  }

  void begin_(Attempt &attempt, Request *request) {
    prepare_(attempt, request);
    route_(attempt);
  }

  void prepare_(Attempt &attempt, Request *request) {
    attempt.request = request;
    attempt.url = impl()->requestURL(*request);
    attempt.redirects = 0;
    attempt.retries = 0;
    attempt.endpointIndex = 0;
    attempt.endpoints.length(0);
    attempt.requestID = ++m_requestID;
    attempt.id = ++m_attemptID;
    reset_(attempt);
    armTimer_(attempt);
  }

  void nextAttempt_(Attempt &attempt, bool generation) {
    attempt.id = ++m_attemptID;
    if (generation) {
      attempt.retries = 0;
      attempt.endpointIndex = 0;
      attempt.endpoints.length(0);
    }
    reset_(attempt);
  }

  void reset_(Attempt &attempt) {
    attempt.discovery = nullptr;
    attempt.endpoint = {};
    attempt.location.length(0);
    attempt.status = 0;
    attempt.bodyBytes = 0;
    attempt.endpointSet = false;
    attempt.headersDone = false;
    attempt.responseStarted = false;
    attempt.responseDone = false;
    attempt.selectionObserved = false;
    attempt.failed = false;
    attempt.connectFailed = false;
    attempt.failureObserved = false;
    attempt.transient = false;
    attempt.terminal = -1;
    attempt.connectionClose = false;
    attempt.connectionKeepAlive = false;
    attempt.http10 = false;
    attempt.closeDelimited = false;
  }

  template <typename Link>
  bool direct_(Attempt &attempt) {
    using Protocol = typename Link::Protocol;
    if constexpr (ZuIsSame<Protocol, TCP>{})
      return attempt.url.scheme == "http";
    if constexpr (ZuIsSame<Protocol, TLS>{})
      if (attempt.url.scheme == "https") {
	if (m_config.protocol() == ProtocolPolicy::DisableH3) return true;
	if (m_config.protocol() == ProtocolPolicy::PreferH3 &&
	    attempt.transport == Transport::TLS)
	  return !m_altSvc.hasH3(attempt.url.origin(), Zm::now());
      }
    return false;
  }

  template <typename Profile>
  static void select_(Attempt &attempt) {
    using HTTP = ProfileTraits<Profile>;
    attempt.transport = HTTP::Transport::ID;
    attempt.httpVersion = HTTP::HTTPVersion;
  }

  void route_(Attempt &attempt) {
    if (attempt.url.scheme == "http") {
      startTCP_(attempt);
      return;
    }
    switch (m_config.protocol()) {
      case ProtocolPolicy::ForceH3:
	startQUIC_(attempt, nullptr);
	break;
      case ProtocolPolicy::DisableH3:
	startTLS_(attempt);
	break;
      default:
	prefer_(attempt);
	break;
    }
  }

  void prefer_(Attempt &attempt) {
    AltSvcValues values;
    if (m_altSvc.get(attempt.url.origin(), values, Zm::now()))
      for (unsigned i = 0; i < values.length(); ++i) {
	if (!values[i].h3) continue;
	bool crossHost = values[i].host != attempt.url.host;
	if (crossHost && !m_config.altSvcCrossHost()) continue;
	Endpoint endpoint{
	  .origin = attempt.url.origin(),
	  .target = values[i].host ? values[i].host : attempt.url.host,
	  .tlsName = attempt.url.host,
	  .port = values[i].port ? values[i].port : attempt.url.port,
	  .source = EndpointSource::AltSvc,
	  .httpVersion = Version::H3
	};
	if (crossHost)
	  resolveAltSvc_(attempt, ZuMv(endpoint));
	else
	  startQUIC_(attempt, &endpoint);
	return;
      }
    discover_(attempt);
  }

  void resolveAltSvc_(Attempt &attempt, Endpoint endpoint) {
    unsigned slot = attempt.slot;
    uint64_t id = attempt.id;
    attempt.discovery = resolveH3(
      ZuMv(endpoint), m_config.discoveryLimits(), DiscoveryFn{[
      this, slot, id](DiscoveryError error, Endpoints endpoints) mutable {
	DiscoveryPostRef post =
	  new DiscoveryPost{error, ZuMv(endpoints)};
#ifdef ZmObject_DEBUG
	post->ZmObject::debug();
#endif
	rxRun_([this, slot, id, post = ZuMv(post)]() mutable {
	  auto &attempt = m_attempts[slot];
	  if (!attempt.request || attempt.id != id) return;
	  attempt.discovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    attempt.endpoints = ZuMv(post->endpoints);
	    attempt.endpointIndex = 0;
	    startQUIC_(attempt, &attempt.endpoints[0]);
	  }
	  else {
	    m_altSvc.del(attempt.url.origin());
	    discover_(attempt);
	  }
	});
      }}, m_resolverOps);
  }

  void discover_(Attempt &attempt) {
    unsigned slot = attempt.slot;
    uint64_t id = attempt.id;
    attempt.discovery = discoverH3(
      attempt.url.origin(), attempt.url.host, attempt.url.port,
      m_config.blindH3(), m_config.discoveryLimits(), DiscoveryFn{[
	this, slot, id](DiscoveryError error, Endpoints endpoints) mutable {
	DiscoveryPostRef post =
	  new DiscoveryPost{error, ZuMv(endpoints)};
#ifdef ZmObject_DEBUG
	post->ZmObject::debug();
#endif
	rxRun_([this, slot, id, post = ZuMv(post)]() mutable {
	  auto &attempt = m_attempts[slot];
	  if (!attempt.request || attempt.id != id) return;
	  attempt.discovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    attempt.endpoints = ZuMv(post->endpoints);
	    attempt.endpointIndex = 0;
	    startQUIC_(attempt, &attempt.endpoints[0]);
	  }
	  else
	    startTLS_(attempt);
	});
      }}, m_resolverOps);
  }

  void startTCP_(Attempt &attempt) {
    select_<H1TCP>(attempt);
    attempt.selectionObserved = true;
    observe_(
      attempt.request, event_(attempt, AgentEventType::Selected));
    m_tcp.open(&attempt, attempt.slot);
  }
  void startTLS_(Attempt &attempt) {
    attempt.transport = Transport::TLS;
    switch (m_config.h2Policy()) {
      case H2Policy::Disable: attempt.httpVersion = Version::H1; break;
      default: attempt.httpVersion = Version::H2; break;
    }
    m_tls.open(&attempt, attempt.slot);
  }
  void startQUIC_(Attempt &attempt, const Endpoint *endpoint) {
    select_<H3QUIC>(attempt);
    attempt.selectionObserved = true;
    if (endpoint) {
      attempt.endpoint = *endpoint;
      attempt.endpointSet = true;
      impl()->selected(*attempt.request, attempt.endpoint);
    }
    observe_(
      attempt.request, event_(attempt, AgentEventType::Selected));
    m_quic.open(&attempt, attempt.slot);
  }

  void armTimer_(Attempt &attempt) {
    if (!m_config.requestTimeout()) return;
    auto timer = m_timers[attempt.slot].ptr();
    timer->request = attempt.requestID;
    timer->armed = true;
    m_mx->add(
      &timer->timer, Zm::now(m_config.requestTimeout()),
      ZmScheduler::Update,
      [timer](auto &&arm) {
	return arm([timer]() { timer->fire(); });
      }, m_rxThread);
  }

  void cancelTimer_(Attempt &attempt) {
    auto timer = m_timers[attempt.slot].ptr();
    if (!timer->armed) return;
    timer->armed = false;
    m_mx->del(&timer->timer);
  }

  void timeout_(unsigned slot, uint64_t requestID) {
    if (m_stopping || slot >= m_attempts.length()) return;
    auto &attempt = m_attempts[slot];
    if (!attempt.request || attempt.requestID != requestID) return;
    if (attempt.discovery) {
      auto discovery = ZuMv(attempt.discovery);
      completeAttempt_(attempt, ResultCode::TimedOut);
      discovery->cancel();
      return;
    }
    attempt.terminal = ResultCode::TimedOut;
    switch (attempt.transport) {
      case Transport::TCP: (void)m_tcp.cancel(&attempt); break;
      case Transport::TLS: (void)m_tls.cancel(&attempt); break;
      case Transport::QUIC: (void)m_quic.cancel(&attempt); break;
    }
  }

  template <typename Link>
  bool retry_(Link &link, Attempt &attempt) {
    if (!attempt.connectFailed || !attempt.transient ||
	attempt.responseStarted ||
	attempt.retries >= m_config.maxRetries() ||
	!impl()->requestReplayable(*attempt.request))
      return false;

    uint64_t previous = attempt.id;
    ++attempt.retries;
    if (attempt.transport == Transport::QUIC &&
	attempt.endpointIndex + 1 < attempt.endpoints.length())
      ++attempt.endpointIndex;
    nextAttempt_(attempt, false);
    auto event = event_(attempt, AgentEventType::Retried);
    event.previousAttempt = previous;
    observe_(attempt.request, event);
    switch (attempt.transport) {
      case Transport::TCP:
	startTCP_(attempt);
	break;
      case Transport::TLS:
	startTLS_(attempt);
	break;
      case Transport::QUIC:
	startQUIC_(
	  attempt, attempt.endpoints ?
	    &attempt.endpoints[attempt.endpointIndex] : nullptr);
	break;
    }
    link.retire();
    return true;
  }

  template <typename Link>
  void headersDone_(Link &link, Attempt &attempt) {
    if (attempt.headersDone) return;
    attempt.headersDone = true;
    link.responseHeadersParsed(&attempt);
  }

  template <typename Link>
  void finish_(
    Link &link, Attempt &attempt, int8_t code, bool reuse) {
    cancelTimer_(attempt);
    Request *request = attempt.request;
    emit_(*request, result_(attempt, code));

    Request *next = shift_();
    if (next) {
      Origin prev = attempt.url.origin();
      prepare_(attempt, next);
      if (reuse && prev == attempt.url.origin() && direct_<Link>(attempt)) {
	attempt.selectionObserved = true;
	observe_(
	  attempt.request, event_(attempt, AgentEventType::Selected));
	link.assign(&attempt);
	link.sendRequest();
      } else {
	route_(attempt);
	link.retire();
      }
      return;
    }

    attempt.request = nullptr;
    attempt.discovery = nullptr;
    m_free.push(attempt.slot);
    --m_active;
    link.retire();
    idle_();
  }

  void complete_(Request &request, const URL &url, int8_t code) {
    Result result{.request = ++m_requestID, .code = code};
    (void)url;
    emit_(request, result);
  }

  Result result_(const Attempt &attempt, int8_t code) const {
    return {
      .request = attempt.requestID,
      .attempt = attempt.id,
      .status = attempt.status,
      .redirects = uint16_t(attempt.redirects),
      .retries = uint16_t(attempt.retries),
      .code = code,
      .transport = attempt.transport,
      .httpVersion = attempt.httpVersion
    };
  }

  void emit_(Request &request, const Result &result) {
    if (result.code == ResultCode::Cancelled)
      observe_(&request, event_(result, AgentEventType::Cancelled));
    observe_(&request, event_(result, AgentEventType::Completed));
    impl()->completed(request, result);
    ++m_completed;
    if (!result.ok()) ++m_failed;
  }

  void completeAttempt_(Attempt &attempt, int8_t code) {
    cancelTimer_(attempt);
    Request *request = attempt.request;
    emit_(*request, result_(attempt, code));
    ++attempt.id;
    attempt.request = nullptr;
    attempt.discovery = nullptr;
    m_free.push(attempt.slot);
    --m_active;
    idle_();
  }

  void stopIngress_() {
    if (m_stopping) return;
    m_stopping = true;
    observe_(nullptr, AgentEvent{.type = AgentEventType::Stopping});
    while (auto request = shift_())
      complete_(*request, {}, ResultCode::Cancelled);
    for (unsigned i = 0; i < m_attempts.length(); ++i) {
      auto &attempt = m_attempts[i];
      if (!attempt.request) continue;
      cancelTimer_(attempt);
      if (attempt.discovery) {
	auto discovery = ZuMv(attempt.discovery);
	completeAttempt_(attempt, ResultCode::Cancelled);
	discovery->cancel();
      } else
	attempt.terminal = ResultCode::Cancelled;
    }
    m_runtime.stop();
  }

  void idle_() {
    if (m_sealed && !m_active && !m_count) m_runtime.stop();
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  AgentConfig	m_config;
  TCPPool	m_tcp;
  TLSPool	m_tls;
  QUICPool	m_quic;
  Engines	m_engines;
  Runtime	m_runtime;
  AltSvcCache	m_altSvc;
  const DiscoveryResolver *m_resolverOps = nullptr;
  Requests	m_pending;
  Attempts	m_attempts;
  RequestTimers	m_timers;
  Free		m_free;
  uint64_t	m_attemptID = 0;
  uint64_t	m_requestID = 0;
  unsigned	m_head = 0;
  unsigned	m_tail = 0;
  unsigned	m_count = 0;
  unsigned	m_active = 0;
  unsigned	m_completed = 0;
  unsigned	m_failed = 0;
  bool		m_resolverOwned = false;
  bool		m_sealed = false;
  bool		m_stopping = false;
};

} // namespace Zhttp

#endif /* ZhttpAgent_HH */
