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
#include <zlib/ZmContext.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmThread.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZiResolver.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZhttpH2Hub.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

ZtEnumStruct(ZhttpAPI, ProtocolPolicy, int8_t,
  ForceH3, PreferH3, DisableH3);
ZtEnumStruct(ZhttpAPI, ResultCode, int8_t,
  OK, Failed, Cancelled, TimedOut, RedirectLimit, InvalidRedirect,
  ReplayUnsafe, Unprocessed, Indeterminate);

struct Result {
  uint64_t	request = 0;
  uint64_t	attempt = 0;
  uint64_t	requestBodyProduced = 0;
  uint64_t	requestBodyCommitted = 0;
  uint64_t	requestBodyReset = 0;
  uint64_t	requestBodyDiscarded = 0;
  uint64_t	responseBodyReceived = 0;
  uint64_t	responseBodyConsumed = 0;
  uint64_t	responseBodyReset = 0;
  uint64_t	responseBodyDiscarded = 0;
  uint32_t	status = 0;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  ResultCode::T	code = ResultCode::OK;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;

  bool ok() const { return code == ResultCode::OK; }
};
ZtEnumStruct(ZhttpAPI, ClientEventType, int8_t,
  Selected, AttemptFailed, Redirected, Retried, Fallback, Completed,
  Cancelled);

struct ClientEvent {
  uint64_t	request = 0;
  uint64_t	attempt = 0;
  uint64_t	previousAttempt = 0;
  uint32_t	status = 0;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  ClientEventType::T type = ClientEventType::Selected;
  ResultCode::T	result = ResultCode::OK;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  EndpointSource::T endpointSource = EndpointSource::Origin;
  bool		transient = false;
  bool		responseStarted = false;
};

class ClientConfig {
public:
  unsigned concurrency() const { return m_concurrency; }
  unsigned requestTimeout() const { return m_requestTimeout; }
  unsigned maxRedirects() const { return m_maxRedirects; }
  unsigned maxRetries() const { return m_maxRetries; }
  unsigned maxOrigins() const { return m_maxOrigins; }
  unsigned maxAltSvc() const { return m_maxAltSvc; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
  const DiscoveryLimits &discoveryLimits() const {
    return m_discoveryLimits;
  }
  ProtocolPolicy::T protocol() const { return m_protocol; }
  H2Policy::T h2Policy() const { return m_h2Policy; }
  bool blindH3() const { return m_blindH3; }
  bool altSvcCrossHost() const { return m_altSvcCrossHost; }
  bool tcp() const { return m_tcp; }
  bool tls() const { return m_tls; }
  bool quic() const { return m_quic; }

  ClientConfig &concurrency(unsigned v) {
    m_concurrency = v;
    return *this;
  }
  ClientConfig &requestTimeout(unsigned v) {
    m_requestTimeout = v;
    return *this;
  }
  ClientConfig &maxRedirects(unsigned v) {
    m_maxRedirects = v;
    return *this;
  }
  ClientConfig &maxRetries(unsigned v) {
    m_maxRetries = v;
    return *this;
  }
  ClientConfig &maxOrigins(unsigned v) {
    m_maxOrigins = v;
    return *this;
  }
  ClientConfig &maxAltSvc(unsigned v) {
    m_maxAltSvc = v;
    return *this;
  }
  ClientConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ClientConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    return *this;
  }
  ClientConfig &discoveryLimits(DiscoveryLimits v) {
    m_discoveryLimits = v;
    return *this;
  }
  ClientConfig &protocol(ProtocolPolicy::T v) {
    m_protocol = v;
    return *this;
  }
  ClientConfig &h2Policy(H2Policy::T v) {
    m_h2Policy = v;
    return *this;
  }
  ClientConfig &blindH3(bool v) { m_blindH3 = v; return *this; }
  ClientConfig &altSvcCrossHost(bool v) {
    m_altSvcCrossHost = v;
    return *this;
  }
  ClientConfig &tcp(bool v) { m_tcp = v; return *this; }
  ClientConfig &tls(bool v) { m_tls = v; return *this; }
  ClientConfig &quic(bool v) { m_quic = v; return *this; }

private:
  unsigned	m_concurrency = 1;
  unsigned	m_requestTimeout = 0;
  unsigned	m_maxRedirects = 8;
  unsigned	m_maxRetries = 0;
  unsigned	m_maxOrigins = 256;
  unsigned	m_maxAltSvc = 8;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  DiscoveryLimits m_discoveryLimits;
  ProtocolPolicy::T m_protocol = ProtocolPolicy::PreferH3;
  H2Policy::T	m_h2Policy = H2Policy::Prefer;
  bool		m_blindH3 = false;
  bool		m_altSvcCrossHost = false;
  bool		m_tcp = true;
  bool		m_tls = true;
  bool		m_quic = true;
};

#if 0
// Extended request Builder contract used by Client. Request must derive from
// ZmObject, and TxQ::Msg must publicly derive from Request (normally by
// configuring the queue with ZmPQueueNode<Request_>, then Request = TxQ::Msg);
// containment should not be used.
//
// Client retains a TxQ::Msg pointer for both queue identity/lifetime and direct
// Request access. Client calls reset() once for every wire request (including
// replay attempts and redirects), then uses the Builder callbacks above. A
// failed connection which emits no request is not a message. All lifecycle
// callbacks are synchronous.
struct Request : public ZmObject, public Builder {
  // Request start line / pseudo-headers. operation() is called exactly once
  // per message; l(method, target).
  template <typename L> void operation(L &&l);
  template <typename L> void host(L &&l);	// l(authority)
  template <typename L> void protocol(L &&l);	// l(value), CONNECT only

  // Monotonic queue identity and discrete-message length.
  uint64_t key() const;
  uint64_t length() const; // returns 1

  // Absolute URL of the submitted request. Client snapshots it on
  // submission; redirected() receives each subsequently accepted URL.
  Zhttp::URLStorage url;

  // Whether the request semantics permit another attempt after a redirect or
  // an unprocessed failure. Called before Client decides to replay.
  bool replayable() const;

  // Whether another Builder pass will reproduce the same request, including
  // identical body bytes. Both replayable() and reproducible() must be true
  // for Client to replay a request.
  bool reproducible() const;

  // A transport connection for the current attempt is ready. Called before
  // reset() and request construction; info identifies the selected transport
  // and negotiated HTTP version.
  void connected(const ConnectedInfo &);

  // The current attempt's connection ended; peer is true when the peer
  // initiated the disconnect. No callback is made without a bound request.
  void disconnected(bool peer);

  // Connection establishment failed. transient classifies whether Client
  // may retry subject to its configured limit and the replay predicates.
  void connectFailed(bool transient);

  // Client selected a concrete endpoint for the attempt. This precedes
  // connection establishment and may occur more than once across attempts.
  void selected(const Endpoint &);

  // Client accepted a redirect to url. Update any request construction state
  // which operation(), host(), protocol(), or header() derives from the URL.
  void redirected(const URL &);

  // Reports each typed attempt/request transition. Multiple observations may
  // precede the single terminal completed() callback.
  void observed(const ClientEvent &);

  // Exactly one terminal result for the submitted request, after its final
  // observed Completed or Cancelled/Completed transition.
  void completed(const Result &);
};

// Extended response Parser contract used by Client. One ResParser is
// constructed for each reusable ClientMessage stream. init() is called once
// before every response message, including the first, and before status(),
// header(), or body(); it clears per-response state and binds the response to
// its submitted request. init() calls Parser::reset() itself when that reset
// is needed; Client does not call Parser::reset() in addition to init(). The
// same object can therefore serve many messages.
struct ResParser : Parser {
  void init(const Request_ &request);
};
#endif

// Protocol-neutral client message adapter.  App supplies request intent and
// response handling; HTTP-version-specific builders, parsers, EOF rules, and
// link completion remain library-owned.
template <
  typename App_, typename LiveReq_, typename Link_, typename Profile_,
  typename Request_, typename ResParser_>
class ClientMessage {
public:
  using App = App_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using Link = Link_;
  using Profile = Profile_;
  using ResParser = ResParser_;
  using ReqHeaders = typename Request::Headers;
  using RespHeaders = typename ResParser::Headers;
  using ReqTrailers = typename BuilderTrailers<Request>::T;
  using Message = MessageTraits<Profile>;
  using BodyPolicy = typename Request::BodyPolicy;
  using ReqHeaderKeys = ZuTypeSlice<2, 0, ReqHeaders>;
  enum {
    ReqBody = BodyPolicy::HasBody,
    ReqStreaming = BodyPolicy::Streaming,
    ReqOptional = BodyPolicy::Optional
  };
  static constexpr uint64_t RespBodyMax = ParserBodyMax<ResParser>::V;
  ZuAssert((ReqStreaming || !ReqBody ||
    ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{}),
    "fixed request body requires content-length in Headers");
  ZuAssert((!ReqStreaming ||
    !ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{}),
    "streaming request body cannot declare content-length");
  ZuAssert((
    !ZuTypeIn<ZuStringT<"transfer-encoding">, ReqHeaderKeys>{}),
    "libZhttp owns request transfer-encoding framing");

private:
  struct ReqOps {
    ReqOps(
      Request &app_, bool operationCached_ = false,
      Method::T method_ = Method::GET, ZuCSpan target_ = {}) :
      app{&app_}, target{target_}, method{method_},
      operationCached{operationCached_} { }

