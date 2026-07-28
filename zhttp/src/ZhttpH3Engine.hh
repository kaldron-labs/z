//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - private HTTP/3 transport/session adapters

#ifndef ZhttpH3Engine_HH
#define ZhttpH3Engine_HH

#ifndef Zhttp_HH
#define Zhttp_CORE_ONLY
#include <zlib/Zhttp.hh>
#undef Zhttp_CORE_ONLY
#endif

#include <zlib/ZmContext.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmRandom.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpH3Session.hh>
#include <zlib/ZhttpServer.hh>

namespace Zhttp {

namespace H3_ {

template <typename Diag>
void printDiag(const Diag &diag) {
  ZiLOG(Info, "Zhttp", ([
    datagramsRx = diag.rx.datagramsRx,
    datagramsTx = diag.tx.datagramsTx,
    bytesRx = diag.rx.bytesRx,
    bytesTx = diag.tx.bytesTx,
    txBackPressure = diag.tx.txBackPressure,
    failures = diag.failures()
  ](auto &s) {
    s << "H3 diag datagramsRx=" << datagramsRx <<
      " datagramsTx=" << datagramsTx <<
      " bytesRx=" << bytesRx <<
      " bytesTx=" << bytesTx <<
      " txBackPressure=" << txBackPressure <<
      " failures=" << failures;
  }));
}

template <typename Impl>
class Faults {
public:
  void faults(const QUICConfig &config) {
#ifdef ZiMultiplex_FILTER
    m_rxDrop = config.rxDrop();
    m_txDrop = config.txDrop();
    auto impl = static_cast<Impl *>(this);
    auto mx = impl->mx();
    if (m_rxDrop)
      mx->rxFilter(FilterFn{this, [](Faults *faults,
	  ZiConnection *cxn, uint8_t *, unsigned) {
	if (ZuUnlikely(!cxn->info().options.udp())) return false;
	return faults->m_rng.rand() < faults->m_rxDrop;
      }});
    if (m_txDrop)
      mx->txFilter(FilterFn{this, [](Faults *faults,
	  ZiConnection *cxn, uint8_t *, unsigned) {
	if (ZuUnlikely(!cxn->info().options.udp())) return false;
	return faults->m_rng.rand() < faults->m_txDrop;
      }});
#else
    (void)config;
#endif
  }
  void clearFaults() {
#ifdef ZiMultiplex_FILTER
    auto impl = static_cast<Impl *>(this);
    auto mx = impl->mx();
    if (!mx) return;
    if (m_rxDrop) mx->rxFilter({});
    if (m_txDrop) mx->txFilter({});
#endif
  }
  void printDiag() {
#ifdef Zquic_DEBUG
    static_cast<Impl *>(this)->endpointDiag([](const auto &diag) {
      H3_::printDiag(diag);
    });
#endif
  }

private:
#ifdef ZiMultiplex_FILTER
  double	m_rxDrop = 0;
  double	m_txDrop = 0;
  ZmRandom	m_rng;
#endif
};

template <typename App>
class ClientEngine;
template <typename App, typename Logical>
struct ClientSession;
template <typename App, typename Logical>
struct ClientStream;
template <typename App>
class ServerEngine;

struct ClientSessionSlot {
  using CloseFn = void (*)(void *);
  using DownFn = bool (*)(void *);
  using PrintDiagFn = void (*)(void *);

