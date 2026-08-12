//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP server

#ifndef ZhttpServer_HH
#define ZhttpServer_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpH2Hub.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

template <typename Impl, typename Parser_, typename Message_>
struct ServerSession {
  using Parser = Parser_;
  using State = typename Parser::State;
  using Message = Message_;

  Parser	parser;
  bool		complete = false;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(auto &) { }
  void disconnected(auto &, bool) { }

  template <typename Link, typename Rx>
  int process(Link &link, Rx &rx) {
    if (complete) return 1;
    auto state = link.receive(parser, rx);
    if (state == State::Error) return impl()->error(link, parser);
    if (state != State::Complete) {
      if constexpr (Message::ID == Version::H1)
	return parser.progressed();
      return 0;
    }
    int rc = impl()->request(link, parser);
    if constexpr (Message::OneMessagePerLink)
      complete = true;
    else
      parser.reset();
    return rc;
  }

  template <typename Link>
  int error(Link &, Parser &) { return -1; }
  template <typename Link>
  int request(Link &, Parser &) { return 1; }
};


template <typename App, typename Profile>
class ProtocolServer :
  public ProfileTraits<Profile>::Transport::template Server<App> {
  using HTTP = ProfileTraits<Profile>;
  using Traits = typename HTTP::Transport;

public:
  using Base = typename Traits::template Server<App>;
  using Base::init;
  using Base::start;
  enum {
    TLS = Traits::Secure,
    Multiplexed = HTTP::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const HubConfig &hub, const typename Traits::Config &config) {
    return Base::init(Traits::serverParams(hub, config));
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }

  void linkDisconnected_() { }
  void linkDrained_() { Base::linkDisconnected_(); }

  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!impl()->admit(ci)) return nullptr;
    using Link = typename App::Link;
    return new typename Link::Cxn(new Link{impl(), ci}, ci);
  }

  bool admit(const ZiCxnInfo &) { return true; }
  void release() { }
  unsigned idleTimeout() const { return 0; }

  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

namespace H2_ {

template <typename App> class ServerHub;
template <typename App> class SrvLink;

template <typename Headers, typename Sets,
  bool = bool(ZuTypeIn<Headers, Sets>{})>
struct HeaderPlan_ {
  enum { Valid = 0, Index = unsigned(-1) };
};
template <typename Headers, typename Sets>
struct HeaderPlan_<Headers, Sets, true> {
  enum { Valid = 1, Index = ZuTypeIndex<Headers, Sets>{} };
};
template <typename Headers, typename Sets, bool = bool(Sets::N)>
struct HeaderPlan {
  enum { Valid = 0, Index = unsigned(-1) };
};
template <typename Headers, typename Sets>
struct HeaderPlan<Headers, Sets, true> : public HeaderPlan_<Headers, Sets> {
};

template <typename App>
class ServerHub : public Ztls::Server<ServerHub<App>> {
public:
  using Base = Ztls::Server<ServerHub>;
  using Link = SrvLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config) || config.policy() != H2Policy::Force)
      return false;
    m_config = config;
    auto params = TLS_::serverParams(hub, config);
    return Base::init(ZuMv(params));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Link::Cxn(
      new Link{this, ci, m_config}, ci);
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    Base::stopListening();
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      this->rxRun([this]() {
	m_stopPending = 0;
	Base::allLinks_({this, [](ServerHub *hub, Ztc::Link *link) {
	  ++hub->m_stopPending;
	  static_cast<Link *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void linkDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    ZmAssert(!m_stopPending);
    ZmAssert(!m_stopFns);
    Base::final();
  }
  const H2Config &h2Config() const { return m_config; }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
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

  H2Config	m_config;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class SrvLink :
  public Ztls::SrvLink<ServerHub<App>, SrvLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<SrvLink<App>, typename App::Link> {
public:
  using Hub = ServerHub<App>;
  using Logical = typename App::Link;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<SrvLink, Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  SrvLink(
    Hub *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}, m_remoteIP{ci.remoteIP}, m_remotePort{ci.remotePort}
  {
    Wire_::initWire(true, config);
  }
  ~SrvLink() {
    Wire_::finalWire();
  }

  void connected(Ztls::Connected info) {
    if (info.alpn != "h2") {
      Base::disconnect();
      return;
    }
    Wire_::sendInitial();
  }
  void h2SettingsReceived() { }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire_::stopWire();
    Active active;
    Wire_::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire_::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      if (!link->m_released) {
	link->m_released = true;
	link->app()->user()->release();
      }
      link->app()->linkDown();
    });
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  void disconnectNative() { Base::disconnect(); }
  auto logicalTx(uint32_t id) {
    return HeaderBlock<SrvLink>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize()};
  }
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }

  Stream<Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire_::canOpenPeerStream(id)) return nullptr;
    ZmRef<Logical> logical = new Logical{
      this->app()->user(), this, id, m_remoteIP, m_remotePort};
    auto entry = Wire_::openPeerStream(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return Wire_::streamClosed(id); }
  Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return Error::StreamClosed;
    if (Wire_::peerStreamIdle(id)) {
      Wire_::refusePeerStream(id);
      return Error::RefusedStream;
    }
    return Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire_::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire_::peerProcessed(id);
    if (auto entry = Wire_::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, Error::T) { notify_(id, true); }
  void h2Cancel(uint32_t id) {
    rst_(id, Error::Cancel);
    notify_(id, false);
  }
  void h2Goaway_(uint32_t, Error::T) { m_draining = true; }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    if (key == Setting::InitialWindowSize &&
	!Wire_::peerInitialWindow(value))
      this->h2Error(Error::FlowControlError);
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire_::stopWire();
    Wire_::graceful();
    Base::disconnect();
  }

private:
  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, Error::T error) {
    auto tx = Base::txStream();
    StreamBytes<decltype(tx)> sink{tx};
    putHeader(sink, {
      .length = 4, .streamID = id, .type = FrameType::RSTStream
    });
    putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire_::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire_::removeStream(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }

  ZiIP		m_remoteIP;
  uint16_t	m_remotePort = 0;
  bool		m_draining = false;
  bool		m_down = false;
  bool		m_released = false;
  bool		m_stopping = false;
};

template <typename App, typename Impl, typename Session, typename NativeLink>
class ServerLogical : public ZmObject, public LogicalStream<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ServerLogical(
    App *app, NativeLink *native, uint32_t streamID,
    const ZiIP &remoteIP, uint16_t remotePort) :
      m_app{app}, m_native{native}, m_streamID{streamID},
      m_remoteIP{remoteIP}, m_remotePort{remotePort}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }
  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  const ZiIP &remoteIP() const { return m_remoteIP; }
  uint16_t remotePort() const { return m_remotePort; }
  Session &session() { return m_session; }
  auto txStream() { return m_native->logicalTx(m_streamID); }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native)
      m_native->logicalTxErrorFn(m_streamID, ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) {
    auto tx = txStream();
    using Headers = typename Builder::Headers;
    using Sets = typename ResponseHeaderSets<App>::T;
    if constexpr (HeaderPlan<Headers, Sets>::Valid)
      tx.plan(HeaderPlan<Headers, Sets>::Index);
    return tx;
  }
  bool active() const { return m_native && m_streamID; }
  void txComplete(Transport_::TxCompleteFn fn) {
    m_txComplete = ZuMv(fn);
  }
  void txCancel() { m_txComplete = {}; }
  bool txFence(Transport_::TxCompleteFn fn) {
    return m_native && m_native->fenceTx(m_streamID, ZuMv(fn));
  }
  void finish() {
    auto fn = ZuMv(m_txComplete);
    m_txComplete = {};
    if (!fn) return;
    if (!m_native || !m_native->finishTx(m_streamID, ZuMv(fn)))
      fn(ResponseOutcome::TxFailed);
  }
  void disconnect() {
    if (m_native) m_native->h2Cancel(m_streamID);
  }
  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), ZuMv(info));
  }
  void disconnected_(bool peer) {
    if (!m_native) return;
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_app->txRun([
      logical = ZmMkRef(impl()), native = ZmMkRef(m_native)]() mutable {
      (void)native;
      logical->disconnectedTx_();
    });
  }
  template <typename Rx>
  int process_(Rx &rx) { return m_session.process(*impl(), rx); }

private:
  void disconnectedTx_() {
    auto fn = ZuMv(m_txComplete);
    m_txComplete = {};
    m_native = nullptr;
    m_streamID = 0;
    if (fn) fn(ResponseOutcome::Reset);
  }

  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  Transport_::TxCompleteFn m_txComplete;
  uint32_t	m_streamID = 0;
  Session	m_session;
  ZiIP		m_remoteIP;
  uint16_t	m_remotePort = 0;
};

} // namespace H2_