    template <typename L>
    void operation(L &&l) {
      if (operationCached)
	l(method, target);
      else
	app->operation(ZuFwd<L>(l));
    }
    template <typename L>
    void host(L &&l) { app->host(ZuFwd<L>(l)); }
    template <typename L>
    void protocol(L &&l) { app->protocol(ZuFwd<L>(l)); }
    template <typename Key, typename L>
    void header(L &&l) {
      if (suppressPads) {
	app->template header<Key>([&l]<typename V>(V &&v) {
	  if constexpr (!IsPlaceholder<ZuDecay<V>>{})
	    l(ZuFwd<V>(v));
	});
	return;
      }
      patches.template header<Message::ID == Version::H1, Key>(
	*app, ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      app->header([this, &l]<typename K, typename V>(K &&k, V &&v) {
	ZtString<ZtStringHeapID<"Zhttp.RuntimeHeader.Name">> name;
	name << k;
	if (!validRuntimeHeader<ReqHeaders>(
	      ZuCSpan{name}, Message::ID == Version::H1)) {
	  headersOK = false;
	  return;
	}
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
    }

    void patch() { patches.patch(*app); }
    uint64_t contentLength() const { return produced; }
    bool headersValid() const { return headersOK; }
    Request &appBuilder() { return *app; }
    template <typename Emit>
    void emitBody(Emit &&emit) { app->body(ZuFwd<Emit>(emit)); }

    Request		*app = nullptr;
    HeaderPatches<ReqHeaders> patches;
    ZuCSpan		target;
    uint64_t		produced = 0;
    Method::T		method = Method::GET;
    bool		headersOK = true;
    bool		suppressPads = false;
    bool		operationCached = false;
  };

  template <bool HasBody, bool Streaming>
  struct Builder_ :
    public Message::template Request<
      Builder_<HasBody, Streaming>,
      ReqHeaders, ReqTrailers, HasBody, Streaming>,
    public ReqOps {
    using Base = typename Message::template Request<
      Builder_, ReqHeaders, ReqTrailers, HasBody, Streaming>;
    enum { Optional = ReqOptional };

    Builder_(
      Request &app_, bool suppressPads = false,
      bool operationCached = false, Method::T method = Method::GET,
      ZuCSpan target = {}) :
      ReqOps{app_, operationCached, method, target} {
      this->suppressPads = suppressPads;
    }

    using ReqOps::contentLength;
    using ReqOps::emitBody;
    using ReqOps::header;
    using ReqOps::host;
    using ReqOps::operation;
    using ReqOps::protocol;
  };

  struct Parser;

  struct ParserSink_ {
    using Protocol = typename Message::template ResponseParser<
      Parser, RespHeaders, RespBodyMax>;
    using State = typename Protocol::State;

    void operation(Method::T, const RequestTarget &) { }
    void status(unsigned value) {
      app->status(*link, *request, sink(), value);
    }
    void contentLength(uint64_t value) {
      app->contentLength(*link, *request, sink(), value);
    }
    void chunked() { app->chunked(*link, *request, sink()); }
    void version(ZuBSpan value) {
      app->version(*link, *request, sink(), value);
    }
    template <typename Key>
    void header(ZuBSpan value) {
      app->template header<Key>(*link, *request, sink(), value);
    }
    template <typename Rx>
    void body(Rx &rx) {
      app->body(*link, *request, sink(), rx);
    }
    void complete(typename State::T state) {
      app->template complete<State>(
	*link, *request, sink(), state);
    }

    ResParser &sink() { return *sink_; }

    App		*app = nullptr;
    Link	*link = nullptr;
    LiveReq	*request = nullptr;
    ResParser	*sink_ = nullptr;
  };

  struct Parser :
    public Message::template ResponseParser<
      Parser, RespHeaders, RespBodyMax>,
    public ParserSink_ {
    using Base = typename Message::template ResponseParser<
      Parser, RespHeaders, RespBodyMax>;
    using State = typename Base::State;

    using ParserSink_::body;
    using ParserSink_::chunked;
    using ParserSink_::complete;
    using ParserSink_::contentLength;
    using ParserSink_::header;
    using ParserSink_::operation;
    using ParserSink_::status;
    using ParserSink_::version;
  };

public:
  using ParserState = typename Parser::State;

  ClientMessage(App *app = nullptr, Link *link = nullptr) :
    m_app{app}, m_link{link} { }

  void bind(LiveReq *request) {
    m_request = request;
    if (!request) return;
    m_requestApp = request->request;
    m_requestApp->reset();
    if constexpr (Message::ID != Version::H1) {
      m_operationOK = false;
      m_requestTarget.length(0);
      unsigned operations = 0;
      m_requestApp->operation(
	[this, &operations](Method::T method, auto &&target) {
	  if (++operations != 1) return;
	  m_requestMethod = method;
	  m_requestTarget << ZuFwd<decltype(target)>(target);
	  m_operationOK = true;
	});
      if (operations != 1) m_operationOK = false;
    }
    m_response.init(*m_requestApp);
    static_cast<ParserSink_ &>(m_parser) = {
      m_app, m_link, request, &m_response};
  }
  void reset() {
    m_parser.reset();
    if constexpr (Message::ID != Version::H1) {
      if (!m_request || !m_operationOK) return;
      m_parser.requestMethod(m_requestMethod);
    }
  }

  // Tx-owned synchronous request construction.
  void beginTx() {
    m_commit = {};
    m_txState = TxState::Active;
  }

  void cancelTx() {
    if (m_txState != TxState::Active) return;
    m_commit.reset = m_commit.committed;
    m_txState = TxState::Cancelled;
  }

  BodyCommit commit() const { return m_commit; }

  bool send() {
    if (m_txState != TxState::Active) return false;
    if constexpr (Message::ID != Version::H1)
      if (!m_operationOK) return failTx_();
    return sendApp_(*m_requestApp);
  }

private:
  bool sendApp_(Request &app) {
    if constexpr (!ReqBody)
      return sendEmpty_(app);
    else if constexpr (ReqStreaming)
      return sendStreaming_(app);
    else
      return sendFixed_(app);
  }

  struct TxOps {
    ClientMessage *owner;

    Link &link() { return *owner->m_link; }
    uint64_t fixedBodyMax() const { return owner->fixedBodyMax_(); }
    uint64_t retainedMax() const {
      return owner->m_app->retainedMessageMax();
    }
    void headers() { owner->m_commit.headers = true; }
    template <bool Streaming>
    void produced(uint64_t n) {
      owner->m_commit.produced = n;
      if constexpr (Streaming) owner->m_commit.committed = n;
    }
    bool empty(Request &app) { return owner->sendEmpty_(app, true); }
    template <bool Streaming>
    bool fail() { return owner->template failFor_<Streaming>(); }
    bool complete(uint64_t n) {
      owner->m_commit.produced = n;
      owner->m_commit.committed = n;
      owner->m_commit.final = true;
      owner->m_txState = TxState::Complete;
      return true;
    }
  };

  bool sendStreaming_(Request &app) {
    Builder_<true, true> builder{
      app, false, m_operationOK, m_requestMethod, m_requestTarget};
    TxOps ops{this};
    return MessageTx<Message, TxOps>{ops}.streaming(builder);
  }

  bool sendEmpty_(Request &app, bool suppressPads = false) {
    Builder_<false, false> builder{
      app, suppressPads,
      m_operationOK, m_requestMethod, m_requestTarget};
    auto tx = m_link->transmit(builder);
    if (!builder.begin(tx) || !builder.headersValid())
      return failTx_();
    m_commit.headers = true;
    builder.finish(tx);
    m_link->finish();
    m_commit.final = true;
    m_txState = TxState::Complete;
    return true;
  }

  bool sendFixed_(Request &app) {
    Builder_<true, false> builder{
      app, false, m_operationOK, m_requestMethod, m_requestTarget};
    TxOps ops{this};
    return MessageTx<Message, TxOps>{ops}.fixed(builder);
  }

public:
  template <typename Rx>
  int process(Rx &rx) {
    auto state = m_link->receive(m_parser, rx);
    if (state == ParserState::Error) return -1;
    if (state == ParserState::Complete) return 1;
    if (m_app->done(*m_request)) return -1;
    if constexpr (Message::ID == Version::H1)
      return m_parser.progressed();
    return 0;
  }

  void eof() {
    if constexpr (Message::CloseDelimited) m_parser.eof();
  }

private:
  uint64_t fixedBodyMax_() const {
    uint64_t n = m_app->retainedBodyMax();
    if (n > m_app->retainedMessageMax())
      n = m_app->retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
  }

  struct TxState {
    enum { Idle, Active, Complete, Failed, Cancelled };
  };

  bool failTx_() {
    m_commit.discarded = m_commit.produced;
    m_txState = TxState::Failed;
    return false;
  }

  bool failStreamedTx_() {
    m_commit.discarded = m_commit.produced > m_commit.committed ?
      m_commit.produced - m_commit.committed : 0;
    m_commit.reset = m_commit.committed;
    m_link->disconnect();
    m_txState = TxState::Failed;
    return false;
  }

  template <bool Streaming>
  bool failFor_() {
    if constexpr (Streaming)
      return failStreamedTx_();
    else
      return failTx_();
  }

  App		*m_app = nullptr;
  Link		*m_link = nullptr;
  LiveReq	*m_request = nullptr;
  Request	*m_requestApp = nullptr;
  Parser	m_parser;
  ResParser	m_response;
  ZtString<ZtStringHeapID<"Zhttp.RequestTarget">> m_requestTarget;
  BodyCommit	m_commit;
  Method::T	m_requestMethod = Method::GET;
  int8_t	m_txState = TxState::Idle;
  bool		m_operationOK = false;
};



template <typename App, typename Profile>
class ClientHub :
  public ProfileTraits<Profile>::Transport::template Client<App> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;

public:
  using Base = typename Traits::template Client<App>;
  using Base::init;
  enum {
    TLS = Traits::Secure,
    Multiplexed = HTTP::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const HubConfig &hub, const typename Traits::Config &config) {
    return Base::init(Traits::clientParams(hub, config));
  }

  unsigned reconnFreq() const { return 0; }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }
};


namespace H2_ {

template <typename App> class ClientHub;
template <typename App> class CliLink;

struct ClientSlot {
  ZmContext	owner;
  void		(*close)(void *) = nullptr;
  bool		(*available)(void *) = nullptr;
  bool		(*down)(void *) = nullptr;
  Ztls::Host	host;
  uint16_t	port = 0;
};

struct ClientPoolKey {
  Ztls::Host	host;
  uint16_t	port = 0;

  bool equals(const ClientPoolKey &key) const {
    return port == key.port && host == key.host;
  }
  friend bool operator ==(
    const ClientPoolKey &l, const ClientPoolKey &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    return ZuHash<Ztls::Host>::hash(host) ^ uint32_t(port);
  }
};

struct ClientPoolEntry {
  ClientPoolKey	key;
  ZmContext	owner;
};

inline const ClientPoolKey &ClientPoolEntry_KeyAxor(
  const ClientPoolEntry &entry)
{
  return entry.key;
}

ZuDerive(ClientPoolHash,
  (ZmHash<ClientPoolEntry,
    ZmHashNode<ClientPoolEntry,
      ZmHashKey<ClientPoolEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zhttp.H2">>>>>));

template <typename App>
class ClientHub : public Ztls::Client<ClientHub<App>> {
public:
  using Base = Ztls::Client<ClientHub>;
  using Link = CliLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ClientSlot,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientHub() : m_pool{new ClientPoolHash} { }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::clientParams(hub, config);
    return Base::init(ZuMv(params));
  }

  template <typename Logical>
  void connect(Logical *logical, Ztls::Host host, uint16_t port) {
    ZmRef<Logical> logical_ = ZmMkRef(logical);
    this->rxInvoke([
      this, logical = ZuMv(logical_), host = ZuMv(host), port
    ]() mutable {
      ClientPoolKey key{host, port};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) {
	link = poolEntry->owner.object<Link>();
	if (!link->available()) link = nullptr;
      }
      if (!link) {
	link = new Link{this, host, port, m_config};
	m_slots.push(ClientSlot{
	  .owner = link,
	  .close = [](void *ptr) {
	    static_cast<Link *>(ptr)->beginStop();
	  },
	  .available = [](void *ptr) {
	    return static_cast<Link *>(ptr)->available();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Link *>(ptr)->isDown();
	  },
	  .host = host,
	  .port = port
	});
	if (poolEntry)
	  poolEntry->owner = link;
	else
	  m_pool->add(ClientPoolEntry{ZuMv(key), link});
	link->add(ZuMv(logical));
	link->connect(ZuMv(host), port);
	return;
      }
      link->add(ZuMv(logical));
    });
  }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      m_stopPending = 0;
      for (auto &slot: m_slots)
	if (!slot.down(slot.owner.object<void>())) ++m_stopPending;
      if (!m_stopPending) {
	stopDrain_();
	return;
      }
      for (auto &slot: m_slots)
	if (!slot.down(slot.owner.object<void>()))
	  slot.close(slot.owner.object<void>());
    });
  }
  void linkDown(CliLink<App> *link) {
    if (auto entry = m_pool->findPtr(
	ClientPoolKey{link->host(), link->port()}))
      if (entry->owner.object<CliLink<App>>() == link)
	m_pool->delNode(
	  static_cast<ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].owner.object<CliLink<App>>() == link) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots)
      ZmAssert(slot.down(slot.owner.object<void>()));
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }
  const H2Config &h2Config() const { return m_config; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  Slots		m_slots;
  ZmRef<ClientPoolHash> m_pool;
  StopFns	m_stopFns;
  H2Config	m_config;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class CliLink :
  public Ztls::CliLink<ClientHub<App>, CliLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<CliLink<App>, typename App::Link> {
public:
  using Hub = ClientHub<App>;
  using Logical = typename App::Link;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<CliLink, Logical>;
  using Pending = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host_, uint16_t port_,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host_)}, m_port{port_}
  {
    Wire_::initWire(false, config);
    m_pendingMax = config.maxPending();
  }
  ~CliLink() {
    Wire_::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  void add(ZmRef<Logical> logical) {
    if (pendingCount_() >= m_pendingMax &&
	(!m_ready || !Wire_::canOpenLocalStream())) {
      logical->connectFailed_(false);
      return;
    }
    logical->native(this);
    if (!m_ready || pending_() || !Wire_::canOpenLocalStream()) {
      compactPending_();
      m_pending.push(ZuMv(logical));
      return;
    }
    openNow_(ZuMv(logical));
  }
  bool available() const {
    return !m_down && !m_draining &&
      !Wire_::localStreamsExhausted() &&
      (m_ready && Wire_::canOpenLocalStream() ||
	pendingCount_() < m_pendingMax);
  }
  bool isDown() const { return m_down; }
  void connected(Ztls::Connected info) {
    if (info.alpn != "h2") {
      connectFailed(false);
      return;
    }
    Wire_::sendInitial();
  }
  void h2SettingsReceived() {
    m_ready = true;
    admit_();
  }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    Active active;
    Wire_::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire_::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (unsigned i = link->m_pendingHead;
	  i < link->m_pending.length(); ++i) {
	auto &logical = link->m_pending[i];
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Unprocessed);
	logical->connectFailed_(false);
      }
      link->m_pending.length(0);
      link->m_pendingHead = 0;
      link->app()->linkDown(link);
    });
  }
  void connectFailed(bool transient) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    for (unsigned i = m_pendingHead; i < m_pending.length(); ++i) {
      auto &logical = m_pending[i];
      if (logical->result() == ResultCode::OK)
	logical->result_(ResultCode::Unprocessed);
      logical->connectFailed_(transient);
    }
    m_pending.length(0);
    m_pendingHead = 0;
    Base::disconnect();
    this->app()->linkDown(this);
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  void disconnectNative() { Base::disconnect(); }

  void openNow_(ZmRef<Logical> logical) {
    auto entry = Wire_::openLocalStream(logical);
    if (!entry) {
      logical->connectFailed_(false);
      logical->native({});
      return;
    }
    logical->stream(entry->id);
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
  }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<CliLink>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }
  void close(Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      link->rst_(id, Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, Error::T) { notify_(id, true); }
  void h2Cancel(uint32_t id) {
    rst_(id, Error::Cancel);
    notify_(id, false);
  }
  auto h2OpenPeer(uint32_t) -> Stream<Logical> * { return nullptr; }
  bool h2Closed(uint32_t id) const { return Wire_::streamClosed(id); }
  Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ? Error::StreamClosed : Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire_::allStreams([last, &close](auto &entry) {
      if (entry.id > last) {
	entry.logical->result_(ResultCode::Unprocessed);
	close.push(entry.id);
      }
    });
    for (auto id: close) notify_(id, true);
    requeue_();
  }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    switch (key) {
      case Setting::MaxConcurrentStreams:
	Wire_::localStreamMax(value);
	admit_();
	break;
      case Setting::InitialWindowSize:
	if (!Wire_::peerInitialWindow(value))
	  this->h2Error(Error::FlowControlError);
	break;
      default:
	break;
    }
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire_::allStreams([](auto &entry) {
      entry.logical->result_(ResultCode::Cancelled);
    });
    for (unsigned i = m_pendingHead; i < m_pending.length(); ++i)
      m_pending[i]->result_(ResultCode::Cancelled);
    Wire_::stopWire();
    Wire_::graceful();
    Base::disconnect();
  }

