//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Typed WebSocket handshakes over the concrete HTTP channel boundary.

#ifndef ZwsHub_HH
#define ZwsHub_HH

#ifndef ZwsLib_HH
#include <zlib/ZwsLib.hh>
#endif

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpServer.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZwsCodec.hh>
#include <zlib/ZwsH1.hh>
#include <zlib/ZwsExtended.hh>

namespace Zws {
namespace H1_ {

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().connected(ZuDeclVal<Link &>(),
    ZuMv(ZuDeclVal<Zhttp::ConnectedInfo &>())), void())>
void connected(
    App &app, Link &link, Zhttp::ConnectedInfo info, int)
{
  app.connected(link, ZuMv(info));
}
template <typename App, typename Link>
void connected(App &, Link &, Zhttp::ConnectedInfo, ...) { }

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().disconnected(ZuDeclVal<Link &>(),
    ZuDeclVal<bool &>()), void())>
void disconnected(App &app, Link &link, bool peer, int)
{
  app.disconnected(link, peer);
}
template <typename App, typename Link>
void disconnected(App &, Link &, bool, ...) { }

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().connectFailed(ZuDeclVal<Link &>(),
    ZuDeclVal<bool &>()), void())>
void connectFailed(App &app, Link &link, bool transient, int)
{
  app.connectFailed(link, transient);
}
template <typename App, typename Link>
void connectFailed(App &, Link &, bool, ...) { }

template <typename App, typename Link, typename Rx,
  typename = decltype(int(ZuDeclVal<App &>().process(ZuDeclVal<Link &>(), ZuDeclVal<Rx &>())))>
int process(App &app, Link &link, Rx &rx, int)
{
  return int(app.process(link, rx));
}
template <typename App, typename Link, typename Rx>
int process(App &, Link &, Rx &rx, ...) {
  return Zhttp::bodyDrain(rx) ? 1 : -1;
}

template <typename App, typename Link,
  typename = decltype(int(ZuDeclVal<App &>().messageStart(ZuDeclVal<Link &>(),
    ZuDeclVal<Opcode::T &>())))>
int messageStart(
    App &app, Link &link, Opcode::T opcode, int)
{
  return int(app.messageStart(link, opcode));
}
template <typename App, typename Link>
int messageStart(App &, Link &, Opcode::T, ...) { return 1; }

template <typename App, typename Link,
  typename = decltype(int(ZuDeclVal<App &>().messageEnd(ZuDeclVal<Link &>())))>
int messageEnd(App &app, Link &link, int)
{
  return int(app.messageEnd(link));
}
template <typename App, typename Link>
int messageEnd(App &, Link &, ...) { return 1; }

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().pong(ZuDeclVal<Link &>(), ZuDeclVal<ZuBSpan &>()),
    void())>
void pong(App &app, Link &link, ZuBSpan payload, int)
{
  app.pong(link, payload);
}
template <typename App, typename Link>
void pong(App &, Link &, ZuBSpan, ...) { }

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().closed(ZuDeclVal<Link &>(),
    ZuDeclVal<uint16_t &>(), ZuDeclVal<ZuBSpan &>()), void())>
void closed(
    App &app, Link &link, uint16_t code, ZuBSpan reason, int)
{
  app.closed(link, code, reason);
}
template <typename App, typename Link>
void closed(App &, Link &, uint16_t, ZuBSpan, ...) { }

template <typename App, typename Link,
  typename = decltype(ZuDeclVal<App &>().error(ZuDeclVal<Link &>(),
    ZuDeclVal<Failure::T &>()), void())>
void error(App &app, Link &link, Failure::T failure, int)
{
  app.error(link, failure);
}
template <typename App, typename Link>
void error(App &, Link &, Failure::T, ...) { }

template <typename App, typename Link,
  typename = decltype(bool(ZuDeclVal<App &>().accept(ZuDeclVal<Link &>(),
    ZuDeclVal<ZuBSpan &>(), ZuDeclVal<ZuBSpan &>(), ZuDeclVal<ZuBSpan &>(),
    ZuDeclVal<HandshakeString &>())))>