  ZmContext		owner;
  CloseFn		close = nullptr;
  DownFn		down = nullptr;
  PrintDiagFn		printDiag = nullptr;
  Zquic::Host		host;
  ZiIP			remote;
  uint16_t		port = 0;
};

// App is incomplete while its CRTP base is instantiated.  ZmContext pins the
// protocol-private session; the two function pointers are control-plane only.
template <typename App>
class ClientEngine :
  public Zquic::Client<ClientEngine<App>>,
  public Faults<ClientEngine<App>> {
public:
  using StopFn =
    ZmFn<void(bool), ZmFnHeapID<"Zhttp.H3.ClientStop">>;
  using StopFns =
    ZtArray<StopFn, ZtArrayHeapID<"Zhttp.H3.ClientStopFns">>;
  using Sessions =
    ZtArray<ClientSessionSlot,
      ZtArrayHeapID<"Zhttp.H3.ClientSessions">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port) {
    connect_(link, ZuMv(host), port, {});
  }
  template <typename Link>
  void connect(Link *link, Zquic::Host host, uint16_t port, ZiIP remote) {
    connect_(link, ZuMv(host), port, ZuMv(remote));
  }

private:
  template <typename Link>
  void connect_(
    Link *link, Zquic::Host host, uint16_t port, ZiIP remote) {
    using Session = ClientSession<App, Link>;
    ZmRef<Link> logical = ZmMkRef(link);
    this->rxInvoke([
      this, logical = ZuMv(logical), host = ZuMv(host),
      remote = ZuMv(remote), port
    ]() mutable {
      ZmRef<Session> session;
      for (unsigned i = 0; i < sessions.length(); ++i) {
	auto &slot = sessions[i];
	auto candidate = slot.owner.object<Session>();
	if (!candidate->down && slot.host == host &&
	    slot.remote == remote && slot.port == port) {
	  session = candidate;
	  break;
	}
      }
      if (!session) {
	session = new Session{this, host, port};
	sessions.push(ClientSessionSlot{
	  .owner = session,
	  .close = [](void *ptr) {
		    // Engine shutdown must not wait behind an in-flight migration;
		    // abort guarantees endpointDown() and deterministic draining.
		    static_cast<Session *>(ptr)->abort();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Session *>(ptr)->down;
	  },
	  .printDiag = [](void *ptr) {
#ifdef Zquic_DEBUG
	    static_cast<Session *>(ptr)->endpointDiag([](const auto &diag) {
	      H3_::printDiag(diag);
	    });
#else
	    (void)ptr;
#endif
	  },
	  .host = host,
	  .remote = remote,
	  .port = port
	});
	session->add(ZuMv(logical));
	session->connect(ZuMv(host), port, ZuMv(remote));
	return;
      }
      session->add(ZuMv(logical));
    });
  }

public:
  unsigned reconnFreq() const { return 0; }

  void printDiag() {
    for (unsigned i = 0; i < sessions.length(); ++i) {
      auto &slot = sessions[i];
      slot.printDiag(slot.owner.object<void>());
    }
  }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      stopRx_(ZuMv(fn));
    });
  }
  void sessionDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopBase_();
  }
  void final() {
    this->clearFaults();
    sessions.length(0);
    Zquic::Client<ClientEngine>::final();
  }

private:
  Sessions	sessions;

  void stopRx_(StopFn done) {
    m_stopFns.push(ZuMv(done));
    if (m_stopping) return;
    m_stopping = true;
    m_stopPending = 0;
    for (unsigned i = 0; i < sessions.length(); ++i) {
      auto &slot = sessions[i];
      if (slot.down(slot.owner.object<void>())) continue;
      ++m_stopPending;
    }
    if (!m_stopPending) {
      stopBase_();
      return;
    }
    for (unsigned i = 0; i < sessions.length(); ++i) {
      auto &slot = sessions[i];
      if (slot.down(slot.owner.object<void>())) continue;
      slot.close(slot.owner.object<void>());
    }
  }

  void stopBase_() {
    Zquic::Client<ClientEngine>::stop(
      [this](bool ok) {
	this->rxRun([this, ok]() { stopped_(ok); });
      });
  }

  void stopped_(bool ok) {
    auto fns = ZuMv(m_stopFns);
    m_stopFns.init_();
    m_stopPending = 0;
    m_stopping = false;
    for (auto &fn: fns) {
      fn(ok);
      fn = {};
    }
  }

  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App, typename Logical>
struct ClientStream :
  public Zquic::CliStream<
    ClientSession<App, Logical>, ClientStream<App, Logical>>,
  public H3::CxnStream<ClientStream<App, Logical>,
    H3::Cxn<ClientSession<App, Logical>,
      ZmRef<ClientStream<App, Logical>>>> {
  using Session = ClientSession<App, Logical>;
  using Base = Zquic::CliStream<Session, ClientStream>;
  using H3Cxn = H3::Cxn<Session, ZmRef<ClientStream>>;
  using CxnStream = H3::CxnStream<ClientStream, H3Cxn>;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    return logical ? logical->process_(rx) : -1;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

  ZmRef<Logical>	logical;
};