private:
  bool pending_() const { return m_pendingHead < m_pending.length(); }
  unsigned pendingCount_() const {
    return m_pending.length() - m_pendingHead;
  }
  void compactPending_() {
    if (!m_pendingHead) return;
    unsigned n = pendingCount_();
    for (unsigned i = 0; i < n; ++i)
      m_pending[i] = ZuMv(m_pending[m_pendingHead + i]);
    m_pending.length(n);
    m_pendingHead = 0;
  }
  void admit_() {
    if (!m_ready) return;
    while (pending_() && Wire_::canOpenLocalStream())
      openNow_(ZuMv(m_pending[m_pendingHead++]));
    if (!pending_()) {
      m_pending.length(0);
      m_pendingHead = 0;
    }
    if (Wire_::localStreamsExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!pending_() || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      link = this]() { link->requeueNow_(); });
  }
  void requeueNow_() {
    unsigned n = pendingCount_();
    if (n > H2_::RequeueBatch) n = H2_::RequeueBatch;
    for (unsigned i = 0; i < n; ++i) {
      auto logical = ZuMv(m_pending[m_pendingHead++]);
      if (m_stopping) {
	logical->result_(ResultCode::Cancelled);
	logical->connectFailed_(false);
      } else {
	logical->connect(m_host, m_port);
      }
    }
    if (pending_()) {
      this->app()->rxRun([
	link = this]() { link->requeueNow_(); });
    } else {
      m_pending.length(0);
      m_pendingHead = 0;
      m_requeuePosted = false;
    }
  }
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, Error::T error) {
    auto tx = Base::txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4, .streamID = id, .type = FrameType::RSTStream
    });
    putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire_::removeStream(id, [
      link = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      link->admit_();
    });
  }

  Pending	m_pending;
  unsigned	m_pendingHead = 0;
  Ztls::Host	m_host;
  uint16_t	m_port = 0;
  uint32_t	m_pendingMax = 0;
  bool		m_ready = false;
  bool		m_down = false;
  bool		m_draining = false;
  bool		m_stopping = false;
  bool		m_requeuePosted = false;
};

template <typename App, typename Impl, typename NativeLink>
class ClientLogical : public ZmObject, public LogicalStream<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ClientLogical(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }
  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Host>
  void connect(Host &&host, uint16_t port) {
    prepare_();
    m_app->connect(
      impl(), Ztls::Host{ZuFwd<Host>(host)}, port);
  }
  void prepare_() {
    m_cancelled = false;
    m_connected = false;
    m_failed = false;
    m_result = ResultCode::OK;
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.target, endpoint.port);
  }
  auto txStream() { return m_native->logicalTx(m_streamID); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native && m_streamID)
      m_native->logicalTxErrorFn(m_streamID, m_txErrorFn);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  bool active() const { return m_native && m_streamID; }
  void disconnect() {
    m_cancelled = true;
    if (m_native && m_streamID)
      m_native->close(impl(), m_streamID);
  }
  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    m_native = nullptr;
    m_streamID = 0;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_native = nullptr;
    m_streamID = 0;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  template <typename Rx>
  int process_(Rx &rx) { return m_app->process(*impl(), rx); }
  void native(NativeLink *native) { m_native = native; }
  void stream(uint32_t id) {
    m_streamID = id;
    if (m_native && m_streamID)
      m_native->logicalTxErrorFn(m_streamID, m_txErrorFn);
  }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  ZiTxErrorFn	m_txErrorFn;
  uint32_t	m_streamID = 0;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  int8_t	m_result = ResultCode::OK;
};

} // namespace H2_

template <typename App, typename Impl>
class ClientLink<App, Impl, H2TLS> :
  public H2_::ClientLogical<App, Impl, H2_::CliLink<App>> {
  using Base =
    H2_::ClientLogical<App, Impl, H2_::CliLink<App>>;

public:
  using Base::Base;
};

template <typename App>
class ClientHub<App, H2TLS> : public H2_::ClientHub<App> {
public:
  using Base = H2_::ClientHub<App>;
  enum { TLS = 1, Multiplexed = 1 };
  using Base::connect;
  using Base::init;

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }
};

namespace TLS_ {

template <typename App, typename H1Logical, typename H2Logical>
class ClientHub;
template <typename App, typename H1Logical, typename H2Logical>
class CliLink;

template <typename App, typename Impl, typename NativeLink>
class ClientH1Logical : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ClientH1Logical(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  auto txStream() { return m_native->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native) m_native->txErrorFn(m_txErrorFn);
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  void finish() { }
  template <typename Done>
  void complete(Done &&done) {
    if (m_native)
      m_native->releaseH1(impl(), ZuFwd<Done>(done));
    else
      done();
  }
  bool active() const { return m_native; }
  void prepare_() {
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    m_result = ResultCode::OK;
  }
  void disconnect() {
    m_cancelled = true;
    if (m_native) m_native->disconnectNative();
  }
  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    m_native = nullptr;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_native = nullptr;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  int process_(Ztls::RxStream &rx) {
    return m_app->process(*impl(), rx);
  }
  void native(NativeLink *native) {
    m_native = native;
    if (m_native) m_native->txErrorFn(m_txErrorFn);
  }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  ZiTxErrorFn	m_txErrorFn;
  bool		m_connected = false;
  bool		m_failed = false;
  bool		m_cancelled = false;
  int8_t	m_result = ResultCode::OK;
};

template <typename H1Logical, typename H2Logical>
struct ClientChoice {
  ZmRef<H1Logical>	h1;
  ZmRef<H2Logical>	h2;
};

template <typename App, typename H1Logical_, typename H2Logical_>
class ClientHub :
  public Ztls::Client<ClientHub<App, H1Logical_, H2Logical_>> {
public:
  using Base = Ztls::Client<ClientHub>;
  using Link = CliLink<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ZmRef<Link>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  ClientHub() : m_pool{new H2_::ClientPoolHash} { }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::clientParams(hub, config));
  }
  void connect(
    H1Logical *h1, H2Logical *h2, Ztls::Host host, uint16_t port) {
    ZmRef<H1Logical> h1_ = ZmMkRef(h1);
    ZmRef<H2Logical> h2_ = ZmMkRef(h2);
    h1_->prepare_();
    h2_->prepare_();
    this->rxInvoke([
      this, h1 = ZuMv(h1_), h2 = ZuMv(h2_),
      host = ZuMv(host), port
    ]() mutable {
      H2_::ClientPoolKey key{host, port};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) {
	link = poolEntry->owner.object<Link>();
	if (!link->available()) link = nullptr;
      }
      if (!link) {
	link = new Link{this, host, port, m_config};
	m_slots.push(link);
	if (poolEntry)
	  poolEntry->owner = link;
	else
	  m_pool->add(H2_::ClientPoolEntry{ZuMv(key), link});
	link->add({ZuMv(h1), ZuMv(h2)});
	link->connect(ZuMv(host), port);
	return;
      }
      link->add({ZuMv(h1), ZuMv(h2)});
    });
  }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      m_stopPending = 0;
      for (auto &slot: m_slots)
	if (!slot->isDown()) ++m_stopPending;
      if (!m_stopPending) {
	stopDrain_();
	return;
      }
      for (auto &slot: m_slots)
	if (!slot->isDown()) slot->beginStop();
    });
  }
  void linkDown(Link *link) {
    if (auto entry = m_pool->findPtr(
	H2_::ClientPoolKey{link->host(), link->port()}))
      if (entry->owner.object<Link>() == link)
	m_pool->delNode(
	  static_cast<H2_::ClientPoolHash::Node *>(entry));
    for (unsigned i = 0; i < m_slots.length(); ++i)
      if (m_slots[i].ptr() == link) {
	m_slots.splice(i, 1);
	break;
      }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void goaway(uint32_t) { }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots) ZmAssert(slot->isDown());
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
    });
  }
  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  Slots		m_slots;
  ZmRef<H2_::ClientPoolHash> m_pool;
  StopFns	m_stopFns;
  H2Config	m_config;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App, typename H1Logical_, typename H2Logical_>
class CliLink :
  public Ztls::CliLink<
    ClientHub<App, H1Logical_, H2Logical_>,
    CliLink<App, H1Logical_, H2Logical_>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<
    CliLink<App, H1Logical_, H2Logical_>, H2Logical_> {
public:
  using Hub = ClientHub<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using Choice = ClientChoice<H1Logical, H2Logical>;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<CliLink, H2Logical>;
  using Pending = ZtArray<Choice,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StreamIDs = ZtArray<uint32_t,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host, uint16_t port,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host)}, m_port{port},
      m_pendingMax{config.maxPending()}, m_policy{config.policy()}
  {
    Wire::initWire(false, config);
  }
  ~CliLink() {
    Wire::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  bool isDown() const { return m_down; }
  bool available() const {
    if (m_down || m_stopping || m_draining) return false;
    if (!m_ready)
      return m_policy == H2Policy::Force &&
	pendingCount_() < m_pendingMax;
    switch (m_version) {
      case Version::H1:
      return !m_h1 || pendingCount_() < m_pendingMax;
      case Version::H2:
	return !Wire::localStreamsExhausted() &&
	  (Wire::canOpenLocalStream() ||
	    pendingCount_() < m_pendingMax);
      default:
	return false;
    }
  }
  void add(Choice choice) {
    if (pendingCount_() >= m_pendingMax &&
	(!m_ready || m_version != Version::H2 ||
	 !Wire::canOpenLocalStream())) {
      fail_(choice, false);
      return;
    }
    if (!m_ready || pending_() ||
	m_version == Version::H1 && m_h1 ||
	m_version == Version::H2 && !Wire::canOpenLocalStream()) {
      compactPending_();
      m_pending.push(ZuMv(choice));
      return;
    }
    open_(ZuMv(choice));
  }
  void connected(Ztls::Connected info) {
    m_version = TLS_::version(info.alpn, m_policy);
    m_tlsVersion = info.version;
    switch (m_version) {
      case Version::H1:
	m_ready = true;
	admit_();
	break;
      case Version::H2:
	Wire::sendInitial();
	break;
      default:
	connectFailed(false);
	break;
    }
  }
  void h2SettingsReceived() {
    if (m_version != Version::H2) return;
    m_ready = true;
    admit_();
  }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    if (m_h1) {
      if (m_h1->result() == ResultCode::OK)
	m_h1->result_(ResultCode::Indeterminate);
      m_h1->disconnected_(peer);
      m_h1 = nullptr;
    }
    Active active;
    if (m_version == Version::H2)
      Wire::allStreams([&active](auto &entry) {
	if (!entry.notified) active.push(entry.logical);
      });
    Wire::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) {
	if (logical->result() == ResultCode::OK)
	  logical->result_(ResultCode::Indeterminate);
	logical->disconnected_(peer);
      }
      for (unsigned i = link->m_pendingHead;
	  i < link->m_pending.length(); ++i)
	link->fail_(link->m_pending[i], false);
      link->m_pending.length(0);
      link->m_pendingHead = 0;
      link->app()->linkDown(link);
    });
  }
  void connectFailed(bool transient) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    for (unsigned i = m_pendingHead; i < m_pending.length(); ++i)
      fail_(m_pending[i], transient);
    m_pending.length(0);
    m_pendingHead = 0;
    Base::disconnect();
    this->app()->linkDown(this);
  }
  int process(Ztls::RxStream &rx) {
    switch (m_version) {
      case Version::H1: return m_h1 ? m_h1->process_(rx) : -1;
      case Version::H2: return Wire::process(rx);
      default: return -1;
    }
  }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return H2_::HeaderBlock<CliLink>{
      *this, this->encoder(), id, this->peerFrameSize()};
  }
  void disconnectNative() { Base::disconnect(); }
  template <typename Done>
  void releaseH1(H1Logical *logical, Done &&done) {
    this->app()->rxRun([
      link = this, logical = ZmMkRef(logical),
      done = ZuFwd<Done>(done)
    ]() mutable {
      if (link->m_h1.ptr() == logical.ptr()) {
	link->m_h1->disconnected_(false);
	link->m_h1 = nullptr;
	link->admit_();
      }
      done();
    });
  }

  void close(H2Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmMkRef(logical), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->logical.ptr() != logical.ptr()) return;
      link->rst_(id, H2::Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, H2::Error::T) {
    notify_(id, true);
  }
  void h2Cancel(uint32_t id) {
    rst_(id, H2::Error::Cancel);
    notify_(id, false);
  }
  auto h2OpenPeer(uint32_t) -> H2_::Stream<H2Logical> * {
    return nullptr;
  }
  bool h2Closed(uint32_t id) const { return Wire::streamClosed(id); }
  H2::Error::T h2OpenError(uint32_t id) const {
    return h2Closed(id) ?
      H2::Error::StreamClosed : H2::Error::ProtocolError;
  }
  void h2Goaway_(uint32_t last, H2::Error::T) {
    m_draining = true;
    StreamIDs close;
    Wire::allStreams([last, &close](auto &entry) {
      if (entry.id > last) {
	entry.logical->result_(ResultCode::Unprocessed);
	close.push(entry.id);
      }
    });
    for (auto id: close) notify_(id, true);
    requeue_();
    this->app()->user()->goaway(last);
  }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    switch (key) {
      case H2::Setting::MaxConcurrentStreams:
	Wire::localStreamMax(value);
	admit_();
	break;
      case H2::Setting::InitialWindowSize:
	if (!Wire::peerInitialWindow(value))
	  this->h2Error(H2::Error::FlowControlError);
	break;
      default:
	break;
    }
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    if (m_h1) m_h1->result_(ResultCode::Cancelled);
    Wire::allStreams([](auto &entry) {
      entry.logical->result_(ResultCode::Cancelled);
    });
    for (unsigned i = m_pendingHead; i < m_pending.length(); ++i) {
      auto &choice = m_pending[i];
      switch (m_policy) {
	case H2Policy::Disable:
	  choice.h1->result_(ResultCode::Cancelled);
	  break;
	default:
	  choice.h2->result_(ResultCode::Cancelled);
	  break;
      }
    }
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
  bool pending_() const { return m_pendingHead < m_pending.length(); }
  unsigned pendingCount_() const {
    return m_pending.length() - m_pendingHead;
  }
  void compactPending_() {
    if (!m_pendingHead) return;
    unsigned n = pendingCount_();
    for (unsigned i = 0; i < n; ++i)
      m_pending[i] = ZuMv(m_pending[m_pendingHead + i]);
    m_pending.length(n);
    m_pendingHead = 0;
  }
  void open_(Choice choice) {
    switch (m_version) {
      case Version::H1:
	m_h1 = ZuMv(choice.h1);
	m_h1->native(this);
	m_h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = "http/1.1",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      case Version::H2: {
	auto logical = ZuMv(choice.h2);
	logical->native(this);
	auto entry = Wire::openLocalStream(logical);
	if (!entry) {
	  logical->connectFailed_(false);
	  logical->native({});
	  return;
	}
	logical->stream(entry->id);
	logical->connected_(ProfileTraits<H2TLS>::apply({
	  .alpn = "h2",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      }
    }
  }
  void admit_() {
    if (!m_ready) return;
    switch (m_version) {
      case Version::H1:
	if (!m_h1 && pending_()) {
	  open_(ZuMv(m_pending[m_pendingHead++]));
	}
	break;
      case Version::H2:
	while (pending_() && Wire::canOpenLocalStream())
	  open_(ZuMv(m_pending[m_pendingHead++]));
	break;
    }
    if (!pending_()) {
      m_pending.length(0);
      m_pendingHead = 0;
    }
    if (m_version == Version::H2 && Wire::localStreamsExhausted()) {
      m_draining = true;
      requeue_();
    }
  }
  void requeue_() {
    if (!pending_() || m_requeuePosted) return;
    m_requeuePosted = true;
    this->app()->rxRun([
      link = this]() { link->requeueNow_(); });
  }
  void requeueNow_() {
    unsigned n = pendingCount_();
    if (n > H2_::RequeueBatch) n = H2_::RequeueBatch;
    for (unsigned i = 0; i < n; ++i) {
      auto choice = ZuMv(m_pending[m_pendingHead++]);
      if (m_stopping) {
	switch (m_policy) {
	  case H2Policy::Disable:
	    choice.h1->result_(ResultCode::Cancelled);
	    choice.h1->connectFailed_(false);
	    break;
	  default:
	    choice.h2->result_(ResultCode::Cancelled);
	    choice.h2->connectFailed_(false);
	    break;
	}
      } else {
	this->app()->connect(
	  choice.h1, choice.h2, m_host, m_port);
      }
    }
    if (pending_()) {
      this->app()->rxRun([
	link = this]() { link->requeueNow_(); });
    } else {
      m_pending.length(0);
      m_pendingHead = 0;
      m_requeuePosted = false;
    }
  }
  void fail_(Choice &choice, bool transient) {
    switch (m_policy) {
      case H2Policy::Disable:
	if (choice.h1->result() == ResultCode::OK)
	  choice.h1->result_(ResultCode::Unprocessed);
	choice.h1->connectFailed_(transient);
	break;
      default:
	if (choice.h2->result() == ResultCode::OK)
	  choice.h2->result_(ResultCode::Unprocessed);
	choice.h2->connectFailed_(transient);
	break;
    }
  }
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, H2::Error::T error) {
    auto tx = Base::txStream();
    H2_::StreamBytes<decltype(tx)> sink{tx};
    H2::putHeader(sink, {
      .length = 4, .streamID = id, .type = H2::FrameType::RSTStream
    });
    H2::putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire::removeStream(id, [
      link = this, logical = ZuMv(logical), peer
    ]() mutable {
      logical->disconnected_(peer);
      link->admit_();
    });
  }

  Pending		m_pending;
  unsigned		m_pendingHead = 0;
  ZmRef<H1Logical>	m_h1;
  Ztls::Host		m_host;
  uint16_t		m_port = 0;
  uint32_t		m_pendingMax = 0;
  int			m_tlsVersion = 0;
  int8_t		m_policy = H2Policy::Force;
  int8_t		m_version = -1;
  bool			m_ready = false;
  bool			m_down = false;
  bool			m_draining = false;
  bool			m_stopping = false;
  bool			m_requeuePosted = false;
};

} // namespace TLS_