bool accept(
    App &app, Link &link, ZuBSpan host, ZuBSpan target,
    ZuBSpan offered, HandshakeString &selected, int)
{
  return bool(app.accept(link, host, target, offered, selected));
}
template <typename App, typename Link>
bool accept(
    App &, Link &, ZuBSpan, ZuBSpan, ZuBSpan,
    HandshakeString &, ...)
{
  return true;
}

template <typename App,
  typename = decltype(ZuDeclVal<App &>().listening(ZuDeclVal<const ZiListenInfo &>()), void())>
void listening(App &app, const ZiListenInfo &info, int)
{
  app.listening(info);
}
template <typename App>
void listening(App &, const ZiListenInfo &, ...) { }

template <typename App,
  typename = decltype(ZuDeclVal<App &>().listening(), void())>
void listening(App &app, int)
{
  app.listening();
}
template <typename App>
void listening(App &, ...) { }

template <typename App,
  typename = decltype(ZuDeclVal<App &>().listenFailed(ZuDeclVal<bool &>()), void())>
void listenFailed(App &app, bool transient, int)
{
  app.listenFailed(transient);
}
template <typename App>
void listenFailed(App &, bool, ...) { }

} // namespace H1_

template <typename Profile> struct Binding {
  using ClientParser = Extended::ClientParser;
  using ServerParser = Extended::ServerParser;
  using Request = Extended::Request<Profile>;
  using Response = Extended::Response<Profile>;
  using Config = ZuIf<ZuIsSame<Profile, Zhttp::H3QUIC>{},
    Zhttp::QUICConfig, Zhttp::H2Config>;
};
template <> struct Binding<Zhttp::H1TCP> {
  using ClientParser = H1::ClientParser;
  using ServerParser = H1::ServerParser;
  using Request = H1::Request;
  using Response = H1::Response;
  using Config = Zhttp::TCPConfig;
};
template <> struct Binding<Zhttp::H1TLS> : Binding<Zhttp::H1TCP> {
  using Config = Zhttp::TLSConfig;
};

template <typename App, typename Profile> class Client;
template <typename App, typename Profile> class Server;
template <typename App, typename Profile> class ClientLink;
template <typename App, typename Profile> class ServerLink;

