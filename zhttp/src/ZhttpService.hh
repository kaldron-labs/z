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
  MessageString	pathStorage;
  MessageString	queryStorage;
  MessageString	authority;
  MessageString	protocol;
  MessageString	host;
  MessageString	authorization;
  MessageString	range;
  MessageString	ifModifiedSince;
  MessageString	connection;
  MessageString	referer;
  MessageString	userAgent;
  MessageString	remote;
  uint64_t	bodyReceived = 0;
  uint64_t	bodyConsumed = 0;
  uint64_t	bodyReset = 0;
  uint64_t	bodyDiscarded = 0;
  uint32_t	pathOffset = 0;
  uint32_t	pathLength = 0;
  uint32_t	queryOffset = 0;
  uint32_t	queryLength = 0;
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

class ServiceConfig {
public:
  ServiceConfig() { m_tls.policy(H2Policy::Prefer); }

  const ZiIP &localIP() const { return m_localIP; }
  uint16_t port() const { return m_port; }
  unsigned idleTimeout() const { return m_idleTimeout; }
  unsigned maxConnections() const { return m_maxConnections; }
  unsigned altSvcMaxAge() const { return m_altSvcMaxAge; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
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
  ServiceConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ServiceConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
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
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  uint16_t	m_port = 0;
  bool		m_tcpEnabled = false;
  bool		m_tlsEnabled = false;
  bool		m_quicEnabled = false;
};

template <typename Completion>
struct ServiceResponseDone : public ZmObject {
  RequestInfo	request;
  Completion	completion;

  ServiceResponseDone(RequestInfo request_, Completion completion_) :
    request{ZuMv(request_)}, completion{ZuMv(completion_)} { }
};

// Workload_ and its message types are plain application structs.  Service
// wraps RequestParser and each emitted ResponseBuilder in protocol CRTP
// adapters; application types do not inherit Zhttp bases.  response() calls
// emit(builder, completion) at most once.  All Parser and Builder lambda calls
// are synchronous.
#if 0
struct Workload {
  struct RequestParser {
    using Headers = ZhttpHeaders(...);
    static constexpr uint64_t BodyMax = DefltMaxBody;

    void operation(Method::T, const RequestTarget &);
    void version(ZuBSpan);
    void contentLength(uint64_t);
    void chunked();
    template <typename Key> void header(ZuBSpan value);
    // Synchronous queue prompt; incomplete application framing may remain
    // queued for a later decoded-body append.
    template <typename Rx> void body(Rx &);
    void complete(bool ok);
  };

  RequestParser requestParser();

  template <typename Emit>
  void response(const RequestInfo &, RequestParser &, Emit &&emit);
  // emit(ResponseBuilder, completion)
};

struct ResponseBuilder {
  using Headers = ZhttpHeaders(...);
  using Trailers = ZhttpHeaders(...);	// optional
  using BodyPolicy = Body::None;

  unsigned status();
  template <typename L> void reason(L &&l);	// l(value), H1 only
  template <typename Key, typename L> void header(L &&l); // l(value)
  template <typename L> void header(L &&l);		   // l(key, value)
  template <typename Emit> void body(Emit &&emit); // body policies only
  // Fixed policies provision HeaderPad in header<Key>() and patch it here;
  // there is no contentLength() callback.
  template <typename L> void bodyHdrs(L &&l);
  bool close() const;
};
#endif

template <typename Workload_>
class Service {
public:
  using Workload = Workload_;
  using AppRequestParser = typename Workload::RequestParser;
  using ReqHeaders = typename AppRequestParser::Headers;
  using StopFn = Engines::DoneFn;
  static constexpr uint64_t ReqBodyMax =
    ParserBodyMax<AppRequestParser>::V;

private:
  template <typename Protocol> struct Session;
  template <typename Protocol> struct Engine;
  template <typename Protocol> struct Link;
  struct TLSEngine;
  struct TLSH1Link;
  struct TLSH2Link;