namespace H3_ {

template <typename App>
class ClientHub;
template <typename App, typename Logical>
struct CliLink;
template <typename App, typename Logical>
struct ClientStream;
struct CliLinkKey {
  Zquic::Host	host;
  ZiIP		remote;
  uint16_t	port = 0;

  friend bool operator ==(const CliLinkKey &l, const CliLinkKey &r) {
    return l.port == r.port && l.host == r.host && l.remote == r.remote;
  }
  uint32_t hash() const {
    return ZuHash<Zquic::Host>::hash(host) ^
      ZuHash<ZiIP>::hash(remote) ^ uint32_t(port);
  }
};

struct CliLinkEntry {
  using CloseFn = void (*)(void *);
  using DownFn = bool (*)(void *);
  using PrintDiagFn = void (*)(void *);

  CliLinkKey		key;
  ZmContext		owner;
  CloseFn		close = nullptr;
  DownFn		down = nullptr;
  PrintDiagFn		printDiag = nullptr;
};

struct CliLinkRetired {
  using SlotFn = void (*)(void *, unsigned);

  ZmContext	owner;
  SlotFn	slot = nullptr;
};

inline const CliLinkKey &CliLinkEntry_KeyAxor(const CliLinkEntry &entry) {
  return entry.key;
}

ZuDerive(CliLinkHash,
  (ZmHash<CliLinkEntry,
    ZmHashNode<CliLinkEntry,
      ZmHashKey<CliLinkEntry_KeyAxor,
	ZmHashLock<ZmNoLock,
	  ZmHashHeapID<"Zhttp.H3.ClientLinks">>>>>));

// App is incomplete while its CRTP base is instantiated.  ZmContext pins the
// protocol-private link; the two function pointers are control-plane only.
template <typename App>
class ClientHub :
  public Zquic::Client<ClientHub<App>>,
  public Faults<ClientHub<App>> {
public:
  using StopFn =
    ZmFn<void(bool), ZmFnHeapID<"Zhttp.H3.ClientStop">>;
  using StopFns =
    ZtArray<StopFn, ZtArrayHeapID<"Zhttp.H3.ClientStopFns">>;
  using Retired =
    ZtArray<CliLinkRetired,
      ZtArrayHeapID<"Zhttp.H3.ClientRetired">>;

  ClientHub() : m_links{new CliLinkHash} { }

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port) {
    connect_(link, ZuMv(host), port, {});
  }
  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port, ZiIP remote) {
    connect_(link, ZuMv(host), port, ZuMv(remote));
  }

private:
  template <typename Logical>
  void connect_(
    Logical *logical_, Zquic::Host host, uint16_t port, ZiIP remote) {
    using Link = CliLink<App, Logical>;
    ZmRef<Logical> logical = ZmMkRef(logical_);
    this->rxInvoke([
      this, logical = ZuMv(logical), host = ZuMv(host),
      remote = ZuMv(remote), port
    ]() mutable {
      ZmRef<Link> link;
      CliLinkKey key{host, remote, port};
      if (auto entry = m_links->findPtr(key))
	link = entry->owner.object<Link>();
      if (!link) {
	link = new Link{this, host, port, remote};
	m_links->add(CliLinkEntry{
	  .key = key,
	  .owner = link,
	  .close = [](void *ptr) {
		    // Hub shutdown must not wait behind an in-flight migration;
		    // abort guarantees endpointDown() and deterministic draining.
		    static_cast<Link *>(ptr)->abort();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Link *>(ptr)->down;
	  },
	  .printDiag = [](void *ptr) {
#ifdef Zquic_DEBUG
	    static_cast<Link *>(ptr)->endpointDiag([](const auto &diag) {
	      H3_::printDiag(diag);
	    });
#else
	    (void)ptr;
#endif
	  }
	});
	link->add(ZuMv(logical));
	link->connect(ZuMv(host), port, ZuMv(remote));
	return;
      }
      link->add(ZuMv(logical));
    });
  }

public:
  unsigned reconnFreq() const { return 0; }

  void printDiag() {
    auto i = m_links->iter();
    while (auto entry = i())
      entry->printDiag(entry->owner.object<void>());
  }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      stopRx_(ZuMv(fn));
    });
  }
  template <typename Link>
  void linkDown(Link *link) {
    if (link->retiredSlot != QueueSlot::Invalid) {
      unsigned slot = link->retiredSlot;
      unsigned last = m_retired.length() - 1;
      ZmAssert(slot <= last);
      if (slot != last) {
	m_retired[slot] = ZuMv(m_retired[last]);
	auto &moved = m_retired[slot];
	moved.slot(moved.owner.object<void>(), slot);
      }
      m_retired.length(last);
      link->retiredSlot = QueueSlot::Invalid;
    }
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopBase_();
  }
  template <typename Link>
  void removeLink(Link *link) {
    if (!link->indexed) return;
    CliLinkKey key{link->host, link->remote, link->port};
    auto entry = m_links->del(key);
    ZmAssert(entry && entry->owner.object<Link>() == link);
    link->indexed = false;
    link->retiredSlot = m_retired.length();
    m_retired.push(CliLinkRetired{
      .owner = ZuMv(entry->owner),
      .slot = [](void *ptr, unsigned slot) {
	static_cast<Link *>(ptr)->retiredSlot = slot;
      }
    });
  }
  void final() {
    this->clearFaults();
    ZmAssert(!m_links->count_());
    ZmAssert(!m_retired.length());
    Zquic::Client<ClientHub>::final();
  }

private:
  ZmRef<CliLinkHash>	m_links;
  Retired		m_retired;

  void stopRx_(StopFn done) {
    m_stopFns.push(ZuMv(done));
    if (m_stopping) return;
    m_stopping = true;
    m_stopPending = 0;
    {
      auto i = m_links->iter();
      while (auto entry = i()) {
	if (entry->down(entry->owner.object<void>())) continue;
	++m_stopPending;
      }
    }
    if (!m_stopPending) {
      stopBase_();
      return;
    }
    auto i = m_links->iter();
    while (auto entry = i()) {
      if (entry->down(entry->owner.object<void>())) continue;
      entry->close(entry->owner.object<void>());
    }
  }

  void stopBase_() {
    Zquic::Client<ClientHub>::stop(
      [this](bool ok) {
	this->rxRun([this, ok]() { stopped_(ok); });
      });
  }

  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App, typename Logical>
struct ClientStream :
  public Zquic::CliStream<
    CliLink<App, Logical>, ClientStream<App, Logical>>,
  public H3::CxnStream<ClientStream<App, Logical>,
    H3::Cxn<CliLink<App, Logical>,
      ZmRef<ClientStream<App, Logical>>>> {
  using Link = CliLink<App, Logical>;
  using Base = Zquic::CliStream<Link, ClientStream>;
  using H3Cxn = H3::Cxn<Link, ZmRef<ClientStream>>;
  using CxnStream = H3::CxnStream<ClientStream, H3Cxn>;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!logical) return -1;
    int rc = logical->process_(rx);
    if (rc < 0) return 0; // parser has scheduled a stream-local reset
    if (this->rxComplete()) this->link()->remoteEnd(this);
    return rc;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

  ZmRef<Logical>	logical;
  uint32_t		slot = QueueSlot::Invalid;
  bool		localEnd = false;
  bool		remoteEnd = false;
  bool		closing = false;
};