template <typename App, typename Logical>
struct ClientSession :
  public Zquic::CliLink<ClientEngine<App>, ClientSession<App, Logical>,
    ClientStream<App, Logical>> {
  using Engine = ClientEngine<App>;
  using Stream = ClientStream<App, Logical>;
  using Base = Zquic::CliLink<Engine, ClientSession, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<ClientSession, StreamRef>;
  using Pending =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientPending">>;
  using Waiting =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientWaiting">>;
  using Streams =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ClientStreams">>;
  using Base::Base;

  ClientSession(Engine *app, Zquic::Host host_, uint16_t port_) :
    Base{app}, host{ZuMv(host_)}, port{port_} { }

  void add(ZmRef<Logical> logical) {
    logical->session(ZmMkRef(this));
    if (!ready) {
      pending.push(ZuMv(logical));
      return;
    }
    open(ZuMv(logical));
  }
  void open(ZmRef<Logical> logical) {
    waiting.push(ZuMv(logical));
    auto session = ZmMkRef(this);
    this->app()->txRun([session]() mutable {
      auto stream = session->stream(Zquic::StreamType::Duplex);
      if (!stream) return;
      session->app()->rxRun([
	session = ZuMv(session), stream = ZuMv(stream)
      ]() mutable {
	session->streamed(ZuMv(stream));
      });
    });
  }
  void opened(ZmRef<Logical> logical, StreamRef stream) {
    stream->logical = logical;
    streams.push(stream);
    logical->stream(stream.ptr());
    logical->connected_(ProfileTraits<H3QUIC>::apply({
      .alpn = "h3",
      .version = Zquic::Version1,
      .transport = Transport::QUIC,
      .secure = true
    }));
  }

  void connected(Zquic::Connected info) {
    if (info.version != Zquic::Version1 || info.alpn != "h3" ||
	!h3.openLocal(
	  *this, H3::Params{},
	  this->app()->user()->quicConfig().extendedConnect())) {
      connectFailed(false);
      return;
    }
    if (h3.localExtendedConnect) return;
    h3Ready();
  }
  void h3Ready() {
    if (ready) return;
    ready = true;
    unsigned n = pending.length();
    for (unsigned i = 0; i < n; ++i)
      open(ZuMv(pending[i]));
    pending.length(0);
  }
  void disconnected(bool peer) { closePeer = peer; }
  void migrationPromoted(const Zquic::MigrationResult &) {
    migrationComplete_();
  }
  void migrationFailed(const Zquic::MigrationResult &) {
    migrationComplete_();
  }
  void endpointDown() {
    this->app()->rxRun([session = ZmMkRef(this)]() mutable {
      session->endpointDown_();
    });
  }
  void endpointDown_() {
    down = true;
    for (unsigned i = 0; i < streams.length(); ++i) {
      auto logical = ZuMv(streams[i]->logical);
      if (logical) logical->disconnected_(closePeer);
    }
    streams.length(0);
    for (unsigned i = 0; i < pending.length(); ++i) {
      auto logical = ZuMv(pending[i]);
      if (logical) logical->connectFailed_(false);
    }
    pending.length(0);
    for (unsigned i = 0; i < waiting.length(); ++i) {
      auto logical = ZuMv(waiting[i]);
      if (logical) logical->connectFailed_(false);
    }
    waiting.length(0);
    this->app()->sessionDown();
  }
  void connectFailed(bool transient) {
    auto self = ZmMkRef(this);
    down = true;
    for (unsigned i = 0; i < pending.length(); ++i) {
      auto logical = ZuMv(pending[i]);
      if (logical) logical->connectFailed_(transient);
    }
    pending.length(0);
    for (unsigned i = 0; i < waiting.length(); ++i) {
      auto logical = ZuMv(waiting[i]);
      if (logical) logical->connectFailed_(transient);
    }
    waiting.length(0);
    Base::disconnect();
  }
  void close(Logical *logical, Stream *stream) {
    auto session = this;
    this->app()->rxInvoke(session, [
      session,
      logical = ZmMkRef(logical),
      stream = ZmMkRef(stream)
    ]() mutable {
      session->close_(ZuMv(logical), ZuMv(stream));
      return session;
    });
  }
  void finish(Stream *stream) {
    this->send(ZmMkRef(stream), "", true);
  }
  bool h3PeerCap() const {
    if (this->app()->txInvoked()) return h3PeerCapTx;
    return h3.peerExtendedConnect;
  }
  void h3PeerCap(bool value) {
    auto session = this;
    this->app()->txRun([session, value]() {
      session->h3PeerCapTx = value;
    });
  }
  bool migrate(const ZiSockAddr &local) {
    if (migrationDone) return false;
    if (migrationRequested) return true;
    migrationRequested = true;
    if (this->migrateLocal(local.ip(), local.port())) return true;
    migrationDone = true;
    return false;
  }
  void close_(ZmRef<Logical> logical, StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical close outside Rx thread", return);
    if (!stream) {
      if (!remove_(pending, logical.ptr()))
	(void)remove_(waiting, logical.ptr());
      logical->session({});
      logical->disconnected_(false);
      return;
    }
    if (stream->logical.ptr() != logical.ptr()) return;
    (void)this->send(stream, "", true);
    stream->logical = nullptr;
    for (unsigned i = 0; i < streams.length(); ++i)
      if (streams[i].ptr() == stream.ptr()) {
	streams.splice(i, 1);
	break;
      }
    logical->disconnected_(false);
  }
  void streamed(StreamRef stream) {
    if (!stream || stream->id() < 0 ||
	Zquic::StreamID::server(uint64_t(stream->id())) ||
	Zquic::StreamID::uni(uint64_t(stream->id())))
      return;
    if (!waiting) {
      (void)this->send(stream, "", true);
      return;
    }
    auto logical = ZuMv(waiting[0]);
    waiting.splice(0, 1);
    opened(ZuMv(logical), ZuMv(stream));
  }
  void streamResetReceived(StreamRef stream, uint64_t, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  void streamStopSendingReceived(StreamRef stream, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  H3::QPackTxTable *qpackTx() { return &h3Tx; }

private:
  template <typename List>
  static bool remove_(List &list, Logical *logical) {
    for (unsigned i = 0; i < list.length(); ++i)
      if (list[i].ptr() == logical) {
	list.splice(i, 1);
	return true;
      }
    return false;
  }

  void migrationComplete_() {
    migrationDone = true;
    Pending logical;
    for (unsigned i = 0; i < streams.length(); ++i)
      if (streams[i]->logical)
	logical.push(streams[i]->logical);
    for (unsigned i = 0; i < logical.length(); ++i)
      logical[i]->migrationComplete_();
  }

public:
  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Pending		pending;
  Waiting		waiting;
  Streams		streams;
  Zquic::Host		host;
  uint16_t		port = 0;
  bool			ready = false;
  bool			down = false;
  bool			closePeer = false;
  bool			migrationRequested = false;
  bool			migrationDone = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
};

template <typename App> struct ServerSession;
template <typename App> struct ServerStream;

template <typename App>
class ServerEngine :
  public Zquic::Server<ServerEngine<App>, ServerSession<App>>,
  public Faults<ServerEngine<App>> {
public:
  using Session = ServerSession<App>;
  using Base = Zquic::Server<ServerEngine, Session>;
  using StopFn =
    ZmFn<void(bool), ZmFnHeapID<"Zhttp.H3.ServerStop">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      logicalDisconnect_([this, fn = ZuMv(fn)]() mutable {
	Base::stop([fn = ZuMv(fn)](bool ok) mutable { fn(ok); });
      });
    });
  }
  void final() {
    this->clearFaults();
    Base::final();
  }

  ZiIP localIP() const { return user()->localIP(); }
  uint16_t localPort() const { return user()->localPort(); }
  void listening() { user()->listening(); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  void disconnected(Session *session, bool peer) {
    session->logicalDisconnected(peer);
    Base::disconnected(session, peer);
  }

  ZmRef<Session> accepted(const Zquic::InitialInfo &info) {
    ConnectedInfo ci = ProfileTraits<H3QUIC>::apply({
      .version = Zquic::Version1,
      .transport = Transport::QUIC,
      .secure = true
    });
    if (!user()->admit(ci)) return {};
    EndpointString remote;
    remote << info.peer.ip();
    return new Session{this, ZuMv(remote)};
  }

private:
  template <typename Done>
  void logicalDisconnect_(Done &&done) {
    this->allLinks(
      [](ZmRef<Session> session) {
	session->logicalDisconnected(false);
      },
      ZuFwd<Done>(done));
  }
};

