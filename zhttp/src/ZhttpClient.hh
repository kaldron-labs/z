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

#include <zlib/ZuObjectTraits.hh>

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

enum : unsigned {
  // Measured scheduler-turn bound: amortizes cross-shard publication without
  // allowing large client pools or sparse queues to monopolize an I/O shard.
  ClientWorkBatch = 64
};

struct ClientSessionTxState {
  enum { Idle, Active, Complete, Failed, Cancelled };
};

template <typename Logical, typename Heap = ZuVoid>
struct ClientH2ClearState_ :
    Heap, ZmObject {
  using Active =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H2.ClearState">>;

  Active	active;
};
template <typename Logical>
using ClientH2ClearStateHeap = ZmHeap<"Zhttp.H2.ClearState",
  ClientH2ClearState_<Logical>>;
template <typename Logical>
using ClientH2ClearState =
  ClientH2ClearState_<Logical, ClientH2ClearStateHeap<Logical>>;

ZtEnumStruct(ZhttpAPI, ProtoPolicy, int8_t,
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
  unsigned	pool = 0;
  unsigned	link = NullSlot;
  uint16_t	redirects = 0;
  uint16_t	retries = 0;
  ResultCode::T	code = ResultCode::OK;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;

  bool ok() const { return code == ResultCode::OK; }
};

class Config {
public:
  struct Field {
    enum {
      Links = 1U<<0,
      Concurrency = 1U<<1,
      LinkMax = 1U<<2,
      RequestTimeout = 1U<<3,
      MaxRedirects = 1U<<4,
      MaxRetries = 1U<<5,
      MaxOrigins = 1U<<6,
      MaxAltSvc = 1U<<7,
      RetainedBodyMax = 1U<<8,
      RetainedMessageMax = 1U<<9,
      DiscoveryLimits = 1U<<10,
      Protocol = 1U<<11,
      H2Policy = 1U<<12,
      BlindH3 = 1U<<13,
      AltSvcCrossHost = 1U<<14,
      Secure = 1U<<15,
      TCP = 1U<<16,
      TLS = 1U<<17,
      QUIC = 1U<<18
    };
  };

  unsigned links() const { return m_links; }
  unsigned concurrency() const { return m_concurrency; }
  // Per-link in-flight operation ceiling: H1 pipeline depth or H2/H3 streams.
  unsigned linkMax() const { return m_linkMax; }
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
  ProtoPolicy::T protocol() const { return m_protocol; }
  H2Policy::T h2Policy() const { return m_h2Policy; }
  bool blindH3() const { return m_blindH3; }
  bool altSvcCrossHost() const { return m_altSvcCrossHost; }
  bool secure() const { return m_secure; }
  bool tcp() const { return m_tcp; }
  bool tls() const { return m_tls; }
  bool quic() const { return m_quic; }

  bool valid() const {
    if (!m_links || !m_concurrency || !m_linkMax ||
	m_protocol < ProtoPolicy::ForceH3 ||
	m_protocol > ProtoPolicy::DisableH3 ||
	m_h2Policy < H2Policy::Force || m_h2Policy > H2Policy::Disable)
      return false;
    if (!m_secure) return m_tcp;
    switch (m_protocol) {
      case ProtoPolicy::ForceH3: return m_quic;
      case ProtoPolicy::PreferH3: return m_tls && m_quic;
      case ProtoPolicy::DisableH3: return m_tls;
      default: return false;
    }
  }

  Config &links(unsigned v) {
    m_links = v;
    m_set |= Field::Links;
    return *this;
  }
  Config &concurrency(unsigned v) {
    m_concurrency = v;
    m_set |= Field::Concurrency;
    return *this;
  }
  Config &linkMax(unsigned v) {
    m_linkMax = v;
    m_set |= Field::LinkMax;
    return *this;
  }
  Config &requestTimeout(unsigned v) {
    m_requestTimeout = v;
    m_set |= Field::RequestTimeout;
    return *this;
  }
  Config &maxRedirects(unsigned v) {
    m_maxRedirects = v;
    m_set |= Field::MaxRedirects;
    return *this;
  }
  Config &maxRetries(unsigned v) {
    m_maxRetries = v;
    m_set |= Field::MaxRetries;
    return *this;
  }
  Config &maxOrigins(unsigned v) {
    m_maxOrigins = v;
    m_set |= Field::MaxOrigins;
    return *this;
  }
  Config &maxAltSvc(unsigned v) {
    m_maxAltSvc = v;
    m_set |= Field::MaxAltSvc;
    return *this;
  }
  Config &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    m_set |= Field::RetainedBodyMax;
    return *this;
  }
  Config &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    m_set |= Field::RetainedMessageMax;
    return *this;
  }
  Config &discoveryLimits(DiscoveryLimits v) {
    m_discoveryLimits = v;
    m_set |= Field::DiscoveryLimits;
    return *this;
  }
  Config &protocol(ProtoPolicy::T v) {
    m_protocol = v;
    m_set |= Field::Protocol;
    return *this;
  }
  Config &h2Policy(H2Policy::T v) {
    m_h2Policy = v;
    m_set |= Field::H2Policy;
    return *this;
  }
  Config &blindH3(bool v) {
    m_blindH3 = v;
    m_set |= Field::BlindH3;
    return *this;
  }
  Config &altSvcCrossHost(bool v) {
    m_altSvcCrossHost = v;
    m_set |= Field::AltSvcCrossHost;
    return *this;
  }
  Config &secure(bool v) {
    m_secure = v;
    m_set |= Field::Secure;
    return *this;
  }
  Config &tcp(bool v) {
    m_tcp = v; m_set |= Field::TCP; return *this;
  }
  Config &tls(bool v) {
    m_tls = v; m_set |= Field::TLS; return *this;
  }
  Config &quic(bool v) {
    m_quic = v; m_set |= Field::QUIC; return *this;
  }

  Config overlay(const Config &o) const {
    Config v{*this};
#define Zhttp_Config_Overlay(bit, member) \
    if (o.m_set & bit) v.member = o.member
    Zhttp_Config_Overlay(Field::Links, m_links);
    Zhttp_Config_Overlay(Field::Concurrency, m_concurrency);
    Zhttp_Config_Overlay(Field::LinkMax, m_linkMax);
    Zhttp_Config_Overlay(Field::RequestTimeout, m_requestTimeout);
    Zhttp_Config_Overlay(Field::MaxRedirects, m_maxRedirects);
    Zhttp_Config_Overlay(Field::MaxRetries, m_maxRetries);
    Zhttp_Config_Overlay(Field::MaxOrigins, m_maxOrigins);
    Zhttp_Config_Overlay(Field::MaxAltSvc, m_maxAltSvc);
    Zhttp_Config_Overlay(Field::RetainedBodyMax, m_retainedBodyMax);
    Zhttp_Config_Overlay(Field::RetainedMessageMax, m_retainedMessageMax);
    Zhttp_Config_Overlay(Field::DiscoveryLimits, m_discoveryLimits);
    Zhttp_Config_Overlay(Field::Protocol, m_protocol);
    Zhttp_Config_Overlay(Field::H2Policy, m_h2Policy);
    Zhttp_Config_Overlay(Field::BlindH3, m_blindH3);
    Zhttp_Config_Overlay(Field::AltSvcCrossHost, m_altSvcCrossHost);
    Zhttp_Config_Overlay(Field::Secure, m_secure);
    Zhttp_Config_Overlay(Field::TCP, m_tcp);
    Zhttp_Config_Overlay(Field::TLS, m_tls);
    Zhttp_Config_Overlay(Field::QUIC, m_quic);
#undef Zhttp_Config_Overlay
    v.m_set |= o.m_set;
    return v;
  }

private:
  unsigned	m_links = 1;
  unsigned	m_concurrency = 1;
  unsigned	m_linkMax = 1;
  unsigned	m_requestTimeout = 0;
  unsigned	m_maxRedirects = 8;
  unsigned	m_maxRetries = 0;
  unsigned	m_maxOrigins = 256;
  unsigned	m_maxAltSvc = 8;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  DiscoveryLimits m_discoveryLimits;
  ProtoPolicy::T m_protocol = ProtoPolicy::PreferH3;
  H2Policy::T	m_h2Policy = H2Policy::Prefer;
  bool		m_blindH3 = false;
  bool		m_altSvcCrossHost = false;
  bool		m_secure = true;
  bool		m_tcp = true;
  bool		m_tls = true;
  bool		m_quic = true;
  uint32_t	m_set = 0;
};

struct Destination {
  URLString	host;
  URLString	authority;
  uint16_t	port = 0;
  bool		ipv6Literal = false;

  Destination() = default;
  Destination(ZuBSpan host_, uint16_t port_, bool ipv6Literal_ = false) :
    host{host_}, port{port_}, ipv6Literal{ipv6Literal_} { init(); }
  Destination(const OriginView &origin) :
    Destination{origin.host, origin.port, origin.ipv6Literal} { }

  void init() {
    URLString input;
    if (ipv6Literal) input << '[';
    input << host;
    if (ipv6Literal) input << ']';
    input << ':' << ZuBoxed(port);
    AuthorityView parsed;
    ZuBSpan input_ = input;
    auto error = parseAuthority(parsed, input_, 0, 0, true);
    if (!port || !error.ok() || parsed.port != port ||
	parsed.ipv6Literal != ipv6Literal) {
      host.length(0);
      authority.length(0);
      port = 0;
      ipv6Literal = false;
      return;
    }
    if (ipv6Literal) {
      ZiIP ip;
      if (!ZiIP::parse(ip, parsed.host) || !ip.v6()) {
	host.length(0);
	authority.length(0);
	port = 0;
	ipv6Literal = false;
	return;
      }
      host.length(0);
      host << ip;
    } else {
      host = parsed.host;
      for (unsigned i = 0, n = host.length(); i < n; ++i)
	if (host[i] >= 'A' && host[i] <= 'Z')
	  host[i] += 'a' - 'A';
    }
    authority.length(0);
    if (ipv6Literal) authority << '[';
    authority << host;
    if (ipv6Literal) authority << ']';
    authority << ':' << ZuBoxed(port);
  }
  bool valid() const { return host && authority && port; }
};

// Application request Builder base.  The application ReqBuilder_ derives from
// an intrusive object base followed by ReqBuilder.  The final application
// ReqBuilder is TxQ::Msg, an intrusive ZmPQueue::Node which adds the heap base
// ahead of, and publicly derives from, the application data type.
// Each node represents one logical request and is never reset or repurposed.
// Retry and redirect attempts may call its Builder callbacks again; every
// Builder must reproduce the same message each time.  idempotent() controls
// attempts made after the peer might have applied the request; an attempt
// known not to have been applied can be repeated independently.
struct ReqBuilder : public Builder {
  // l(value), for extended CONNECT only.
  template <typename L> void protocol(L &&) const { }

  // A priority-queue item represents one discrete request.  key() remains an
  // application requirement because it supplies the monotonic queue identity.
  uint64_t length() const { return 1; }

  bool idempotent(Method::T method) const {
    return idempotentMethod(method);
  }

  // Synchronous attempt lifecycle notifications.  completed() is called once
  // for the submitted ReqBuilder; the other callbacks may repeat by attempt.
  void connected(const ConnectedInfo &) { }
  void disconnected(bool) { }
  void connectFailed(bool) { }

  // Optional synchronous event callbacks, detected on the concrete
  // ReqBuilder; intentionally commented declarations only, not base
  // implementations:
  //
  // void selected(
  //   const Endpoint *, uint64_t request, uint64_t attempt,
  //   unsigned pool, unsigned link, Transport::T, Version::T);
  // void attemptFailed(
  //   uint64_t request, uint64_t attempt, unsigned pool, unsigned link,
  //   unsigned status, Transport::T, Version::T,
  //   bool transient, bool responseStarted);
  // void redirected(
  //   const URLView &, uint64_t request, uint64_t attempt,
  //   uint64_t previousAttempt, unsigned status, uint16_t redirects);
  // void retried(
  //   uint64_t request, uint64_t attempt,
  //   uint64_t previousAttempt, uint16_t retries);
  // void fallback(
  //   uint64_t request, uint64_t attempt, uint64_t previousAttempt,
  //   Transport::T fromTransport, Version::T fromVersion);
  // void cancelled(const Result &);
  // void completed(const Result &);
};

template <typename U, typename = void>
struct HasReqSelected : public ZuFalse { };
template <typename U>
struct HasReqSelected<U, decltype(
  ZuDeclVal<U &>().selected(
    ZuDeclVal<const Endpoint *>(), uint64_t{}, uint64_t{},
    unsigned{}, unsigned{}, Transport::T{}, Version::T{}), void())> :
  public ZuTrue { };

template <typename U, typename = void>
struct HasReqAttemptFailed : public ZuFalse { };
template <typename U>
struct HasReqAttemptFailed<U, decltype(
  ZuDeclVal<U &>().attemptFailed(
    uint64_t{}, uint64_t{}, unsigned{}, unsigned{}, unsigned{},
    Transport::T{}, Version::T{}, bool{}, bool{}), void())> :
  public ZuTrue { };

template <typename U, typename = void>
struct HasReqRedirected : public ZuFalse { };
template <typename U>
struct HasReqRedirected<U, decltype(
  ZuDeclVal<U &>().redirected(
    ZuDeclVal<const URLView &>(), uint64_t{}, uint64_t{}, uint64_t{},
    unsigned{}, uint16_t{}), void())> : public ZuTrue { };

template <typename U, typename = void>
struct HasReqRetried : public ZuFalse { };
template <typename U>
struct HasReqRetried<U, decltype(
  ZuDeclVal<U &>().retried(
    uint64_t{}, uint64_t{}, uint64_t{}, uint16_t{}), void())> :
  public ZuTrue { };

template <typename U, typename = void>
struct HasReqFallback : public ZuFalse { };
template <typename U>
struct HasReqFallback<U, decltype(
  ZuDeclVal<U &>().fallback(
    uint64_t{}, uint64_t{}, uint64_t{}, Transport::T{}, Version::T{}),
  void())> : public ZuTrue { };

template <typename U, typename = void>
struct HasReqCancelled : public ZuFalse { };
template <typename U>
struct HasReqCancelled<U, decltype(
  ZuDeclVal<U &>().cancelled(ZuDeclVal<const Result &>()), void())> :
  public ZuTrue { };

template <typename U, typename = void>
struct HasReqCompleted : public ZuFalse { };
template <typename U>
struct HasReqCompleted<U, decltype(
  ZuDeclVal<U &>().completed(ZuDeclVal<const Result &>()), void())> :
  public ZuTrue { };

// Protocol-neutral client session.  App supplies request intent and
// response handling; HTTP-version-specific builders, parsers, EOF rules, and
// link completion remain library-owned.
template <
  typename App_, typename LiveReq_, typename Link_, typename Profile_,
  typename Request_, typename ResParser_>
class ClientSession {
public:
  using App = App_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using Link = Link_;
  using Profile = Profile_;
  using ResParser = ResParser_;
  using ReqHdrCatalog = typename Request::HdrCatalog;
  using RespHdrCatalog = typename ResParser::HdrCatalog;
  using ReqHeaders = typename ReqHdrCatalog::List;
  using RespHeaders = typename RespHdrCatalog::List;
  using Message = MessageTraits<Profile>;
  ZuAssert((ZuIs_<ResParser, Zhttp::Parser>{}),
    "Zhttp::Client requires ResParser to derive from Zhttp::Parser");
  using ReqHeaderKeys = typename HeaderList<ReqHdrCatalog>::Keys;
  enum { ReqContentLength =
    ZuTypeIn<ZuStringT<"content-length">, ReqHeaderKeys>{} };
  ZuAssert((
    !ZuTypeIn<ZuStringT<"transfer-encoding">, ReqHeaderKeys>{}),
    "libZhttp owns request transfer-encoding framing");

private:
  struct ReqOps {
    using Spans = HeaderSpans<ReqHdrCatalog>;

    Request		*app = nullptr;
    Spans		spans;
    ZuBSpan		target;
    ZuBSpan		authority;
    uint64_t		produced = 0;
    Method::T		method = Method::GET;
    bool		rejectContentLength = false;
    bool		operationCached = false;

    ReqOps(
      Request &app_, ZuBSpan authority_, bool operationCached_ = false,
      Method::T method_ = Method::GET, ZuBSpan target_ = {}) :
      app{&app_}, target{target_}, authority{authority_}, method{method_},
      operationCached{operationCached_} { }

