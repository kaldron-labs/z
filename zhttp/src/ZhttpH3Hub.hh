//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - private HTTP/3 transport/link adapters

#ifndef ZhttpH3Hub_HH
#define ZhttpH3Hub_HH

#ifndef Zhttp_HH
#define Zhttp_CORE_ONLY
#include <zlib/Zhttp.hh>
#undef Zhttp_CORE_ONLY
#endif

#include <zlib/ZmContext.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmRandom.hh>

#include <zlib/ZhttpClientHub.hh>
#include <zlib/ZhttpH3Cxn.hh>
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
class ClientHub;
template <typename App, typename Logical>
struct CliLink;
template <typename App, typename Logical>
struct ClientStream;
template <typename App>
class ServerHub;

struct CliLinkSlot {
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
// protocol-private link; the two function pointers are control-plane only.
template <typename App>
class ClientHub :
  public Zquic::Client<ClientHub<App>>,
  public Faults<ClientHub<App>> {
public:
  using StopFn =
    ZmFn<void(bool), ZmFnHeapID<"Zhttp.H3.ClientStop">>;
  using StopFns =
    ZtArray<StopFn, ZtArrayHeapID<"Zhttp.H3.ClientStopFns">>;
  using Links =
    ZtArray<CliLinkSlot,
      ZtArrayHeapID<"Zhttp.H3.ClientLinks">>;

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
  template <typename Logical>
  void connect_(
    Logical *logical_, Zquic::Host host, uint16_t port, ZiIP remote) {
    using Link = CliLink<App, Logical>;
    ZmRef<Logical> logical = ZmMkRef(logical_);
    this->rxInvoke([
      this, logical = ZuMv(logical), host = ZuMv(host),
      remote = ZuMv(remote), port
    ]() mutable {
      ZmRef<Link> link;
      for (unsigned i = 0; i < m_links.length(); ++i) {
	auto &slot = m_links[i];
	auto candidate = slot.owner.object<Link>();
	if (!candidate->down && slot.host == host &&
	    slot.remote == remote && slot.port == port) {
	  link = candidate;
	  break;
	}
      }
      if (!link) {
	link = new Link{this, host, port};
	m_links.push(CliLinkSlot{
	  .owner = link,
	  .close = [](void *ptr) {
		    // Hub shutdown must not wait behind an in-flight migration;
		    // abort guarantees endpointDown() and deterministic draining.
		    static_cast<Link *>(ptr)->abort();
	  },
	  .down = [](void *ptr) {
	    return static_cast<Link *>(ptr)->down;
	  },
	  .printDiag = [](void *ptr) {
#ifdef Zquic_DEBUG
	    static_cast<Link *>(ptr)->endpointDiag([](const auto &diag) {
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
	link->add(ZuMv(logical));
	link->connect(ZuMv(host), port, ZuMv(remote));
	return;
      }
      link->add(ZuMv(logical));
    });
  }

public:
  unsigned reconnFreq() const { return 0; }

  void printDiag() {
    for (unsigned i = 0; i < m_links.length(); ++i) {
      auto &slot = m_links[i];
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
  void linkDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopBase_();
  }
  void final() {
    this->clearFaults();
    m_links.length(0);
    Zquic::Client<ClientHub>::final();
  }

private:
  Links	m_links;

  void stopRx_(StopFn done) {
    m_stopFns.push(ZuMv(done));
    if (m_stopping) return;
    m_stopping = true;
    m_stopPending = 0;
    for (unsigned i = 0; i < m_links.length(); ++i) {
      auto &slot = m_links[i];
      if (slot.down(slot.owner.object<void>())) continue;
      ++m_stopPending;
    }
    if (!m_stopPending) {
      stopBase_();
      return;
    }
    for (unsigned i = 0; i < m_links.length(); ++i) {
      auto &slot = m_links[i];
      if (slot.down(slot.owner.object<void>())) continue;
      slot.close(slot.owner.object<void>());
    }
  }

  void stopBase_() {
    Zquic::Client<ClientHub>::stop(
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
    CliLink<App, Logical>, ClientStream<App, Logical>>,
  public H3::CxnStream<ClientStream<App, Logical>,
    H3::Cxn<CliLink<App, Logical>,
      ZmRef<ClientStream<App, Logical>>>> {
  using Link = CliLink<App, Logical>;
  using Base = Zquic::CliStream<Link, ClientStream>;
  using H3Cxn = H3::Cxn<Link, ZmRef<ClientStream>>;
  using CxnStream = H3::CxnStream<ClientStream, H3Cxn>;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!logical) return -1;
    int rc = logical->process_(rx);
    if (rc >= 0 && this->rxComplete()) this->link()->remoteEnd(this);
    return rc;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

  ZmRef<Logical>	logical;
  bool		localEnd = false;
  bool		remoteEnd = false;
  bool		closing = false;
};

template <typename App, typename Logical>
struct CliLink :
  public Zquic::CliLink<ClientHub<App>, CliLink<App, Logical>,
    ClientStream<App, Logical>> {
  using Hub = ClientHub<App>;
  using Stream = ClientStream<App, Logical>;
  using Base = Zquic::CliLink<Hub, CliLink, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<CliLink, StreamRef>;
  using Pending =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientPending">>;
  using Waiting =
    ZtArray<ZmRef<Logical>, ZtArrayHeapID<"Zhttp.H3.ClientWaiting">>;
  using Streams =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ClientStreams">>;
  using Base::Base;

  CliLink(Hub *app, Zquic::Host host_, uint16_t port_) :
    Base{app}, host{ZuMv(host_)}, port{port_} { }

  unsigned txQueueMax() const {
    return this->app()->user()->quicConfig().maxQueuedFrames();
  }

  void add(ZmRef<Logical> logical) {
    logical->native(ZmMkRef(this));
    if (!ready) {
      pending.push(ZuMv(logical));
      return;
    }
    open(ZuMv(logical));
  }
  void open(ZmRef<Logical> logical) {
    waiting.push(ZuMv(logical));
    auto link = ZmMkRef(this);
    this->app()->txRun([link]() mutable {
      auto stream = link->stream(Zquic::StreamType::Duplex);
      if (!stream) return;
      link->app()->rxRun([
	link = ZuMv(link), stream = ZuMv(stream)
      ]() mutable {
	link->streamed(ZuMv(stream));
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
    if (info.version != Zquic::Version1 || info.alpn != "h3") {
      connectFailed(false);
      return;
    }
    const auto &config = this->app()->user()->quicConfig();
    H3::QPackLimits limits{
      config.qpackRxCapacity(), config.qpackTxCapacity(),
      config.qpackRxBlocked(), config.qpackTxSections()
    };
    bool extendedConnect = config.extendedConnect();
    auto link = ZmMkRef(this);
    this->app()->txRun([link, limits, extendedConnect]() mutable {
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      link->app()->rxRun([
	link = ZuMv(link), limits, extendedConnect, ok
      ]() mutable {
	if (!ok || link->down) {
	  link->connectFailed(false);
	  return;
	}
	auto params = H3::Params().qpackLimits(limits);
	if (!link->h3.openLocal(*link, params, extendedConnect)) {
	  link->connectFailed(false);
	  return;
	}
	if (!link->h3.localExtendedConnect) link->h3Ready();
      });
    });
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
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void migrationFailed(const Zquic::MigrationResult &) {
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->migrationComplete_();
    });
  }
  void endpointDown() {
    this->app()->rxRun([link = ZmMkRef(this)]() mutable {
      link->endpointDown_();
    });
  }
  void endpointDown_() {
    if (finalizing) return;
    finalizing = true;
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
    h3.qpackRxTable.final();
    auto link = ZmMkRef(this);
    this->app()->txRun([link]() mutable {
      link->h3Tx.final();
      link->app()->rxRun([link = ZuMv(link)]() mutable {
	link->app()->linkDown();
      });
    });
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
    auto link = this;
    this->app()->rxInvoke(link, [
      link,
      logical = ZmMkRef(logical),
      stream = ZmMkRef(stream)
    ]() mutable {
      link->close_(ZuMv(logical), ZuMv(stream));
      return link;
    });
  }
  void finish(Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link, stream = ZmMkRef(stream)
    ]() mutable {
      link->finish_(ZuMv(stream));
      return link;
    });
  }
  void finish_(StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->localEnd) return;
    stream->localEnd = true;
    if (stream->remoteEnd) closeLater_(stream, false);
    this->send(ZuMv(stream), "", true);
  }
  void remoteEnd(Stream *stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 remote logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->remoteEnd) return;
    stream->remoteEnd = true;
    if (stream->localEnd) closeLater_(stream, true);
  }
  bool h3PeerCap() const {
    if (this->app()->txInvoked()) return h3PeerCapTx;
    return h3.peerExtendedConnect;
  }
  void h3PeerCap(bool value) {
    auto link = this;
    this->app()->txRun([link, value]() {
      link->h3PeerCapTx = value;
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
      logical->native({});
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
  void closeLater_(Stream *stream, bool peer) {
    if (!stream || stream->closing) return;
    stream->closing = true;
    this->app()->rxRun([
      link = ZmMkRef(this), stream = ZmMkRef(stream), peer
    ]() mutable {
      if (!stream->logical) return;
      auto logical = ZuMv(stream->logical);
      for (unsigned i = 0; i < link->streams.length(); ++i)
	if (link->streams[i].ptr() == stream.ptr()) {
	  link->streams.splice(i, 1);
	  break;
	}
      logical->disconnected_(peer);
    });
  }

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
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 migration completion outside Rx thread", return);
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
  bool			finalizing = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
};

template <typename App> struct SrvLink;
template <typename App> struct ServerStream;

template <typename App>
class ServerHub :
  public Zquic::Server<ServerHub<App>, SrvLink<App>>,
  public Faults<ServerHub<App>> {
public:
  using Link = SrvLink<App>;
  using Base = Zquic::Server<ServerHub, Link>;
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
  void disconnected(Link *link, bool peer) {
    link->logicalDisconnected(peer);
    Base::disconnected(link, peer);
  }

  ZmRef<Link> accepted(const Zquic::InitialInfo &info) {
    ConnectedInfo ci = ProfileTraits<H3QUIC>::apply({
      .version = Zquic::Version1,
      .transport = Transport::QUIC,
      .secure = true
    });
    if (!user()->admit(ci)) return {};
    EndpointString remote;
    remote << info.peer.ip();
    return new Link{this, ZuMv(remote)};
  }

private:
  template <typename Done>
  void logicalDisconnect_(Done &&done) {
    this->allLinks(
      [](ZmRef<Link> link) {
	link->logicalDisconnected(false);
      },
      ZuFwd<Done>(done));
  }
};

template <typename App>
struct ServerStream :
  public Zquic::SrvStream<SrvLink<App>, ServerStream<App>>,
  public H3::CxnStream<ServerStream<App>,
    H3::Cxn<SrvLink<App>, ZmRef<ServerStream<App>>>> {
  using Link = SrvLink<App>;
  using Base = Zquic::SrvStream<Link, ServerStream>;
  using H3Cxn = H3::Cxn<Link, ZmRef<ServerStream>>;
  using CxnStream = H3::CxnStream<ServerStream, H3Cxn>;
  using Logical = typename App::Link;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!logical) {
      auto link = this->link();
      logical = new Logical{
	link->app()->user(), link, this, link->remote};
      link->logical.push(ZmMkRef(this));
      logical->connected_(ProfileTraits<H3QUIC>::apply({
	.alpn = "h3",
	.version = Zquic::Version1,
	.transport = Transport::QUIC,
	.secure = true
      }));
    }
    int rc = logical->process_(rx);
    if (rc >= 0 && this->rxComplete()) this->link()->remoteEnd(this);
    return rc;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

  ZmRef<Logical>	logical;
  bool		localEnd = false;
  bool		remoteEnd = false;
  bool		closing = false;
};

template <typename App>
struct SrvLink :
  public Zquic::SrvLink<
    ServerHub<App>, SrvLink<App>, ServerStream<App>> {
  using Hub = ServerHub<App>;
  using Stream = ServerStream<App>;
  using Base = Zquic::SrvLink<Hub, SrvLink, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<SrvLink, StreamRef>;
  using Logical =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ServerLogical">>;
  using Base::Base;

  unsigned txQueueMax() const {
    return this->app()->user()->quicConfig().maxQueuedFrames();
  }

  SrvLink(Hub *app, EndpointString remote_) :
    Base{app}, remote{ZuMv(remote_)} { }

  void connected(Zquic::Connected info) {
    if (info.version != Zquic::Version1 || info.alpn != "h3") {
      Base::disconnect(H3::SettingsError);
      return;
    }
    const auto &config = this->app()->user()->quicConfig();
    H3::QPackLimits limits{
      config.qpackRxCapacity(), config.qpackTxCapacity(),
      config.qpackRxBlocked(), config.qpackTxSections()
    };
    bool extendedConnect = config.extendedConnect();
    auto link = ZmMkRef(this);
    this->app()->txRun([link, limits, extendedConnect]() mutable {
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      link->app()->rxRun([
	link = ZuMv(link), limits, extendedConnect, ok
      ]() mutable {
	if (!ok || link->closed()) return;
	auto params = H3::Params().qpackLimits(limits);
	if (!link->h3.openLocal(*link, params, extendedConnect))
	  link->disconnect(H3::SettingsError);
      });
    });
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
    h3.qpackRxTable.final();
    auto link = ZmMkRef(this);
    this->app()->txRun([link]() mutable {
      link->h3Tx.final();
      link->app()->rxRun([link = ZuMv(link)]() mutable {
	link->app()->user()->release();
      });
    });
  }
  void streamed(StreamRef) { }
  void streamResetReceived(StreamRef stream, uint64_t, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  void streamStopSendingReceived(StreamRef stream, uint64_t) {
    if (stream) (void)stream->process(stream->rxStream());
  }
  void finish(Stream *stream) {
    auto link = this;
    this->app()->rxInvoke(link, [
      link, stream = ZmMkRef(stream)
    ]() mutable {
      link->finish_(ZuMv(stream));
      return link;
    });
  }
  void finish_(StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->localEnd) return;
    stream->localEnd = true;
    if (stream->remoteEnd) closeLater_(stream, false);
    this->send(ZuMv(stream), "", true);
  }
  void remoteEnd(Stream *stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 remote logical end outside Rx thread", return);
    if (!stream || !stream->logical || stream->remoteEnd) return;
    stream->remoteEnd = true;
    if (stream->localEnd) closeLater_(stream, true);
  }
  bool h3PeerCap() const {
    if (this->app()->txInvoked()) return h3PeerCapTx;
    return h3.peerExtendedConnect;
  }
  void h3PeerCap(bool value) {
    auto link = this;
    this->app()->txRun([link, value]() {
      link->h3PeerCapTx = value;
    });
  }
  H3::QPackTxTable *qpackTx() { return &h3Tx; }

private:
  void closeLater_(Stream *stream, bool peer) {
    if (!stream || stream->closing) return;
    stream->closing = true;
    this->app()->rxRun([
      link = ZmMkRef(this), stream = ZmMkRef(stream), peer
    ]() mutable {
      if (!stream->logical) return;
      auto logical = ZuMv(stream->logical);
      for (unsigned i = 0; i < link->logical.length(); ++i)
	if (link->logical[i].ptr() == stream.ptr()) {
	  link->logical.splice(i, 1);
	  break;
	}
      logical->disconnected_(peer);
    });
  }

public:
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

template <typename Impl>
class LogicalStream {
public:
  bool streamPeerCap() const {
    auto native = streamImpl_()->h3Native_();
    return native && native->h3PeerCap();
  }
  bool streamLocalCap() const {
    auto native = streamImpl_()->h3Native_();
    return native && native->h3.localExtendedConnect;
  }
  template <typename L>
  void streamTx(L &&l) {
    auto stream = streamImpl_()->h3Stream_();
    if (!stream) return;
    auto tx = stream->txStream();
    auto body = H3::dataStream(tx);
    ZuFwd<L>(l)(body);
  }
  void streamTxEnd() {
    auto impl = streamImpl_();
    auto native = impl->h3Native_();
    auto stream = impl->h3Stream_();
    if (native && stream) native->finish(stream);
  }
  void streamTxReset() {
    auto stream_ = streamImpl_()->h3Stream_();
    if (!stream_) return;
    auto stream = ZmMkRef(stream_);
    stream_->link()->app()->txRun([stream = ZuMv(stream)]() mutable {
      stream->stop(H3::RequestCancelled);
      stream->quicReset(H3::RequestCancelled);
    });
  }

private:
  const Impl *streamImpl_() const {
    return static_cast<const Impl *>(this);
  }
  Impl *streamImpl_() { return static_cast<Impl *>(this); }
};

} // namespace H3_

template <typename App, typename Impl>
class ClientLink<App, Impl, H3QUIC> :
  public ZmObject, public H3_::LogicalStream<Impl> {
  using Hub = H3_::ClientHub<App>;
  using NativeLink = H3_::CliLink<App, Impl>;
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
  void txErrorFn(ZiTxErrorFn fn) {
    m_txErrorFn = ZuMv(fn);
    if (m_native) m_native->h3.txErrorFn(m_txErrorFn);
    if (m_stream) m_stream->txErrorFn(m_txErrorFn);
  }
  NativeLink *h3Native_() const { return m_native; }
  NativeStream *h3Stream_() const { return m_stream; }
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
      uint64_t(m_stream->id()), &m_native->h3.params);
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
      uint64_t(m_stream->id()), m_native->h3PeerCap(),
      &m_native->h3.params);
    return m_stream->txStream();
  }
  void finish() {
    if (m_native && m_stream) m_native->finish(m_stream);
  }
  bool active() const { return m_native && m_stream; }
  void disconnect() {
    m_cancelled = true;
    if (!m_native || m_disconnecting) return;
    if (m_migrationRequested && !m_migrationComplete) {
      m_disconnectPending = true;
      return;
    }
    m_disconnecting = true;
    m_native->close(impl(), m_stream);
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
    ZmRef<NativeLink> native = ZuMv(m_native);
    m_stream = nullptr;
    if (m_connected)
      m_app->disconnected(*impl(), peer);
    else if (!m_failed)
      connectFailed_(false);
  }
  void connectFailed_(bool transient) {
    if (m_failed) return;
    m_failed = true;
    ZmRef<NativeLink> native = ZuMv(m_native);
    m_stream = nullptr;
    m_app->connectFailed(*impl(), transient);
  }
  template <typename Rx>
  int process_(Rx &rx) {
    return m_app->process(*impl(), rx);
  }
  void native(ZmRef<NativeLink> native) {
    m_native = ZuMv(native);
    if (m_native)
      m_native->h3.txErrorFn(m_txErrorFn);
    else
      m_stream = nullptr;
  }
  NativeLink *native() const { return m_native; }
  void stream(NativeStream *stream) {
    m_stream = stream;
    if (m_stream) m_stream->txErrorFn(m_txErrorFn);
  }
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
    if (m_migrationRequested || !m_native) return true;
    m_migrationRequested = true;
    ZiSockAddr local = m_app->quicConfig().migrationLocal();
    if (!local) local = m_native->local();
    if (m_native->migrate(local)) return true;
    m_migrationComplete = true;
    return false;
  }

  App			*m_app = nullptr;
  ZmRef<NativeLink>	m_native;
  NativeStream		*m_stream = nullptr;
  ZiTxErrorFn		m_txErrorFn;
  bool			m_connected = false;
  bool			m_failed = false;
  bool			m_cancelled = false;
  bool			m_disconnecting = false;
  bool			m_migrationRequested = false;
  bool			m_migrationComplete = false;
  bool			m_disconnectPending = false;
};

template <typename App>
class ClientHub<App, H3QUIC> : public H3_::ClientHub<App> {
public:
  using Base = H3_::ClientHub<App>;
  using Traits = Transport_::Traits<QUIC>;
  enum { TLS = 1, Multiplexed = 1 };

  using Base::connect;
  using Base::init;

  bool init(const HubConfig &hub, const QUICConfig &config) {
    if (!config.qpackValid() || !config.maxQueuedFrames()) return false;
    m_config = config;
    if (!Base::init(Traits::clientParams(hub, config))) return false;
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
class ServerLink<App, Impl, H3QUIC, Session> :
  public ZmObject, public H3_::LogicalStream<Impl> {
  using Hub = H3_::ServerHub<App>;
  using NativeLink = H3_::SrvLink<App>;
  using NativeStream = H3_::ServerStream<App>;

public:
  enum { TLS = 1, Multiplexed = 1 };
  using Protocol = QUIC;

  ServerLink(
    App *app, NativeLink *native, NativeStream *stream, ZuCSpan remote) :
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
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native) m_native->h3.txErrorFn(fn);
    if (m_stream) m_stream->txErrorFn(ZuMv(fn));
  }
  NativeLink *h3Native_() const { return m_native; }
  NativeStream *h3Stream_() const { return m_stream; }
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
      uint64_t(m_stream->id()), &m_native->h3.params);
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
      uint64_t(m_stream->id()), m_native->h3PeerCap(),
      &m_native->h3.params);
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
  NativeLink		*m_native = nullptr;
  NativeStream		*m_stream = nullptr;
  Session		m_session;
  EndpointString	m_remote;
};

template <typename App>
class Server<App, H3QUIC> : public H3_::ServerHub<App> {
public:
  using Base = H3_::ServerHub<App>;
  using Traits = Transport_::Traits<QUIC>;
  enum { TLS = 1, Multiplexed = 1 };

  using Base::init;

  bool init(const HubConfig &hub, const QUICConfig &config) {
    if (!config.qpackValid() || !config.maxQueuedFrames()) return false;
    m_config = config;
    if (!Base::init(Traits::serverParams(hub, config))) return false;
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

#endif /* ZhttpH3Hub_HH */
