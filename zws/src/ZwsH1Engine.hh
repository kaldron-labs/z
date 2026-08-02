//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// WebSocket-over-HTTP/1 client and server bindings

#ifndef ZwsH1Engine_HH
#define ZwsH1Engine_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/Zhttp.hh>

#include <zlib/ZwsCodec.hh>
#include <zlib/ZwsH1.hh>

namespace Zws {
namespace H1_ {

template <typename App, typename Link>
auto connected(
    App &app, Link &link, Zhttp::ConnectedInfo info, int) ->
  decltype(app.connected(link, ZuMv(info)), void())
{
  app.connected(link, ZuMv(info));
}
template <typename App, typename Link>
void connected(App &, Link &, Zhttp::ConnectedInfo, ...) { }

template <typename App, typename Link>
auto disconnected(App &app, Link &link, bool peer, int) ->
  decltype(app.disconnected(link, peer), void())
{
  app.disconnected(link, peer);
}
template <typename App, typename Link>
void disconnected(App &, Link &, bool, ...) { }

template <typename App, typename Link>
auto connectFailed(App &app, Link &link, bool transient, int) ->
  decltype(app.connectFailed(link, transient), void())
{
  app.connectFailed(link, transient);
}
template <typename App, typename Link>
void connectFailed(App &, Link &, bool, ...) { }

template <typename App, typename Link, typename Rx>
auto process(App &app, Link &link, Rx &rx, int) ->
  decltype(int(app.process(link, rx)))
{
  return int(app.process(link, rx));
}
template <typename App, typename Link, typename Rx>
int process(App &, Link &, Rx &rx, ...) {
  while (rx.input()) {
    if (rx.consume(
	[](ZuBSpan span) -> int64_t { return span.length(); },
	[](ZuBSpan) { }) <= 0)
      break;
  }
  return rx.failed() ? -1 : 1;
}

template <typename App, typename Link>
auto pong(App &app, Link &link, ZuBSpan payload, int) ->
  decltype(app.pong(link, payload), void())
{
  app.pong(link, payload);
}
template <typename App, typename Link>
void pong(App &, Link &, ZuBSpan, ...) { }

template <typename App, typename Link>
auto closed(
    App &app, Link &link, uint16_t code, ZuBSpan reason, int) ->
  decltype(app.closed(link, code, reason), void())
{
  app.closed(link, code, reason);
}
template <typename App, typename Link>
void closed(App &, Link &, uint16_t, ZuBSpan, ...) { }

template <typename App, typename Link>
auto error(App &app, Link &link, Failure::T failure, int) ->
  decltype(app.error(link, failure), void())
{
  app.error(link, failure);
}
template <typename App, typename Link>
void error(App &, Link &, Failure::T, ...) { }

template <typename App, typename Link>
auto accept(
    App &app, Link &link, ZuCSpan host, ZuCSpan target,
    ZuCSpan offered, HandshakeString &selected, int) ->
  decltype(bool(app.accept(link, host, target, offered, selected)))
{
  return bool(app.accept(link, host, target, offered, selected));
}
template <typename App, typename Link>
bool accept(
    App &, Link &, ZuCSpan, ZuCSpan, ZuCSpan,
    HandshakeString &, ...)
{
  return true;
}

template <typename App>
auto listening(App &app, const ZiListenInfo &info, int) ->
  decltype(app.listening(info), void())
{
  app.listening(info);
}
template <typename App>
void listening(App &, const ZiListenInfo &, ...) { }

template <typename App>
auto listening(App &app, int) ->
  decltype(app.listening(), void())
{
  app.listening();
}
template <typename App>
void listening(App &, ...) { }

template <typename App>
auto listenFailed(App &app, bool transient, int) ->
  decltype(app.listenFailed(transient), void())
{
  app.listenFailed(transient);
}
template <typename App>
void listenFailed(App &, bool, ...) { }

} // namespace H1_

template <typename App, typename Profile> class H1ClientLink;
template <typename App, typename Profile> class H1ServerLink;

template <typename App, typename Profile>
class H1Client :
  public Zhttp::ClientEngine<H1Client<App, Profile>, Profile> {
  using Traits = typename Zhttp::ProfileTraits<Profile>::Transport;
  using Base = Zhttp::ClientEngine<H1Client, Profile>;

public:
  static_assert(!Zhttp::ProfileTraits<Profile>::Multiplexed);
  using Link = H1ClientLink<App, Profile>;
  using Config = typename Traits::Config;

  H1Client(App *app) : m_app{app} { }

  bool init(
      const Zhttp::EngineConfig &engine, const Config &config,
      Zws::Config wsConfig = {}) {
    m_config = wsConfig;
    return m_random.init() && Base::init(engine, config);
  }

  App *app() const { return m_app; }
  Ztls::Random &random() { return m_random; }
  const Zws::Config &wsConfig() const { return m_config; }

  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.open_(ZuMv(info));
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) { return link.handshake_(rx); }
  void disconnected(Link &link, bool peer) {
    link.down_(peer);
  }
  void connectFailed(Link &link, bool transient) {
    H1_::connectFailed(*m_app, link, transient, 0);
  }

private:
  App			*m_app;
  Ztls::Random		m_random;
  Zws::Config		m_config;
};