// WebSocket state retains a channel explicitly. Its codec and application
// callbacks never instantiate a transport or HTTP wire parser.
template <typename App, typename Profile, bool IsServer>
class LinkState : public ZmObject,
  public Codec<ZuIf<IsServer, ServerLink<App, Profile>, ClientLink<App, Profile>>,
    LinkState<App, Profile, IsServer>, IsServer, Ztls::Random>,
  public AppLinkState<App> {
  using Self = LinkState;
  using Actual = ZuIf<IsServer,
    ServerLink<App, Profile>, ClientLink<App, Profile>>;
  using Base = Codec<Actual, Self, IsServer, Ztls::Random>;
  using Hub = ZuIf<IsServer, Server<App, Profile>, Client<App, Profile>>;
public:
  using Rx = Base::Rx;
  enum { TLS = !ZuIsSame<Profile, Zhttp::H1TCP>{}, Multiplexed = Profile::Multiplexed };
  LinkState(Hub *hub) : Base{*this, hub->random(), hub->wsConfig()}, m_hub{hub} { }
  ~LinkState() = default;
  Hub *app() const { return m_hub; }
  Actual &actual() { return *static_cast<Actual *>(this); }
  AppLinkState<App> &state() { return *this; }
  const AppLinkState<App> &state() const { return *this; }
  Zhttp::Channel &channel() const { return *m_channel; }
  bool down() const { return m_down; }
  void disconnect() {
    if (m_channel) m_channel->disconnect();
  }
  const ZiIP &remoteIP() const { return channel().remoteIP(); }
  unsigned remotePort() const { return channel().remotePort(); }
  ZuBSpan protocol() const { return m_protocol; }
  void open_(Zhttp::ConnectedInfo info) {
    Base::reopen_(*this);
    m_down = false;
    m_handshakeFailed = false;
    m_info = ZuMv(info);
    Base::opening_();
  }
  void retain_(Zhttp::Channel *channel) { m_channel = channel; }
  bool bind_(Zhttp::Channel *channel) {
    retain_(channel);
    return channel->streamEnable();
  }
  bool establish_(ZuBSpan selected) {
    if (!channel().streamAccept({
      this,
      [](void *ctx, Zhttp::Channel *channel, Zhttp::Input &input) {
	return static_cast<Self *>(ctx)->Base::process(*channel, input);
      },
      [](void *ctx, Zhttp::Channel *channel) {
	static_cast<Self *>(ctx)->Base::peerEnd(*channel);
      },
      [](void *ctx, Zhttp::Channel *channel) {
	static_cast<Self *>(ctx)->Base::streamError(*channel);
      }})) return false;
    m_protocol = selected;
    Base::up_();
    H1_::connected(*m_hub->app(), actual(), ZuMv(m_info), 0);
    return true;
  }
  void failHandshake_(Failure::T failure = Failure::Handshake) {
    if (m_handshakeFailed) return;
    m_handshakeFailed = true;
    H1_::error(*m_hub->app(), actual(), failure, 0);
  }
  void down_(bool peer) {
    if (m_down) return;
    m_down = true;
    Base::disable_();
    m_hub->rxRun([link = ZmRef{&actual()}, peer]() mutable {
      link->Base::final_();
      auto hub = link->app();
      hub->txRun([link = ZuMv(link), peer]() mutable {
	auto hub = link->app();
	hub->rxRun([link = ZuMv(link), peer]() mutable {
	  H1_::disconnected(*link->app()->app(), *link, peer, 0);
	  link->m_channel.reset();
	});
      });
    });
  }
  int messageStart(Opcode::T opcode) {
    return H1_::messageStart(*m_hub->app(), actual(), opcode, 0);
  }
  int message(Rx &rx) { return H1_::process(*m_hub->app(), actual(), rx, 0); }
  int messageEnd() { return H1_::messageEnd(*m_hub->app(), actual(), 0); }
  void pong(ZuBSpan payload) { H1_::pong(*m_hub->app(), actual(), payload, 0); }
  void closed(uint16_t code, ZuBSpan reason) {
    H1_::closed(*m_hub->app(), actual(), code, reason, 0);
  }
  void error(Failure::T failure) { H1_::error(*m_hub->app(), actual(), failure, 0); }

private:
  friend class ClientLink<App, Profile>;
  friend class ServerLink<App, Profile>;

  Hub *m_hub;
  Zhttp::ChannelRef m_channel;
  HandshakeString m_protocol;
  Zhttp::ConnectedInfo m_info;
  bool m_down = false;
  bool m_handshakeFailed = false;
};