  struct RequestOps {
    void reset() {
      request = {};
      if (service) sink = service->m_workload->requestParser();
      bodyReceived = bodyConsumed = bodyPending = 0;
      bodyReset = bodyDiscarded = 0;
      complete_ = false;
    }
    void operation(Method::T method, const RequestTarget &target) {
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
      sink.operation(method, target);
    }
    void version(ZuBSpan version_) {
      request.http10 = ZuCSpan{version_} == "HTTP/1.0";
      sink.version(version_);
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
      sink.template header<Key>(value);
    }
    void contentLength(uint64_t value) { sink.contentLength(value); }
    void chunked() { sink.chunked(); }
    void status(unsigned) { }
    template <typename Rx>
    void body(Rx &rx) {
      uint64_t before = rx.length();
      if (before < bodyPending) {
	complete_ = false;
	return;
      }
      bodyReceived += before - bodyPending;
      sink.body(rx);
      uint64_t pending = rx.length();
      if (pending > before) { complete_ = false; return; }
      bodyConsumed += before - pending;
      bodyPending = pending;
      request.bodyReceived = bodyReceived;
      request.bodyConsumed = bodyConsumed;
      request.bodyReset = bodyReset;
      request.bodyDiscarded = bodyDiscarded;
    }
    template <typename ParserState>
    void complete(typename ParserState::T state) {
      complete_ = state == ParserState::Complete;
      bodyReset += bodyPending;
      bodyDiscarded += bodyPending;
      bodyPending = 0;
      request.bodyReceived = bodyReceived;
      request.bodyConsumed = bodyConsumed;
      request.bodyReset = bodyReset;
      request.bodyDiscarded = bodyDiscarded;
      sink.complete(complete_);
    }

    Service	*service = nullptr;
    RequestInfo	request;
    AppRequestParser sink;
    uint64_t	bodyReceived = 0;
    uint64_t	bodyConsumed = 0;
    uint64_t	bodyPending = 0;
    uint64_t	bodyReset = 0;
    uint64_t	bodyDiscarded = 0;
    bool	complete_ = false;
  };

  template <typename Profile>
  struct Parser :
    public MessageTraits<Profile>::template RequestParser<
      Parser<Profile>, ReqHeaders, ReqBodyMax>,
    public RequestOps {
    using Base = typename MessageTraits<Profile>::template RequestParser<
      Parser, ReqHeaders, ReqBodyMax>;
    using State = typename Base::State;
    void reset() { Base::reset(); RequestOps::reset(); }
    void complete(typename State::T state) {
      RequestOps::template complete<State>(state);
    }
    using RequestOps::body;
    using RequestOps::chunked;
    using RequestOps::contentLength;
    using RequestOps::header;
    using RequestOps::operation;
    using RequestOps::status;
    using RequestOps::version;
  };

  template <typename AppBuilder>
  struct BuilderApp_ {
    using Headers = typename AppBuilder::Headers;

