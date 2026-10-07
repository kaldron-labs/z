//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC typed dispatch and servers

#ifndef ZjrpcServer_HH
#define ZjrpcServer_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZjrpcDispatch.hh>
#include <zlib/ZjrpcStdio.hh>
#include <zlib/ZjrpcPending.hh>
#include <zlib/ZjrpcHTTP.hh>
#include <zlib/ZjrpcWS.hh>

namespace Zjrpc {

template <typename Derived, typename Impl, typename Catalog>
class Server {
public:
  const Limits &limits() const { return m_limits; }
  unsigned histSize() const { return m_histSize; }
  void histSize(unsigned size) { m_histSize = size; }
protected:
  Impl *impl() const { return m_impl; }
  bool init(Impl *impl, Limits limits) {
    if (!impl || !valid(limits)) return false;
    m_impl = impl;
    m_limits = limits;
    return true;
  }
private:
  Impl *m_impl = nullptr;
  Limits m_limits;
  unsigned m_histSize = Default::HistSize;
};

template <typename Impl, typename Catalog>
class IOServer : public Server<IOServer<Impl, Catalog>, Impl, Catalog>,
    public Dispatcher<IOServer<Impl, Catalog>, Impl, Catalog>,
    public IOEndpoint<IOServer<Impl, Catalog>>,
    public Caller<IOServer<Impl, Catalog>, Catalog> {
  using Common = Server<IOServer, Impl, Catalog>;
  using IO = IOEndpoint<IOServer>;
  using Calls = Caller<IOServer, Catalog>;
  using Dispatch = Dispatcher<IOServer, Impl, Catalog>;
  friend Dispatch;
  friend Calls;
  friend BatchCaller<IOServer>;
public:
  ~IOServer() { IO::final(); }

  bool init(ZiMultiplex *mx, StdioConfig config, Impl *impl) {
    if (!Common::init(impl, config.limits())) return false;
    if (!Calls::init(config.limits().maxPending)) return false;
    if (!IO::init(mx, ZuMv(config))) return false;
    m_inbound.init(this->histSize());
    return true;
  }
  void ioReady() { }
  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    return Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == RoutePolicy::Abort) { this->fail_(); return false; }
      return message.empty() || this->send(message);
    }, &m_inbound);
  }
  void ioClosed(bool failed) { m_failed |= failed; drain_(); }

private:
  using IO::fail_;
  using IO::continue_;

  void drain_() {
    bool done = Dispatch::close();
    if (!Calls::closeCalls() || !done) { this->continue_([this]() { drain_(); }); return; }
    (void)m_inbound.close(0);
    try {
      if (m_failed) this->impl()->failed();
      else this->impl()->closed();
    } catch (...) { }
    this->ioDone();
  }

  InboundCalls m_inbound;
  bool m_failed = false;
};

