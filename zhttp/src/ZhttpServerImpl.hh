//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTP server

#ifndef ZhttpServerImpl_HH
#define ZhttpServerImpl_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuObjectTraits.hh>
#include <zlib/ZuRef.hh>

#include <zlib/ZmBlock.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/ZhttpServer.hh>
#include "ZhttpWire.hh"
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpH2Hub.hh>
#include <zlib/ZhttpH3.hh>
#include "ZhttpNative.hh"

namespace Zhttp {

struct StreamState {
  enum {
    Initial, InitialWriting, InitialOpen, InitialTerminal,
    Writing, Open, Terminal
  };
};

namespace Server_ {

template <typename App,
  typename = decltype(ZuDeclVal<App * &>()->connected(ZuDeclVal<Session &>()), void())>
void connected(App *app, Session session, int)
{
  app->connected(session);
}
template <typename App>
void connected(App *, Session, ...) { }

template <typename App,
  typename = decltype(ZuDeclVal<App * &>()->disconnected(ZuDeclVal<Session &>()), void())>
void disconnected(App *app, Session session, int)
{
  app->disconnected(session);
}
template <typename App>
void disconnected(App *, Session, ...) { }

inline bool streamTxThread(int txThread)
{
  bool valid = ZmSelf() &&ZmSelf()->sid() == txThread;
  ZiAssert(valid, "Zhttp", (), "response stream called outside its Tx shard",
    return false);
  return valid;
}

inline bool streamEnter(int8_t &state, bool &beginHeaders)
{
  switch (state) {
    case StreamState::Initial:
      state = StreamState::InitialWriting;
      beginHeaders = true;
      return true;
    case StreamState::InitialOpen:
      state = StreamState::InitialWriting;
      beginHeaders = false;
      return true;
    case StreamState::Open:
      state = StreamState::Writing;
      beginHeaders = false;
      return true;
    case StreamState::InitialWriting:
    case StreamState::Writing:
      ZiAssert(false, "Zhttp", (), "reentrant stream emission", return false);
      return false;
    case StreamState::InitialTerminal:
    case StreamState::Terminal:
      return false;
    default:
      ZiAssert(false, "Zhttp", (), "invalid stream state", return false);
      return false;
  }
}

inline bool streamContentLength()
{
  ZiAssert(false, "Zhttp", (),
    "streaming response supplied Content-Length", return false);
  return false;
}

inline bool streamBodyAbsent(bool optional)
{
  ZiAssert(optional, "Zhttp", (),
    "required streaming body emitted no writer", return false);
  return optional;
}

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

inline bool bodyAllowed_(Method::T method, unsigned status)
{
  if (method == Method::HEAD || (status >= 100 && status < 200) ||
      status == 204 || status == 304)
    return false;
  return method != Method::CONNECT || status < 200 || status >= 300;
}

inline bool contentLengthForbidden_(Method::T method, unsigned status)
{
  return (status >= 100 && status < 200) || status == 204 ||
    (method == Method::CONNECT && status >= 200 && status < 300);
}

} // namespace Server_

struct StreamTask {
  StreamTask() = default;
  StreamTask(const StreamTask &) = delete;
  StreamTask &operator =(const StreamTask &) = delete;
  StreamTask(StreamTask &&task) :
    driver{task.driver}, stopFn{task.stopFn}, releaseFn{task.releaseFn} {
    task.driver = nullptr;
  }
  StreamTask &operator =(StreamTask &&task) {
    if (this == &task) return *this;
    release_();
    driver = task.driver;
    stopFn = task.stopFn;
    releaseFn = task.releaseFn;
    task.driver = nullptr;
    return *this;
  }
  template <typename Driver>
  StreamTask(ZuRef<Driver> driver_) :
    driver{ZuMv(driver_).release()},
    stopFn{[](void *ptr) { static_cast<Driver *>(ptr)->stopTask_(); }},
    releaseFn{[](void *ptr) {
      auto driver_ = static_cast<Driver *>(ptr);
      if (driver_->deref()) delete driver_;
    }} { }
  ~StreamTask() { release_(); }

  void stop_() { if (driver) stopFn(driver); }
  void release_() {
    if (!driver) return;
    auto driver_ = driver;
    driver = nullptr;
    releaseFn(driver_);
  }

  void	*driver = nullptr;
  void	(*stopFn)(void *) = nullptr;
  void	(*releaseFn)(void *) = nullptr;
};
ZmListDerive(StreamTaskQ, StreamTask,
  ZmListNode<StreamTask, ZmListHeapID<"Zhttp.StreamTask">>);

enum { StreamStopBatch = 64 };

template <typename Impl, typename Parser_, typename Message_>
struct ServerSession {
  using Parser = Parser_;
  using State = Parser::State;
  using Message = Message_;

  Parser	parser;
  bool		complete = false;

  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }

  void connected(auto &) { }
  void disconnected(auto &, bool) { }