    template <typename L>
    void operation(L &&l) {
      if (operationCached) {
	l(method, [this](auto &&emit) {
	  emit([this](auto &tx) { tx << target; });
	});
      } else
	app->operation(ZuFwd<L>(l));
    }
    template <typename L>
    void host(L &&l) { l(authority); }
    template <typename L>
    void protocol(L &&l) { app->protocol(ZuFwd<L>(l)); }
    template <typename Key, typename Value, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (rejectContentLength) return;
      builderFixedHeader<Key, Value>(app, ZuFwd<L>(l), 0);
    }
    template <typename Key, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (rejectContentLength) return;
      if constexpr (HasBuilderHeader<Request, Key, L &&>{})
	app->template header<Key>(ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      app->header([&l]<typename K, typename V>(K &&k, V &&v) {
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
    }

    template <typename Key>
    void headerSpan(ZuSpan<uint8_t> span) {
      spans.template record<Key>(span);
    }
    template <typename Key>
    void headerOffset(uint64_t offset, unsigned length) {
      spans.template recordOffset<Key>(offset, length);
    }
    void headerBase(uint8_t *base) { spans.resolve(base); }
    void patch() { spans.patch(*app); }
    uint64_t contentLength() const { return produced; }
    Request &appBuilder() { return *app; }
    template <typename Emit>
    void emitBody(Emit &&emit) {
      if constexpr (HasBuilderBody<Request, Emit &&>{})
	app->body(ZuFwd<Emit>(emit));
    }

  };

  template <bool HasBody, bool Streaming>
  struct Builder_ :
    public Message::template Request<
      Builder_<HasBody, Streaming>,
      ReqHdrCatalog, HasBody, Streaming>,
    public ReqOps {
    using Base = typename Message::template Request<
      Builder_, ReqHdrCatalog, HasBody, Streaming>;
    static constexpr unsigned HdrBufSize = Request::HdrBufSize;

    Builder_(
      Request &app_, ZuBSpan authority,
      bool rejectContentLength = false,
      bool operationCached = false, Method::T method = Method::GET,
      ZuBSpan target = {}) :
      ReqOps{app_, authority, operationCached, method, target} {
      this->rejectContentLength = rejectContentLength;
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
      Parser, RespHdrCatalog>;
    using State = typename Protocol::State;

    bool operation(Method::T, Target &) { return true; }
    void status(unsigned value, bool http10) {
      app->status(*link, *request, sink(), value, http10);
    }
    bool bodyInfo(BodyType::T type, uint64_t length) {
      return app->bodyInfo(*link, *request, sink(), type, length);
    }
    template <typename Key>
    void header(
        Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
      app->template header<Key>(
	*link, *request, sink(), section, value);
    }
    template <typename Key, typename Value>
    void header(Zhttp::FieldSection::T section) {
      app->template header<Key, Value>(
	*link, *request, sink(), section);
    }
    void header(
	Zhttp::FieldSection::T section,
	ZuBSpan key, ZuSpan<uint8_t> value) {
      app->header(*link, *request, sink(), section, key, value);
    }
    template <typename Rx>
    bool body(Rx &rx) {
      return app->body(*link, *request, sink(), rx);
    }
    void complete(typename State::T state) {
      if (!*active) return;
      *active = false;
      app->template complete<State>(
	*link, *request, sink(), state);
    }

    ResParser &sink() { return *sink_; }
    const ResParser &sink() const { return *sink_; }

    App		*app = nullptr;
    Link	*link = nullptr;
    LiveReq	*request = nullptr;
    ResParser	*sink_ = nullptr;
    bool	*active = nullptr;
  };

  struct Parser :
    public Message::template ResponseParser<
      Parser, RespHdrCatalog>,
    public ParserSink_ {
    using Base = typename Message::template ResponseParser<
      Parser, RespHdrCatalog>;
    using State = typename Base::State;

    using ParserSink_::body;
    using ParserSink_::bodyInfo;
    using ParserSink_::complete;
    using ParserSink_::header;
    using ParserSink_::operation;
    bool enable1xx() const { return ParserSink_::sink().enable1xx(); }
    void status(unsigned value) {
      if constexpr (Message::ID == Version::H1)
	ParserSink_::status(value, Base::http10());
      else
	ParserSink_::status(value, false);
    }
  };

public:
  using ParserState = typename Parser::State;

  ClientSession(App *app = nullptr, Link *link = nullptr) :
    m_app{app}, m_link{link} { }

  void bind(LiveReq *request) {
    ZmAssert(!m_responseActive);
    m_request = request;
    if (!request) return;
    m_requestApp = request->request;
    m_operationOK = false;
    m_target.length(0);
    unsigned operations = 0;
    m_requestApp->operation(
      [this, &operations](
	  Method::T method, auto &&emit) {
	if (++operations != 1) return;
	m_requestMethod = method;
	emit([this](auto &&write) { write(m_target); });
	m_operationOK = true;
      });
    if (operations != 1) m_operationOK = false;
    if (m_operationOK)
      m_operationOK = m_app->poolOperation(
	*request, m_requestMethod, m_target);
    m_response.init(*m_requestApp);
    m_responseActive = true;
    static_cast<ParserSink_ &>(m_parser) = {
      m_app, m_link, request, &m_response, &m_responseActive};
  }
  void reset() {
    m_parser.reset(m_app->retainedBodyMax());
    if constexpr (Message::ID != Version::H1) {
      if (!m_request || !m_operationOK) return;
      m_parser.requestMethod(m_requestMethod);
    }
  }
  void fail() {
    if (!m_responseActive) {
      m_link->complete(false);
      return;
    }
    m_responseActive = false;
    m_app->template complete<ParserState>(
      *m_link, *m_request, m_response, ParserState::Error);
  }

  // Tx-owned synchronous request construction.
  void beginTx() {
    m_commit = {};
    m_txState = ClientSessionTxState::Active;
  }

  void cancelTx() {
    if (m_txState != ClientSessionTxState::Active) return;
    m_commit.reset = m_commit.committed;
    m_txState = ClientSessionTxState::Cancelled;
  }

  BodyCommit commit() const { return m_commit; }

  bool send() {
    if (m_txState != ClientSessionTxState::Active) return false;
    if (!m_operationOK) return failTx_();
    return sendApp_(*m_requestApp);
  }

private:
  Link &txLink_() { return *m_link; }
  uint64_t txRetainedMax_() const { return m_app->retainedMessageMax(); }
  void txHeaders_() { m_commit.headers = true; }
  template <bool Streaming>
  void txProduced_(uint64_t n) {
    m_commit.produced = n;
    if constexpr (Streaming) m_commit.committed = n;
  }
  bool txComplete_(uint64_t n) {
    m_commit.produced = n;
    m_commit.committed = n;
    m_commit.final = true;
    m_txState = ClientSessionTxState::Complete;
    return true;
  }

  bool sendApp_(Request &app) {
    auto policy = app.bodyPolicy();
    if (!BodyPolicy::hasBody(policy)) return sendEmpty_(app);
    if (BodyPolicy::streaming(policy)) {
      return sendStreaming_(app, BodyPolicy::optional(policy));
    }
    if constexpr (!ReqContentLength) return failTx_();
    return sendFixed_(app, BodyPolicy::optional(policy));
  }

  struct TxOps {
    ClientSession *owner;

    Link &link() { return owner->txLink_(); }
    uint64_t fixedBodyMax() const { return owner->fixedBodyMax_(); }
    uint64_t retainedMax() const {
      return owner->txRetainedMax_();
    }
    void headers() { owner->txHeaders_(); }
    template <bool Streaming>
    void produced(uint64_t n) {
      owner->template txProduced_<Streaming>(n);
    }
    bool empty(Request &app) { return owner->sendEmpty_(app); }
    template <bool Streaming>
    bool fail() { return owner->template failFor_<Streaming>(); }
    bool complete(uint64_t n) {
      return owner->txComplete_(n);
    }
  };

  bool sendStreaming_(Request &app, bool optional) {
    Builder_<true, true> builder{
      app, m_app->authority(), true,
      m_operationOK, m_requestMethod, m_target};
    TxOps ops{this};
    return MessageTx<Message, TxOps>{ops}.streaming(builder, optional);
  }

  bool sendEmpty_(Request &app) {
    Builder_<false, false> builder{
      app, m_app->authority(), true,
      m_operationOK, m_requestMethod, m_target};
    auto tx = m_link->transmit_(builder);
    if (!builder.begin(tx))
      return failTx_();
    m_commit.headers = true;
    builder.finish(tx);
    m_link->finish();
    m_commit.final = true;
    m_txState = ClientSessionTxState::Complete;
    return true;
  }

  bool sendFixed_(Request &app, bool optional) {
    Builder_<true, false> builder{
      app, m_app->authority(), false,
      m_operationOK, m_requestMethod, m_target};
    TxOps ops{this};
    return MessageTx<Message, TxOps>{ops}.fixed(builder, optional);
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
    if (n > FixedBodyMax) n = FixedBodyMax;
    return n;
  }

  bool failTx_() {
    m_commit.discarded = m_commit.produced;
    m_txState = ClientSessionTxState::Failed;
    return false;
  }

  bool failStreamedTx_() {
    m_commit.discarded = m_commit.produced > m_commit.committed ?
      m_commit.produced - m_commit.committed : 0;
    m_commit.reset = m_commit.committed;
    m_link->disconnect();
    m_txState = ClientSessionTxState::Failed;
    return false;
  }

  template <bool Streaming>
  bool failFor_() {
    if constexpr (Streaming)
      return failStreamedTx_();
    else
      return failTx_();
  }

  // Stable for one bound operation.  bind() completes on Rx before the Tx
  // send post publishes the request metadata and immutable target.
  App		*m_app = nullptr;
  Link		*m_link = nullptr;
  LiveReq	*m_request = nullptr;
  Request	*m_requestApp = nullptr;
  ZtString<ZtStringHeapID<"Zhttp.Target">> m_target;
  Method::T	m_requestMethod = Method::GET;
  bool		m_operationOK = false;
  bool		m_responseActive = false;

  // Rx thread exclusive after bind publication.
  alignas(Zm::CacheLineSize)
  Parser	m_parser;
  ResParser	m_response;

  // Tx thread exclusive.
  alignas(Zm::CacheLineSize)
  BodyCommit	m_commit;
  int8_t	m_txState = ClientSessionTxState::Idle;
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

template <typename Link>
inline auto ClientLogicalID_(const Link *link, int) -> decltype(link->id()) {
  return link->id();
}

template <typename Link>
inline auto ClientLogicalID_(const Link *link, long) -> decltype(link->id) {
  return link->id;
}

namespace H2_ {

template <typename App> class ClientHub;
template <typename App, typename Heap = ZuVoid> class CliLink;
template <typename App>
using CliLinkHeap = ZmHeap<"Zhttp.H2.Link", CliLink<App>>;
template <typename App>
using CliLinkT = CliLink<App, CliLinkHeap<App>>;

struct ClientSlot {
  ZmContext	owner;
  void		(*close)(void *) = nullptr;
  bool		(*available)(void *) = nullptr;
  bool		(*up)(void *) = nullptr;
  bool		(*stopping)(void *) = nullptr;
  Ztls::Host	host;
  uint16_t	port = 0;
};

struct ClientPoolKey {
  Ztls::Host	host;
  uint16_t	port = 0;
  unsigned	id = 0;

  bool equals(const ClientPoolKey &key) const {
    return port == key.port && id == key.id && host == key.host;
  }
  friend bool operator ==(
    const ClientPoolKey &l, const ClientPoolKey &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    return ZuHash<Ztls::Host>::hash(host) ^ uint32_t(port) ^ id;
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

ZmHashDerive(ClientPoolHash, ClientPoolEntry,
  (ZmHashNode<ClientPoolEntry,
    ZmHashKey<ClientPoolEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2">>>>));

struct ClientGenEntry {
  uint64_t	generation = 0;
  unsigned	id = 0;
  ZmContext	owner;
};

inline uint64_t ClientGenEntry_KeyAxor(const ClientGenEntry &entry) {
  return entry.generation;
}

ZmHashDerive(ClientGenHash, ClientGenEntry,
  (ZmHashNode<ClientGenEntry,
    ZmHashKey<ClientGenEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H2.Generations">>>>));

template <typename App>
class ClientHub : public Ztls::Client<ClientHub<App>> {
public:
  using Base = Ztls::Client<ClientHub>;
  using Link = CliLinkT<App>;
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
    ZmRef<Logical> logical_ = ZmRef(logical);
    this->rxInvoke([
      this, logical = ZuMv(logical_), host = ZuMv(host), port
    ]() mutable {
      logical->prepare_();
      unsigned id = ClientLogicalID_(logical.ptr(), 0);
      ClientPoolKey key{host, port, id};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) {
	link = poolEntry->owner.object<Link>();
	if (link->replaceable()) link = nullptr;
      }
      if (!link) {
	link = new Link{this, host, port, id, m_config};
	link->hubSlot(m_slots.length());
	m_slots.push(ClientSlot{
	  .owner = link,
	  .close = [](void *ptr) {
	    static_cast<Link *>(ptr)->beginStop();
	  },
	  .available = [](void *ptr) {
	    return static_cast<Link *>(ptr)->available();
	  },
	  .up = [](void *ptr) {
	    return static_cast<const Link *>(ptr)->isUp();
	  },
	  .stopping = [](void *ptr) {
	    return static_cast<const Link *>(ptr)->stopping();
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
      m_stopScan = 0;
      m_stopScanning = true;
      stopLinksBatch_();
    });
  }
  void linkDown(Link *link) {
    if (auto entry = m_pool->findPtr(
	ClientPoolKey{link->host(), link->port(), link->id()}))
      if (entry->owner.object<Link>() == link)
	m_pool->delNode(
	  static_cast<ClientPoolHash::Node *>(entry));
    unsigned i = link->hubSlot();
    if (i < m_slots.length() &&
	m_slots[i].owner.object<Link>() == link) {
      unsigned last = m_slots.length() - 1;
      if (i != last) {
	m_slots[i] = ZuMv(m_slots[last]);
	m_slots[i].owner.object<Link>()->hubSlot(i);
      }
      m_slots.length(last);
      link->hubSlot(NullSlot);
      if (m_stopping && i < m_stopScan && last >= m_stopScan)
	m_stopScan = i;
    }
    if (m_stopping && m_stopPending && !--m_stopPending && !m_stopScanning)
      stopDrain_();
  }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots)
      ZmAssert(!slot.up(slot.owner.object<void>()));
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }
  const H2Config &h2Config() const { return m_config; }

private:
  void stopLinksBatch_() {
    Slots close;
    unsigned inspected = 0, n = m_slots.length();
    while (m_stopScan < n &&
	inspected++ < ClientWorkBatch) {
      auto &slot = m_slots[m_stopScan++];
      auto owner = slot.owner.object<void>();
      if (!slot.up(owner) || slot.stopping(owner)) continue;
      ++m_stopPending;
      close.push(slot);
    }
    for (auto &slot: close) slot.close(slot.owner.object<void>());
    if (m_stopScan < n) {
      this->rxRun([this]() { stopLinksBatch_(); });
      return;
    }
    m_stopScanning = false;
    if (!m_stopPending) stopDrain_();
  }

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
    m_stopScan = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  // Stable after initialization.
  ZmRef<ClientPoolHash> m_pool;
  H2Config	m_config;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Slots		m_slots;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  unsigned	m_stopScan = 0;
  bool		m_stopping = false;
  bool		m_stopScanning = false;
};

template <typename App, typename Heap>
class CliLink :
  public Heap,
  public Ztls::CliLink<ClientHub<App>, CliLink<App, Heap>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<CliLink<App, Heap>, typename App::Link> {
public:
  using Hub = ClientHub<App>;
  using Logical = typename App::Link;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<CliLink, Logical>;
  using Base::up;
  using Pending = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host_, uint16_t port_, unsigned id_,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host_)}, m_port{port_}, m_id{id_}
  {
    Wire_::initWire(false, config);
    m_pendingMax = config.maxPending();
  }
  ~CliLink() {
    Wire_::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  unsigned id() const { return m_id; }
  unsigned hubSlot() const { return m_hubSlot; }
  void hubSlot(unsigned slot) { m_hubSlot = slot; }
  void add(ZmRef<Logical> logical) {
    if (pendingCount_() >= m_pendingMax &&
	(!m_ready || !Wire_::canOpenLocalStream())) {
      logical->connectFailed_(false);
      return;
    }
    logical->native(this);
    if (!m_ready || pending_() || !Wire_::canOpenLocalStream()) {
      m_pending.push(ZuMv(logical));
      return;
    }
    openNow_(ZuMv(logical));
  }
  bool available() const {
    return isUp() && !m_draining &&
      !Wire_::localStreamsExhausted() &&
      (m_ready && Wire_::canOpenLocalStream() ||
	pendingCount_() < m_pendingMax);
  }
  bool isUp() const { return !m_down; }
  void up(bool value) { m_down = !value; }
  bool stopping() const { return m_stopping; }
  bool replaceable() const {
    return m_down || m_stopping || m_draining ||
      Wire_::localStreamsExhausted();
  }
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
    if (!isUp()) return;
    up(false);
    Wire_::stopWire();
    auto clear = ZmRef<ClientH2ClearState<Logical>>{
      new ClientH2ClearState<Logical>{}};
    auto pending = ZuMv(m_pending);
    unsigned pendingHead = m_pendingHead;
    bool cancelled = m_stopping;
    m_pending.length(0);
    m_pendingHead = 0;
    Wire_::clearStreams(
      [clear](auto &entry) {
	if (!entry.notified) clear->active.push(entry.logical);
      }, [
      link = ZmRef(this), clear = ZuMv(clear),
      pending = ZuMv(pending), pendingHead, cancelled, peer
    ]() mutable {
      link->disconnectBatch_(
	ZuMv(clear->active), 0,
	ZuMv(pending), pendingHead, cancelled, peer);
    });
  }
  void connectFailed(bool transient) {
    if (!isUp()) return;
    up(false);
    Wire_::stopWire();
    auto pending = ZuMv(m_pending);
    unsigned pendingHead = m_pendingHead;
    m_pending.length(0);
    m_pendingHead = 0;
    Base::disconnect();
    connectFailedBatch_(ZuMv(pending), pendingHead, transient);
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto txStream_() { return Base::txStream_(); }
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
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }
  void close(Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmRef(logical), id
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
    if (!m_goawayPosted) {
      m_goawayNext = Wire_::nextLocalStreamID();
      if (m_goawayNext >= 2) m_goawayNext -= 2;
      m_goawayLast = last;
      m_goawayPosted = true;
      goawayBatch_();
    } else if (last < m_goawayLast)
      m_goawayLast = last;
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
    Wire_::stopWire();
    Wire_::graceful();
    Base::disconnect();
  }

private:
  void disconnectBatch_(
      Active active, unsigned activei, Pending pending, unsigned pendingi,
      bool cancelled, bool peer) {
    unsigned n = 0, l = active.length(), m = pending.length();
    while (n < ClientWorkBatch && activei < l) {
      auto logical = ZuMv(active[activei++]);
      ++n;
      if (logical->result() == ResultCode::OK)
	logical->result_(cancelled ?
	  ResultCode::Cancelled : ResultCode::Indeterminate);
      logical->disconnected_(peer);
    }
    while (n < ClientWorkBatch && pendingi < m) {
      auto logical = ZuMv(pending[pendingi++]);
      ++n;
      if (logical->result() == ResultCode::OK)
	logical->result_(cancelled ?
	  ResultCode::Cancelled : ResultCode::Unprocessed);
      logical->connectFailed_(false);
    }
    if (activei < l || pendingi < m) {
      this->app()->rxRun([
	link = ZmRef(this), active = ZuMv(active), activei,
	pending = ZuMv(pending), pendingi, cancelled, peer]() mutable {
	link->disconnectBatch_(
	  ZuMv(active), activei, ZuMv(pending), pendingi, cancelled, peer);
      });
      return;
    }
    this->app()->linkDown(this);
  }

  void connectFailedBatch_(
      Pending pending, unsigned i, bool transient) {
    unsigned end = i + ClientWorkBatch;
    if (end > pending.length()) end = pending.length();
    while (i < end) {
      auto logical = ZuMv(pending[i++]);
      if (logical->result() == ResultCode::OK)
	logical->result_(ResultCode::Unprocessed);
      logical->connectFailed_(transient);
    }
    if (i < pending.length()) {
      this->app()->rxRun([
	link = ZmRef(this), pending = ZuMv(pending), i, transient
      ]() mutable {
	link->connectFailedBatch_(ZuMv(pending), i, transient);
      });
      return;
    }
    this->app()->linkDown(this);
  }

  bool pending_() const { return m_pendingHead < m_pending.length(); }
  unsigned pendingCount_() const {
    return m_pending.length() - m_pendingHead;
  }
  void admit_() {
    if (m_admitPosted) return;
    m_admitPosted = true;
    admitBatch_();
  }
  void admitBatch_() {
    if (!m_ready || m_down || m_stopping) {
      m_admitPosted = false;
      return;
    }
    unsigned n = 0;
    while (n++ < ClientWorkBatch && pending_() &&
	Wire_::canOpenLocalStream())
      openNow_(ZuMv(m_pending[m_pendingHead++]));
    if (pending_() && Wire_::canOpenLocalStream()) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->admitBatch_(); });
      return;
    }
    m_admitPosted = false;
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

  void goawayBatch_() {
    if (m_down) {
      m_goawayPosted = false;
      return;
    }
    unsigned n = 0;
    while (n++ < ClientWorkBatch && m_goawayNext > m_goawayLast) {
      uint32_t id = m_goawayNext;
      m_goawayNext = id >= 2 ? id - 2 : 0;
      if (auto entry = Wire_::h2Stream(id)) {
	entry->logical->result_(ResultCode::Unprocessed);
	notify_(id, true);
      }
    }
    if (m_goawayNext > m_goawayLast) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->goawayBatch_(); });
      return;
    }
    m_goawayPosted = false;
  }

  // Stable for the native TLS connection lifetime.
  Ztls::Host	m_host;
  uint16_t	m_port = 0;
  unsigned	m_id = 0;
  uint32_t	m_pendingMax = 0;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Pending	m_pending;
  unsigned	m_pendingHead = 0;
  unsigned	m_hubSlot = NullSlot;
  uint32_t	m_goawayNext = 0;
  uint32_t	m_goawayLast = 0;
  bool		m_ready = false;
  bool		m_down = false;
  bool		m_draining = false;
  bool		m_stopping = false;
  bool		m_requeuePosted = false;
  bool		m_admitPosted = false;
  bool		m_goawayPosted = false;
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
  auto transmit(Builder &) {
    auto tx = txStream();
    tx.plan(0);
    return tx;
  }
  template <typename Builder>
  auto transmit_(Builder &builder) { return transmit(builder); }
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
  void released_() {
    m_native = nullptr;
    m_connected = false;
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
  public H2_::ClientLogical<App, Impl, H2_::CliLinkT<App>> {
  using Base =
    H2_::ClientLogical<App, Impl, H2_::CliLinkT<App>>;

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
template <
  typename App, typename H1Logical, typename H2Logical,
  typename Heap = ZuVoid>
class CliLink;
template <typename App, typename H1Logical, typename H2Logical>
using CliLinkHeap = ZmHeap<"Zhttp.TLS.Link",
  CliLink<App, H1Logical, H2Logical>>;
template <typename App, typename H1Logical, typename H2Logical>
using CliLinkT = CliLink<App, H1Logical, H2Logical,
  CliLinkHeap<App, H1Logical, H2Logical>>;

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
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  template <typename Builder>
  auto transmit_(Builder &) { return m_native->txStream_(); }
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
    m_nativeSlot = NullSlot;
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
    m_nativeSlot = NullSlot;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void released_() {
    m_native = nullptr;
    m_nativeSlot = NullSlot;
    m_connected = false;
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    m_native = nullptr;
    m_nativeSlot = NullSlot;
    m_app->connectFailed(*impl(), transient);
  }
  int8_t result() const { return m_result; }
  void result_(int8_t result) { m_result = result; }
  int process_(Ztls::RxStream &rx) {
    return m_app->process(*impl(), rx);
  }
  void native(NativeLink *native) {
    m_native = native;
  }
  unsigned nativeSlot() const { return m_nativeSlot; }
  void nativeSlot(unsigned slot) { m_nativeSlot = slot; }
  template <typename State> void responseHeadersParsed(State *) { }
  template <typename State> void responseBodyBytes(State *) { }

private:
  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  unsigned	m_nativeSlot = NullSlot;
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
  using Link = CliLinkT<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using Choice = ClientChoice<H1Logical, H2Logical>;
  using Pending = ZtArray<Choice,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using Slots = ZtArray<ZmRef<Link>,
    ZtArrayHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }

  void capacity(unsigned id, uint64_t generation, bool saturated) {
    tlsCapacity_(user(), id, generation, saturated, 0);
  }
  bool txError(unsigned id, uint64_t generation, ZeException &e) {
    return nativeTxError_(
      user(), Transport::TLS, id, generation, e, 0);
  }
  void txFailed(unsigned id, uint64_t generation) {
    auto entry = m_generations->findPtr(generation);
    if (!entry || entry->id != id) return;
    entry->owner.object<Link>()->disconnectNative();
  }

  ClientHub() :
    m_pool{new H2_::ClientPoolHash},
    m_generations{new H2_::ClientGenHash} { }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::clientParams(hub, config));
  }
  void connect(
    H1Logical *h1, H2Logical *h2, Ztls::Host host, uint16_t port,
    unsigned id, ZiIP remote = {}) {
    ZmRef<H1Logical> h1_ = ZmRef(h1);
    ZmRef<H2Logical> h2_ = ZmRef(h2);
    this->rxInvoke([
      this, h1 = ZuMv(h1_), h2 = ZuMv(h2_),
      host = ZuMv(host), remote = ZuMv(remote), port, id
    ]() mutable {
      h1->prepare_();
      h2->prepare_();
      H2_::ClientPoolKey key{host, port, id};
      auto poolEntry = m_pool->findPtr(key);
      ZmRef<Link> link;
      if (poolEntry) link = poolEntry->owner.object<Link>();
      if (!link) {
	link = new Link{
	  this, host, port, id, ++m_linkGeneration, m_config};
	link->hubSlot(m_slots.length());
	nativeUp_(user(), id, link->generation(), link, 0);
	m_slots.push(link);
	m_generations->add(H2_::ClientGenEntry{
	  link->generation(), id, link});
	if (poolEntry)
	  poolEntry->owner = link;
	else
	  m_pool->add(H2_::ClientPoolEntry{ZuMv(key), link});
	link->add({ZuMv(h1), ZuMv(h2)});
	if (remote)
	  link->connect(ZuMv(host), port, ZuMv(remote));
	else
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
      m_stopScan = 0;
      m_stopScanning = true;
      stopLinksBatch_();
    });
  }
  void linkDown(
      Link *link, Pending reconnect = {}, unsigned reconnectHead = 0) {
    nativeDown_(user(), link->id(), link->generation(), 0);
    auto generation = m_generations->del(link->generation());
    ZmAssert(generation &&
	generation->owner.template object<Link>() == link);
    if (auto entry = m_pool->findPtr(
	H2_::ClientPoolKey{link->host(), link->port(), link->id()}))
      if (entry->owner.object<Link>() == link)
	m_pool->delNode(
	  static_cast<H2_::ClientPoolHash::Node *>(entry));
    unsigned i = link->hubSlot();
    if (i < m_slots.length() && m_slots[i].ptr() == link) {
      unsigned last = m_slots.length() - 1;
      if (i != last) {
	m_slots[i] = ZuMv(m_slots[last]);
	m_slots[i]->hubSlot(i);
      }
      m_slots.length(last);
      link->hubSlot(NullSlot);
      if (m_stopping && i < m_stopScan && last >= m_stopScan)
	m_stopScan = i;
    }
    capacity(link->id(), link->generation(), false);
    if (reconnect)
      reconnect_(ZuMv(reconnect), reconnectHead,
	link->host(), link->port(), link->id());
    if (m_stopping && m_stopPending && !--m_stopPending && !m_stopScanning)
      stopDrain_();
  }
  void goaway(uint32_t) { }
  void final() {
    for ([[maybe_unused]] auto &slot: m_slots)
      ZmAssert(!slot->isUp());
    m_slots.length(0);
    ZmAssert(!m_pool->count_());
    m_pool->clean();
    ZmAssert(!m_generations->count_());
    m_generations->clean();
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }

private:
  void stopLinksBatch_() {
    Slots close;
    unsigned inspected = 0, n = m_slots.length();
    while (m_stopScan < n &&
	inspected++ < ClientWorkBatch) {
      auto &slot = m_slots[m_stopScan++];
      if (!slot->isUp() || slot->stopping())
	continue;
      ++m_stopPending;
      close.push(slot);
    }
    for (auto &link: close) link->beginStop();
    if (m_stopScan < n) {
      this->rxRun([this]() { stopLinksBatch_(); });
      return;
    }
    m_stopScanning = false;
    if (!m_stopPending) stopDrain_();
  }

  void reconnect_(
      Pending pending, unsigned i,
      Ztls::Host host, uint16_t port, unsigned id) {
    unsigned end = i + ClientWorkBatch;
    if (end > pending.length()) end = pending.length();
    while (i < end) {
      auto choice = ZuMv(pending[i++]);
      connect(choice.h1, choice.h2, host, port, id);
    }
    if (i < pending.length())
      this->rxRun([
	this, pending = ZuMv(pending), i,
	host = ZuMv(host), port, id
      ]() mutable {
	reconnect_(ZuMv(pending), i, ZuMv(host), port, id);
      });
  }

  template <typename A>
  static auto tlsCapacity_(
      A *app, unsigned id, uint64_t generation, bool saturated, int) ->
    decltype(app->tlsCapacity(id, generation, saturated), void()) {
    app->tlsCapacity(id, generation, saturated);
  }
  static void tlsCapacity_(...) { }

  template <typename A, typename Native>
  static auto nativeUp_(
      A *app, unsigned id, uint64_t generation,
      const ZmRef<Native> &native, int) ->
    decltype(app->nativeUp(id, generation, native), void()) {
    app->nativeUp(id, generation, native);
  }
  template <typename A, typename Native>
  static void nativeUp_(
      A *, unsigned, uint64_t, const ZmRef<Native> &, long) { }

  template <typename A>
  static auto nativeDown_(
      A *app, unsigned id, uint64_t generation, int) ->
    decltype(app->nativeDown(id, generation), void()) {
    app->nativeDown(id, generation);
  }
  template <typename A>
  static void nativeDown_(A *, unsigned, uint64_t, long) { }

  template <typename A>
  static auto nativeTxError_(
      A *app, Transport::T transport, unsigned id,
      uint64_t generation, ZeException &e, int) ->
    decltype(app->nativeTxError(transport, id, generation, e), bool()) {
    return app->nativeTxError(transport, id, generation, e);
  }
  template <typename A>
  static bool nativeTxError_(
      A *, Transport::T, unsigned, uint64_t, ZeException &, long) {
    return false;
  }

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
    m_stopScan = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  // Stable after initialization.
  ZmRef<H2_::ClientPoolHash> m_pool;
  ZmRef<H2_::ClientGenHash> m_generations;
  H2Config	m_config;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Slots		m_slots;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  unsigned	m_stopScan = 0;
  uint64_t	m_linkGeneration = 0;
  bool		m_stopping = false;
  bool		m_stopScanning = false;
};

template <
  typename App, typename H1Logical_, typename H2Logical_, typename Heap>
class CliLink :
  public Heap,
  public Ztls::CliLink<
    ClientHub<App, H1Logical_, H2Logical_>,
    CliLink<App, H1Logical_, H2Logical_, Heap>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<
    CliLink<App, H1Logical_, H2Logical_, Heap>, H2Logical_> {
public:
  using Hub = ClientHub<App, H1Logical_, H2Logical_>;
  using H1Logical = H1Logical_;
  using H2Logical = H2Logical_;
  using Choice = typename Hub::Choice;
  using Base = Ztls::CliLink<Hub, CliLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<CliLink, H2Logical>;
  using Base::up;
  using Pending = typename Hub::Pending;
  using H1Active = ZtArray<ZmRef<H1Logical>,
    ZtArrayHeapID<"Zhttp.H1.TLS.Active">>;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  CliLink(
    Hub *app, Ztls::Host host, uint16_t port, unsigned id,
    uint64_t generation,
    const H2Config &config) :
      Base{app}, m_host{ZuMv(host)}, m_port{port},
      m_id{id}, m_generation{generation}, m_pendingMax{config.maxPending()},
      m_activeMax{config.maxConcurrentStreams()}, m_policy{config.policy()}
  {
    Wire::initWire(false, config);
    Base::txErrorFn(ZiTxErrorFn{
      [this, generation](ZeException &e) {
	return this->app()->txError(m_id, generation, e);
      }});
  }
  ~CliLink() {
    Wire::finalWire();
  }

  const Ztls::Host &host() const { return m_host; }
  uint16_t port() const { return m_port; }
  unsigned id() const { return m_id; }
  uint64_t generation() const { return m_generation; }
  unsigned hubSlot() const { return m_hubSlot; }
  void hubSlot(unsigned slot) { m_hubSlot = slot; }
  bool isUp() const { return !m_down; }
  void up(bool value) { m_down = !value; }
  bool stopping() const { return m_stopping; }
  bool available() const {
    return !saturated();
  }
  bool saturated() const {
    if (!isUp() || m_stopping || m_draining || this->disconnecting() ||
	(m_ready && !this->cxn())) return true;
    if (m_flowSaturated) return true;
    if (!m_ready) return pendingCount_() >= m_activeMax;
    switch (m_version) {
      case Version::H1:
	return m_h1.length() + pendingCount_() >= m_activeMax;
      case Version::H2:
	return Wire::localStreamsExhausted() ||
	  !Wire::canOpenLocalStream();
      default:
	return true;
    }
  }
  void add(Choice choice) {
    if (m_down || m_stopping) {
      fail_(choice, false);
      capacity_();
      return;
    }
    if (m_draining || this->disconnecting()) {
      m_pending.push(ZuMv(choice));
      requeue_();
      capacity_();
      return;
    }
    if (pendingCount_() >= m_pendingMax &&
	(!m_ready || m_version != Version::H2 ||
	 !Wire::canOpenLocalStream())) {
      fail_(choice, false);
      capacity_();
      return;
    }
    if (!m_ready || pending_() ||
	m_version == Version::H1 && m_h1.length() >= m_activeMax ||
	m_version == Version::H2 && !Wire::canOpenLocalStream()) {
      m_pending.push(ZuMv(choice));
      capacity_();
      return;
    }
    open_(ZuMv(choice));
    capacity_();
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
    if (!isUp()) return;
    up(false);
    capacity_();
    Wire::stopWire();
    auto h1 = ZuMv(m_h1);
    m_h1.init_();
    auto clear = ZmRef<ClientH2ClearState<H2Logical>>{
      new ClientH2ClearState<H2Logical>{}};
    bool replacing = m_replacing;
    bool cancelled = m_stopping;
    auto pending = ZuMv(m_pending);
    unsigned pendingHead = m_pendingHead;
    m_pending.length(0);
    m_pendingHead = 0;
    Wire::clearStreams(
      [clear](auto &entry) {
	if (!entry.notified) clear->active.push(entry.logical);
      }, [
      link = ZmRef(this), h1 = ZuMv(h1), clear = ZuMv(clear),
      pending = ZuMv(pending), pendingHead, replacing, cancelled, peer
    ]() mutable {
      link->disconnectBatch_(
	ZuMv(h1), 0, ZuMv(clear->active), 0,
	ZuMv(pending), pendingHead,
	pendingHead, replacing, cancelled, peer);
    });
  }
  void connectFailed(bool transient) {
    if (!isUp()) return;
    up(false);
    capacity_();
    Wire::stopWire();
    auto pending = ZuMv(m_pending);
    unsigned pendingHead = m_pendingHead;
    m_pending.length(0);
    m_pendingHead = 0;
    Base::disconnect();
    connectFailedBatch_(ZuMv(pending), pendingHead, transient);
  }
  int process(Ztls::RxStream &rx) {
    switch (m_version) {
      case Version::H1:
	return m_h1 ? m_h1[0]->process_(rx) : -1;
      case Version::H2: return Wire::process(rx);
      default: return -1;
    }
  }
  auto txStream() { return Base::txStream(); }
  auto txStream_() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return H2_::HeaderBlock<CliLink>{
      *this, this->encoder(), id, this->peerFrameSize()};
  }
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }
  void disconnectNative() {
    m_draining = true;
    Base::disconnect();
  }
  template <typename Done>
  void releaseH1(H1Logical *logical, Done &&done) {
    this->app()->rxRun([
      link = this, logical = ZmRef(logical),
      done = ZuFwd<Done>(done)
    ]() mutable {
      bool released = false;
      unsigned i = logical->nativeSlot();
      if (i < link->m_h1.length() &&
	  link->m_h1[i].ptr() == logical.ptr()) {
	unsigned last = link->m_h1.length() - 1;
	link->m_h1[i]->released_();
	if (i != last) {
	  link->m_h1[i] = ZuMv(link->m_h1[last]);
	  link->m_h1[i]->nativeSlot(i);
	}
	link->m_h1.length(last);
	released = true;
      }
      done();
      if (!released) return;
      auto app = link->app();
      app->txRun([link = ZuMv(link)]() mutable {
	auto app = link->app();
	app->rxRun([link = ZuMv(link)]() mutable {
	  if (!link->m_down && !link->disconnecting() &&
	      (!link->m_ready || link->cxn()))
	    link->admit_();
	});
      });
    });
  }

  void close(H2Logical *logical, uint32_t id) {
    this->app()->rxInvoke([
      link = this, logical = ZmRef(logical), id
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
    if (!m_goawayPosted) {
      m_goawayNext = Wire::nextLocalStreamID();
      if (m_goawayNext >= 2) m_goawayNext -= 2;
      m_goawayLast = last;
      m_goawayPosted = true;
      goawayBatch_();
    } else if (last < m_goawayLast)
      m_goawayLast = last;
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
  void h2CapacityTx_(bool saturated) {
    this->app()->rxRun([
      link = ZmRef(this), saturated]() mutable {
      if (link->m_down) return;
      link->m_flowSaturated = saturated;
      link->capacity_();
    });
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    m_replacing = false;
    m_requeuePosted = false;
    capacity_();
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
  void disconnectBatch_(
      H1Active h1, unsigned h1i, Active active, unsigned activei,
      Pending pending, unsigned pendingi, unsigned reconnectHead,
      bool replacing, bool cancelled, bool peer) {
    unsigned n = 0, l = h1.length(), m = active.length(),
      k = pending.length();
    while (n < ClientWorkBatch && h1i < l) {
      auto logical = ZuMv(h1[h1i++]);
      ++n;
      if (logical->result() == ResultCode::OK)
	logical->result_(cancelled ?
	  ResultCode::Cancelled : ResultCode::Indeterminate);
      logical->disconnected_(peer);
    }
    while (n < ClientWorkBatch && activei < m) {
      auto logical = ZuMv(active[activei++]);
      ++n;
      if (logical->result() == ResultCode::OK)
	logical->result_(cancelled ?
	  ResultCode::Cancelled : ResultCode::Indeterminate);
      logical->disconnected_(peer);
    }
    if (replacing)
      pendingi = k;
    else
      while (n < ClientWorkBatch && pendingi < k) {
	auto &choice = pending[pendingi++];
	++n;
	if (cancelled)
	  cancel_(choice);
	else
	  fail_(choice, false);
      }
    if (h1i < l || activei < m || pendingi < k) {
      this->app()->rxRun([
	link = ZmRef(this), h1 = ZuMv(h1), h1i,
	active = ZuMv(active), activei, pending = ZuMv(pending), pendingi,
	reconnectHead, replacing, cancelled, peer]() mutable {
	link->disconnectBatch_(
	  ZuMv(h1), h1i, ZuMv(active), activei,
	  ZuMv(pending), pendingi, reconnectHead,
	  replacing, cancelled, peer);
      });
      return;
    }
    this->app()->linkDown(
      this, replacing ? ZuMv(pending) : Pending{},
      replacing ? reconnectHead : 0);
  }

  void connectFailedBatch_(
      Pending pending, unsigned i, bool transient) {
    unsigned end = i + ClientWorkBatch;
    if (end > pending.length()) end = pending.length();
    while (i < end) fail_(pending[i++], transient);
    if (i < pending.length()) {
      this->app()->rxRun([
	link = ZmRef(this), pending = ZuMv(pending), i, transient
      ]() mutable {
	link->connectFailedBatch_(ZuMv(pending), i, transient);
      });
      return;
    }
    this->app()->linkDown(this);
  }

  bool pending_() const { return m_pendingHead < m_pending.length(); }
  unsigned pendingCount_() const {
    return m_pending.length() - m_pendingHead;
  }
  void open_(Choice choice) {
    switch (m_version) {
      case Version::H1:
	choice.h1->native(this);
	choice.h1->nativeSlot(m_h1.length());
	choice.h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = "http/1.1",
	  .version = uint32_t(m_tlsVersion),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	m_h1.push(ZuMv(choice.h1));
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
    if (m_admitPosted) return;
    m_admitPosted = true;
    admitBatch_();
  }
  void admitBatch_() {
    if (!m_ready || m_down || m_stopping || this->disconnecting()) {
      m_admitPosted = false;
      return;
    }
    unsigned n = 0;
    switch (m_version) {
      case Version::H1:
	while (n++ < ClientWorkBatch && pending_() &&
	    m_h1.length() < m_activeMax)
	  open_(ZuMv(m_pending[m_pendingHead++]));
	break;
      case Version::H2:
	while (n++ < ClientWorkBatch && pending_() &&
	    Wire::canOpenLocalStream())
	  open_(ZuMv(m_pending[m_pendingHead++]));
	break;
    }
    bool more = pending_() &&
      (m_version == Version::H1 && m_h1.length() < m_activeMax ||
	m_version == Version::H2 && Wire::canOpenLocalStream());
    if (more) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->admitBatch_(); });
      capacity_();
      return;
    }
    m_admitPosted = false;
    if (!pending_()) {
      m_pending.length(0);
      m_pendingHead = 0;
    }
    if (m_version == Version::H2 && Wire::localStreamsExhausted()) {
      m_draining = true;
      requeue_();
    }
    capacity_();
  }
  void capacity_() {
    this->app()->capacity(m_id, m_generation, saturated());
  }
  void requeue_() {
    if (!pending_() || m_requeuePosted) return;
    m_requeuePosted = true;
    m_replacing = true;
    m_draining = true;
    capacity_();
    Base::disconnect();
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
  void cancel_(Choice &choice) {
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

  void goawayBatch_() {
    if (m_down) {
      m_goawayPosted = false;
      return;
    }
    unsigned n = 0;
    while (n++ < ClientWorkBatch && m_goawayNext > m_goawayLast) {
      uint32_t id = m_goawayNext;
      m_goawayNext = id >= 2 ? id - 2 : 0;
      if (auto entry = Wire::h2Stream(id)) {
	entry->logical->result_(ResultCode::Unprocessed);
	notify_(id, true);
      }
    }
    if (m_goawayNext > m_goawayLast) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->goawayBatch_(); });
      return;
    }
    m_goawayPosted = false;
  }

  // Stable for the native TLS connection lifetime.
  Ztls::Host		m_host;
  uint16_t		m_port = 0;
  unsigned		m_id = 0;
  uint64_t		m_generation = 0;
  uint32_t		m_pendingMax = 0;
  uint32_t		m_activeMax = 0;
  int8_t		m_policy = H2Policy::Force;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Pending		m_pending;
  H1Active		m_h1;
  unsigned		m_pendingHead = 0;
  unsigned		m_hubSlot = NullSlot;
  uint32_t		m_goawayNext = 0;
  uint32_t		m_goawayLast = 0;
  int			m_tlsVersion = 0;
  int8_t		m_version = -1;
  bool			m_ready = false;
  bool			m_down = false;
  bool			m_draining = false;
  bool			m_stopping = false;
  bool			m_requeuePosted = false;
  bool			m_flowSaturated = false;
  bool			m_replacing = false;
  bool			m_admitPosted = false;
  bool			m_goawayPosted = false;
};

} // namespace TLS_

namespace H3_ {

template <typename App>
class ClientHub;
template <typename App, typename Logical, typename Heap = ZuVoid>
struct CliLink;
template <typename App, typename Logical>
using CliLinkHeap = ZmHeap<"Zhttp.H3.Link", CliLink<App, Logical>>;
template <typename App, typename Logical>
using CliLinkT =
  CliLink<App, Logical, CliLinkHeap<App, Logical>>;
template <typename App, typename Logical>
struct ClientStream;
struct CliLinkKey {
  Zquic::Host	host;
  ZiIP		remote;
  uint16_t	port = 0;
  unsigned	id = 0;