template <typename Impl, typename Catalog>
class HTTPServer : public Server<HTTPServer<Impl, Catalog>, Impl, Catalog>,
    public Dispatcher<HTTPServer<Impl, Catalog>, Impl, Catalog,
      HTTPContext<HTTPReqHeaders<Catalog, Impl>>> {
  using Common = Server<HTTPServer, Impl, Catalog>;
  using Dispatch = Dispatcher<HTTPServer, Impl, Catalog,
    HTTPContext<HTTPReqHeaders<Catalog, Impl>>>;
  friend Dispatch;
public:
  using Headers = HTTPReqHeaders<Catalog, Impl>;
  using Context = HTTPContext<Headers>;
  using ResBuilderQ = ZmList<HTTPResponseData<Impl, Catalog>,
    ZmListNode<HTTPResponseData<Impl, Catalog>,
      ZmListHeapID<"Zjrpc.HTTP.Response">>>;
  using Response = typename ResBuilderQ::Node;

  class Parser : public HTTPRequestParser<Parser, Headers> {
    using Base = HTTPRequestParser<Parser, Headers>;
  public:
    void init(HTTPServer &server) { m_server = &server; }
    ZuCSpan endpoint() const { return m_server->endpoint(); }
    const Limits &limits() const { return m_server->limits(); }
    template <typename Link>
    void receiveHTTP(Link *link, ZmRef<ZiIOBuf> body, HTTPHeaders<Headers> headers) {
      m_server->receiveHTTP(link, ZuMv(body), ZuMv(headers));
    }
    template <typename Link>
    void corruptHTTP(Link *link) { if (link) link->disconnect(); }
    void reset() { Base::reset(); m_server = nullptr; }
  private:
    HTTPServer *m_server = nullptr;
  };
  using HTTP = Zhttp::Server<HTTPServer>;

  ~HTTPServer() { final(); }
  bool init(const Zhttp::HubConfig &hub, ServerConfig config, Impl *impl) {
    if (m_mx || !hub.mx() || !config.endpoint() || !Common::init(impl, config.limits()))
      return false;
    m_mx = hub.mx();
    m_owner = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_endpoint = config.endpoint();
    this->histSize(config.histSize());
    uint64_t retained = uint64_t(this->limits().maxSSEEventBytes) + Default::SSEFrameOverhead;
    if (retained < this->limits().maxJSONBytes) retained = this->limits().maxJSONBytes;
    config.retainedBodyMax(this->limits().maxJSONBytes).retainedMessageMax(retained);
    if (!m_http.init(hub, ZuMv(config), this)) { m_mx = nullptr; return false; }
    m_up = true;
    return true;
  }
  bool start() { return m_http.start(); }
  bool stop() {
    if (!m_mx || m_done) return true;
    m_up = false;
    bool ok = m_http.stop();
    ZmBlock<>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable { drain_(ZuMv(wake)); }, m_owner);
    });
    m_done = true;
    return ok;
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    m_http.final();
    m_mx = nullptr;
    m_endpoint.null();
  }
  HTTP &http() { return m_http; }
  const HTTP &http() const { return m_http; }
  ZuCSpan endpoint() const { return m_endpoint; }
  bool up() const { return m_up.load_(); }
  CompletionRoute completionRoute() const { return {m_mx, m_owner}; }
  bool invoked() const { return m_mx && m_mx->invoked(m_owner); }
  template <typename L>
  bool ownerRun(L &&l) {
    if (!up()) return false;
    if (invoked()) l();
    else m_mx->run(ZuFwd<L>(l), m_owner);
    return true;
  }

  template <typename Link>
  void receiveHTTP(Link *link, ZmRef<ZiIOBuf> body, HTTPHeaders<Headers> headers) {
    if (!link || !body || !up()) return;
    using R = Route<Link>;
    ZmRef<R> route = new R{this, link, ZuMv(headers)};
    ownerRun([this, route = ZuMv(route), body = ZuMv(body)]() mutable {
      if (!up()) return;
      route->open_();
      auto inbound = route->peer ? route->peer->inbound() : nullptr;
      auto context = static_cast<Context *>(route.ptr());
      auto retained = route;
      if (!Dispatch::receive(ZuMv(body),
	  [route = ZuMv(retained)](auto message, int policy) mutable {
	    return route->send_(ZuMv(message), policy);
	  }, inbound, context, &route->control())) route->abort_();
    });
  }

  void listening(int transport, unsigned port) { this->impl()->listening(transport, port); }
  void listenFailed(int transport, bool transient) {
    this->impl()->listenFailed(transport, transient);
  }
  void connected(int transport) { this->impl()->connected(transport); }
  void disconnected(int transport) { this->impl()->disconnected(transport); }

private:
  // HTTP accepts client requests; it supplies no unsolicited server-call channel.
  bool response(const Envelope &) { return false; }
  void fail_() { }
  template <typename L> void continue_(L &&l) { m_mx->run(ZuFwd<L>(l), m_owner); }
  template <typename Done>
  void drain_(Done done) {
    if (Dispatch::close()) { done(); return; }
    continue_([this, done = ZuMv(done)]() mutable { drain_(ZuMv(done)); });
  }

  template <typename Link, typename Heap = ZuVoid>
  class Route_ : public Heap, public ZmObject, public Context {
  public:
    Route_(HTTPServer *server, Link *link, HTTPHeaders<Headers> headers) :
      Context{ZuMv(headers), {}}, m_server{server}, m_link{link} { }
    ~Route_() { if (m_hook) m_link->responseCancel({}); }
    WorkControl &control() { return m_work; }
    void open_() {
      this->peer = httpPeer(m_server->impl(), this->headers, 0);
      m_hook = true;
      m_link->responseCancel(Zhttp::StreamCancelFn{this, [](Route_ *self) {
	self->m_hook = false;
	self->m_link = nullptr;
	if (!self->peer) self->m_work.close();
      }});
    }
    bool send_(typename Dispatch::Message message, int policy) {
      if (!m_link) return true;
      if (policy == RoutePolicy::Abort) { abort_(); return true; }
      ZmRef<Response> response = new Response{};
      if (!response->data().init(m_server->impl(), ZuMv(message), policy, m_server->limits())) {
	abort_();
	return true;
      }
      m_link->responseCancel({});
      m_hook = false;
      m_link->send(ZuMv(response));
      m_link = nullptr;
      return true;
    }
    void abort_() {
      if (!m_link) return;
      if (m_hook) m_link->responseCancel({});
      m_hook = false;
      m_link->disconnect();
      m_link = nullptr;
    }
  private:
    HTTPServer *m_server;
    WorkControl m_work;
    ZmRef<Link> m_link;
    bool m_hook = false;
  };
  template <typename Link>
  using RouteHeap = ZmHeap<"Zjrpc.HTTP.Route", Route_<Link>>;
  template <typename Link>
  using Route = Route_<Link, RouteHeap<Link>>;

  HTTP m_http;
  ZiMultiplex *m_mx = nullptr;
  HTTPValue m_endpoint;
  unsigned m_owner = 0;
  ZmAtomic<unsigned> m_up = 0;
  bool m_done = false;
};