template <typename App, typename Logical>
struct CliLink :
  public Zquic::CliLink<ClientHub<App>, CliLink<App, Logical>,
    ClientStream<App, Logical>> {
  using Hub = ClientHub<App>;
  using Stream = ClientStream<App, Logical>;
  using Base = Zquic::CliLink<Hub, CliLink, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<CliLink, StreamRef>;
  using Pending =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientPending">>;
  using Waiting =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientWaiting">>;
  using Streams =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ClientStreams">>;
  using Base::Base;

  CliLink(Hub *app, Zquic::Host host_, uint16_t port_, ZiIP remote_) :
    Base{app}, host{ZuMv(host_)}, remote{ZuMv(remote_)}, port{port_} { }

  unsigned txQueueMax() const {
    return this->app()->user()->quicConfig().maxQueuedFrames();
  }

  void add(ZmRef<Logical> logical) {
    logical->native(ZmMkRef(this));
    if (!ready) {
      queue_(pending, pendingLive, QueueSlot::Pending, ZuMv(logical));
      return;
    }
    open(ZuMv(logical));
  }
  void open(ZmRef<Logical> logical) {
    queue_(waiting, waitingLive, QueueSlot::Waiting, ZuMv(logical));
    auto link = ZmMkRef(this);
    this->app()->txRun([link]() mutable {
      auto stream = link->stream(Zquic::StreamType::Duplex);
      if (!stream) return;
      link->app()->rxRun([
	link = ZuMv(link), stream = ZuMv(stream)
      ]() mutable {
	link->streamed(ZuMv(stream));
      });
    });
  }
  void opened(ZmRef<Logical> logical, StreamRef stream) {
    stream->logical = logical;
    stream->slot = streams.length();
    streams.push(stream);
    logical->stream(stream.ptr());
    logical->connected_(ProfileTraits<H3QUIC>::apply({
      .alpn = "h3",
      .version = Zquic::Version1,
      .transport = Transport::QUIC,
      .secure = true
    }));
  }

  void connected(Zquic::Connected info) {
    if (info.version != Zquic::Version1 || info.alpn != "h3") {
      connectFailed(false);
      return;
    }
    const auto &config = this->app()->user()->quicConfig();
    H3::QPackLimits limits{
      config.qpackRxCapacity(), config.qpackTxCapacity(),
      config.qpackRxBlocked(), config.qpackTxSections()
    };
    bool extendedConnect = config.extendedConnect();
    auto link = ZmMkRef(this);
    this->app()->txRun([link, limits, extendedConnect]() mutable {
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      link->app()->rxRun([
	link = ZuMv(link), limits, extendedConnect, ok
      ]() mutable {
	if (!ok || link->down) {
	  link->connectFailed(false);
	  return;
	}
	auto params = H3::Params().qpackLimits(limits);
	if (!link->h3.openLocal(*link, params, extendedConnect)) {
	  link->connectFailed(false);
	  return;
	}
	if (!link->h3.localExtendedConnect) link->h3Ready();
      });
    });
  }
  void h3Ready() {
    if (ready) return;
    ready = true;
    for (unsigned i = pendingHead; i < pending.length(); ++i) {
      auto logical = ZuMv(pending[i]);
      if (!logical) continue;
      logical->h3QueueClear();
      open(ZuMv(logical));
    }
    pending.length(0);
    pendingHead = pendingLive = 0;
  }
  void disconnected(bool peer) { closePeer = peer; }
  void migrationPromoted(const Zquic::MigrationResult &) {
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void migrationFailed(const Zquic::MigrationResult &) {
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void endpointDown() {
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->endpointDown_();
    });
  }
  void endpointDown_() {
    if (finalizing) return;
    finalizing = true;
    down = true;
    this->app()->removeLink(this);
    for (unsigned i = 0; i < streams.length(); ++i) {
      streams[i]->slot = QueueSlot::Invalid;
      auto logical = ZuMv(streams[i]->logical);
      if (logical) logical->disconnected_(closePeer);
    }
    streams.length(0);
    clearQueue_(pending, pendingHead, pendingLive,
      [](Logical *logical) { logical->connectFailed_(false); });
    clearQueue_(waiting, waitingHead, waitingLive,
      [](Logical *logical) { logical->connectFailed_(false); });
    h3.qpackRxTable.final();
    auto link = ZmMkRef(this);
    this->app()->txRun([link]() mutable {
      link->h3Tx.final();
      link->app()->rxRun([link = ZuMv(link)]() mutable {
	link->app()->linkDown(link.ptr());
      });
    });
  }
  void connectFailed(bool transient) {
    auto self = ZmMkRef(this);
    down = true;
    this->app()->removeLink(this);
    clearQueue_(pending, pendingHead, pendingLive,
      [transient](Logical *logical) { logical->connectFailed_(transient); });
    clearQueue_(waiting, waitingHead, waitingLive,
      [transient](Logical *logical) { logical->connectFailed_(transient); });
    Base::disconnect();
  }
  void close(Logical *logical, Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link,
      logical = ZmMkRef(logical),
      stream = ZmMkRef(stream)
    ]() mutable {
      link->close_(ZuMv(logical), ZuMv(stream));
      return link;
    });
  }
  void finish(Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link, stream = ZmMkRef(stream)
    ]() mutable {
      link->finish_(ZuMv(stream));
      return link;
    });
  }
  void finish_(StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->localEnd) return;
    stream->localEnd = true;
    if (stream->remoteEnd) closeLater_(stream, false);
    this->send(ZuMv(stream), "", true);
  }
  void remoteEnd(Stream *stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 remote logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->remoteEnd) return;
    stream->remoteEnd = true;
    if (stream->localEnd) closeLater_(stream, true);
  }
  bool h3PeerCap() const {
    if (this->app()->txInvoked()) return h3PeerCapTx;
    return h3.peerExtendedConnect;
  }
  void h3PeerCap(bool value) {
    auto link = this;
    this->app()->txRun([link, value]() {
      link->h3PeerCapTx = value;
    });
  }
  bool migrate(const ZiSockAddr &local) {
    if (migrationDone) return false;
    if (migrationRequested) return true;
    migrationRequested = true;
    if (this->migrateLocal(local.ip(), local.port())) return true;
    migrationDone = true;
    return false;
  }
  void close_(ZmRef<Logical> logical, StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical close outside Rx thread", return);
    if (!stream) {
      removeQueued_(logical.ptr());
      logical->native({});
      logical->disconnected_(false);
      return;
    }
    if (stream->logical.ptr() != logical.ptr()) return;
    (void)this->send(stream, "", true);
    stream->logical = nullptr;
    removeStream_(stream);
    logical->disconnected_(false);
  }
  void streamed(StreamRef stream) {
    if (!stream || stream->id() < 0 ||
	Zquic::StreamID::server(uint64_t(stream->id())) ||
	Zquic::StreamID::uni(uint64_t(stream->id())))
      return;
    auto logical = shift_(waiting, waitingHead, waitingLive);
    if (!logical) {
      (void)this->send(stream, "", true);
      return;
    }
    opened(ZuMv(logical), ZuMv(stream));
  }
  void streamResetReceived(StreamRef stream, uint64_t error, uint64_t) {
    if (!stream) return;
    (void)stream->process(stream->rxStream());
    closeLater_(stream, true);
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->quicReset(error);
    });
  }
  void streamStopSendingReceived(StreamRef stream, uint64_t error) {
    if (!stream) return;
    (void)stream->process(stream->rxStream());
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->quicReset(error);
    });
  }
  void h3StreamError(StreamRef stream, uint64_t error) {
    closeLater_(stream, false);
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->stop(error);
      stream->quicReset(error);
    });
  }
  H3::QPackTxTable *qpackTx() { return &h3Tx; }

private:
  void closeLater_(Stream *stream, bool peer) {
    if (!stream || stream->closing) return;
    stream->closing = true;
    this->app()->rxRun([
      link = ZmMkRef(this), stream = ZmMkRef(stream), peer
    ]() mutable {
      if (!stream->logical) return;
      auto logical = ZuMv(stream->logical);
      link->removeStream_(stream);
      logical->disconnected_(peer);
    });
  }

  template <typename List>
  static void queue_(List &list, unsigned &live, QueueSlot::Kind kind,
      ZmRef<Logical> logical) {
    logical->h3Queue(kind, list.length());
    list.push(ZuMv(logical));
    ++live;
  }
  template <typename List>
  static void compact_(List &list, unsigned &head, unsigned live) {
    if (!list.length() || uint64_t(live) * 2 > list.length()) return;
    unsigned out = 0;
    for (unsigned i = head; i < list.length(); ++i) {
      if (!list[i]) continue;
      if (out != i) list[out] = ZuMv(list[i]);
      list[out]->h3QueueSlot(out);
      ++out;
    }
    list.length(out);
    head = 0;
  }
  template <typename List>
  static ZmRef<Logical> shift_(
      List &list, unsigned &head, unsigned &live) {
    while (head < list.length() && !list[head]) ++head;
    if (head == list.length()) {
      list.length(0);
      head = live = 0;
      return {};
    }
    auto logical = ZuMv(list[head++]);
    --live;
    logical->h3QueueClear();
    compact_(list, head, live);
    return logical;
  }
  template <typename List, typename Fn>
  static void clearQueue_(
      List &list, unsigned &head, unsigned &live, Fn &&fn) {
    for (unsigned i = head; i < list.length(); ++i) {
      auto logical = ZuMv(list[i]);
      if (!logical) continue;
      logical->h3QueueClear();
      fn(logical.ptr());
    }
    list.length(0);
    head = live = 0;
  }
  void removeQueued_(Logical *logical) {
    auto kind = logical->h3QueueKind();
    unsigned slot = logical->h3QueueSlot();
    switch (kind) {
      case QueueSlot::Pending:
	ZmAssert(slot < pending.length() && pending[slot].ptr() == logical);
	pending[slot] = nullptr;
	logical->h3QueueClear();
	--pendingLive;
	compact_(pending, pendingHead, pendingLive);
	break;
      case QueueSlot::Waiting:
	ZmAssert(slot < waiting.length() && waiting[slot].ptr() == logical);
	waiting[slot] = nullptr;
	logical->h3QueueClear();
	--waitingLive;
	compact_(waiting, waitingHead, waitingLive);
	break;
      default:
	break;
    }
  }
  void removeStream_(Stream *stream) {
    unsigned slot = stream->slot;
    ZmAssert(slot < streams.length() && streams[slot].ptr() == stream);
    unsigned last = streams.length() - 1;
    if (slot != last) {
      streams[slot] = ZuMv(streams[last]);
      streams[slot]->slot = slot;
    }
    streams.length(last);
    stream->slot = QueueSlot::Invalid;
  }

  void migrationComplete_() {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 migration completion outside Rx thread", return);
    migrationDone = true;
    Pending logical;
    for (unsigned i = 0; i < streams.length(); ++i)
      if (streams[i]->logical)
	logical.push(streams[i]->logical);
    for (unsigned i = 0; i < logical.length(); ++i)
      logical[i]->migrationComplete_();
  }

public:
  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Pending		pending;
  Waiting		waiting;
  Streams		streams;
  Zquic::Host		host;
  ZiIP			remote;
  uint16_t		port = 0;
  unsigned		pendingHead = 0;
  unsigned		waitingHead = 0;
  unsigned		pendingLive = 0;
  unsigned		waitingLive = 0;
  bool			ready = false;
  bool			down = false;
  bool			indexed = true;
  unsigned		retiredSlot = QueueSlot::Invalid;
  bool			closePeer = false;
  bool			migrationRequested = false;
  bool			migrationDone = false;
  bool			finalizing = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
};

} // namespace H3_

template <typename App, typename Impl>
class ClientLink<App, Impl, H3QUIC> :
  public ZmObject, public H3_::LogicalStream<Impl> {
  using Hub = H3_::ClientHub<App>;
  using NativeLink = H3_::CliLink<App, Impl>;
  using NativeStream = H3_::ClientStream<App, Impl>;
  using QueueSlot = H3_::QueueSlot;

public:
  enum { TLS = 1, Multiplexed = 1 };
  using Protocol = QUIC;

  ClientLink(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Host>
  void connect(Host &&host, uint16_t port) {
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    m_disconnecting = false;
    m_migrationRequested = false;
    m_migrationComplete = false;
    m_disconnectPending = false;
    m_app->connect(impl(), Zquic::Host{ZuFwd<Host>(host)}, port);
  }
  template <typename Host>
  void connect(Host &&host, uint16_t port, ZiIP remote) {
    m_connected = false;
    m_failed = false;
    m_disconnecting = false;
    m_migrationRequested = false;
    m_migrationComplete = false;
    m_disconnectPending = false;
    m_app->connect(
      impl(), Zquic::Host{ZuFwd<Host>(host)}, port, ZuMv(remote));
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.tlsName, endpoint.port, endpoint.ip);
  }
  auto txStream() { return m_stream->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native) m_native->h3.txErrorFn(m_txErrorFn);
    if (m_stream) m_stream->txErrorFn(m_txErrorFn);
  }
  NativeLink *h3Native_() const { return m_native; }
  NativeStream *h3Stream_() const { return m_stream; }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    parser.h3(
      m_native->h3.qpackRx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackDecoderWrite(span);
      },
      m_stream,
      [](void *ptr, uint64_t error) {
	static_cast<NativeStream *>(ptr)->h3StreamError(error);
      },
      uint64_t(m_stream->id()), &m_native->h3.params);
    parser.extendedConnect(m_native->h3.localExtendedConnect);
    (void)rx;
    return parser.process(*m_stream);
  }
  template <typename Builder>
  auto transmit(Builder &builder) {
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    builder.h3(
      m_native->qpackTx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackEncoderWrite(span);
      },
      uint64_t(m_stream->id()), m_native->h3PeerCap(),
      &m_native->h3.params);
    return m_stream->txStream();
  }
  void finish() {
    if (m_native && m_stream) m_native->finish(m_stream);
  }
  bool active() const { return m_native && m_stream; }
  void disconnect() {
    m_cancelled = true;
    if (!m_native || m_disconnecting) return;
    if (m_migrationRequested && !m_migrationComplete) {
      m_disconnectPending = true;
      return;
    }
    m_disconnecting = true;
    m_native->close(impl(), m_stream);
  }

  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    if (m_app->quicConfig().migrateOnOpen()) requestMigration_();
    m_app->connected(*impl(), info);
  }
  void disconnected_(bool peer) {
    ZmRef<NativeLink> native = ZuMv(m_native);
    m_stream = nullptr;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    ZmRef<NativeLink> native = ZuMv(m_native);
    m_stream = nullptr;
    m_app->connectFailed(*impl(), transient);
  }
  template <typename Rx>
  int process_(Rx &rx) {
    return m_app->process(*impl(), rx);
  }
  void native(ZmRef<NativeLink> native) {
    m_native = ZuMv(native);
    if (m_native)
      m_native->h3.txErrorFn(m_txErrorFn);
    else
      m_stream = nullptr;
  }
  NativeLink *native() const { return m_native; }
  void h3Queue(QueueSlot::Kind kind, uint32_t slot) {
    ZmAssert(m_queueKind == QueueSlot::None);
    m_queueKind = kind;
    m_queueSlot = slot;
  }
  QueueSlot::Kind h3QueueKind() const { return m_queueKind; }
  uint32_t h3QueueSlot() const { return m_queueSlot; }
  void h3QueueSlot(uint32_t slot) { m_queueSlot = slot; }
  void h3QueueClear() {
    m_queueKind = QueueSlot::None;
    m_queueSlot = QueueSlot::Invalid;
  }
  void stream(NativeStream *stream) {
    m_stream = stream;
    if (m_stream) m_stream->txErrorFn(m_txErrorFn);
  }
  void migrationComplete_() {
    m_migrationComplete = true;
    if (m_disconnectPending) disconnect();
  }
  template <typename State>
  void responseHeadersParsed(State *) {
    if (m_app->quicConfig().migrateAfterHeaders()) requestMigration_();
  }
  template <typename State>
  void responseBodyBytes(State *state) {
    auto bytes = m_app->quicConfig().migrateAfterBytes();
    if (bytes && state && state->responseBody.consumed >= bytes)
      requestMigration_();
  }

