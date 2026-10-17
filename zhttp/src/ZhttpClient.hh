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
#include <zlib/ZmHash.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/Zhttp.hh>

namespace Zhttp {

struct Endpoint;
struct DiscoveryResolver;

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

  ZhttpAPI void init();
  bool valid() const { return host && authority && port; }
};

struct PrintSink {
  void *ctx;
  void (*put)(void *, PrintView);

  void operator ()(PrintView value) const { put(ctx, value); }
};

struct OperationEmit {
  const void *ctx;
  void (*put)(const void *, PrintSink);

  void operator ()(PrintSink sink) const { put(ctx, sink); }
};

struct OperationSink {
  void *ctx;
  void (*put)(void *, Method::T, OperationEmit);

  void operator ()(Method::T method, OperationEmit emit) const { put(ctx, method, emit); }
};

struct RequestOps {
  bool contentLength;
  BodyPolicy::T (*bodyPolicy)(const void *);
  bool (*idempotent)(const void *, Method::T);
  void (*operation)(void *, OperationSink);
  void (*protocol)(void *, PrintSink);
  void (*body)(void *, BodyEmitter);
  void (*encode)(void *, FieldContext &, void *, void (*)(void *, FieldProvider &));
  void (*connected)(void *, Channel *, const ConnectedInfo &);
  void (*disconnected)(void *, bool);
  void (*connectFailed)(void *, bool);
  void (*selected)(void *, const Endpoint *, uint64_t, uint64_t,
    unsigned, unsigned, Transport::T, Version::T);
  void (*attemptFailed)(void *, uint64_t, uint64_t, unsigned, unsigned,
    unsigned, Transport::T, Version::T, bool, bool);
  void (*redirected)(void *, const URLView &, uint64_t, uint64_t,
    uint64_t, unsigned, uint16_t);
  void (*retried)(void *, uint64_t, uint64_t, uint64_t, uint16_t);
  void (*fallback)(void *, uint64_t, uint64_t, uint64_t, Transport::T, Version::T);
  void (*cancelled)(void *, const Result &);
  void (*completed)(void *, const Result &);
  void (*retired)(void *);
  void (*host)(void *, PrintSink);
};

struct RequestRecord : MessageRecord {
  const RequestOps *requestOps = nullptr;
  const CatalogView *catalog = nullptr;
  unsigned hdrBufSize = 0;

  BodyPolicy::T bodyPolicy() const { return requestOps->bodyPolicy(ctx); }
  bool idempotent(Method::T method) const { return requestOps->idempotent(ctx, method); }
  void operation(OperationSink sink) { requestOps->operation(ctx, sink); }
  void protocol(PrintSink sink) { requestOps->protocol(ctx, sink); }
  void host(PrintSink sink) { requestOps->host(ctx, sink); }
  void body(BodyEmitter emit) { requestOps->body(ctx, ZuMv(emit)); }
  void encode(FieldContext &fields, void *encoder,
      void (*put)(void *, FieldProvider &)) {
    requestOps->encode(ctx, fields, encoder, put);
  }
  void connected(Channel *channel, const ConnectedInfo &info) {
    requestOps->connected(ctx, channel, info);
  }
  void disconnected(bool peer) { requestOps->disconnected(ctx, peer); }
  void connectFailed(bool transient) { requestOps->connectFailed(ctx, transient); }
  void selected(const Endpoint *endpoint, uint64_t request, uint64_t attempt,
      unsigned pool, unsigned link, Transport::T transport, Version::T version) {
    if (requestOps->selected)
      requestOps->selected(ctx, endpoint, request, attempt, pool, link, transport, version);
  }
  void attemptFailed(uint64_t request, uint64_t attempt, unsigned pool, unsigned link,
      unsigned status, Transport::T transport, Version::T version,
      bool transient, bool responseStarted) {
    if (requestOps->attemptFailed)
      requestOps->attemptFailed(ctx, request, attempt, pool, link,
	status, transport, version, transient, responseStarted);
  }
  void redirected(const URLView &url, uint64_t request, uint64_t attempt,
      uint64_t previous, unsigned status, uint16_t redirects) {
    if (requestOps->redirected)
      requestOps->redirected(ctx, url, request, attempt, previous, status, redirects);
  }
  void retried(uint64_t request, uint64_t attempt, uint64_t previous, uint16_t retries) {
    if (requestOps->retried) requestOps->retried(ctx, request, attempt, previous, retries);
  }
  void fallback(uint64_t request, uint64_t attempt, uint64_t previous,
      Transport::T transport, Version::T version) {
    if (requestOps->fallback) requestOps->fallback(ctx, request, attempt, previous, transport,
      version);
  }
  void cancelled(const Result &result) {
    if (requestOps->cancelled) requestOps->cancelled(ctx, result);
  }
  void completed(const Result &result) {
    if (requestOps->completed) requestOps->completed(ctx, result);
  }
  void retired() { requestOps->retired(ctx); }
};

