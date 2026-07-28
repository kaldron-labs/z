//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - multi-protocol HTTP service coordinator

#ifndef ZhttpService_HH
#define ZhttpService_HH

#ifndef Zhttp_HH
#include <zlib/Zhttp.hh>
#endif

#include <zlib/ZmBlock.hh>

#include <zlib/ZhttpRuntime.hh>

namespace Zhttp {

ZuDerive(MessageString, ZtString<ZtStringHeapID<"Zhttp.Message">>);

struct RequestInfo {
  Method::T	method = -1;
  MessageString	target;
  MessageString	host;
  MessageString	authorization;
  MessageString	range;
  MessageString	ifModifiedSince;
  MessageString	connection;
  MessageString	referer;
  MessageString	userAgent;
  MessageString	remote;
  int8_t	transport = Transport::TCP;
  int8_t	httpVersion = Version::H1;
  bool		secure = false;
  bool		http10 = false;
};

class ServiceConfig {
public:
  ServiceConfig() { m_tls.policy(H2Policy::Prefer); }

  const ZiIP &localIP() const { return m_localIP; }
  uint16_t port() const { return m_port; }
  unsigned idleTimeout() const { return m_idleTimeout; }
  unsigned maxConnections() const { return m_maxConnections; }
  unsigned altSvcMaxAge() const { return m_altSvcMaxAge; }
  bool tcpEnabled() const { return m_tcpEnabled; }
  bool tlsEnabled() const { return m_tlsEnabled; }
  bool quicEnabled() const { return m_quicEnabled; }
  const TCPConfig &tcpConfig() const { return m_tcp; }
  const H2Config &tlsConfig() const { return m_tls; }
  const QUICConfig &quicConfig() const { return m_quic; }
  QUICConfig quicEngineConfig() const {
    QUICConfig config{m_quic};
    // QUIC transport parameters use milliseconds; service idleTimeout uses
    // seconds.  An explicit QUIC value overrides the transport-neutral default.
    if (!config.maxIdleTimeout() && m_idleTimeout)
      config.maxIdleTimeout(uint64_t(m_idleTimeout) * 1000);
    return config;
  }

  ServiceConfig &localIP(ZiIP v) { m_localIP = ZuMv(v); return *this; }
  ServiceConfig &port(uint16_t v) { m_port = v; return *this; }
  ServiceConfig &idleTimeout(unsigned v) {
    m_idleTimeout = v;
    return *this;
  }
  ServiceConfig &maxConnections(unsigned v) {
    m_maxConnections = v;
    return *this;
  }
  ServiceConfig &altSvcMaxAge(unsigned v) {
    m_altSvcMaxAge = v;
    return *this;
  }
  ServiceConfig &tcp(TCPConfig v = {}) {
    m_tcp = ZuMv(v);
    m_tcpEnabled = true;
    return *this;
  }
  ServiceConfig &tls(H2Config v) {
    m_tls = ZuMv(v);
    m_tlsEnabled = true;
    return *this;
  }
  ServiceConfig &quic(QUICConfig v) {
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
  unsigned	m_altSvcMaxAge = 86400;
  uint16_t	m_port = 0;
  bool		m_tcpEnabled = false;
  bool		m_tlsEnabled = false;
  bool		m_quicEnabled = false;
};

template <
  typename Workload_, typename ReqHeaders_, typename RespHeaders_,
  uint64_t ReqBodyMax_>
class Service {
public:
  using Workload = Workload_;
  using ReqHeaders = ReqHeaders_;
  using RespHeaders = RespHeaders_;
  using Response = typename Workload::Response;
  using StopFn = Engines::DoneFn;
  enum { ReqBodyMax = ReqBodyMax_ };

private:
  template <typename Protocol> struct Session;
  template <typename Protocol> struct Engine;
  template <typename Protocol> struct Link;
  struct TLSEngine;
  struct TLSH1Link;
  struct TLSH2Link;

  struct ReqSink {
    void reset() { request = {}; complete_ = false; }
    void operation(Method::T method, ZuBSpan target) {
      request.method = method;
      request.target = ZuCSpan{target};
    }
    void version(ZuBSpan version_) {
      request.http10 = ZuCSpan{version_} == "HTTP/1.0";
    }
    template <typename Key>
    void header(ZuBSpan value) {
      if constexpr (Key{}() == "host")
	request.host = ZuCSpan{value};
      else if constexpr (Key{}() == "authorization")
	request.authorization = ZuCSpan{value};
      else if constexpr (Key{}() == "range")
	request.range = ZuCSpan{value};
      else if constexpr (Key{}() == "if-modified-since")
	request.ifModifiedSince = ZuCSpan{value};
      else if constexpr (Key{}() == "connection")
	request.connection = ZuCSpan{value};
      else if constexpr (Key{}() == "referer")
	request.referer = ZuCSpan{value};
      else if constexpr (Key{}() == "user-agent")
	request.userAgent = ZuCSpan{value};
    }
    void contentLength(uint64_t) { }
    void status(unsigned) { }
    void body(ZuBSpan) { }
    template <typename ParserState>
    void complete(typename ParserState::T state) {
      complete_ = state == ParserState::Complete;
    }

