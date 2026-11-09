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

#include <zlib/ZmEngine.hh>
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
  template <typename L>
  bool close(L &&ready) {
    bool done = Dispatch::close();
    return Calls::closeCalls(ZuFwd<L>(ready)) && done;
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

  void ioStarting() {
    m_failed = false;
    ZmAssert_((Caller<IOClient, Catalog>::init(this->limits().maxPending)));
    m_inbound.init(Default::HistSize);
  }
  void ioReady() { this->impl()->ready(); }
  bool stdioFrame(ZmRef<ZiIOBuf> body) {
    return Common::receive(ZuMv(body), [this](auto message, int policy) {
      if (policy == RoutePolicy::Abort) { this->fail_(); return false; }
      return message.empty() || this->send(message);
    }, &m_inbound);
  }
  void ioClosed(bool failed) { m_failed |= failed; stop_1(); }

private:
  using IO::send;
  using IO::fail_;
  using IO::continue_;

  void stop_1() {
    typename Common::ReadyFn ready{this, [](auto self) { self->stop_1(); }};
    if (!Common::close(ready)) {
      if (!Common::callsWaiting()) this->continue_([this]() { stop_1(); });
      return;
    }
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

template <typename Impl, typename Catalog, typename Selection>
class HTTPClient : public Client<HTTPClient<Impl, Catalog, Selection>, Impl, Catalog>,
    private ZmEngine<HTTPClient<Impl, Catalog, Selection>>,
    public Zhttp::Client<HTTPClient<Impl, Catalog, Selection>, HTTPPool<Impl, Catalog,
      Selection>> {
  using Engine = ZmEngine<HTTPClient>;
  friend Engine;
  using Engine::spawn;
  using Engine::wake;
  using Engine::stateChanged;
  using Common = Client<HTTPClient, Impl, Catalog>;
  using Calls = Caller<HTTPClient, Catalog>;
  using HTTP = Zhttp::Client<HTTPClient, HTTPPool<Impl, Catalog, Selection>>;
  using RequestData = HTTPRequestData<Impl, Catalog, Selection>;
  using Request = HTTPRequestQ<Impl, Catalog, Selection>::Node;
  using Meta = HTTPHeaders<HTTPResHeaders<Catalog, Impl>>;
  friend Calls;
  friend BatchCaller<HTTPClient>;
  friend Dispatcher<HTTPClient, Impl, Catalog>;
  friend struct HTTPRequestData<Impl, Catalog, Selection>;
public:
  using Engine::start;
  using Engine::stop;
  using Engine::state;
  using Engine::stopping;

  ~HTTPClient() { final(); }
  bool init(const Zhttp::HubConfig &hub, Zhttp::Destination destination,
      ClientConfig config, Impl *impl, const Zhttp::TCPConfig &tcp = {},
      const Zhttp::H2Config &h2 = {}, const Zhttp::QUICConfig &quic = {}) {
    if (m_mx || !hub.mx() || !config.endpoint() || !Common::init(impl, config.limits()))
      return false;
    m_mx = hub.mx();
    m_owner = hub.rxThread() ? m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_endpoint = config.endpoint();
    config.bodyMax(this->limits().maxJSONBytes)
      .retainedBodyMax(this->limits().maxJSONBytes)
      .retainedMessageMax(this->limits().maxSSEEventBytes);
    if (!HTTP::init(hub, 1, config, tcp, h2, quic) || !HTTP::pool(0, ZuMv(destination))) {
      HTTP::final();
      m_mx = nullptr;
      return false;
    }
    m_failure = false;
    return true;
  }
  void final() {
    if (!m_mx) return;
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    HTTP::final();
    m_mx = nullptr;
    m_endpoint.null();
  }
  bool up() const { return m_up.load_(); }
  CompletionRoute completionRoute() const {
    return {m_mx, m_owner, this->limits().maxQueue};
  }
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
  bool receiveSSE(RequestData *request, Zhttp::SSEEvent event, const Meta &) {
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
  void start_() {
    if (!m_mx) { Engine::started(false); return; }
    HTTP::start([this](bool ok) {
      if (!ok) { Engine::started(false); return; }
      continue_([this]() {
	ZmAssert_(Calls::init(this->limits().maxPending));
	m_failure = false;
	m_up = true;
	try { this->impl()->ready(); } catch (...) { fail_(); }
	if (m_failure) { stop_0(); return; }
	Engine::started(true);
      });
    });
  }
  void stop_() { continue_([this]() { m_up = false; stop_0(); }); }
  void stop_0() {
    HTTP::stop([this](bool ok) {
      continue_([this, ok]() { m_stopOK = ok; stop_1(); });
    });
  }
  void stop_1() {
    typename Common::ReadyFn ready{this, [](auto self) { self->stop_1(); }};
    if (!Common::close(ready)) {
      if (!Common::callsWaiting()) continue_([this]() { stop_1(); });
      return;
    }
    bool ok = m_stopOK;
    HTTP::txRun([this, ok]() {
      HTTP::rxRun([this, ok]() {
	if (m_failure) {
	  try { this->impl()->failed(); } catch (...) { }
	}
	switch (Engine::state()) {
	  case ZmEngineState::Starting:
	  case ZmEngineState::StopPending: Engine::started(false); return;
	  default: Engine::stopped(ok); return;
	}
      });
    });
  }
  void fail_() {
    if (m_failure) return;
    m_failure = true;
    m_up = false;
    if (Engine::state() == ZmEngineState::Running) Engine::stop({});
  }

  ZiMultiplex *m_mx = nullptr;
  HTTPValue m_endpoint;
  unsigned m_owner = 0;
  uint64_t m_next = 1;
  ZmAtomic<unsigned> m_up = 0;
  bool m_failure = false;
  bool m_stopOK = true;
};

// One run owns one WebSocket connection. Restart opens fresh RPC state and
// cannot revive pending calls or historic IDs from the preceding run.
template <typename Impl, typename Catalog, typename Profile = Zhttp::H1TCP>
class WSClient : public Client<WSClient<Impl, Catalog, Profile>, Impl, Catalog>,
    public WSIO<WSClient<Impl, Catalog, Profile>>,
    private ZmEngine<WSClient<Impl, Catalog, Profile>> {
  using Engine = ZmEngine<WSClient>;
  friend Engine;
  using Common = Client<WSClient, Impl, Catalog>;
  using Calls = Caller<WSClient, Catalog>;
  using IO = WSIO<WSClient>;
  friend IO;
  friend Caller<WSClient, Catalog>;
  friend BatchCaller<WSClient>;
  friend Dispatcher<WSClient, Impl, Catalog>;
public:
  using Engine::start;
  using Engine::stop;
  using Engine::state;
  using Engine::stopping;
  using LinkState = WSClientState;
  using WS = Zws::Client<WSClient, Profile>;
  using Link = WS::Link;
  using Config = WS::Config;

  WSClient() : m_ws{this} { }
  ~WSClient() { final(); }
  bool init(const Zhttp::HubConfig &hub, Config profile, Zhttp::Destination destination,
    WSConfig config, Impl *impl) {
    if (m_mx || !hub.mx() || !Common::init(impl, config.limits())) return false;
    m_mx = hub.mx();
    m_owner = hub.txThread() ? m_mx->sid(hub.txThread()) : m_mx->txThread();
    if (!m_ws.init(hub, ZuMv(profile), ZuMv(destination), config.binding())) {
      m_mx = nullptr; return false;
    }
    m_inbound.init(Default::HistSize);
    m_failed = m_closed = m_down = false;
    return true;
  }
  template <typename ...Args>
  bool connect(const Zws::URI &uri, Args &&...args) {
    if (Engine::state() != ZmEngineState::Running ||
	m_connect.cmpXch(WSConnectState::Posted, WSConnectState::Idle) != WSConnectState::Idle)
      return false;
    try {
      ZmRef<Link> link = new Link{&m_ws, uri, ZuFwd<Args>(args)...};
      continue_([this, link = ZuMv(link)]() mutable { connectPosted_(ZuMv(link)); });
    } catch (...) {
      continue_([this]() { connectPosted_({}); });
      throw;
    }
    return true;
  }
  void final() {
    if (!m_mx) return;
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!m_closing && !m_waitConnect);
    m_link = nullptr;
    m_ws.final();
    m_mx = nullptr;
  }
  WS &ws() { return m_ws; }
  const WS &ws() const { return m_ws; }
  bool up() const { return m_up.load_() && wsAccept(); }
  bool wsAccept() const { return Engine::state() == ZmEngineState::Running; }
  CompletionRoute completionRoute() const {
    return {m_mx, m_owner, this->limits().maxQueue};
  }
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
    if (!wsAccept()) { link.close(Zws::CloseCode::GoingAway); return; }
    ZmRef<WSOpen<Link>> action = new WSOpen<Link>{&link, ZuMv(info)};
    continue_([this, action = ZuMv(action)]() mutable {
      if (!wsAccept()) return;
      m_up = true;
      try {
	Zws::H1_::connected(*this->impl(), *action->link, ZuMv(action->info), 0);
	this->impl()->ready();
      } catch (...) { fail_(); }
    });
  }
  void connectFailed(Link &link, bool) {
    link.state().rx.close();
    continue_([this]() {
      m_up = false;
      m_down = true;
      m_failed = true;
      close_0();
    });
  }
  void disconnected(Link &link, bool) {
    link.state().rx.close();
    continue_([this]() {
      m_up = false;
      m_down = true;
      close_0();
    });
  }
  void closed(Link &link, uint16_t code, ZuBSpan reason) {
    IO::template control_<true>(link, code, reason);
  }
  void error(Link &link, Zws::Failure::T failure) {
    // Transport shutdown can end the wire before the close handshake finishes.
    bool cancelled = failure == Zws::Failure::AbnormalClose && Engine::stopping();
    continue_([this, link = ZmRef{&link}, failure, cancelled]() mutable {
      m_up = false;
      if (!cancelled) m_failed = true;
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
  void wsClosed() { m_up = false; }
  template <typename M>
  bool send(const M &message) { return up() && m_link && IO::sendWS_(*m_link, message); }
  template <typename L> void continue_(L &&l) { m_ws.txRun(ZuFwd<L>(l)); }
  void start_() {
    if (!m_mx) { Engine::started(false); return; }
    m_ws.start([this](bool ok) {
      if (!ok) { Engine::started(false); return; }
      continue_([this]() {
	ZmAssert_(!m_link && !m_closing && !m_waitConnect);
	ZmAssert_(Calls::init(this->limits().maxPending));
	m_inbound.init(Default::HistSize);
	m_failed = m_closed = m_down = false;
	m_transportStopped = false;
	m_connect = WSConnectState::Idle;
	Engine::started(true);
      });
    });
  }
  void stop_() {
    int previous;
    do {
      previous = m_connect.load_();
    } while (m_connect.cmpXch(previous == WSConnectState::Posted ?
	WSConnectState::StopPosted : WSConnectState::Stopped, previous) != previous);
    continue_([this]() { stop_0(); });
  }
  void connectPosted_(ZmRef<Link> link) {
    int state = m_connect.cmpXch(link ? WSConnectState::Assigned : WSConnectState::Idle,
      WSConnectState::Posted);
    if (state == WSConnectState::Posted) {
      if (!link) return;
      m_link = link;
      m_ws.rxRun([link = ZuMv(link)]() mutable { link->connect(); });
      return;
    }
    ZmAssert_(state == WSConnectState::StopPosted);
    m_connect = WSConnectState::Stopped;
    if (m_waitConnect) { m_waitConnect = false; stop_0(); }
  }
  void stop_0() {
    m_up = false;
    if (m_connect.load_() == WSConnectState::StopPosted) {
      m_waitConnect = true;
      return;
    }
    if (m_link && !m_down)
      m_ws.rxRun([this, link = m_link]() { IO::stopWS(*link); });
    m_ws.stop([this](bool ok) {
      continue_([this, ok]() {
	m_stopOK = ok;
	m_transportStopped = true;
	close_0();
      });
    });
  }
  void close_2() {
    if (!m_transportStopped || !m_closed || m_closing) return;
    m_transportStopped = false;
    m_ws.rxRun([this]() {
      continue_([this]() {
	m_link = nullptr;
	Engine::stopped(m_stopOK);
      });
    });
  }
  void fail_() {
    m_failed = true;
    m_up = false;
    if (m_link) m_link->close(Zws::CloseCode::Internal);
  }
  void close_0() {
    m_up = false;
    if (m_closed) { close_2(); return; }
    if (m_closing) return;
    m_closing = true;
    close_1();
  }
  void close_1() {
    typename Common::ReadyFn ready{this, [](auto self) { self->close_1(); }};
    if (!Common::close(ready)) {
      if (!Common::callsWaiting()) continue_([this]() { close_1(); });
      return;
    }
    (void)m_inbound.close(0);
    m_closing = false;
    m_closed = true;
    try {
      if (m_failed) this->impl()->failed();
      else this->impl()->closed();
    } catch (...) { }
    close_2();
  }

  WS m_ws;
  ZmRef<Link> m_link;
  InboundCalls m_inbound;
  ZiMultiplex *m_mx = nullptr;
  unsigned m_owner = 0;
  ZmAtomic<unsigned> m_up = 0;
  ZmAtomic<int> m_connect = WSConnectState::Stopped;
  bool m_closing = false;
  bool m_transportStopped = false;
  bool m_stopOK = true;
  bool m_waitConnect = false;
  bool m_failed = false;
  bool m_closed = false;
  bool m_down = false;
};

} // Zjrpc

#endif /* ZjrpcClient_HH */