template <typename App, typename Impl, typename Session>
class ServerLink<App, Impl, H2TLS, Session> :
  public H2_::ServerLogical<
    App, Impl, Session, H2_::SrvLink<App>> {
  using Base = H2_::ServerLogical<
    App, Impl, Session, H2_::SrvLink<App>>;

public:
  using Base::Base;
};

namespace TLS_ {

template <typename App> class ServerHub;
template <typename App> class SrvLink;
template <
  typename App, typename Impl, typename Session, typename NativeLink>
class ServerH1Logical : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ServerH1Logical(
      App *app, NativeLink *native,
      const ZiIP &remoteIP, uint16_t remotePort) :
    m_app{app}, m_native{native},
    m_remoteIP{remoteIP}, m_remotePort{remotePort}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() { return static_cast<Impl *>(this); }
  const ZiIP &remoteIP() const { return m_remoteIP; }
  uint16_t remotePort() const { return m_remotePort; }
  Session &session() { return m_session; }
  auto txStream() { return m_native->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native) m_native->txErrorFn(ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  bool active() const { return m_native; }
  void txComplete(Transport_::TxCompleteFn fn) {
    if (m_native) m_native->armH1Complete(ZuMv(fn));
    else fn(ResponseOutcome::Cancelled);
  }
  void txCancel() {
    if (m_native) m_native->cancelH1Complete();
  }
  bool txFence(Transport_::TxCompleteFn fn) {
    return m_native && m_native->fenceH1(ZuMv(fn));
  }
  void finish() {
    if (m_native) m_native->finishH1();
  }
  void disconnect() {
    if (m_native) m_native->disconnectNative();
  }
  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), ZuMv(info));
    touch_();
  }
  void disconnected_(bool peer) {
    if (!m_native) return;
    m_app->mx()->del(&m_idleTimer);
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_app->txRun([
      logical = ZmMkRef(impl()), native = ZmMkRef(m_native)]() mutable {
      (void)native;
      logical->disconnectedTx_();
    });
  }
  int process_(Ztls::RxStream &rx) {
    int rc = m_session.process(*impl(), rx);
    if (rc >= 0) touch_();
    return rc;
  }

private:
  void disconnectedTx_() {
    m_native->resetH1();
    m_native = nullptr;
  }

  void touch_() {
    if (!m_native) return;
    auto timeout = m_app->idleTimeout();
    if (!timeout) return;
    m_app->mx()->add(
      &m_idleTimer, Zm::now(timeout), ZmScheduler::Update,
      [this](auto &&arm) {
	return arm([link = impl()]() { link->disconnect(); });
      }, m_app->rxThread());
  }

  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  Session	m_session;
  ZmScheduler::Timer m_idleTimer;
  ZiIP		m_remoteIP;
  uint16_t	m_remotePort = 0;
};

template <typename App>
class ServerHub : public Ztls::Server<ServerHub<App>> {
public:
  using Base = Ztls::Server<ServerHub>;
  using Link = SrvLink<App>;
  using StopFn = ZmFn<void(bool), ZmFnHeapID<"Zhttp.H2">>;
  using StopFns = ZtArray<StopFn,
    ZtArrayHeapID<"Zhttp.H2">>;

  App *user() { return static_cast<App *>(this); }
  const App *user() const { return static_cast<const App *>(this); }

