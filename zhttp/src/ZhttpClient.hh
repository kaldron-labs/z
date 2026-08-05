//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - multi-protocol client request coordinator

#ifndef ZhttpClient_HH
#define ZhttpClient_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmBlock.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtEnum.hh>

#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClientPool.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZhttpHubs.hh>
#include <zlib/ZhttpRuntime.hh>
#include <zlib/ZhttpTLSClientPool.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpAltSvc.hh>

namespace Zhttp {

ZtEnumStruct(ZhttpAPI, AttemptPhase, int8_t,
  Idle, Resolving, Connecting, Sending,
  ReceivingHeaders, ReceivingBody, Closing);

ZtEnumStruct(ZhttpAPI, FailureKind, int8_t,
  None, Connect, Tx, Protocol, Body);

ZtEnumStruct(ZhttpAPI, RedirectState, int8_t,
  None, Valid, Invalid);

ZtEnumStruct(ZhttpAPI, Persistence, int8_t,
  Default, KeepAlive, Close);

ZtFlagsStruct(ZhttpAPI, AttemptEvent, int8_t,
  SelectionObserved, FailureObserved);

template <
  typename Owner, typename Profile, typename Attempt,
  typename Request, typename ResParser>
class ClientPool;

// TxQ is an unordered ZmPQTx specialized on the final application type.
// Request_ and ResParser_ conform to the extended application Builder and
// Parser contracts documented in Zhttp.hh.

template <typename TxQ, typename ResParser_>
class Client : public TxQ {
public:
  using Tx = TxQ;
  using Request = typename Tx::Msg;
  using Request_ = typename Request::T;
  using ResParser = ResParser_;
  using ReqHeaders = typename Request_::Headers;
  using RespHeaders = typename ResParser::Headers;
  using BodyPolicy = typename Request_::BodyPolicy;
  using Self = Client;
  static constexpr uint64_t RespBodyMax = ResParser::BodyMax;

  static_assert(!Tx::Ordered,
    "Zhttp::Client requires unordered ZmPQTx acknowledgements");

  struct AttemptID {
    uint64_t	request = 0; // logical request; stable across wire attempts
    uint64_t	attempt = 0; // wire generation; changes on retry/redirect
  };

  struct ResponseBody {
    uint64_t	received = 0;
    uint64_t	consumed = 0;
    uint64_t	pending = 0;
    uint64_t	reset = 0;
    uint64_t	discarded = 0;
  };

  struct AttemptRoute {
    URLStorage		url;
    URLStorage		redirect;
    Endpoint		endpoint;
    Endpoints		endpoints;
    unsigned		endpointIndex = 0;
    RedirectState::T	redirectState = RedirectState::None;
    bool		endpointSet = false;
  };

  struct AttemptProtocol {
    unsigned		status = 0;
    Transport::T	transport = Transport::TCP;
    Version::T		httpVersion = Version::H1;
    Persistence::T	persistence = Persistence::Default;
    bool		http10 = false;
    bool		closeDelimited = false;
  };

  struct AttemptFailureState {
    FailureKind::T	kind = FailureKind::None;
    bool		transient = false;
  };

  struct Attempt {
    // Valid while node is non-null; the Tx queue owns the node.
    Request_ &request_() { return node->data(); }
    const Request_ &request_() const { return node->data(); }

    Request		*node = nullptr;
    DiscoveryRequestRef	discovery;
    AttemptID		identity;
    AttemptRoute	route;
    BodyCommit		requestBody;
    ResponseBody	responseBody;
    AttemptProtocol	protocol;
    AttemptFailureState failure;
    AttemptEvent::T	events = 0;
    ResultCode::T	terminal = -1;
    unsigned		slot = 0;
    unsigned		redirects = 0;
    unsigned		retries = 0;
    AttemptPhase::T	phase = AttemptPhase::Idle;
  };

  using TCPPool = ClientPool<
    Self, H1TCP, Attempt, Request_, ResParser>;
  using TLSPool = TLSClientPool<
    Self, Attempt, Request_, ResParser>;
  using QUICPool = ClientPool<
    Self, H3QUIC, Attempt, Request_, ResParser>;
  using Attempts =
    ZtArray<Attempt, ZtArrayHeapID<"Zhttp.Client.Attempts">>;
  using Free =
    ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Client.Free">>;

private:
  template <typename Heap>
  struct DiscoveryPost_ : public Heap, public ZmObject {
    DiscoveryPost_(DiscoveryError error_, Endpoints endpoints_) :
      endpoints{ZuMv(endpoints_)}, error{error_} { }