    Service	*service = nullptr;
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
      patches.template header<false, Key>(app, ZuFwd<L>(l));
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
      if (service->m_altSvc) l("alt-svc", service->m_altSvc);
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
  struct Builder :
    public MessageTraits<Profile>::template ResponseBuilder<
      Builder<Profile, AppBuilder, HasBody, Streaming>,
      typename AppBuilder::Headers,
      typename BuilderTrailers<AppBuilder>::T, HasBody, Streaming>,
    public BuilderApp_<AppBuilder> {
    using Base = typename MessageTraits<Profile>::template ResponseBuilder<
      Builder, typename AppBuilder::Headers,
      typename BuilderTrailers<AppBuilder>::T, HasBody, Streaming>;
    using Ops = BuilderApp_<AppBuilder>;
    enum { Optional = AppBuilder::BodyPolicy::Optional };

    Builder(
      Service *service, AppBuilder app, bool suppressPads = false,
      bool rejectContentLength = false) :
      Ops{service, ZuMv(app)} {
      this->h1 = MessageTraits<Profile>::ID == Version::H1;
      this->suppressPads = suppressPads;
      this->rejectContentLength = rejectContentLength;
    }

    bool streamResponse() const { return false; }
    template <typename Key, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (this->rejectContentLength) return;
      if (this->suppressPads) {
	unsigned count = 0;
	this->app.template header<Key>(
	  [this, &l, &count]<typename V>(V &&v) {
	  if (++count > 1) {
	    this->patches.invalidate();
	    return;
	  }
	  if constexpr (!IsHeaderPad<ZuDecay<V>>{})
	    l(ZuFwd<V>(v));
	  });
	return;
      }
      this->patches.template header<
	MessageTraits<Profile>::ID == Version::H1, Key>(
	this->app, ZuFwd<L>(l));
    }
    using Ops::contentLength;
    using Ops::header;
    using Ops::reason;
    using Ops::status;
  };

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

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendResponse_(Link_ &link, Method::T method, AppBuilder &&app_) {
    using App = ZuDecay<AppBuilder>;
    using Policy = typename App::BodyPolicy;
    unsigned status = app_.status();
    if constexpr (!Policy::HasBody) {
      Builder<Profile, App, false, false> builder{
	this, ZuFwd<AppBuilder>(app_), true,
	contentLengthForbidden_(method, status)};
      auto tx = link.transmit(builder);
      builder.response(tx);
      if (!builder.headersValid()) return false;
      builder.finish(tx);
      link.finish();
      return true;
    } else {
      if (!bodyAllowed_(method, status)) {
	Builder<Profile, App, false, false> builder{
	  this, ZuFwd<AppBuilder>(app_), true,
	  contentLengthForbidden_(method, status)};
	auto tx = link.transmit(builder);
	builder.response(tx);
	if (!builder.headersValid()) return false;
	builder.finish(tx);
	link.finish();
	return true;
      }
      if constexpr (Policy::Streaming)
	return sendStreamingResponse_<Profile>(
	  link, ZuFwd<AppBuilder>(app_));
      else
	return sendFixedResponse_<Profile>(
	  link, ZuFwd<AppBuilder>(app_));
    }
  }

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendStreamingResponse_(Link_ &link, AppBuilder &&app_) {
    using App = ZuDecay<AppBuilder>;
    using Policy = typename App::BodyPolicy;
    Builder<Profile, App, true, true> builder{
      this, ZuFwd<AppBuilder>(app_)};
    auto tx = link.transmit(builder);
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    if constexpr (Policy::Optional) {
      builder.emitBody([
	&builder, &tx, &emitted, &duplicate, &writerOK](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	builder.response(tx);
	if (!builder.headersValid()) return;
	auto body = builder.body(tx);
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	if (!body.valid()) writerOK = false;
	if (writerOK) builder.finish(tx);
      });
      if (!emitted) {
	Builder<Profile, App, false, false> empty{
	  this, ZuMv(builder.app), true};
	auto emptyTx = link.transmit(empty);
	empty.response(emptyTx);
	if (!empty.headersValid()) return false;
	empty.finish(emptyTx);
	link.finish();
	return true;
      }
    } else {
      builder.response(tx);
      if (!builder.headersValid()) return false;
      builder.emitBody([
	&builder, &tx, &emitted, &duplicate, &writerOK](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	auto body = builder.body(tx);
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	body.flush();
	if (!body.valid()) writerOK = false;
      });
      if (emitted && writerOK) builder.finish(tx);
    }
    if ((!Policy::Optional && !emitted) || duplicate || !writerOK)
      return false;
    link.finish();
    return true;
  }

