//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC clients

#ifndef ZjrpcClient_HH
#define ZjrpcClient_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZmFn.hh>

#include <zlib/ZjrpcDispatch.hh>
#include <zlib/ZjrpcPending.hh>
#include <zlib/ZjrpcStdio.hh>
#include <zlib/ZjrpcHTTP.hh>
#include <zlib/ZjrpcWS.hh>

namespace Zjrpc {

template <typename Derived, typename Impl, typename Catalog>
class Client : public Dispatcher<Derived, Impl, Catalog>,
    public Caller<Derived, Catalog> {
  using Dispatch = Dispatcher<Derived, Impl, Catalog>;
  using Calls = Caller<Derived, Catalog>;
public:
  const Limits &limits() const { return m_limits; }
protected:
  Impl *impl() const { return m_impl; }
  bool init(Impl *impl, Limits limits) {
    if (!impl || !valid(limits) || !Calls::init(limits.maxPending)) return false;
    m_impl = impl;
    m_limits = limits;
    return true;
  }
  bool close() {
    bool done = Dispatch::close();
    return Calls::closeCalls() && done;
  }
private:
  Impl *m_impl = nullptr;
  Limits m_limits;
};

template <typename Impl, typename Catalog>
class IOClient : public Client<IOClient<Impl, Catalog>, Impl, Catalog>,
    public IOEndpoint<IOClient<Impl, Catalog>> {
  using Common = Client<IOClient, Impl, Catalog>;
  using IO = IOEndpoint<IOClient>;
  friend Common;
  friend Caller<IOClient, Catalog>;
  friend BatchCaller<IOClient>;
  friend Dispatcher<IOClient, Impl, Catalog>;
public:
  ~IOClient() { IO::final(); }

  bool init(ZiMultiplex *mx, StdioConfig config, Impl *impl) {
    if (!Common::init(impl, config.limits()) || !IO::init(mx, ZuMv(config))) return false;
    m_inbound.init(Default::HistSize);
    return true;
  }

  void ioReady() { this->impl()->ready(); }
  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    return Common::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == RoutePolicy::Abort) { this->fail_(); return false; }
      return message.empty() || this->send(message);
    }, &m_inbound);
  }
  void ioClosed(bool failed) { m_failed |= failed; drain_(); }

private:
  using IO::send;
  using IO::fail_;
  using IO::continue_;

  void drain_() {
    if (!Common::close()) { this->continue_([this]() { drain_(); }); return; }
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
class HTTPClient : public Client<HTTPClient<Impl, Catalog>, Impl, Catalog>,
    public Zhttp::Client<HTTPClient<Impl, Catalog>, HTTPPool<Impl, Catalog>> {
  using Common = Client<HTTPClient, Impl, Catalog>;
  using Calls = Caller<HTTPClient, Catalog>;
  using HTTP = Zhttp::Client<HTTPClient, HTTPPool<Impl, Catalog>>;
  using RequestData = HTTPRequestData<Impl, Catalog>;
  using Request = typename HTTPRequestQ<Impl, Catalog>::Node;
  using Meta = HTTPHeaders<HTTPResHeaders<Catalog, Impl>>;
  friend Calls;
  friend BatchCaller<HTTPClient>;
  friend Dispatcher<HTTPClient, Impl, Catalog>;
  friend struct HTTPRequestData<Impl, Catalog>;
public:
  ~HTTPClient() { final(); }
  bool init(const Zhttp::HubConfig &hub, Zhttp::Destination destination,
      ClientConfig config, Impl *impl, const Zhttp::TCPConfig &tcp = {},
      const Zhttp::H2Config &h2 = {}, const Zhttp::QUICConfig &quic = {}) {
    if (m_mx || !hub.mx() || !config.endpoint() || !Common::init(impl, config.limits()))
      return false;
    m_mx = hub.mx();
    m_owner = hub.rxThread() ? m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_endpoint = config.endpoint();
    config.retainedBodyMax(this->limits().maxJSONBytes)
      .retainedMessageMax(this->limits().maxSSEEventBytes);
    if (!HTTP::init(hub, 1, config, tcp, h2, quic) || !HTTP::pool(0, ZuMv(destination))) {
      HTTP::final();
      m_mx = nullptr;
      return false;
    }
    return true;
  }
  bool start() {
    if (!m_mx || m_started || !HTTP::start()) return false;
    m_started = true;
    m_up = true;
    return ownerRun([this]() {
      try { this->impl()->ready(); } catch (...) { fail_(); }
    });
  }
  bool stop() {
    if (!m_mx) return true;
    m_up = false;
    if (m_started) HTTP::stop();
    m_started = false;
    ZmBlock<>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable { drain_(ZuMv(wake)); }, m_owner);
    });
    return !m_failure;
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    HTTP::final();
    m_mx = nullptr;
    m_endpoint.null();
  }
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

  bool receive(RequestData *, ZmRef<ZiIOBuf> body) {
    if (!body) return false;
    auto parsed = parse(body->span(), this->limits().maxJSONBytes);
    if (!parsed) return false;
    if (parsed.envelope.kind == MessageKind::Result || parsed.envelope.kind == MessageKind::Error)
      return Calls::response(parsed.envelope);
    if (parsed.envelope.kind != MessageKind::Notification && !parsed.batch()) return false;
    return Common::receive(ZuMv(body), ZuMv(parsed), [](auto message, int policy) {
      return message.empty() && policy != RoutePolicy::Abort;
    }, nullptr);
  }
  bool receiveSSE(RequestData *request, SSEEvent event, const Meta &) {
    bool ok = receive(request, ZuMv(event.body));
    request->responseOK = ok;
    return ok;
  }
  void headers(RequestData *request, const Meta &meta) {
    headerNotify(request->message.id(), meta, 0);
  }
  void completed(RequestData *request, const Zhttp::Result &result) {
    const auto &id = request->message.id();
    if (!id.template is<void>() &&
	(!result.ok() || !request->responseOK ||
	  (!request->message.batch() && Calls::pending(id))))
      (void)Calls::fail(id);
  }