  bool init(const HubConfig &hub, const H2Config &config) {
    if (!TLS_::valid(config)) return false;
    m_config = config;
    return Base::init(TLS_::serverParams(hub, config));
  }
  ZiIP localIP() const { return user()->localIP(); }
  unsigned localPort() const { return user()->localPort(); }
  unsigned nAccepts() const { return 8; }
  void listening(const ZiListenInfo &info) { user()->listening(info); }
  void listenFailed(bool transient) { user()->listenFailed(transient); }
  ZiConnection *accepted(const ZiCxnInfo &ci) {
    if (!user()->admit(ci)) return nullptr;
    return new typename Link::Cxn(
      new Link{this, ci, m_config}, ci);
  }
  bool start() {
    if (!Base::start()) return false;
    Base::listen();
    return true;
  }
  template <typename Done>
  void start(Done &&done) {
    Base::start([this, done = ZuFwd<Done>(done)](bool ok) mutable {
      if (ok) Base::listen();
      done(ok);
    });
  }
  void stopAccepting() { Base::stopListening(); }
  template <typename Done>
  void drain(Done &&done) {
    this->rxRun([this, done = ZuFwd<Done>(done)]() mutable {
      Base::allLinks_({this, [](ServerHub *, Ztc::Link *link) {
	static_cast<Link *>(link)->drain();
      }});
      done();
    });
  }
  bool stop() {
    return ZmBlock<bool>{}(
      [this](auto wake) { stop(StopFn{ZuMv(wake)}); });
  }
  template <typename Done>
  void stop(Done &&done) {
    StopFn fn{ZuFwd<Done>(done)};
    Base::stopListening();
    this->rxRun([this, fn = ZuMv(fn)]() mutable {
      m_stopFns.push(ZuMv(fn));
      if (m_stopping) return;
      m_stopping = true;
      this->rxRun([this]() {
	m_stopPending = 0;
	Base::allLinks_({this, [](ServerHub *hub, Ztc::Link *link) {
	  ++hub->m_stopPending;
	  static_cast<Link *>(link)->beginStop();
	}});
	if (!m_stopPending) stopDrain_();
      });
    });
  }
  void linkDown() {
    if (m_stopping && m_stopPending && !--m_stopPending)
      stopDrain_();
  }
  void final() {
    ZmAssert(!m_stopPending);
    ZmAssert(!m_stopFns);
    Base::final();
  }

private:
  void stopDrain_() {
    this->rxRun([this]() {
      this->txRun([this]() {
	this->rxRun([this]() {
	  Base::stop([this](bool ok) {
	    this->rxRun([this, ok]() { stopped_(ok); });
	  });
	});
      });
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

  H2Config	m_config;
  StopFns	m_stopFns;
  unsigned	m_stopPending = 0;
  bool		m_stopping = false;
};

template <typename App>
class SrvLink :
  public Ztls::SrvLink<ServerHub<App>, SrvLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<SrvLink<App>, typename App::H2Link> {
public:
  using Hub = ServerHub<App>;
  using H1Logical = typename App::H1Link;
  using H2Logical = typename App::H2Link;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<SrvLink, H2Logical>;
  using Cxn = typename Base::Cxn;
  using Active = ZtArray<ZmRef<H2Logical>,
    ZtArrayHeapID<"Zhttp.H2">>;

  SrvLink(
    Hub *app, const ZiCxnInfo &ci, const H2Config &config) :
      Base{app}, m_remoteIP{ci.remoteIP}, m_remotePort{ci.remotePort},
      m_policy{config.policy()}
  {
    Wire::initWire(true, config);
  }
  ~SrvLink() {
    Wire::finalWire();
  }

  using Base::send;
  bool send(ZmRef<ZiIOBuf> buf) {
    ZiIOBuf *last = buf.ptr();
    sendingH1_(last);
    if (!Base::send(ZuMv(buf))) {
      failedH1_(last);
      return false;
    }
    return true;
  }
  bool send(ZmRef<ZiIOBuf> buf, uint64_t generation) {
    ZiIOBuf *last = buf.ptr();
    sendingH1_(last);
    if (!Base::send(ZuMv(buf), generation)) {
      failedH1_(last);
      return false;
    }
    return true;
  }
  void sent(ZmRef<ZiTxBuf> buf, bool ok) {
    ZiIOBuf *sent = buf.ptr();
    auto fn = ZuMv(Transport_::txBufNode(sent)->txComplete);
    Transport_::txBufNode(sent)->txComplete = {};
    if (m_h1TxLast == sent) {
      m_h1TxLast = nullptr;
      m_h1TxOutcome = ok ?
	ResponseOutcome::Success : ResponseOutcome::TxFailed;
    }
    if (!ok) {
      auto i = this->txQueue.iter();
      while (auto queued = i())
	Transport_::txBufNode(queued)->complete(ResponseOutcome::TxFailed);
    }
    Base::sent(ZuMv(buf), ok);
    if (fn)
      fn(ok ? ResponseOutcome::Success : ResponseOutcome::TxFailed);
  }
  void armH1Complete(Transport_::TxCompleteFn fn) {
    m_h1Complete = ZuMv(fn);
  }
  void cancelH1Complete() {
    m_h1Complete = {};
    m_h1TxLast = nullptr;
    m_h1TxReady = false;
  }
  void resetH1() {
    auto fn = ZuMv(m_h1Complete);
    m_h1Complete = {};
    m_h1TxLast = nullptr;
    m_h1TxReady = false;
    if (fn) fn(ResponseOutcome::Reset);
  }
  void finishH1() {
    auto fn = ZuMv(m_h1Complete);
    m_h1Complete = {};
    if (!fn) return;
    if (!m_h1TxReady) {
      fn(ResponseOutcome::TxFailed);
      return;
    }
    m_h1TxReady = false;
    if (!m_h1TxLast) {
      fn(m_h1TxOutcome);
      return;
    }
    Transport_::txBufNode(m_h1TxLast)->txComplete = ZuMv(fn);
    m_h1TxLast = nullptr;
  }
  bool fenceH1(Transport_::TxCompleteFn fn) {
    if (!m_h1TxReady) return false;
    m_h1TxReady = false;
    if (!m_h1TxLast) {
      fn(m_h1TxOutcome);
      return true;
    }
    auto node = Transport_::txBufNode(m_h1TxLast);
    if (node->txComplete) return false;
    node->txComplete = ZuMv(fn);
    m_h1TxLast = nullptr;
    return true;
  }

  void connected(Ztls::Connected info) {
    m_version = TLS_::version(info.alpn, m_policy);
    switch (m_version) {
	case Version::H1:
	m_h1 = new H1Logical{
	  this->app()->user(), this, m_remoteIP, m_remotePort};
	m_h1->connected_(ProfileTraits<H1TLS>::apply({
	  .alpn = info.alpn,
	  .version = uint32_t(info.version),
	  .transport = Transport::TLS,
	  .secure = true
	}));
	break;
      case Version::H2:
	Wire::sendInitial();
	break;
      default:
	Base::disconnect();
	break;
    }
  }
  void h2SettingsReceived() { }
  void disconnected(bool peer) {
    if (m_down) return;
    m_down = true;
    Wire::stopWire();
    if (m_h1) {
      m_h1->disconnected_(peer);
      m_h1 = nullptr;
      down_();
      return;
    }
    if (m_version != Version::H2) {
      down_();
      return;
    }
    Active active;
    Wire::allStreams([&active](auto &entry) {
      if (!entry.notified) active.push(entry.logical);
    });
    Wire::clearStreams([
      link = ZmMkRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &logical: active) logical->disconnected_(peer);
      link->down_();
    });
  }
  int process(Ztls::RxStream &rx) {
    switch (m_version) {
      case Version::H1: return m_h1 ? m_h1->process_(rx) : -1;
      case Version::H2: return Wire::process(rx);
      default: return -1;
    }
  }
  auto txStream() { return Base::txStream(); }
  auto directTxStream() { return Base::txStream_(); }
  auto logicalTx(uint32_t id) {
    return H2_::HeaderBlock<SrvLink>{
      *this, Wire::encoder(), id, Wire::peerFrameSize()};
  }
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }
  void disconnectNative() { Base::disconnect(); }
  void drain() {
    if (m_version != Version::H2 || m_draining || m_down) return;
    m_draining = true;
    Wire::graceful();
  }

  H2_::Stream<H2Logical> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire::canOpenPeerStream(id)) return nullptr;
    ZmRef<H2Logical> logical = new H2Logical{
      this->app()->user(), this, id, m_remoteIP, m_remotePort};
    auto entry = Wire::openPeerStream(id, logical);
    if (!entry) return nullptr;
    logical->connected_(ProfileTraits<H2TLS>::apply({
      .alpn = "h2",
      .version = uint32_t(13),
      .transport = Transport::TLS,
      .secure = true
    }));
    return entry;
  }
  bool h2Closed(uint32_t id) const { return Wire::streamClosed(id); }
  H2::Error::T h2OpenError(uint32_t id) {
    if (h2Closed(id)) return H2::Error::StreamClosed;
    if (Wire::peerStreamIdle(id)) {
      Wire::refusePeerStream(id);
      return H2::Error::RefusedStream;
    }
    return H2::Error::ProtocolError;
  }
  void h2LocalEnd(uint32_t id) {
    if (auto entry = Wire::h2Stream(id)) {
      entry->localEnd = true;
      if (entry->remoteEnd) closeLater_(id, false);
    }
  }
  void h2RemoteEnd(uint32_t id) {
    Wire::peerProcessed(id);
    if (auto entry = Wire::h2Stream(id)) {
      entry->remoteEnd = true;
      if (entry->localEnd) closeLater_(id, true);
    }
  }
  void h2ResetLogical(uint32_t id, H2::Error::T) {
    notify_(id, true);
  }
  void h2Cancel(uint32_t id) {
    rst_(id, H2::Error::Cancel);
    notify_(id, false);
  }
  void h2Goaway_(uint32_t, H2::Error::T) { m_draining = true; }
  void h2PeerSetting(uint16_t key, uint32_t value) {
    if (key == H2::Setting::InitialWindowSize &&
	!Wire::peerInitialWindow(value))
      this->h2Error(H2::Error::FlowControlError);
  }
  void beginStop() {
    if (m_stopping || m_down) return;
    m_stopping = true;
    m_draining = true;
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
  void sendingH1_(ZiIOBuf *last) {
    if (m_version != Version::H1) return;
    m_h1TxLast = last;
    m_h1TxReady = true;
  }
  void failedH1_(ZiIOBuf *last) {
    if (m_h1TxLast == last) m_h1TxLast = nullptr;
    m_h1TxOutcome = ResponseOutcome::TxFailed;
  }

  void closeLater_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->closing) return;
    entry->closing = true;
    this->app()->rxRun([
      link = this, id, peer]() {
      link->notify_(id, peer);
    });
  }
  void rst_(uint32_t id, H2::Error::T error) {
    auto tx = Base::txStream();
    H2_::StreamBytes<decltype(tx)> sink{tx};
    H2::putHeader(sink, {
      .length = 4, .streamID = id, .type = H2::FrameType::RSTStream
    });
    H2::putUInt32(sink, error);
    tx.flush();
  }
  void notify_(uint32_t id, bool peer) {
    auto entry = Wire::h2Stream(id);
    if (!entry || entry->notified) return;
    entry->notified = true;
    auto logical = entry->logical;
    Wire::removeStream(id, [logical = ZuMv(logical), peer]() mutable {
      logical->disconnected_(peer);
    });
  }
  void down_() {
    this->app()->user()->release();
    this->app()->linkDown();
  }

  // shared: stable after construction/connection setup
  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
  int8_t		m_policy = H2Policy::Force;
  // Shared published protocol selection; Rx initializes it before Tx use.
  int8_t		m_version = -1;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<H1Logical>	m_h1;
  bool			m_draining = false;
  bool			m_down = false;
  bool			m_stopping = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  Transport_::TxCompleteFn m_h1Complete;
  ZiIOBuf		*m_h1TxLast = nullptr;
  ResponseOutcome::T	m_h1TxOutcome = ResponseOutcome::Success;
  bool			m_h1TxReady = false;
};

} // namespace TLS_

template <typename App>
class ProtocolServer<App, H2TLS> : public H2_::ServerHub<App> {
public:
  using Base = H2_::ServerHub<App>;
  enum { TLS = 1, Multiplexed = 1 };
  using Base::init;
  using Base::start;

  bool admit(const ZiCxnInfo &) { return true; }
  void release() { }
  template <typename Link>
  void connected(Link &, ConnectedInfo) { }
  template <typename Link>
  void disconnected(Link &, bool) { }
};

namespace H3_ {

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
    return new Link{this, info.peer.ip(), info.peer.port()};
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

  ~ServerStream() {
    completeFence_(ResponseOutcome::Cancelled);
    completeTx_(ResponseOutcome::Cancelled);
  }

  void txComplete(Transport_::TxCompleteFn fn) {
    if (this->txCompleted()) {
      fn(this->txError() == Zquic::StreamError::None ?
	ResponseOutcome::Success : ResponseOutcome::Reset);
      return;
    }
    m_txComplete = ZuMv(fn);
  }
  void txCancel() { m_txComplete = {}; }
  bool txFence(Transport_::TxCompleteFn fn) {
    if (m_txFence) return false;
    m_txFence = ZuMv(fn);
    if (this->txDrained()) completeFence_(ResponseOutcome::Success);
    return true;
  }
  void txDrained_() { completeFence_(ResponseOutcome::Success); }
  void txComplete_(bool ok) {
    if (!ok) completeFence_(ResponseOutcome::Reset);
    completeTx_(ok ? ResponseOutcome::Success : ResponseOutcome::Reset);
  }

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!logical) {
      if (this->resetReceived() || (this->rxComplete() && !rx)) return 0;
      auto link = this->link();
      logical = new Logical{
	link->app()->user(), link, this,
	link->remoteIP, link->remotePort};
      slot = link->logical.length();
      link->logical.push(ZmMkRef(this));
      logical->connected_(ProfileTraits<H3QUIC>::apply({
	.alpn = "h3",
	.version = Zquic::Version1,
	.transport = Transport::QUIC,
	.secure = true
      }));
    }
    int rc = logical->process_(rx);
    if (rc < 0) return 0; // parser has scheduled a stream-local reset
    if (this->rxComplete()) this->link()->remoteEnd(this);
    return rc;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