template <typename App, typename Profile>
class H1ClientLink :
  public Zhttp::ClientLink<
    H1Client<App, Profile>, H1ClientLink<App, Profile>, Profile>,
  public Codec<
    H1ClientLink<App, Profile>, H1ClientLink<App, Profile>, false,
    Ztls::Random>,
  public AppLinkState<App> {
  using Engine = H1Client<App, Profile>;
  using Self = H1ClientLink<App, Profile>;
  using HTTPBase = Zhttp::ClientLink<Engine, Self, Profile>;
  using CodecBase =
    Codec<Self, Self, false, Ztls::Random>;
  using StateBase = AppLinkState<App>;

public:
  using RxLayer = typename CodecBase::RxLayer;
  using HTTPBase::up_;
  using HTTPBase::process;
  using CodecBase::txStream;
  StateBase &state() { return *this; }
  const StateBase &state() const { return *this; }

  H1ClientLink(Engine *engine, const URI &uri, ZuCSpan protocol = {}) :
    HTTPBase{engine, ZtString<>{uri.host}, uri.port},
    CodecBase{*this, engine->random(), engine->wsConfig()},
    m_uri{uri}, m_protocol{protocol} { }

  void open_(Zhttp::ConnectedInfo info) {
    this->CodecBase::reopen_(*this);
    m_down = false;
    m_handshakeFailed = false;
    m_info = ZuMv(info);
    if (!this->streamEnable() ||
	!nonce(this->app()->random(), m_key)) {
      failHandshake_();
      this->disconnect();
      return;
    }
    this->CodecBase::opening_();
    m_parser.expected(m_key, m_protocol);
    auto tx = HTTPBase::txStream();
    H1::Request request{m_uri, m_key, m_protocol};
    if (!request.request(tx)) {
      failHandshake_();
      this->disconnect();
      return;
    }
    tx.flush();
  }

  template <typename Rx>
  int handshake_(Rx &rx) {
    auto state = m_parser.process(rx);
    if (state == H1::ClientParser::State::Error) {
      failHandshake_();
      return -1;
    }
    if (state != H1::ClientParser::State::Complete)
      return m_parser.progressed();
    if (!m_parser.valid() || !this->streamAccept()) {
      failHandshake_();
      return -1;
    }
    m_up = true;
    this->CodecBase::up_();
    H1_::connected(*this->app()->app(), *this, ZuMv(m_info), 0);
    return 1;
  }

  template <typename Rx>
  int process(Zhttp::Stream<H1ClientLink> stream, Rx &rx) {
    return CodecBase::process(ZuMv(stream), rx);
  }
  int message(RxLayer &rx) {
    return H1_::process(*this->app()->app(), *this, rx, 0);
  }
  void pong(ZuBSpan payload) {
    H1_::pong(*this->app()->app(), *this, payload, 0);
  }
  void closed(uint16_t code, ZuBSpan reason) {
    H1_::closed(*this->app()->app(), *this, code, reason, 0);
  }
  void error(Failure::T failure) {
    H1_::error(*this->app()->app(), *this, failure, 0);
  }
  ZuCSpan protocol() const { return m_parser.selected(); }

  void down_(bool peer) {
    if (m_down) return;
    m_down = true;
    m_up = false;
    this->CodecBase::disable_();
    this->app()->rxRun([link = ZmMkRef(this), peer]() mutable {
      link->CodecBase::final_();
      auto app = link->app();
      app->txRun([link = ZuMv(link), peer]() mutable {
	auto app = link->app();
	app->rxRun([link = ZuMv(link), peer]() mutable {
	  auto app = link->app();
	  H1_::disconnected(*app->app(), *link, peer, 0);
	});
      });
    });
  }

private:
  void failHandshake_() {
    if (m_handshakeFailed) return;
    m_handshakeFailed = true;
    H1_::error(
      *this->app()->app(), *this, Failure::Handshake, 0);
  }

  URI			m_uri;
  HandshakeString	m_protocol;
  HandshakeString	m_key;
  H1::ClientParser	m_parser;
  Zhttp::ConnectedInfo	m_info;
  bool			m_up = false;
  bool			m_down = false;
  bool			m_handshakeFailed = false;
};