using RequestRef = RecordRef<RequestRecord>;

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
  RequestRecord record;

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

template <typename B>
auto requestConnected(B &builder, Channel *channel, const ConnectedInfo &info, int)
    -> decltype(builder.connected(channel, info), void()) {
  builder.connected(channel, info);
}
template <typename B>
void requestConnected(B &builder, Channel *, const ConnectedInfo &info, long) {
  builder.connected(info);
}

template <typename U, typename = void>
struct HasReqHost : ZuFalse { };
template <typename U>
struct HasReqHost<U, decltype(ZuDeclVal<U &>().host(ZuDeclVal<PrintSink &>()), void())> :
  ZuTrue { };

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

template <typename U, typename = void>
struct HasReqRetired : public ZuFalse { };
template <typename U>
struct HasReqRetired<U, decltype(
  ZuDeclVal<U &>().retired(), void())> : public ZuTrue { };

template <typename B>
struct RequestFields {
  using HdrCatalog = typename B::HdrCatalog;

  B *builder;
  FieldContext &context;
  HeaderSpans<HdrCatalog> spans;

  template <typename Key, typename Value, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length")
      if (context.rejectContentLength) return;
    builderFixedHeader<Key, Value>(builder, ZuFwd<L>(l), 0);
  }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "content-length")
      if (context.rejectContentLength) return;
    if constexpr (HasBuilderHeader<B, Key, L &&>{})
      builder->template header<Key>(ZuFwd<L>(l));
  }
  template <typename L>
  void header(L &&l) { builder->header(ZuFwd<L>(l)); }
  template <typename Key>
  void headerOffset(uint64_t offset, unsigned length) {
    spans.template recordOffset<Key>(offset, length);
  }
  void headerBase(uint8_t *base) { spans.resolve(base); }
  void patch() { spans.patch(*builder); }
};