private:
  template <typename M>
  bool send(M message) {
    if (!up()) return false;
    ZmRef<Request> request = new Request{};
    request->client = this;
    request->message = ClientMessage<Catalog>{ZuMv(message)};
    request->endpoint = m_endpoint;
    request->sequence = m_next++;
    request->maxBodyBytes = this->limits().maxJSONBytes;
    return HTTP::send(0, ZuMv(request));
  }
  template <typename I = Impl,
    typename = decltype(ZuDeclVal<I *>()->headers(
      ZuDeclVal<const ID &>(), ZuDeclVal<const Meta &>()), void())>
  void headerNotify(const ID &id, const Meta &meta, int) { this->impl()->headers(id, meta); }
  void headerNotify(const ID &, const Meta &, long) { }
  template <typename L> void continue_(L &&l) { m_mx->run(ZuFwd<L>(l), m_owner); }
  template <typename Done>
  void drain_(Done done) {
    if (Common::close()) { done(); return; }
    continue_([this, done = ZuMv(done)]() mutable { drain_(ZuMv(done)); });
  }
  void fail_() {
    if (m_failure) return;
    m_failure = true;
    m_up = false;
    drain_([this]() {
      try { this->impl()->failed(); } catch (...) { }
    });
  }

  ZiMultiplex *m_mx = nullptr;
  HTTPValue m_endpoint;
  unsigned m_owner = 0;
  uint64_t m_next = 1;
  ZmAtomic<unsigned> m_up = 0;
  bool m_started = false;
  bool m_failure = false;
};

