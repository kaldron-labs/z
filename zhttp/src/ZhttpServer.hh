//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP typed server bindings

#ifndef ZhttpServer_HH
#define ZhttpServer_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuObjectTraits.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmNode.hh>
#include <zlib/ZiTx.hh>
#include <zlib/Zhttp.hh>

namespace Zhttp {

struct ResBuilder : public Builder {
  ResponseRecord record;

  Method::T method() const { return Method::GET; }
  bool disconnect() const { return false; }
  void close() { }
  void completed(const ResponseResult &) { }
  uint64_t routeSequence() const { return record.sequence; }
  void routeSequence(uint64_t value) { record.sequence = value; }
};

struct ResponseExt { };

template <typename B, typename HeapID = ZuStringT<"Zhttp.Response">,
  bool Sharded = false>
struct ResponseQ {
  using T = B;
  using Node = ZmNode<B, ZuDefaultAxor(), ZuDefaultAxor(),
    B, ResponseExt, HeapID, Sharded>;
};

template <typename Builder>
class Route {
public:
  Route() = default;
  Route(Channel *channel, Method::T method) :
    m_channel{channel}, m_method{method},
    m_sequence{channel->requestSequence()}, m_session{channel->session()} { }
  Route(const Route &) = delete;
  Route &operator =(const Route &) = delete;
  Route(Route &&) = default;
  Route &operator =(Route &&) = default;

  explicit operator bool() const { return bool(m_channel); }
  Channel *channel() const { return m_channel.ptr(); }
  Method::T method() const { return m_method; }
  uint64_t sequence() const { return m_sequence; }
  Session session() const { return m_session; }
  void send(ZmRef<Builder> response) const {
    if (m_channel && response)
      m_channel->routeSend(m_sequence,
	bindResponse(ZuMv(response), m_channel->responseCatalog()));
  }
  void close() const { if (m_channel) m_channel->disconnect(); }
  void reset() { m_channel.reset(); }

private:
  ChannelRef m_channel;
  Method::T m_method = Method::GET;
  uint64_t m_sequence = 0;
  Session m_session;
};

struct ServerAppOps {
  ParserBinding (*parser)(void *, const CatalogView &);
  void (*connected)(void *, Transport::T);
  void (*disconnected)(void *, Transport::T);
  void (*sessionConnected)(void *, Session);
  void (*sessionDisconnected)(void *, Session);
  void (*listening)(void *, Transport::T, unsigned);
  void (*listenFailed)(void *, Transport::T, bool);
  void (*seeds)(void *, void (*)(void *, const CatalogView &));
};

class ServerImpl;
struct BoundReqParser;
struct TCPBackend;
struct TLSBackend;
struct QUICBackend;

class ZhttpAPI ServerCore : public ProfileCore {
  friend class ServerImpl;
  friend struct BoundReqParser;
  friend struct TCPBackend;
  friend struct TLSBackend;
  friend struct QUICBackend;

public:
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"ZmEngine.CtrlFn">>;

  ServerCore(const ServerCore &) = delete;
  ServerCore &operator =(const ServerCore &) = delete;
  ~ServerCore();

  bool init(const HubConfig &, ServerConfig);
  void final();
  void stopAccepting();
  void start(CtrlFn);
  void stop(CtrlFn);
  bool start();
  bool stop();
  int state() const;
  bool running() const;
  bool stopping() const;
  bool ok() const;
  void txErrorFn(ZiTxErrorFn);