template <typename Node>
RequestRecord &bindRequest(Node *request, const CatalogView &catalog) {
  using B = ZuDecay<decltype(request->data())>;
  static constexpr RequestOps ops = []() {
    RequestOps table{
      .contentLength = ZuTypeIn<ZuStringT<"content-length">,
	  typename HeaderList<typename B::HdrCatalog>::Keys>{},
      .bodyPolicy = [](const void *ctx) -> BodyPolicy::T {
	return static_cast<const Node *>(ctx)->data().bodyPolicy();
      },
      .idempotent = [](const void *ctx, Method::T method) -> bool {
	return static_cast<const Node *>(ctx)->data().idempotent(method);
      },
      .operation = [](void *ctx, OperationSink sink) {
	static_cast<Node *>(ctx)->data().operation(
	  [sink](Method::T method, auto &&emit) {
	    using Emit = ZuDeref<decltype(emit)>;
	    OperationEmit view{&emit, [](const void *ctx, PrintSink out) {
	      auto &emit = *static_cast<Emit *>(const_cast<void *>(ctx));
	      emit([out](auto &&write) {
		auto printable = Builder_::writer(write);
		out(PrintView::borrow(printable));
	      });
	    }};
	    sink(method, view);
	  });
      },
      .protocol = [](void *ctx, PrintSink sink) {
	static_cast<Node *>(ctx)->data().protocol([sink](auto &&value) {
	  sink(PrintView::borrow(value));
	});
      },
      .body = [](void *ctx, BodyEmitter emit) {
	auto &builder = static_cast<Node *>(ctx)->data();
	if constexpr (HasBuilderBody<B, BodyEmitter &>{}) builder.body(emit);
      },
      .encode = [](void *ctx, FieldContext &context, void *encoder,
	  void (*put)(void *, FieldProvider &)) {
	auto &builder = static_cast<Node *>(ctx)->data();
	RequestFields<B> fields{&builder, context};
	BuilderFields<RequestFields<B>> adapter{&fields, builder.record.catalog};
	auto provider = adapter.provider();
	put(encoder, provider);
      },
      .connected = [](void *ctx, Channel *channel, const ConnectedInfo &info) {
	requestConnected(static_cast<Node *>(ctx)->data(), channel, info, 0);
      },
      .disconnected = [](void *ctx, bool peer) {
	static_cast<Node *>(ctx)->data().disconnected(peer);
      },
      .connectFailed = [](void *ctx, bool transient) {
	static_cast<Node *>(ctx)->data().connectFailed(transient);
      }};
    if constexpr (HasReqHost<B>{}) table.host = [](void *ctx, PrintSink sink) {
      static_cast<Node *>(ctx)->data().host([sink](auto &&value) {
	sink(PrintView::borrow(value));
      });
    };
    if constexpr (HasReqSelected<Node>{})
      table.selected = [](void *ctx, const Endpoint *endpoint,
	  uint64_t request, uint64_t attempt, unsigned pool, unsigned link,
	  Transport::T transport, Version::T version) {
	static_cast<Node *>(ctx)->selected(
	  endpoint, request, attempt, pool, link, transport, version);
      };
    if constexpr (HasReqAttemptFailed<Node>{})
      table.attemptFailed = [](void *ctx, uint64_t request, uint64_t attempt,
	  unsigned pool, unsigned link, unsigned status, Transport::T transport,
	  Version::T version, bool transient, bool started) {
	static_cast<Node *>(ctx)->attemptFailed(request, attempt,
	  pool, link, status, transport, version, transient, started);
      };
    if constexpr (HasReqRedirected<Node>{})
      table.redirected = [](void *ctx, const URLView &url, uint64_t request,
	  uint64_t attempt, uint64_t previous, unsigned status, uint16_t redirects) {
	static_cast<Node *>(ctx)->redirected(url, request, attempt, previous, status, redirects);
      };
    if constexpr (HasReqRetried<Node>{})
      table.retried = [](void *ctx, uint64_t request, uint64_t attempt,
	  uint64_t previous, uint16_t retries) {
	static_cast<Node *>(ctx)->retried(request, attempt, previous, retries);
      };
    if constexpr (HasReqFallback<Node>{})
      table.fallback = [](void *ctx, uint64_t request, uint64_t attempt,
	  uint64_t previous, Transport::T transport, Version::T version) {
	static_cast<Node *>(ctx)->fallback(request, attempt, previous, transport, version);
      };
    if constexpr (HasReqCancelled<Node>{})
      table.cancelled = [](void *ctx, const Result &result) {
	static_cast<Node *>(ctx)->cancelled(result);
      };
    if constexpr (HasReqCompleted<Node>{})
      table.completed = [](void *ctx, const Result &result) {
	static_cast<Node *>(ctx)->completed(result);
      };
    if constexpr (HasReqRetired<Node>{})
      table.retired = [](void *ctx) { static_cast<Node *>(ctx)->retired(); };
    return table;
  }();
  auto &record = request->data().record;
  record.bind(request);
  record.requestOps = &ops;
  record.catalog = &catalog;
  record.hdrBufSize = B::HdrBufSize;
  return record;
}

template <typename Node>
RequestRef bindRequest(ZmRef<Node> request, const CatalogView &catalog) {
  auto &record = bindRequest(request.ptr(), catalog);
  return RequestRef::take(&record, ZuMv(request));
}

class ClientCore;
class PoolImpl;
class ClientImpl;
struct BoundResParser;

struct PoolQueueOps {
  void (*submit)(void *, RequestRef);
  void (*start)(void *);
  void (*stop)(void *);
  void (*send)(void *);
  void (*resend)(void *);
  void (*archive)(void *);
  void (*ack)(void *, RequestRecord *);
  unsigned (*count)(void *);
  unsigned (*stopBatch)(void *, unsigned, void *, void (*)(void *, RequestRef));
  void (*activeAdd)(void *, RequestRecord *, unsigned);
  void (*activeDel)(void *, RequestRecord *, unsigned);
  unsigned (*activeCount)(const void *);
  bool (*keyEqual)(const RequestRecord *, const void *);
  ParserBinding (*parser)(const CatalogView &);
};

class ZhttpAPI PoolCore : public MessageRecord, public ProfileCore {
  friend class PoolImpl;
  friend class ClientImpl;
  friend struct BoundResParser;
  friend struct TCPBackend;
  friend struct TLSBackend;
  friend struct QUICBackend;

public:
  using DoneFn = Hubs::DoneFn;