  friend bool operator ==(const CliLinkKey &l, const CliLinkKey &r) {
    return l.port == r.port && l.id == r.id &&
      l.host == r.host && l.remote == r.remote;
  }
  uint32_t hash() const {
    return ZuHash<Zquic::Host>::hash(host) ^
      ZuHash<ZiIP>::hash(remote) ^ uint32_t(port) ^ id;
  }
};

struct CliLinkEntry {
  using CloseFn = void (*)(void *);
  using DownFn = bool (*)(void *);
  using PrintDiagFn = void (*)(void *);

  CliLinkKey		key;
  uint64_t		generation = 0;
  ZmContext		owner;
  CloseFn		close = nullptr;
  DownFn		down = nullptr;
  PrintDiagFn		printDiag = nullptr;
  bool			stopping = false;
};

struct CliLinkClose {
  ZmContext		owner;
  CliLinkEntry::CloseFn close = nullptr;
};

struct CliLinkRetired {
  using SlotFn = void (*)(void *, unsigned);

  ZmContext	owner;
  SlotFn	slot = nullptr;
};

inline const CliLinkKey &CliLinkEntry_KeyAxor(const CliLinkEntry &entry) {
  return entry.key;
}

ZmHashDerive(CliLinkHash, CliLinkEntry,
  (ZmHashNode<CliLinkEntry,
    ZmHashKey<CliLinkEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H3.ClientLinks">>>>));

struct CliLinkGenEntry {
  uint64_t	generation = 0;
  unsigned	id = 0;
  ZmContext	owner;
  CliLinkEntry::CloseFn close = nullptr;
};

inline uint64_t CliLinkGenEntry_KeyAxor(const CliLinkGenEntry &entry) {
  return entry.generation;
}

ZmHashDerive(CliLinkGenHash, CliLinkGenEntry,
  (ZmHashNode<CliLinkGenEntry,
    ZmHashKey<CliLinkGenEntry_KeyAxor,
      ZmHashLock<ZmNoLock,
	ZmHashHeapID<"Zhttp.H3.Generations">>>>));

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

  ClientHub() :
    m_links{new CliLinkHash}, m_generations{new CliLinkGenHash} { }

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  void capacity(unsigned id, uint64_t generation, bool saturated) {
    quicCapacity_(user(), id, generation, saturated, 0);
  }
  bool txError(unsigned id, uint64_t generation, ZeException &e) {
    return nativeTxError_(
      user(), Transport::QUIC, id, generation, e, 0);
  }
  void txFailed(unsigned id, uint64_t generation) {
    auto entry = m_generations->findPtr(generation);
    if (!entry || entry->id != id) return;
    entry->close(entry->owner.object<void>());
  }

  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port) {
    connect_(link, ZuMv(host), port, {});
  }
  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port, ZiIP remote) {
    connect_(link, ZuMv(host), port, ZuMv(remote));
  }

private:
  template <typename A>
  static auto quicCapacity_(
      A *app, unsigned id, uint64_t generation, bool saturated, int) ->
    decltype(app->quicCapacity(id, generation, saturated), void()) {
    app->quicCapacity(id, generation, saturated);
  }
  static void quicCapacity_(...) { }

  template <typename A, typename Native>
  static auto nativeUp_(
      A *app, unsigned id, uint64_t generation,
      const ZmRef<Native> &native, int) ->
    decltype(app->nativeUp(id, generation, native), void()) {
    app->nativeUp(id, generation, native);
  }
  template <typename A, typename Native>
  static void nativeUp_(
      A *, unsigned, uint64_t, const ZmRef<Native> &, long) { }

  template <typename A>
  static auto nativeDown_(
      A *app, unsigned id, uint64_t generation, int) ->
    decltype(app->nativeDown(id, generation), void()) {
    app->nativeDown(id, generation);
  }
  template <typename A>
  static void nativeDown_(A *, unsigned, uint64_t, long) { }

  template <typename A>
  static auto nativeTxError_(
      A *app, Transport::T transport, unsigned id,
      uint64_t generation, ZeException &e, int) ->
    decltype(app->nativeTxError(transport, id, generation, e), bool()) {
    return app->nativeTxError(transport, id, generation, e);
  }
  template <typename A>
  static bool nativeTxError_(
      A *, Transport::T, unsigned, uint64_t, ZeException &, long) {
    return false;
  }