  template <typename Link, typename Rx>
  ZuInline int process(Link &link, Rx &rx) {
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

template <typename App, typename Profile, typename AppLink>
class ProtocolServer :
  public NativeTraits<Profile>::Transport::template Server<App> {
  using HTTP = NativeTraits<Profile>;
  using Traits = HTTP::Transport;

public:
  using Base = Traits::template Server<App>;
  using Base::init;
  using Base::start;
  enum {
    TLS = Traits::Secure,
    Multiplexed = HTTP::Multiplexed
  };

  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  bool init(const HubConfig &hub, const Traits::Config &config) {
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
    return new AppLink::Cxn(
      new AppLink{impl(), ci}, ci);
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

template <typename App, typename AppLink> class ServerHub;
template <typename App, typename AppLink> class SrvLink;

template <typename App, typename AppLink>
class ServerHub : public Ztls::Server<ServerHub<App, AppLink>> {
public:
  using Base = Ztls::Server<ServerHub>;
  using Link = SrvLink<App, AppLink>;
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
    return new Link::Cxn(
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
	  static_cast<Link *>(link)->beginStop(hub->m_stopPending);
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

template <typename App, typename AppLink>
class SrvLink :
  public ZmNHeap<SrvLink<App, AppLink>>,
  public Ztls::SrvLink<ServerHub<App, AppLink>, SrvLink<App, AppLink>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public Wire<SrvLink<App, AppLink>, AppLink> {
  friend inline ZuStringT<"Zhttp.Server.H2Link"> ZmNHeapID(SrvLink *);
public:
  using Hub = ServerHub<App, AppLink>;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire_ = Wire<SrvLink, AppLink>;
  using Cxn = Base::Cxn;
  using Active = ZtArray<ZmRef<AppLink>,
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
      if (!entry.notified) active.push(entry.appLink);
    });
    Wire_::clearStreams([
      link = ZmRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &appLink: active) appLink->disconnected_(peer);
      if (!link->m_released) {
	link->m_released = true;
	link->app()->user()->release();
      }
      link->app()->linkDown();
    });
  }
  int process(Ztls::RxStream &rx) { return Wire_::process(rx); }
  auto txStream() { return Base::txStream(); }
  auto txStream_() { return Base::txStream_(); }
  void disconnectNative() { Base::disconnect(); }
  auto appTx(uint32_t id, unsigned plan = unsigned(-1)) {
    return HeaderBlock<SrvLink>{
      *this, Wire_::encoder(), id, Wire_::peerFrameSize(), plan};
  }
  auto appTx_(uint32_t id, unsigned plan = unsigned(-1)) {
    ZmAssert(this->app()->txInvoked());
    return HeaderBlock<SrvLink, false>{
      *this, Wire_::encoder(), id, Wire_::template peerFrameSize<false>(), plan};
  }
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }

  Stream<AppLink> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire_::canOpenPeerStream(id)) return nullptr;
    ZmRef<AppLink> appLink = new AppLink{
      this->app()->user(), this, id, m_remoteIP, m_remotePort};
    auto entry = Wire_::openPeerStream(id, appLink);
    if (!entry) return nullptr;
    appLink->connected_(NativeTraits<H2TLS>::apply({
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
  void close(AppLink *appLink, uint32_t id) {
    this->app()->rxInvoke([
      link = this, appLink = ZmRef(appLink), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->appLink.ptr() != appLink.ptr()) return;
      link->rst_(id, Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2ResetAppLink(uint32_t id, Error::T) { notify_(id, true); }
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
  void beginStop(unsigned &pending) {
    if (m_stopping || m_down) return;
    ++pending;
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
    auto appLink = entry->appLink;
    Wire_::removeStream(id, [appLink = ZuMv(appLink), peer]() mutable {
      appLink->disconnected_(peer);
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
class ServerAppLink : public ZmObject, public StreamOps<Impl> {
public:
  enum { TLS = 1, Multiplexed = 1 };

  ServerAppLink(
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
  Zhttp::Session session() const {
    return {uintptr_t(m_native), Transport::TLS};
  }
  Session &rxState() { return m_session; }
  const Session &rxState() const { return m_session; }
  auto txStream(unsigned plan = unsigned(-1)) {
    return m_native->appTx(m_streamID, plan);
  }
  auto txStream_(unsigned plan = unsigned(-1)) {
    return m_native->appTx_(m_streamID, plan);
  }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native)
      m_native->appTxErrorFn(m_streamID, ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  // HPACK encoding is Tx-owned; construct headers with transmit_() on Tx.
  template <typename Builder>
  auto transmit_(Builder &builder) { return txStream_(builder.hdrPlan()); }
  bool active() const { return m_native && m_streamID; }
  void finish() { }
  void disconnect() {
    if (m_native) m_native->close(impl(), m_streamID);
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
      appLink = ZmRef(impl()), native = ZmRef(m_native)]() mutable {
      (void)native;
      appLink->disconnectedTx_();
    });
  }
  template <typename Rx>
  int process_(Rx &rx) { return m_session.process(*impl(), rx); }

private:
  void disconnectedTx_() {
    m_native = nullptr;
    m_streamID = 0;
  }

  App		*m_app = nullptr;
  NativeLink	*m_native = nullptr;
  uint32_t	m_streamID = 0;
  Session	m_session;
  ZiIP		m_remoteIP;
  uint16_t	m_remotePort = 0;
};

} // namespace H2_

template <typename App, typename Impl, typename Session>
class ServerLink<App, Impl, H2TLS, Session> :
  public H2_::ServerAppLink<
    App, Impl, Session, H2_::SrvLink<App, Impl>> {
  using Base = H2_::ServerAppLink<
    App, Impl, Session, H2_::SrvLink<App, Impl>>;

public:
  using Base::Base;
};

namespace TLS_ {

template <typename App> class ServerHub;
template <typename App> class SrvLink;
template <
  typename App, typename Impl, typename Session, typename NativeLink>
class ServerH1AppLink : public ZmObject {
public:
  enum { TLS = 1, Multiplexed = 0 };

  ServerH1AppLink(
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
  Zhttp::Session session() const {
    return {uintptr_t(m_native), Transport::TLS};
  }
  Session &rxState() { return m_session; }
  const Session &rxState() const { return m_session; }
  bool streamEnable(bool enabled = true) { return m_stream.enable(enabled); }
  bool streamAccept() { return m_stream.accept(*impl(), *impl()); }
  bool streamLocalCap() const { return m_stream.localCap(); }
  bool streamPeerCap() const { return m_stream.peerCap(); }
  template <typename L>
  void streamTx_(L &&write) {
    if (!m_stream.tx()) return;
    auto tx = m_native->txStream_();
    ZuFwd<L>(write)(tx);
  }
  template <typename L>
  void streamTx(L &&write) {
    if (!m_stream.tx()) return;
    auto tx = m_native->txStream();
    ZuFwd<L>(write)(tx);
  }
  void streamTxEnd() { if (m_stream.end()) m_native->streamTxEnd(); }
  void streamTxReset() { if (m_stream.reset()) disconnect(); }
  template <typename Stream>
  int process(Stream, Input &input) { return impl()->channel().frameInput(input); }
  template <typename Stream>
  void peerEnd(Stream) { impl()->channel().frameEnd(); }
  template <typename Stream>
  void error(Stream) { impl()->channel().frameError(); }
  auto txStream() { return m_native->txStream(); }
  void txErrorFn(ZiTxErrorFn fn) {
    if (m_native) m_native->txErrorFn(ZuMv(fn));
  }
  template <typename Parser, typename Rx>
  auto receive(Parser &parser, Rx &rx) { return parser.process(rx); }
  template <typename Builder>
  auto transmit(Builder &) { return txStream(); }
  template <typename Builder>
  auto transmit_(Builder &) { return m_native->txStream_(); }
  bool active() const { return m_native; }
  void finish() { }
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
    m_stream.disconnected(peer);
    m_session.disconnected(*impl(), peer);
    m_app->disconnected(*impl(), peer);
    m_app->txRun([
      appLink = ZmRef(impl()), native = ZmRef(m_native)]() mutable {
      (void)native;
      appLink->disconnectedTx_();
    });
  }
  int process_(Ztls::RxStream &rx) {
    if (m_stream.stream()) return m_stream.process(rx);
    if (m_stream.terminal()) return 0;
    int rc = m_session.process(*impl(), rx);
    if (m_stream.stream()) {
      int streamRC = m_stream.process(rx);
      if (streamRC) rc = streamRC;
    }
    if (rc >= 0) touch_();
    return rc;
  }

private:
  void disconnectedTx_() {
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
  H1::StreamBinding<Impl, Impl> m_stream;
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
    return new Link::Cxn(
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
	  static_cast<Link *>(link)->beginStop(hub->m_stopPending);
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
  public ZmNHeap<SrvLink<App>>,
  public Ztls::SrvLink<ServerHub<App>, SrvLink<App>,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>,
  public H2_::Wire<SrvLink<App>, typename App::H2Link> {
  friend inline ZuStringT<"Zhttp.Server.TLSLink"> ZmNHeapID(SrvLink *);
public:
  using Hub = ServerHub<App>;
  using H1AppLink = App::H1Link;
  using H2AppLink = App::H2Link;
  using Base = Ztls::SrvLink<Hub, SrvLink,
    Transport_::TLSRxBufAlloc, Transport_::TLSTxBufAlloc>;
  using Wire = H2_::Wire<SrvLink, H2AppLink>;
  using Cxn = Base::Cxn;
  using Active = ZtArray<ZmRef<H2AppLink>,
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

private:
  using Base::send;
public:

  void connected(Ztls::Connected info) {
    m_version = TLS_::version(info.alpn, m_policy);
    if (m_version == Version::H1 || m_version == Version::H2)
      Server_::connected(
	this->app()->user(), {uintptr_t(this), Transport::TLS}, 0);
    switch (m_version) {
	case Version::H1:
	m_h1 = new H1AppLink{
	  this->app()->user(), this, m_remoteIP, m_remotePort};
	m_h1->connected_(NativeTraits<H1TLS>::apply({
	  .alpn = ALPNString{
	    info.alpn.data(), unsigned(info.alpn.length())},
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
      if (!entry.notified) active.push(entry.appLink);
    });
    Wire::clearStreams([
      link = ZmRef(this), active = ZuMv(active), peer
    ]() mutable {
      for (auto &appLink: active) appLink->disconnected_(peer);
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
  auto txStream_() { return Base::txStream_(); }
  auto appTx(uint32_t id, unsigned plan = unsigned(-1)) {
    return H2_::HeaderBlock<SrvLink>{
      *this, Wire::encoder(), id, Wire::peerFrameSize(), plan};
  }
  auto appTx_(uint32_t id, unsigned plan = unsigned(-1)) {
    ZmAssert(this->app()->txInvoked());
    return H2_::HeaderBlock<SrvLink, false>{
      *this, Wire::encoder(), id, Wire::template peerFrameSize<false>(), plan};
  }
  const HPackSeedPlans &hpackSeedPlans() const {
    const auto &user = *this->app()->user();
    return AppHPackSeedPlans<ZuDecay<decltype(user)>>::get(user);
  }
  void streamTxEnd() {
    this->app()->txRun([link = ZmRef(this)]() mutable {
      if (link->txQueue.count_()) { link->m_streamEnd = true; return; }
      link->streamTxClose_();
    });
  }
  void sent(ZmRef<ZiTxBuf> buffer, bool ok) {
    Base::sent(ZuMv(buffer), ok);
    if (!m_streamEnd || (ok && this->txQueue.count_())) return;
    m_streamEnd = false;
    streamTxClose_();
  }
private:
  void streamTxClose_() {
    this->app()->rxRun([link = ZmRef(this)]() mutable { link->disconnectNative(); });
  }
  bool m_streamEnd = false; // Tx-owned
public:
  void disconnectNative() { Base::disconnect(); }
  void drain() {
    if (m_version != Version::H2 || m_draining || m_down) return;
    m_draining = true;
    Wire::graceful();
  }

  H2_::Stream<H2AppLink> *h2OpenPeer(uint32_t id) {
    if (m_draining || !Wire::canOpenPeerStream(id)) return nullptr;
    ZmRef<H2AppLink> appLink = new H2AppLink{
      this->app()->user(), this, id, m_remoteIP, m_remotePort};
    auto entry = Wire::openPeerStream(id, appLink);
    if (!entry) return nullptr;
    appLink->connected_(NativeTraits<H2TLS>::apply({
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
  void close(H2AppLink *appLink, uint32_t id) {
    this->app()->rxInvoke([
      link = this, appLink = ZmRef(appLink), id
    ]() mutable {
      auto entry = link->h2Stream(id);
      if (!entry || entry->appLink.ptr() != appLink.ptr()) return;
      link->rst_(id, H2::Error::Cancel);
      link->notify_(id, false);
    });
  }
  void h2ResetAppLink(uint32_t id, H2::Error::T) {
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
  void beginStop(unsigned &pending) {
    if (m_stopping || m_down) return;
    ++pending;
    m_stopping = true;
    m_draining = true;
    Wire::stopWire();
    if (m_version == Version::H2) Wire::graceful();
    Base::disconnect();
  }

private:
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
    auto appLink = entry->appLink;
    Wire::removeStream(id, [appLink = ZuMv(appLink), peer]() mutable {
      appLink->disconnected_(peer);
    });
  }
  void down_() {
    Server_::disconnected(
      this->app()->user(), {uintptr_t(this), Transport::TLS}, 0);
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
  ZmRef<H1AppLink>	m_h1;
  bool			m_draining = false;
  bool			m_down = false;
  bool			m_stopping = false;

};

} // namespace TLS_

template <typename App, typename AppLink>
class ProtocolServer<App, H2TLS, AppLink> :
    public H2_::ServerHub<App, AppLink> {
public:
  using Base = H2_::ServerHub<App, AppLink>;
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

template <typename App, typename AppLink> struct SrvLink;
template <typename App, typename AppLink> struct ServerStream;

template <typename App, typename AppLink>
class ServerHub :
  public Zquic::Server<ServerHub<App, AppLink>, SrvLink<App, AppLink>>,
  public Faults<ServerHub<App, AppLink>> {
public:
  using Link = SrvLink<App, AppLink>;
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
      disconnectAppLinks_([this, fn = ZuMv(fn)]() mutable {
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
    link->disconnectAppLinks(peer);
    Base::disconnected(link, peer);
  }

  ZmRef<Link> accepted(const Zquic::InitialInfo &info) {
    ConnectedInfo ci = NativeTraits<H3QUIC>::apply({
      .version = Zquic::Version1,
      .transport = Transport::QUIC,
      .secure = true
    });
    if (!user()->admit(ci)) return {};
    return new Link{this, info.peer.ip(), info.peer.port()};
  }

private:
  template <typename Done>
  void disconnectAppLinks_(Done &&done) {
    this->allLinks(
      [](ZmRef<Link> link) {
	link->disconnectAppLinks(false);
      },
      ZuFwd<Done>(done));
  }
};

template <typename App, typename AppLink>
struct ServerStream :
  public Zquic::SrvStream<SrvLink<App, AppLink>, ServerStream<App, AppLink>>,
  public H3::CxnStream<ServerStream<App, AppLink>,
    H3::Cxn<SrvLink<App, AppLink>, ZmRef<ServerStream<App, AppLink>>>> {
  using Link = SrvLink<App, AppLink>;
  using Base = Zquic::SrvStream<Link, ServerStream>;
  using H3Cxn = H3::Cxn<Link, ZmRef<ServerStream>>;
  using CxnStream = H3::CxnStream<ServerStream, H3Cxn>;
  using Base::Base;

  int process(Zquic::RxStream &rx) {
    if (Zquic::StreamID::uni(uint64_t(this->id())))
      return CxnStream::process(*this);
    if (!appLink) {
      if (this->resetReceived() || (this->rxComplete() && !rx)) return 0;
      auto link = this->link();
      appLink = new AppLink{
	link->app()->user(), link, this,
	link->remoteIP, link->remotePort};
      slot = link->activeStreams.length();
      link->activeStreams.push(ZmRef(this));
      appLink->connected_(NativeTraits<H3QUIC>::apply({
	.alpn = "h3",
	.version = Zquic::Version1,
	.transport = Transport::QUIC,
	.secure = true
      }));
    }
    int rc = appLink->process_(rx);
    if (rc < 0) return 0; // parser has scheduled a stream-local reset
    if (this->rxComplete()) this->link()->remoteEnd(this);
    return rc;
  }
  H3Cxn &h3Cxn() const { return this->link()->h3; }
  void quicReset(uint64_t error) { Base::reset(error); }

public:

  ZmRef<AppLink>	appLink;
  uint32_t		slot = QueueSlot::Invalid;
  bool		localEnd = false;
  bool		remoteEnd = false;
  bool		closing = false;

};

template <typename App, typename AppLink>
struct SrvLink :
  public Zquic::SrvLink<
    ServerHub<App, AppLink>, SrvLink<App, AppLink>,
    ServerStream<App, AppLink>> {
  using Hub = ServerHub<App, AppLink>;
  using Stream = ServerStream<App, AppLink>;
  using Base = Zquic::SrvLink<Hub, SrvLink, Stream>;
  using StreamRef = ZmRef<Stream>;
  using H3Cxn = H3::Cxn<SrvLink, StreamRef>;
  using Streams =
    ZtArray<StreamRef, ZtArrayHeapID<"Zhttp.H3.ServerStreams">>;
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
    Server_::connected(
      this->app()->user(), {uintptr_t(this), Transport::QUIC}, 0);
    const auto &config = this->app()->user()->quicConfig();
    H3::QPackLimits limits{
      config.qpackRxCapacity(), config.qpackTxCapacity(),
      config.qpackRxBlocked(), config.qpackTxSections()
    };
    bool extendedConnect = config.extendedConnect();
    h3.link_ = this;
    ZiTxErrorFn txError = h3.txError;
    auto link = ZmRef(this);
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
    disconnectAppLinks(peer);
  }
  void disconnectAppLinks(bool peer) {
    if (notified) return;
    notified = true;
    for (unsigned i = 0, n = activeStreams.length(); i < n; ++i) {
      activeStreams[i]->slot = QueueSlot::Invalid;
      auto owner = ZuMv(activeStreams[i]->appLink);
      if (owner) owner->disconnected_(peer);
    }
    activeStreams.length(0);
    Server_::disconnected(
      this->app()->user(), {uintptr_t(this), Transport::QUIC}, 0);
    h3.qpackRxTable.final();
    auto link = ZmRef(this);
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
      link, stream = ZmRef(stream)
    ]() mutable {
      link->finish_(ZuMv(stream));
      return link;
    });
  }
  void finish_(StreamRef stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 logical end outside Rx thread", return);
    if (!stream || !stream->appLink || stream->localEnd) return;
    stream->localEnd = true;
    if (stream->remoteEnd) closeLater_(stream, false);
    this->send(ZuMv(stream), "", true);
  }
  void remoteEnd(Stream *stream) {
    ZiAssert(this->app()->rxInvoked(), "Zhttp", (),
      "H3 remote logical end outside Rx thread", return);
    if (!stream || !stream->appLink || stream->remoteEnd) return;
    stream->remoteEnd = true;
    if (stream->localEnd) closeLater_(stream, true);
  }
  bool h3PeerCap() const {
    return h3.peerExtendedConnect;
  }
  bool h3PeerCap_() const { return h3PeerCapTx; }
  void h3PeerCap(bool value) {
    auto link = this;
    this->app()->txRun([link, value]() {
      link->h3PeerCapTx = value;
    });
  }
  H3::QPackTxTable *qpackTx() {
    ZmAssert(this->app()->txInvoked());
    return &h3Tx;
  }
  void qpackSeed(StreamRef encoder) {
    auto link = ZmRef(this);
    this->app()->txRun([link = ZuMv(link), encoder = ZuMv(encoder)]() mutable {
      if (link->h3SeedStateTx != QPackSeedState::Unseeded) return;
      auto result = installQPackSeeds(
	    link->h3Tx, AppHeaderSeeds<ZuDecay<
	      decltype(*link->app()->user())>>::get(*link->app()->user()),
	    [link, &encoder](ZuBSpan bytes) {
	      return link->send_(encoder, bytes, false);
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
      link = ZmRef(this), stream = ZmRef(stream), peer
    ]() mutable {
      if (!stream->appLink) return;
      auto appLink = ZuMv(stream->appLink);
      link->removeStream_(stream);
      appLink->disconnected_(peer);
    });
  }

  void removeStream_(Stream *stream) {
    unsigned slot = stream->slot;
    ZmAssert(slot < activeStreams.length() &&
      activeStreams[slot].ptr() == stream);
    unsigned last = activeStreams.length() - 1;
    if (slot != last) {
      activeStreams[slot] = ZuMv(activeStreams[last]);
      activeStreams[slot]->slot = slot;
    }
    activeStreams.length(last);
    stream->slot = QueueSlot::Invalid;
  }

public:
  // Stable for the native connection lifetime
  ZiIP			remoteIP;
  uint16_t		remotePort = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Streams		activeStreams;
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
  public ZmObject, public H3_::StreamOps<Impl> {
  using Hub = H3_::ServerHub<App, Impl>;
  using NativeLink = H3_::SrvLink<App, Impl>;
  using NativeStream = H3_::ServerStream<App, Impl>;

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
  Zhttp::Session session() const {
    return {uintptr_t(m_native), Transport::QUIC};
  }
  Session &rxState() { return m_session; }
  const Session &rxState() const { return m_session; }
  auto txStream() { return m_stream->txStream(); }
  auto txStream_() { return m_stream->txStream_(); }
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
	  stream->link()->h3RequestRejected(ZmRef(stream), error);
	else
	  stream->h3StreamError(error);
      },
      uint64_t(m_stream->id()), &m_native->h3.params);
    parser.extendedConnect(m_native->h3.localExtendedConnect);
    (void)rx;
    return parser.process(*m_stream);
  }
  // QPACK encoding is Tx-owned; construct headers with transmit_() on Tx.
  template <typename Builder>
  auto transmit_(Builder &builder) {
    ZmAssert(m_native->app()->txInvoked());
    using H3Cxn = ZuDecay<decltype(m_native->h3)>;
    builder.h3(
      m_native->qpackTx(), &m_native->h3,
      [](void *ptr, ZuBSpan span) {
	return static_cast<H3Cxn *>(ptr)->qpackEncoderWrite(span);
      },
      uint64_t(m_stream->id()), m_native->h3PeerCap_(),
      &m_native->h3.params);
    return m_stream->txStream_();
  }
  bool active() const { return m_native && m_stream; }
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
      appLink = ZmRef(impl()), native = ZmRef(m_native),
      stream = ZmRef(m_stream)]() mutable {
      (void)native;
      (void)stream;
      appLink->disconnectedTx_();
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

template <typename App, typename AppLink>
class ProtocolServer<App, H3QUIC, AppLink> :
    public H3_::ServerHub<App, AppLink> {
public:
  using Base = H3_::ServerHub<App, AppLink>;
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


template <typename Q, bool Ordered>
struct RouteState { };
template <typename Q>
struct RouteState<Q, true> {
  Q m_routeResponses;
  uint64_t m_routeNext = 1;
};

// The queue owns the transferred reference on Tx, using the actual message's
// existing intrusive allocation. Framework shadow links live in that record.
class ServerResponseQ {
  using Items = ZmList<ResponseRecord, ZmListShadow<>, ResponseRecord>;

public:
  ~ServerResponseQ() { clean(); }
  void pushNode(ResponseRef response) {
    m_items.pushNode(response.release(this));
  }
  ResponseRef shift() { return ResponseRef::acquire(m_items.shift(), this); }
  ResponseRecord *headNode() const { return m_items.headNode(); }
  unsigned count() const { return m_items.count_(); }
  void clean() {
    while (auto response = shift()) { }
  }
  auto iter() { return m_items.iter(); }
  void insert(Items::Iter &iter, ResponseRef response) {
    iter.unshiftNode(response.release(this));
  }

private:
  Items m_items;
};


struct BoundReqParser : public Zhttp::Parser {
  ParserBinding binding{};

  BoundReqParser() = default;
  BoundReqParser(const BoundReqParser &) = delete;
  BoundReqParser &operator =(const BoundReqParser &) = delete;
  ~BoundReqParser() { if (binding.ctx) binding.destroy(); }

  void construct(ServerCore &owner, Channel *channel) {
    binding = owner.parser();
    binding.channel = channel;
  }
  void init(ServerCore &owner, Channel *channel) {
    channel->resetFrames();
    binding.channel = channel;
    binding.ops->init(binding.ctx, owner.context());
  }
  void bodyMax(uint64_t max) { binding.ops->bodyMax(binding.ctx, max); }
  const CatalogView &catalog() const { return *binding.catalog; }
  bool operation(Method::T method, Target &target) {
    return binding.operation(method, target);
  }
  void header(FieldSection::T section, int id, ZuBSpan name, ZuSpan<uint8_t> value) {
    binding.header(section, id, name, value);
  }
  bool bodyInfo(BodyType::T type, uint64_t length) {
    return binding.bodyInfo(type, length);
  }
  bool body(Input &input) { return binding.body(input); }
  template <typename Link>
  ZuInline void complete(Link *link, bool ok) {
    binding.ops->complete(binding.ctx, &link->channel(), ok);
  }
  void reset() { binding.reset(); }
};

class ServerImpl : public ZmNHeap<ServerImpl>, public ZmEngine<ServerImpl> {
  friend inline ZuStringT<"Zhttp.Server"> ZmNHeapID(ServerImpl *);
  friend class ServerCore;
  friend struct TCPBackend;
  friend struct TLSBackend;
  friend struct QUICBackend;
  friend ZmEngine<ServerImpl>;
public:
  using Engine = ZmEngine<ServerImpl>;
  using Impl = ServerCore;
  using ReqParser = BoundReqParser;
  using ResQ = ServerResponseQ;
  using ReqHdrCatalog = ReqParser::HdrCatalog;
  using ResBuilder = ResponseRecord;
  using ResBuilder_ = ResponseRecord;
  using ResponseHeaders = ZuTypeList<>;
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

  template <typename Profile, typename Link_ = ProfileLink<Profile>> struct Session;
  template <typename Protocol> struct Hub;
  template <typename Profile, typename Link_ = ProfileLink<Profile>> struct Parser;
  template <typename Profile, typename Link_> struct Responses;

  const ServerConfig &config() const { return m_config; }
  auto impl() { return m_owner; }
  auto impl() const { return m_owner; }
  const MessageString &altSvc() const { return m_altSvc; }
  ZiMultiplex *mx() const { return m_mx; }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }
  Hubs &hubs() { return m_hubs; }
  Backend &tcpBackend() { return m_tcp; }
  Backend &tlsBackend() { return m_tls; }
  Backend &quicBackend() { return m_quic; }
  HeaderSeedCatalog &qpackCatalog() { return m_qpackSeeds; }
  HPackSeedCatalog &hpackCatalog() { return m_hpackSeeds; }
  Server_::Stats &stats() { return m_stats; }
  const Server_::Stats &stats() const { return m_stats; }
  template <typename Driver>
  StreamTaskQ::Node *streamTaskAdd_(ZuRef<Driver> driver) {
    return m_streamTasks.push(StreamTask{ZuMv(driver)});
  }
  void streamTaskDel_(StreamTaskQ::Node *task) {
    if (auto node = m_streamTasks.delNode(task)) node->release_();
  }

  template <typename Profile, typename Link_>
  struct Parser :
    public MessageTraits<Profile>::template RequestParser<
      Parser<Profile, Link_>> {
    using Base = MessageTraits<Profile>::template RequestParser<
      Parser>;
    using State = Base::State;

    template <typename AppParser>
    static auto bodyMax_(AppParser &parser, uint64_t max, int) ->
	decltype(parser.bodyMax(max), void()) {
      parser.bodyMax(max);
    }
    template <typename AppParser>
    static void bodyMax_(AppParser &, uint64_t, long) { }

    void bind(ServerImpl *server_, Link_ *link_) {
      server = server_;
      link = link_;
      Base::bodyMax(server->config().retainedBodyMax());
      if constexpr (Profile::HTTPVersion == Version::H2)
	Base::extendedConnect(server->config().tlsConfig().extendedConnect());
      appParser.construct(*server->owner(), &link->channel());
    }

    void connectedInfo(const ConnectedInfo &info) {
      appParser.binding.connectedInfo(server->owner()->m_app, info);
    }
    const CatalogView &catalog() const { return appParser.catalog(); }

    void begin() {
      appParser.init(*server->impl(), &link->channel());
      bodyMax_(appParser, server->config().retainedBodyMax(), 0);
      active = true;
      notified = false;
    }

    void reset() {
      Base::reset();
      active = false;
      notified = false;
    }

    bool operation(Method::T method, Target &target) {
      return parser_().operation(method, target);
    }
    void header(FieldSection::T section, int id,
	ZuBSpan key, ZuSpan<uint8_t> value) {
      parser_().header(section, id, key, value);
    }
    bool bodyInfo(BodyType::T type, uint64_t length) {
      bool accepted = parser_().bodyInfo(type, length);
      if constexpr (Profile::HTTPVersion != Version::H1)
	if (accepted && link->channel().frameMode()) Base::stream();
      return accepted;
    }
    void streamRx_(Input &input) {
      if (link->channel().frameInput(input) < 0) link->channel().streamTxReset();
    }
    void streamPeerEnd_() { link->channel().frameEnd(); }
    void streamError_() { link->channel().frameError(); }
    void status(unsigned) { }
    template <typename Rx>
    bool body(Rx &rx) {
      return parser_().body(rx);
    }
    ZuInline void complete(State::T state) {
      notify(state == State::Complete);
    }
    ZuInline void notify(bool ok) {
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
    ReqParser &parser_() { return appParser; }

  public:
    ServerImpl		*server = nullptr;
    Link_		*link = nullptr;
    ReqParser	appParser;
    bool	active = false;
    bool	notified = false;
  };

  template <typename Profile, typename Builder>
  struct ResponseOps {
    ServerImpl *server;
    Builder *builder;
    FieldProvider *provider = nullptr;
    uint64_t produced = 0;
    FieldContext context;

    ResponseOps(ServerImpl *server_, Builder &builder_,
	bool rejectContentLength = false) : server{server_}, builder{&builder_} {
      context.altSvc = server->altSvc();
      context.rejectContentLength = rejectContentLength;
      context.reject = [](void *) { (void)Server_::streamContentLength(); };
    }
    unsigned status() const { return builder->status(); }
    unsigned hdrPlan() const { return builder->hdrPlan; }
    bool streamResponse() const { return builder->streamResponse(); }
    void fields(FieldOutput &out) { provider->fields(provider->ctx, out); }
    void headerBase(uint8_t *base) { provider->base(provider->ctx, base); }
    void patch() { provider->patch(provider->ctx); }
    bool validHeaders() const { return !context.invalidHeader; }
    uint64_t contentLength() const { return produced; }
    Builder &appBuilder() { return *builder; }
    template <typename Emit>
    void emitBody(Emit &&emit) {
      Native_::BorrowedEmit<ZuDeref<Emit>> control{emit};
      builder->body(BodyEmitter{&control.record});
    }
  };

  template <typename Profile, typename Builder, bool HasBody, bool Streaming>
  struct ResponseTx :
    public MessageTraits<Profile>::template Response<
      ResponseTx<Profile, Builder, HasBody, Streaming>, HasBody, Streaming>,
    public ResponseOps<Profile, Builder> {
    using Base = MessageTraits<Profile>::template Response<
      ResponseTx, HasBody, Streaming>;
    using Ops = ResponseOps<Profile, Builder>;
    static constexpr unsigned HdrBufSize = Zhttp::Builder::HdrBufSize;

    ResponseTx(ServerImpl *server, Builder &builder,
	bool rejectContentLength = false) : Ops{server, builder, rejectContentLength} { }

    unsigned hdrBufSize() const { return Ops::builder->hdrBufSize; }
    using Ops::contentLength;
    using Ops::emitBody;
    using Ops::fields;
    using Ops::status;
    using Ops::streamResponse;

    template <typename Tx>
    bool begin(Tx &tx) {
      struct Encode {
	ResponseTx *self;
	Tx *tx;
	bool ok = true;
      } encode{this, &tx};
      Ops::builder->encode(Ops::context, &encode,
	[](void *ctx, FieldProvider &provider) {
	  auto &encode = *static_cast<Encode *>(ctx);
	  auto self = encode.self;
	  struct Reset {
	    ResponseTx *self;
	    ~Reset() { self->provider = nullptr; }
	  } reset{self};
	  self->provider = &provider;
	  using R = decltype(self->Base::begin(*encode.tx));
	  if constexpr (ZuIsSame<R, void>{}) self->Base::begin(*encode.tx);
	  else encode.ok = self->Base::begin(*encode.tx);
	});
      return encode.ok;
    }
  };

  template <typename Profile, typename Link_, typename Builder>
  struct ServerTxOps {
    ServerImpl	*server;
    Link_	*link_;
    uint64_t	*retainedBytes;

    Link_ &link() { return *link_; }

    uint64_t fixedBodyMax() const { return server->fixedBodyMax_(); }
    uint64_t retainedMax() const {
      uint64_t n = server->retainedAvailable_();
      if (n > server->config().retainedMessageMax())
	n = server->config().retainedMessageMax();
      return n;
    }
    void headers() { }
    template <bool> void produced(uint64_t) { }
    bool empty(Builder &builder) {
      ResponseTx<Profile, Builder, false, false> empty{
	server, builder};
      auto tx = link_->transmit_(empty);
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
  bool sendEmptyResponse_(Link_ &link, Builder &builder) {
    ResponseTx<Profile, Builder, true, true> response{
      this, builder, true};
    auto tx = link.transmit_(response);
    response.begin(tx);
    response.finish(tx);
    link.finish();
    return true;
  }

  template <typename Profile, typename Link_, typename Builder>
  bool sendResponse_(
      Link_ &link, Builder &builder, uint64_t &retainedBytes) {
    unsigned status = builder.status();
    auto policy = builder.bodyPolicy();
    Method::T method = builder.method();
    if (!BodyPolicy::hasBody(policy) ||
	!Server_::bodyAllowed_(method, status)) {
      ResponseTx<Profile, Builder, false, false> response{
	this, builder, Server_::contentLengthForbidden_(method, status)};
	auto tx = link.transmit_(response);
      response.begin(tx);
      response.finish(tx);
      if (!builder.streamResponse()) link.finish();
      return true;
    }
    ZiAssert(!BodyPolicy::streaming(policy), "Zhttp", (),
      "streaming response reached fixed response path", return false);
    ResponseTx<Profile, Builder, true, false> response{this, builder};
    ServerTxOps<Profile, Link_, Builder> ops{
      this, &link, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.fixed(response, BodyPolicy::optional(policy));
  }

  template <typename Driver>
  struct BodyEmit : public ZmNHeap<BodyEmit<Driver>>, public ZmObject {
  friend inline ZuStringT<"Zhttp.Server.BodyEmit"> ZmNHeapID(BodyEmit *);

    BodyEmit(Driver *driver_, int txThread_) :
      driver{driver_}, txThread{txThread_} {
      record.bind(this);
      record.emit = [](void *ctx, BodyWrite write) {
	return static_cast<BodyEmit *>(ctx)->emit_(write);
      };
    }

    ~BodyEmit() {
      ZiAssert(!driver, "Zhttp", (),
	"active stream emitter destroyed", return);
    }

    template <typename Write>
    bool emit_(Write &&write) {
      if (!Server_::streamTxThread(txThread)) return false;
      Driver *driver_ = driver;
      if (!driver_) return false;
      return driver_->turn_(ZuFwd<Write>(write));
    }

    void close_(Driver *driver_) {
      if (!Server_::streamTxThread(txThread)) return;
      if (!driver) return;
      ZiAssert(driver == driver_, "Zhttp", (),
	"stream emitter driver mismatch", return);
      driver = nullptr;
    }

    BodyEmitRecord record;
    Driver	*driver;
    const int	txThread;
  };

  template <typename Profile, typename Link_> struct StreamBody;

  template <typename Profile, typename Link_>
  struct StreamBody : public ZmNHeap<StreamBody<Profile, Link_>>, public ZuObject {
  friend inline ZuStringT<"Zhttp.Server.StreamBody"> ZmNHeapID(StreamBody *);

    using Self = StreamBody<Profile, Link_>;
    using Emit = BodyEmit<Self>;
    using Tx = ResponseTx<Profile, ResBuilder_, true, true>;

    StreamBody(ServerImpl *server_, Link_ *link_, ResBuilder *appResponse_) :
      server{server_}, link{link_}, appResponse{appResponse_},
      response{server_, appResponse_->data(), true} { }

    ~StreamBody() {
      ZiAssert(!emit && !task, "Zhttp", (),
	"active response stream driver destroyed", return);
    }

    bool start_(ZuRef<Self> self, bool optional) {
      constexpr bool handled = true;
      if (!Server_::streamTxThread(int(server->txThread()))) return false;
      task = server->streamTaskAdd_(ZuMv(self));
      link->responseCancel_(StreamCancelFn{static_cast<Self *>(this),
	[](Self *self_) {
	self_->stop_();
      }});
      emit = new Emit{static_cast<Self *>(this), int(server->txThread())};
      appResponse->data().body(BodyEmitter{&emit->record});
      switch (state) {
	case StreamState::Initial:
	  if (Server_::streamBodyAbsent(optional)) {
	    closeProducer_();
	    finishTask_();
	    appResponse->data().close();
	    return server->template sendEmptyResponse_<Profile>(
	      *link, appResponse->data());
	  }
	  failed_(server->stats().responseBuildFailures);
	  if (state == StreamState::InitialTerminal)
	    appResponse->data().close();
	  return handled;
	case StreamState::InitialOpen:
	  state = StreamState::Open;
	  return handled;
	case StreamState::InitialTerminal:
	  appResponse->data().close();
	  return handled;
	default:
	  ZiAssert(false, "Zhttp", (), "invalid initial stream state", );
	  failed_(server->stats().responseBuildFailures);
	  if (state == StreamState::InitialTerminal)
	    appResponse->data().close();
	  return handled;
      }
    }

    template <typename Write>
    bool turn_(Write &&write) {
      ZuRef<Self> self{static_cast<Self *>(this)};
      bool beginHeaders;
      if (!Server_::streamEnter(state, beginHeaders)) return false;
      auto tx = link->transmit_(response);
      if (beginHeaders) response.begin(tx);
      if (beginHeaders && !response.validHeaders()) {
	failed_(server->stats().responseBuildFailures);
	return true;
      }
      auto body = response.body(tx);
      WriteOutcome::T outcome = invokeBodyWriter(
	ZuFwd<Write>(write), body);
      body.flush();
      bool txOK = body.valid();
      if (!txOK) outcome = WriteOutcome::Failed;
	switch (outcome) {
	case WriteOutcome::End:
	  end_(tx);
	  return true;
	case WriteOutcome::Stream:
	  state = state == StreamState::InitialWriting ?
	    StreamState::InitialOpen : StreamState::Open;
	  return true;
	case WriteOutcome::Abort:
	  abort_();
	  return true;
	case WriteOutcome::Failed:
	default:
	  if (txOK)
	    failed_(server->stats().responseBuildFailures);
	  else
	    failed_(server->stats().transportFailures);
	  return true;
      }
    }

    void stop_() {
      ZuRef<Self> self{static_cast<Self *>(this)};
      if (!closeProducer_()) return;
      if (task) finishTask_();
    }

    void stopTask_() {
      task = nullptr;
      stop_();
    }

    template <typename Tx_>
    void end_(Tx_ &tx) {
      if (!closeProducer_()) return;
      finishTask_();
      response.finish(tx);
      link->finish();
      link->responseDone_(appResponse, true);
    }

    void abort_() {
      if (!closeProducer_()) return;
      finishTask_();
      link->responseAbort_(appResponse);
    }

    void failed_(ZmAtomic<uint64_t> &counter) {
      if (!closeProducer_()) return;
      finishTask_();
      ++counter;
      link->responseAbort_(appResponse);
    }

    bool closeProducer_() {
      bool deferClose;
      switch (state) {
	case StreamState::Initial:
	case StreamState::InitialWriting:
	case StreamState::InitialOpen:
	  state = StreamState::InitialTerminal;
	  deferClose = true;
	  break;
	case StreamState::Writing:
	case StreamState::Open:
	  state = StreamState::Terminal;
	  deferClose = false;
	  break;
	case StreamState::InitialTerminal:
	case StreamState::Terminal:
	  return false;
	default:
	  ZiAssert(false, "Zhttp", (), "invalid terminal stream state", );
	  state = StreamState::Terminal;
	  deferClose = false;
	  break;
      }
      emit->close_(static_cast<Self *>(this));
      emit = nullptr;
      link->responseCancel_({});
      if (!deferClose) appResponse->data().close();
      return true;
    }

    void finishTask_() {
      if (!task) return;
      auto task_ = task;
      task = nullptr;
      server->streamTaskDel_(task_);
    }

    ServerImpl			*server;
    Link_			*link;
    ResBuilder			*appResponse;
    Tx				response;
    ZmRef<Emit>			emit;
    StreamTaskQ::Node	*task = nullptr;
    int8_t			state = StreamState::Initial;
  };

  template <typename Profile, typename Link_>
  bool startResponse_(
      ZmRef<Link_> link, ResponseRef response,
      BodyPolicy::T policy, uint64_t &retainedBytes) {
    if (BodyPolicy::streaming(policy)) {
      using State = StreamBody<Profile, Link_>;
      ZuRef<State> state = new State{this, link.ptr(), response.ptr()};
      return state->start_(state, BodyPolicy::optional(policy));
    }
    return sendResponse_<Profile>(
      *link, response->data(), retainedBytes);
  }

  template <typename Profile, typename Link_>
  struct Responses : private RouteState<ResQ,
      !NativeTraits<Profile>::Multiplexed> {
    auto impl() { return static_cast<Link_ *>(this); }
    auto impl() const { return static_cast<const Link_ *>(this); }

    void send(ResponseRef response) {
      if (!response) return;
      auto link = ZmRef(impl());
      auto server = link->app()->server;
      ++server->stats().queuedResponses;
      link->app()->txRun([
	link = ZuMv(link), response = ZuMv(response)]() mutable {
	link->responseSend_(ZuMv(response));
      });
    }

    ZuInline void routeSend(uint64_t sequence, ResponseRef response) {
      if constexpr (NativeTraits<Profile>::Multiplexed) {
	send(ZuMv(response));
      } else {
	if (!response) return;
	auto link = ZmRef(impl());
	auto server = link->app()->server;
	++server->stats().queuedResponses;
	link->app()->txRun([
	  link = ZuMv(link), response = ZuMv(response), sequence]() mutable {
	  link->responseRoute_(sequence, ZuMv(response));
	});
      }
    }

    void responseDisconnected_() {
      auto link = ZmRef(impl());
      link->app()->txRun([link = ZuMv(link)]() mutable {
	link->responseCancelAll_();
      });
    }

    void responseCancel(StreamCancelFn fn) {
      auto server = impl()->app()->server;
      server->assertTx_();
      responseCancel_(ZuMv(fn));
    }

  private:
    friend ServerImpl;
    template <typename P, typename L> friend struct StreamBody;

    void responseRoute_(uint64_t sequence, ResponseRef response) {
      auto server = impl()->app()->server;
      server->assertTx_();
      if (!impl()->active() || sequence < this->m_routeNext) {
	--server->stats().queuedResponses;
	response->data().completed({ResponseCode::Discarded});
	return;
      }
      response->data().routeSequence(sequence);
      if (sequence == this->m_routeNext) {
	++this->m_routeNext;
	responseSend_(ZuMv(response));
	while (auto next = this->m_routeResponses.headNode()) {
	  if (next->data().routeSequence() != this->m_routeNext) break;
	  ++this->m_routeNext;
	  responseSend_(this->m_routeResponses.shift());
	}
	return;
      }
      bool duplicate = false, inserted = false;
      {
	auto iter = this->m_routeResponses.iter();
	while (auto next = iter()) {
	  auto ordered = next->data().routeSequence();
	  if (sequence == ordered) { duplicate = true; break; }
	  if (sequence < ordered) {
	    this->m_routeResponses.insert(iter, response);
	    inserted = true;
	    break;
	  }
	}
      }
      if (duplicate) {
	--server->stats().queuedResponses;
	response->data().completed({ResponseCode::Discarded});
	return;
      }
      if (inserted) return;
      this->m_routeResponses.pushNode(ZuMv(response));
    }

    void responseSend_(ResponseRef response) {
      auto server = impl()->app()->server;
      server->assertTx_();
      if (!impl()->active()) {
	--server->stats().queuedResponses;
	response->data().completed({ResponseCode::Discarded});
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
      --server->stats().queuedResponses;
      m_disconnect = m_response->data().disconnect();
      auto response = m_response;
      auto ptr = response.ptr();
      auto policy = response->data().bodyPolicy();
      bool ok = server->template startResponse_<Profile>(
	ZmRef(impl()), ZuMv(response), policy, m_retainedBytes);
      // The initial producer callback may synchronously finish or abort.
      if (m_response.ptr() != ptr) return;
      if (!ok)
	responseBuildFailed_(ptr);
      else if (!BodyPolicy::streaming(policy))
	responseDone_(ptr, true);
    }

    void responseDone_(ResBuilder *response, bool ok) {
      if (m_response.ptr() != response) return;
      auto server = impl()->app()->server;
      responseCancel_({});
      responseRelease_(m_retainedBytes);
      auto done = ZuMv(m_response);
      m_response = nullptr;
      bool disconnect = m_disconnect;
      ResponseCode::T code = ok ? ResponseCode::Submitted :
	ResponseCode::Failed;
      done->data().completed({code});
      if (!ok) ++server->stats().transportFailures;
      if (!ok)
	ZiLOG(Warning, "Zhttp", "HTTP response transport failed");
      if (!ok || disconnect) {
	responseCancelPending_();
	impl()->disconnect();
	return;
      }
      responseNext_();
    }

    void responseBuildFailed_(ResBuilder *response) {
      if (m_response.ptr() != response) return;
      ++impl()->app()->server->stats().responseBuildFailures;
	ZiLOG(Error, "Zhttp", "HTTP response construction failed");
      responseFailed_(response);
    }

    void responseAbort_(ResBuilder *response) {
      if (m_response.ptr() != response) return;
      responseFailed_(response);
    }

    void responseFailed_(ResBuilder *response) {
      if (m_response.ptr() != response) return;
      responseCancel_({});
      responseRelease_(m_retainedBytes);
      auto failed = ZuMv(m_response);
      m_response = nullptr;
      failed->data().completed({ResponseCode::Failed});
      responseCancelPending_();
      impl()->disconnect();
    }

    void responseCancelPending_() {
      auto server = impl()->app()->server;
      while (auto response = m_responses.shift()) {
	--server->stats().queuedResponses;
	response->data().completed({ResponseCode::Discarded});
      }
      if constexpr (!NativeTraits<Profile>::Multiplexed)
	while (auto response = this->m_routeResponses.shift()) {
	  --server->stats().queuedResponses;
	  response->data().completed({ResponseCode::Discarded});
	}
    }

    void responseCancelAll_() {
      auto fn = ZuMv(m_cancel);
      m_cancel = {};
      if (fn) fn();
      responseCancelPending_();
      if (m_response) {
	responseRelease_(m_retainedBytes);
	auto discarded = ZuMv(m_response);
	m_response = nullptr;
	discarded->data().completed({ResponseCode::Discarded});
      }
    }

    void responseCancel_(StreamCancelFn fn) {
      m_cancel = ZuMv(fn);
    }

    bool responseRetain_(uint64_t n) {
      return impl()->app()->server->retainResponse_(m_retainedBytes, n);
    }

    void responseRelease_(uint64_t n) {
      if (!n) return;
      auto server = impl()->app()->server;
      ZmAssert(m_retainedBytes >= n);
      ZmAssert(server->stats().retainedBytes.load_() >= n);
      m_retainedBytes -= n;
      server->stats().retainedBytes -= n;
    }

    ResQ			m_responses;
    ResponseRef	m_response;
    StreamCancelFn	m_cancel;
    uint64_t		m_retainedBytes = 0;
    bool		m_disconnect = false;
  };

  template <typename Profile, typename Link_>
  struct Session :
    public ServerSession<
      Session<Profile, Link_>, Parser<Profile, Link_>, MessageTraits<Profile>> {
    using Message = MessageTraits<Profile>;
    using Parser_ = Parser<Profile, Link_>;
    using Base = ServerSession<Session, Parser_, Message>;
    using Base::parser;

    uint64_t requestSequence() const { return m_requestSequence; }

    template <typename LinkT>
    void connected(LinkT &) { }

    template <typename LinkT, typename Rx>
    ZuInline int process(LinkT &link, Rx &rx) {
      if (terminal) return 0;
      if (this->complete) return 1;
      auto server = link.app()->server;
      if (!parser.active) {
	if constexpr (Message::ID == Version::H1)
	  if (!rx.length()) return 0;
	if (!server->admitRequest_()) return -1;
	++m_requestSequence;
	parser.begin();
      }
      return Base::process(link, rx);
    }

    template <typename LinkT>
    void disconnected(LinkT &, bool peer) {
      if (parser.active) {
	parser.notify(false);
	finish_();
      }
      parser.appParser.binding.disconnected(peer);
    }

    template <typename LinkT>
    int error(LinkT &, Parser_ &parser_) {
      const RequestError &error = parser_.error();
      if (error.code == RequestErrorCode::BodyRejected)
	++parser_.server->stats().bodyFailures;
      else
	++parser_.server->stats().parseFailures;
	ZiLOG(Warning, "Zhttp", ([error](auto &s) {
	  s << "HTTP request rejected code=" << int(error.code)
	    << " scope=" << int(error.scope)
	    << " responsePossible=" << error.responsePossible;
	}));
      parser_.notify(false);
      finish_();
      terminal = true;
      return -1;
    }

    template <typename LinkT>
    int request(LinkT &, Parser_ &) {
      finish_();
      return 1;
    }

  private:
    void finish_() {
      if (!parser.active) return;
      --parser.server->stats().activeRequests;
      parser.finish();
    }

    bool terminal = false;
    uint64_t m_requestSequence = 0;
  };

  template <typename Profile>
  struct Hub : public ZmNHeap<Hub<Profile>>, public ProtocolServer<
      Hub<Profile>, Profile,
      typename ServerImpl::template Link<Profile>> {
    friend inline ZuStringT<"Zhttp.Server.Hub"> ZmNHeapID(Hub *);

    using HTTP = NativeTraits<Profile>;
    using Link_ = ServerImpl::template Link<Profile>;
    using Link = Link_;
    ServerImpl *server = nullptr;

    Hub(ServerImpl *server_) : server{server_} { }
    ZiIP localIP() const { return server->config().localIP(); }
    unsigned localPort() const { return server->config().port(); }
    unsigned idleTimeout() const { return server->config().idleTimeout(); }
    template <typename Info>
    bool admit(const Info &) { return server->admit(); }
    template <typename LinkT>
    void connected(LinkT &link, const ConnectedInfo &info) {
      link.rxState().parser.connectedInfo(info);
      server->registerTxError_(link);
      if constexpr (!HTTP::Multiplexed)
	server->sessionConnected_(&link, HTTP::Transport::ID);
      server->impl()->connected(HTTP::Transport::ID);
    }
    template <typename LinkT>
    void disconnected(LinkT &link, bool) {
      link.responseDisconnected_();
      if constexpr (!HTTP::Multiplexed)
	server->sessionDisconnected_(&link, HTTP::Transport::ID);
    }
    void connected(Zhttp::Session session) {
      server->sessionConnected_(session);
    }
    void disconnected(Zhttp::Session session) {
      server->sessionDisconnected_(session);
    }
    void release() {
      server->release(HTTP::Transport::ID);
    }
    template <typename Info>
    void listening(const Info &info) {
      server->impl()->listening(HTTP::Transport::ID, info.port);
    }
    void listening() {
      server->impl()->listening(
	HTTP::Transport::ID, server->config().port());
    }
    void listenFailed(bool transient) {
      server->impl()->listenFailed(HTTP::Transport::ID, transient);
      server->failServer_();
    }
  };

  template <typename Profile>
  struct Link :
    public ZmNHeap<Link<Profile>>,
    public ServerLink<
      Hub<Profile>, Link<Profile>, Profile, Session<Profile, Link<Profile>>>,
    public Responses<Profile, Link<Profile>> {
    friend inline ZuStringT<"Zhttp.Server.Link"> ZmNHeapID(Link *);
    using Base = ServerLink<
      Hub<Profile>, Link, Profile, Session<Profile, Link>>;
    template <typename ...Args>
    Link(Args &&...args) : Base{ZuFwd<Args>(args)...} {
      this->rxState().parser.bind(this->app()->server, this);
    }
    using Responses<Profile, Link>::send;
    auto send(ZmRef<ZiIOBuf> buf) { return Base::send(ZuMv(buf)); }
    Channel &channel() { return m_channel; }

  private:
    Channel m_channel = Native_::serverChannel(static_cast<Link *>(this));
  };


public:
  ServerImpl(ServerCore *owner) : m_owner{owner} { }

  ServerCore *owner() const { return m_owner; }

  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  void connected(Zhttp::Session) { }
  void disconnected(Zhttp::Session) { }

  bool init(
    const HubConfig &hub, ServerConfig config) {
    return Engine::lock(ZmEngineState::Stopped, [this, &hub, &config]() {
      return init_(hub, ZuMv(config));
    });
  }

private:
  bool init_(
    const HubConfig &hub, ServerConfig config) {
    if (!config.port() || !hub.mx()) return false;
    if (m_hubs.count()) return true;
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = ZuMv(config);
    m_hpackSeeds.clear();
    m_qpackSeeds.clear();
    m_failed = false;
    m_altSvc.null();
    if (!m_owner->backends(hub)) return false;
    if (m_config.tlsEnabled() && m_tls.ctx && m_config.quicEnabled() &&
	m_quic.ctx && m_config.altSvcMaxAge())
      m_altSvc << "h3=\":" << m_config.port() << "\"; ma=" <<
	m_config.altSvcMaxAge();
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
	stopStreams_();
      });
    });
  }

  void stopStreams_() {
    unsigned n = StreamStopBatch;
    while (n--) {
      auto task = m_streamTasks.shift();
      if (!task) break;
      task->stop_();
    }
    if (!m_streamTasks.empty_()) {
      txRun_([this]() { stopStreams_(); });
      return;
    }
    stopTransports_();
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
    ZmAssert(m_streamTasks.empty_());
    m_hubs.final();
    m_hpackSeeds.final();
    m_qpackSeeds.final();
    m_altSvc.null();
    m_txErrorFn = ZiTxErrorFn{};
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
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
    if (n > FixedBodyMax) n = FixedBodyMax;
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
    impl()->disconnected(transport);
  }

  void sessionConnected_(const void *id, Transport::T transport) {
    sessionConnected_(Zhttp::Session{uintptr_t(id), transport});
  }

  void sessionConnected_(Zhttp::Session session) {
    impl()->connected(session);
  }

  void sessionDisconnected_(const void *id, Transport::T transport) {
    sessionDisconnected_(Zhttp::Session{uintptr_t(id), transport});
  }

  void sessionDisconnected_(Zhttp::Session session) {
    impl()->disconnected(session);
  }

  ZiMultiplex		*m_mx = nullptr;
  ServerConfig		m_config;
  MessageString		m_altSvc;
  Backend		m_tcp;
  Backend		m_tls;
  Backend		m_quic;
  using QPackSeeds = HeaderSeedCatalog;
  QPackSeeds		m_qpackSeeds;
  HPackSeedCatalog	m_hpackSeeds;
  Hubs			m_hubs;
  ZiTxErrorFn		m_txErrorFn;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;

  Server_::Stats		m_stats;
  ZmAtomic<unsigned>	m_admitRequests = 0;
  ZmAtomic<unsigned>	m_failed = 0;

  alignas(Zm::CacheLineSize)
  StreamTaskQ		m_streamTasks;
  ServerCore *m_owner;

};

} // namespace Zhttp

#endif /* ZhttpServerImpl_HH */