    RequestInfo	request;
    bool	complete_ = false;
  };

  template <typename Profile>
  struct Parser :
    public MessageTraits<Profile>::template Parser<
      Parser<Profile>, true, ReqHeaders, ReqBodyMax>,
    public ReqSink {
    using Base = typename MessageTraits<Profile>::template Parser<
      Parser, true, ReqHeaders, ReqBodyMax>;
    using State = typename Base::State;
    void reset() { Base::reset(); ReqSink::reset(); }
    void complete(typename State::T state) {
      ReqSink::template complete<State>(state);
    }
    using ReqSink::body;
    using ReqSink::contentLength;
    using ReqSink::header;
    using ReqSink::operation;
    using ReqSink::status;
    using ReqSink::version;
  };

  struct RespOps {
    Service		*service = nullptr;
    const Response	*plan = nullptr;

    unsigned status() const {
      return service->m_workload->status(*plan);
    }
    template <typename L>
    void reason(L &&l) const {
      service->m_workload->reason(*plan, ZuFwd<L>(l));
    }
    uint64_t contentLength() const {
      return service->m_workload->contentLength(*plan);
    }
    template <typename Key, typename L>
    void header(L &&l) const {
      service->m_workload->template header<Key>(*plan, ZuFwd<L>(l));
    }
  };

  template <typename Profile>
  struct Builder :
    public MessageTraits<Profile>::template Builder<
      Builder<Profile>, RespHeaders, ZuTypeList<>, true, false>,
    public RespOps {
    using Base = typename MessageTraits<Profile>::template Builder<
      Builder, RespHeaders, ZuTypeList<>, true, false>;
    Builder(Service *service, const Response *response) :
      RespOps{service, response} { }
    template <typename L>
    void header(L &&l) const {
      if constexpr (ZuIsSame<typename Profile::Protocol, TLS>{})
	if (this->service->m_altSvc)
	  l("alt-svc", this->service->m_altSvc);
    }
    using Base::body;
    using RespOps::contentLength;
    using RespOps::header;
    using RespOps::reason;
    using RespOps::status;
  };

  template <typename Profile>
  struct Session :
    public ServerSession<
      Session<Profile>, Parser<Profile>, MessageTraits<Profile>> {
    using Message = MessageTraits<Profile>;
    using Parser_ = Parser<Profile>;
    using Builder_ = Builder<Profile>;
    using Base = ServerSession<Session, Parser_, Message>;
    using Base::parser;

    template <typename Link_>
    int error(Link_ &link, Parser_ &) {
      link.app()->service->failed();
      return -1;
    }

    template <typename Link_>
    int request(Link_ &link, Parser_ &parser) {
      auto service = link.app()->service;
      auto &request = parser.request;
      request.remote = link.remote();
      request.transport = Message::Transport::ID;
      request.httpVersion = Message::ID;
      request.secure = Message::Transport::Secure;
      Response response = service->m_workload->request(request);
      Builder_ builder{service, &response};
      auto tx = link.transmit(builder);
      builder.response(tx);
      service->m_workload->body(tx, builder, response);
      builder.finish(tx);
      link.finish();
      service->m_workload->complete(request, response);
      return Message::OneMessagePerLink ? 1 :
	(service->m_workload->close(response) ? -1 : 1);
    }
  };

  template <typename Profile>
  struct Engine : public Server<Engine<Profile>, Profile> {
    using HTTP = ProfileTraits<Profile>;
    using Link_ = typename Service::template Link<Profile>;
    using Link = Link_;
    Service *service = nullptr;

    Engine(Service *service_) : service{service_} { }
    ZiIP localIP() const { return service->m_config.localIP(); }
    unsigned localPort() const { return service->m_config.port(); }
    unsigned idleTimeout() const { return service->m_config.idleTimeout(); }
    template <typename Info>
    bool admit(const Info &) { return service->admit(); }
    template <typename Link_>
    void connected(Link_ &, const ConnectedInfo &) {
      service->m_workload->connected(
	HTTP::Transport::ID);
    }
    void release() {
      service->release(HTTP::Transport::ID);
    }
    template <typename Info>
    void listening(const Info &info) {
      service->m_workload->listening(
	HTTP::Transport::ID, info.port);
    }
    void listening() {
      service->m_workload->listening(
	HTTP::Transport::ID, service->m_config.port());
    }
    void listenFailed(bool transient) {
      service->m_workload->listenFailed(
	HTTP::Transport::ID, transient);
      service->failed();
    }
  };

  template <typename Profile>
  struct Link :
    public ServerLink<
      Engine<Profile>, Link<Profile>, Profile, Session<Profile>> {
    using Base = ServerLink<
      Engine<Profile>, Link, Profile, Session<Profile>>;
    using Base::Base;
  };

  struct TLSEngine : public TLS_::ServerEngine<TLSEngine> {
    using H1Link = TLSH1Link;
    using H2Link = TLSH2Link;
    Service *service = nullptr;