  PoolCore(const PoolCore &) = delete;
  PoolCore &operator =(const PoolCore &) = delete;
  ~PoolCore();

  bool init(unsigned, Destination, const HubConfig &, const Config &,
    const TCPConfig &, H2Config, const QUICConfig &);
  bool start();
  void start(DoneFn);
  void stop();
  void stop(DoneFn);
  void final();
  void send(RequestRef);
  void seal();
  bool limited(unsigned, bool);
  void txErrorFn(ZiTxErrorFn);
  void discoveryResolver(const DiscoveryResolver *);
  ZiMultiplex *mx() const;
  unsigned rxThread() const;
  unsigned txThread() const;
  template <typename L>
  void txRun(L &&l) { mx()->run(ZuFwd<L>(l), txThread()); }
  template <typename L>
  void rxRun(L &&l) { mx()->run(ZuFwd<L>(l), rxThread()); }
  const Config &config() const;
  const Destination &destination() const;
  unsigned completed() const;
  unsigned failed() const;
  unsigned active() const;
  unsigned hubCount() const;
  void printQUICDiag();
  bool poolH1TxError(unsigned, unsigned, ZeException &);

protected:
  PoolCore(ClientCore *, const PoolQueueOps &,
    const CatalogView &, const CatalogView &);
  const CatalogView &requestCatalog() const { return *m_reqCatalog; }
  bool send_(RequestRecord *, bool);
  bool gap_();
  void *clientContext() const;
  void idleTx_();
  void scheduleSend();
  void scheduleResend();
  void scheduleArchive();
  void cancelQueued_(RequestRef);
  void cancelActive_(unsigned, const void *);

private:
  PoolImpl &impl() { return *m_impl; }
  const PoolQueueOps &queueOps() const { return *m_queueOps; }
  ParserBinding parser() const { return m_queueOps->parser(*m_resCatalog); }
  bool backends(const PoolInit &);

  PoolImpl *m_impl;
  const PoolQueueOps *m_queueOps;
  const CatalogView *m_reqCatalog;
  const CatalogView *m_resCatalog;
};

using PoolRef = RecordRef<PoolCore>;

struct ClientAppOps {
  PoolRef (*pool)(void *);
  void (*idle)(void *);
};

class ZhttpAPI ClientCore {
  friend class PoolImpl;
  friend class PoolCore;

public:
  using DoneFn = Hubs::DoneFn;

  ClientCore(const ClientCore &) = delete;
  ClientCore &operator =(const ClientCore &) = delete;
  ~ClientCore();

  bool init(const HubConfig &, unsigned, const Config & = {},
    const TCPConfig & = {}, const H2Config & = {}, const QUICConfig & = {});
  bool pool(unsigned, Destination, const Config & = {});
  bool start();
  void start(DoneFn);
  void stop();
  void stop(DoneFn);
  void final();
  bool seal(unsigned);
  bool limited(unsigned, unsigned, bool);
  void txErrorFn(ZiTxErrorFn);
  void discoveryResolver(const DiscoveryResolver *);
  ZiMultiplex *mx() const;
  unsigned rxThread() const;
  unsigned txThread() const;
  template <typename L> void rxRun(L &&l) { mx()->run(ZuFwd<L>(l), rxThread()); }
  template <typename L> void txRun(L &&l) { mx()->run(ZuFwd<L>(l), txThread()); }
  unsigned poolCount() const;
  void printQUICDiag();
  unsigned completed() const;
  unsigned failed() const;
  unsigned active() const;
  unsigned completed(unsigned) const;
  unsigned failed(unsigned) const;
  unsigned active(unsigned) const;
  const Config *config(unsigned) const;

protected:
  ClientCore(void *, const ClientAppOps &);
  PoolCore *pool_(unsigned) const;
  bool running() const;

private:
  void *context() const { return m_app; }
  void poolIdle_(unsigned);
  void poolResult_(bool);
  void poolActive_(bool);

  using Pools = ZtArray<PoolRef, ZtArrayHeapID<"Zhttp.Client.Pools">>;
  using Idle = ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.Client.Idle">>;

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


  void *m_app;
  const ClientAppOps *m_ops;
};

template <typename App, typename P> class Client;