private:
  bool requestMigration_() {
    if (m_migrationRequested || !m_native) return true;
    m_migrationRequested = true;
    ZiSockAddr local = m_app->quicConfig().migrationLocal();
    if (!local) local = m_native->local();
    if (m_native->migrate(local)) return true;
    m_migrationComplete = true;
    return false;
  }

  App			*m_app = nullptr;
  ZmRef<NativeLink>	m_native;
  NativeStream		*m_stream = nullptr;
  ZiTxErrorFn		m_txErrorFn;
  uint32_t		m_queueSlot = QueueSlot::Invalid;
  QueueSlot::Kind	m_queueKind = QueueSlot::None;
  bool			m_connected = false;
  bool			m_failed = false;
  bool			m_cancelled = false;
  bool			m_disconnecting = false;
  bool			m_migrationRequested = false;
  bool			m_migrationComplete = false;
  bool			m_disconnectPending = false;
};

template <typename App>
class ClientHub<App, H3QUIC> : public H3_::ClientHub<App> {
public:
  using Base = H3_::ClientHub<App>;
  using Traits = Transport_::Traits<QUIC>;
  enum { TLS = 1, Multiplexed = 1 };

  using Base::connect;
  using Base::init;

  bool init(const HubConfig &hub, const QUICConfig &config) {
    if (!config.qpackValid() || !config.maxQueuedFrames()) return false;
    m_config = config;
    if (!Base::init(Traits::clientParams(hub, config))) return false;
    Base::faults(config);
    return true;
  }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }

  const QUICConfig &quicConfig() const { return m_config; }

private:
  QUICConfig	m_config;
};


// One typed pool of normalized HTTP client links.  The pool owns transport
// links and message machinery; Owner owns request policy and attempt results.
template <
  typename Owner_, typename Profile_, typename LiveReq_,
  typename Request_, typename ResParser_>
class ClientPool :
  public ClientHub<
    ClientPool<
      Owner_, Profile_, LiveReq_,
      Request_, ResParser_>,
    Profile_> {
public:
  using Owner = Owner_;
  using Profile = Profile_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using ResParser = ResParser_;
  using Pool = ClientPool;
  using Message = MessageTraits<Profile>;
  using Base = ClientHub<Pool, Profile>;
  enum : unsigned { InvalidSlot = unsigned(-1) };

  struct Link :
    public ClientLink<Pool, Link, Profile_> {
    using Base = ClientLink<Pool, Link, Profile_>;
    using Protocol = typename Profile::Protocol;
    using IO = ClientMessage<
      Owner, LiveReq, Link, Profile,
      Request, ResParser>;

    Link(Pool *pool, unsigned id_) :
      Base{pool}, id{id_}, message{pool->owner(), this} { }

    LiveReq *request() const { return m_request; }
    bool stopped() const { return m_stopped; }

    void assign(LiveReq *request) {
      ++m_generation;
      m_request = request;
      m_complete = false;
      m_sent = false;
      m_stopped = false;
      m_closing = false;
    }
    void start() {
      if (!m_request) return;
      this->txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
	return owner()->poolTxError(*this, m_request, e);
      }});
      owner()->poolConnect(*this, *m_request);
    }
    void sendRequest() {
      if (!m_request) return;
      message.bind(m_request);
      message.reset();
      m_sent = true;
      owner()->poolSend(*this, *m_request, Message::ID);
      auto link = ZmMkRef(this);
      unsigned generation = m_generation;
      pool()->txRun([link = ZuMv(link), generation]() mutable {
	link->message.beginTx();
	link->sendRequestTx_(generation);
      });
    }
    void close() {
      if (m_closing) return;
      m_closing = true;
      auto link = ZmMkRef(this);
      pool()->txRun([link = ZuMv(link)]() mutable {
	link->message.cancelTx();
	auto pool = link->pool();
	pool->rxRun([link = ZuMv(link)]() mutable {
	  link->disconnect();
	});
      });
    }
    void retire() {
      pool()->detach(*this, m_request);
      m_request = nullptr;
      if (this->active())
	close();
      else
	notifyStopped_();
    }

    bool reusable() const {
      if constexpr (Message::OneMessagePerLink) return false;
      return m_request && owner()->poolReusable(*m_request);
    }

    void onConnected(const ConnectedInfo &info) {
      if (!m_request || !pool()->accepting()) {
	close();
	return;
      }
      owner()->poolConnected(*this, *m_request, info);
      sendRequest();
    }
    void onDisconnected(bool peer) {
      owner()->poolDisconnected(*this, m_request, peer);
      if (m_request && !m_complete) {
	if constexpr (Message::CloseDelimited) {
	  owner()->poolCloseDelimited(*m_request);
	  message.eof();
	}
	if (m_request && !m_complete) complete(false);
      }
      notifyStopped_();
    }
    void onConnectFailed(bool transient) {
      owner()->poolConnectFailed(*this, m_request, transient);
      if (m_request && !m_complete) complete(false);
    }
    template <typename Rx>
    int process(Rx &rx) {
      return m_request ? message.process(rx) : -1;
    }
    template <
      typename Stream, typename Rx, int ID = Message::ID,
      ZuIfT<ID == Version::H1, int> = 0>
    int process(Stream, Rx &) { return -1; }

    void complete(bool ok) {
      if (!m_request || m_complete) return;
      m_complete = true;
      auto link = ZmMkRef(this);
      unsigned generation = m_generation;
      bool sent = m_sent;
      pool()->txRun([link = ZuMv(link), generation, ok, sent]() mutable {
	if (sent) link->message.cancelTx();
	auto pool = link->pool();
	BodyCommit commit = sent ? link->message.commit() : BodyCommit{};
	pool->rxRun([
	  link = ZuMv(link), commit, generation, ok]() mutable {
	  if (link->m_generation != generation) return;
	  auto request = link->m_request;
	  if (!request) return;
	  link->owner()->poolTxCommitted(*link, *request, commit);
	  bool reuse = ok && link->reusable();
	  link->owner()->poolComplete(*link, *request, ok, reuse);
	});
      });
    }

    Owner *owner() const { return pool()->owner(); }
    Pool *pool() const { return this->app(); }

    unsigned	id = 0;
    unsigned	slot = 0;
    IO		message;

  private:
    void sendRequestTx_(unsigned generation) {
      bool ok = message.send();
      auto link = ZmMkRef(this);
      auto pool = this->pool();
      BodyCommit commit = message.commit();
      pool->rxRun([link = ZuMv(link), commit, generation, ok]() mutable {
	if (link->m_generation != generation) return;
	auto request = link->request();
	if (!request) return;
	if (ok)
	  link->owner()->poolTxCommitted(*link, *request, commit);
	else {
	  link->owner()->poolTxFailed(*link, *request, commit);
	  link->complete(false);
	}
      });
    }

    void notifyStopped_() {
      if (m_stopped) return;
      m_stopped = true;
      pool()->linkStopped(*this);
    }

    LiveReq	*m_request = nullptr;
    unsigned	m_generation = 0;
    bool	m_complete = false;
    bool	m_sent = false;
    bool	m_stopped = false;
    bool	m_closing = false;
  };

  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.ClientPool.Links">>;

  ClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  void owner(Owner *owner_) { m_owner = owner_; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }
  unsigned linkCount() const { return m_links.length(); }
  const Links &links() const { return m_links; }

  bool cancel(LiveReq *request) {
    if (!request) return false;
    unsigned slot = request->poolSlot;
    if (slot >= m_links.length() ||
	m_links[slot]->request() != request) return false;
    m_links[slot]->close();
    return true;
  }

  void detach(Link &link, LiveReq *request) {
    if (request && request->poolTransport == Message::Transport::ID &&
	request->poolSlot == link.slot && request->poolLink == &link) {
      request->poolSlot = InvalidSlot;
      request->poolLink = nullptr;
    }
  }

  ZmRef<Link> open(LiveReq *request, unsigned id) {
    if (!accepting() || !request) return {};
    if constexpr (!Message::Multiplexed)
      for (unsigned i = 0; i < m_links.length(); ++i)
	if (m_links[i]->stopped()) {
	  auto link = m_links[i];
	  ++m_live;
	  link->id = id;
	  link->assign(request);
	  request->poolTransport = Message::Transport::ID;
	  request->poolSlot = link->slot;
	  request->poolLink = link.ptr();
	  link->start();
	  return link;
	}
    ZmRef<Link> link = new Link{this, id};
#ifdef ZmObject_DEBUG
    if constexpr (Message::Multiplexed)
      link->ZmObject::debug();
    else
      link->ZmPolymorph::debug();
#endif
    link->slot = m_links.length();
    m_links.push(link);
    ++m_live;
    link->assign(request);
    request->poolTransport = Message::Transport::ID;
    request->poolSlot = link->slot;
    request->poolLink = link.ptr();
    link->start();
    return link;
  }

  void connected(Link &link, const ConnectedInfo &info) {
    link.onConnected(info);
  }
  void disconnected(Link &link, bool peer) {
    link.onDisconnected(peer);
  }
  void connectFailed(Link &link, bool transient) {
    link.onConnectFailed(transient);
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    return link.process(rx);
  }

  void linkStopped(Link &link) {
    m_owner->poolStopped(link);
    if (m_live) --m_live;
    if (m_stopping && !m_live)
      if constexpr (!Message::Multiplexed) Base::stop_();
    if constexpr (Message::Multiplexed)
      this->rxRun([this, hold = ZmMkRef(&link)]() mutable {
	unsigned i = hold->slot;
	unsigned n = m_links.length();
	if (i >= n || m_links[i].ptr() != hold.ptr()) return;
	if (i != --n) {
	  m_links[i] = ZuMv(m_links[n]);
	  m_links[i]->slot = i;
	  if (auto request = m_links[i]->request())
	    if (request->poolTransport == Message::Transport::ID)
	      request->poolSlot = i;
	}
	m_links.length(n);
      });
  }

  // TCP/TLS ZmEngine hook.  The native engine retains stop(done);
  // Base::stop_() completes it only after every link is down.  QUIC uses
  // H3_::ClientHub::stop(done) instead and never enters this hook.
  void stop_() {
    m_stopping = true;
    if constexpr (!Message::Multiplexed) {
      if (!m_live) {
	Base::stop_();
	return;
      }
      for (unsigned i = 0; i < m_links.length(); ++i)
	if (m_links[i] && !m_links[i]->stopped())
	  m_links[i]->close();
    }
  }

  void final() {
    m_links.length(0);
    Base::final();
  }

  unsigned reconnFreq() const { return 0; }

private:
  Owner		*m_owner = nullptr;
  Links		m_links;
  unsigned	m_live = 0;
  bool		m_stopping = false;
};



template <
  typename Owner, typename LiveReq,
  typename Request, typename ResParser>
class TLSClientPool;

template <
  typename Pool, typename Impl, typename Owner_, typename LiveReq_,
  typename Request_, typename ResParser_,
  typename Profile>
class TLSClientPoolLink_ {
public:
  using Owner = Owner_;
  using LiveReq = LiveReq_;
  using Message = MessageTraits<Profile>;
  using IO = ClientMessage<
    Owner, LiveReq, Impl, Profile,
    Request_, ResParser_>;

  TLSClientPoolLink_(Pool *pool, Impl *impl, unsigned slot_) :
    m_pool{pool}, m_impl{impl}, m_slot{slot_},
    m_message{pool->owner(), impl} { }

  LiveReq *request() const { return m_request; }
  bool stopped() const { return m_stopped; }
  unsigned slot() const { return m_slot; }
  void slot(unsigned slot_) { m_slot = slot_; }