struct H1ServerSession {
  template <typename Link>
  void connected(Link &link) { link.open_(); }
  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) { return link.handshake_(rx); }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

template <typename App, typename Profile>
class H1Server :
  public Zhttp::Server<H1Server<App, Profile>, Profile> {
  using Traits = typename Zhttp::ProfileTraits<Profile>::Transport;
  using Base = Zhttp::Server<H1Server, Profile>;

public:
  static_assert(!Zhttp::ProfileTraits<Profile>::Multiplexed);
  using Link = H1ServerLink<App, Profile>;
  using Config = typename Traits::Config;

  H1Server(App *app, ZiIP localIP, unsigned localPort) :
    m_app{app}, m_localIP{ZuMv(localIP)}, m_localPort{localPort} { }

  bool init(
      const Zhttp::EngineConfig &engine, const Config &config,
      Zws::Config wsConfig = {}) {
    m_config = wsConfig;
    return m_random.init() && Base::init(engine, config);
  }

  App *app() const { return m_app; }
  Ztls::Random &random() { return m_random; }
  const Zws::Config &wsConfig() const { return m_config; }
  ZiIP localIP() const { return m_localIP; }
  unsigned localPort() const { return m_localPort; }

  void listening(const ZiListenInfo &info) {
    H1_::listening(*m_app, info, 0);
  }
  void listening() { H1_::listening(*m_app, 0); }
  void listenFailed(bool transient) {
    H1_::listenFailed(*m_app, transient, 0);
  }
  void connected(Link &link, Zhttp::ConnectedInfo info) {
    link.info_(ZuMv(info));
  }
  void disconnected(Link &link, bool peer) {
    link.down_(peer);
  }

private:
  App			*m_app;
  Ztls::Random		m_random;
  Zws::Config		m_config;
  ZiIP			m_localIP;
  unsigned		m_localPort;
};