  template <typename Logical>
  void connect_(
    Logical *logical_, Zquic::Host host, uint16_t port, ZiIP remote) {
    using Link = CliLinkT<App, Logical>;
    ZmRef<Logical> logical = ZmRef(logical_);
    this->rxInvoke([
      this, logical = ZuMv(logical), host = ZuMv(host),
      remote = ZuMv(remote), port
    ]() mutable {
      logical->prepare_();
      ZmRef<Link> link;
      unsigned id = ClientLogicalID_(logical.ptr(), 0);
      CliLinkKey key{host, remote, port, id};
      if (auto entry = m_links->findPtr(key))
	link = entry->owner.object<Link>();
      if (!link) {
	link = new Link{
	  this, host, port, remote, id, ++m_linkGeneration};
	nativeUp_(user(), id, link->generation, link, 0);
	m_links->add(CliLinkEntry{
	  .key = key,
	  .generation = link->generation,
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
	m_generations->add(CliLinkGenEntry{
	  .generation = link->generation,
	  .id = id,
	  .owner = link,
	  .close = [](void *ptr) {
	    static_cast<Link *>(ptr)->abort();
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
    nativeDown_(user(), link->id, link->generation, 0);
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
    capacity(link->id, link->generation, false);
    if (m_stopping && m_stopPending && !--m_stopPending) {
      if (!m_stopPublishing) {
	if (!m_stopScanning)
	  stopBase_();
	else
	  this->rxRun([this]() { stopLinksBatch_(); });
      }
    }
  }
  template <typename Link>
  void removeLink(Link *link) {
    if (!link->indexed) return;
    CliLinkKey key{link->host, link->remote, link->port, link->id};
    auto entry = m_links->del(key);
    ZmAssert(entry && entry->owner.object<Link>() == link);
    auto generation = m_generations->del(link->generation);
    ZmAssert(generation &&
	generation->owner.template object<Link>() == link);
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
    ZmAssert(!m_generations->count_());
    ZmAssert(!m_retired.length());
    Zquic::Client<ClientHub>::final();
  }

private:
  void stopRx_(StopFn done) {
    m_stopFns.push(ZuMv(done));
    if (m_stopping) return;
    m_stopping = true;
    m_stopPending = 0;
    m_stopScanning = true;
    stopLinksBatch_();
  }

  void stopLinksBatch_() {
    ZtArray<CliLinkClose,
      ZtArrayHeapID<"Zhttp.H3.ClientStopBatch">> close;
    bool exhausted = false;
    {
      auto i = m_links->iter();
      while (close.length() < ClientWorkBatch) {
	auto entry = i();
	if (!entry) {
	  exhausted = true;
	  break;
	}
	if (entry->stopping ||
	    entry->down(entry->owner.object<void>())) continue;
	entry->stopping = true;
	++m_stopPending;
	close.push(CliLinkClose{entry->owner, entry->close});
      }
    }
    if (exhausted) m_stopScanning = false;
    m_stopPublishing = true;
    for (auto &entry: close)
      entry.close(entry.owner.object<void>());
    m_stopPublishing = false;
    if (!m_stopPending)
      if (!m_stopScanning)
	stopBase_();
      else
	this->rxRun([this]() { stopLinksBatch_(); });
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

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  ZmRef<CliLinkHash> m_links;
  ZmRef<CliLinkGenHash> m_generations;
  Retired	m_retired;
  StopFns	m_stopFns;
  uint64_t	m_linkGeneration = 0;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
  bool		m_stopScanning = false;
  bool		m_stopPublishing = false;
};

template <typename App, typename Logical>
struct ClientStream :
  public Zquic::CliStream<
    CliLinkT<App, Logical>, ClientStream<App, Logical>>,
  public H3::CxnStream<ClientStream<App, Logical>,
    H3::Cxn<CliLinkT<App, Logical>,
      ZmRef<ClientStream<App, Logical>>>> {
  using Link = CliLinkT<App, Logical>;
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

template <typename App, typename Logical, typename Heap>
struct CliLink :
  public Heap,
  public Zquic::CliLink<ClientHub<App>, CliLink<App, Logical, Heap>,
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

  CliLink(
    Hub *app, Zquic::Host host_, uint16_t port_, ZiIP remote_,
    unsigned id_, uint64_t generation_) :
    Base{app}, host{ZuMv(host_)}, remote{ZuMv(remote_)},
    port{port_}, id{id_}, generation{generation_} {
    h3.txErrorFn(ZiTxErrorFn{
      [this, id_, generation_](ZeException &e) {
	return this->app()->txError(id_, generation_, e);
      }});
  }

  unsigned txQueueMax() const {
    return this->app()->user()->quicConfig().maxQueuedFrames();
  }

  void flowBlocked(
      Zquic::FrameType::T type, uint64_t,
      Zquic::StreamType::T streamType, uint64_t) {
    switch (type) {
      case Zquic::FrameType::DataBlocked:
	dataBlockedTx = true;
	break;
      case Zquic::FrameType::StreamDataBlocked:
	break;
      case Zquic::FrameType::StreamsBlocked:
	if (streamType == Zquic::StreamType::Duplex)
	  streamsBlockedTx = true;
	break;
      default:
	break;
    }
    capacityTx_();
  }
  void flowCredit_(
      Zquic::FrameType::T type, uint64_t,
      Zquic::StreamType::T) {
    switch (type) {
      case Zquic::FrameType::MaxData:
	dataBlockedTx = false;
	break;
      case Zquic::FrameType::MaxStreamData:
	break;
      default:
	break;
    }
    capacityTx_();
  }
  void streamCredit_(Zquic::StreamType::T type) {
    if (type != Zquic::StreamType::Duplex) return;
    streamsBlockedTx = false;
    capacityTx_();
  }

  void add(ZmRef<Logical> logical) {
    logical->native(ZmRef(this));
    if (!ready || readyDraining) {
      queue_(pending, pendingLive, QueueSlot::Pending, ZuMv(logical));
      return;
    }
    open(ZuMv(logical));
  }
  void open(ZmRef<Logical> logical) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical stream admission outside Rx thread", return);
    queue_(waiting, waitingLive, QueueSlot::Waiting, ZuMv(logical));
    this->app()->txRun([link = ZmRef(this)]() mutable {
      auto stream = link->stream(Zquic::StreamType::Duplex);
      link->capacityTx_();
      if (!stream) return;
      link->app()->rxRun([
	link = ZuMv(link), stream = ZuMv(stream)
      ]() mutable { link->streamed(ZuMv(stream)); });
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
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    auto link = ZmRef(this);
    this->app()->txRun([
      link, limits, extendedConnect, txError = ZuMv(txError)
    ]() mutable {
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      typename H3Cxn::LocalStreams streams;
      if (ok)
	streams = H3Cxn::openLocalStreams(*link, txError);
      link->app()->rxRun([
	link = ZuMv(link), limits, extendedConnect, ok,
	streams = ZuMv(streams)
      ]() mutable {
	if (!ok || link->down) {
	  link->connectFailed(false);
	  return;
	}
	auto params = H3::Params().qpackLimits(limits);
	if (!link->h3.openLocal(
	    *link, ZuMv(streams), params, extendedConnect)) {
	  link->connectFailed(false);
	  return;
	}
      });
    });
  }
  void h3Ready() {
    if (ready) return;
    ready = true;
    readyDraining = true;
    h3ReadyBatch_();
  }
  void disconnected(bool peer) { closePeer = peer; }
  void migrationPromoted(const Zquic::MigrationResult &) {
    this->app()->rxRun([link = ZmRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void migrationFailed(const Zquic::MigrationResult &) {
    this->app()->rxRun([link = ZmRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void endpointDown() {
    this->app()->rxRun([link = ZmRef(this)]() mutable {
      link->endpointDown_();
    });
  }
  void endpointDown_() {
    if (finalizing) return;
    finalizing = true;
    down = true;
    this->app()->removeLink(this);
    teardownStream = 0;
    endpointDownStreams_();
  }
  void endpointDownStreams_() {
    unsigned end = teardownStream + ClientWorkBatch;
    if (end > streams.length()) end = streams.length();
    while (teardownStream < end) {
      auto stream = ZuMv(streams[teardownStream++]);
      stream->slot = QueueSlot::Invalid;
      auto logical = ZuMv(stream->logical);
      if (logical) logical->disconnected_(closePeer);
    }
    if (teardownStream < streams.length()) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->endpointDownStreams_(); });
      return;
    }
    streams.length(0);
    endpointDownPending_();
  }
  void endpointDownPending_() {
    if (!clearQueueBatch_(pending, pendingHead, pendingLive,
	[](Logical *logical) { logical->connectFailed_(false); })) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->endpointDownPending_(); });
      return;
    }
    endpointDownWaiting_();
  }
  void endpointDownWaiting_() {
    if (!clearQueueBatch_(waiting, waitingHead, waitingLive,
	[](Logical *logical) { logical->connectFailed_(false); })) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->endpointDownWaiting_(); });
      return;
    }
    h3.qpackRxTable.final();
    auto link = ZmRef(this);
    this->app()->txRun([link]() mutable {
      link->h3Tx.final();
      link->app()->rxRun([link = ZuMv(link)]() mutable {
	link->app()->linkDown(link.ptr());
      });
    });
  }
  void connectFailed(bool transient) {
    if (connectFailing || finalizing) return;
    auto self = ZmRef(this);
    connectFailing = true;
    failTransient = transient;
    down = true;
    this->app()->removeLink(this);
    connectFailedPending_();
  }
  void close(Logical *logical, Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link,
      logical = ZmRef(logical),
      stream = ZmRef(stream)
    ]() mutable {
      link->close_(ZuMv(logical), ZuMv(stream));
      return link;
    });
  }
  void finish(Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link, stream = ZmRef(stream)
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
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 local stream publication outside Rx thread", return);
    if (!stream || stream->id() < 0 ||
	Zquic::StreamID::server(uint64_t(stream->id())) ||
	Zquic::StreamID::uni(uint64_t(stream->id())))
      return;
    auto logical = shift_(waiting, waitingHead, waitingLive);
    if (!logical) {
      if (waitingLive) {
	this->app()->rxRun([
	  link = ZmRef(this), stream = ZuMv(stream)
	]() mutable { link->streamed(ZuMv(stream)); });
	return;
      }
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
  void qpackSeed(StreamRef encoder) {
    auto link = ZmRef(this);
    this->app()->txRun([link = ZuMv(link), encoder = ZuMv(encoder)]() mutable {
      if (link->h3SeedStateTx != QPackSeedState::Unseeded) return;
      auto result = installQPackSeeds(
	link->h3Tx, AppHeaderSeeds<ZuDecay<
	  decltype(*link->app()->user())>>::get(*link->app()->user()),
	[link, &encoder](ZuBSpan bytes) {
	  return link->send(encoder, bytes, false);
	});
      if (result != QPackSeedResult::Failed)
	link->h3SeedStateTx = result == QPackSeedResult::Seeded ?
	  QPackSeedState::Seeded : QPackSeedState::Disabled;
      link->app()->rxRun([link = ZuMv(link), result]() mutable {
	if (result == QPackSeedResult::Failed || link->down) {
	  link->disconnect();
	  return;
	}
	link->h3Ready();
      });
    });
  }

public:
  void h3ReadyBatch_() {
    unsigned n = 0;
    while (n++ < ClientWorkBatch) {
      auto logical = shift_(pending, pendingHead, pendingLive);
      if (!logical) break;
      open(ZuMv(logical));
    }
    if (pendingLive) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->h3ReadyBatch_(); });
      return;
    }
    readyDraining = false;
  }

  void connectFailedPending_() {
    if (!clearQueueBatch_(pending, pendingHead, pendingLive,
	[this](Logical *logical) {
	  logical->connectFailed_(failTransient);
	})) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->connectFailedPending_(); });
      return;
    }
    connectFailedWaiting_();
  }
  void connectFailedWaiting_() {
    if (!clearQueueBatch_(waiting, waitingHead, waitingLive,
	[this](Logical *logical) {
	  logical->connectFailed_(failTransient);
	})) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->connectFailedWaiting_(); });
      return;
    }
    Base::disconnect();
  }

  void capacityTx_() {
    bool saturated = dataBlockedTx || streamsBlockedTx;
    this->app()->rxRun([
      link = ZmRef(this), saturated]() mutable {
      link->app()->capacity(link->id, link->generation, saturated);
    });
  }

  void closeLater_(Stream *stream, bool peer) {
    if (!stream || stream->closing) return;
    stream->closing = true;
    this->app()->rxRun([
      link = ZmRef(this), stream = ZmRef(stream), peer
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
  static ZmRef<Logical> shift_(
      List &list, unsigned &head, unsigned &live) {
    unsigned end = head + ClientWorkBatch;
    if (end > list.length()) end = list.length();
    while (head < end && !list[head]) ++head;
    if (head == list.length()) {
      list.length(0);
      head = live = 0;
      return {};
    }
    if (head == end) return {};
    auto logical = ZuMv(list[head++]);
    --live;
    logical->h3QueueClear();
    return logical;
  }
  template <typename List, typename Fn>
  static bool clearQueueBatch_(
      List &list, unsigned &head, unsigned &live, Fn &&fn) {
    unsigned end = head + ClientWorkBatch;
    if (end > list.length()) end = list.length();
    while (head < end) {
      auto logical = ZuMv(list[head++]);
      if (!logical) continue;
      logical->h3QueueClear();
      --live;
      fn(logical.ptr());
    }
    if (head < list.length()) return false;
    list.length(0);
    head = live = 0;
    return true;
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
	break;
      case QueueSlot::Waiting:
	ZmAssert(slot < waiting.length() && waiting[slot].ptr() == logical);
	waiting[slot] = nullptr;
	logical->h3QueueClear();
	--waitingLive;
	break;
      default:
	break;
    }
  }
  void removeStream_(Stream *stream) {
    unsigned slot = stream->slot;
    ZmAssert(slot < streams.length() && streams[slot].ptr() == stream);
    if (migrationNotifying && slot < migrationStream) --migrationStream;
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
    if (migrationNotifying) return;
    migrationNotifying = true;
    migrationStream = 0;
    migrationCompleteBatch_();
  }
  void migrationCompleteBatch_() {
    Pending logical;
    unsigned end = migrationStream + ClientWorkBatch;
    if (end > streams.length()) end = streams.length();
    while (migrationStream < end) {
      auto &stream = streams[migrationStream++];
      if (stream->logical) logical.push(stream->logical);
    }
    for (auto &entry: logical) entry->migrationComplete_();
    if (migrationStream < streams.length()) {
      this->app()->rxRun([
	link = ZmRef(this)]() mutable { link->migrationCompleteBatch_(); });
      return;
    }
    migrationNotifying = false;
    migrationStream = 0;
  }

public:
  // Stable for the native connection lifetime
  Zquic::Host		host;
  ZiIP			remote;
  uint16_t		port = 0;
  unsigned		id = 0;
  uint64_t		generation = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Pending		pending;
  Waiting		waiting;
  Streams		streams;
  unsigned		pendingHead = 0;
  unsigned		waitingHead = 0;
  unsigned		pendingLive = 0;
  unsigned		waitingLive = 0;
  unsigned		teardownStream = 0;
  unsigned		migrationStream = 0;
  unsigned		retiredSlot = QueueSlot::Invalid;
  bool			ready = false;
  bool			readyDraining = false;
  bool			down = false;
  bool			indexed = true;
  bool			closePeer = false;
  bool			migrationRequested = false;
  bool			migrationDone = false;
  bool			migrationNotifying = false;
  bool			finalizing = false;
  bool			connectFailing = false;
  bool			failTransient = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
  QPackSeedState::T	h3SeedStateTx = QPackSeedState::Unseeded;
  bool			dataBlockedTx = false;
  bool			streamsBlockedTx = false;
};

} // namespace H3_