  template <typename Profile, typename Link_, typename AppBuilder>
  bool sendFixedResponse_(Link_ &link, AppBuilder &&app_) {
    using App = ZuDecay<AppBuilder>;
    using Policy = typename App::BodyPolicy;
    Builder<Profile, App, true, false> builder{
      this, ZuFwd<AppBuilder>(app_)};
    auto native = link.transmit(builder);
    if constexpr (MessageTraits<Profile>::ID == Version::H2)
      return sendFixedResponseH2_<Profile>(link, builder, native);
    else {
      if constexpr (MessageTraits<Profile>::ID == Version::H3)
	builder.deferCompression();
      RetainedBudget budget{
	.max = m_config.retainedMessageMax()};
      RetainedTx headerTx{native, budget};
      RetainedTx bodyTx{native, budget};
      auto body = builder.body(bodyTx, fixedBodyMax_());
      bool emitted = false;
      bool duplicate = false;
      bool writerOK = false;
      bool headersOK = false;
      builder.emitBody([
	&builder, &headerTx, &body,
	&emitted, &duplicate, &writerOK, &headersOK]
	(auto &&write) {
	(void)headerTx;
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if (!(headersOK = builder.provision())) return;
	if constexpr (MessageTraits<Profile>::ID == Version::H1)
	  builder.response(headerTx);
	if (!builder.headersValid()) headersOK = false;
	writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      body.flush();
      if (emitted) builder.produced = body.produced();
      if ((!Policy::Optional && !emitted) || duplicate ||
	  (emitted && (!headersOK || !writerOK || !body.valid())))
	return false;
      if (!emitted) {
	Builder<Profile, App, false, false> empty{
	  this, ZuMv(builder.app), true};
	auto emptyTx = link.transmit(empty);
	empty.response(emptyTx);
	if (!empty.headersValid()) return false;
	empty.finish(emptyTx);
	link.finish();
	return true;
      }
      if (!body.valid()) return false;
      if (!builder.patch(builder.produced)) return false;
      if constexpr (MessageTraits<Profile>::ID != Version::H1)
	builder.response(headerTx);
      if (!builder.headersValid()) return false;
      builder.finish(bodyTx);
      if (!headerTx.seal() || !bodyTx.seal()) return false;
      headerTx.commit();
      bodyTx.commit();
      link.finish();
      return true;
    }
  }

  template <typename Profile, typename Link_, typename Builder_, typename Tx>
  bool sendFixedResponseH2_(Link_ &link, Builder_ &builder, Tx &tx) {
    tx.defer(m_config.retainedMessageMax());
    auto body = builder.body(tx, fixedBodyMax_());
    bool emitted = false;
    bool duplicate = false;
    bool writerOK = false;
    bool headersOK = false;
    builder.emitBody([
      &builder, &body, &emitted, &duplicate, &writerOK, &headersOK]
      (auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      if (!(headersOK = builder.provision())) return;
      writerOK = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    body.flush();
    if (emitted) builder.produced = body.produced();
    if ((!Builder_::Optional && !emitted) || duplicate ||
	(emitted && (!headersOK || !writerOK || !body.valid())))
      return false;
    if (!emitted) {
      using App = ZuDecay<decltype(builder.app)>;
      Builder<Profile, App, false, false> empty{
	this, ZuMv(builder.app), true};
      auto emptyTx = link.transmit(empty);
      empty.response(emptyTx);
      if (!empty.headersValid()) return false;
      empty.finish(emptyTx);
      link.finish();
      return true;
    }
    if (!body.valid()) return false;
    if (!builder.patch(builder.produced)) return false;
    builder.response(tx);
    if (!builder.headersValid()) return false;
    builder.finish(tx);
    if (!tx.valid()) return false;
    tx.commit();
    link.finish();
    return true;
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
      if (!parser.service) {
	parser.service = link.app()->service;
	parser.sink = parser.service->m_workload->requestParser();
      }
      auto &request = parser.request;
      if (!request.remote) {
	request.remote = link.remote();
	request.transport = Message::Transport::ID;
	request.httpVersion = Message::ID;
	request.secure = Message::Transport::Secure;
      }
      return Base::process(link, rx);
    }

    template <typename Link_>
    int error(Link_ &link, Parser_ &) {
      link.app()->service->failed();
      return -1;
    }

    template <typename Link_>
    int request(Link_ &link, Parser_ &parser) {
      auto service = link.app()->service;
      auto &request = parser.request;
      bool emitted = false;
      service->m_workload->response(
	request, parser.sink,
	[service, &link, &request, &emitted]
	<typename AppBuilder, typename Completion>(
	    AppBuilder &&app, Completion &&completion) {
	  if (emitted) return;
	  emitted = true;
	  using Done = ServiceResponseDone<ZuDecay<Completion>>;
	  ZmRef<Done> done = new Done{
	    ZuMv(request), ZuFwd<Completion>(completion)};
	  auto hold = ZmMkRef(&link);
	  bool close = app.close();
	  Method::T method = done->request.method;
	  link.app()->txRun([
	    service, link = ZuMv(hold), app = ZuFwd<AppBuilder>(app),
	    done = ZuMv(done), method, close]() mutable {
	    bool sent = service->template sendResponse_<Profile>(
	      *link, method, ZuMv(app));
	    auto engine = link->app();
	    engine->rxRun([
	      service, link = ZuMv(link), done = ZuMv(done),
	      sent, close]() mutable {
	      service->m_workload->complete(
		done->request, done->completion, sent);
	      if (!sent) service->failed();
	      if (close || !sent) link->disconnect();
	    });
	  });
	});
      if (!emitted) {
	service->failed();
	return -1;
      }
      return 1;
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
    void connected(Link_ &link, const ConnectedInfo &) {
      service->registerTxError_(link);
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
    void connected(Link_ &link, const ConnectedInfo &) {
      service->registerTxError_(link);
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

  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  // Request metadata callbacks precede request-body input.  Body input is a
  // concrete bounded stream on the Rx shard and is valid only for the
  // synchronous workload callback.  Message completion follows validated,
  // fully consumed input; no workload callback follows terminal completion.
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

  uint64_t fixedBodyMax_() const {
    uint64_t n = m_config.retainedBodyMax();
    if (n > m_config.retainedMessageMax())
      n = m_config.retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
  }

  bool admit() {
    unsigned active = ++m_active;
    if (!m_config.maxConnections() ||
	active <= m_config.maxConnections())
      return true;
    --m_active;
    return false;
  }
  void release(Transport::T transport) {
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
  ZiTxErrorFn	m_txErrorFn;
  ZmAtomic<unsigned> m_active = 0;
  bool		m_failed = false;
};

} // namespace Zhttp

#endif /* ZhttpService_HH */