template <typename Client_, typename TxQ, typename ResParser_, typename Selection>
class Pool : public ZmObject, public TxQ, public PoolCore {
public:
  using Client = Client_;
  using Tx = TxQ;
  using ReqBuilder = typename Tx::Msg;
  using ReqBuilder_ = typename ReqBuilder::T;
  using ResParser = ResParser_;
  using ReqHdrCatalog = typename ReqBuilder_::HdrCatalog;
  using RespHdrCatalog = typename ResParser::HdrCatalog;
  using Profiles = Selection;
  using Self = Pool;

private:
  using QueueImpl = typename Tx::Impl_;
  using Keys = typename HeaderList<ReqHdrCatalog>::Keys;

  ZuAssert(!Tx::Ordered,
    "Zhttp::Pool requires unordered ZmPQTx acknowledgements");
  ZuAssert((ZuIs_<ReqBuilder, ReqBuilder_>{}),
    "Zhttp::Pool requires TxQ::Msg to derive from ReqBuilder_");
  ZuAssert(ZuIsObject<ReqBuilder_>{},
    "Zhttp::Pool requires ReqBuilder_ to be intrusively reference-counted");
  ZuAssert((ZuIs_<ReqBuilder_, Zhttp::ReqBuilder>{}),
    "Zhttp::Pool requires ReqBuilder_ to derive from Zhttp::ReqBuilder");
  ZuAssert((ZuIs_<ResParser, Zhttp::Parser>{}),
    "Zhttp::Client requires ResParser to derive from Zhttp::Parser");
  ZuAssert((!ZuTypeIn<ZuStringT<"transfer-encoding">, Keys>{}),
    "libZhttp owns request transfer-encoding framing");

  struct ActiveEntry {
    typename Tx::Key key;
    unsigned slot;
  };
  static const typename Tx::Key &activeKey(const ActiveEntry &entry) {
    return entry.key;
  }
  using ActiveHash = ZmHash<ActiveEntry,
    ZmHashKey<activeKey, ZmHashLock<ZmNoLock,
      ZmHashHeapID<"Zhttp.Client.Active">>>>;

public:
  Pool(Client *client) : PoolCore{clientCore(client), ops(),
    CatalogData<ReqHdrCatalog>::configure(), CatalogData<RespHdrCatalog>::configure()} {
    this->bind(static_cast<QueueImpl *>(this));
    Selection::configure(*this);
  }

  Client *client() const { return static_cast<Client *>(clientContext()); }
  void send(ZmRef<ReqBuilder> request) {
    if (!request) return;
    PoolCore::send(bindRequest(ZuMv(request), requestCatalog()));
  }
  void cancel(typename Tx::Key key) {
    this->txRun([this, key]() {
      if (auto request = Tx::abort(key)) {
	cancelQueued_(bindRequest(ZuMv(request), requestCatalog()));
	return;
      }
      this->rxRun([this, key]() {
	auto active = m_activeReqs->findPtr(key);
	cancelActive_(active ? active->data().slot : NullSlot, &key);
      });
    });
  }