    Endpoints		endpoints;
    DiscoveryError	error;
  };
  using DiscoveryPost = DiscoveryPost_<
    ZmHeap<"Zhttp.Client.Discovery",
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
    ZmHeap<"Zhttp.Client.Timer", RequestTimer_<ZuVoid>>>;
  using RequestTimerRef = ZmRef<RequestTimer>;
  using RequestTimers =
    ZtArray<RequestTimerRef, ZtArrayHeapID<"Zhttp.Client.Timers">>;

public:
  Client() :
    m_tcp{this}, m_tls{this}, m_quic{this}, m_altSvc{1} { }

  uint64_t retainedBodyMax() const { return m_config.retainedBodyMax(); }
  uint64_t retainedMessageMax() const {
    return m_config.retainedMessageMax();
  }
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  bool init(
    const HubConfig &hub, const ClientConfig &config,
    const TCPConfig &tcp, H2Config tls, const QUICConfig &quic)
  {
    if (!hub.mx() || !config.concurrency() ||
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
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = config;
    m_altSvc = AltSvcCache{config.maxOrigins()};
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
    if (config.tcp() && !m_hubs.init(m_tcp, hub, tcp)) return false;
    if (config.tls() && !m_hubs.init(m_tls, hub, tls)) return false;
    if (config.quic() && !m_hubs.init(m_quic, hub, quic)) return false;
    return m_hubs.count();
  }

  bool start() {
    if (!m_hubs.start()) return false;
    txRun_([this]() { Tx::start(); });
    return true;
  }

  void enqueue(ZmRef<Request> request) {
    txRun_([this, request = ZuMv(request)]() mutable {
      (void)enqueue_(ZuMv(request));
    });
  }
  bool enqueue_(ZmRef<Request> request) {
    assertTx_();
    if (m_txStopping || m_sealed || !request) return false;
    Tx::send(ZuMv(request));
    return true;
  }
  void cancel(typename Tx::Key key) {
    txRun_([this, key]() { cancel_(key); });
  }
  void seal() {
    txRun_([this]() { seal_(); });
  }
  void seal_() {
    assertTx_();
    m_sealed = true;
    idleTx_();
  }
  template <typename L>
  void txRun(L &&l) {
    txRun_(ZuFwd<L>(l));
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
      [this](auto wake) { stop(Hubs::DoneFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    Hubs::DoneFn done_{ZuFwd<Done>(done)};
    if (!m_mx) {
      m_hubs.stop(ZuMv(done_));
      return;
    }
    rxRun_([this, done = ZuMv(done_)]() mutable {
      stopIngress_();
      m_hubs.stop(Hubs::DoneFn{
	[this, done = ZuMv(done)](bool ok) mutable {
	  txRun_([this, done = ZuMv(done), ok]() mutable {
	    stopTx_();
	    rxRun_([done = ZuMv(done), ok]() mutable { done(ok); });
	  });
	}});
    });
  }

  void final() {
    if (m_mx) stop();
    m_hubs.final();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_runtime.final();
    m_attempts.length(0);
    m_timers.length(0);
    m_free.length(0);
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
  }

  unsigned completed() const { return m_completed; }
  unsigned failed() const { return m_failed; }
  unsigned active() const { return m_active; }

  bool send_(Request *request, bool) {
    assertTx_();
    if (m_txStopping || m_txActive >= m_config.concurrency()) return false;
    ++m_txActive;
    rxRun_([this, request]() { admit_(request); });
    return true;
  }
  bool resend_(Request *request, bool more) {
    return send_(request, more);
  }
  bool sendGap_(const typename Tx::Span &, bool) {
    assertTx_();
    return true;
  }
  bool resendGap_(const typename Tx::Span &, bool) {
    assertTx_();
    return true;
  }

  void scheduleSend() { txRun_([this]() { Tx::send(); }); }
  void rescheduleSend() { scheduleSend(); }
  void idleSend() { idleTx_(); }
  void scheduleResend() { txRun_([this]() { Tx::resend(); }); }
  void rescheduleResend() { scheduleResend(); }
  void idleResend() { idleTx_(); }
  void scheduleArchive() { txRun_([this]() { Tx::archive(); }); }
  void rescheduleArchive() { scheduleArchive(); }
  void idleArchive() { idleTx_(); }

  void printQUICDiag() { m_quic.printDiag(); }

  template <typename Link>
  void poolConnect(Link &link, Attempt &attempt) {
    URL url = attempt.route.url.url();
    if constexpr (ZuIsSame<typename Link::Protocol, QUIC>{}) {
      if (attempt.route.endpointSet)
	link.connectEndpoint(attempt.route.endpoint);
      else
	link.connect(url.host, url.port);
    } else
      link.connect(url.host, url.port);
  }
  template <typename Link>
  bool poolTxError(Link &, Attempt *attempt, ZeException &e) {
    if (!m_txErrorFn) return true;
    return m_txErrorFn(e);
  }
  template <typename Link>
  void poolSend(Link &, Attempt &attempt, int) {
    sending_(attempt);
  }
  template <typename Link>
  void poolConnected(
    Link &, Attempt &attempt, const ConnectedInfo &info) {
    attempt.protocol.transport = info.transport;
    attempt.protocol.httpVersion = info.httpVersion;
    if (!(attempt.events & Zhttp::AttemptEvent{}.SelectionObserved())) {
      attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
      observe_(
	&attempt.request_(), event_(attempt, ClientEventType::Selected));
    }
    attempt.request_().connected(info);
  }
  template <typename Link>
  void poolDisconnected(Link &, Attempt *attempt, bool peer) {
    if (attempt && attempt->node) attempt->request_().disconnected(peer);
  }
  template <typename Link>
  void poolConnectFailed(Link &, Attempt *attempt, bool transient) {
    if (attempt) {
      fail_(*attempt, FailureKind::Connect);
      attempt->failure.transient = transient;
      attempt->events |= Zhttp::AttemptEvent{}.FailureObserved();
      observe_(
	&attempt->request_(), event_(*attempt, ClientEventType::AttemptFailed,
	  ResultCode::Failed, transient));
    }
    if (attempt && attempt->node)
      attempt->request_().connectFailed(transient);
  }
  template <typename Link>
  void poolTxCommitted(
    Link &, Attempt &attempt, const BodyCommit &commit) {
    attempt.requestBody = commit;
  }
  template <typename Link>
  void poolTxFailed(
    Link &link, Attempt &attempt, const BodyCommit &commit) {
    poolTxCommitted(link, attempt, commit);
    fail_(attempt, FailureKind::Tx);
  }
  void poolCloseDelimited(Attempt &attempt) {
    attempt.protocol.closeDelimited = true;
  }
  bool poolReusable(const Attempt &attempt) const {
    return attempt.protocol.persistence != Persistence::Close &&
      !attempt.protocol.closeDelimited &&
      (!attempt.protocol.http10 ||
	attempt.protocol.persistence == Persistence::KeepAlive) &&
      attempt.failure.kind == FailureKind::None;
  }

  template <typename Link>
  void poolComplete(
    Link &link, Attempt &attempt, bool ok, bool reuse) {
    if (m_rxStopping || attempt.terminal >= 0) {
      finish_(link, attempt,
	attempt.terminal >= 0 ? attempt.terminal : ResultCode::Cancelled,
	false);
      return;
    }
    ok = ok && attempt.failure.kind == FailureKind::None;
    if (!ok && !(attempt.events & Zhttp::AttemptEvent{}.FailureObserved())) {
      attempt.events |= Zhttp::AttemptEvent{}.FailureObserved();
      observe_(
	&attempt.request_(), event_(attempt, ClientEventType::AttemptFailed,
	  ResultCode::Failed));
    }
    if (ok && redirectStatus_(attempt.protocol.status) &&
	attempt.route.redirectState != RedirectState::None) {
      if (!canReplay_(attempt)) {
	finish_(link, attempt, ResultCode::ReplayUnsafe, reuse);
	return;
      }
      if (attempt.redirects >= m_config.maxRedirects()) {
	finish_(link, attempt, ResultCode::RedirectLimit, reuse);
	return;
      }
      if (attempt.route.redirectState == RedirectState::Invalid) {
	finish_(link, attempt, ResultCode::InvalidRedirect, reuse);
	return;
      }
      URL current = attempt.route.url.url();
      URL next = attempt.route.redirect.url();
      bool same = current.origin() == next.origin();
      ++attempt.redirects;
      attempt.route.url = ZuMv(attempt.route.redirect);
      uint64_t previous = attempt.identity.attempt;
      nextAttempt_(attempt, true);
      auto event = event_(attempt, ClientEventType::Redirected);
      event.previousAttempt = previous;
      observe_(&attempt.request_(), event);
      attempt.request_().redirected(attempt.route.url.url());
      if (reuse && same && direct_<Link>(attempt)) {
	attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
	observe_(
	  &attempt.request_(), event_(attempt, ClientEventType::Selected));
	link.assign(&attempt);
	link.sendRequest();
      } else {
	route_(attempt);
	link.retire();
      }
      return;
    }
    if (!ok && attempt.failure.kind == FailureKind::Tx) {
      finish_(link, attempt,
	(attempt.requestBody.headers &&
	  !canReplay_(attempt)) ?
	  ResultCode::Indeterminate : ResultCode::Failed,
	false);
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
	    if (responseStarted_(attempt) || !canReplay_(attempt)) {
	      finish_(link, attempt, ResultCode::ReplayUnsafe, false);
	      return;
	    }
	    {
	      uint64_t previous = attempt.identity.attempt;
	      nextAttempt_(attempt, false);
	      auto event = event_(attempt, ClientEventType::Retried);
	      event.previousAttempt = previous;
	      observe_(&attempt.request_(), event);
	      startTLS_(attempt);
	      link.retire();
	    }
	    return;
	}
      }
    if (!ok && retry_(link, attempt)) return;
    if (!ok && attempt.protocol.transport == Transport::QUIC &&
	m_config.protocol() == ProtocolPolicy::PreferH3) {
      if (!responseStarted_(attempt) && canReplay_(attempt)) {
	uint64_t previous = attempt.identity.attempt;
	nextAttempt_(attempt, false);
	auto event = event_(attempt, ClientEventType::Fallback);
	event.previousAttempt = previous;
	observe_(&attempt.request_(), event);
	startTLS_(attempt);
	link.retire();
	return;
      }
      finish_(link, attempt,
	(responseStarted_(attempt) || attempt.requestBody.headers) ?
	  ResultCode::Indeterminate :
	  ResultCode::ReplayUnsafe, false);
      return;
    }
    finish_(link, attempt,
      ok ? ResultCode::OK :
	((responseStarted_(attempt) || attempt.requestBody.headers) &&
	  !canReplay_(attempt) ?
	  ResultCode::Indeterminate : ResultCode::Failed),
      reuse);
  }

  template <typename Link>
  void poolStopped(Link &) {
  }

  template <typename Link>
  void status(
    Link &, Attempt &attempt, ResParser &parser, unsigned value) {
    attempt.protocol.status = value;
    receivingHeaders_(attempt);
    parser.status(value);
  }
  template <typename Link>
  void contentLength(
    Link &, Attempt &, ResParser &parser, uint64_t value) {
    parser.contentLength(value);
  }
  template <typename Link>
  void chunked(Link &, Attempt &, ResParser &parser) {
    parser.chunked();
  }
  template <typename Link>
  void version(
    Link &, Attempt &attempt, ResParser &parser, ZuBSpan value) {
    attempt.protocol.http10 = ZuCSpan(value) == "HTTP/1.0";
    parser.version(value);
  }
  template <typename Key, typename Link>
  void header(
    Link &, Attempt &attempt, ResParser &parser, ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc") {
      URL url = attempt.route.url.url();
      Origin origin{url.origin()};
      m_altSvc.update(
	origin, value, m_config.maxAltSvc(), Zm::now());
    } else if constexpr (Key{}() == "connection") {
      if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "close"))
	attempt.protocol.persistence = Persistence::Close;
      else if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "keep-alive"))
	attempt.protocol.persistence = Persistence::KeepAlive;
    } else if constexpr (Key{}() == "location") {
      attempt.route.redirectState =
	attempt.route.redirect.resolve(attempt.route.url.url(), value).ok() ?
	RedirectState::Valid : RedirectState::Invalid;
    }
    parser.template header<Key>(value);
  }
  template <typename Link, typename Rx>
  void body(
    Link &link, Attempt &attempt, ResParser &parser, Rx &rx) {
    headersDone_(link, attempt);
    uint64_t before = rx.length();
    if (before < attempt.responseBody.pending) {
      fail_(attempt, FailureKind::Body);
      return;
    }
    attempt.responseBody.received += before - attempt.responseBody.pending;
    parser.body(rx);
    uint64_t pending = rx.length();
    if (pending > before) {
      fail_(attempt, FailureKind::Body);
      return;
    }
    attempt.responseBody.consumed += before - pending;
    attempt.responseBody.pending = pending;
    link.responseBodyBytes(&attempt);
  }
  template <typename ParserState, typename Link>
  void complete(
    Link &link, Attempt &attempt, ResParser &parser,
    typename ParserState::T state) {
    if (attempt.phase == AttemptPhase::Closing) return;
    headersDone_(link, attempt);
    closing_(attempt);
    attempt.responseBody.reset += attempt.responseBody.pending;
    attempt.responseBody.discarded += attempt.responseBody.pending;
    attempt.responseBody.pending = 0;
    bool ok = state == ParserState::Complete;
    if (!ok) fail_(attempt, FailureKind::Protocol);
    parser.complete(ok);
    link.complete(ok);
  }
  bool done(const Attempt &attempt) const {
    return attempt.phase == AttemptPhase::Closing;
  }

