//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Generic WebSocket hubs over normalized Zhttp profiles

#ifndef ZwsHub_HH
#define ZwsHub_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>

#include <zlib/ZwsExtended.hh>
#include <zlib/ZwsH1Hub.hh>

namespace Zws {

template <typename Profile> struct ExtendedConfig;
template <> struct ExtendedConfig<Zhttp::H2TLS> {
  using T = Zhttp::H2Config;
};
template <> struct ExtendedConfig<Zhttp::H3QUIC> {
  using T = Zhttp::QUICConfig;
};

template <typename App, typename Profile> class ExtendedClientLink;
template <typename App, typename Profile> class ExtendedServerLink;

template <typename App, typename Profile>
class ExtendedClient :
  public Zhttp::ClientHub<ExtendedClient<App, Profile>, Profile> {
  using Base = Zhttp::ClientHub<ExtendedClient, Profile>;

public:
  ZuAssert(Zhttp::ProfileTraits<Profile>::Multiplexed);
  using Link = ExtendedClientLink<App, Profile>;
  using Config = typename ExtendedConfig<Profile>::T;

  ExtendedClient(App *app) : m_app{app} { }

  bool init(
      const Zhttp::HubConfig &hub, Config config,
      Zws::Config wsConfig = {}) {
    m_config = wsConfig;
    config.extendedConnect(true);
    return m_random.init() && Base::init(hub, config);
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
class ExtendedClientLink :
  public Zhttp::ClientLink<
    ExtendedClient<App, Profile>, ExtendedClientLink<App, Profile>, Profile>,
  public Codec<
    ExtendedClientLink<App, Profile>,
    ExtendedClientLink<App, Profile>, false, Ztls::Random>,
  public AppLinkState<App> {
  using Hub = ExtendedClient<App, Profile>;
  using HTTPBase =
    Zhttp::ClientLink<Hub, ExtendedClientLink, Profile>;
  using CodecBase =
    Codec<ExtendedClientLink, ExtendedClientLink, false, Ztls::Random>;
  using StateBase = AppLinkState<App>;
  using Parser = Extended::ClientParser<ExtendedClientLink, Profile>;

public:
  using Rx = typename CodecBase::Rx;
  using CodecBase::txStream;
  StateBase &state() { return *this; }
  const StateBase &state() const { return *this; }
  unsigned id() const { return 0; }

  ExtendedClientLink(
      Hub *hub, const URI &uri, ZuBSpan protocol = {}) :
    HTTPBase{hub},
    CodecBase{*this, hub->random(), hub->wsConfig()},
    m_uri{uri}, m_protocol{protocol} { }

  auto txStream() { return HTTPBase::txStream(); }

  void open_(Zhttp::ConnectedInfo info) {
    this->CodecBase::reopen_(*this);
    m_down = false;
    m_handshakeFailed = false;
    m_info = ZuMv(info);
    if (!Zhttp::Stream{*this}.peerCap()) {
      H1_::error(*this->app()->app(), *this, Failure::Capability, 0);
      this->disconnect();
      return;
    }
    m_parser.bind(*this, m_protocol);
    m_bound = true;
    this->CodecBase::opening_();
    this->app()->txRun([this]() {
      Extended::Request<Profile> request{m_uri, m_protocol};
      auto tx = this->transmit_(request);
      if (!request.begin(tx)) this->app()->rxRun([this]() {
	H1_::error(*this->app()->app(), *this, Failure::Handshake, 0);
	this->disconnect();
      });
    });
  }

  template <typename Rx>
  int handshake_(Rx &rx) {
    auto state = this->receive(m_parser, rx);
    if (!m_parser.established() && (m_parser.invalid() ||
	state == Extended::ClientParser<
	  ExtendedClientLink, Profile>::State::Error)) {
      failHandshake_();
      return -1;
    }
    return 0;
  }
  void established_() {
    if (m_up) return;
    m_up = true;
    this->CodecBase::up_();
    H1_::connected(
      *this->app()->app(), *this, ZuMv(m_info), 0);
  }
  void down_(bool peer) {
    if (m_down) return;
    m_down = true;
    m_up = false;
    if (m_bound) {
      m_parser.disable_();
    }
    this->CodecBase::disable_();
    this->app()->rxRun([link = ZmRef(this), peer]() mutable {
      if (link->m_bound) {
	link->m_parser.final_();
	link->m_bound = false;
      }
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

  template <typename Rx>
  int process(Zhttp::Stream<ExtendedClientLink> stream, Rx &rx) {
    return CodecBase::process(ZuMv(stream), rx);
  }
  int messageStart(Opcode::T opcode) {
    return H1_::messageStart(
      *this->app()->app(), *this, opcode, 0);
  }
  int message(Rx &rx) {
    return H1_::process(*this->app()->app(), *this, rx, 0);
  }
  int messageEnd() {
    return H1_::messageEnd(*this->app()->app(), *this, 0);
  }
  void peerEnd(Zhttp::Stream<ExtendedClientLink> stream) {
    CodecBase::peerEnd(ZuMv(stream));
  }
  void error(Zhttp::Stream<ExtendedClientLink> stream) {
    CodecBase::streamError(ZuMv(stream));
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
  ZuBSpan protocol() const { return m_parser.selected(); }

private:
  void failHandshake_() {
    if (m_handshakeFailed) return;
    m_handshakeFailed = true;
    H1_::error(
      *this->app()->app(), *this, Failure::Handshake, 0);
  }

  Parser					m_parser;
  URI							m_uri;
  HandshakeString					m_protocol;
  Zhttp::ConnectedInfo					m_info;
  bool							m_bound = false;
  bool							m_up = false;
  bool							m_down = false;
  bool							m_handshakeFailed = false;
};

template <typename Link>
struct ExtendedServerSession {
  template <typename Link_>
  void connected(Link_ &link) { link.open_(); }
  template <typename Link_>
  void disconnected(Link_ &, bool) { }
  template <typename Link_, typename Rx>
  int process(Link_ &link, Rx &rx) { return link.handshake_(rx); }
};

template <typename App, typename Profile>
class ExtendedServer :
  public Zhttp::ProtocolServer<ExtendedServer<App, Profile>, Profile> {
  using Base = Zhttp::ProtocolServer<ExtendedServer, Profile>;

public:
  ZuAssert(Zhttp::ProfileTraits<Profile>::Multiplexed);
  using Link = ExtendedServerLink<App, Profile>;
  using Config = typename ExtendedConfig<Profile>::T;

  ExtendedServer(App *app, ZiIP localIP, unsigned localPort) :
    m_app{app}, m_localIP{ZuMv(localIP)}, m_localPort{localPort} { }

  bool init(
      const Zhttp::HubConfig &hub, Config config,
      Zws::Config wsConfig = {}) {
    m_config = wsConfig;
    config.extendedConnect(true);
    return m_random.init() && Base::init(hub, config);
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
  bool admit(const auto &) { return true; }
  void release() { }
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
class ExtendedServerLink :
  public Zhttp::ServerLink<
    ExtendedServer<App, Profile>, ExtendedServerLink<App, Profile>,
    Profile, ExtendedServerSession<ExtendedServerLink<App, Profile>>>,
  public Codec<
    ExtendedServerLink<App, Profile>,
    ExtendedServerLink<App, Profile>, true, Ztls::Random>,
  public AppLinkState<App> {
  using Hub = ExtendedServer<App, Profile>;
  using Session = ExtendedServerSession<ExtendedServerLink>;
  using HTTPBase =
    Zhttp::ServerLink<Hub, ExtendedServerLink, Profile, Session>;
  using CodecBase =
    Codec<ExtendedServerLink, ExtendedServerLink, true, Ztls::Random>;
  using StateBase = AppLinkState<App>;
  using Parser = Extended::ServerParser<ExtendedServerLink, Profile>;

public:
  using Rx = typename CodecBase::Rx;
  using CodecBase::txStream;
  StateBase &state() { return *this; }
  const StateBase &state() const { return *this; }

  template <typename ...Args>
  ExtendedServerLink(Hub *hub, Args &&...args) :
    HTTPBase{hub, ZuFwd<Args>(args)...},
    CodecBase{*this, hub->random(), hub->wsConfig()} { }

  auto txStream() { return HTTPBase::txStream(); }

  void open_() {
    this->CodecBase::reopen_(*this);
    m_protocol.length(0);
    m_info = {};
    m_down = false;
    m_handshakeFailed = false;
    m_parser.bind(*this);
    m_bound = true;
    this->CodecBase::opening_();
  }
  void down_(bool peer) {
    if (m_down) return;
    m_down = true;
    m_up = false;
    if (m_bound) {
      m_parser.disable_();
    }
    this->CodecBase::disable_();
    this->app()->rxRun([link = ZmRef(this), peer]() mutable {
      if (link->m_bound) {
	link->m_parser.final_();
	link->m_bound = false;
      }
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

  template <typename Rx>
  int handshake_(Rx &rx) {
    auto state = this->receive(m_parser, rx);
    if (!m_parser.established() && (m_parser.invalid() ||
	state == Extended::ServerParser<
	  ExtendedServerLink, Profile>::State::Error))
      return -1;
    return 0;
  }
  bool accept_(
      ZuBSpan host, ZuBSpan target, ZuBSpan offered,
      HandshakeString &selected) {
    return H1_::accept(
      *this->app()->app(), *this,
      host, target, offered, selected, 0);
  }
  void respond_(ZuBSpan selected) {
    m_protocol = selected;
    this->app()->txRun([this]() {
      Extended::Response<Profile> response{m_protocol};
      auto tx = this->transmit_(response);
      response.begin(tx);
    });
  }
  void reject_() {
    if (!m_handshakeFailed) {
      m_handshakeFailed = true;
      H1_::error(
	*this->app()->app(), *this, Failure::Handshake, 0);
    }
    this->app()->txRun([this]() {
      Extended::ErrorResponse<Profile> response;
      auto tx = this->transmit_(response);
      response.begin(tx);
      this->finish();
    });
  }
  void established_() {
    if (m_up) return;
    m_up = true;
    this->CodecBase::up_();
    H1_::connected(
      *this->app()->app(), *this, ZuMv(m_info), 0);
  }

  template <typename Rx>
  int process(Zhttp::Stream<ExtendedServerLink> stream, Rx &rx) {
    return CodecBase::process(ZuMv(stream), rx);
  }
  int messageStart(Opcode::T opcode) {
    return H1_::messageStart(
      *this->app()->app(), *this, opcode, 0);
  }
  int message(Rx &rx) {
    return H1_::process(*this->app()->app(), *this, rx, 0);
  }
  int messageEnd() {
    return H1_::messageEnd(*this->app()->app(), *this, 0);
  }
  void peerEnd(Zhttp::Stream<ExtendedServerLink> stream) {
    CodecBase::peerEnd(ZuMv(stream));
  }
  void error(Zhttp::Stream<ExtendedServerLink> stream) {
    CodecBase::streamError(ZuMv(stream));
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
  ZuBSpan protocol() const { return m_protocol; }
  void info_(Zhttp::ConnectedInfo info) { m_info = ZuMv(info); }

private:
  Parser					m_parser;
  HandshakeString					m_protocol;
  Zhttp::ConnectedInfo					m_info;
  bool							m_bound = false;
  bool							m_up = false;
  bool							m_down = false;
  bool							m_handshakeFailed = false;
};

template <
  typename App, typename Profile,
  bool Multiplexed = Zhttp::ProfileTraits<Profile>::Multiplexed>
class Client;

template <typename App, typename Profile>
class Client<App, Profile, false> : public H1Client<App, Profile> {
  using Base = H1Client<App, Profile>;
public:
  using Base::Base;
};

template <typename App, typename Profile>
class Client<App, Profile, true> : public ExtendedClient<App, Profile> {
  using Base = ExtendedClient<App, Profile>;
public:
  using Base::Base;
};

template <
  typename App, typename Profile,
  bool Multiplexed = Zhttp::ProfileTraits<Profile>::Multiplexed>
class Server;

template <typename App, typename Profile>
class Server<App, Profile, false> : public H1Server<App, Profile> {
  using Base = H1Server<App, Profile>;
public:
  using Base::Base;
};

template <typename App, typename Profile>
class Server<App, Profile, true> : public ExtendedServer<App, Profile> {
  using Base = ExtendedServer<App, Profile>;
public:
  using Base::Base;
};

} // namespace Zws

#endif /* ZwsHub_HH */