    TLSEngine(Service *service_) : service{service_} { }
    ZiIP localIP() const { return service->m_config.localIP(); }
    unsigned localPort() const { return service->m_config.port(); }
    unsigned idleTimeout() const {
      return service->m_config.idleTimeout();
    }
    bool admit(const ZiCxnInfo &) { return service->admit(); }
    template <typename Link_>
    void connected(Link_ &, const ConnectedInfo &) {
      service->m_workload->connected(Transport::TLS);
    }
    template <typename Link_>
    void disconnected(Link_ &, bool) { }
    void release() { service->release(Transport::TLS); }
    void listening(const ZiListenInfo &info) {
      service->m_workload->listening(Transport::TLS, info.port);
    }
    void listenFailed(bool transient) {
      service->m_workload->listenFailed(Transport::TLS, transient);
      service->failed();
    }
  };

  struct TLSH1Link :
    public TLS_::ServerH1Logical<
      TLSEngine, TLSH1Link, Session<H1TLS>,
      TLS_::ServerSession<TLSEngine>> {
    using Base = TLS_::ServerH1Logical<
      TLSEngine, TLSH1Link, Session<H1TLS>,
      TLS_::ServerSession<TLSEngine>>;
    using Base::Base;
  };

  struct TLSH2Link :
    public H2_::ServerLogical<
      TLSEngine, TLSH2Link, Session<H2TLS>,
      TLS_::ServerSession<TLSEngine>> {
    using Base = H2_::ServerLogical<
      TLSEngine, TLSH2Link, Session<H2TLS>,
      TLS_::ServerSession<TLSEngine>>;
    using Base::Base;
  };

public:
  Service() : m_tcp{this}, m_tls{this}, m_quic{this} { }

  bool init(
    const EngineConfig &engine, ServiceConfig config, Workload *workload) {
    if (!workload || !config.port()) return false;
    if (!engine.mx()) return false;
    m_mx = engine.mx();
    m_rxThread = engine.rxThread() ?
      m_mx->sid(engine.rxThread()) : m_mx->rxThread();
    m_config = ZuMv(config);
    m_workload = workload;
    m_altSvc.null();
    if (m_config.tlsEnabled() && m_config.quicEnabled() &&
	m_config.altSvcMaxAge())
      m_altSvc << "h3=\":" << m_config.port() << "\"; ma=" <<
	m_config.altSvcMaxAge();
    if (!m_runtime.init()) return false;
    if (m_config.tcpEnabled() &&
	!m_engines.init(m_tcp, engine, m_config.tcpConfig()))
      return false;
    if (m_config.tlsEnabled() &&
	!m_engines.init(m_tls, engine, m_config.tlsConfig()))
      return false;
    if (m_config.quicEnabled() &&
	!m_engines.init(m_quic, engine, m_config.quicEngineConfig()))
      return false;
    return m_engines.count();
  }

  bool start() { return m_engines.start(); }
  template <typename Done>
  void start(Done &&done) { m_engines.start(ZuFwd<Done>(done)); }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn done_{ZuFwd<Done>(done)};
    m_engines.stop([
      this, done = ZuMv(done_)
    ](bool ok) mutable {
      // Native engine teardown can enqueue final link notifications.  Drain
      // the owning Rx shard before allowing finalization to release owners.
      rxRun_([done = ZuMv(done), ok]() mutable { done(ok); });
    });
  }
  void diagnostic(unsigned seconds, DiagnosticFn fn) {
    m_runtime.add(seconds, ZuMv(fn));
  }
  void wait() { m_runtime.wait(); }
  bool wait(unsigned timeout) { return m_runtime.wait(timeout); }
  void final() {
    if (m_mx) (void)stop();
    m_engines.final();
    m_runtime.final();
    m_altSvc.null();
    m_mx = nullptr;
    m_rxThread = 0;
  }

  bool failed() {
    m_failed = true;
    m_runtime.stop();
    return false;
  }
  bool ok() const { return !m_failed; }
  unsigned active() const { return m_active.load_(); }
  unsigned engineCount() const { return m_engines.count(); }

#ifdef Zquic_DEBUG
  void printQUICDiag() { m_quic.printDiag(); }
#endif

private:
  template <typename L>
  void rxRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_rxThread);
  }

  bool admit() {
    unsigned active = ++m_active;
    if (!m_config.maxConnections() ||
	active <= m_config.maxConnections())
      return true;
    --m_active;
    return false;
  }
  void release(int8_t transport) {
    --m_active;
    m_workload->disconnected(transport);
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  ServiceConfig	m_config;
  MessageString	m_altSvc;
  Workload	*m_workload = nullptr;
  Engine<H1TCP>	m_tcp;
  TLSEngine	m_tls;
  Engine<H3QUIC>	m_quic;
  Engines	m_engines;
  Runtime	m_runtime;
  ZmAtomic<unsigned> m_active = 0;
  bool		m_failed = false;
};

} // namespace Zhttp

#endif /* ZhttpService_HH */