template <typename Impl, typename Catalog, typename Profile = Zhttp::H1TCP>
class WSServer : public Server<WSServer<Impl, Catalog, Profile>, Impl, Catalog>,
    public WSIO<WSServer<Impl, Catalog, Profile>> {
  using Common = Server<WSServer, Impl, Catalog>;
  using IO = WSIO<WSServer>;
  friend IO;
public:
  using WS = Zws::Server<WSServer, Profile>;
  using Link = typename WS::Link;
  using Config = typename WS::Config;

  // The native Zws link owns its RPC state in place. No parallel peer registry
  // or second link type is needed; application callbacks receive that link.
  class Connection : public Dispatcher<Connection, Impl, Catalog, Link>,
      public Caller<Connection, Catalog> {
    using Dispatch = Dispatcher<Connection, Impl, Catalog, Link>;
    using Calls = Caller<Connection, Catalog>;
    friend Dispatch;
    friend Calls;
    friend BatchCaller<Connection>;
  public:
    Connection(WSServer *server, Link *link) :
      m_server{server}, m_link{link}, m_inbound{server->histSize()} {
      (void)Calls::init(limits().maxPending);
    }
    const Limits &limits() const { return m_server->limits(); }
    bool up() const { return m_up.load_() && m_server->wsAccept(); }
    CompletionRoute completionRoute() const { return m_server->completionRoute(); }
    bool invoked() const { return m_server->invoked(); }
    template <typename L>
    bool ownerRun(L &&l) {
      if (!up()) return false;
      if (invoked()) l();
      else continue_(ZuFwd<L>(l));
      return true;
    }
    void receive(ZmRef<ZiIOBuf> body) {
      if (!up()) return;
      if (!Dispatch::receive(ZuMv(body), [this](auto message, int policy) {
	    if (policy == RoutePolicy::Abort) { fail_(); return false; }
	    return message.empty() || send(message);
	  }, &m_inbound, m_link)) fail_();
    }
    bool close() {
      m_up = false;
      bool done = Dispatch::close();
      done = Calls::closeCalls() && done;
      if (done) (void)m_inbound.close(0);
      return done;
    }

  private:
    Impl *impl() const { return m_server->impl(); }
    template <typename M>
    bool send(const M &message) { return up() && m_server->send_(*m_link, message); }
    template <typename L> void continue_(L &&l) { m_server->ws().txRun(ZuFwd<L>(l)); }
    void fail_() { m_up = false; m_link->close(Zws::CloseCode::Internal); }

    WSServer *m_server;
    Link *m_link;
    InboundCalls m_inbound;
    ZmAtomic<unsigned> m_up = 1;
  };
  struct LinkState {
    WSRx rx;
    // Construct once before publishing the connected link to the application.
    // Drain on disconnect, but retain the object until the native link dies:
    // off-shard Caller entry points may still inspect its atomic up/ingress state.
    ZuUnion<void, Connection> rpc;
  };

  ~WSServer() { final(); }
  bool init(const Zhttp::HubConfig &hub, ZiIP address, unsigned port,
      Config profile, WSConfig config, Impl *impl) {
    if (m_mx || !hub.mx() || !port || !Common::init(impl, config.limits())) return false;
    m_mx = hub.mx();
    m_owner = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    auto transport = new (m_ws.template new_<WS>()) WS{this, ZuMv(address), port};
    if (!transport->init(hub, ZuMv(profile), config.binding())) {
      m_ws.null();
      m_mx = nullptr;
      return false;
    }
    return true;
  }
  bool start() {
    if (!m_mx || m_started || !ws().start()) return false;
    m_started = true;
    return true;
  }
  bool stop() {
    if (!m_mx) return true;
    m_stopping = true;
    bool ok = !m_started || ws().stop();
    m_started = false;
    ZmBlock<>{}([this](auto wake) mutable {
      ws().txRun([this, wake = ZuMv(wake)]() mutable {
	m_stop = ZuMv(wake);
	stopped_();
      });
    });
    return ok;
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    ws().final();
    m_ws.null();
    m_mx = nullptr;
  }
  WS &ws() { return m_ws.template p<WS>(); }
  const WS &ws() const { return m_ws.template p<WS>(); }
  bool wsAccept() const { return !m_stopping.load_(); }
  CompletionRoute completionRoute() const { return {m_mx, m_owner}; }
  bool invoked() const { return m_mx && m_mx->invoked(m_owner); }

  template <typename Req, typename Call>
  bool call(Link &link, ObjectValue<typename Req::Object> object, ZmRef<Call> call) {
    auto connection = link.state().rpc.template ptr<Connection>();
    return connection && connection->template call<Req>(
      ZuMv(object), ZuMv(call));
  }
  template <typename Req>
  bool notify(Link &link, ObjectValue<typename Req::Object> object) {
    auto connection = link.state().rpc.template ptr<Connection>();
    return connection && connection->template notify<Req>(ZuMv(object));
  }
  template <typename Call>
  bool callBatch(Link &link, Batch<Catalog> batch, ZmRef<Call> call) {
    auto connection = link.state().rpc.template ptr<Connection>();
    return connection && connection->callBatch(ZuMv(batch), ZuMv(call));
  }
  bool notifyBatch(Link &link, Batch<Catalog> batch) {
    auto connection = link.state().rpc.template ptr<Connection>();
    return connection && connection->notifyBatch(ZuMv(batch));
  }

  bool accept(Link &link, ZuBSpan host, ZuBSpan target, ZuBSpan offered,
      Zws::HandshakeString &selected) {
    return wsAccept() && this->impl()->accept(link, host, target, offered, selected);
  }
  void listening(const ZiListenInfo &info) { Zws::H1_::listening(*this->impl(), info, 0); }
  void listening() { Zws::H1_::listening(*this->impl(), 0); }
  void listenFailed(bool transient) { Zws::H1_::listenFailed(*this->impl(), transient, 0); }
  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.state().rx.open();
    ZmRef<WSOpen<Link>> action = new WSOpen<Link>{&link, ZuMv(info)};
    ws().txRun([this, action = ZuMv(action)]() mutable {
      if (!wsAccept()) return;
      auto link = action->link.ptr();
      new (link->state().rpc.template new_<Connection>()) Connection{this, link};
      try {
	Zws::H1_::connected(*this->impl(), *link, ZuMv(action->info), 0);
      } catch (...) { link->close(Zws::CloseCode::Internal); }
    });
  }
  void disconnected(Link &link, bool peer) {
    link.state().rx.close();
    ws().txRun([this, link = ZmRef{&link}, peer]() mutable {
      ++m_closing;
      close_(ZuMv(link), peer);
    });
  }
  void closed(Link &link, uint16_t code, ZuBSpan reason) {
    IO::template control_<true>(link, code, reason);
  }
  void error(Link &link, Zws::Failure::T failure) {
    ws().txRun([this, link = ZmRef{&link}, failure]() mutable {
      try {
	Zws::H1_::error(*this->impl(), *link, failure, 0);
      } catch (...) { link->close(Zws::CloseCode::Internal); }
    });
  }
  void wsFrame_(Link &link, ZmRef<ZiIOBuf> body) {
    ZiAssert(invoked(), "Zjrpc", (), "WS receive outside owner shard", return);
    if (auto connection = link.state().rpc.template ptr<Connection>())
      connection->receive(ZuMv(body));
  }

private:
  template <typename M>
  bool send_(Link &link, const M &message) { return IO::sendWS_(link, message); }
  void close_(ZmRef<Link> link, bool peer) {
    auto connection = link->state().rpc.template ptr<Connection>();
    if (connection && !connection->close()) {
      ws().txRun([this, link = ZuMv(link), peer]() mutable { close_(ZuMv(link), peer); });
      return;
    }
    --m_closing;
    try { Zws::H1_::disconnected(*this->impl(), *link, peer, 0); } catch (...) { }
    stopped_();
  }
  void stopped_() {
    if (m_closing || !m_stop) return;
    auto done = ZuMv(m_stop);
    ws().rxRun([this, done = ZuMv(done)]() mutable {
      ws().txRun([done = ZuMv(done)]() mutable { done(); });
    });
  }

  ZuUnion<void, WS> m_ws;
  ZiMultiplex *m_mx = nullptr;
  ZmFn<void()> m_stop;
  unsigned m_owner = 0;
  unsigned m_closing = 0;
  ZmAtomic<unsigned> m_stopping = 0;
  bool m_started = false;
};

} // Zjrpc

#endif /* ZjrpcServer_HH */