  void assign(LiveReq *request) {
    ++m_generation;
    m_request = request;
    m_complete = -1;
    m_sent = false;
    m_stopped = false;
    m_closing = false;
    m_impl->txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
      return owner()->poolTxError(*m_impl, m_request, e);
    }});
  }
  void sendRequest() {
    if (!m_request) return;
    m_message.bind(m_request);
    m_message.reset();
    m_sent = true;
    owner()->poolSend(*m_impl, *m_request, Message::ID);
    auto link = ZmMkRef(m_impl);
    unsigned generation = m_generation;
    m_pool->txRun([this, link = ZuMv(link), generation]() mutable {
      m_message.beginTx();
      sendRequestTx_(generation);
    });
  }
  void close() {
    if (m_closing) return;
    m_closing = true;
    auto link = ZmMkRef(m_impl);
    m_pool->txRun([this, link = ZuMv(link)]() mutable {
      m_message.cancelTx();
      m_pool->rxRun([this, link = ZuMv(link)]() mutable {
	m_impl->disconnect();
      });
    });
  }
  void retire() {
    m_pool->detach(*m_impl, m_request);
    m_request = nullptr;
    if (!m_impl->active()) {
      notifyStopped_();
      return;
    }
    if constexpr (Message::OneMessagePerLink) {
      if (m_complete != 1) close();
    } else
      close();
  }

  bool reusable() const {
    if constexpr (Message::OneMessagePerLink) return false;
    return m_request && owner()->poolReusable(*m_request);
  }

  void onConnected(const ConnectedInfo &info) {
    if (!m_request || !m_pool->accepting()) {
      close();
      return;
    }
    m_pool->selected(*m_impl, info.httpVersion);
    owner()->poolConnected(*m_impl, *m_request, info);
    sendRequest();
  }
  void onDisconnected(bool peer) {
    owner()->poolDisconnected(*m_impl, m_request, peer);
    if (m_request && m_complete < 0) {
      if constexpr (Message::CloseDelimited) {
	owner()->poolCloseDelimited(*m_request);
	m_message.eof();
      }
      if (m_request && m_complete < 0) complete(false);
    }
    notifyStopped_();
  }
  void onConnectFailed(bool transient) {
    owner()->poolConnectFailed(*m_impl, m_request, transient);
    if (m_request && m_complete < 0) complete(false);
    notifyStopped_();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return m_request ? m_message.process(rx) : -1;
  }

  void complete(bool ok) {
    if (!m_request || m_complete >= 0) return;
    m_complete = int8_t(ok);
    auto link = ZmMkRef(m_impl);
    unsigned generation = m_generation;
    bool sent = m_sent;
    m_pool->txRun([
      this, link = ZuMv(link), generation, ok, sent]() mutable {
      if (sent) m_message.cancelTx();
      BodyCommit commit = sent ? m_message.commit() : BodyCommit{};
      m_pool->rxRun([
	this, link = ZuMv(link), commit, generation, ok]() mutable {
	if (m_generation != generation || !m_request) return;
	owner()->poolTxCommitted(*m_impl, *m_request, commit);
	bool reuse = ok && reusable();
	owner()->poolComplete(*m_impl, *m_request, ok, reuse);
      });
    });
  }

  Owner *owner() const { return m_pool->owner(); }

private:
  void sendRequestTx_(unsigned generation) {
    bool ok = m_message.send();
    auto link = ZmMkRef(m_impl);
    BodyCommit commit = m_message.commit();
    m_pool->rxRun([
      this, link = ZuMv(link), commit, generation, ok]() mutable {
      if (m_generation != generation || !m_request) return;
      if (ok)
	owner()->poolTxCommitted(*m_impl, *m_request, commit);
      else {
	owner()->poolTxFailed(*m_impl, *m_request, commit);
	complete(false);
      }
    });
  }

  void notifyStopped_() {
    if (m_stopped) return;
    m_stopped = true;
    m_pool->linkStopped(*m_impl);
  }

  Pool		*m_pool = nullptr;
  Impl		*m_impl = nullptr;
  LiveReq	*m_request = nullptr;
  unsigned	m_generation = 0;
  unsigned	m_slot = 0;
  int8_t	m_complete = -1;
  bool		m_sent = false;
  bool		m_stopped = false;
  bool		m_closing = false;
  IO		m_message;
};

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Profile>
class TLSClientPoolLink;

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser>
class TLSClientPoolLink<
  Pool, Owner, LiveReq, Request, ResParser, H1TLS> :
  public TLS_::ClientH1Logical<
    Pool, TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
    TLS_::CliLink<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
    Owner, LiveReq, Request, ResParser, H1TLS> {
  using Impl = TLSClientPoolLink;
  using Native = TLS_::ClientH1Logical<
    Pool, Impl,
    TLS_::CliLink<
      Pool, Impl,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS>>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, LiveReq, Request, ResParser, H1TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot) :
    Native{pool}, Link{pool, this, slot} { }

  using Link::assign;
  using Link::close;
  using Link::onConnected;
  using Link::onConnectFailed;
  using Link::onDisconnected;
  using Link::process;
  using Link::request;
  using Link::retire;
  using Link::sendRequest;
  using Link::slot;
  using Link::stopped;

  void complete(bool ok) { Link::complete(ok); }
};

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser>
class TLSClientPoolLink<
  Pool, Owner, LiveReq, Request, ResParser, H2TLS> :
  public H2_::ClientLogical<
    Pool, TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H2TLS>,
    TLS_::CliLink<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H2TLS>,
    Owner, LiveReq, Request, ResParser, H2TLS> {
  using Impl = TLSClientPoolLink;
  using Native = H2_::ClientLogical<
    Pool, Impl,
    TLS_::CliLink<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
      Impl>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, LiveReq, Request, ResParser, H2TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot) :
    Native{pool}, Link{pool, this, slot} { }

  using Link::assign;
  using Link::close;
  using Link::onConnected;
  using Link::onConnectFailed;
  using Link::onDisconnected;
  using Link::process;
  using Link::request;
  using Link::retire;
  using Link::sendRequest;
  using Link::slot;
  using Link::stopped;

  void complete(bool ok) { Link::complete(ok); }
};

template <
  typename Owner_, typename LiveReq_,
  typename Request_, typename ResParser_>
class TLSClientPool :
  public TLS_::ClientHub<
    TLSClientPool<
      Owner_, LiveReq_, Request_, ResParser_>,
    TLSClientPoolLink<
      TLSClientPool<
	Owner_, LiveReq_, Request_, ResParser_>,
      Owner_, LiveReq_, Request_, ResParser_, H1TLS>,
    TLSClientPoolLink<
      TLSClientPool<
	Owner_, LiveReq_, Request_, ResParser_>,
      Owner_, LiveReq_, Request_, ResParser_, H2TLS>> {
public:
  using Owner = Owner_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using ResParser = ResParser_;
  using Pool = TLSClientPool;
  using H1Link = TLSClientPoolLink<
    Pool, Owner, LiveReq, Request, ResParser, H1TLS>;
  using H2Link = TLSClientPoolLink<
    Pool, Owner, LiveReq, Request, ResParser, H2TLS>;
  using Base = TLS_::ClientHub<Pool, H1Link, H2Link>;
  using Base::stop;
  enum : unsigned { InvalidSlot = unsigned(-1) };

  struct Pair {
    ZmRef<H1Link>	h1;
    ZmRef<H2Link>	h2;
    int8_t		selected = -1;
  };
  using Pairs =
    ZtArray<Pair, ZtArrayHeapID<"Zhttp.TLSClientPool.Pairs">>;

  TLSClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }

  void open(LiveReq *request, unsigned) {
    if (!accepting() || !request) return;
    unsigned slot = m_pairs.length();
    Pair pair{
      .h1 = new H1Link{this, slot},
      .h2 = new H2Link{this, slot}
    };
    pair.h1->assign(request);
    pair.h2->assign(request);
    request->poolTransport = Transport::TLS;
    request->poolSlot = slot;
    request->poolLink = pair.h1.ptr();
    auto h1 = pair.h1;
    auto h2 = pair.h2;
    m_pairs.push(ZuMv(pair));
    ++m_live;
    auto url = request->route.url.url();
    Base::connect(h1, h2, url.host, url.port);
  }

  bool cancel(LiveReq *request) {
    if (!request || request->poolSlot >= m_pairs.length()) return false;
    auto &pair = m_pairs[request->poolSlot];
    switch (pair.selected) {
      case Version::H1:
	if (pair.h1->request() != request) return false;
	pair.h1->close();
	break;
      case Version::H2:
	if (pair.h2->request() != request) return false;
	pair.h2->close();
	break;
      default:
	if (pair.h1->request() != request) return false;
	pair.h1->close();
	pair.h2->close();
	break;
    }
    return true;
  }

  template <typename Link>
  void detach(Link &link, LiveReq *request) {
    if (!request || request->poolTransport != Transport::TLS ||
	request->poolSlot != link.slot() ||
	request->poolSlot >= m_pairs.length() ||
	request->poolLink != m_pairs[request->poolSlot].h1.ptr()) return;
    request->poolSlot = InvalidSlot;
    request->poolLink = nullptr;
  }

  template <typename Link>
  void selected(Link &link, int8_t version) {
    unsigned slot = link.slot();
    if (slot < m_pairs.length()) m_pairs[slot].selected = version;
  }

  template <typename Link>
  void connected(Link &link, const ConnectedInfo &info) {
    link.onConnected(info);
  }
  template <typename Link>
  void disconnected(Link &link, bool peer) {
    link.onDisconnected(peer);
  }
  template <typename Link>
  void connectFailed(Link &link, bool transient) {
    link.onConnectFailed(transient);
  }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    return link.process(rx);
  }

  template <typename Link>
  void linkStopped(Link &link) {
    if (m_live) --m_live;
    m_owner->poolStopped(link);
    this->rxRun([this, hold = ZmMkRef(&link)]() mutable {
      unsigned i = hold->slot();
      unsigned n = m_pairs.length();
      if (i >= n) return;
      auto &pair = m_pairs[i];
      if constexpr (ZuIsSame<Link, H1Link>{}) {
	if (pair.h1.ptr() != hold.ptr()) return;
      } else {
	if (pair.h2.ptr() != hold.ptr()) return;
      }
      if (i != --n) {
	m_pairs[i] = ZuMv(m_pairs[n]);
	m_pairs[i].h1->slot(i);
	m_pairs[i].h2->slot(i);
	if (auto request = m_pairs[i].h1->request()) {
	  request->poolTransport = Transport::TLS;
	  request->poolSlot = i;
	  request->poolLink = m_pairs[i].h1.ptr();
	}
      }
      m_pairs.length(n);
    });
  }

  template <typename Done>
  void stop(Done &&done) {
    m_stopping = true;
    Base::stop(ZuFwd<Done>(done));
  }
  void final() {
    ZmAssert(!m_pairs);
    Base::final();
  }

  unsigned reconnFreq() const { return 0; }
  void goaway(uint32_t) { }

private:
  Owner		*m_owner = nullptr;
  Pairs		m_pairs;
  unsigned	m_live = 0;
  bool		m_stopping = false;
};


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
  typename Owner, typename Profile, typename LiveReq,
  typename Request, typename ResParser>
class ClientPool;