  ZiMultiplex *mx() const;
  unsigned rxThread() const;
  unsigned txThread() const;
  template <typename L> void rxRun(L &&l) { mx()->run(ZuFwd<L>(l), rxThread()); }
  template <typename L> void txRun(L &&l) { mx()->run(ZuFwd<L>(l), txThread()); }
  unsigned activeConnections() const;
  unsigned activeRequests() const;
  unsigned queuedResponses() const;
  uint64_t retainedBytes() const;
  uint64_t serverFaults() const;
  uint64_t rejectedRequests() const;
  uint64_t parseFailures() const;
  uint64_t bodyFailures() const;
  uint64_t responseBuildFailures() const;
  uint64_t transportFailures() const;
  unsigned hubCount() const;
  const CatalogView &responseCatalog() const { return *m_resCatalog; }

protected:
  ServerCore(void *, const ServerAppOps &,
    const CatalogView &, const CatalogView &);

private:
  ServerImpl &impl() { return *m_impl; }
  void *context() const { return m_app; }
  void seeds(void *ctx, void (*add)(void *, const CatalogView &)) {
    m_ops->seeds(ctx, add);
  }
  ParserBinding parser() const { return m_ops->parser(m_app, *m_reqCatalog); }
  void connected(Transport::T transport) { m_ops->connected(m_app, transport); }
  void disconnected(Transport::T transport) { m_ops->disconnected(m_app, transport); }
  void connected(Session session) { m_ops->sessionConnected(m_app, session); }
  void disconnected(Session session) { m_ops->sessionDisconnected(m_app, session); }
  void listening(Transport::T transport, unsigned port) {
    m_ops->listening(m_app, transport, port);
  }
  void listenFailed(Transport::T transport, bool transient) {
    m_ops->listenFailed(m_app, transport, transient);
  }
  bool backends(const HubConfig &);

  ServerImpl *m_impl;
  void *m_app;
  const ServerAppOps *m_ops;
  const CatalogView *m_reqCatalog;
  const CatalogView *m_resCatalog;
};

template <typename Impl, typename ReqParser, typename ResQ, typename Selection>
class Server : public ServerCore {
  using B = typename ResQ::T;

  ZuAssert((ZuIs_<ReqParser, Zhttp::Parser>{}),
    "Zhttp::Server requires ReqParser to derive from Zhttp::Parser");
  ZuAssert((ZuIs_<typename ResQ::Node, B>{}),
    "Zhttp::Server requires ResQ::Node to derive from ResBuilder_");
  ZuAssert(ZuIsObject<B>{},
    "Zhttp::Server requires ResBuilder_ to be intrusively reference-counted");
  ZuAssert((ZuIs_<B, Zhttp::ResBuilder>{}),
    "Zhttp::Server requires ResBuilder_ to derive from Zhttp::ResBuilder");

public:
  using ResBuilder_ = B;
  using ResBuilder = typename ResQ::Node;
  using ResponseHeaders = typename ResponseHeaderSets<B>::T;
  using Profiles = Selection;
  using Engine = ServerCore;

  Server() : ServerCore{static_cast<Impl *>(this), ops(),
    CatalogData<typename ReqParser::HdrCatalog>::configure(),
    CatalogData<typename B::HdrCatalog>::configure()} {
    ZuUnroll::all<ResponseHeaders>([]<typename Catalog>() {
      (void)seedCatalog<Catalog>();
    });
    Selection::configure(*this);
  }

  void connected(Session) { }
  void disconnected(Session) { }

private:
  template <typename Catalog>
  static const CatalogView &seedCatalog() {
    using List = HeaderList<Catalog>;
    ZuUnroll::all<List::N>([](auto I) {
      using V = typename List::template Value<I>;
      if constexpr (V::N) (void)sizeof(HeaderValue<V>);
    });
    return CatalogData<Catalog>::configure();
  }
  static const ServerAppOps &ops() {
    static constexpr ServerAppOps table{
      [](void *ctx, const CatalogView &catalog) {
	return TypedParser<ReqParser, Impl>::create(catalog, *static_cast<Impl *>(ctx));
      },
      [](void *ctx, Transport::T transport) { static_cast<Impl *>(ctx)->connected(transport); },
      [](void *ctx, Transport::T transport) {
	static_cast<Impl *>(ctx)->disconnected(transport);
      },
      [](void *ctx, Session session) { static_cast<Impl *>(ctx)->connected(session); },
      [](void *ctx, Session session) { static_cast<Impl *>(ctx)->disconnected(session); },
      [](void *ctx, Transport::T transport, unsigned port) {
	static_cast<Impl *>(ctx)->listening(transport, port);
      },
      [](void *ctx, Transport::T transport, bool transient) {
	static_cast<Impl *>(ctx)->listenFailed(transport, transient);
      },
      [](void *ctx, void (*add)(void *, const CatalogView &)) {
	ZuUnroll::all<ResponseHeaders>([ctx, add]<typename Catalog>() {
	  add(ctx, seedCatalog<Catalog>());
	});
      }};
    return table;
  }
};

} // namespace Zhttp

#endif /* ZhttpServer_HH */