template <typename App, typename Profile>
class ClientLink : public ZmNHeap<ClientLink<App, Profile>>,
  public LinkState<App, Profile, false>, public Zhttp::ReqBuilder,
  public ZmPQueue_NodeExt<ClientLink<App, Profile>, ZmPQueue_Defaults::Levels> {
  using State = LinkState<App, Profile, false>;
  using Request = Binding<Profile>::Request;
  using Hub = Client<App, Profile>;
  friend inline ZuStringT<"Zws.Client.Link"> ZmNHeapID(ClientLink *);
public:
  using T = ClientLink;
  using Ext = ZmPQueue_NodeExt<ClientLink, ZmPQueue_Defaults::Levels>;
  using HdrCatalog = Request::HdrCatalog;
  using Zhttp::ReqBuilder::header;
  using Zhttp::ReqBuilder::protocol;
  using State::protocol;
  ClientLink(Hub *hub, const URI &uri, ZuBSpan protocol = {},
      ZuBSpan authorization = {}, ZuBSpan cookie = {}, ZuBSpan origin = {}) :
    State{hub}, m_uri{uri}, m_offered{protocol},
    m_authorization{authorization}, m_cookie{cookie}, m_origin{origin} { }
  ~ClientLink() { clearAuthorization(); }
  ClientLink &data() { return *this; }
  const ClientLink &data() const { return *this; }
  uint64_t key() const { return m_sequence; }
  void key(uint64_t sequence) { m_sequence = sequence; }
  void connect() { this->app()->send(0, ZmRef{this}); }
  void disconnect() {
    if (this->m_channel) State::disconnect();
    else this->app()->cancel(0, key());
  }
  void cancelled(const Zhttp::Result &) { this->down_(false); }
  void clearAuthorization() { if (m_authorization) ZuClear(m_authorization); }
  void connected(Zhttp::Channel *channel, const Zhttp::ConnectedInfo &info) {
    bool enabled = this->bind_(channel);
    this->open_(info);
    if constexpr (Profile::Multiplexed) {
      m_ready = enabled && channel->streamPeerCap();
      if (!m_ready) this->failHandshake_(Failure::Capability);
    } else {
      m_ready = enabled && nonce(this->app()->random(), m_key);
      if (!m_ready) this->failHandshake_();
    }
    if (!m_ready) this->disconnect();
  }
  void disconnected(bool peer) { this->down_(peer); }
  void connectFailed(bool transient) {
    H1_::connectFailed(*this->app()->app(), *this, transient, 0);
  }
  template <typename L> void operation(L &&l) {
    if (!m_ready) return;
    l(Profile::Multiplexed ? Zhttp::Method::CONNECT : Zhttp::Method::GET,
      [this](auto &&emit) { emit([this](auto &tx) { tx << m_uri.target; }); });
  }
  template <typename L> void host(L &&l) { l(m_uri.authority()); }
  template <typename L> void protocol(L &&l) {
    if constexpr (Profile::Multiplexed) l("websocket");
  }
  template <typename Key, typename L> void header(L &&l) {
    if constexpr (Key{}() == "sec-websocket-key") l(m_key);
    else if constexpr (Key{}() == "sec-websocket-version") l("13");
    else if constexpr (Key{}() == "sec-websocket-protocol") {
      if (m_offered) l(m_offered);
    }
    else if constexpr (Key{}() == "authorization") l(m_authorization);
    else if constexpr (Key{}() == "cookie") l(m_cookie);
    else if constexpr (Key{}() == "origin") l(m_origin);
  }
  ZuBSpan expectedKey() const { return m_key; }
  ZuBSpan offered() const { return m_offered; }
private:
  URI m_uri;
  HandshakeString m_offered;
  HandshakeString m_authorization;
  HandshakeString m_cookie;
  HandshakeString m_origin;
  HandshakeString m_key;
  uint64_t m_sequence = 0;
  bool m_ready = false;
};

template <typename App, typename Profile>
class ResponseParser : public Binding<Profile>::ClientParser {
  using Base = Binding<Profile>::ClientParser;
public:
  void init(ClientLink<App, Profile> &link) {
    m_link = &link;
    if constexpr (Profile::Multiplexed) Base::expected(link.offered());
    else Base::expected(link.expectedKey(), link.offered());
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T type, uint64_t length) {
    Base::bodyInfo(channel, type, length);
    if (!Base::valid()) { m_link->failHandshake_(); return false; }
    return m_link->establish_(Base::selected());
  }
  void complete(Zhttp::Channel *, bool ok) {
    if (!ok && !m_link->down()) m_link->failHandshake_();
  }
private:
  ClientLink<App, Profile> *m_link = nullptr;
};

template <typename App, typename Profile> class Pool;
template <typename App, typename Profile>
using RequestQ = ZmPQueue<ClientLink<App, Profile>,
  ZmPQueueOverlap<false, ZmPQueueNode<ClientLink<App, Profile>>>,
  ClientLink<App, Profile>>;
template <typename App, typename Profile>
using TxQ = ZmPQTx<Pool<App, Profile>, RequestQ<App, Profile>, ZmPQTxOrdered<false>>;
template <typename App, typename Profile>
class Pool : public ZmNHeap<Pool<App, Profile>>,
  public Zhttp::Pool<Client<App, Profile>, TxQ<App, Profile>,
    ResponseParser<App, Profile>, Zhttp::Profiles<Profile>> {
  using Base = Zhttp::Pool<Client<App, Profile>, TxQ<App, Profile>,
    ResponseParser<App, Profile>, Zhttp::Profiles<Profile>>;
  friend inline ZuStringT<"Zws.Client.Pool"> ZmNHeapID(Pool *);
public:
  Pool(Client<App, Profile> *client) : Base{client} { }
  RequestQ<App, Profile> *txQueue() { return &m_queue; }
  void send(ZmRef<ClientLink<App, Profile>> link) {
    this->txRun([this, link = ZuMv(link)]() mutable {
      link->key(m_sequence++);
      Base::send(ZuMv(link));
    });
  }
  void archive_(ClientLink<App, Profile> *) { }
  ZmRef<ClientLink<App, Profile>> retrieve_(uint64_t, uint64_t) { return {}; }
private:
  RequestQ<App, Profile> m_queue;
  uint64_t m_sequence = 0;
};