template <typename App, typename Impl>
class ClientLink<App, Impl, H3QUIC> :
  public ZmObject, public H3_::LogicalStream<Impl> {
  using Hub = H3_::ClientHub<App>;
  using NativeLink = H3_::CliLinkT<App, Impl>;
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
    m_app->connect(impl(), Zquic::Host{ZuFwd<Host>(host)}, port);
  }
  template <typename Host>
  void connect(Host &&host, uint16_t port, ZiIP remote) {
    m_app->connect(
      impl(), Zquic::Host{ZuFwd<Host>(host)}, port, ZuMv(remote));
  }
  void prepare_() {
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    m_disconnecting = false;
    m_migrationRequested = false;
    m_migrationComplete = false;
    m_disconnectPending = false;
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.tlsName, endpoint.port, endpoint.ip);
  }
  auto txStream() { return m_stream->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
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
    return transmit_(builder, m_stream->txStream());
  }
  template <typename Builder>
  auto transmit_(Builder &builder) {
    return transmit_(builder, m_stream->txStream_());
  }
  template <typename Builder, typename Tx>
  auto transmit_(Builder &builder, Tx tx) {
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    builder.h3(
      m_native->qpackTx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackEncoderWrite(span);
      },
      uint64_t(m_stream->id()), m_native->h3PeerCap(),
      &m_native->h3.params);
    return tx;
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
    if (!m_native) m_stream = nullptr;
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

  template <typename Heap = ZuVoid>
  class Link_ :
    public Heap,
    public ClientLink<Pool, Link_<Heap>, Profile_> {
  public:
    using Base = ClientLink<Pool, Link_, Profile_>;
    using Protocol = typename Profile::Protocol;
    using Session = ClientSession<
      Owner, LiveReq, Link_, Profile,
      Request, ResParser>;

    Link_(Pool *pool, unsigned id_) :
      Base{pool}, m_id{id_}, m_session{pool->owner(), this} { }

    LiveReq *request() const { return m_request; }
    bool stopped() const { return m_stopped; }
    const ConnectedInfo &info() const { return m_info; }
    unsigned id() const { return m_id; }
    unsigned slot() const { return m_slot; }
    void slot(unsigned slot_) { m_slot = slot_; }
    bool reuseListed() const { return m_reuseListed; }
    Link_ *reusePrev() const { return m_reusePrev; }
    Link_ *reuseNext() const { return m_reuseNext; }
    void reusePrev(Link_ *link) { m_reusePrev = link; }
    void reuseNext(Link_ *link) { m_reuseNext = link; }
    void reuseListed(bool listed) { m_reuseListed = listed; }

    void assign(LiveReq *request) {
      ++m_generation;
      m_request = request;
      m_complete = false;
      m_success = false;
      m_sent = false;
      m_stopped = false;
      m_closing = false;
    }
    void start() {
      if (!m_request) return;
      this->txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
	return owner()->poolTxError(*this, e);
      }});
      owner()->poolConnect(*this, *m_request);
    }
    void sendRequest() {
      if (!m_request) return;
      m_session.bind(m_request);
      m_session.reset();
      m_sent = true;
      owner()->poolSend(*this, *m_request, Message::ID);
      auto link = ZmRef(this);
      unsigned generation = m_generation;
      pool()->txRun([link = ZuMv(link), generation]() mutable {
	link->m_session.beginTx();
	link->sendRequestTx_(generation);
      });
    }
    void close() {
      if (m_closing) return;
      m_closing = true;
      auto link = ZmRef(this);
      pool()->txRun([link = ZuMv(link)]() mutable {
	link->m_session.cancelTx();
	auto pool = link->pool();
	pool->rxRun([link = ZuMv(link)]() mutable {
	  link->disconnect();
	});
      });
    }
    void retire(bool reuse = false) {
      pool()->detach(*this, m_request);
      m_request = nullptr;
      if constexpr (Message::OneMessagePerLink) {
	if (!this->active()) {
	  notifyStopped_();
	  return;
	}
	if (!m_success) close();
	return;
      } else
	if (reuse && this->active()) {
	  m_complete = false;
	  m_success = false;
	  m_sent = false;
	  m_closing = false;
	  pool()->reuseAdd_(*this);
	  return;
	}
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
      m_info = info;
      owner()->poolConnected(*this, *m_request, info);
      sendRequest();
    }
    void onDisconnected(bool peer) {
      owner()->poolDisconnected(*this, m_request, peer);
      if (m_request && !m_complete) {
	if constexpr (Message::CloseDelimited) {
	  owner()->poolCloseDelimited(*m_request);
	  m_session.eof();
	}
	if (m_request && !m_complete) m_session.fail();
      }
      notifyStopped_();
    }
    void onConnectFailed(bool transient) {
      owner()->poolConnectFailed(*this, m_request, transient);
      if (m_request && !m_complete) m_session.fail();
    }
    template <typename Rx>
    int process(Rx &rx) {
      return m_request ? m_session.process(rx) : -1;
    }
    template <
      typename Stream, typename Rx, int ID = Message::ID,
      ZuIfT<ID == Version::H1, int> = 0>
    int process(Stream, Rx &) { return -1; }

    void complete(bool ok) {
      if (!m_request || m_complete) return;
      m_complete = true;
      m_success = ok;
      auto link = ZmRef(this);
      unsigned generation = m_generation;
      bool sent = m_sent;
      pool()->txRun([link = ZuMv(link), generation, ok, sent]() mutable {
	if (sent) link->m_session.cancelTx();
	auto pool = link->pool();
	BodyCommit commit = sent ? link->m_session.commit() : BodyCommit{};
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

  private:
    void sendRequestTx_(unsigned generation) {
      bool ok = m_session.send();
      auto link = ZmRef(this);
      auto pool = this->pool();
      BodyCommit commit = m_session.commit();
      pool->rxRun([link = ZuMv(link), commit, generation, ok]() mutable {
	if (link->m_generation != generation) return;
	auto request = link->request();
	if (!request) return;
	if (ok)
	  link->owner()->poolTxCommitted(*link, *request, commit);
	else {
	  link->owner()->poolTxFailed(*link, *request, commit);
	  link->m_session.fail();
	}
      });
    }

    void notifyStopped_() {
      if (m_stopped) return;
      m_stopped = true;
      pool()->linkStopped(*this);
    }

    // Stable for the logical link lifetime.
    unsigned	m_id = 0;
    unsigned	m_slot = 0;
    Session	m_session;

    // Rx thread exclusive.
    alignas(Zm::CacheLineSize)
    LiveReq	*m_request = nullptr;
    unsigned	m_generation = 0;
    bool	m_complete = false;
    bool	m_success = false;
    bool	m_sent = false;
    bool	m_stopped = false;
    bool	m_closing = false;
    Link_	*m_reusePrev = nullptr;
    Link_	*m_reuseNext = nullptr;
    bool	m_reuseListed = false;
    ConnectedInfo m_info;

    friend Pool;
  };

  using LinkHeap = ZmHeap<"Zhttp.ClientPool.Link", Link_<>>;
  using Link = Link_<LinkHeap>;
  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.ClientPool.Links">>;
  using Reusable =
    ZtArray<Link *, ZtArrayHeapID<"Zhttp.ClientPool.Reusable">>;

  ClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  void owner(Owner *owner_) { m_owner = owner_; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }
  unsigned linkCount() const { return m_links.length(); }
  const Links &links() const { return m_links; }
  void quicCapacity(
      unsigned id, uint64_t generation, bool saturated) {
    m_owner->quicCapacity(id, generation, saturated);
  }
  template <typename Native>
  void nativeUp(
      unsigned id, uint64_t generation, const ZmRef<Native> &native) {
    m_owner->nativeUp(Transport::QUIC, id, generation, native);
  }
  void nativeDown(unsigned id, uint64_t generation) {
    m_owner->nativeDown(Transport::QUIC, id, generation);
  }
  bool nativeTxError(
      Transport::T transport, unsigned id,
      uint64_t generation, ZeException &e) {
    return m_owner->nativeTxError(transport, id, generation, e);
  }
  void txFailed(unsigned id, uint64_t generation) {
    Base::txFailed(id, generation);
  }
  void slots(unsigned n) {
    if constexpr (!Message::OneMessagePerLink) {
      m_reusable.size(n);
      while (m_reusable.length() < n) m_reusable.push(nullptr);
    }
  }

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
	request->poolSlot == link.slot() &&
	request->poolSlot < m_links.length() &&
	m_links[request->poolSlot].ptr() == &link) {
      request->poolSlot = NullSlot;
    }
  }

  ZmRef<Link> open(LiveReq *request, unsigned id) {
    if (!accepting() || !request) return {};
    if constexpr (!Message::OneMessagePerLink)
	if (id < m_reusable.length())
	  if (auto reuse = m_reusable[id]) {
	    auto link = ZmRef(reuse);
	    reuseDel_(*link);
	    ZmAssert(!link->request());
	    bool active = link->active();
	    if (!active) ++m_live;
	    link->assign(request);
	    request->poolTransport = Message::Transport::ID;
	    request->poolSlot = link->slot();
	    if (active) {
	      link->owner()->poolConnected(*link, *request, link->info());
	      link->sendRequest();
	    } else
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
    link->slot(m_links.length());
    m_links.push(link);
    ++m_live;
    link->assign(request);
    request->poolTransport = Message::Transport::ID;
    request->poolSlot = link->slot();
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
    reuseDel_(link);
    m_owner->poolStopped(link);
    if (m_live) --m_live;
    if (!m_stopping) reuseAdd_(link);
    if (m_stopping && !m_live)
      if constexpr (!Message::Multiplexed) Base::stop_();
    if constexpr (Message::Multiplexed)
      this->rxRun([this, hold = ZmRef(&link)]() mutable {
	unsigned i = hold->slot();
	unsigned n = m_links.length();
	if (i >= n || m_links[i].ptr() != hold.ptr()) return;
	if (i != --n) {
	  m_links[i] = ZuMv(m_links[n]);
	  m_links[i]->slot(i);
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
      m_stopHead = 0;
      stopBatch_();
    }
  }

  void final() {
#ifdef ZDEBUG
    for (auto link: m_reusable) ZmAssert(!link);
#endif
    m_reusable.length(0);
    m_links.length(0);
    Base::final();
  }

  unsigned reconnFreq() const { return 0; }

private:
  void reuseAdd_(Link &link) {
    if constexpr (Message::OneMessagePerLink) return;
    unsigned id = link.id();
    ZmAssert(id < m_reusable.length() && !link.reuseListed());
    link.reusePrev(nullptr);
    link.reuseNext(m_reusable[id]);
    if (auto next = link.reuseNext()) next->reusePrev(&link);
    m_reusable[id] = &link;
    link.reuseListed(true);
  }

  void reuseDel_(Link &link) {
    if constexpr (Message::OneMessagePerLink) return;
    if (!link.reuseListed()) return;
    unsigned id = link.id();
    ZmAssert(id < m_reusable.length());
    if (auto prev = link.reusePrev())
      prev->reuseNext(link.reuseNext());
    else {
      ZmAssert(m_reusable[id] == &link);
      m_reusable[id] = link.reuseNext();
    }
    if (auto next = link.reuseNext())
      next->reusePrev(link.reusePrev());
    link.reusePrev(nullptr);
    link.reuseNext(nullptr);
    link.reuseListed(false);
  }

  void stopBatch_() {
    unsigned end = m_stopHead + ClientWorkBatch;
    if (end > m_links.length()) end = m_links.length();
    while (m_stopHead < end) {
      auto &link = m_links[m_stopHead++];
      if (link) reuseDel_(*link);
      if (link && !link->stopped()) link->close();
    }
    if (m_stopHead < m_links.length())
      this->rxRun([this]() { stopBatch_(); });
  }

  // Stable after pool initialization.
  Owner		*m_owner = nullptr;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Links		m_links;
  Reusable	m_reusable;
  unsigned	m_live = 0;
  unsigned	m_stopHead = 0;
  bool		m_stopping = false;
};


// Plain H1 keeps one native TCP connection per stable link slot and assigns
// multiple ordered operations to that connection.  Request bytes are emitted
// in assignment order on Tx; responses are parsed from the Rx FIFO.
template <
  typename Owner_, typename LiveReq_,
  typename Request_, typename ResParser_>
class ClientPool<Owner_, H1TCP, LiveReq_, Request_, ResParser_> :
  public ClientHub<
    ClientPool<Owner_, H1TCP, LiveReq_, Request_, ResParser_>, H1TCP> {
public:
  using Owner = Owner_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using ResParser = ResParser_;
  using Pool = ClientPool;
  using Base = ClientHub<Pool, H1TCP>;

  template <typename Heap = ZuVoid> class Link_;
  using LinkHeap = ZmHeap<"Zhttp.H1.Link", Link_<>>;
  using Link = Link_<LinkHeap>;

  template <typename Heap> class Operation_;
  template <typename Heap>
  struct OperationLinkData {
	Operation_<Heap>	*prev = nullptr;
	ZmRef<Operation_<Heap>> next;
    bool	linked = false;
    bool	counted = false;
  };

  template <typename Heap>
  class Operation_ :
    public Heap, public ZmObject, public OperationLinkData<Heap> {
  public:
    using Protocol = TCP;
    using Session = ClientSession<
      Owner, LiveReq, Operation_, H1TCP, Request, ResParser>;

    Operation_(Link *link_, LiveReq *request_) :
      m_link{link_}, m_session{link_->pool()->owner(), this},
      m_request{request_}, m_generation{1} {
#ifdef ZmObject_DEBUG
      this->ZmObject::debug();
#endif
    }

    LiveReq *request() const { return m_request; }
    bool completed() const { return m_complete; }
    bool sent() const { return m_sent; }
    Session &session() { return m_session; }
    Owner *owner() const { return m_link->pool()->owner(); }
    Pool *pool() const { return m_link->pool(); }
    unsigned stableID() const { return m_link->stableID(); }

    void assign(LiveReq *request) {
      ++m_generation;
      m_request = request;
      m_complete = false;
      m_sent = false;
      m_closing = false;
      if (request) {
	request->poolTransport = Transport::TCP;
	request->poolSlot = stableID();
      }
      if (this->counted) m_link->reassigned(*this);
    }
    void connected(const ConnectedInfo &info) {
      if (!m_request) return;
      owner()->poolConnected(*this, *m_request, info);
      sendRequest();
    }
    void sendRequest() {
      if (!m_request || m_sent) return;
      m_session.bind(m_request);
      m_session.reset();
      m_sent = true;
      owner()->poolSend(*this, *m_request, Version::H1);
      auto op = ZmRef(this);
      unsigned generation = m_generation;
      pool()->txRun([op = ZuMv(op), generation]() mutable {
	op->m_session.beginTx();
	op->sendRequestTx_(generation);
      });
    }
    void close() {
      if (m_closing) return;
      m_closing = true;
      m_link->close();
    }
    void disconnect() { close(); }
    void cancelTx() {
      auto op = ZmRef(this);
      pool()->txRun([op = ZuMv(op)]() mutable {
	op->m_session.cancelTx();
      });
    }
    void retire(bool reuse = false) {
      bool replaced = m_request && m_link->pool()->operation(m_request) != this;
      pool()->detach(*this, m_request);
      m_request = nullptr;
      m_link->retire(*this, reuse || replaced);
    }
    void complete(bool ok) {
      if (!m_request || m_complete) return;
      m_complete = true;
      auto op = ZmRef(this);
      unsigned generation = m_generation;
      bool sent = m_sent;
      pool()->txRun([
	op = ZuMv(op), generation, ok, sent]() mutable {
	if (sent) op->m_session.cancelTx();
	BodyCommit commit = sent ? op->m_session.commit() : BodyCommit{};
	op->pool()->rxRun([
	  op = ZuMv(op), commit, generation, ok]() mutable {
	  if (op->m_generation != generation || !op->m_request) return;
	  op->owner()->poolTxCommitted(*op, *op->m_request, commit);
	  bool reuse = ok && op->owner()->poolReusable(*op->m_request);
	  op->owner()->poolComplete(*op, *op->m_request, ok, reuse);
	});
      });
    }

    template <typename Parser, typename Rx>
    auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
    template <typename Builder>
    auto transmit(Builder &builder) { return m_link->transmit(builder); }
    template <typename Builder>
    auto transmit_(Builder &builder) { return m_link->transmit_(builder); }
    void finish() { }
    template <typename State> void responseHeadersParsed(State *) { }
    template <typename State> void responseBodyBytes(State *) { }

  private:
    void sendRequestTx_(unsigned generation) {
      bool ok = m_session.send();
      auto op = ZmRef(this);
      BodyCommit commit = m_session.commit();
      pool()->rxRun([
	op = ZuMv(op), commit, generation, ok]() mutable {
	if (op->m_generation != generation || !op->m_request) return;
	if (ok)
	  op->owner()->poolTxCommitted(*op, *op->m_request, commit);
	else {
	  op->owner()->poolTxFailed(*op, *op->m_request, commit);
	  op->m_session.fail();
	}
      });
    }

    // Stable for the operation lifetime.
    Link	*m_link = nullptr;
    Session	m_session;

    // Rx thread exclusive.
    alignas(Zm::CacheLineSize)
    LiveReq	*m_request = nullptr;
    unsigned	m_generation = 0;
    bool	m_complete = false;
    bool	m_sent = false;
    bool	m_closing = false;
  };

  using Operation = Operation_<
    ZmHeap<"Zhttp.H1.Operation", Operation_<ZuVoid>>>;
  using OperationRef = ZmRef<Operation>;
  using OperationSlots =
    ZtArray<Operation *, ZtArrayHeapID<"Zhttp.H1.OperationSlots">>;
  template <typename Heap>
  class Link_ :
    public Heap,
    public ClientLink<Pool, Link_<Heap>, H1TCP> {
  public:
    using LinkBase = ClientLink<Pool, Link_, H1TCP>;
    using Protocol = TCP;

    Link_(Pool *pool_, unsigned id_) : LinkBase{pool_}, m_id{id_} { }

    Pool *pool() const { return this->app(); }
    bool stopped() const { return m_stopped; }
    bool drained() const {
      return !m_head && !m_operationCount && m_stopped;
    }
    unsigned stableID() const { return m_id; }

    void add(LiveReq *request) {
      OperationRef op = new Operation{this, request};
      request->poolTransport = Transport::TCP;
      request->poolSlot = m_id;
      pool()->operation(request, op.ptr());
      append_(op);
      op->counted = true;
      ++m_operationCount;
      if (m_connected && !m_closing) {
	op->connected(m_info);
	return;
      }
      if (m_connecting || m_closing) return;
      m_connecting = true;
      bool wasStopped = m_stopped;
      m_stopped = false;
      unsigned generation = ++m_generation;
      this->txErrorFn(ZiTxErrorFn{
	[this, generation](ZeException &e) {
	  return pool()->owner()->poolH1TxError(m_id, generation, e);
	}});
      if (wasStopped) pool()->liveInc_();
      pool()->owner()->poolConnect(*this, *request);
    }
    void onConnected(const ConnectedInfo &info) {
      if (!pool()->accepting()) {
	close();
	return;
      }
      m_connecting = false;
      m_connected = true;
      m_info = info;
      pool()->owner()->nativeUp(
	Transport::TCP, m_id, m_generation, ZmRef(this));
      connectedBatch_(m_head, m_generation);
    }
    void onDisconnected(bool peer) {
      m_connecting = false;
      m_connected = false;
      m_closing = false;
      pool()->owner()->nativeDown(Transport::TCP, m_id, m_generation);
      auto operations = ZuMv(m_head);
      m_tail = nullptr;
      drainFailed_(ZuMv(operations), peer, true, false);
    }
    void onConnectFailed(bool transient) {
      m_connecting = false;
      m_connected = false;
      m_closing = false;
      pool()->owner()->nativeDown(Transport::TCP, m_id, m_generation);
      auto operations = ZuMv(m_head);
      m_tail = nullptr;
      drainFailed_(ZuMv(operations), transient, false, true);
    }
    void txFailed(unsigned generation) {
      if (generation != m_generation || (!m_connecting && !m_connected))
	return;
      close();
    }
    template <typename Rx>
    int process(Rx &rx) {
      auto op = m_head;
      if (!op || !op->request()) return -1;
      int rc = op->session().process(rx);
      if (op->completed() && op->linked) unlink_(*op);
      return rc;
    }
    void close() {
      if (m_closing) return;
      m_closing = true;
      closeBatch_(m_head, m_generation);
    }
    void retire(Operation &operation, bool reuse) {
      if (operation.linked) unlink_(operation);
      if (operation.counted) {
	operation.counted = false;
	ZmAssert(m_operationCount);
	--m_operationCount;
      }
      if (!m_operationCount && !m_connecting && !m_connected)
	notifyStopped_();
      else if (!reuse && (m_connecting || m_connected))
	close();
    }
    void reassigned(Operation &operation) {
      ZmAssert(operation.counted);
      if (operation.linked && m_tail == &operation) return;
      OperationRef op = &operation;
      if (operation.linked) unlink_(operation);
      append_(ZuMv(op));
    }

  private:
    void connectedBatch_(OperationRef operations, unsigned generation) {
      unsigned n = 0;
      while (operations && n++ < ClientWorkBatch) {
	auto op = ZuMv(operations);
	operations = op->next;
	if (generation != m_generation || !m_connected || m_closing) return;
	if (op->request() && !op->sent()) op->connected(m_info);
      }
      if (operations)
	pool()->rxRun([
	  link = ZmRef(this), operations = ZuMv(operations), generation
	]() mutable {
	  link->connectedBatch_(ZuMv(operations), generation);
	});
    }

    void closeBatch_(OperationRef operations, unsigned generation) {
      unsigned n = 0;
      while (operations && n++ < ClientWorkBatch) {
	auto op = ZuMv(operations);
	operations = op->next;
	op->cancelTx();
      }
      if (operations) {
	pool()->rxRun([
	  link = ZmRef(this), operations = ZuMv(operations), generation
	]() mutable {
	  if (link->m_closing && generation == link->m_generation)
	    link->closeBatch_(ZuMv(operations), generation);
	});
	return;
      }
      auto link = ZmRef(this);
      pool()->txRun([link = ZuMv(link), generation]() mutable {
	auto pool = link->pool();
	pool->rxRun([link = ZuMv(link), generation]() mutable {
	  if (link->m_closing && generation == link->m_generation)
	    link->LinkBase::disconnect();
	});
      });
    }

    void drainFailed_(
        OperationRef operations, bool value, bool first, bool connectFailed) {
      unsigned n = 0;
      while (operations && n++ < ClientWorkBatch) {
	auto op = ZuMv(operations);
	operations = ZuMv(op->next);
	op->prev = nullptr;
	op->linked = false;
	if (!op->request()) continue;
	if (connectFailed)
	  pool()->owner()->poolConnectFailed(*op, op->request(), value);
	else {
	  pool()->owner()->poolDisconnected(*op, op->request(), value);
	  if (first) {
	    pool()->owner()->poolCloseDelimited(*op->request());
	    op->session().eof();
	  }
	}
	first = false;
	if (op->request()) op->session().fail();
      }
      if (operations) {
	auto link = ZmRef(this);
	pool()->rxRun([
	  link = ZuMv(link), operations = ZuMv(operations),
	  value, first, connectFailed
	]() mutable {
	  link->drainFailed_(
	    ZuMv(operations), value, first, connectFailed);
	});
	return;
      }
      if (!m_operationCount) notifyStopped_();
    }

    void append_(OperationRef op) {
      ZmAssert(op && !op->prev && !op->next);
      op->prev = m_tail;
      if (m_tail)
	m_tail->next = op;
      else
	m_head = op;
      m_tail = op.ptr();
      op->linked = true;
    }
    void unlink_(Operation &operation) {
      OperationRef hold = &operation;
      auto next = ZuMv(operation.next);
      auto prev = operation.prev;
      if (prev)
	prev->next = next;
      else
	m_head = next;
      if (next)
	next->prev = prev;
      else
	m_tail = prev;
      operation.prev = nullptr;
      operation.next = nullptr;
      operation.linked = false;
    }

    void notifyStopped_() {
      if (m_stopped) return;
      m_stopped = true;
      pool()->linkStopped_();
      pool()->owner()->poolStopped(*this);
    }

    // Stable after pool initialization.
    unsigned	m_id = 0;

    // Rx thread exclusive.
    alignas(Zm::CacheLineSize)
    OperationRef m_head;
    Operation	*m_tail = nullptr;
    unsigned	m_operationCount = 0;
    unsigned	m_generation = 0;
    ConnectedInfo m_info;
    bool	m_connecting = false;
    bool	m_connected = false;
    bool	m_closing = false;
    bool	m_stopped = true;
  };

  using Links =
    ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.H1.Links">>;

  ClientPool(Owner *owner = nullptr) : m_owner{owner} { }

  Owner *owner() const { return m_owner; }
  void owner(Owner *owner_) { m_owner = owner_; }
  bool accepting() const { return !m_stopping && this->running(); }
  unsigned live() const { return m_live; }
  unsigned linkCount() const { return m_links.length(); }
  const Links &links() const { return m_links; }

  void slots(unsigned n, unsigned requests) {
    m_links.size(n);
    for (unsigned i = 0; i < n; ++i) {
      ZmRef<Link> link = new Link{this, i};
      m_links.push(ZuMv(link));
    }
    m_operations.size(requests);
    while (m_operations.length() < requests) m_operations.push(nullptr);
  }
  ZmRef<Operation> open(LiveReq *request, unsigned id) {
    if (!accepting() || !request || id >= m_links.length()) return {};
    m_links[id]->add(request);
    return operation(request);
  }
  bool cancel(LiveReq *request) {
    if (!request || request->poolTransport != Transport::TCP ||
	request->poolSlot >= m_links.length())
      return false;
    auto op = operation(request);
    if (!op) return false;
    if (op->request() != request) return false;
    op->close();
    return true;
  }
  void detach(Operation &operation, LiveReq *request) {
    if (!request || request->poolTransport != Transport::TCP ||
	this->operation(request) != &operation) return;
    this->operation(request, nullptr);
    request->poolSlot = NullSlot;
  }
  void connected(Link &link, const ConnectedInfo &info) {
    link.onConnected(info);
  }
  void disconnected(Link &link, bool peer) { link.onDisconnected(peer); }
  void connectFailed(Link &link, bool transient) {
    link.onConnectFailed(transient);
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) { return link.process(rx); }
  void txFailed(unsigned id, unsigned generation) {
    if (id < m_links.length()) m_links[id]->txFailed(generation);
  }

  void stop_() {
    m_stopping = true;
    if (!m_live) {
      Base::stop_();
      return;
    }
    m_stopHead = 0;
    stopBatch_();
  }
  void final() {
#ifdef ZDEBUG
    for (auto operation: m_operations) ZmAssert(!operation);
    for (auto &link: m_links) ZmAssert(link->drained());
#endif
    m_links.length(0);
    m_operations.length(0);
    Base::final();
  }
  unsigned reconnFreq() const { return 0; }

private:
  void liveInc_() { ++m_live; }
  void linkStopped_() {
    if (m_live) --m_live;
    if (m_stopping && !m_live) Base::stop_();
  }

  void stopBatch_() {
    unsigned end = m_stopHead + ClientWorkBatch;
    if (end > m_links.length()) end = m_links.length();
    while (m_stopHead < end) {
      auto &link = m_links[m_stopHead++];
      if (link && !link->stopped()) link->close();
    }
    if (m_stopHead < m_links.length())
      this->rxRun([this]() { stopBatch_(); });
  }

  Operation *operation(const LiveReq *request) const {
    return request && request->slot < m_operations.length() ?
      m_operations[request->slot] : nullptr;
  }
  void operation(const LiveReq *request, Operation *operation_) {
    ZmAssert(request && request->slot < m_operations.length());
    m_operations[request->slot] = operation_;
  }

  // Stable after pool initialization.
  Owner		*m_owner = nullptr;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  Links		m_links;
  OperationSlots m_operations;
  unsigned	m_live = 0;
  unsigned	m_stopHead = 0;
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
  using Session = ClientSession<
    Owner, LiveReq, Impl, Profile,
    Request_, ResParser_>;

  TLSClientPoolLink_(
      Pool *pool, Impl *impl, unsigned slot_, unsigned id_) :
    m_pool{pool}, m_impl{impl}, m_id{id_},
    m_session{pool->owner(), impl}, m_slot{slot_} { }

  LiveReq *request() const { return m_request; }
  bool stopped() const { return m_stopped; }
  unsigned slot() const { return m_slot; }
  void slot(unsigned slot_) { m_slot = slot_; }
  unsigned stableID() const { return m_id; }

  void assign(LiveReq *request) {
    ++m_generation;
    m_request = request;
    m_complete = -1;
    m_sent = false;
    m_stopped = false;
    m_closing = false;
  }
  void sendRequest() {
    if (!m_request) return;
    m_session.bind(m_request);
    m_session.reset();
    m_sent = true;
    owner()->poolSend(*m_impl, *m_request, Message::ID);
    auto link = ZmRef(m_impl);
    unsigned generation = m_generation;
    m_pool->txRun([this, link = ZuMv(link), generation]() mutable {
      m_session.beginTx();
      sendRequestTx_(generation);
    });
  }
  void close() {
    if (m_closing) return;
    m_closing = true;
    auto link = ZmRef(m_impl);
    m_pool->txRun([this, link = ZuMv(link)]() mutable {
      m_session.cancelTx();
      m_pool->rxRun([this, link = ZuMv(link)]() mutable {
	m_impl->disconnect();
      });
    });
  }
  void retire(bool reuse = false) {
    m_pool->detach(*m_impl, m_request);
    m_request = nullptr;
    if (!m_impl->active()) {
      notifyStopped_();
      return;
    }
    if constexpr (Message::OneMessagePerLink) {
      if (m_complete != 1) close();
    } else {
      if (!reuse) {
	close();
	return;
      }
      auto link = ZmRef(m_impl);
      m_impl->release([this, link = ZuMv(link)]() mutable {
	notifyStopped_();
      });
    }
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
	m_session.eof();
      }
      if (m_request && m_complete < 0) m_session.fail();
    }
    notifyStopped_();
  }
  void onConnectFailed(bool transient) {
    owner()->poolConnectFailed(*m_impl, m_request, transient);
    if (m_request && m_complete < 0) m_session.fail();
    notifyStopped_();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return m_request ? m_session.process(rx) : -1;
  }

  void complete(bool ok) {
    if (!m_request || m_complete >= 0) return;
    m_complete = int8_t(ok);
    auto link = ZmRef(m_impl);
    unsigned generation = m_generation;
    bool sent = m_sent;
    m_pool->txRun([
      this, link = ZuMv(link), generation, ok, sent]() mutable {
      if (sent) m_session.cancelTx();
      BodyCommit commit = sent ? m_session.commit() : BodyCommit{};
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
    bool ok = m_session.send();
    auto link = ZmRef(m_impl);
    BodyCommit commit = m_session.commit();
    m_pool->rxRun([
      this, link = ZuMv(link), commit, generation, ok]() mutable {
      if (m_generation != generation || !m_request) return;
      if (ok)
	owner()->poolTxCommitted(*m_impl, *m_request, commit);
      else {
	owner()->poolTxFailed(*m_impl, *m_request, commit);
	m_session.fail();
      }
    });
  }

  void notifyStopped_() {
    if (m_stopped) return;
    m_stopped = true;
    m_pool->linkStopped(*m_impl);
  }

  // Stable for the logical TLS link lifetime.  Session internally partitions
  // its Rx and Tx state with cache-line boundaries.
  Pool		*m_pool = nullptr;
  Impl		*m_impl = nullptr;
  unsigned	m_id = 0;
  Session	m_session;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  LiveReq	*m_request = nullptr;
  unsigned	m_generation = 0;
  unsigned	m_slot = 0;
  int8_t	m_complete = -1;
  bool		m_sent = false;
  bool		m_stopped = false;
  bool		m_closing = false;
};

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Profile,
  typename Heap = ZuVoid>
class TLSClientPoolLink;
template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Profile>
using TLSClientPoolLinkHeap = ZmHeap<"Zhttp.TLS.Logical",
  TLSClientPoolLink<
    Pool, Owner, LiveReq, Request, ResParser, Profile>>;
template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Profile>
using TLSClientPoolLinkT = TLSClientPoolLink<
  Pool, Owner, LiveReq, Request, ResParser, Profile,
  TLSClientPoolLinkHeap<
    Pool, Owner, LiveReq, Request, ResParser, Profile>>;

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Heap>
class TLSClientPoolLink<
  Pool, Owner, LiveReq, Request, ResParser, H1TLS, Heap> :
  public Heap,
  public TLS_::ClientH1Logical<
    Pool, TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H1TLS, Heap>,
    TLS_::CliLinkT<
      Pool,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS, Heap>,
      TLSClientPoolLinkT<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H1TLS, Heap>,
    Owner, LiveReq, Request, ResParser, H1TLS> {
  using Impl = TLSClientPoolLink;
  using Native = TLS_::ClientH1Logical<
    Pool, Impl,
    TLS_::CliLinkT<
      Pool, Impl,
      TLSClientPoolLinkT<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS>>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, LiveReq, Request, ResParser, H1TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot, unsigned id) :
    Native{pool}, Link{pool, this, slot, id} { }

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
  using Link::stableID;
  using Link::stopped;

  void complete(bool ok) { Link::complete(ok); }
  template <typename Done>
  void release(Done &&done) {
    Native::complete(ZuFwd<Done>(done));
  }
};

template <
  typename Pool, typename Owner, typename LiveReq,
  typename Request, typename ResParser, typename Heap>
class TLSClientPoolLink<
  Pool, Owner, LiveReq, Request, ResParser, H2TLS, Heap> :
  public Heap,
  public H2_::ClientLogical<
    Pool, TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H2TLS, Heap>,
    TLS_::CliLinkT<
      Pool,
      TLSClientPoolLinkT<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
      TLSClientPoolLink<
	Pool, Owner, LiveReq, Request, ResParser, H2TLS, Heap>>>,
  public TLSClientPoolLink_<
    Pool,
    TLSClientPoolLink<
      Pool, Owner, LiveReq, Request, ResParser, H2TLS, Heap>,
    Owner, LiveReq, Request, ResParser, H2TLS> {
  using Impl = TLSClientPoolLink;
  using Native = H2_::ClientLogical<
    Pool, Impl,
    TLS_::CliLinkT<
      Pool,
      TLSClientPoolLinkT<
	Pool, Owner, LiveReq, Request, ResParser, H1TLS>,
      Impl>>;
  using Link = TLSClientPoolLink_<
    Pool, Impl, Owner, LiveReq, Request, ResParser, H2TLS>;

public:
  using Protocol = TLS;

  TLSClientPoolLink(Pool *pool, unsigned slot, unsigned id) :
    Native{pool}, Link{pool, this, slot, id} { }

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
  using Link::stableID;
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
    TLSClientPoolLinkT<
      TLSClientPool<
	Owner_, LiveReq_, Request_, ResParser_>,
      Owner_, LiveReq_, Request_, ResParser_, H1TLS>,
    TLSClientPoolLinkT<
      TLSClientPool<
	Owner_, LiveReq_, Request_, ResParser_>,
      Owner_, LiveReq_, Request_, ResParser_, H2TLS>> {
public:
  using Owner = Owner_;
  using LiveReq = LiveReq_;
  using Request = Request_;
  using ResParser = ResParser_;
  using Pool = TLSClientPool;
  using H1Link = TLSClientPoolLinkT<
    Pool, Owner, LiveReq, Request, ResParser, H1TLS>;
  using H2Link = TLSClientPoolLinkT<
    Pool, Owner, LiveReq, Request, ResParser, H2TLS>;
  using Base = TLS_::ClientHub<Pool, H1Link, H2Link>;
  using Base::stop;

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
  void tlsCapacity(
      unsigned id, uint64_t generation, bool saturated) {
    m_owner->tlsCapacity(id, generation, saturated);
  }
  template <typename Native>
  void nativeUp(
      unsigned id, uint64_t generation, const ZmRef<Native> &native) {
    m_owner->nativeUp(Transport::TLS, id, generation, native);
  }
  void nativeDown(unsigned id, uint64_t generation) {
    m_owner->nativeDown(Transport::TLS, id, generation);
  }
  bool nativeTxError(
      Transport::T transport, unsigned id,
      uint64_t generation, ZeException &e) {
    return m_owner->nativeTxError(transport, id, generation, e);
  }
  void txFailed(unsigned id, uint64_t generation) {
    Base::txFailed(id, generation);
  }

  void open(LiveReq *request, unsigned id) {
    if (!accepting() || !request) return;
    unsigned slot = m_pairs.length();
    Pair pair{
      .h1 = new H1Link{this, slot, id},
      .h2 = new H2Link{this, slot, id}
    };
    pair.h1->assign(request);
    pair.h2->assign(request);
    request->poolTransport = Transport::TLS;
    request->poolSlot = slot;
    auto h1 = pair.h1;
    auto h2 = pair.h2;
    m_pairs.push(ZuMv(pair));
    ++m_live;
    auto url = request->route.url.url();
    ZiIP remote;
    uint16_t port = url.port;
    if (request->route.endpointSet) {
      remote = request->route.endpoint.ip;
      port = request->route.endpoint.port;
    }
    Base::connect(h1, h2, url.host, port, id, ZuMv(remote));
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
	m_pairs[request->poolSlot].h1->request() != request) return;
    request->poolSlot = NullSlot;
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
    this->rxRun([this, hold = ZmRef(&link)]() mutable {
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
  // Stable after pool initialization.
  Owner		*m_owner = nullptr;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
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

template <typename Heap>
class ClientRoute_ : public Heap, public ZmObject {
public:
  ClientRoute_(uint64_t generation, Endpoints endpoints) :
    m_endpoints{ZuMv(endpoints)}, m_generation{generation} { }

  const Endpoints &endpoints() const { return m_endpoints; }
  uint64_t generation() const { return m_generation; }

private:
  Endpoints	m_endpoints;
  uint64_t	m_generation = 0;
};
using ClientRoute = ClientRoute_<
  ZmHeap<"Zhttp.Client.Route", ClientRoute_<ZuVoid>>>;
using ClientRouteRef = ZmRef<ClientRoute>;

struct ClientRouteState {
  enum { Unresolved, Resolving, TCP, TLS, H3, Failed };
};

struct ClientAttemptID {
  uint64_t	request = 0; // logical request; stable across wire attempts
  uint64_t	attempt = 0; // wire generation; changes on retry/redirect
};

struct ClientResponseBody {
  uint64_t	received = 0;
  uint64_t	consumed = 0;
  uint64_t	pending = 0;
  uint64_t	reset = 0;
  uint64_t	discarded = 0;
};

struct ClientAttemptRoute {
  URL			url;
  URL			redirect;
  Endpoint		endpoint;
  ClientRouteRef	endpoints;
  unsigned		endpointIndex = 0;
  RedirectState::T	redirectState = RedirectState::None;
  bool			endpointSet = false;
};

struct ClientAttemptProtocol {
  unsigned		status = 0;
  Transport::T		transport = Transport::TCP;
  Version::T		httpVersion = Version::H1;
  Method::T		method = Method::GET;
  Persistence::T	persistence = Persistence::Default;
  bool			http10 = false;
  bool			closeDelimited = false;
};

struct ClientAttemptFailureState {
  FailureKind::T	kind = FailureKind::None;
  bool			transient = false;
};

template <typename Heap>
struct ClientDiscoveryPost_ : public Heap, public ZmObject {
  ClientDiscoveryPost_(DiscoveryError error_, Endpoints endpoints_) :
    endpoints{ZuMv(endpoints_)}, error{error_} { }

  Endpoints		endpoints;
  DiscoveryError	error;
};
using ClientDiscoveryPost = ClientDiscoveryPost_<
  ZmHeap<"Zhttp.Client.Discovery", ClientDiscoveryPost_<ZuVoid>>>;
using ClientDiscoveryPostRef = ZmRef<ClientDiscoveryPost>;

template <
  typename Owner, typename Profile, typename LiveReq,
  typename Request, typename ResParser>
class ClientPool;

// TxQ is an unordered ZmPQTx specialized on the final application pool type.
// TxQ::Msg is the final ReqBuilder and publicly derives from ReqBuilder_;
// ReqBuilder_ and ResParser_ conform to the application contracts documented
// here and in Zhttp.hh.  Pool::send(ReqBuilder) is thread-safe ingress and
// intentionally hides TxQ::send(ReqBuilder); internal queue insertion is
// always Tx::send().

template <typename Client_, typename TxQ, typename ResParser_>
class Pool :
  public ZmObject,
  public TxQ {
public:
  using Client = Client_;
  using Tx = TxQ;
  using ReqBuilder = typename Tx::Msg;
  using ReqBuilder_ = typename ReqBuilder::T;
  using ResParser = ResParser_;
  using ReqHdrCatalog = typename ReqBuilder_::HdrCatalog;
  using RespHdrCatalog = typename ResParser::HdrCatalog;
  using Self = Pool;

  using RouteState = ClientRouteState;

  ZuAssert(!Tx::Ordered,
    "Zhttp::Pool requires unordered ZmPQTx acknowledgements");
  ZuAssert((ZuIs_<ReqBuilder, ReqBuilder_>{}),
    "Zhttp::Pool requires TxQ::Msg to derive from ReqBuilder_");
  ZuAssert(ZuIsObject<ReqBuilder_>{},
    "Zhttp::Pool requires ReqBuilder_ to be intrusively reference-counted");
  ZuAssert((ZuIs_<ReqBuilder_, Zhttp::ReqBuilder>{}),
    "Zhttp::Pool requires ReqBuilder_ to derive from Zhttp::ReqBuilder");

  using AttemptID = ClientAttemptID;
  using ResponseBody = ClientResponseBody;
  using AttemptRoute = ClientAttemptRoute;
  using AttemptProtocol = ClientAttemptProtocol;
  using AttemptFailureState = ClientAttemptFailureState;

private:
  template <typename Heap>
  class PoolLink_ : public Heap, public ZmObject {
  public:
    PoolLink_(Self *pool, unsigned slot) :
      m_pool{pool}, m_slot{slot} { }

    Self *pool() const { return m_pool; }
    unsigned slot() const { return m_slot; }
    unsigned reserved() const { return m_reserved; }
    uint64_t generation() const { return m_generation; }
    uint64_t nativeGeneration() const { return m_nativeGeneration; }
    Transport::T transport() const { return Transport::T(m_transport); }
    bool native() const { return bool(m_native); }
    bool drained() const { return !m_reserved && !m_native; }
    bool limited() const { return m_limited; }
    bool stopping() const { return m_stopping; }
    bool saturated(unsigned limit) const {
      return m_protocolSaturated || m_reserved >= limit;
    }

    void reserve() { ++m_reserved; ++m_assigned; }
    void emitted() { ++m_emitted; }
    void release() {
      ZmAssert(m_reserved);
      --m_reserved;
      ++m_completed;
    }
    bool limited(bool value) {
      bool changed = m_limited != value;
      m_limited = value;
      return changed;
    }
    bool capacity(
        uint64_t routeGeneration, uint64_t generation, bool saturated) {
      selectRoute(routeGeneration);
      if (generation < m_generation) return false;
      m_generation = generation;
      if (m_protocolSaturated == saturated) return false;
      m_protocolSaturated = saturated;
      return !saturated;
    }
    void selectRoute(uint64_t routeGeneration) {
      if (m_routeGeneration == routeGeneration) return;
      m_routeGeneration = routeGeneration;
      m_protocolSaturated = false;
      m_generation = 0;
    }
    void native(
        Transport::T transport, uint64_t generation, ZmContext native) {
      if (generation < m_nativeGeneration) return;
      m_transport = transport;
      m_nativeGeneration = generation;
      m_native = ZuMv(native);
    }
    bool nativeDown(Transport::T transport, uint64_t generation) {
      if (m_transport != transport || m_nativeGeneration != generation)
	return false;
      m_native = {};
      ++m_nativeGeneration;
      return true;
    }
    void stopping(bool value) { m_stopping = value; }

  private:
    // Stable after pool initialization.
    Self		*m_pool = nullptr;
    unsigned	m_slot = 0;

    // Rx thread exclusive.
    alignas(Zm::CacheLineSize)
    unsigned	m_reserved = 0;
    uint64_t	m_assigned = 0;
    uint64_t	m_emitted = 0;
    uint64_t	m_completed = 0;
    uint64_t	m_routeGeneration = 0;
    uint64_t	m_generation = 0;
    uint64_t	m_nativeGeneration = 0;
    ZmContext	m_native;
    int8_t	m_transport = -1;
    bool	m_protocolSaturated = false;
    bool	m_limited = false;
    bool	m_stopping = false;
  };
  using PoolLink = PoolLink_<
    ZmHeap<"Zhttp.Pool.Link", PoolLink_<ZuVoid>>>;
  using PoolLinkRef = ZmRef<PoolLink>;
  using PoolLinks =
    ZtArray<PoolLinkRef, ZtArrayHeapID<"Zhttp.Pool.Links">>;

public:

  struct LiveReq {
    // Valid while non-null; the Tx queue owns the request.
    ReqBuilder		*request = nullptr;
    PoolLink		*link = nullptr;
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
    unsigned		poolSlot = NullSlot;
    unsigned		linkSlot = NullSlot;
    unsigned		redirects = 0;
    unsigned		retries = 0;
    unsigned		routeGeneration = 0;
    unsigned		admissionStart = 0;
    unsigned		admissionScanned = 0;
    AttemptPhase::T	phase = AttemptPhase::Idle;
    Transport::T	poolTransport = -1;
    bool		routePending = false;
    bool		routeTransition = false;
  };

  using TCPPool = ClientPool<
    Self, H1TCP, LiveReq, ReqBuilder_, ResParser>;
  using TLSPool = TLSClientPool<
    Self, LiveReq, ReqBuilder_, ResParser>;
  using QUICPool = ClientPool<
    Self, H3QUIC, LiveReq, ReqBuilder_, ResParser>;
private:
  template <typename Heap>
  class RequestSlot_ : public Heap, public ZmObject, public LiveReq {
  public:
    RequestSlot_(Self *pool, unsigned slot) : m_pool{pool} {
      LiveReq::slot = slot;
    }

    ZmScheduler::Timer *timer() { return &m_timer; }
    uint64_t arm(uint64_t request) {
      m_request = request;
      m_armed = true;
      return ++m_generation;
    }
    void cancel() {
      m_armed = false;
      ++m_generation;
    }
    void fire(uint64_t generation) {
      if (!m_armed || generation != m_generation) return;
      m_armed = false;
      m_pool->timeout_(LiveReq::slot, m_request);
    }
    bool armed() const { return m_armed; }

  private:
    // Stable after pool initialization.
    Self		*m_pool = nullptr;

    // Rx thread exclusive.
    alignas(Zm::CacheLineSize)
    ZmScheduler::Timer	m_timer;
    uint64_t		m_request = 0;
    uint64_t		m_generation = 0;
    bool		m_armed = false;
  };
  using RequestSlot = RequestSlot_<
    ZmHeap<"Zhttp.Pool.Request", RequestSlot_<ZuVoid>>>;
  using RequestSlotRef = ZmRef<RequestSlot>;
  using LiveReqs =
    ZtArray<RequestSlotRef, ZtArrayHeapID<"Zhttp.Pool.Requests">>;

public:
  using Free =
    ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Client.Free">>;
  using Pending =
    ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Pool.Pending">>;
  using StopFns =
    ZtArray<Hubs::DoneFn, ZtArrayHeapID<"Zhttp.Pool.StopFns">>;
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

public:
  Pool(Client *client) :
    m_client{client}, m_tcp{this}, m_tls{this}, m_quic{this}, m_altSvc{1} { }

  uint64_t retainedBodyMax() const { return m_config.retainedBodyMax(); }
  uint64_t retainedMessageMax() const {
    return m_config.retainedMessageMax();
  }
  const HeaderSeeds &qpackSeeds() const { return m_qpackSeeds.entries(); }
  const HPackSeedPlans &hpackSeedPlans() const {
    return m_hpackSeeds.plans();
  }
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  bool init(
    unsigned slot, Destination dest,
    const HubConfig &hub, const Config &config,
    const TCPConfig &tcp, H2Config tls, const QUICConfig &quic)
  {
    if (!m_client || !dest.valid() || !hub.mx() || !config.valid() ||
	(config.secure() && config.tls() && !TLS_::valid(tls)) ||
	(config.secure() && config.quic() &&
	  (!quic.qpackValid() || !quic.maxQueuedFrames())))
      return false;
    m_slot = slot;
    m_dest = ZuMv(dest);
    tls.policy(config.h2Policy());
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = config;
    if (config.secure() && config.tls())
      m_hpackSeeds.template add<ReqHdrCatalog>(tls.hpackTxCapacity());
    if (config.secure() && config.quic()) {
      auto params = H3::Params().qpackLimits({
	quic.qpackRxCapacity(), quic.qpackTxCapacity(),
	quic.qpackRxBlocked(), quic.qpackTxSections()});
      m_qpackSeeds.template add<ReqHdrCatalog>(
	params, quic.qpackTxCapacity());
    }
    URLString origin;
    origin << (config.secure() ? "https://" : "http://") <<
      m_dest.authority << '/';
    if (!m_origin.assign(origin).ok()) return false;
    m_altSvc = AltSvcCache{config.maxOrigins()};
    m_liveReqs.size(config.concurrency());
    m_links.size(config.links());
    m_free.size(config.concurrency());
    for (unsigned i = 0, n = config.concurrency(); i < n; ++i) {
      RequestSlotRef request = new RequestSlot{this, i};
#ifdef ZmObject_DEBUG
      request->ZmObject::debug();
#endif
      m_liveReqs.push(ZuMv(request));
      m_free.push(config.concurrency() - i - 1);
    }
    for (unsigned i = 0, n = config.links(); i < n; ++i) {
      PoolLinkRef link = new PoolLink{this, i};
#ifdef ZmObject_DEBUG
      link->ZmObject::debug();
#endif
      m_links.push(ZuMv(link));
    }
    if (!config.secure() && config.tcp() &&
	!m_hubs.init(m_tcp, hub, tcp)) return false;
    if (config.secure() && config.tls() &&
	!m_hubs.init(m_tls, hub, tls)) return false;
    if (config.secure() && config.quic() &&
	!m_hubs.init(m_quic, hub, quic)) return false;
    if (!m_hubs.count()) return false;
    m_tcp.slots(config.links(), config.concurrency());
    return true;
  }

  bool start() {
    return ZmBlock<bool>{}([this](auto wake) { this->start(ZuMv(wake)); });
  }
  template <typename Done>
  void start(Done &&done) {
    m_hubs.start([this, done = Hubs::DoneFn{ZuFwd<Done>(done)}](bool ok) mutable {
      txRun_([this, done = ZuMv(done), ok]() mutable {
	if (ok) Tx::start();
	done(ok);
      });
    });
  }

  void send(ZmRef<ReqBuilder> request) {
    txRun_([this, request = ZuMv(request)]() mutable {
      (void)submit_(ZuMv(request));
    });
  }
  bool submit_(ZmRef<ReqBuilder> request) {
    assertTx_();
    if (m_txIngressStopped || m_sealed || !request) return false;
    Tx::send(ZuMv(request));
    return true;
  }
  void cancel(typename Tx::Key key) {
    txRun_([this, key]() { cancel_(key); });
  }
  void seal() {
    txRun_([this]() { seal_(); });
  }
  bool limited(unsigned link, bool value) {
    if (link >= m_config.links()) return false;
    rxRun_([this, link, value]() {
      assertRx_();
      m_links[link]->limited(value);
      if (!value) dispatchPending_();
    });
    return true;
  }
  void tlsCapacity(
      unsigned link, uint64_t generation, bool saturated) {
    capacity_(Transport::TLS, link, generation, saturated);
  }
  void quicCapacity(
      unsigned link, uint64_t generation, bool saturated) {
    capacity_(Transport::QUIC, link, generation, saturated);
  }
  template <typename Native>
  void nativeUp(
      Transport::T transport, unsigned link,
      uint64_t generation, const ZmRef<Native> &native) {
    assertRx_();
    if (link < m_links.length())
      m_links[link]->native(transport, generation, ZmContext{native});
  }
  void nativeDown(
      Transport::T transport, unsigned link, uint64_t generation) {
    assertRx_();
    if (link < m_links.length() &&
	m_links[link]->nativeDown(transport, generation) &&
	transport == Transport::QUIC && m_routeTransition)
      routeTransitionReady_();
  }
  void capacity_(
      Transport::T transport, unsigned link,
      uint64_t generation, bool saturated) {
    assertRx_();
    if (transport != m_capacityTransport || link >= m_links.length()) return;
    if (m_links[link]->capacity(
	  m_routeGeneration, generation, saturated))
      dispatchPending_();
  }
  void selectCapacity_(Transport::T transport) {
    if (m_capacityTransport == transport) return;
    bool transition = m_capacityTransport >= 0;
    m_capacityTransport = transport;
    ++m_routeGeneration;
    if (transition) rxRun_([this]() { dispatchPending_(); });
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
      stopIngress_(Hubs::DoneFn{
	[this, done = ZuMv(done)](bool) mutable {
	  // Cross one Rx queue boundary after timer cancellation so any callback
	  // already dequeued on this shard observes its invalidated generation
	  // before native and Tx teardown can release request slots.
	  rxRun_([this, done = ZuMv(done)]() mutable {
	    m_hubs.stop(Hubs::DoneFn{
	      [this, done = ZuMv(done)](bool ok) mutable {
		txRun_([this, done = ZuMv(done), ok]() mutable {
		  stopTx_(Hubs::DoneFn{
		    [this, done = ZuMv(done), ok](bool txOK) mutable {
		      rxRun_([done = ZuMv(done), ok, txOK]() mutable {
			done(ok && txOK);
		      });
		    }});
		});
	      }});
	  });
	}});
    });
  }

  void final() {
    // The owner has awaited stop(), or this pool was never started.
    ZmAssert(m_hubs.state() == ZmEngineState::Stopped);
    ZmAssert(!m_rxStopping || (m_rxStopped && m_txStopped));
    ZmAssert(!m_activeReqs->count_());
    m_hubs.final();
#ifdef ZDEBUG
    for (auto &request: m_liveReqs)
      ZmAssert(!request->request && !request->armed());
    for (auto &link: m_links) ZmAssert(link->drained());
#endif
    ZmAssert(!m_pending && !m_routePending && !m_transitionPending &&
	!m_dispatchPosted);
    ZmAssert(!m_stopIngressFns && !m_stopTxFns);
    ZmAssert(!m_routeDiscovery);
    m_liveReqs.length(0);
    m_links.length(0);
    m_pending.length(0);
    m_pendingHead = 0;
    m_routePending.length(0);
    m_transitionPending.length(0);
    m_routeEndpoints = nullptr;
    m_routeDiscovery = nullptr;
    m_routeState = RouteState::Unresolved;
    m_capacityTransport = -1;
    m_routeGeneration = 0;
    m_routeDiscoveryGeneration = 0;
    m_free.length(0);
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
  }

  unsigned completed() const { return m_completed; }
  unsigned failed() const { return m_failed; }
  unsigned active() const { return m_active; }
  unsigned slot() const { return m_slot; }
  Client *client() const { return m_client; }
  const Config &config() const { return m_config; }
  const Destination &destination() const { return m_dest; }
  ZuBSpan authority() const { return m_dest.authority; }

  bool send_(ReqBuilder *request, bool) {
    assertTx_();
    if (m_txIngressStopped || m_txActive >= m_config.concurrency())
      return false;
    ++m_txActive;
    rxRun_([this, request]() { admit_(request); });
    return true;
  }
  bool resend_(ReqBuilder *request, bool more) {
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
    URLView url = attempt.route.url.url();
    if constexpr (ZuIsSame<typename Link::Protocol, QUIC>{}) {
      if (attempt.route.endpointSet)
	link.connectEndpoint(attempt.route.endpoint);
      else
	link.connect(url.host, url.port);
    } else if constexpr (ZuIsSame<typename Link::Protocol, TCP>{}) {
      if (attempt.route.endpointSet && attempt.route.endpoint.ip) {
	URLString ip;
	ip << attempt.route.endpoint.ip;
	link.connect(ZuMv(ip), attempt.route.endpoint.port);
      } else
	link.connect(url.host, url.port);
    } else
      link.connect(url.host, url.port);
  }
  template <typename Link>
  bool poolTxError(Link &, ZeException &e) {
    if (!m_txErrorFn) return true;
    return m_txErrorFn(e);
  }
  bool poolH1TxError(
      unsigned link, unsigned generation, ZeException &e) {
    bool handled = !m_txErrorFn || m_txErrorFn(e);
    rxRun_([this, link, generation]() {
      assertRx_();
      m_tcp.txFailed(link, generation);
    });
    return handled;
  }
  bool nativeTxError(
      Transport::T transport, unsigned link,
      uint64_t generation, ZeException &e) {
    bool handled = !m_txErrorFn || m_txErrorFn(e);
    rxRun_([this, transport, link, generation]() {
      assertRx_();
      switch (transport) {
	case Transport::TLS: m_tls.txFailed(link, generation); break;
	case Transport::QUIC: m_quic.txFailed(link, generation); break;
	default: break;
      }
    });
    return handled;
  }
  template <typename Link>
  void poolSend(Link &, LiveReq &attempt, int) {
    if (attempt.link) attempt.link->emitted();
    sending_(attempt);
  }
  bool poolOperation(
    LiveReq &attempt, Method::T method, ZuBSpan target) {
    attempt.protocol.method = method;
    return attempt.route.url.resolve(m_origin.url(), target).ok() &&
      attempt.route.url.url().origin() == m_origin.url().origin();
  }
  template <typename Link>
  void poolConnected(
    Link &, LiveReq &attempt, const ConnectedInfo &info) {
    attempt.protocol.transport = info.transport;
    attempt.protocol.httpVersion = info.httpVersion;
    if (!(attempt.events & Zhttp::AttemptEvent{}.SelectionObserved())) {
      attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
      selected_(attempt);
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
      attemptFailed_(*attempt, transient);
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
    if (attempt.routeTransition) {
      if (!retrySafe_(attempt)) {
	finish_(link, attempt, ResultCode::ReplayUnsafe, false);
	return;
      }
      uint64_t previous = attempt.identity.attempt;
      Transport::T fromTransport = attempt.protocol.transport;
      Version::T fromVersion = attempt.protocol.httpVersion;
      link.retire();
      nextAttempt_(attempt, false);
      fallback_(attempt, previous, fromTransport, fromVersion);
      attempt.routeTransition = false;
      m_transitionPending.push(attempt.slot);
      routeTransitionReady_();
      return;
    }
    ok = ok && attempt.failure.kind == FailureKind::None;
    if (!ok && !(attempt.events & Zhttp::AttemptEvent{}.FailureObserved())) {
      attempt.events |= Zhttp::AttemptEvent{}.FailureObserved();
      attemptFailed_(attempt);
    }
    if (ok && redirectStatus_(attempt.protocol.status) &&
	attempt.route.redirectState != RedirectState::None) {
      if (attempt.redirects >= m_config.maxRedirects()) {
	finish_(link, attempt, ResultCode::RedirectLimit, reuse);
	return;
      }
      if (attempt.route.redirectState == RedirectState::Invalid) {
	finish_(link, attempt, ResultCode::InvalidRedirect, reuse);
	return;
      }
      URLView current = attempt.route.url.url();
      URLView next = attempt.route.redirect.url();
      bool same = current.origin() == next.origin();
      if (!same) {
	finish_(link, attempt, ResultCode::InvalidRedirect, reuse);
	return;
      }
      if (!attempt.request->idempotent(attempt.protocol.method)) {
	finish_(link, attempt, ResultCode::ReplayUnsafe, reuse);
	return;
      }
      ++attempt.redirects;
      attempt.route.url = ZuMv(attempt.route.redirect);
      uint64_t previous = attempt.identity.attempt;
      unsigned status = attempt.protocol.status;
      nextAttempt_(attempt, true);
      redirected_(attempt, attempt.route.url.url(), previous, status);
      if (reuse && same && direct_<Link>(attempt)) {
	attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
	selected_(attempt);
	link.assign(&attempt);
	link.sendRequest();
      } else {
	route_(attempt);
	link.retire();
      }
      return;
    }
    if (!ok && attempt.failure.kind == FailureKind::Tx) {
      finish_(link, attempt, ResultCode::Failed, false);
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
	    if (responseStarted_(attempt)) {
	      finish_(link, attempt, ResultCode::ReplayUnsafe, false);
	      return;
	    }
	    {
	      uint64_t previous = attempt.identity.attempt;
	      nextAttempt_(attempt, false);
	      retried_(attempt, previous);
	      startTLS_(attempt);
	      link.retire();
	    }
	    return;
	}
      }
    if (!ok && retry_(link, attempt)) return;
    if (!ok && attempt.protocol.transport == Transport::QUIC &&
	m_config.protocol() == ProtoPolicy::PreferH3) {
      if (!responseStarted_(attempt)) {
	if (!retrySafe_(attempt)) {
	  finish_(link, attempt, ResultCode::ReplayUnsafe, false);
	  return;
	}
	beginRouteTransition_(link, attempt);
	return;
      }
      finish_(link, attempt, ResultCode::Indeterminate, false);
      return;
    }
    finish_(link, attempt, ok ? ResultCode::OK : ResultCode::Failed, reuse);
  }

  template <typename Link>
  void poolStopped(Link &) { }

  template <typename Link>
  void status(
    Link &, LiveReq &attempt, ResParser &parser,
    unsigned value, bool http10) {
    attempt.protocol.status = value;
    attempt.protocol.http10 = http10;
    receivingHeaders_(attempt);
    parser.status(value);
  }
  template <typename Link>
  bool bodyInfo(
    Link &, LiveReq &, ResParser &parser,
    BodyType::T type, uint64_t length) {
    return parser.bodyInfo(type, length);
  }
  template <typename Key, typename Link>
  void header(
    Link &, LiveReq &attempt, ResParser &parser,
    Zhttp::FieldSection::T section, ZuSpan<uint8_t> value) {
    if constexpr (Key{}() == "alt-svc") {
      if (section != Zhttp::FieldSection::Final) {
	parser.template header<Key>(section, value);
	return;
      }
      URLView url = attempt.route.url.url();
      Origin origin{url.origin()};
      m_altSvc.update(
	origin, value, m_config.maxAltSvc(), Zm::now());
    } else if constexpr (Key{}() == "connection") {
      if (section != Zhttp::FieldSection::Final) {
	parser.template header<Key>(section, value);
	return;
      }
      if (ZuICmp<ZuBSpan>::equals(value, "close"))
	attempt.protocol.persistence = Persistence::Close;
      else if (ZuICmp<ZuBSpan>::equals(value, "keep-alive"))
	attempt.protocol.persistence = Persistence::KeepAlive;
    } else if constexpr (Key{}() == "location") {
      if (section != Zhttp::FieldSection::Final) {
	parser.template header<Key>(section, value);
	return;
      }
      attempt.route.redirectState =
	attempt.route.redirect.resolve(attempt.route.url.url(), value).ok() ?
	RedirectState::Valid : RedirectState::Invalid;
    }
    parser.template header<Key>(section, value);
  }
  template <typename Key, typename Value, typename Link>
  void header(
    Link &, LiveReq &, ResParser &parser,
    Zhttp::FieldSection::T section) {
    parser.template header<Key, Value>(section);
  }
  template <typename Link>
  void header(
    Link &, LiveReq &, ResParser &parser,
    Zhttp::FieldSection::T section,
    ZuBSpan key, ZuSpan<uint8_t> value) {
    if constexpr (Fields::HasRuntime<ResParser>{})
      parser.header(section, key, value);
  }
  template <typename Link, typename Rx>
  bool body(
    Link &link, LiveReq &attempt, ResParser &parser, Rx &rx) {
    headersDone_(link, attempt);
    uint64_t before = rx.length();
    if (before < attempt.responseBody.pending) {
      fail_(attempt, FailureKind::Body);
      return false;
    }
    attempt.responseBody.received += before - attempt.responseBody.pending;
    bool accepted = parser.body(rx);
    uint64_t pending = rx.length();
    if (pending > before) {
      fail_(attempt, FailureKind::Body);
      return false;
    }
    attempt.responseBody.consumed += before - pending;
    attempt.responseBody.pending = pending;
    link.responseBodyBytes(&attempt);
    if (ZuUnlikely(!accepted)) fail_(attempt, FailureKind::Body);
    return accepted;
  }
  template <typename ParserState, typename Link>
  void complete(
    Link &link, LiveReq &attempt, ResParser &parser,
    typename ParserState::T state) {
    headersDone_(link, attempt);
    closing_(attempt);
    attempt.responseBody.reset += attempt.responseBody.pending;
    attempt.responseBody.discarded += attempt.responseBody.pending;
    attempt.responseBody.pending = 0;
    bool ok = state == ParserState::Complete;
    if (!ok) fail_(attempt, FailureKind::Protocol);
    parser.complete(&link, ok);
    parser.reset();
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

  ZuInline void selected_(LiveReq &attempt) {
    if constexpr (HasReqSelected<ReqBuilder>{})
      attempt.request->selected(
	attempt.route.endpointSet ? &attempt.route.endpoint : nullptr,
	attempt.identity.request, attempt.identity.attempt,
	m_slot, attempt.linkSlot,
	attempt.protocol.transport, attempt.protocol.httpVersion);
  }

  ZuInline void attemptFailed_(LiveReq &attempt, bool transient = false) {
    if constexpr (HasReqAttemptFailed<ReqBuilder>{})
      attempt.request->attemptFailed(
	attempt.identity.request, attempt.identity.attempt,
	m_slot, attempt.linkSlot, attempt.protocol.status,
	attempt.protocol.transport, attempt.protocol.httpVersion,
	transient, responseStarted_(attempt));
  }

  ZuInline void redirected_(
      LiveReq &attempt, const URLView &url,
      uint64_t previousAttempt, unsigned status) {
    if constexpr (HasReqRedirected<ReqBuilder>{})
      attempt.request->redirected(
	url, attempt.identity.request, attempt.identity.attempt,
	previousAttempt, status, uint16_t(attempt.redirects));
  }

  ZuInline void retried_(LiveReq &attempt, uint64_t previousAttempt) {
    if constexpr (HasReqRetried<ReqBuilder>{})
      attempt.request->retried(
	attempt.identity.request, attempt.identity.attempt,
	previousAttempt, uint16_t(attempt.retries));
  }

  ZuInline void fallback_(
      LiveReq &attempt, uint64_t previousAttempt,
      Transport::T fromTransport, Version::T fromVersion) {
    if constexpr (HasReqFallback<ReqBuilder>{})
      attempt.request->fallback(
	attempt.identity.request, attempt.identity.attempt, previousAttempt,
	fromTransport, fromVersion);
  }

  static ZuInline void cancelled_(
      ReqBuilder &request, const Result &result) {
    if constexpr (HasReqCancelled<ReqBuilder>{}) request.cancelled(result);
  }

  static ZuInline void completed_(
      ReqBuilder &request, const Result &result) {
    if constexpr (HasReqCompleted<ReqBuilder>{}) request.completed(result);
  }

  static typename Tx::Key key_(const ReqBuilder *request) {
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
    auto &attempt = *m_liveReqs[active->data().slot];
    ZmAssert(attempt.request && key_(attempt.request) == key);
    if (attempt.discovery) {
      auto discovery = ZuMv(attempt.discovery);
      completeAttempt_(attempt, ResultCode::Cancelled);
      discovery->cancel();
      return;
    }
    if (attempt.routePending) {
      completeAttempt_(attempt, ResultCode::Cancelled);
      return;
    }
    if (attempt.linkSlot == NullSlot) {
      completeAttempt_(attempt, ResultCode::Cancelled);
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

  void admit_(ReqBuilder *request) {
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
    m_client->poolActive_(true);
    begin_(*m_liveReqs[slot], request);
  }

  void begin_(LiveReq &attempt, ReqBuilder *request) {
    prepare_(attempt, request);
    m_pending.push(attempt.slot);
    dispatchPending_();
  }

  int dispatch_(LiveReq &attempt, unsigned &work) {
    if (m_routeTransition) return 0;
    unsigned n = m_links.length();
    ZmAssert(n);
    if (!attempt.admissionScanned) attempt.admissionStart = m_nextLink;
    while (attempt.admissionScanned < n && work < ClientWorkBatch) {
      unsigned slot =
	(attempt.admissionStart + attempt.admissionScanned++) % n;
      ++work;
      auto link = m_links[slot].ptr();
      link->selectRoute(m_routeGeneration);
      if (link->stopping() || link->limited() ||
          link->saturated(m_config.linkMax())) continue;
      attempt.admissionScanned = 0;
      attempt.linkSlot = slot;
      attempt.link = link;
      link->reserve();
      m_nextLink = slot + 1;
      if (m_nextLink == n) m_nextLink = 0;
      route_(attempt);
      return 1;
    }
    if (attempt.admissionScanned < n) return -1;
    attempt.admissionScanned = 0;
    return 0;
  }

  void dispatchPending_() {
    unsigned work = 0, n = m_pending.length();
    while (m_pendingHead < n &&
	work < ClientWorkBatch) {
      unsigned slot = m_pending[m_pendingHead];
      auto &attempt = *m_liveReqs[slot];
      if (!attempt.request || attempt.linkSlot != NullSlot ||
	  attempt.phase != AttemptPhase::Idle) {
	++m_pendingHead;
	++work;
	continue;
      }
      int dispatched = dispatch_(attempt, work);
      if (dispatched < 0) {
	scheduleDispatch_();
	return;
      }
      if (!dispatched) break;
      ++m_pendingHead;
    }
    if (m_pendingHead == m_pending.length()) {
      m_pending.length(0);
      m_pendingHead = 0;
    } else if (work >= ClientWorkBatch)
      scheduleDispatch_();
  }

  void scheduleDispatch_() {
    if (m_dispatchPosted) return;
    m_dispatchPosted = true;
    rxRun_([this]() {
      m_dispatchPosted = false;
      dispatchPending_();
    });
  }

  void releaseLink_(LiveReq &attempt) {
    unsigned slot = attempt.linkSlot;
    if (slot == NullSlot) return;
    ZmAssert(slot < m_links.length() && attempt.link == m_links[slot].ptr());
    attempt.link->release();
    attempt.link = nullptr;
    attempt.linkSlot = NullSlot;
  }

  void prepare_(LiveReq &attempt, ReqBuilder *request) {
    attempt.request = request;
    attempt.route.url = m_origin;
    attempt.redirects = 0;
    attempt.retries = 0;
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints = nullptr;
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
      attempt.route.endpoints = nullptr;
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
    attempt.poolSlot = NullSlot;
    attempt.poolTransport = -1;
    attempt.admissionStart = 0;
    attempt.admissionScanned = 0;
    attempt.phase = AttemptPhase::Idle;
    attempt.routePending = false;
    attempt.routeTransition = false;
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
    attempt.linkSlot = NullSlot;
    attempt.identity = {};
    attempt.route.endpointIndex = 0;
    attempt.route.endpoints = nullptr;
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
    URLView url = attempt.route.url.url();
    if constexpr (ZuIsSame<Protocol, TCP>{})
      return url.scheme == Scheme::http;
    if constexpr (ZuIsSame<Protocol, TLS>{})
      if (url.scheme == Scheme::https) {
	if (m_config.protocol() == ProtoPolicy::DisableH3) return true;
	if (m_config.protocol() == ProtoPolicy::PreferH3 &&
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

  template <typename Link>
  void beginRouteTransition_(Link &link, LiveReq &attempt) {
    ZmAssert(!m_routeTransition &&
      attempt.protocol.transport == Transport::QUIC);
    m_routeTransition = true;
    m_routeState = RouteState::Unresolved;
    m_routeEndpoints = nullptr;

    uint64_t previous = attempt.identity.attempt;
    Transport::T fromTransport = attempt.protocol.transport;
    Version::T fromVersion = attempt.protocol.httpVersion;
    link.retire();
    nextAttempt_(attempt, false);
    fallback_(attempt, previous, fromTransport, fromVersion);
    m_transitionPending.push(attempt.slot);
    m_routeTransitionScan = 0;
    m_routeTransitionClose = 0;
    m_routeTransitionScanning = true;
    routeTransitionBatch_(&attempt);
  }

  void routeTransitionBatch_(LiveReq *origin) {
    if (!m_routeTransition) return;
    unsigned n = 0, l = m_liveReqs.length();
    while (m_routeTransitionScan < l &&
	++n <= ClientWorkBatch) {
      auto &other = *m_liveReqs[m_routeTransitionScan++];
      if (&other == origin || !other.request ||
	  other.protocol.transport != Transport::QUIC ||
	  other.phase == AttemptPhase::Closing)
	continue;
      if (!responseStarted_(other)) {
	other.routeTransition = true;
	if (!m_quic.cancel(&other)) {
	  uint64_t prior = other.identity.attempt;
	  Transport::T fromTransport = other.protocol.transport;
	  Version::T fromVersion = other.protocol.httpVersion;
	  nextAttempt_(other, false);
	  fallback_(other, prior, fromTransport, fromVersion);
	  m_transitionPending.push(other.slot);
	}
      } else {
	other.terminal = ResultCode::Indeterminate;
	if (!m_quic.cancel(&other))
	  completeAttempt_(other, other.terminal);
      }
    }
    if (m_routeTransitionScan < l) {
      rxRun_([this, origin]() { routeTransitionBatch_(origin); });
      return;
    }
    n = 0, l = m_links.length();
    while (m_routeTransitionClose < l &&
	++n <= ClientWorkBatch) {
      auto &poolLink = m_links[m_routeTransitionClose++];
      if (poolLink->native() &&
	  poolLink->transport() == Transport::QUIC)
	m_quic.txFailed(poolLink->slot(), poolLink->nativeGeneration());
    }
    if (m_routeTransitionClose < l) {
      rxRun_([this, origin]() { routeTransitionBatch_(origin); });
      return;
    }
    m_routeTransitionScanning = false;
    routeTransitionReady_();
  }

  void routeTransitionReady_() {
    if (!m_routeTransition || m_routeTransitionScanning ||
	m_routeTransitionReadyPosted) return;
    m_routeTransitionReadyPosted = true;
    routeTransitionReadyBatch_(0);
  }

  void routeTransitionReadyBatch_(unsigned i) {
    if (!m_routeTransition) {
      m_routeTransitionReadyPosted = false;
      return;
    }
    unsigned total = m_links.length() + m_liveReqs.length();
    unsigned end = i + ClientWorkBatch;
    if (end > total) end = total;
    while (i < end) {
      if (i < m_links.length()) {
	auto &link = m_links[i++];
	if (link->native() && link->transport() == Transport::QUIC) {
	  m_routeTransitionReadyPosted = false;
	  return;
	}
      } else {
	auto &attempt = m_liveReqs[i++ - m_links.length()];
	if (attempt->request && attempt->routeTransition) {
	  m_routeTransitionReadyPosted = false;
	  return;
	}
      }
    }
    if (i < total) {
      rxRun_([this, i]() { routeTransitionReadyBatch_(i); });
      return;
    }
    m_routeTransitionReadyPosted = false;
    m_routeTransition = false;
    m_transitionPendingHead = 0;
    if (!m_transitionPending) {
      m_routeState = RouteState::TLS;
      dispatchPending_();
      return;
    }
    m_routeState = RouteState::Unresolved;
    routeTransitionPublishBatch_();
  }

  void routeTransitionPublishBatch_() {
    unsigned end = m_transitionPendingHead + ClientWorkBatch;
    if (end > m_transitionPending.length()) end = m_transitionPending.length();
    while (m_transitionPendingHead < end) {
      unsigned slot = m_transitionPending[m_transitionPendingHead++];
      if (slot >= m_liveReqs.length()) continue;
      auto &attempt = *m_liveReqs[slot];
      if (!attempt.request || attempt.phase != AttemptPhase::Idle) continue;
      resolveRoute_(attempt, RouteState::TLS,
	m_config.h2Policy() == H2Policy::Disable ? Version::H1 : Version::H2);
    }
    if (m_transitionPendingHead < m_transitionPending.length()) {
      rxRun_([this]() { routeTransitionPublishBatch_(); });
      return;
    }
    m_transitionPending.length(0);
    m_transitionPendingHead = 0;
    dispatchPending_();
  }

  void route_(LiveReq &attempt) {
    if (attempt.route.url.url().scheme == Scheme::http) {
      resolveRoute_(attempt, RouteState::TCP, Version::H1);
      return;
    }
    switch (m_config.protocol()) {
      case ProtoPolicy::ForceH3:
	resolveRoute_(attempt, RouteState::H3, Version::H3);
	break;
      case ProtoPolicy::DisableH3:
	resolveRoute_(attempt, RouteState::TLS,
	  m_config.h2Policy() == H2Policy::Disable ? Version::H1 : Version::H2);
	break;
      default:
	prefer_(attempt);
	break;
    }
  }

  void resolveRoute_(
      LiveReq &attempt, int8_t state, Version::T version) {
    switch (m_routeState) {
      case RouteState::TCP:
	if (state == RouteState::TCP) {
	  startTCP_(attempt);
	  return;
	}
	break;
      case RouteState::TLS:
	if (state == RouteState::TLS) {
	  startTLS_(attempt);
	  return;
	}
	break;
      case RouteState::H3:
	if (state == RouteState::H3) {
	  attempt.route.endpoints = m_routeEndpoints;
	  attempt.route.endpointIndex = 0;
	  startQUIC_(attempt, attempt.route.endpoints ?
	    &attempt.route.endpoints->endpoints()[0] : nullptr);
	  return;
	}
	break;
      case RouteState::Resolving:
	waitRoute_(attempt);
	return;
      case RouteState::Failed:
	completeAttempt_(attempt, ResultCode::Failed);
	return;
      default:
	break;
    }
    m_routeState = RouteState::Resolving;
    waitRoute_(attempt);
    Endpoint endpoint{
      .origin = Origin{m_origin.url().origin()},
      .target = m_dest.host,
      .tlsName = m_dest.host,
      .port = m_dest.port,
      .source = EndpointSource::Origin,
      .httpVersion = version
    };
    resolveRouteStart_(state, ZuMv(endpoint));
  }

  void resolveRouteStart_(int8_t state, Endpoint endpoint) {
    ZmAssert(m_routeState == RouteState::Resolving);
    ZmAssert(!m_routeDiscovery);
    uint64_t generation = ++m_routeDiscoveryGeneration;
    auto discovery = resolveH3(
      ZuMv(endpoint), m_config.discoveryLimits(), DiscoveryFn{[
      this, state, generation](
          DiscoveryError error, Endpoints endpoints) mutable {
	ClientDiscoveryPostRef post =
	  new ClientDiscoveryPost{error, ZuMv(endpoints)};
#ifdef ZmObject_DEBUG
	post->ZmObject::debug();
#endif
	rxRun_([this, state, generation, post = ZuMv(post)]() mutable {
	  if (generation != m_routeDiscoveryGeneration ||
	      m_routeState != RouteState::Resolving) return;
	  m_routeDiscovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    m_routeEndpoints = new ClientRoute{
	      generation, ZuMv(post->endpoints)};
	    m_routeState = state;
	  } else
	    m_routeState = RouteState::Failed;
	  routeReady_();
	});
      }}, m_resolverOps);
    if (generation == m_routeDiscoveryGeneration &&
	m_routeState == RouteState::Resolving && !m_routeDiscovery)
      m_routeDiscovery = ZuMv(discovery);
    else if (discovery)
      discovery->cancel();
  }

  void prefer_(LiveReq &attempt) {
    URLView url = attempt.route.url.url();
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
      m_routeState = RouteState::Resolving;
      waitRoute_(attempt);
      resolveRouteStart_(RouteState::H3, ZuMv(endpoint));
      return;
    }
    discover_(attempt);
  }

  void discover_(LiveReq &attempt) {
    switch (m_routeState) {
      case RouteState::TLS:
	startTLS_(attempt);
	return;
      case RouteState::H3:
	attempt.route.endpoints = m_routeEndpoints;
	attempt.route.endpointIndex = 0;
	startQUIC_(attempt, attempt.route.endpoints ?
	  &attempt.route.endpoints->endpoints()[0] : nullptr);
	return;
      case RouteState::Resolving:
	waitRoute_(attempt);
	return;
      default:
	break;
    }
    URLView url = attempt.route.url.url();
    m_routeState = RouteState::Resolving;
    waitRoute_(attempt);
    ZmAssert(!m_routeDiscovery);
    uint64_t generation = ++m_routeDiscoveryGeneration;
    auto discovery = discoverH3(
      Origin{url.origin()}, url.host, url.port,
      m_config.blindH3(), m_config.discoveryLimits(), DiscoveryFn{[
	this, generation](DiscoveryError error, Endpoints endpoints) mutable {
	ClientDiscoveryPostRef post =
	  new ClientDiscoveryPost{error, ZuMv(endpoints)};
#ifdef ZmObject_DEBUG
	post->ZmObject::debug();
#endif
	rxRun_([this, generation, post = ZuMv(post)]() mutable {
	  if (generation != m_routeDiscoveryGeneration ||
	      m_routeState != RouteState::Resolving) return;
	  m_routeDiscovery = nullptr;
	  if (post->error.ok() && post->endpoints) {
	    m_routeEndpoints = new ClientRoute{
	      generation, ZuMv(post->endpoints)};
	    m_routeState = RouteState::H3;
	    routeReady_();
	    return;
	  }
	  URLView url = m_origin.url();
	  resolveRouteStart_(RouteState::TLS, Endpoint{
	    .origin = Origin{url.origin()},
	    .target = m_dest.host,
	    .tlsName = m_dest.host,
	    .port = m_dest.port,
	    .source = EndpointSource::Origin,
	    .httpVersion = Version::T(
	      m_config.h2Policy() == H2Policy::Disable ?
		Version::H1 : Version::H2)
	  });
	});
      }}, m_resolverOps);
    if (generation == m_routeDiscoveryGeneration &&
	m_routeState == RouteState::Resolving && !m_routeDiscovery)
      m_routeDiscovery = ZuMv(discovery);
    else if (discovery)
      discovery->cancel();
  }

  void waitRoute_(LiveReq &attempt) {
    if (attempt.phase != AttemptPhase::Resolving) resolving_(attempt);
    attempt.routePending = true;
    m_routePending.push(attempt.slot);
  }

  void routeReady_() {
    routeReadyBatch_(0);
  }

  void routeReadyBatch_(unsigned i) {
    unsigned end = i + ClientWorkBatch;
    if (end > m_routePending.length()) end = m_routePending.length();
    while (i < end) {
      unsigned slot = m_routePending[i++];
      if (slot >= m_liveReqs.length()) continue;
      auto &attempt = *m_liveReqs[slot];
      if (!attempt.request || !attempt.routePending) continue;
      attempt.routePending = false;
      switch (m_routeState) {
	case RouteState::TCP:
	  startTCP_(attempt);
	  break;
	case RouteState::H3:
	  attempt.route.endpoints = m_routeEndpoints;
	  attempt.route.endpointIndex = 0;
	  startQUIC_(attempt, attempt.route.endpoints ?
	    &attempt.route.endpoints->endpoints()[0] : nullptr);
	  break;
	case RouteState::TLS:
	  startTLS_(attempt);
	  break;
	default:
	  completeAttempt_(attempt, ResultCode::Failed);
	  break;
      }
    }
    if (i < m_routePending.length()) {
      rxRun_([this, i]() { routeReadyBatch_(i); });
      return;
    }
    m_routePending.length(0);
  }

  void startTCP_(LiveReq &attempt) {
    selectCapacity_(Transport::TCP);
    attempt.routeGeneration = m_routeGeneration;
    select_<H1TCP>(attempt);
    if (m_routeEndpoints) {
      attempt.route.endpoints = m_routeEndpoints;
      attempt.route.endpointIndex = 0;
      attempt.route.endpoint = attempt.route.endpoints->endpoints()[0];
      attempt.route.endpointSet = true;
    }
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    selected_(attempt);
    m_tcp.open(&attempt, attempt.linkSlot);
  }
  void startTLS_(LiveReq &attempt) {
    selectCapacity_(Transport::TLS);
    attempt.routeGeneration = m_routeGeneration;
    attempt.protocol.transport = Transport::TLS;
    switch (m_config.h2Policy()) {
      case H2Policy::Disable:
	attempt.protocol.httpVersion = Version::H1;
	break;
      default:
	attempt.protocol.httpVersion = Version::H2;
	break;
    }
    if (m_routeEndpoints) {
      attempt.route.endpoints = m_routeEndpoints;
      attempt.route.endpointIndex = 0;
      attempt.route.endpoint = attempt.route.endpoints->endpoints()[0];
      attempt.route.endpointSet = true;
    }
    connecting_(attempt);
    m_tls.open(&attempt, attempt.linkSlot);
  }
  void startQUIC_(LiveReq &attempt, const Endpoint *endpoint) {
    selectCapacity_(Transport::QUIC);
    attempt.routeGeneration = m_routeGeneration;
    select_<H3QUIC>(attempt);
    connecting_(attempt);
    attempt.events |= Zhttp::AttemptEvent{}.SelectionObserved();
    if (endpoint) {
      attempt.route.endpoint = *endpoint;
      attempt.route.endpointSet = true;
    }
    selected_(attempt);
    m_quic.open(&attempt, attempt.linkSlot);
  }

  void armTimer_(LiveReq &attempt) {
    if (!m_config.requestTimeout()) return;
    auto timer = m_liveReqs[attempt.slot].ptr();
    uint64_t generation = timer->arm(attempt.identity.request);
    m_mx->add(
      timer->timer(), Zm::now(m_config.requestTimeout()),
      ZmScheduler::Update,
      [timer, generation](auto &&arm) {
	return arm([timer, generation]() { timer->fire(generation); });
      }, m_rxThread);
  }

  void cancelTimer_(LiveReq &attempt) {
    auto timer = m_liveReqs[attempt.slot].ptr();
    if (!timer->armed()) return;
    timer->cancel();
    m_mx->del(timer->timer());
  }

  void timeout_(unsigned slot, uint64_t requestID) {
    if (m_rxStopping || slot >= m_liveReqs.length()) return;
    auto &attempt = *m_liveReqs[slot];
    if (!attempt.request || attempt.identity.request != requestID) return;
    if (attempt.discovery) {
      auto discovery = ZuMv(attempt.discovery);
      completeAttempt_(attempt, ResultCode::TimedOut);
      discovery->cancel();
      return;
    }
    if (attempt.routePending) {
      completeAttempt_(attempt, ResultCode::TimedOut);
      return;
    }
    if (attempt.linkSlot == NullSlot) {
      completeAttempt_(attempt, ResultCode::TimedOut);
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
	attempt.retries >= m_config.maxRetries())
      return false;

    uint64_t previous = attempt.identity.attempt;
    ++attempt.retries;
    if (attempt.protocol.transport == Transport::QUIC &&
	attempt.route.endpoints && attempt.route.endpointIndex + 1 <
	  attempt.route.endpoints->endpoints().length())
      ++attempt.route.endpointIndex;
    nextAttempt_(attempt, false);
    retried_(attempt, previous);
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
	    &attempt.route.endpoints->endpoints()[attempt.route.endpointIndex] :
	      nullptr);
	break;
    }
    link.retire();
    return true;
  }

  static bool retrySafe_(const LiveReq &attempt) {
    return (!attempt.requestBody.headers &&
	!attempt.requestBody.committed) ||
      attempt.request->idempotent(attempt.protocol.method);
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
    cancelTimer_(attempt);
    auto key = key_(attempt.request);
    emit_(*attempt.request, result_(attempt, code));
    closing_(attempt);
    link.retire(reuse);
    releaseAttempt_(attempt, key);
    terminal_(key);
  }

  void complete_(ReqBuilder &request, ResultCode::T code) {
    Result result{
      .request = ++m_requestID, .pool = m_slot, .code = code};
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
      .pool = m_slot,
      .link = attempt.linkSlot,
      .redirects = uint16_t(attempt.redirects),
      .retries = uint16_t(attempt.retries),
      .code = code,
      .transport = attempt.protocol.transport,
      .httpVersion = attempt.protocol.httpVersion
    };
  }

  void emit_(ReqBuilder &request, const Result &result) {
    if (result.code == ResultCode::Cancelled)
      cancelled_(request, result);
    completed_(request, result);
    ++m_completed;
    if (!result.ok()) ++m_failed;
    m_client->poolResult_(!result.ok());
  }

  void completeAttempt_(LiveReq &attempt, ResultCode::T code) {
    assertRx_();
    cancelTimer_(attempt);
    auto key = key_(attempt.request);
    emit_(*attempt.request, result_(attempt, code));
    if (attempt.phase != AttemptPhase::Idle) closing_(attempt);
    releaseAttempt_(attempt, key);
    terminal_(key);
  }

  void releaseAttempt_(LiveReq &attempt, typename Tx::Key key) {
    unsigned slot = attempt.slot;
    releaseLink_(attempt);
    idleAttempt_(attempt);
    auto active = m_activeReqs->del(key);
    ZmAssert(active && active->data().slot == slot);
    m_free.push(slot);
    --m_active;
    m_client->poolActive_(false);
    dispatchPending_();
  }

  void stopIngress_(Hubs::DoneFn done) {
    assertRx_();
    if (m_rxStopped) {
      done(true);
      return;
    }
    m_stopIngressFns.push(ZuMv(done));
    if (m_rxStopping) return;
    m_rxStopping = true;
    m_pending.length(0);
    m_pendingHead = 0;
    m_routePending.length(0);
    m_transitionPending.length(0);
    m_transitionPendingHead = 0;
    m_routeTransition = false;
    m_routeTransitionScanning = false;
    ++m_routeDiscoveryGeneration;
    m_routeState = RouteState::Unresolved;
    if (m_routeDiscovery) {
      auto discovery = ZuMv(m_routeDiscovery);
      discovery->cancel();
    }
    // Establish the Tx ingress barrier before cancelling admitted requests.
    // Their terminal acknowledgements must not restart the Tx queue and admit
    // replacement work while teardown is draining the Rx side.
    txRun_([this]() {
      assertTx_();
      m_txIngressStopped = true;
      Tx::stop();
      rxRun_([this]() { stopIngressBatch_(0); });
    });
  }

  void stopIngressBatch_(unsigned i) {
    assertRx_();
    unsigned end = i + ClientWorkBatch;
    if (end > m_liveReqs.length()) end = m_liveReqs.length();
    while (i < end) {
      auto &attempt = *m_liveReqs[i];
      ++i;
      if (!attempt.request) continue;
      cancelTimer_(attempt);
      if (attempt.discovery) {
	auto discovery = ZuMv(attempt.discovery);
	completeAttempt_(attempt, ResultCode::Cancelled);
	discovery->cancel();
      } else if (attempt.routePending)
	completeAttempt_(attempt, ResultCode::Cancelled);
      else if (attempt.linkSlot == NullSlot)
	completeAttempt_(attempt, ResultCode::Cancelled);
      else
	attempt.terminal = ResultCode::Cancelled;
    }
    if (i < m_liveReqs.length()) {
      rxRun_([this, i]() { stopIngressBatch_(i); });
      return;
    }
    m_rxStopped = true;
    auto fns = ZuMv(m_stopIngressFns);
    m_stopIngressFns.init_();
    for (auto &fn: fns) {
      fn(true);
      fn = {};
    }
  }

  void terminal_(typename Tx::Key key) {
    assertRx_();
    txRun_([this, key]() {
      assertTx_();
      if (m_txActive) --m_txActive;
      Tx::ackd(key);
      if (!m_txIngressStopped) Tx::start();
      idleTx_();
    });
  }

  typename Tx::Impl_ *app_() {
    return static_cast<typename Tx::Impl_ *>(this);
  }

  void stopTx_(Hubs::DoneFn done) {
    assertTx_();
    if (m_txStopped) {
      done(true);
      return;
    }
    m_stopTxFns.push(ZuMv(done));
    if (m_txStopping) return;
    m_txStopping = true;
    Tx::stop();
    ZmAssert(!m_txActive);
    stopTxBatch_();
  }

  void stopTxBatch_() {
    assertTx_();
    unsigned n = 0;
    {
      auto i = app_()->txQueue()->iter();
      while (n < ClientWorkBatch && i()) {
	auto request = i.del();
	++n;
	rxRun_([this, request = ZuMv(request)]() mutable {
	  complete_(*request, ResultCode::Cancelled);
	});
      }
    }
    if (app_()->txQueue()->count_()) {
      txRun_([this]() { stopTxBatch_(); });
      return;
    }
    m_txStopped = true;
    auto fns = ZuMv(m_stopTxFns);
    m_stopTxFns.init_();
    for (auto &fn: fns) {
      fn(true);
      fn = {};
    }
  }

  void idleTx_() {
    assertTx_();
    if (m_sealed && !m_idle && !m_txActive &&
	!app_()->txQueue()->count_()) {
      m_idle = true;
      m_client->poolIdle_(m_slot);
    }
  }

  // Stable/shared for the configured pool lifetime.  Counters are telemetry
  // snapshots and may be read uncleanly by application threads.
  ZiMultiplex	*m_mx = nullptr;
  Client		*m_client = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  unsigned	m_slot = 0;
  Config	m_config;
  Destination	m_dest;
  URL		m_origin;
  TCPPool	m_tcp;
  TLSPool	m_tls;
  QUICPool	m_quic;
  HeaderSeedCatalog m_qpackSeeds;
  HPackSeedCatalog m_hpackSeeds;
  Hubs		m_hubs;
  ZiTxErrorFn	m_txErrorFn;
  const DiscoveryResolver *m_resolverOps = nullptr;
  unsigned	m_active = 0;
  unsigned	m_completed = 0;
  unsigned	m_failed = 0;

  // Rx thread exclusive.
  alignas(Zm::CacheLineSize)
  AltSvcCache	m_altSvc;
  LiveReqs	m_liveReqs;
  ZmRef<ActiveHash> m_activeReqs = new ActiveHash;
  Free		m_free;
  PoolLinks	m_links;
  Pending	m_pending;
  Pending	m_routePending;
  Pending	m_transitionPending;
  DiscoveryRequestRef m_routeDiscovery;
  ClientRouteRef m_routeEndpoints;
  unsigned	m_pendingHead = 0;
  unsigned	m_transitionPendingHead = 0;
  unsigned	m_nextLink = 0;
  unsigned	m_routeTransitionScan = 0;
  unsigned	m_routeTransitionClose = 0;
  unsigned	m_routeGeneration = 0;
  uint64_t	m_routeDiscoveryGeneration = 0;
  uint64_t	m_attemptID = 0;
  uint64_t	m_requestID = 0;
  int8_t	m_routeState = RouteState::Unresolved;
  int8_t	m_capacityTransport = -1;
  StopFns	m_stopIngressFns;
  bool		m_rxStopping = false;
  bool		m_rxStopped = false;
  bool		m_routeTransition = false;
  bool		m_routeTransitionScanning = false;
  bool		m_routeTransitionReadyPosted = false;
  bool		m_dispatchPosted = false;

  // Tx thread exclusive.
  alignas(Zm::CacheLineSize)
  StopFns	m_stopTxFns;
  unsigned	m_txActive = 0;
  bool		m_sealed = false;
  bool		m_idle = false;
  bool		m_txIngressStopped = false;
  bool		m_txStopping = false;
  bool		m_txStopped = false;
};

// Client is the application-facing lifecycle and dispatch coordinator.  Pool_
// is the final application pool type; its TxQ must be bound to Pool_, not to
// this coordinator.
template <typename App_, typename Pool_>
class Client {
public:
  using App = App_;
  using Pool = Pool_;
  using ReqBuilder = typename Pool::ReqBuilder;
  using Key = typename Pool::Tx::Key;
  using DoneFn = Hubs::DoneFn;
  using Pools =
    ZtArray<ZmRef<Pool>, ZtArrayHeapID<"Zhttp.Client.Pools">>;
  using Idle =
    ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.Client.Idle">>;

public:
  bool init(
    const HubConfig &hub, unsigned poolCount,
    const Config &config = {}, const TCPConfig &tcp = {},
    const H2Config &h2 = {}, const QUICConfig &quic = {})
  {
    if (m_inited || !hub.mx() || !poolCount || !config.valid())
      return false;
    m_hub = hub;
    m_config = config;
    m_tcp = tcp;
    m_h2 = h2;
    m_quic = quic;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    m_idleCount = 0;
    m_completed = 0;
    m_failed = 0;
    m_active = 0;
    m_pools.size(poolCount);
    m_idle.size(poolCount);
    for (unsigned i = 0; i < poolCount; ++i) {
      m_pools.push(ZmRef<Pool>{});
      m_idle.push(0);
    }
    m_inited = true;
    return true;
  }

  bool pool(
    unsigned slot, Destination destination,
    const Config &config = {})
  {
    if (!m_inited || m_ctl.state() != ZmEngineState::Stopped || slot >= m_pools.length() ||
	m_pools[slot] || !destination.valid())
      return false;
    Config effective = m_config.overlay(config);
    H2Config h2{m_h2};
    QUICConfig quic{m_quic};
    h2.maxConcurrentStreams(effective.linkMax());
    h2.maxPending(effective.linkMax());
    quic.maxStreamsDuplex(effective.linkMax());
    ZmRef<Pool> pool = new Pool{app_()};
#ifdef ZmObject_DEBUG
    pool->ZmObject::debug();
#endif
    if (!pool->init(
	slot, ZuMv(destination), m_hub, effective, m_tcp, h2, quic) ||
	!m_ctl.add(*pool)) {
      pool->final();
      return false;
    }
    m_pools[slot] = ZuMv(pool);
    return true;
  }

  bool start() {
    return ZmBlock<bool>{}([this](auto wake) { this->start(ZuMv(wake)); });
  }
  template <typename Done>
  void start(Done &&done) {
    DoneFn done_{ZuFwd<Done>(done)};
    if (!m_inited) { done_(false); return; }
    for (unsigned i = 0, n = m_pools.length(); i < n; ++i)
      if (!m_pools[i]) { done_(false); return; }
    if (m_resolverOwned) ZiResolver::start();
    if (!ZiResolver::instance()->running()) { done_(false); return; }
    m_ctl.start(ZuMv(done_));
  }

  bool send(unsigned slot, ZmRef<ReqBuilder> request) {
    if (!m_ctl.running() || !request) return false;
    auto pool = pool_(slot);
    if (!pool) return false;
    pool->send(ZuMv(request));
    return true;
  }
  bool cancel(unsigned slot, Key key) {
    auto pool = pool_(slot);
    if (!pool) return false;
    pool->cancel(key);
    return true;
  }
  bool seal(unsigned slot) {
    if (!m_ctl.running()) return false;
    auto pool = pool_(slot);
    if (!pool) return false;
    pool->seal();
    return true;
  }
  template <typename L>
  bool txRun(unsigned slot, L &&l) {
    auto pool = pool_(slot);
    if (!pool) return false;
    pool->txRun(ZuFwd<L>(l));
    return true;
  }
  bool limited(unsigned pool, unsigned link, bool value) {
    auto pool_ = this->pool_(pool);
    return pool_ && pool_->limited(link, value);
  }

  void txErrorFn(ZiTxErrorFn fn) {
    for (auto &pool: m_pools) if (pool) pool->txErrorFn(fn);
  }
  void discoveryResolver(const DiscoveryResolver *resolver) {
    for (auto &pool: m_pools) if (pool) pool->discoveryResolver(resolver);
  }

  void stop() {
    (void)ZmBlock<bool>{}(
      [this](auto wake) { stop(DoneFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    m_ctl.stop(ZuFwd<Done>(done));
  }

  void final() {
    m_ctl.final();
    m_pools.init();
    m_idle.init();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_idleCount = 0;
    m_completed = 0;
    m_failed = 0;
    m_active = 0;
    m_inited = false;
  }

  unsigned poolCount() const { return m_pools.length(); }
  unsigned completed() const { return m_completed; }
  unsigned failed() const { return m_failed; }
  unsigned active() const { return m_active; }
  unsigned completed(unsigned slot) const {
    auto pool = pool_(slot); return pool ? pool->completed() : 0;
  }
  unsigned failed(unsigned slot) const {
    auto pool = pool_(slot); return pool ? pool->failed() : 0;
  }
  unsigned active(unsigned slot) const {
    auto pool = pool_(slot); return pool ? pool->active() : 0;
  }
  const Config *config(unsigned slot) const {
    auto pool = pool_(slot); return pool ? &pool->config() : nullptr;
  }
  void printQUICDiag() {
    for (auto &pool: m_pools) if (pool) pool->printQUICDiag();
  }

  void poolIdle_(unsigned slot) {
    if (slot >= m_idle.length() || m_idle[slot]) return;
    m_idle[slot] = 1;
    if (++m_idleCount == m_idle.length()) idle_(app_(), 0);
  }
  void poolResult_(bool failed_) {
    ++m_completed;
    if (failed_) ++m_failed;
  }
  void poolActive_(bool active_) {
    if (active_) ++m_active;
    else --m_active;
  }

private:
  template <typename A>
  static auto idle_(A *app, int) -> decltype(app->idle(), void()) {
    app->idle();
  }
  static void idle_(...) { }

  App *app_() { return static_cast<App *>(this); }
  Pool *pool_(unsigned slot) const {
    return slot < m_pools.length() ? m_pools[slot].ptr() : nullptr;
  }

  // Application-lifecycle state is stable while started.  Result counters are
  // telemetry snapshots and may be read uncleanly by application threads.
  HubConfig	m_hub;
  Config	m_config;
  TCPConfig	m_tcp;
  H2Config	m_h2;
  QUICConfig	m_quic;
  Pools		m_pools;
  Hubs		m_ctl;
  unsigned	m_completed = 0;
  unsigned	m_failed = 0;
  unsigned	m_active = 0;
  bool		m_resolverOwned = false;
  bool		m_inited = false;

  // Tx thread exclusive after start().
  alignas(Zm::CacheLineSize)
  Idle		m_idle;
  unsigned	m_idleCount = 0;
};

} // namespace Zhttp

#endif /* ZhttpClient_HH */