template <typename App>
struct ServerStream :
  public Zquic::SrvStream<ServerSession<App>, ServerStream<App>>,
  public H3::CxnStream<ServerStream<App>,
    H3::Cxn<ServerSession<App>, ZmRef<ServerStream<App>>>> {
  using Session = ServerSession<App>;
  using Base = Zquic::SrvStream<Session, ServerStream>;
  using H3Cxn = H3::Cxn<Session, ZmRef<ServerStream>>;
  using CxnStream = H3::CxnStream<ServerStream, H3Cxn>;
  using Logical = typename App::Link;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!logical) {
      auto session = this->link();
      logical = new Logical{
	session->app()->user(), session, this, session->remote};
      session->logical.push(ZmMkRef(this));
      logical->connected_(ProfileTraits<H3QUIC>::apply({
	.alpn = "h3",
	.version = Zquic::Version1,
	.transport = Transport::QUIC,
	.secure = true
      }));
    }
    return logical->process_(rx);
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

  ZmRef<Logical>	logical;
};

template <typename App>
struct ServerSession :
  public Zquic::SrvLink<
    ServerEngine<App>, ServerSession<App>, ServerStream<App>> {
  using Engine = ServerEngine<App>;
  using Stream = ServerStream<App>;
  using Base = Zquic::SrvLink<Engine, ServerSession, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<ServerSession, StreamRef>;
  using Logical =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ServerLogical">>;
  using Base::Base;

  ServerSession(Engine *app, EndpointString remote_) :
    Base{app}, remote{ZuMv(remote_)} { }

  void connected(Zquic::Connected info) {
    if (info.version != Zquic::Version1 || info.alpn != "h3" ||
	!h3.openLocal(
	  *this, H3::Params{},
	  this->app()->user()->quicConfig().extendedConnect()))
      Base::disconnect(H3::SettingsError);
  }
  void disconnected(bool peer) {
    logicalDisconnected(peer);
  }
  void logicalDisconnected(bool peer) {
    if (notified) return;
    notified = true;
    for (unsigned i = 0; i < logical.length(); ++i)
      if (logical[i]->logical)
	logical[i]->logical->disconnected_(peer);
    logical.length(0);
    this->app()->user()->release();
  }
  void streamed(StreamRef) { }
  void streamResetReceived(StreamRef stream, uint64_t, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  void streamStopSendingReceived(StreamRef stream, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  void finish(Stream *stream) {
    this->send(ZmMkRef(stream), "", true);
  }
  bool h3PeerCap() const {
    if (this->app()->txInvoked()) return h3PeerCapTx;
    return h3.peerExtendedConnect;
  }
  void h3PeerCap(bool value) {
    auto session = this;
    this->app()->txRun([session, value]() {
      session->h3PeerCapTx = value;
    });
  }
  H3::QPackTxTable *qpackTx() { return &h3Tx; }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Logical		logical;
  EndpointString	remote;
  bool			notified = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
};

} // namespace H3_

template <typename App, typename Impl>
class ClientLink<App, Impl, H3QUIC> : public ZmObject {
  using Engine = H3_::ClientEngine<App>;
  using NativeSession = H3_::ClientSession<App, Impl>;
  using NativeStream = H3_::ClientStream<App, Impl>;

public:
  enum { TLS = 1, Multiplexed = 1 };
  using Protocol = QUIC;

  ClientLink(App *app) : m_app{app} {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  template <typename Host>
  void connect(Host &&host, uint16_t port) {
    m_connected = false;
    m_failed = false;
    m_cancelled = false;
    m_disconnecting = false;
    m_migrationRequested = false;
    m_migrationComplete = false;
    m_disconnectPending = false;
    m_app->connect(impl(), Zquic::Host{ZuFwd<Host>(host)}, port);
  }
  template <typename Host>
  void connect(Host &&host, uint16_t port, ZiIP remote) {
    m_connected = false;
    m_failed = false;
    m_disconnecting = false;
    m_migrationRequested = false;
    m_migrationComplete = false;
    m_disconnectPending = false;
    m_app->connect(
      impl(), Zquic::Host{ZuFwd<Host>(host)}, port, ZuMv(remote));
  }
  template <typename Endpoint>
  void connectEndpoint(const Endpoint &endpoint) {
    connect(endpoint.tlsName, endpoint.port, endpoint.ip);
  }
  auto txStream() { return m_stream->txStream(); }
  bool tunnelPeerCap() const {
    return m_session && m_session->h3PeerCap();
  }
  bool tunnelLocalCap() const {
    return m_session && m_session->h3.localExtendedConnect;
  }
  template <typename L>
  void tunnelSend(L &&l) {
    if (!m_stream) return;
    auto tx = m_stream->txStream();
    auto body = H3::dataStream(tx);
    ZuFwd<L>(l)(body);
    body.flush();
  }
  void tunnelEnd() {
    if (m_session && m_stream) m_session->finish(m_stream);
  }
  void tunnelReset() {
    if (!m_stream) return;
    auto stream = ZmMkRef(m_stream);
    m_stream->link()->app()->txRun([stream = ZuMv(stream)]() mutable {
      stream->stop(H3::RequestCancelled);
      stream->quicReset(H3::RequestCancelled);
    });
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    using H3Cxn = ZuDecay<decltype(m_session->h3)>;
    parser.h3(
      m_session->h3.qpackRx(), &m_session->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackDecoderWrite(span);
      },
      [](void *ptr, uint64_t error) {
	static_cast<H3Cxn *>(ptr)->error(error);
      },
      uint64_t(m_stream->id()));
    parser.extendedConnect(m_session->h3.localExtendedConnect);
    (void)rx;
    return parser.process(*m_stream);
  }
  template <typename Builder>
  auto transmit(Builder &builder) {
    using H3Cxn = ZuDecay<decltype(m_session->h3)>;
    builder.h3(
      m_session->qpackTx(), &m_session->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackEncoderWrite(span);
      },
      uint64_t(m_stream->id()), m_session->h3PeerCap());
    return m_stream->txStream();
  }
  void finish() {
    if (m_session && m_stream) m_session->finish(m_stream);
  }
  bool active() const { return m_session && m_stream; }
  void disconnect() {
    m_cancelled = true;
    if (!m_session || m_disconnecting) return;
    if (m_migrationRequested && !m_migrationComplete) {
      m_disconnectPending = true;
      return;
    }
    m_disconnecting = true;
    m_session->close(impl(), m_stream);
  }

  void connected_(ConnectedInfo info) {
    if (m_cancelled) {
      disconnect();
      return;
    }
    m_connected = true;
    if (m_app->quicConfig().migrateOnOpen()) requestMigration_();
    m_app->connected(*impl(), info);
  }
  void disconnected_(bool peer) {
    ZmRef<NativeSession> session = ZuMv(m_session);
    m_stream = nullptr;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    ZmRef<NativeSession> session = ZuMv(m_session);
    m_stream = nullptr;
    m_app->connectFailed(*impl(), transient);
  }
  template <typename Rx>
  int process_(Rx &rx) {
    return m_app->process(*impl(), rx);
  }
  void session(ZmRef<NativeSession> session) {
    m_session = ZuMv(session);
    if (!m_session) m_stream = nullptr;
  }
  NativeSession *session() const { return m_session; }
  void stream(NativeStream *stream) { m_stream = stream; }
  void migrationComplete_() {
    m_migrationComplete = true;
    if (m_disconnectPending) disconnect();
  }
  template <typename State>
  void responseHeadersParsed(State *) {
    if (m_app->quicConfig().migrateAfterHeaders()) requestMigration_();
  }
  template <typename State>
  void responseBodyBytes(State *state) {
    auto bytes = m_app->quicConfig().migrateAfterBytes();
    if (bytes && state && state->bodyBytes >= bytes) requestMigration_();
  }

private:
  bool requestMigration_() {
    if (m_migrationRequested || !m_session) return true;
    m_migrationRequested = true;
    ZiSockAddr local = m_app->quicConfig().migrationLocal();
    if (!local) local = m_session->local();
    if (m_session->migrate(local)) return true;
    m_migrationComplete = true;
    return false;
  }

  App			*m_app = nullptr;
  ZmRef<NativeSession>	m_session;
  NativeStream		*m_stream = nullptr;
  bool			m_connected = false;
  bool			m_failed = false;
  bool			m_cancelled = false;
  bool			m_disconnecting = false;
  bool			m_migrationRequested = false;
  bool			m_migrationComplete = false;
  bool			m_disconnectPending = false;
};