template <typename App, typename Profile>
class Client : public Zhttp::Client<Client<App, Profile>, Pool<App, Profile>> {
  using Base = Zhttp::Client<Client, Pool<App, Profile>>;
public:
  using Link = ClientLink<App, Profile>;
  using Config = Binding<Profile>::Config;
  Client(App *app) : m_app{app} { }
  bool init(const Zhttp::HubConfig &hub, Config profile,
      Zhttp::Destination destination, Zws::Config ws = {},
      const Zhttp::Config &limits = {}) {
    m_config = ws;
    Zhttp::TCPConfig tcp;
    Zhttp::H2Config tls;
    Zhttp::QUICConfig quic;
    if constexpr (ZuIsSame<Profile, Zhttp::H1TCP>{}) tcp = ZuMv(profile);
    else if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{}) {
      profile.extendedConnect(true);
      quic = ZuMv(profile);
    } else if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{}) {
      static_cast<Zhttp::TLSConfig &>(tls) = ZuMv(profile);
    } else {
      profile.extendedConnect(true);
      tls = ZuMv(profile);
    }
    return m_random.init() && Base::init(hub, 1, limits, tcp, tls, quic) &&
      Base::pool(0, ZuMv(destination));
  }
  using Base::stop;
  bool stop() {
    return ZmBlock<bool>{}([this](auto wake) { Base::stop(ZuMv(wake)); });
  }
  App *app() const { return m_app; }
  Ztls::Random &random() { return m_random; }
  const Zws::Config &wsConfig() const { return m_config; }
private:
  App *m_app;
  Ztls::Random m_random;
  Zws::Config m_config;
};

template <typename Profile>
struct Response : public ZmObject, public Zhttp::ResBuilder {
  using Success = Binding<Profile>::Response;
  using HdrCatalog = Success::HdrCatalog;
  using Zhttp::ResBuilder::header;
  Success value;
  bool accepted;

  Response(ZuBSpan accept, ZuBSpan protocol, bool good) :
    value{make(accept, protocol)}, accepted{good} { }
  static Success make(ZuBSpan accept, ZuBSpan protocol) {
    if constexpr (Profile::Multiplexed) return Success{protocol};
    else return Success{accept, protocol};
  }
  unsigned status() const { return accepted ? value.status() : 400; }
  bool disconnect() const { return !Profile::Multiplexed && !accepted; }
  bool streamResponse() const { return accepted; }
  template <typename Key, typename Value, typename L> void header(L &&l) {
    if (accepted) l();
  }
  template <typename Key, typename L> void header(L &&l) {
    if (accepted) value.template header<Key>(ZuFwd<L>(l));
  }
};

template <typename App, typename Profile>
class ServerLink : public ZmNHeap<ServerLink<App, Profile>>,
  public LinkState<App, Profile, true> {
  using State = LinkState<App, Profile, true>;
  friend inline ZuStringT<"Zws.Server.Link"> ZmNHeapID(ServerLink *);
public:
  using State::State;
  ZuBSpan authorization() const { return m_authorization; }
  ZuBSpan cookie() const { return m_cookie; }
  ZuBSpan origin() const { return m_origin; }
  void credentials(HandshakeString authorization, HandshakeString cookie,
      HandshakeString origin) {
    m_authorization = ZuMv(authorization);
    m_cookie = ZuMv(cookie);
    m_origin = ZuMv(origin);
  }
private:
  HandshakeString m_authorization;
  HandshakeString m_cookie;
  HandshakeString m_origin;
};