private:
  void completeFence_(ResponseOutcome::T outcome) {
    auto fn = ZuMv(m_txFence);
    m_txFence = {};
    if (fn) fn(outcome);
  }
  void completeTx_(ResponseOutcome::T outcome) {
    auto fn = ZuMv(m_txComplete);
    m_txComplete = {};
    if (fn) fn(outcome);
  }

  Transport_::TxCompleteFn m_txFence;

public:

  ZmRef<Logical>	logical;
  uint32_t		slot = QueueSlot::Invalid;
  bool		localEnd = false;
  bool		remoteEnd = false;
  bool		closing = false;

private:
  Transport_::TxCompleteFn m_txComplete;
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

  SrvLink(Hub *app, const ZiIP &remoteIP_, uint16_t remotePort_) :
    Base{app}, remoteIP{remoteIP_}, remotePort{remotePort_} { }

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
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    auto link = ZmMkRef(this);
    this->app()->txRun([
      link, limits, extendedConnect, txError = ZuMv(txError)
    ]() mutable {
      bool ok = link->h3Tx.init(limits.txCapacity, limits.txSections);
      typename H3Cxn::LocalStreams streams;
      if (ok)
	streams = H3Cxn::openLocalStreams(*link, txError);
      link->app()->rxRun([
	link = ZuMv(link), limits, extendedConnect, ok,
	streams = ZuMv(streams)
      ]() mutable {
	if (!ok || link->closed()) return;
	auto params = H3::Params().qpackLimits(limits);
	if (!link->h3.openLocal(
	    *link, ZuMv(streams), params, extendedConnect))
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
    for (unsigned i = 0; i < logical.length(); ++i) {
      logical[i]->slot = QueueSlot::Invalid;
      auto owner = ZuMv(logical[i]->logical);
      if (owner) owner->disconnected_(peer);
    }
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
  void streamResetReceived(StreamRef stream, uint64_t error, uint64_t) {
    if (!stream) return;
    (void)stream->process(stream->rxStream());
    closeLater_(stream, true);
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->quicReset(error);
    });
  }
  void streamStopSendingReceived(StreamRef stream, uint64_t error) {
    if (!stream) return;
    (void)stream->process(stream->rxStream());
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->quicReset(error);
    });
  }
  void h3StreamError(StreamRef stream, uint64_t error) {
    closeLater_(stream, false);
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->stop(error);
      stream->quicReset(error);
    });
  }
  void h3RequestRejected(StreamRef stream, uint64_t error) {
    this->app()->txRun([stream = ZuMv(stream), error]() mutable {
      stream->stop(error);
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
  H3::QPackTxTable *qpackTx() { return &h3Tx; }
  void qpackSeed(StreamRef encoder) {
    auto link = ZmMkRef(this);
    this->app()->txRun([link = ZuMv(link), encoder = ZuMv(encoder)]() mutable {
      if (link->h3SeedStateTx != QPackSeedState::Unseeded) return;
      auto result = installQPackSeeds(
	    link->h3Tx, AppHeaderSeeds<ZuDecay<
	      decltype(*link->app()->user())>>::get(*link->app()->user()),
	    [link, &encoder](ZuBSpan bytes) {
	      return link->send(encoder, bytes, false);
	    });
      if (result == QPackSeedResult::Failed) {
	link->disconnect(H3::QPackEncoderError);
	return;
      }
      link->h3SeedStateTx = result == QPackSeedResult::Seeded ?
	QPackSeedState::Seeded : QPackSeedState::Disabled;
    });
  }

private:
  void closeLater_(Stream *stream, bool peer) {
    if (!stream || stream->closing) return;
    stream->closing = true;
    this->app()->rxRun([
      link = ZmMkRef(this), stream = ZmMkRef(stream), peer
    ]() mutable {
      if (!stream->logical) return;
      auto logical = ZuMv(stream->logical);
      link->removeLogical_(stream);
      logical->disconnected_(peer);
    });
  }

  void removeLogical_(Stream *stream) {
    unsigned slot = stream->slot;
    ZmAssert(slot < logical.length() && logical[slot].ptr() == stream);
    unsigned last = logical.length() - 1;
    if (slot != last) {
      logical[slot] = ZuMv(logical[last]);
      logical[slot]->slot = slot;
    }
    logical.length(last);
    stream->slot = QueueSlot::Invalid;
  }

public:
  // Stable for the native connection lifetime
  ZiIP			remoteIP;
  uint16_t		remotePort = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Logical		logical;
  bool			notified = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
  QPackSeedState::T	h3SeedStateTx = QPackSeedState::Unseeded;
};

} // namespace H3_

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
    App *app, NativeLink *native, NativeStream *stream,
    const ZiIP &remoteIP, uint16_t remotePort) :
      m_app{app}, m_native{native}, m_stream{stream},
      m_remoteIP{remoteIP}, m_remotePort{remotePort}
  {
#ifdef ZmObject_DEBUG
    this->ZmObject::debug();
#endif
  }

  App *app() const { return m_app; }
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  const ZiIP &remoteIP() const { return m_remoteIP; }
  uint16_t remotePort() const { return m_remotePort; }
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
      m_stream,
      [](void *ptr, uint64_t error) {
	auto stream = static_cast<NativeStream *>(ptr);
	if (error == H3::RequestCancelled)
	  stream->link()->h3RequestRejected(ZmMkRef(stream), error);
	else
	  stream->h3StreamError(error);
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
  bool active() const { return m_native && m_stream; }
  void txComplete(Transport_::TxCompleteFn fn) {
    if (m_stream) m_stream->txComplete(ZuMv(fn));
    else fn(ResponseOutcome::Cancelled);
  }
  void txCancel() {
    if (m_stream) m_stream->txCancel();
  }
  bool txFence(Transport_::TxCompleteFn fn) {
    return m_stream && m_stream->txFence(ZuMv(fn));
  }
  void finish() {
    if (m_native && m_stream) m_native->finish(m_stream);
  }
  void disconnect() {
    this->streamTxReset();
  }

  void connected_(ConnectedInfo info) {
    m_session.connected(*impl());
    m_app->connected(*impl(), info);
  }
  void disconnected_(bool peer) {
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_app->txRun([
      logical = ZmMkRef(impl()), native = ZmMkRef(m_native),
      stream = ZmMkRef(m_stream)]() mutable {
      (void)native;
      (void)stream;
      logical->disconnectedTx_();
    });
  }
  template <typename Rx>
  int process_(Rx &rx) {
    return m_session.process(*impl(), rx);
  }

private:
  void disconnectedTx_() {
    m_native = nullptr;
    m_stream = nullptr;
  }

  App			*m_app = nullptr;
  NativeLink		*m_native = nullptr;
  NativeStream		*m_stream = nullptr;
  Session		m_session;
  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
};

template <typename App>
class ProtocolServer<App, H3QUIC> : public H3_::ServerHub<App> {
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

ZuDerive(MessageString, ZtBArray<ZtArrayHeapID<"Zhttp.Message">>);

class ServerConfig {
public:
  ServerConfig() { m_tls.policy(H2Policy::Prefer); }

  const ZiIP &localIP() const { return m_localIP; }
  uint16_t port() const { return m_port; }
  unsigned idleTimeout() const { return m_idleTimeout; }
  unsigned maxConnections() const { return m_maxConnections; }
  unsigned maxRequests() const { return m_maxRequests; }
  unsigned altSvcMaxAge() const { return m_altSvcMaxAge; }
  uint64_t retainedBodyMax() const { return m_retainedBodyMax; }
  uint64_t retainedMessageMax() const { return m_retainedMessageMax; }
  uint64_t retainedBytesMax() const { return m_retainedBytesMax; }
  bool tcpEnabled() const { return m_tcpEnabled; }
  bool tlsEnabled() const { return m_tlsEnabled; }
  bool quicEnabled() const { return m_quicEnabled; }
  const TCPConfig &tcpConfig() const { return m_tcp; }
  const H2Config &tlsConfig() const { return m_tls; }
  const QUICConfig &quicConfig() const { return m_quic; }
  QUICConfig quicHubConfig() const {
    QUICConfig config{m_quic};
    // QUIC transport parameters use milliseconds; server idleTimeout uses
    // seconds.  An explicit QUIC value overrides the transport-neutral default.
    if (!config.maxIdleTimeout() && m_idleTimeout)
      config.maxIdleTimeout(uint64_t(m_idleTimeout) * 1000);
    return config;
  }