  bool send_(ReqBuilder *request, bool more) {
    auto &record = request->data().record;
    // Direct TxQ producers publish the same immutable binding as ingress.
    if (!record.requestOps) bindRequest(request, requestCatalog());
    return PoolCore::send_(&record, more);
  }
  bool resend_(ReqBuilder *request, bool more) { return send_(request, more); }
  bool sendGap_(const typename Tx::Span &, bool) { return gap_(); }
  bool resendGap_(const typename Tx::Span &, bool) { return gap_(); }
  using PoolCore::scheduleSend;
  using PoolCore::scheduleResend;
  using PoolCore::scheduleArchive;
  void rescheduleSend() { scheduleSend(); }
  void rescheduleResend() { scheduleResend(); }
  void rescheduleArchive() { scheduleArchive(); }
  void idleSend() { idleTx_(); }
  void idleResend() { idleTx_(); }
  void idleArchive() { idleTx_(); }
  void idle() { }

private:
  static ClientCore *clientCore(Client *client) {
    return static_cast<Zhttp::Client<Client, QueueImpl> *>(client);
  }
  static QueueImpl &pool(void *ctx) { return *static_cast<QueueImpl *>(ctx); }
  static const QueueImpl &pool(const void *ctx) { return *static_cast<const QueueImpl *>(ctx); }
  static ZmRef<ReqBuilder> typed(RequestRef request) {
    auto node = ZmRef<ReqBuilder>::acquire(static_cast<ReqBuilder *>(request->ctx));
    (void)request.release(&node);
    return node;
  }
  static const PoolQueueOps &ops() {
    static constexpr PoolQueueOps table{
      [](void *ctx, RequestRef request) { pool(ctx).Tx::send(typed(ZuMv(request))); },
      [](void *ctx) { pool(ctx).Tx::start(); },
      [](void *ctx) { pool(ctx).Tx::stop(); },
      [](void *ctx) { pool(ctx).Tx::send(); },
      [](void *ctx) { pool(ctx).Tx::resend(); },
      [](void *ctx) { pool(ctx).Tx::archive(); },
      [](void *ctx, RequestRecord *request) {
	auto key = static_cast<ReqBuilder *>(request->ctx)->key();
	pool(ctx).Tx::ackd(key);
      },
      [](void *ctx) -> unsigned { return pool(ctx).txQueue()->count_(); },
      [](void *ctx, unsigned max, void *owner, void (*put)(void *, RequestRef)) {
	auto &self = pool(ctx);
	unsigned n = 0;
	{
	  auto iter = self.txQueue()->iter();
	  while (n < max && iter()) {
	    auto request = iter.del();
	    ++n;
	    put(owner, bindRequest(ZuMv(request), self.requestCatalog()));
	  }
	}
	return n;
      },
      [](void *ctx, RequestRecord *request, unsigned slot) {
	auto &self = pool(ctx);
	auto key = static_cast<ReqBuilder *>(request->ctx)->key();
	ZmAssert(!self.m_activeReqs->findPtr(key));
	self.m_activeReqs->add(ActiveEntry{key, slot});
      },
      [](void *ctx, RequestRecord *request, unsigned slot) {
	auto key = static_cast<ReqBuilder *>(request->ctx)->key();
	auto active = pool(ctx).m_activeReqs->del(key);
	ZmAssert(active && active->data().slot == slot);
      },
      [](const void *ctx) -> unsigned { return pool(ctx).m_activeReqs->count_(); },
      [](const RequestRecord *request, const void *key) -> bool {
	return static_cast<const ReqBuilder *>(request->ctx)->key() ==
	  *static_cast<const typename Tx::Key *>(key);
      },
      [](const CatalogView &catalog) {
	return TypedParser<ResParser, ReqBuilder,
	  [](ReqBuilder &request) -> decltype(auto) { return request.data(); }>::create(catalog);
      }};
    return table;
  }

  ZmRef<ActiveHash> m_activeReqs = new ActiveHash;
};

template <typename App, typename P>
class Client : public ClientCore {
public:
  using Pool = P;
  using ReqBuilder = typename Pool::ReqBuilder;
  using Key = typename Pool::Tx::Key;
  using Profiles = typename Pool::Profiles;
  using ClientCore::txRun;

  Client() : ClientCore{static_cast<App *>(this), ops()} { }

  bool send(unsigned slot, ZmRef<ReqBuilder> request) {
    if (!running() || !request) return false;
    auto pool = pool_(slot);
    if (!pool) return false;
    static_cast<Pool *>(pool->ctx)->send(ZuMv(request));
    return true;
  }
  bool cancel(unsigned slot, Key key) {
    auto pool = pool_(slot);
    if (!pool) return false;
    static_cast<Pool *>(pool->ctx)->cancel(key);
    return true;
  }
  template <typename L>
  bool txRun(unsigned slot, L &&l) {
    auto pool = pool_(slot);
    if (!pool) return false;
    pool->txRun(ZuFwd<L>(l));
    return true;
  }

private:
  template <typename A,
    typename = decltype(ZuDeclVal<A *>()->idle(), void())>
  static void idle(A *app, int) { app->idle(); }
  static void idle(...) { }
  static const ClientAppOps &ops() {
    static constexpr ClientAppOps table{
      [](void *ctx) {
	ZmRef<Pool> pool = new Pool{static_cast<App *>(ctx)};
#ifdef ZmObject_DEBUG
	pool->ZmObject::debug();
#endif
	return PoolRef::take(static_cast<PoolCore *>(pool.ptr()), ZuMv(pool));
      },
      [](void *ctx) { idle(static_cast<App *>(ctx), 0); }};
    return table;
  }
};

} // namespace Zhttp

#endif /* ZhttpClient_HH */