private:
  void assertRx_() const {
    ZmAssert(ZmSelf() && ZmSelf()->sid() == int(m_rxThread));
  }
  void assertTx_() const {
    ZmAssert(ZmSelf() && ZmSelf()->sid() == int(m_txThread));
  }

  template <typename L>
  void rxRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_rxThread);
  }
  template <typename L>
  void txRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_txThread);
  }

  static bool redirectStatus_(unsigned status) {
    return status == 301 || status == 302 || status == 303 ||
      status == 307 || status == 308;
  }

  ClientEvent event_(
    const Attempt &attempt, ClientEventType::T type,
    ResultCode::T result = ResultCode::OK, bool transient = false) const {
    return {
      .request = attempt.identity.request,
      .attempt = attempt.identity.attempt,
      .status = attempt.protocol.status,
      .redirects = uint16_t(attempt.redirects),
      .retries = uint16_t(attempt.retries),
      .type = type,
      .result = result,
      .transport = attempt.protocol.transport,
      .httpVersion = attempt.protocol.httpVersion,
      .endpointSource = EndpointSource::T(attempt.route.endpointSet ?
	attempt.route.endpoint.source : EndpointSource::Origin),
      .transient = transient,
      .responseStarted = responseStarted_(attempt)
    };
  }

  static ClientEvent event_(
    const Result &result, ClientEventType::T type) {
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

  void observe_(Request_ *request, const ClientEvent &event) {
    if (request) request->observed(event);
  }

  static typename Tx::Key key_(const Request *request) {
    return request->key();
  }

  void cancel_(typename Tx::Key key) {
    assertTx_();
    if (auto request = Tx::abort(key)) {
      rxRun_([this, request = ZuMv(request)]() mutable {
	complete_(request->data(), ResultCode::Cancelled);
	txRun_([this]() { idleTx_(); });
      });
      return;
    }
    rxRun_([this, key]() { cancelActive_(key); });
  }

  void cancelActive_(typename Tx::Key key) {
    assertRx_();
    for (unsigned i = 0; i < m_attempts.length(); ++i) {
      auto &attempt = m_attempts[i];
      if (!attempt.node || key_(attempt.node) != key) continue;
      if (attempt.discovery) {
	auto discovery = ZuMv(attempt.discovery);
	completeAttempt_(attempt, ResultCode::Cancelled);
	discovery->cancel();
	return;
      }
      cancelTimer_(attempt);
      attempt.terminal = ResultCode::Cancelled;
      switch (attempt.protocol.transport) {
	case Transport::TCP: (void)m_tcp.cancel(&attempt); break;
	case Transport::TLS: (void)m_tls.cancel(&attempt); break;
	case Transport::QUIC: (void)m_quic.cancel(&attempt); break;
      }
      return;
    }
  }

  void admit_(Request *request) {
    assertRx_();
    if (m_rxStopping || !request) {
      if (request) {
	complete_(request->data(), ResultCode::Cancelled);
	terminal_(key_(request));
      }
      return;
    }
    if (!m_free) {
      complete_(request->data(), ResultCode::Failed);
      terminal_(key_(request));
      return;
    }
    unsigned slot = m_free.pop();
    ++m_active;
    begin_(m_attempts[slot], request);
  }

  void begin_(Attempt &attempt, Request *request) {
    prepare_(attempt, request);
    route_(attempt);
  }

  void prepare_(Attempt &attempt, Request *request) {
    attempt.node = request;
    attempt.route.url = attempt.request_().url;
    attempt.redirects = 0;
    attempt.retries = 0;
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints.length(0);
    attempt.identity.request = ++m_requestID;
    attempt.identity.attempt = ++m_attemptID;
    resetWire_(attempt);
    armTimer_(attempt);
  }

  void nextAttempt_(Attempt &attempt, bool generation) {
    attempt.identity.attempt = ++m_attemptID;
    if (generation) {
      attempt.retries = 0;
      attempt.route.endpointIndex = 0;
      attempt.route.endpoints.length(0);
    }
    resetWire_(attempt);
  }

  static void resetWire_(Attempt &attempt) {
    // Preserve the request node, logical identity, route URL, discovered
    // endpoints, and cumulative retry/redirect counts for the next wire
    // generation.
    attempt.discovery = nullptr;
    attempt.route.redirectState = RedirectState::None;
    attempt.route.endpointSet = false;
    attempt.requestBody = {};
    attempt.responseBody = {};
    attempt.protocol = {};
    attempt.failure = {};
    attempt.events = 0;
    attempt.terminal = -1;
    attempt.phase = AttemptPhase::Idle;
  }

  static void resolving_(Attempt &attempt) {
    ZmAssert(attempt.node && attempt.phase == AttemptPhase::Idle);
    attempt.phase = AttemptPhase::Resolving;
  }

  static void connecting_(Attempt &attempt) {
    ZmAssert(attempt.node &&
      (attempt.phase == AttemptPhase::Idle ||
	attempt.phase == AttemptPhase::Resolving));
    attempt.phase = AttemptPhase::Connecting;
  }

  static void sending_(Attempt &attempt) {
    ZmAssert(attempt.node &&
      (attempt.phase == AttemptPhase::Idle ||
	attempt.phase == AttemptPhase::Connecting));
    attempt.phase = AttemptPhase::Sending;
  }

  static void receivingHeaders_(Attempt &attempt) {
    ZmAssert(attempt.node &&
      (attempt.phase == AttemptPhase::Sending ||
	attempt.phase == AttemptPhase::ReceivingHeaders));
    attempt.phase = AttemptPhase::ReceivingHeaders;
  }

  static void receivingBody_(Attempt &attempt) {
    ZmAssert(attempt.node &&
      (attempt.phase == AttemptPhase::Sending ||
	attempt.phase == AttemptPhase::ReceivingHeaders ||
	attempt.phase == AttemptPhase::ReceivingBody));
    attempt.phase = AttemptPhase::ReceivingBody;
  }

  static void closing_(Attempt &attempt) {
    ZmAssert(attempt.node && attempt.phase != AttemptPhase::Idle);
    attempt.phase = AttemptPhase::Closing;
  }

  static bool responseStarted_(const Attempt &attempt) {
    return attempt.protocol.status != 0;
  }

  static void idleAttempt_(Attempt &attempt) {
    attempt.node = nullptr;
    resetWire_(attempt);
    attempt.identity = {};
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints.length(0);
    attempt.redirects = 0;
    attempt.retries = 0;
    ZmAssert(!attempt.node && !attempt.discovery &&
      attempt.phase == AttemptPhase::Idle);
  }

  static void fail_(Attempt &attempt, FailureKind::T kind) {
    // The first classified failure determines result/retry precedence.
    if (attempt.failure.kind == FailureKind::None)
      attempt.failure.kind = kind;
  }

  template <typename Link>
  bool direct_(Attempt &attempt) {
    using Protocol = typename Link::Protocol;
    URL url = attempt.route.url.url();
    if constexpr (ZuIsSame<Protocol, TCP>{})
      return url.scheme == Scheme::http;
    if constexpr (ZuIsSame<Protocol, TLS>{})
      if (url.scheme == Scheme::https) {
	if (m_config.protocol() == ProtocolPolicy::DisableH3) return true;
	if (m_config.protocol() == ProtocolPolicy::PreferH3 &&
	    attempt.protocol.transport == Transport::TLS)
	  return !m_altSvc.hasH3(Origin{url.origin()}, Zm::now());
      }
    return false;
  }

  template <typename Profile>
  static void select_(Attempt &attempt) {
    using HTTP = ProfileTraits<Profile>;
    attempt.protocol.transport = HTTP::Transport::ID;
    attempt.protocol.httpVersion = HTTP::HTTPVersion;
  }

  void route_(Attempt &attempt) {
    if (attempt.route.url.url().scheme == Scheme::http) {
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
    URL url = attempt.route.url.url();
    Origin origin{url.origin()};
    Endpoint endpoint;
    bool found = false;
    m_altSvc.get(origin, [&found, &endpoint, &origin, &url, this](
	const AltSvcValue &value) {
	if (found || !value.h3) return;
	bool crossHost = value.host != url.host;
	if (crossHost && !m_config.altSvcCrossHost()) return;
	endpoint = Endpoint{
	  .origin = origin,
	  .target = value.host,
	  .tlsName = url.host,
	  .port = value.port ? value.port : url.port,
	  .source = EndpointSource::AltSvc,
	  .httpVersion = Version::H3
	};
	found = true;
    }, Zm::now());
    if (found) {
      if (ZuBSpan{endpoint.target} != url.host)
	resolveAltSvc_(attempt, ZuMv(endpoint));
      else
	startQUIC_(attempt, &endpoint);
      return;
    }
    discover_(attempt);
  }

  void resolveAltSvc_(Attempt &attempt, Endpoint endpoint) {
    unsigned slot = attempt.slot;
    uint64_t id = attempt.identity.attempt;
    resolving_(attempt);
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
	  if (!attempt.node || attempt.identity.attempt != id) return;
	  attempt.discovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    attempt.route.endpoints = ZuMv(post->endpoints);
	    attempt.route.endpointIndex = 0;
	    startQUIC_(attempt, &attempt.route.endpoints[0]);
	  }
	  else {
	    m_altSvc.del(Origin{attempt.route.url.url().origin()});
	    discover_(attempt);
	  }
	});
      }}, m_resolverOps);
  }

  void discover_(Attempt &attempt) {
    URL url = attempt.route.url.url();
    unsigned slot = attempt.slot;
    uint64_t id = attempt.identity.attempt;
    if (attempt.phase != AttemptPhase::Resolving) resolving_(attempt);
    attempt.discovery = discoverH3(
      Origin{url.origin()}, url.host, url.port,
      m_config.blindH3(), m_config.discoveryLimits(), DiscoveryFn{[
	this, slot, id](DiscoveryError error, Endpoints endpoints) mutable {
	DiscoveryPostRef post =
	  new DiscoveryPost{error, ZuMv(endpoints)};
#ifdef ZmObject_DEBUG
	post->ZmObject::debug();
#endif
	rxRun_([this, slot, id, post = ZuMv(post)]() mutable {
	  auto &attempt = m_attempts[slot];
	  if (!attempt.node || attempt.identity.attempt != id) return;
	  attempt.discovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    attempt.route.endpoints = ZuMv(post->endpoints);
	    attempt.route.endpointIndex = 0;
	    startQUIC_(attempt, &attempt.route.endpoints[0]);
	  }
	  else
	    startTLS_(attempt);
	});
      }}, m_resolverOps);
  }

  void startTCP_(Attempt &attempt) {
    select_<H1TCP>(attempt);
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    observe_(
      &attempt.request_(), event_(attempt, ClientEventType::Selected));
    m_tcp.open(&attempt, attempt.slot);
  }
  void startTLS_(Attempt &attempt) {
    attempt.protocol.transport = Transport::TLS;
    switch (m_config.h2Policy()) {
      case H2Policy::Disable:
	attempt.protocol.httpVersion = Version::H1;
	break;
      default:
	attempt.protocol.httpVersion = Version::H2;
	break;
    }
    connecting_(attempt);
    m_tls.open(&attempt, attempt.slot);
  }
  void startQUIC_(Attempt &attempt, const Endpoint *endpoint) {
    select_<H3QUIC>(attempt);
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    if (endpoint) {
      attempt.route.endpoint = *endpoint;
      attempt.route.endpointSet = true;
      attempt.request_().selected(attempt.route.endpoint);
    }
    observe_(
      &attempt.request_(), event_(attempt, ClientEventType::Selected));
    m_quic.open(&attempt, attempt.slot);
  }

  void armTimer_(Attempt &attempt) {
    if (!m_config.requestTimeout()) return;
    auto timer = m_timers[attempt.slot].ptr();
    timer->request = attempt.identity.request;
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
    if (m_rxStopping || slot >= m_attempts.length()) return;
    auto &attempt = m_attempts[slot];
    if (!attempt.node || attempt.identity.request != requestID) return;
    if (attempt.discovery) {
      auto discovery = ZuMv(attempt.discovery);
      completeAttempt_(attempt, ResultCode::TimedOut);
      discovery->cancel();
      return;
    }
    attempt.terminal = ResultCode::TimedOut;
    switch (attempt.protocol.transport) {
      case Transport::TCP: (void)m_tcp.cancel(&attempt); break;
      case Transport::TLS: (void)m_tls.cancel(&attempt); break;
      case Transport::QUIC: (void)m_quic.cancel(&attempt); break;
    }
  }

  template <typename Link>
  bool retry_(Link &link, Attempt &attempt) {
    if (attempt.failure.kind != FailureKind::Connect ||
	!attempt.failure.transient || responseStarted_(attempt) ||
	attempt.retries >= m_config.maxRetries() ||
	!canReplay_(attempt))
      return false;

    uint64_t previous = attempt.identity.attempt;
    ++attempt.retries;
    if (attempt.protocol.transport == Transport::QUIC &&
	attempt.route.endpointIndex + 1 < attempt.route.endpoints.length())
      ++attempt.route.endpointIndex;
    nextAttempt_(attempt, false);
    auto event = event_(attempt, ClientEventType::Retried);
    event.previousAttempt = previous;
    observe_(&attempt.request_(), event);
    switch (attempt.protocol.transport) {
      case Transport::TCP:
	startTCP_(attempt);
	break;
      case Transport::TLS:
	startTLS_(attempt);
	break;
      case Transport::QUIC:
	startQUIC_(
	  attempt, attempt.route.endpoints ?
	    &attempt.route.endpoints[attempt.route.endpointIndex] : nullptr);
	break;
    }
    link.retire();
    return true;
  }

  bool canReplay_(const Attempt &attempt) const {
    return attempt.request_().replayable() &&
      attempt.request_().reproducible();
  }

  template <typename Link>
  void headersDone_(Link &link, Attempt &attempt) {
    if (attempt.phase == AttemptPhase::ReceivingBody ||
	attempt.phase == AttemptPhase::Closing)
      return;
    receivingBody_(attempt);
    link.responseHeadersParsed(&attempt);
  }

  template <typename Link>
  void finish_(
    Link &link, Attempt &attempt, ResultCode::T code, bool reuse) {
    assertRx_();
    (void)reuse;
    cancelTimer_(attempt);
    auto key = key_(attempt.node);
    emit_(attempt.request_(), result_(attempt, code));
    closing_(attempt);
    idleAttempt_(attempt);
    m_free.push(attempt.slot);
    --m_active;
    link.retire();
    terminal_(key);
  }

  void complete_(Request_ &request, ResultCode::T code) {
    Result result{.request = ++m_requestID, .code = code};
    emit_(request, result);
  }

  Result result_(const Attempt &attempt, ResultCode::T code) const {
    return {
      .request = attempt.identity.request,
      .attempt = attempt.identity.attempt,
      .requestBodyProduced = attempt.requestBody.produced,
      .requestBodyCommitted = attempt.requestBody.committed,
      .requestBodyReset = attempt.requestBody.reset,
      .requestBodyDiscarded = attempt.requestBody.discarded,
      .responseBodyReceived = attempt.responseBody.received,
      .responseBodyConsumed = attempt.responseBody.consumed,
      .responseBodyReset = attempt.responseBody.reset,
      .responseBodyDiscarded = attempt.responseBody.discarded,
      .status = attempt.protocol.status,
      .redirects = uint16_t(attempt.redirects),
      .retries = uint16_t(attempt.retries),
      .code = code,
      .transport = attempt.protocol.transport,
      .httpVersion = attempt.protocol.httpVersion
    };
  }

  void emit_(Request_ &request, const Result &result) {
    if (result.code == ResultCode::Cancelled)
      observe_(&request, event_(result, ClientEventType::Cancelled));
    observe_(&request, event_(result, ClientEventType::Completed));
    request.completed(result);
    ++m_completed;
    if (!result.ok()) ++m_failed;
  }

  void completeAttempt_(Attempt &attempt, ResultCode::T code) {
    assertRx_();
    cancelTimer_(attempt);
    auto key = key_(attempt.node);
    emit_(attempt.request_(), result_(attempt, code));
    closing_(attempt);
    idleAttempt_(attempt);
    m_free.push(attempt.slot);
    --m_active;
    terminal_(key);
  }

  void stopIngress_() {
    assertRx_();
    if (m_rxStopping) return;
    m_rxStopping = true;
    for (unsigned i = 0; i < m_attempts.length(); ++i) {
      auto &attempt = m_attempts[i];
      if (!attempt.node) continue;
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

  void terminal_(typename Tx::Key key) {
    assertRx_();
    txRun_([this, key]() {
      assertTx_();
      if (m_txActive) --m_txActive;
      Tx::ackd(key);
      Tx::start();
      idleTx_();
    });
  }

  typename Tx::Impl_ *app_() {
    return static_cast<typename Tx::Impl_ *>(this);
  }

  void stopTx_() {
    assertTx_();
    if (m_txStopping) return;
    m_txStopping = true;
    Tx::stop();
    ZmAssert(!m_txActive);
    auto i = app_()->txQueue()->iter();
    while (auto request = i()) {
      auto ref = i.del();
      rxRun_([this, request = ZuMv(ref)]() mutable {
	complete_(request->data(), ResultCode::Cancelled);
      });
    }
  }

  void idleTx_() {
    assertTx_();
    if (m_sealed && !m_txActive && !app_()->txQueue()->count_())
      m_runtime.stop();
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  ClientConfig	m_config;
  TCPPool	m_tcp;
  TLSPool	m_tls;
  QUICPool	m_quic;
  Hubs	m_hubs;
  Runtime	m_runtime;
  AltSvcCache	m_altSvc;
  ZiTxErrorFn	m_txErrorFn;
  const DiscoveryResolver *m_resolverOps = nullptr;
  Attempts	m_attempts;
  RequestTimers	m_timers;
  Free		m_free;
  uint64_t	m_attemptID = 0;
  uint64_t	m_requestID = 0;
  unsigned	m_txActive = 0;
  unsigned	m_active = 0;
  unsigned	m_completed = 0;
  unsigned	m_failed = 0;
  bool		m_resolverOwned = false;
  bool		m_sealed = false;
  bool		m_rxStopping = false;
  bool		m_txStopping = false;
};

} // namespace Zhttp

#endif /* ZhttpClient_HH */