// TxQ is an unordered ZmPQTx specialized on the final application type.
// TxQ::Msg publicly derives from Request_; Request_ and ResParser_ conform to
// the extended application Builder and Parser contracts documented in
// Zhttp.hh.

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

  ZuAssert(!Tx::Ordered,
    "Zhttp::Client requires unordered ZmPQTx acknowledgements");
  ZuAssert((ZuIs_<Request, Request_>{}),
    "Zhttp::Client requires TxQ::Msg to publicly derive from Request_");
  ZuAssert((ZuIs_<Request_, ZmObject>{}),
    "Zhttp::Client requires Request_ to derive from ZmObject");

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

  struct LiveReq {
    // Valid while non-null; the Tx queue owns the request.
    Request		*request = nullptr;
    void		*poolLink = nullptr;
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
    unsigned		poolSlot = unsigned(-1);
    unsigned		redirects = 0;
    unsigned		retries = 0;
    AttemptPhase::T	phase = AttemptPhase::Idle;
    Transport::T	poolTransport = -1;
  };

  using TCPPool = ClientPool<
    Self, H1TCP, LiveReq, Request_, ResParser>;
  using TLSPool = TLSClientPool<
    Self, LiveReq, Request_, ResParser>;
  using QUICPool = ClientPool<
    Self, H3QUIC, LiveReq, Request_, ResParser>;
  using LiveReqs =
    ZtArray<LiveReq, ZtArrayHeapID<"Zhttp.Client.LiveReqs">>;
  using Free =
    ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Client.Free">>;
  struct ActiveEntry {
    typename Tx::Key	key;
    unsigned		slot = 0;
  };
  static const typename Tx::Key &activeEntryKey_(
      const ActiveEntry &entry) {
    return entry.key;
  }
  using ActiveHash = ZmHash<ActiveEntry,
    ZmHashKey<activeEntryKey_,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.Client.Active">>>>;

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
    m_liveReqs.length(config.concurrency());
    m_timers.size(config.concurrency());
    m_free.size(config.concurrency());
    for (unsigned i = config.concurrency(); i; --i) {
      m_liveReqs[i - 1].slot = i - 1;
      RequestTimerRef timer = new RequestTimer{this, i - 1};
#ifdef ZmObject_DEBUG
      timer->ZmObject::debug();
#endif
      m_timers.push(ZuMv(timer));
      m_free.push(i - 1);
    }
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
    ZmAssert(!m_activeReqs->count_());
    m_hubs.final();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_liveReqs.length(0);
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

  // Optional CRTP callback when a sealed client has drained its transmit
  // queue and all admitted requests have reached a terminal state.
  void idle() { }

  void printQUICDiag() { m_quic.printDiag(); }

  template <typename Link>
  void poolConnect(Link &link, LiveReq &attempt) {
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
  bool poolTxError(Link &, LiveReq *attempt, ZeException &e) {
    if (!m_txErrorFn) return true;
    return m_txErrorFn(e);
  }
  template <typename Link>
  void poolSend(Link &, LiveReq &attempt, int) {
    sending_(attempt);
  }
  template <typename Link>
  void poolConnected(
    Link &, LiveReq &attempt, const ConnectedInfo &info) {
    attempt.protocol.transport = info.transport;
    attempt.protocol.httpVersion = info.httpVersion;
    if (!(attempt.events & Zhttp::AttemptEvent{}.SelectionObserved())) {
      attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
      observe_(
	attempt.request, event_(attempt, ClientEventType::Selected));
    }
    attempt.request->connected(info);
  }
  template <typename Link>
  void poolDisconnected(Link &, LiveReq *attempt, bool peer) {
    if (attempt && attempt->request) attempt->request->disconnected(peer);
  }
  template <typename Link>
  void poolConnectFailed(Link &, LiveReq *attempt, bool transient) {
    if (attempt) {
      fail_(*attempt, FailureKind::Connect);
      attempt->failure.transient = transient;
      attempt->events |= Zhttp::AttemptEvent{}.FailureObserved();
      observe_(
	attempt->request, event_(*attempt, ClientEventType::AttemptFailed,
	  ResultCode::Failed, transient));
    }
    if (attempt && attempt->request)
      attempt->request->connectFailed(transient);
  }
  template <typename Link>
  void poolTxCommitted(
    Link &, LiveReq &attempt, const BodyCommit &commit) {
    attempt.requestBody = commit;
  }
  template <typename Link>
  void poolTxFailed(
    Link &link, LiveReq &attempt, const BodyCommit &commit) {
    poolTxCommitted(link, attempt, commit);
    fail_(attempt, FailureKind::Tx);
  }
  void poolCloseDelimited(LiveReq &attempt) {
    attempt.protocol.closeDelimited = true;
  }
  bool poolReusable(const LiveReq &attempt) const {
    return attempt.protocol.persistence != Persistence::Close &&
      !attempt.protocol.closeDelimited &&
      (!attempt.protocol.http10 ||
	attempt.protocol.persistence == Persistence::KeepAlive) &&
      attempt.failure.kind == FailureKind::None;
  }

  template <typename Link>
  void poolComplete(
    Link &link, LiveReq &attempt, bool ok, bool reuse) {
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
	attempt.request, event_(attempt, ClientEventType::AttemptFailed,
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
      observe_(attempt.request, event);
      attempt.request->redirected(attempt.route.url.url());
      if (reuse && same && direct_<Link>(attempt)) {
	attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
	observe_(
	  attempt.request, event_(attempt, ClientEventType::Selected));
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
	      observe_(attempt.request, event);
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
	observe_(attempt.request, event);
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
    Link &, LiveReq &attempt, ResParser &parser, unsigned value) {
    attempt.protocol.status = value;
    receivingHeaders_(attempt);
    parser.status(value);
  }
  template <typename Link>
  void contentLength(
    Link &, LiveReq &, ResParser &parser, uint64_t value) {
    parser.contentLength(value);
  }
  template <typename Link>
  void chunked(Link &, LiveReq &, ResParser &parser) {
    parser.chunked();
  }
  template <typename Link>
  void version(
    Link &, LiveReq &attempt, ResParser &parser, ZuBSpan value) {
    attempt.protocol.http10 = ZuCSpan(value) == "HTTP/1.0";
    parser.version(value);
  }
  template <typename Key, typename Link>
  void header(
    Link &, LiveReq &attempt, ResParser &parser, ZuBSpan value) {
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
    Link &link, LiveReq &attempt, ResParser &parser, Rx &rx) {
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
    Link &link, LiveReq &attempt, ResParser &parser,
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
  bool done(const LiveReq &attempt) const {
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
    const LiveReq &attempt, ClientEventType::T type,
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
	complete_(*request, ResultCode::Cancelled);
	txRun_([this]() { idleTx_(); });
      });
      return;
    }
    rxRun_([this, key]() { cancelActive_(key); });
  }

  void cancelActive_(typename Tx::Key key) {
    assertRx_();
    auto active = m_activeReqs->findPtr(key);
    if (!active || active->data().slot >= m_liveReqs.length()) return;
    auto &attempt = m_liveReqs[active->data().slot];
    ZmAssert(attempt.request && key_(attempt.request) == key);
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
  }

  void admit_(Request *request) {
    assertRx_();
    if (m_rxStopping || !request) {
      if (request) {
	complete_(*request, ResultCode::Cancelled);
	terminal_(key_(request));
      }
      return;
    }
    if (!m_free) {
      complete_(*request, ResultCode::Failed);
      terminal_(key_(request));
      return;
    }
    unsigned slot = m_free.pop();
    auto key = key_(request);
    ZmAssert(!m_activeReqs->findPtr(key));
    m_activeReqs->add(ActiveEntry{key, slot});
    ++m_active;
    begin_(m_liveReqs[slot], request);
  }

  void begin_(LiveReq &attempt, Request *request) {
    prepare_(attempt, request);
    route_(attempt);
  }

  void prepare_(LiveReq &attempt, Request *request) {
    attempt.request = request;
    attempt.route.url = attempt.request->url;
    attempt.redirects = 0;
    attempt.retries = 0;
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints.length(0);
    attempt.identity.request = ++m_requestID;
    attempt.identity.attempt = ++m_attemptID;
    resetWire_(attempt);
    armTimer_(attempt);
  }

  void nextAttempt_(LiveReq &attempt, bool generation) {
    attempt.identity.attempt = ++m_attemptID;
    if (generation) {
      attempt.retries = 0;
      attempt.route.endpointIndex = 0;
      attempt.route.endpoints.length(0);
    }
    resetWire_(attempt);
  }

  static void resetWire_(LiveReq &attempt) {
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
    attempt.poolLink = nullptr;
    attempt.poolSlot = unsigned(-1);
    attempt.poolTransport = -1;
    attempt.phase = AttemptPhase::Idle;
  }

  static void resolving_(LiveReq &attempt) {
    ZmAssert(attempt.request && attempt.phase == AttemptPhase::Idle);
    attempt.phase = AttemptPhase::Resolving;
  }

  static void connecting_(LiveReq &attempt) {
    ZmAssert(attempt.request &&
      (attempt.phase == AttemptPhase::Idle ||
	attempt.phase == AttemptPhase::Resolving));
    attempt.phase = AttemptPhase::Connecting;
  }

  static void sending_(LiveReq &attempt) {
    ZmAssert(attempt.request &&
      (attempt.phase == AttemptPhase::Idle ||
	attempt.phase == AttemptPhase::Connecting));
    attempt.phase = AttemptPhase::Sending;
  }

  static void receivingHeaders_(LiveReq &attempt) {
    ZmAssert(attempt.request &&
      (attempt.phase == AttemptPhase::Sending ||
	attempt.phase == AttemptPhase::ReceivingHeaders));
    attempt.phase = AttemptPhase::ReceivingHeaders;
  }

  static void receivingBody_(LiveReq &attempt) {
    ZmAssert(attempt.request &&
      (attempt.phase == AttemptPhase::Sending ||
	attempt.phase == AttemptPhase::ReceivingHeaders ||
	attempt.phase == AttemptPhase::ReceivingBody));
    attempt.phase = AttemptPhase::ReceivingBody;
  }

  static void closing_(LiveReq &attempt) {
    ZmAssert(attempt.request && attempt.phase != AttemptPhase::Idle);
    attempt.phase = AttemptPhase::Closing;
  }

  static bool responseStarted_(const LiveReq &attempt) {
    return attempt.protocol.status != 0;
  }

  static void idleAttempt_(LiveReq &attempt) {
    attempt.request = nullptr;
    resetWire_(attempt);
    attempt.identity = {};
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints.length(0);
    attempt.redirects = 0;
    attempt.retries = 0;
    ZmAssert(!attempt.request && !attempt.discovery &&
      attempt.phase == AttemptPhase::Idle);
  }

  static void fail_(LiveReq &attempt, FailureKind::T kind) {
    // The first classified failure determines result/retry precedence.
    if (attempt.failure.kind == FailureKind::None)
      attempt.failure.kind = kind;
  }

  template <typename Link>
  bool direct_(LiveReq &attempt) {
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
  static void select_(LiveReq &attempt) {
    using HTTP = ProfileTraits<Profile>;
    attempt.protocol.transport = HTTP::Transport::ID;
    attempt.protocol.httpVersion = HTTP::HTTPVersion;
  }

  void route_(LiveReq &attempt) {
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

  void prefer_(LiveReq &attempt) {
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

  void resolveAltSvc_(LiveReq &attempt, Endpoint endpoint) {
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
	  auto &attempt = m_liveReqs[slot];
	  if (!attempt.request || attempt.identity.attempt != id) return;
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

  void discover_(LiveReq &attempt) {
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
	  auto &attempt = m_liveReqs[slot];
	  if (!attempt.request || attempt.identity.attempt != id) return;
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

  void startTCP_(LiveReq &attempt) {
    select_<H1TCP>(attempt);
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    observe_(
      attempt.request, event_(attempt, ClientEventType::Selected));
    m_tcp.open(&attempt, attempt.slot);
  }
  void startTLS_(LiveReq &attempt) {
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
  void startQUIC_(LiveReq &attempt, const Endpoint *endpoint) {
    select_<H3QUIC>(attempt);
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    if (endpoint) {
      attempt.route.endpoint = *endpoint;
      attempt.route.endpointSet = true;
      attempt.request->selected(attempt.route.endpoint);
    }
    observe_(
      attempt.request, event_(attempt, ClientEventType::Selected));
    m_quic.open(&attempt, attempt.slot);
  }

  void armTimer_(LiveReq &attempt) {
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

  void cancelTimer_(LiveReq &attempt) {
    auto timer = m_timers[attempt.slot].ptr();
    if (!timer->armed) return;
    timer->armed = false;
    m_mx->del(&timer->timer);
  }

  void timeout_(unsigned slot, uint64_t requestID) {
    if (m_rxStopping || slot >= m_liveReqs.length()) return;
    auto &attempt = m_liveReqs[slot];
    if (!attempt.request || attempt.identity.request != requestID) return;
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
  bool retry_(Link &link, LiveReq &attempt) {
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
    observe_(attempt.request, event);
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

  bool canReplay_(const LiveReq &attempt) const {
    return attempt.request->replayable() &&
      attempt.request->reproducible();
  }

  template <typename Link>
  void headersDone_(Link &link, LiveReq &attempt) {
    if (attempt.phase == AttemptPhase::ReceivingBody ||
	attempt.phase == AttemptPhase::Closing)
      return;
    receivingBody_(attempt);
    link.responseHeadersParsed(&attempt);
  }

  template <typename Link>
  void finish_(
    Link &link, LiveReq &attempt, ResultCode::T code, bool reuse) {
    assertRx_();
    (void)reuse;
    cancelTimer_(attempt);
    auto key = key_(attempt.request);
    emit_(*attempt.request, result_(attempt, code));
    closing_(attempt);
    releaseAttempt_(attempt, key);
    link.retire();
    terminal_(key);
  }

  void complete_(Request_ &request, ResultCode::T code) {
    Result result{.request = ++m_requestID, .code = code};
    emit_(request, result);
  }

  Result result_(const LiveReq &attempt, ResultCode::T code) const {
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

  void completeAttempt_(LiveReq &attempt, ResultCode::T code) {
    assertRx_();
    cancelTimer_(attempt);
    auto key = key_(attempt.request);
    emit_(*attempt.request, result_(attempt, code));
    closing_(attempt);
    releaseAttempt_(attempt, key);
    terminal_(key);
  }

  void releaseAttempt_(LiveReq &attempt, typename Tx::Key key) {
    unsigned slot = attempt.slot;
    idleAttempt_(attempt);
    auto active = m_activeReqs->del(key);
    ZmAssert(active && active->data().slot == slot);
    m_free.push(slot);
    --m_active;
  }

  void stopIngress_() {
    assertRx_();
    if (m_rxStopping) return;
    m_rxStopping = true;
    for (unsigned i = 0; i < m_liveReqs.length(); ++i) {
      auto &attempt = m_liveReqs[i];
      if (!attempt.request) continue;
      cancelTimer_(attempt);
      if (attempt.discovery) {
	auto discovery = ZuMv(attempt.discovery);
	completeAttempt_(attempt, ResultCode::Cancelled);
	discovery->cancel();
      } else
	attempt.terminal = ResultCode::Cancelled;
    }
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
	complete_(*request, ResultCode::Cancelled);
      });
    }
  }

  void idleTx_() {
    assertTx_();
    if (m_sealed && !m_idle && !m_txActive &&
	!app_()->txQueue()->count_()) {
      m_idle = true;
      app_()->idle();
    }
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  ClientConfig	m_config;
  TCPPool	m_tcp;
  TLSPool	m_tls;
  QUICPool	m_quic;
  Hubs	m_hubs;
  AltSvcCache	m_altSvc;
  ZiTxErrorFn	m_txErrorFn;
  const DiscoveryResolver *m_resolverOps = nullptr;
  LiveReqs	m_liveReqs;
  ZmRef<ActiveHash> m_activeReqs = new ActiveHash;
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
  bool		m_idle = false;
  bool		m_rxStopping = false;
  bool		m_txStopping = false;
};

} // namespace Zhttp

#endif /* ZhttpClient_HH */