template <typename App, typename Profile>
class H1ServerLink :
  public Zhttp::ServerLink<
    H1Server<App, Profile>, H1ServerLink<App, Profile>,
    Profile, H1ServerSession>,
  public Codec<
    H1ServerLink<App, Profile>, H1ServerLink<App, Profile>, true,
    Ztls::Random>,
  public AppLinkState<App> {
  using Engine = H1Server<App, Profile>;
  using Self = H1ServerLink<App, Profile>;
  using HTTPBase = Zhttp::ServerLink<
    Engine, Self, Profile, H1ServerSession>;
  using CodecBase =
    Codec<Self, Self, true, Ztls::Random>;
  using StateBase = AppLinkState<App>;

public:
  using RxLayer = typename CodecBase::RxLayer;
  using HTTPBase::up_;
  using HTTPBase::process;
  using CodecBase::txStream;
  StateBase &state() { return *this; }
  const StateBase &state() const { return *this; }

  H1ServerLink(Engine *engine, const ZiCxnInfo &ci) :
    HTTPBase{engine, ci},
    CodecBase{*this, engine->random(), engine->wsConfig()} { }

  void open_() {
    this->CodecBase::reopen_(*this);
    m_parser.reset();
    m_protocol.length(0);
    m_info = {};
    m_down = false;
    m_handshakeFailed = false;
    if (!this->streamEnable()) {
      this->disconnect();
      return;
    }
    this->CodecBase::opening_();
  }

  template <typename Rx>
  int handshake_(Rx &rx) {
    auto state = m_parser.process(rx);
    if (state == H1::ServerParser::State::Error) return reject_();
    if (state != H1::ServerParser::State::Complete)
      return m_parser.progressed();
    HandshakeString selected;
    if (!m_parser.valid() ||
	!H1_::accept(
	  *this->app()->app(), *this,
	  m_parser.host(), m_parser.target(), m_parser.protocols(),
	  selected, 0))
      return reject_();
    if (selected && !subprotocol(m_parser.protocols(), selected))
      return reject_();
    HandshakeString accept_;
    if (!accept(accept_, m_parser.key())) return reject_();
    m_protocol = selected;
    {
      auto tx = HTTPBase::txStream();
      H1::Response response{accept_, m_protocol};
      response.response(tx);
      tx.flush();
    }
    if (!this->streamAccept()) return -1;
    m_up = true;
    this->CodecBase::up_();
    H1_::connected(
      *this->app()->app(), *this, ZuMv(m_info), 0);
    return 1;
  }

  template <typename Rx>
  int process(Zhttp::Stream<H1ServerLink> stream, Rx &rx) {
    return CodecBase::process(ZuMv(stream), rx);
  }
  int message(RxLayer &rx) {
    return H1_::process(*this->app()->app(), *this, rx, 0);
  }
  void pong(ZuBSpan payload) {
    H1_::pong(*this->app()->app(), *this, payload, 0);
  }
  void closed(uint16_t code, ZuBSpan reason) {
    H1_::closed(*this->app()->app(), *this, code, reason, 0);
  }
  void error(Failure::T failure) {
    H1_::error(*this->app()->app(), *this, failure, 0);
  }
  ZuCSpan protocol() const { return m_protocol; }
  void info_(Zhttp::ConnectedInfo info) { m_info = ZuMv(info); }

  void down_(bool peer) {
    if (m_down) return;
    m_down = true;
    m_up = false;
    this->CodecBase::disable_();
    this->app()->rxRun([link = ZmMkRef(this), peer]() mutable {
      link->CodecBase::final_();
      auto app = link->app();
      app->txRun([link = ZuMv(link), peer]() mutable {
	auto app = link->app();
	app->rxRun([link = ZuMv(link), peer]() mutable {
	  auto app = link->app();
	  H1_::disconnected(*app->app(), *link, peer, 0);
	});
      });
    });
  }

private:
  int reject_() {
    if (!m_handshakeFailed) {
      m_handshakeFailed = true;
      H1_::error(
	*this->app()->app(), *this, Failure::Handshake, 0);
    }
    {
      auto tx = HTTPBase::txStream();
      H1::ErrorResponse response;
      response.response(tx);
      tx.flush();
    }
    this->CodecBase::disable_();
    auto app = this->app();
    app->txRun([link = ZmMkRef(this)]() mutable {
      auto app = link->app();
      app->rxRun([link = ZuMv(link)]() mutable {
	link->disconnect();
      });
    });
    return 0;
  }

  H1::ServerParser	m_parser;
  HandshakeString	m_protocol;
  Zhttp::ConnectedInfo	m_info;
  bool			m_up = false;
  bool			m_down = false;
  bool			m_handshakeFailed = false;
};

} // namespace Zws

#endif /* ZwsH1Engine_HH */