  ServerConfig &localIP(ZiIP v) { m_localIP = ZuMv(v); return *this; }
  ServerConfig &port(uint16_t v) { m_port = v; return *this; }
  ServerConfig &idleTimeout(unsigned v) {
    m_idleTimeout = v;
    return *this;
  }
  ServerConfig &maxConnections(unsigned v) {
    m_maxConnections = v;
    return *this;
  }
  ServerConfig &maxRequests(unsigned v) {
    m_maxRequests = v;
    return *this;
  }
  ServerConfig &altSvcMaxAge(unsigned v) {
    m_altSvcMaxAge = v;
    return *this;
  }
  ServerConfig &retainedBodyMax(uint64_t v) {
    m_retainedBodyMax = v;
    return *this;
  }
  ServerConfig &retainedMessageMax(uint64_t v) {
    m_retainedMessageMax = v;
    return *this;
  }
  ServerConfig &retainedBytesMax(uint64_t v) {
    m_retainedBytesMax = v;
    return *this;
  }
  ServerConfig &tcp(TCPConfig v = {}) {
    m_tcp = ZuMv(v);
    m_tcpEnabled = true;
    return *this;
  }
  ServerConfig &tls(H2Config v) {
    m_tls = ZuMv(v);
    m_tlsEnabled = true;
    return *this;
  }
  ServerConfig &quic(QUICConfig v) {
    m_quic = ZuMv(v);
    m_quicEnabled = true;
    return *this;
  }

private:
  ZiIP		m_localIP;
  TCPConfig	m_tcp;
  H2Config	m_tls;
  QUICConfig	m_quic;
  unsigned	m_idleTimeout = 0;
  unsigned	m_maxConnections = 0;
  unsigned	m_maxRequests = uint32_t(-1);
  unsigned	m_altSvcMaxAge = 86400;
  uint64_t	m_retainedBodyMax = uint32_t(-1);
  uint64_t	m_retainedMessageMax = uint32_t(-1);
  uint64_t	m_retainedBytesMax = uint64_t(-1);
  uint16_t	m_port = 0;
  bool		m_tcpEnabled = false;
  bool		m_tlsEnabled = false;
  bool		m_quicEnabled = false;
};


// Application response Builder base.  The application ResBuilder_ derives from
// ResBuilder and ZmObject.  The final application ResBuilder is
// ResBuilderQ::Node, an intrusive ZmList::Node which publicly derives the
// application data type.
// Each node represents exactly one response and is never reset or repurposed.
struct ResBuilder : public Builder {
  // Original request method, used to suppress forbidden response bodies.
  Method::T method() const { return Method::GET; }
  // Disconnect after successful transmission when true.
  bool close() const { return false; }
};

template <typename App_>
class Server : public ZmEngine<Server<App_>> {
public:
  using Engine = ZmEngine<Server<App_>>;
  using App = App_;
  using AppParser = typename App::Parser;
  using ReqHeaders = typename AppParser::Headers;
  ZuAssert((ZuIs_<AppParser, Zhttp::Parser>{}),
    "Zhttp::Server requires App::Parser to derive from Zhttp::Parser");
  using ResBuilderQ = typename App::ResBuilderQ;
  using ResBuilder = typename ResBuilderQ::Node;
  using ResBuilder_ = typename ResBuilderQ::T;
  ZuAssert((ZuIs_<ResBuilder, ResBuilder_>{}),
    "Zhttp::Server requires ResBuilderQ::Node to derive from ResBuilder_");
  ZuAssert((ZuIs_<ResBuilder_, ZmObject>{}),
    "Zhttp::Server requires ResBuilder_ to derive from ZmObject");
  ZuAssert((ZuIs_<ResBuilder_, Zhttp::ResBuilder>{}),
    "Zhttp::Server requires ResBuilder_ to derive from Zhttp::ResBuilder");
  using Engine::running;
  using Engine::start;
  using Engine::state;
  using Engine::stop;
  using Engine::stopping;

private:
  template <typename Protocol> struct Link;
  struct TLSHub;
  struct TLSH1Link;
  struct TLSH2Link;

  template <typename Profile>
  using ProfileLink = ZuIf<ZuIsSame<Profile, H1TLS>{}, TLSH1Link,
    ZuIf<ZuIsSame<Profile, H2TLS>{}, TLSH2Link, Link<Profile>>>;

  template <typename Protocol> struct Session;
  template <typename Protocol> struct Hub;
  template <typename Profile> struct Parser;
  template <typename Profile, typename Link_> struct Responses;

  using BodyChunkFn = ZmFn<void(ZmRef<ZiIOBuf>, bool),
    ZmFnHeapID<"Zhttp.Server.BodyChunk">>;
  using BodyCancelFn = ZmFn<void(),
    ZmFnHeapID<"Zhttp.Server.BodyCancel">>;

  struct BodyTask {
    BodyCancelFn cancel;
  };
  using BodyTaskQ = ZmList<BodyTask,
    ZmListNode<BodyTask, ZmListHeapID<"Zhttp.Server.BodyTask">>>;

  struct Stats {
    ZmAtomic<unsigned> activeConnections = 0;
    ZmAtomic<unsigned> activeRequests = 0;
    ZmAtomic<unsigned> queuedResponses = 0;
    ZmAtomic<uint64_t> retainedBytes = 0;
    ZmAtomic<uint64_t> serverFaults = 0;
    ZmAtomic<uint64_t> rejectedRequests = 0;
    ZmAtomic<uint64_t> parseFailures = 0;
    ZmAtomic<uint64_t> bodyFailures = 0;
    ZmAtomic<uint64_t> responseBuildFailures = 0;
    ZmAtomic<uint64_t> transportFailures = 0;
  };

  template <typename Builder, typename = void>
  struct HasAsyncBody : public ZuFalse { };
  template <typename Builder>
  struct HasAsyncBody<Builder, decltype(
    ZuDeclVal<Builder &>().next(
      unsigned{}, ZuDeclVal<BodyChunkFn>()), void())> :
      public ZuTrue { };

  template <typename Profile>
  struct Parser :
    public MessageTraits<Profile>::template RequestParser<
      Parser<Profile>, ReqHeaders> {
    using Base = typename MessageTraits<Profile>::template RequestParser<
      Parser, ReqHeaders>;
    using State = typename Base::State;

    void bind(Server *server_, ProfileLink<Profile> *link_) {
      server = server_;
      link = link_;
      Base::bodyMax(server->m_config.retainedBodyMax());
    }

    void begin() {
      appParser.init(*server->m_app);
      active = true;
      notified = false;
    }

    void reset() {
      Base::reset();
      active = false;
      notified = false;
    }

    bool operation(Method::T method, const Target &target) {
      return parser_().operation(method, target);
    }
    template <typename Key>
    void header(Zhttp::FieldSection::T section, ZuBSpan value) {
      parser_().template header<Key>(section, value);
    }
    template <typename Key, typename Value>
    void header(Zhttp::FieldSection::T section) {
      parser_().template header<Key, Value>(section);
    }
    void header(
	Zhttp::FieldSection::T section, ZuBSpan key, ZuBSpan value) {
      if constexpr (Fields::HasRuntime<AppParser>{})
	parser_().header(section, key, value);
    }
    void bodyInfo(BodyType::T type, uint64_t length) {
      parser_().bodyInfo(type, length);
    }
    void status(unsigned) { }
    template <typename Rx>
    bool body(Rx &rx) {
      return parser_().body(rx);
    }
    void complete(typename State::T state) {
      notify(state == State::Complete);
    }
    void notify(bool ok) {
      if (!active || notified) return;
      notified = true;
      parser_().complete(link, ok);
      parser_().reset();
    }
    void finish() {
      active = false;
      notified = false;
    }

  private:
    AppParser &parser_() { return appParser; }

  public:
    Server		*server = nullptr;
    ProfileLink<Profile>	*link = nullptr;
    AppParser	appParser;
    bool	active = false;
    bool	notified = false;
  };

  template <typename Profile, typename Builder>
  struct ResponseOps {
    using Headers = typename Builder::Headers;

    ResponseOps(
	Server *server_, Builder &builder_,
	bool rejectContentLength_ = false) :
      server{server_}, builder{&builder_},
      rejectContentLength{rejectContentLength_} { }