// One endpoint owns one WebSocket connection. A new connection uses a new
// endpoint, so reconnecting cannot revive old pending calls or historic IDs.
template <typename Impl, typename Catalog, typename Profile = Zhttp::H1TCP>
class WSClient : public Client<WSClient<Impl, Catalog, Profile>, Impl, Catalog>,
    public WSIO<WSClient<Impl, Catalog, Profile>> {
  using Common = Client<WSClient, Impl, Catalog>;
  using IO = WSIO<WSClient>;
  friend IO;
  friend Caller<WSClient, Catalog>;
  friend BatchCaller<WSClient>;
  friend Dispatcher<WSClient, Impl, Catalog>;
public:
  using LinkState = WSClientState;
  using WS = Zws::Client<WSClient, Profile>;
  using Link = typename WS::Link;
  using Config = typename WS::Config;

  WSClient() : m_ws{this} { }
  ~WSClient() { final(); }
  bool init(const Zhttp::HubConfig &hub, Config profile, WSConfig config, Impl *impl) {
    if (m_mx || !hub.mx() || !Common::init(impl, config.limits())) return false;
    m_mx = hub.mx();
    m_owner = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    if (!m_ws.init(hub, ZuMv(profile), config.binding())) { m_mx = nullptr; return false; }
    m_inbound.init(Default::HistSize);
    return true;
  }
  bool start() {
    if (!m_mx || m_started || m_stopping.load_() || !m_ws.start()) return false;
    m_started = true;
    return true;
  }
  template <typename ...Args>
  bool connect(const Zws::URI &uri, Args &&...args) {
    if (!m_started || m_link || m_stopping.load_()) return false;
    m_link = new Link{&m_ws, uri, ZuFwd<Args>(args)...};
    if constexpr (Zhttp::ProfileTraits<Profile>::Multiplexed)
      m_link->connect(uri.host, uri.port);
    else
      m_link->connect();
    return true;
  }
  bool stop() {
    if (!m_mx) return true;
    m_stopping = true;
    m_up = false;
    ZmBlock<>{}([this](auto wake) mutable {
      continue_([this, wake = ZuMv(wake)]() mutable {
	m_stop = ZuMv(wake);
	if (m_link && !m_down)
	  m_ws.rxRun([this]() { IO::stopWS_(*m_link); });
	else close_([this]() { stopped_(); });
      });
    });
    bool ok = !m_started || m_ws.stop();
    m_started = false;
    return ok && !m_failed;
  }
  void final() {
    if (!m_mx) return;
    (void)stop();
    m_link = nullptr;
    m_ws.final();
    m_mx = nullptr;
  }
  WS &ws() { return m_ws; }
  const WS &ws() const { return m_ws; }
  bool up() const { return m_up.load_() && !m_stopping.load_(); }
  bool wsAccept() const { return !m_stopping.load_(); }
  CompletionRoute completionRoute() const { return {m_mx, m_owner}; }
  bool invoked() const { return m_mx && m_mx->invoked(m_owner); }
  template <typename L>
  bool ownerRun(L &&l) {
    if (!up()) return false;
    if (invoked()) l();
    else continue_(ZuFwd<L>(l));
    return true;
  }

  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.state().rx.open();
    if (m_stopping.load_()) { link.close(Zws::CloseCode::GoingAway); return; }
    m_up = true;
    ZmRef<WSOpen<Link>> action = new WSOpen<Link>{&link, ZuMv(info)};
    continue_([this, action = ZuMv(action)]() mutable {
      if (!up()) return;
      try {
	Zws::H1_::connected(*this->impl(), *action->link, ZuMv(action->info), 0);
	this->impl()->ready();
      } catch (...) { fail_(); }
    });
  }
  void connectFailed(Link &link, bool) {
    link.state().rx.close();
    m_up = false;
    continue_([this]() {
      m_down = true;
      m_failed = true;
      close_([this]() { stopped_(); });
    });
  }
  void disconnected(Link &link, bool) {
    link.state().rx.close();
    m_up = false;
    continue_([this]() {
      m_down = true;
      close_([this]() { stopped_(); });
    });
  }
  void closed(Link &link, uint16_t code, ZuBSpan reason) {
    m_up = false;
    IO::template control_<true>(link, code, reason);
  }
  void error(Link &link, Zws::Failure::T failure) {
    m_up = false;
    continue_([this, link = ZmRef{&link}, failure]() mutable {
      m_failed = true;
      try { Zws::H1_::error(*this->impl(), *link, failure, 0); } catch (...) { }
    });
  }
  void wsFrame_(Link &, ZmRef<ZiIOBuf> body) {
    ZiAssert(invoked(), "Zjrpc", (), "WS receive outside owner shard", return);
    if (!up()) return;
    if (!Common::receive(ZuMv(body), [this](auto message, int policy) {
	  if (policy == RoutePolicy::Abort) { fail_(); return false; }
	  return message.empty() || send(message);
	}, &m_inbound)) fail_();
  }

private:
  template <typename M>
  bool send(const M &message) { return up() && m_link && IO::sendWS_(*m_link, message); }
  template <typename L> void continue_(L &&l) { m_ws.txRun(ZuFwd<L>(l)); }
  void stopped_() {
    if (!m_stop) return;
    auto done = ZuMv(m_stop);
    m_ws.rxRun([this, done = ZuMv(done)]() mutable {
      continue_([done = ZuMv(done)]() mutable { done(); });
    });
  }
  void fail_() {
    m_failed = true;
    m_up = false;
    if (m_link) m_link->close(Zws::CloseCode::Internal);
  }
  template <typename Done>
  void close_(Done done) {
    m_up = false;
    if (!Common::close()) {
      continue_([this, done = ZuMv(done)]() mutable { close_(ZuMv(done)); });
      return;
    }
    (void)m_inbound.close(0);
    if (!m_closed) {
      m_closed = true;
      try {
	if (m_failed) this->impl()->failed();
	else this->impl()->closed();
      } catch (...) { }
    }
    done();
  }

  WS m_ws;
  ZmRef<Link> m_link;
  ZmFn<void()> m_stop;
  InboundCalls m_inbound;
  ZiMultiplex *m_mx = nullptr;
  unsigned m_owner = 0;
  ZmAtomic<unsigned> m_up = 0;
  ZmAtomic<unsigned> m_stopping = 0;
  bool m_started = false;
  bool m_failed = false;
  bool m_closed = false;
  bool m_down = false;
};

} // Zjrpc

#endif /* ZjrpcClient_HH */