template <typename App>
class Client<App, H3QUIC> : public H3_::ClientEngine<App> {
public:
  using Base = H3_::ClientEngine<App>;
  using Traits = Transport_::Traits<QUIC>;
  enum { TLS = 1, Multiplexed = 1 };

  using Base::connect;
  using Base::init;

  bool init(const EngineConfig &engine, const QUICConfig &config) {
    m_config = config;
    if (!Base::init(Traits::clientParams(engine, config))) return false;
    Base::faults(config);
    return true;
  }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  template <typename Link>
  void connectFailed(Link &, bool) { }

  const QUICConfig &quicConfig() const { return m_config; }

private:
  QUICConfig	m_config;
};

template <typename App, typename Impl, typename Session>
class ServerLink<App, Impl, H3QUIC, Session> : public ZmObject {
  using Engine = H3_::ServerEngine<App>;
  using NativeSession = H3_::ServerSession<App>;
  using NativeStream = H3_::ServerStream<App>;

public:
  enum { TLS = 1, Multiplexed = 1 };
  using Protocol = QUIC;

  ServerLink(
    App *app, NativeSession *native, NativeStream *stream, ZuCSpan remote) :
      m_app{app}, m_native{native}, m_stream{stream}, m_remote{remote}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  ZuCSpan remote() const { return m_remote; }
  Session &session() { return m_session; }
  auto txStream() { return m_stream->txStream(); }
  bool tunnelPeerCap() const {
    return m_native && m_native->h3PeerCap();
  }
  bool tunnelLocalCap() const {
    return m_native && m_native->h3.localExtendedConnect;
  }
  template <typename L>
  void tunnelSend(L &&l) {
    if (!m_stream) return;
    auto tx = m_stream->txStream();
    auto body = H3::dataStream(tx);
    ZuFwd<L>(l)(body);
    body.flush();
  }
  void tunnelEnd() {
    if (m_native && m_stream) m_native->finish(m_stream);
  }
  void tunnelReset() {
    if (!m_stream) return;
    auto stream = ZmMkRef(m_stream);
    m_stream->link()->app()->txRun([stream = ZuMv(stream)]() mutable {
      stream->stop(H3::RequestCancelled);
      stream->quicReset(H3::RequestCancelled);
    });
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) {
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    parser.h3(
      m_native->h3.qpackRx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackDecoderWrite(span);
      },
      [](void *ptr, uint64_t error) {
	static_cast<H3Cxn *>(ptr)->error(error);
      },
      uint64_t(m_stream->id()));
    parser.extendedConnect(m_native->h3.localExtendedConnect);
    (void)rx;
    return parser.process(*m_stream);
  }
  template <typename Builder>
  auto transmit(Builder &builder) {
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    builder.h3(
      m_native->qpackTx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackEncoderWrite(span);
      },
      uint64_t(m_stream->id()), m_native->h3PeerCap());
    return m_stream->txStream();
  }
  void finish() {
    if (m_native && m_stream) m_native->finish(m_stream);
  }
  void disconnect() {
    if (m_native) m_native->disconnect();
  }

  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), info);
  }
  void disconnected_(bool peer) {
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_native = nullptr;
    m_stream = nullptr;
  }
  template <typename Rx>
  int process_(Rx &rx) {
    return m_session.process(*impl(), rx);
  }

private:
  App			*m_app = nullptr;
  NativeSession		*m_native = nullptr;
  NativeStream		*m_stream = nullptr;
  Session		m_session;
  EndpointString	m_remote;
};

template <typename App>
class Server<App, H3QUIC> : public H3_::ServerEngine<App> {
public:
  using Base = H3_::ServerEngine<App>;
  using Traits = Transport_::Traits<QUIC>;
  enum { TLS = 1, Multiplexed = 1 };

  using Base::init;

  bool init(const EngineConfig &engine, const QUICConfig &config) {
    m_config = config;
    if (!Base::init(Traits::serverParams(engine, config))) return false;
    Base::faults(config);
    return true;
  }
  void stopAccepting() { }

  bool admit(const ConnectedInfo &) { return true; }
  void release() { }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
  const QUICConfig &quicConfig() const { return m_config; }

private:
  QUICConfig	m_config;
};

} // namespace Zhttp

#endif /* ZhttpH3Engine_HH */