    unsigned status() const { return builder->status(); }
    template <typename Key, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (rejectContentLength) return;
      if constexpr (HasBuilderHeader<Builder, Key, L &&>{})
	builder->template header<Key>(ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      builder->header([&l]<typename K, typename V>(K &&k, V &&v) {
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
      if (server->m_altSvc) l("alt-svc", server->m_altSvc);
    }
    template <typename Key>
    void headerSpan(ZuSpan<uint8_t> span) {
      spans.template record<Key>(span);
    }
    template <typename Key>
    void headerOffset(uint64_t offset, unsigned length) {
      spans.template recordOffset<Key>(offset, length);
    }
    void headerBase(uint8_t *base) { spans.resolve(base); }
    void patch() { spans.patch(*builder); }
    uint64_t contentLength() const { return produced; }
    Builder &appBuilder() { return *builder; }
    template <typename Emit>
    void emitBody(Emit &&emit) {
      if constexpr (HasBuilderBody<Builder, Emit &&>{})
	builder->body(ZuFwd<Emit>(emit));
    }

    Server		*server = nullptr;
    Builder		*builder = nullptr;
    HeaderSpans<Headers> spans;
    uint64_t		produced = 0;
    bool		rejectContentLength = false;
  };

  template <
    typename Profile, typename Builder,
    bool HasBody, bool Streaming>
  struct ResponseTx :
    public MessageTraits<Profile>::template Response<
      ResponseTx<Profile, Builder, HasBody, Streaming>,
      typename Builder::Headers, HasBody, Streaming>,
    public ResponseOps<Profile, Builder> {
    using Base = typename MessageTraits<Profile>::template Response<
      ResponseTx, typename Builder::Headers, HasBody, Streaming>;
    using Ops = ResponseOps<Profile, Builder>;
    static constexpr unsigned HdrBufSize = Builder::HdrBufSize;

    ResponseTx(
	Server *server, Builder &builder,
	bool rejectContentLength = false) :
      Ops{server, builder, rejectContentLength} { }

    bool streamResponse() const { return false; }
    using Ops::contentLength;
    using Ops::emitBody;
    using Ops::header;
    using Ops::status;
  };

  static bool bodyAllowed_(Method::T method, unsigned status) {
    if (method == Method::HEAD || (status >= 100 && status < 200) ||
	status == 204 || status == 304)
      return false;
    return method != Method::CONNECT || status < 200 || status >= 300;
  }

  static bool contentLengthForbidden_(Method::T method, unsigned status) {
    return (status >= 100 && status < 200) || status == 204 ||
      (method == Method::CONNECT && status >= 200 && status < 300);
  }

  template <typename Profile, typename Link_, typename Builder>
  struct ServerTxOps {
    Server	*server;
    Link_	*link_;
    uint64_t	*retainedBytes;

    Link_ &link() { return *link_; }

    uint64_t fixedBodyMax() const { return server->fixedBodyMax_(); }
    uint64_t retainedMax() const {
      uint64_t n = server->retainedAvailable_();
      if (n > server->m_config.retainedMessageMax())
	n = server->m_config.retainedMessageMax();
      return n;
    }
    void headers() { }
    template <bool> void produced(uint64_t) { }
    bool empty(Builder &builder) {
      ResponseTx<Profile, Builder, false, false> empty{
	server, builder};
      auto tx = link_->transmit(empty);
      empty.begin(tx);
      empty.finish(tx);
      link_->finish();
      return true;
    }
    template <bool> bool fail() { return false; }
    bool complete(uint64_t n) {
      return server->retainResponse_(*retainedBytes, n);
    }
  };

  template <typename Profile, typename Link_, typename Builder>
  bool sendResponse_(
      Link_ &link, Builder &builder, uint64_t &retainedBytes) {
    unsigned status = builder.status();
    auto policy = builder.bodyPolicy();
    Method::T method = builder.method();
    if (!BodyPolicy::hasBody(policy) || !bodyAllowed_(method, status)) {
      ResponseTx<Profile, Builder, false, false> response{
	this, builder, contentLengthForbidden_(method, status)};
      auto tx = link.transmit(response);
      response.begin(tx);
      response.finish(tx);
      link.finish();
      return true;
    }
    if (BodyPolicy::streaming(policy)) {
      ResponseTx<Profile, Builder, true, true> response{this, builder};
      ServerTxOps<Profile, Link_, Builder> ops{
	this, &link, &retainedBytes};
      MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
      return tx.streaming(response, BodyPolicy::optional(policy));
    }
    ResponseTx<Profile, Builder, true, false> response{this, builder};
    ServerTxOps<Profile, Link_, Builder> ops{
      this, &link, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.fixed(response, BodyPolicy::optional(policy));
  }

  typename BodyTaskQ::Node *addBodyTask_(BodyCancelFn fn) {
    return m_bodyTasks.push(BodyTask{ZuMv(fn)});
  }

  void delBodyTask_(typename BodyTaskQ::Node *task) {
    auto node = m_bodyTasks.delNode(task);
    if (node) node->cancel = {};
  }

  void cancelBodies_() {
    while (auto task = m_bodyTasks.shift()) {
      auto fn = ZuMv(task->cancel);
      task->cancel = {};
      if (fn) fn();
    }
  }

  void bodyResolved_() {
    ZmAssert(m_bodyPending);
    if (--m_bodyPending || !Engine::stopping()) return;
    stopTransports_();
  }

  template <typename Profile, typename Link_, typename Heap>
  struct AsyncBody_ : public Heap, public ZmObject {
    using Self = AsyncBody_<Profile, Link_, Heap>;
    using Tx = ResponseTx<Profile, ResBuilder_, true, true>;

    AsyncBody_(
	Server *server_, ZmRef<Link_> link_, ZmRef<ResBuilder> response_) :
      server{server_}, link{ZuMv(link_)}, appResponse{ZuMv(response_)},
      response{server_, appResponse->data()} { }

    bool start(ZmRef<Self> self) {
      auto tx = link->transmit(response);
      response.begin(tx);
      task = server->addBodyTask_(BodyCancelFn{
	[self_ = self]() mutable { self_->cancel_(); }});
      link->responseCancel_(BodyCancelFn{
	[self = ZuMv(self)]() mutable { self->cancel_(); }});
      next_();
      return true;
    }

    void next_() {
      if (cancelled) return;
      uint64_t available = server->retainedAvailable_();
      if (available > server->m_config.retainedMessageMax())
	available = server->m_config.retainedMessageMax();
      if (available > server->m_config.retainedBodyMax())
	available = server->m_config.retainedBodyMax();
      if (available > ZiIOBuf_DefltSize) available = ZiIOBuf_DefltSize;
      if (available > uint32_t(-1)) available = uint32_t(-1);
      if (!available || !link->responseRetain_(available)) {
	fail_(ResponseOutcome::BuildFailed);
	return;
      }
      reserved = available;
      ++server->m_bodyPending;
      appResponse->data().next(unsigned(available), BodyChunkFn{
	[self = ZmRef<Self>{this}](ZmRef<ZiIOBuf> buf, bool final) mutable {
	  self->chunk_(ZuMv(buf), final);
	}});
    }

    void chunk_(ZmRef<ZiIOBuf> buf, bool final) {
      server->txRun_([
	self = ZmRef<Self>{this}, buf = ZuMv(buf), final]() mutable {
	self->chunkTx_(ZuMv(buf), final);
      });
    }

    void chunkTx_(ZmRef<ZiIOBuf> buf, bool final) {
      server->bodyResolved_();
      if (cancelled) return;
      if (!link->active()) {
	fail_(ResponseOutcome::Reset);
	return;
      }
      if (!buf || !buf->length || buf->length > reserved) {
	fail_(ResponseOutcome::BuildFailed);
	return;
      }
      uint64_t unused = reserved - buf->length;
      reserved = 0;
      if (unused) link->responseRelease_(unused);
      auto tx = link->transmit(response);
      auto body = response.body(tx);
      body << buf->cspan();
      body.flush();
      response.produced += buf->length;
      if (!body.valid()) {
	fail_(ResponseOutcome::TxFailed);
	return;
      }
      if (final) {
	response.finish(tx);
	finishTask_();
	link->finish();
	return;
      }
      if (!link->txFence(Transport_::TxCompleteFn{
	  [self = ZmRef<Self>{this}](ResponseOutcome::T outcome) mutable {
	    self->fenced_(outcome);
	  }}))
	fail_(ResponseOutcome::TxFailed);
    }

    void fenced_(ResponseOutcome::T outcome) {
      server->txRun_([self = ZmRef<Self>{this}, outcome]() mutable {
	if (self->cancelled) return;
	if (outcome == ResponseOutcome::Success) self->next_();
	else self->fail_(outcome);
      });
    }

    void fail_(ResponseOutcome::T outcome) {
      if (cancelled) return;
      cancelled = true;
      finishTask_();
      link->txCancel();
      link->responseDone_(appResponse.ptr(), outcome);
    }

    void cancel_() {
      if (cancelled) return;
      fail_(ResponseOutcome::Cancelled);
    }

    void finishTask_() {
      link->responseCancel_({});
      if (!task) return;
      server->delBodyTask_(task);
      task = nullptr;
    }

    Server			*server;
    ZmRef<Link_>		link;
    ZmRef<ResBuilder>		appResponse;
    Tx				response;
    typename BodyTaskQ::Node	*task = nullptr;
    uint64_t			reserved = 0;
    bool			cancelled = false;
  };

  template <typename Profile, typename Link_>
  using AsyncBody = AsyncBody_<Profile, Link_,
    ZmHeap<"Zhttp.Server.AsyncBody",
      AsyncBody_<Profile, Link_, ZuVoid>>>;

  template <typename Profile, typename Link_>
  bool startResponse_(
      ZmRef<Link_> link, ZmRef<ResBuilder> response,
      uint64_t &retainedBytes) {
    auto policy = response->data().bodyPolicy();
    if constexpr (HasAsyncBody<ResBuilder_>{}) {
      if (BodyPolicy::streaming(policy)) {
	using State = AsyncBody<Profile, Link_>;
	ZmRef<State> state =
	  new State{this, ZuMv(link), ZuMv(response)};
	return state->start(state);
      }
    }
    return sendResponse_<Profile>(
      *link, response->data(), retainedBytes);
  }

  template <typename Profile, typename Link_>
  struct Responses {
    auto impl() { return static_cast<Link_ *>(this); }
    auto impl() const { return static_cast<const Link_ *>(this); }

    void send(ZmRef<ResBuilder> response) {
      if (!response) return;
      auto link = ZmMkRef(impl());
      auto server = link->app()->server;
      ++server->m_stats.queuedResponses;
      link->app()->txRun([
	link = ZuMv(link), response = ZuMv(response)]() mutable {
	link->responseSend_(ZuMv(response));
      });
    }

    void responseDisconnected_() {
      auto link = ZmMkRef(impl());
      link->app()->txRun([link = ZuMv(link)]() mutable {
	link->responseCancelAll_();
      });
    }

  private:
    friend Server;
    template <typename P, typename L, typename H> friend struct AsyncBody_;

    void responseSend_(ZmRef<ResBuilder> response) {
      auto server = impl()->app()->server;
      server->assertTx_();
      if (!impl()->active()) {
	--server->m_stats.queuedResponses;
	return;
      }
      m_responses.pushNode(ZuMv(response));
      responseNext_();
    }

    void responseNext_() {
      if (m_response || !impl()->active()) return;
      m_response = m_responses.shift();
      if (!m_response) return;
      auto server = impl()->app()->server;
      --server->m_stats.queuedResponses;
      m_close = m_response->data().close();
      auto response = m_response;
      auto link = ZmMkRef(impl());
      impl()->txComplete(Transport_::TxCompleteFn{
	[link = ZuMv(link), response_ = response.ptr()](
	    ResponseOutcome::T outcome) mutable {
	  link->app()->txRun([
	    link = ZuMv(link), response_, outcome]() mutable {
	    link->responseDone_(response_, outcome);
	  });
	}});
      if (server->template startResponse_<Profile>(
	    ZmMkRef(impl()), ZuMv(response), m_retainedBytes))
	return;
      impl()->txCancel();
      responseDone_(m_response.ptr(), ResponseOutcome::BuildFailed);
    }

    void responseDone_(
	ResBuilder *response, ResponseOutcome::T outcome) {
      if (m_response.ptr() != response) return;
      auto server = impl()->app()->server;
      responseCancel_({});
      responseRelease_(m_retainedBytes);
      m_response = nullptr;
      switch (outcome) {
	case ResponseOutcome::BuildFailed:
	  ++server->m_stats.responseBuildFailures;
	  break;
	case ResponseOutcome::TxFailed:
	case ResponseOutcome::Reset:
	  ++server->m_stats.transportFailures;
	  break;
	default:
	  break;
      }
      if (outcome != ResponseOutcome::Success || m_close) {
	responseCancelPending_();
	impl()->disconnect();
	return;
      }
      responseNext_();
    }

    void responseCancelPending_() {
      auto server = impl()->app()->server;
      while (m_responses.shift())
	--server->m_stats.queuedResponses;
    }

    void responseCancelAll_() {
      auto fn = ZuMv(m_cancel);
      m_cancel = {};
      if (fn) fn();
      responseCancelPending_();
      if (m_response) {
	impl()->txCancel();
	responseRelease_(m_retainedBytes);
	m_response = nullptr;
      }
    }

    void responseCancel_(BodyCancelFn fn) {
      m_cancel = ZuMv(fn);
    }

    bool responseRetain_(uint64_t n) {
      return impl()->app()->server->retainResponse_(m_retainedBytes, n);
    }

    void responseRelease_(uint64_t n) {
      if (!n) return;
      auto server = impl()->app()->server;
      ZmAssert(m_retainedBytes >= n);
      ZmAssert(server->m_stats.retainedBytes.load_() >= n);
      m_retainedBytes -= n;
      server->m_stats.retainedBytes -= n;
    }

    ResBuilderQ		m_responses;
    ZmRef<ResBuilder>	m_response;
    BodyCancelFn	m_cancel;
    uint64_t		m_retainedBytes = 0;
    bool		m_close = false;
  };

  template <typename Profile>
  struct Session :
    public ServerSession<
      Session<Profile>, Parser<Profile>, MessageTraits<Profile>> {
    using Message = MessageTraits<Profile>;
    using Parser_ = Parser<Profile>;
    using Base = ServerSession<Session, Parser_, Message>;
    using Base::parser;

    template <typename Link_, typename Rx>
    int process(Link_ &link, Rx &rx) {
      if (terminal) return 0;
      auto server = link.app()->server;
      if (!parser.link) parser.bind(server, &link);
      if (!parser.active) {
	if constexpr (Message::ID == Version::H1)
	  if (!rx.length()) return 0;
	if (!server->admitRequest_()) return -1;
	parser.begin();
      }
      return Base::process(link, rx);
    }

    template <typename Link_>
    void disconnected(Link_ &, bool) {
      if (!parser.active) return;
      parser.notify(false);
      finish_();
    }

    template <typename Link_>
    int error(Link_ &, Parser_ &parser_) {
      const RequestError &error = parser_.error();
      if (error.code == RequestErrorCode::BodyRejected)
	++parser_.server->m_stats.bodyFailures;
      else
	++parser_.server->m_stats.parseFailures;
      parser_.notify(false);
      finish_();
      terminal = true;
      return -1;
    }

    template <typename Link_>
    int request(Link_ &, Parser_ &) {
      finish_();
      return 1;
    }

  private:
    void finish_() {
      if (!parser.active) return;
      --parser.server->m_stats.activeRequests;
      parser.finish();
    }

    bool terminal = false;
  };

  template <typename Profile>
  struct Hub : public ProtocolServer<Hub<Profile>, Profile> {
    using HTTP = ProfileTraits<Profile>;
    using Link_ = typename Server::template Link<Profile>;
    using Link = Link_;
    Server *server = nullptr;

    Hub(Server *server_) : server{server_} { }
    ZiIP localIP() const { return server->m_config.localIP(); }
    unsigned localPort() const { return server->m_config.port(); }
    unsigned idleTimeout() const { return server->m_config.idleTimeout(); }
    template <typename Info>
    bool admit(const Info &) { return server->admit(); }
    template <typename LinkT>
    void connected(LinkT &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_app->connected(HTTP::Transport::ID);
    }
    template <typename LinkT>
    void disconnected(LinkT &link, bool) {
      link.responseDisconnected_();
    }
    void release() {
      server->release(HTTP::Transport::ID);
    }
    template <typename Info>
    void listening(const Info &info) {
      server->m_app->listening(HTTP::Transport::ID, info.port);
    }
    void listening() {
      server->m_app->listening(
	HTTP::Transport::ID, server->m_config.port());
    }
    void listenFailed(bool transient) {
      server->m_app->listenFailed(HTTP::Transport::ID, transient);
      server->failServer_();
    }
  };

  template <typename Profile>
  struct Link :
    public ServerLink<
      Hub<Profile>, Link<Profile>, Profile, Session<Profile>>,
    public Responses<Profile, Link<Profile>> {
    using Base = ServerLink<
      Hub<Profile>, Link, Profile, Session<Profile>>;
    using Base::Base;
    using Responses<Profile, Link>::send;
    auto send(ZmRef<ZiIOBuf> buf) { return Base::send(ZuMv(buf)); }
  };

  struct TLSHub : public TLS_::ServerHub<TLSHub> {
    using H1Link = TLSH1Link;
    using H2Link = TLSH2Link;
    using ResponseHeaders = typename ResponseHeaderSets<App>::T;
    Server *server = nullptr;

    TLSHub(Server *server_) : server{server_} { }
    ZiIP localIP() const { return server->m_config.localIP(); }
    unsigned localPort() const { return server->m_config.port(); }
    unsigned idleTimeout() const {
      return server->m_config.idleTimeout();
    }
    bool admit(const ZiCxnInfo &) { return server->admit(); }
    const HPackSeedPlans &hpackSeedPlans() const {
      return server->hpackSeedPlans();
    }
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_app->connected(Transport::TLS);
    }
    template <typename Link_>
    void disconnected(Link_ &link, bool) {
      link.responseDisconnected_();
    }
    void release() { server->release(Transport::TLS); }
    void listening(const ZiListenInfo &info) {
      server->m_app->listening(Transport::TLS, info.port);
    }
    void listenFailed(bool transient) {
      server->m_app->listenFailed(Transport::TLS, transient);
      server->failServer_();
    }
  };

  struct TLSH1Link :
    public TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>,
    public Responses<H1TLS, TLSH1Link> {
    using Base = TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
    using Responses<H1TLS, TLSH1Link>::send;
    auto send(ZmRef<ZiIOBuf> buf) { return Base::send(ZuMv(buf)); }
  };

  struct TLSH2Link :
    public H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>,
    public Responses<H2TLS, TLSH2Link> {
    using Base = H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
    using Responses<H2TLS, TLSH2Link>::send;
    auto send(ZmRef<ZiIOBuf> buf) { return Base::send(ZuMv(buf)); }
  };

public:
  Server() : m_tcp{this}, m_tls{this}, m_quic{this} { }

  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  bool init(
    const HubConfig &hub, ServerConfig config, App *app) {
    return Engine::lock(ZmEngineState::Stopped, [&]() {
      return init_(hub, ZuMv(config), app);
    });
  }

private:
  friend Engine;

  bool init_(
    const HubConfig &hub, ServerConfig config, App *app) {
    if (!app || !config.port() || !hub.mx()) return false;
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = ZuMv(config);
    m_app = app;
    if (m_config.tlsEnabled()) {
      using Sets = typename ResponseHeaderSets<App>::T;
      uint32_t capacity = m_config.tlsConfig().hpackTxCapacity();
      ZuUnroll::all<Sets>([this, capacity]<typename Headers>() {
	m_hpackSeeds.template add<Headers>(capacity);
      });
    }
    if (m_config.quicEnabled()) {
      const auto &quic = m_config.quicHubConfig();
      auto params = H3::Params().qpackLimits({
	quic.qpackRxCapacity(), quic.qpackTxCapacity(),
	quic.qpackRxBlocked(), quic.qpackTxSections()});
      using Sets = typename ResponseHeaderSets<App>::T;
      ZuUnroll::all<Sets>([this, &params, &quic]<typename Headers>() {
	m_qpackSeeds.template add<Headers>(
	  params, quic.qpackTxCapacity());
      });
    }
    m_failed = false;
    m_altSvc.null();
    if (m_config.tlsEnabled() && m_config.quicEnabled() &&
	m_config.altSvcMaxAge())
      m_altSvc << "h3=\":" << m_config.port() << "\"; ma=" <<
	m_config.altSvcMaxAge();
    if (m_config.tcpEnabled() &&
	!m_hubs.init(m_tcp, hub, m_config.tcpConfig()))
      return false;
    if (m_config.tlsEnabled() &&
	!m_hubs.init(m_tls, hub, m_config.tlsConfig()))
      return false;
    if (m_config.quicEnabled() &&
	!m_hubs.init(m_quic, hub, m_config.quicHubConfig()))
      return false;
    return m_hubs.count();
  }

  void start_() {
    m_admitRequests = true;
    m_hubs.start([this](bool ok) {
      if (!ok) m_admitRequests = false;
      Engine::started(ok);
    });
  }

  void stop_() {
    m_admitRequests = false;
    rxRun_([this]() {
      txRun_([this]() {
	cancelBodies_();
	if (!m_bodyPending) stopTransports_();
      });
    });
  }

  void stopTransports_() {
    m_hubs.stop([this](bool ok) {
      txRun_([this, ok]() {
	rxRun_([this, ok]() {
	  ZmAssert(!m_stats.activeRequests.load_());
	  ZmAssert(!m_stats.queuedResponses.load_());
	  ZmAssert(!m_stats.retainedBytes.load_());
	  Engine::stopped(ok);
	});
      });
    });
  }

public:
  void final() {
    if (m_mx) (void)Engine::stop();
    ZmAssert(!m_stats.activeConnections.load_());
    ZmAssert(!m_stats.activeRequests.load_());
    ZmAssert(!m_stats.queuedResponses.load_());
    ZmAssert(!m_stats.retainedBytes.load_());
    ZmAssert(m_bodyTasks.empty_());
    ZmAssert(!m_bodyPending);
    m_hubs.final();
    m_altSvc.null();
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
    m_app = nullptr;
    m_config = {};
  }

  bool failServer_() {
    ++m_stats.serverFaults;
    m_failed = true;
    Engine::stop({});
    return false;
  }
  bool ok() const { return !m_failed.load_(); }
  unsigned activeConnections() const {
    return m_stats.activeConnections.load_();
  }
  unsigned activeRequests() const { return m_stats.activeRequests.load_(); }
  unsigned queuedResponses() const {
    return m_stats.queuedResponses.load_();
  }
  uint64_t retainedBytes() const { return m_stats.retainedBytes.load_(); }
  uint64_t serverFaults() const { return m_stats.serverFaults.load_(); }
  uint64_t rejectedRequests() const {
    return m_stats.rejectedRequests.load_();
  }
  uint64_t parseFailures() const { return m_stats.parseFailures.load_(); }
  uint64_t bodyFailures() const { return m_stats.bodyFailures.load_(); }
  uint64_t responseBuildFailures() const {
    return m_stats.responseBuildFailures.load_();
  }
  uint64_t transportFailures() const {
    return m_stats.transportFailures.load_();
  }
  unsigned hubCount() const { return m_hubs.count(); }
  const HeaderSeeds &qpackSeeds() const { return m_qpackSeeds.entries(); }
  const HPackSeedPlans &hpackSeedPlans() const {
    return m_hpackSeeds.plans();
  }

#ifdef Zquic_DEBUG
  void printQUICDiag() { m_quic.printDiag(); }
#endif

private:
  template <typename Link_>
  void registerTxError_(Link_ &link) {
    link.txErrorFn(ZiTxErrorFn{[this](ZeException &e) {
      if (!m_txErrorFn) return true;
      return m_txErrorFn(e);
    }});
  }

  template <typename L>
  void rxRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_rxThread);
  }
  template <typename L>
  void txRun_(L &&l) {
    m_mx->run(ZuFwd<L>(l), m_txThread);
  }