template <typename App, typename Profile>
class RequestParser : public Binding<Profile>::ServerParser {
  using Base = Binding<Profile>::ServerParser;
  using Hub = Server<App, Profile>;
  using Link = ServerLink<App, Profile>;
public:
  void initOwner(Hub &hub) { m_link = new Link{&hub}; }
  void connectedInfo(Hub &, Zhttp::Channel *channel, const Zhttp::ConnectedInfo &info) {
    m_link->retain_(channel);
    m_link->open_(info);
    if constexpr (!Profile::Multiplexed)
      if (!channel->streamEnable()) m_link->disconnect();
  }
  void init(Hub &) { Base::reset(); }
  void disconnected(Zhttp::Channel *channel, bool peer) {
    m_link->retain_(channel);
    m_link->down_(peer);
  }
  bool bodyInfo(Zhttp::Channel *channel, Zhttp::BodyType::T type, uint64_t length) {
    Base::bodyInfo(channel, type, length);
    bool enabled = true;
    if constexpr (Profile::Multiplexed) enabled = channel->streamEnable();
    if constexpr (!Profile::Multiplexed)
      m_link->credentials(ZuMv(this->m_authorization),
	ZuMv(this->m_cookie), ZuMv(this->m_origin));
    HandshakeString selected;
    bool accepted = enabled && Base::valid() &&
      H1_::accept(*m_link->app()->app(), *m_link,
	Base::host(), Base::target(), Base::protocols(), selected, 0);
    if (accepted && selected && !subprotocol(Base::protocols(), selected)) accepted = false;
    HandshakeString key;
    if constexpr (!Profile::Multiplexed)
      if (accepted && !accept(key, Base::key())) accepted = false;
    if (!accepted) m_link->failHandshake_();
    using Node = typename Zhttp::ResponseQ<Response<Profile>, ZuStringT<"Zws.Response">>::Node;
    channel->send(ZmRef<Node>{new Node{key, selected, accepted}});
    if (!accepted) return true;
    return m_link->establish_(selected);
  }
private:
  ZmRef<Link> m_link;
};

template <typename App, typename Profile>
class Server : public Zhttp::Server<Server<App, Profile>, RequestParser<App, Profile>,
  Zhttp::ResponseQ<Response<Profile>, ZuStringT<"Zws.Response">>, Zhttp::Profiles<Profile>> {
  using Base = Zhttp::Server<Server, RequestParser<App, Profile>,
    Zhttp::ResponseQ<Response<Profile>, ZuStringT<"Zws.Response">>, Zhttp::Profiles<Profile>>;
public:
  using Link = ServerLink<App, Profile>;
  using Config = Binding<Profile>::Config;
  using Base::connected;
  using Base::disconnected;
  void connected(Zhttp::Transport::T) { }
  void disconnected(Zhttp::Transport::T) { }
  Server(App *app, ZiIP ip, unsigned port) : m_app{app}, m_ip{ZuMv(ip)}, m_port{port} { }
  bool init(const Zhttp::HubConfig &hub, Config profile, Zws::Config ws = {}) {
    m_config = ws;
    Zhttp::ServerConfig config;
    config.localIP(m_ip).port(m_port);
    if constexpr (ZuIsSame<Profile, Zhttp::H1TCP>{}) config.tcp(ZuMv(profile));
    else if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{}) {
      profile.extendedConnect(true);
      config.quic(ZuMv(profile));
    } else if constexpr (ZuIsSame<Profile, Zhttp::H1TLS>{}) {
      Zhttp::H2Config tls;
      static_cast<Zhttp::TLSConfig &>(tls) = ZuMv(profile);
      config.tls(ZuMv(tls));
    } else {
      profile.extendedConnect(true);
      config.tls(ZuMv(profile));
    }
    return m_random.init() && Base::init(hub, ZuMv(config));
  }
  App *app() const { return m_app; }
  Ztls::Random &random() { return m_random; }
  const Zws::Config &wsConfig() const { return m_config; }
  void listening(Zhttp::Transport::T, unsigned port) {
    ZiListenInfo info;
    info.ip = m_ip;
    info.port = port;
    if constexpr (ZuIsSame<Profile, Zhttp::H3QUIC>{})
      H1_::listening(*m_app, 0);
    else
      H1_::listening(*m_app, info, 0);
  }
  void listenFailed(Zhttp::Transport::T, bool transient) {
    H1_::listenFailed(*m_app, transient, 0);
  }
private:
  App *m_app;
  Ztls::Random m_random;
  Zws::Config m_config;
  ZiIP m_ip;
  unsigned m_port;
};

} // namespace Zws
#endif /* ZwsHub_HH */