  void assertTx_() const {
    ZmAssert(ZmSelf() && ZmSelf()->sid() == int(m_txThread));
  }

  uint64_t fixedBodyMax_() const {
    uint64_t n = m_config.retainedBodyMax();
    if (n > m_config.retainedMessageMax())
      n = m_config.retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
  }

  uint64_t retainedAvailable_() const {
    uint64_t max = m_config.retainedBytesMax();
    uint64_t used = m_stats.retainedBytes.load_();
    return used < max ? max - used : 0;
  }

  bool retainResponse_(uint64_t &retainedBytes, uint64_t n) {
    if (n > retainedAvailable_()) return false;
    m_stats.retainedBytes += n;
    retainedBytes += n;
    return true;
  }

  bool admit() {
    unsigned active = ++m_stats.activeConnections;
    if (!m_config.maxConnections() ||
	active <= m_config.maxConnections())
      return true;
    --m_stats.activeConnections;
    ++m_stats.rejectedRequests;
    return false;
  }

  bool admitRequest_() {
    if (!m_admitRequests.load_()) {
      ++m_stats.rejectedRequests;
      return false;
    }
    unsigned active = ++m_stats.activeRequests;
    if (active <= m_config.maxRequests()) return true;
    --m_stats.activeRequests;
    ++m_stats.rejectedRequests;
    return false;
  }

  void release(Transport::T transport) {
    --m_stats.activeConnections;
    m_app->disconnected(transport);
  }

  ZiMultiplex		*m_mx = nullptr;
  ServerConfig		m_config;
  MessageString		m_altSvc;
  App			*m_app = nullptr;
  Hub<H1TCP>		m_tcp;
  TLSHub		m_tls;
  Hub<H3QUIC>		m_quic;
  HeaderSeedCatalog	m_qpackSeeds;
  HPackSeedCatalog	m_hpackSeeds;
  Hubs			m_hubs;
  ZiTxErrorFn		m_txErrorFn;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;

  Stats			m_stats;
  ZmAtomic<unsigned>	m_admitRequests = 0;
  ZmAtomic<unsigned>	m_failed = 0;

  alignas(Zm::CacheLineSize)
  BodyTaskQ		m_bodyTasks;
  unsigned		m_bodyPending = 0;
};

} // namespace Zhttp

#endif /* ZhttpServer_HH */
