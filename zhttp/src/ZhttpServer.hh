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
#include <zlib/ZmContext.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiIOBuf.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpH2Hub.hh>
#include <zlib/ZhttpH3.hh>
#include <zlib/ZhttpTransport.hh>

namespace Zhttp {

struct RequestBody {
  uint64_t received = 0;
  uint64_t consumed = 0;
  uint64_t pending = 0;
  uint64_t reset = 0;
  uint64_t discarded = 0;
};

struct ResponseResult {
  BodyCommit		body;
  ResponseOutcome::T	outcome = ResponseOutcome::BuildFailed;
};

#if 0
// Extended response Builder contract used by Server. Each call to
// Workload::respond() emits exactly one concrete response value; different
// calls and branches may emit unrelated response types. Headers, optional
// Trailers and bodyPolicy() are properties of each concrete type. Server may
// move the emitted response; applications must not retain references to it or
// its transient state. Server calls reset() exactly once before any other
// response callback, then retains the concrete type in its protocol adapter.
// All calls originate on the Tx shard. Other than reset() being first,
// callback order is protocol-dependent; in particular, fixed H2/H3 bodies can
// be built and patched before headers.
//
// For synchronous body policies, body() follows the Builder contract above.
// A streaming response may instead provide next(); Server retains that
// response and repeatedly requests bounded pooled buffers. done() may be
// called from another thread; Server posts it to the Tx shard before accessing
// link-owned state. An asynchronous response does not also provide body().
struct Response : public Builder {
  unsigned status();
  template <typename L> void reason(L &&l);	// l(value), H1 only
  bool close() const;

  // Optional asynchronous alternative to body(), present only when
  // BodyPolicy::Stream/OptionalStream. done(ZmRef<ZiIOBuf>, final).
  template <typename Done> void next(unsigned max, Done done);
};
#endif

ZtEnumStruct(ZhttpAPI, RequestDisposition, int8_t,
  Continue, Disconnect);

ZtEnumStruct(ZhttpAPI, RequestPhase, int8_t,
  Receiving, Queued, Sending, Committed, Completing, Done);

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
  auto transmit(Builder &) { return txStream(); }
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
    if (m_version == Version::H1) {
      m_h1TxLast = last;
      m_h1TxReady = true;
    }
    if (!Base::send(ZuMv(buf))) {
      if (m_h1TxLast == last) m_h1TxLast = nullptr;
      m_h1TxOutcome = ResponseOutcome::TxFailed;
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

  ZmRef<H1Logical>	m_h1;
  ZiIP			m_remoteIP;
  uint16_t		m_remotePort = 0;
  Transport_::TxCompleteFn m_h1Complete;
  ZiIOBuf		*m_h1TxLast = nullptr;
  ResponseOutcome::T	m_h1TxOutcome = ResponseOutcome::Success;
  int8_t		m_policy = H2Policy::Force;
  int8_t		m_version = -1;
  bool			m_draining = false;
  bool			m_down = false;
  bool			m_stopping = false;
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
      fn(this->error() == Zquic::StreamError::None ?
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
  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  H3Cxn		h3;
  Logical		logical;
  ZiIP			remoteIP;
  uint16_t		remotePort = 0;
  bool			notified = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  H3::QPackTxTable	h3Tx;
  bool			h3PeerCapTx = false;
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
	static_cast<NativeStream *>(ptr)->h3StreamError(error);
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
    if (m_stream) m_stream->txComplete_(false);
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

ZuDerive(MessageString, ZtString<ZtStringHeapID<"Zhttp.Message">>);

struct RequestMeta {
  Method::T	method = -1;
  MessageString	target;
  MessageString	pathStorage;
  MessageString	queryStorage;
  MessageString	authority;
  MessageString	protocol;
  ZiIP		remoteIP;
  uint64_t	bodyReceived = 0;
  uint64_t	bodyConsumed = 0;
  uint64_t	bodyReset = 0;
  uint64_t	bodyDiscarded = 0;
  uint32_t	pathOffset = 0;
  uint32_t	pathLength = 0;
  uint32_t	queryOffset = 0;
  uint32_t	queryLength = 0;
  uint16_t	remotePort = 0;
  Scheme::T	scheme = -1;
  TargetForm::T	form = TargetForm::Origin;
  Transport::T	transport = Transport::TCP;
  Version::T	httpVersion = Version::H1;
  bool		secure = false;
  bool		http10 = false;
  bool		hasQuery = false;
  bool		pathStored = false;
  bool		queryStored = false;

  ZuBSpan path() const {
    if (pathStored) return pathStorage;
    if (!target) return {};
    return
      ZuBSpan{reinterpret_cast<const uint8_t *>(target.data()) + pathOffset,
	pathLength};
  }
  ZuBSpan query() const {
    if (queryStored) return queryStorage;
    if (!target) return {};
    return
      ZuBSpan{reinterpret_cast<const uint8_t *>(target.data()) + queryOffset,
	queryLength};
  }
};

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

// Workload_ is a plain application struct.  Request and each emitted Response
// conform to the application contracts documented in Zhttp.hh.  respond()
// calls emit(response) exactly once.
#if 0
struct Workload {
  using Request = AppRequest;

  Request request();

  template <typename Emit>
  void respond(const RequestMeta &meta, Request &request, Emit &&emit) {
    if (meta.path() == "/health") {
      emit(EmptyResponse{204});
      return;
    }
    if (meta.path() == "/record") {
      emit(JSONResponse{request.record});
      return;
    }
    emit(FileResponse{lookup(meta.path())});
  }
  // EmptyResponse, JSONResponse, and FileResponse are unrelated concrete
  // types which each conform to Response; emit() consumes the selected value.
};
#endif

template <typename Workload_>
class Server : public ZmEngine<Server<Workload_>> {
public:
  using Engine = ZmEngine<Server<Workload_>>;
  using Workload = Workload_;
  using AppRequest = typename Workload::Request;
  using ReqHeaders = typename AppRequest::Headers;
  using Engine::running;
  using Engine::start;
  using Engine::state;
  using Engine::stop;
  using Engine::stopping;
  static constexpr uint64_t ReqBodyMax =
    ParserBodyMax<AppRequest>::V;

private:
  template <typename Protocol> struct Link;
  struct TLSHub;
  struct TLSH1Link;
  struct TLSH2Link;

  template <typename Profile>
  using ProfileLink = ZuIf<ZuIsSame<Profile, H1TLS>{}, TLSH1Link,
    ZuIf<ZuIsSame<Profile, H2TLS>{}, TLSH2Link, Link<Profile>>>;

public:
  template <typename Profile>
  struct LiveReq_ : public ZmObject {
    ZmRef<ProfileLink<Profile>> link;
    RequestMeta		meta;
    AppRequest		request;
    RequestBody		requestBody;
    ResponseResult	response;
    uint64_t		retainedBytes = 0;
    RequestErrorCode::T	errorCode = -1;
    RequestPhase::T	phase = RequestPhase::Receiving;
    bool		close = false;
    bool		completionPosted = false;
  };
  template <typename Profile>
  using LiveReqQ = ZmList<LiveReq_<Profile>,
    ZmListNode<LiveReq_<Profile>,
      ZmListHeapID<"Zhttp.Server.LiveReq">>>;
  template <typename Profile>
  using LiveReq = typename LiveReqQ<Profile>::Node;

  template <typename Profile>
  struct H1Ready_ : public ZmObject {
    H1Ready_(ProfileLink<Profile> *link_) : link{link_} { }
    ProfileLink<Profile> *link = nullptr;
  };
  template <typename Profile>
  using H1ReadyQ = ZmList<H1Ready_<Profile>,
    ZmListNode<H1Ready_<Profile>,
      ZmListHeapID<"Zhttp.Server.H1Ready">>>;
  template <typename Profile>
  using H1Ready = typename H1ReadyQ<Profile>::Node;

private:
  template <typename Protocol> struct Session;
  template <typename Protocol> struct Hub;
  template <typename Profile> struct Parser;

  template <typename Profile>
  struct ResponseQueue {
    LiveReqQ<Profile> fifo;
    H1ReadyQ<Profile> ready;
    bool posted = false;
  };

  template <typename Profile>
  struct H1ResponseState {
    LiveReqQ<Profile> responses;
    ZmRef<H1Ready<Profile>> readyNode;
    LiveReq<Profile> *activeResponse = nullptr;
    bool responseReady = false;
  };
  struct NoResponseState { };
  template <typename Profile>
  using LinkResponseState = ZuIf<
    MessageTraits<Profile>::ID == Version::H1,
    H1ResponseState<Profile>, NoResponseState>;

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
  ResponseQueue<Profile> &responseQueue_() {
    if constexpr (ZuIsSame<Profile, H1TCP>{}) return m_h1TCPResponses;
    else if constexpr (ZuIsSame<Profile, H1TLS>{}) return m_h1TLSResponses;
    else if constexpr (ZuIsSame<Profile, H2TLS>{}) return m_h2TLSResponses;
    else return m_h3QUICResponses;
  }

  template <typename Profile>
  struct Parser :
    public MessageTraits<Profile>::template RequestParser<
      Parser<Profile>, ReqHeaders, ReqBodyMax> {
    using Base = typename MessageTraits<Profile>::template RequestParser<
      Parser, ReqHeaders, ReqBodyMax>;
    using State = typename Base::State;

    void reset() { Base::reset(); live = nullptr; }
    void operation(Method::T method, const RequestTarget &target) {
      auto &request = live->meta;
      request.method = method;
      request.target = ZuCSpan{target.raw};
      request.authority = ZuCSpan{target.authority.raw};
      request.protocol = ZuCSpan{target.protocol};
      request.scheme = target.scheme;
      request.form = target.form;
      request.hasQuery = target.hasQuery;
      auto offsets = [&target](ZuBSpan value, uint32_t &offset,
	  uint32_t &length) {
	if (!target.raw || !value) return false;
	uintptr_t raw = reinterpret_cast<uintptr_t>(target.raw.data());
	uintptr_t data = reinterpret_cast<uintptr_t>(value.data());
	if (data < raw || data - raw > target.raw.length() ||
	    value.length() > target.raw.length() - (data - raw))
	  return false;
	offset = uint32_t(data - raw);
	length = uint32_t(value.length());
	return true;
      };
      request.pathStored = !offsets(
	target.path, request.pathOffset, request.pathLength);
      if (request.pathStored) request.pathStorage = ZuCSpan{target.path};
      request.queryStored = !offsets(
	target.query, request.queryOffset, request.queryLength);
      if (request.queryStored) request.queryStorage = ZuCSpan{target.query};
      live->request.operation(method, target);
    }
    void version(ZuBSpan version_) {
      auto &request = live->meta;
      request.http10 = ZuCSpan{version_} == "HTTP/1.0";
      live->request.version(version_);
    }
    template <typename Key>
    void header(ZuBSpan value) {
      live->request.template header<Key>(value);
    }
    void contentLength(uint64_t value) { live->request.contentLength(value); }
    void chunked() { live->request.chunked(); }
    void status(unsigned) { }
    template <typename Rx>
    void body(Rx &rx) {
      auto &request = live->meta;
      auto &requestBody = live->requestBody;
      uint64_t before = rx.length();
      if (before < requestBody.pending) {
	return;
      }
      requestBody.received += before - requestBody.pending;
	live->request.body(rx);
      uint64_t pending = rx.length();
      if (pending > before) return;
      requestBody.consumed += before - pending;
      requestBody.pending = pending;
      request.bodyReceived = requestBody.received;
      request.bodyConsumed = requestBody.consumed;
      request.bodyReset = requestBody.reset;
      request.bodyDiscarded = requestBody.discarded;
    }
    void complete(typename State::T state) {
      auto &request = live->meta;
      auto &requestBody = live->requestBody;
      requestBody.reset += requestBody.pending;
      requestBody.discarded += requestBody.pending;
      requestBody.pending = 0;
      request.bodyReceived = requestBody.received;
      request.bodyConsumed = requestBody.consumed;
      request.bodyReset = requestBody.reset;
      request.bodyDiscarded = requestBody.discarded;
      live->request.complete(state == State::Complete);
    }

    Server			*server = nullptr;
    ZmRef<LiveReq<Profile>>	live;
  };

  struct ErrorResponse {
    using Headers = ZuTypeList<>;

    unsigned status_ = 400;

    void reset() { }
    unsigned status() const { return status_; }
    template <typename L> void reason(L &&l) const { l(""); }
    template <typename Key, typename L> void header(L &&) const { }
    template <typename L> void header(L &&) const { }
    bool close() const { return false; }
    constexpr BodyPolicy::T bodyPolicy() const { return BodyPolicy::None; }
  };

  template <typename Profile, typename Builder>
  struct ResponseOps {
    using Headers = typename Builder::Headers;

    Server	*server = nullptr;
    Builder	builder;
    HeaderPatches<Headers> patches;
    uint64_t	produced = 0;

    unsigned status() const { return builder.status(); }
    template <typename L>
    void reason(L &&l) const { builder.reason(ZuFwd<L>(l)); }
    template <typename Key, typename L>
    void header(L &&l) {
      if constexpr (Key{}() == "content-length")
	if (rejectContentLength) return;
      if (suppressPads) {
	builder.template header<Key>([&l]<typename V>(V &&v) {
	  if constexpr (!IsPlaceholder<ZuDecay<V>>{})
	    l(ZuFwd<V>(v));
	});
	return;
      }
      patches.template header<
	MessageTraits<Profile>::ID == Version::H1, Key>(
	builder, ZuFwd<L>(l));
    }
    template <typename L>
    void header(L &&l) {
      builder.header([this, &l]<typename K, typename V>(K &&k, V &&v) {
	ZtString<ZtStringHeapID<"Zhttp.RuntimeHeader.Name">> name;
	name << k;
	if (!validRuntimeHeader<Headers>(ZuCSpan{name}, h1)) {
	  headersOK = false;
	  return;
	}
	l(ZuFwd<K>(k), ZuFwd<V>(v));
      });
      if (server->m_altSvc) l("alt-svc", server->m_altSvc);
    }
    void patch() { patches.patch(builder); }
    uint64_t contentLength() const { return produced; }
    bool headersValid() const { return headersOK; }
    Builder &appBuilder() { return builder; }
    template <typename Emit>
    void emitBody(Emit &&emit) {
      if constexpr (HasBuilderBody<Builder, Emit &&>{})
	builder.body(ZuFwd<Emit>(emit));
    }

    bool	h1 = false;
    bool	headersOK = true;
    bool	suppressPads = false;
    bool	rejectContentLength = false;
  };

  template <
    typename Profile, typename Builder,
    bool HasBody, bool Streaming>
  struct ResponseTx :
    public MessageTraits<Profile>::template Response<
      ResponseTx<Profile, Builder, HasBody, Streaming>,
      typename Builder::Headers,
      typename BuilderTrailers<Builder>::T, HasBody, Streaming>,
    public ResponseOps<Profile, Builder> {
    using Base = typename MessageTraits<Profile>::template Response<
      ResponseTx, typename Builder::Headers,
      typename BuilderTrailers<Builder>::T, HasBody, Streaming>;
    using Ops = ResponseOps<Profile, Builder>;
    ResponseTx(
      Server *server, Builder builder, bool suppressPads = false,
      bool rejectContentLength = false) :
      Ops{server, ZuMv(builder)} {
      this->h1 = MessageTraits<Profile>::ID == Version::H1;
      this->suppressPads = suppressPads;
      this->rejectContentLength = rejectContentLength;
    }

    bool streamResponse() const { return false; }
    using Ops::contentLength;
    using Ops::emitBody;
    using Ops::header;
    using Ops::reason;
    using Ops::status;

  };

  template <typename Profile, typename Builder, typename Heap>
  struct AsyncBody_ : public Heap, public ZmObject {
    using Self = AsyncBody_<Profile, Builder, Heap>;
    using Tx = ResponseTx<Profile, Builder, true, true>;

    AsyncBody_(
      Server *server_, ZmRef<LiveReq<Profile>> live_, Builder builder) :
      server{server_}, live{ZuMv(live_)}, response{server_, ZuMv(builder)} { }

    bool start(ZmRef<Self> self) {
      auto tx = live->link->transmit(response);
      response.begin(tx);
      if (!response.headersValid()) return false;
      task = server->addBodyTask_(BodyCancelFn{
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
      if (!available) { fail_(ResponseOutcome::BuildFailed); return; }
      reserved = available;
      server->m_stats.retainedBytes += reserved;
      live->retainedBytes += reserved;
      ++server->m_bodyPending;
      response.builder.next(unsigned(available), BodyChunkFn{
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
      if (!live->link->active()) {
	fail_(ResponseOutcome::Reset);
	return;
      }
      if (!buf || !buf->length || buf->length > reserved) {
	fail_(ResponseOutcome::BuildFailed);
	return;
      }
      uint64_t unused = reserved - buf->length;
      reserved = 0;
      if (unused) {
	server->m_stats.retainedBytes -= unused;
	live->retainedBytes -= unused;
      }
      auto tx = live->link->transmit(response);
      auto body = response.body(tx);
      body << ZuBSpan{buf->data(), buf->length};
      body.flush();
      response.produced += buf->length;
      if (!body.valid()) { fail_(ResponseOutcome::TxFailed); return; }
      if (final) {
	response.finish(tx);
	committed_(live->response.body, response.produced);
	live->response.outcome = ResponseOutcome::Success;
	live->phase = RequestPhase::Committed;
	finishTask_();
	live->link->finish();
	server->template postCommitted_<Profile>(live);
	return;
      }
      if (!live->link->txFence(Transport_::TxCompleteFn{
	  [self = ZmRef<Self>{this}](ResponseOutcome::T outcome) mutable {
	    self->fenced_(outcome);
	  }}))
	fail_(ResponseOutcome::TxFailed);
    }

    void fenced_(ResponseOutcome::T outcome) {
      server->txRun_([self = ZmRef<Self>{this}, outcome]() mutable {
	if (self->cancelled) return;
	self->release_();
	if (outcome == ResponseOutcome::Success) self->next_();
	else self->fail_(outcome);
      });
    }

    void release_() {
      if (!live->retainedBytes) return;
      ZmAssert(
	server->m_stats.retainedBytes.load_() >= live->retainedBytes);
      server->m_stats.retainedBytes -= live->retainedBytes;
      live->retainedBytes = 0;
    }

    void fail_(ResponseOutcome::T outcome) {
      if (cancelled) return;
      cancelled = true;
      finishTask_();
      live->link->txCancel();
      live->response.outcome = outcome;
      live->phase = RequestPhase::Completing;
      server->template postCompleted_<Profile>(ZuMv(live), outcome);
    }

    void cancel_() {
      task = nullptr;
      fail_(ResponseOutcome::Cancelled);
    }

    void finishTask_() {
      if (!task) return;
      server->delBodyTask_(task);
      task = nullptr;
    }

    Server			*server;
    ZmRef<LiveReq<Profile>>	live;
    Tx				response;
    typename BodyTaskQ::Node	*task = nullptr;
    uint64_t			reserved = 0;
    bool			cancelled = false;
  };

  template <typename Profile, typename Builder>
  using AsyncBody = AsyncBody_<Profile, Builder,
    ZmHeap<"Zhttp.Server.AsyncBody", AsyncBody_<Profile, Builder, ZuVoid>>>;

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

  static void committed_(BodyCommit &commit, uint64_t produced = 0) {
    commit.produced = produced;
    commit.committed = produced;
    commit.headers = true;
    commit.final = true;
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

  template <typename Profile>
  void postCommitted_(ZmRef<LiveReq<Profile>> live) {
    auto hub = live->link->app();
    hub->rxRun([this, live = ZuMv(live)]() mutable {
      m_workload->committed(
	live->meta, live->request, live->response.body);
    });
  }

  template <typename Profile, typename Builder>
  bool startAsyncBody_(ZmRef<LiveReq<Profile>> live, Builder builder) {
    using State = AsyncBody<Profile, Builder>;
    ZmRef<State> state = new State{this, ZuMv(live), ZuMv(builder)};
    return state->start(state);
  }

  template <typename Profile, typename Link_, typename Builder>
  struct ServerTxOps {
    Server	*server;
    Link_	*link_;
    BodyCommit	*commit;
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
	server, ZuMv(builder), true};
      auto tx = link_->transmit(empty);
      empty.begin(tx);
      if (!empty.headersValid()) return false;
      empty.finish(tx);
      committed_(*commit);
      link_->finish();
      return true;
    }
    template <bool> bool fail() { return false; }
    bool complete(uint64_t n) {
      if (!server->retainResponse_(*retainedBytes, n)) return false;
      committed_(*commit, n);
      return true;
    }
  };

  template <typename Profile, typename Link_, typename Builder_>
  bool sendResponse_(
      Link_ &link, Method::T method, Builder_ &&builder_, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using Builder = ZuDecay<Builder_>;
    unsigned status = builder_.status();
    auto policy = builder_.bodyPolicy();
    if (!BodyPolicy::hasBody(policy)) {
      ResponseTx<Profile, Builder, false, false> response{
	this, ZuFwd<Builder_>(builder_), true,
	contentLengthForbidden_(method, status)};
      auto tx = link.transmit(response);
      response.begin(tx);
      if (!response.headersValid()) return false;
      response.finish(tx);
      committed_(commit);
      link.finish();
      return true;
    } else {
      if (!bodyAllowed_(method, status)) {
	ResponseTx<Profile, Builder, false, false> response{
	  this, ZuFwd<Builder_>(builder_), true,
	  contentLengthForbidden_(method, status)};
	auto tx = link.transmit(response);
	response.begin(tx);
	if (!response.headersValid()) return false;
	response.finish(tx);
	committed_(commit);
	link.finish();
	return true;
      }
      if (BodyPolicy::streaming(policy))
	return sendStreamingResponse_<Profile>(
	  link, ZuFwd<Builder_>(builder_), BodyPolicy::optional(policy),
	  commit, retainedBytes);
      else
	return sendFixedResponse_<Profile>(
	  link, ZuFwd<Builder_>(builder_), BodyPolicy::optional(policy),
	  commit, retainedBytes);
    }
  }

  template <typename Profile, typename Link_, typename Builder_>
  bool sendStreamingResponse_(
      Link_ &link, Builder_ &&builder_, bool optional, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using Builder = ZuDecay<Builder_>;
    ResponseTx<Profile, Builder, true, true> response{
      this, ZuFwd<Builder_>(builder_)};
    ServerTxOps<Profile, Link_, Builder> ops{
      this, &link, &commit, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.streaming(response, optional);
  }

  template <typename Profile, typename Link_, typename Builder_>
  bool sendFixedResponse_(
      Link_ &link, Builder_ &&builder_, bool optional, BodyCommit &commit,
      uint64_t &retainedBytes) {
    using Builder = ZuDecay<Builder_>;
    ResponseTx<Profile, Builder, true, false> response{
      this, ZuFwd<Builder_>(builder_)};
    ServerTxOps<Profile, Link_, Builder> ops{
      this, &link, &commit, &retainedBytes};
    MessageTx<MessageTraits<Profile>, decltype(ops)> tx{ops};
    return tx.fixed(response, optional);
  }

  enum { ResponseDrainBatch = 16 };

  template <typename Profile>
  void completeLiveRx_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome,
      bool disconnect = true) {
    if (live->phase == RequestPhase::Done) return;
    live->response.outcome = outcome;
    switch (outcome) {
      case ResponseOutcome::BuildFailed:
	++m_stats.responseBuildFailures;
	break;
      case ResponseOutcome::TxFailed:
      case ResponseOutcome::Reset:
	++m_stats.transportFailures;
	break;
      default:
	break;
    }
    live->phase = RequestPhase::Completing;
    m_workload->completed(live->meta, live->request, live->response);
    live->phase = RequestPhase::Done;
    if (disconnect &&
	(live->close || live->response.outcome != ResponseOutcome::Success))
      live->link->disconnect();
    --m_stats.activeRequests;
  }

  template <typename Profile>
  void postCompletedTx_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome) {
    if (live->completionPosted) return;
    live->completionPosted = true;
    if (live->retainedBytes) {
      ZmAssert(m_stats.retainedBytes.load_() >= live->retainedBytes);
      m_stats.retainedBytes -= live->retainedBytes;
      live->retainedBytes = 0;
    }
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      if (live->link->activeResponse == live.ptr()) {
	live->link->activeResponse = nullptr;
	readyH1_<Profile>(live->link.ptr());
	scheduleResponses_<Profile>(live->link->app());
      }
    }
    auto hub = live->link->app();
    hub->rxRun([
      this, live = ZuMv(live), outcome]() mutable {
      completeLiveRx_<Profile>(ZuMv(live), outcome);
    });
  }

  template <typename Profile>
  void postCompleted_(
      ZmRef<LiveReq<Profile>> live, ResponseOutcome::T outcome) {
    if (m_mx->invoked(m_txThread)) {
      postCompletedTx_<Profile>(ZuMv(live), outcome);
      return;
    }
    txRun_([this, live = ZuMv(live), outcome]() mutable {
      postCompletedTx_<Profile>(ZuMv(live), outcome);
    });
  }

  uint64_t retainedAvailable_() const {
    uint64_t max = m_config.retainedBytesMax();
    uint64_t used = m_stats.retainedBytes.load_();
    return used < max ? max - used : 0;
  }

  bool retainResponse_(uint64_t &retainedBytes, uint64_t n) {
    if (n > retainedAvailable_()) return false;
    m_stats.retainedBytes += n;
    retainedBytes = n;
    return true;
  }

  template <typename Profile>
  void sendLiveTx_(ZmRef<LiveReq<Profile>> live) {
    auto link = live->link;
    if (!link->active()) {
	live->response.outcome = ResponseOutcome::Reset;
	live->phase = RequestPhase::Completing;
	postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Reset);
	return;
    }
    auto emit = [this, &live, &link]<typename Builder_>(
	Builder_ &&builder_) {
	builder_.reset();
	if (live->phase != RequestPhase::Queued) {
	  live->response.outcome = ResponseOutcome::BuildFailed;
	  live->phase = RequestPhase::Completing;
	  live->close = true;
	  return;
	}
	live->phase = RequestPhase::Sending;
	live->close = live->close || builder_.close();
	link->txComplete(Transport_::TxCompleteFn{
	  [this, live_ = live](ResponseOutcome::T outcome) mutable {
	    postCompleted_<Profile>(ZuMv(live_), outcome);
	  }});
	using Builder = ZuDecay<Builder_>;
	auto policy = builder_.bodyPolicy();
	auto send = [this, &live, &link, &builder_]() {
	  bool sent = sendResponse_<Profile>(
	    *link, live->meta.method, ZuFwd<Builder_>(builder_),
	    live->response.body, live->retainedBytes);
	  if (sent) {
	    live->response.outcome = ResponseOutcome::Success;
	    live->phase = RequestPhase::Committed;
	  } else {
	    link->txCancel();
	    live->response.outcome = ResponseOutcome::BuildFailed;
	    live->phase = RequestPhase::Completing;
	  }
	};
	if constexpr (HasAsyncBody<Builder>{}) {
	  if (BodyPolicy::streaming(policy)) {
	    if (!startAsyncBody_<Profile>(
		  live, Builder{ZuFwd<Builder_>(builder_)})) {
	      link->txCancel();
	      live->response.outcome = ResponseOutcome::BuildFailed;
	      live->phase = RequestPhase::Completing;
	    }
	  } else
	    send();
	} else
	  send();
      };
    if (live->errorCode >= 0)
      emit(ErrorResponse{requestErrorStatus(live->errorCode)});
    else
      m_workload->respond(live->meta, live->request, emit);
    if (live->phase == RequestPhase::Committed) {
      postCommitted_<Profile>(live);
      return;
    }
    if (live->phase == RequestPhase::Sending) return;
    if (live->phase == RequestPhase::Queued) {
      live->response.outcome = ResponseOutcome::BuildFailed;
      live->phase = RequestPhase::Completing;
    }
    if (!live->response.body.final)
      postCompleted_<Profile>(ZuMv(live), ResponseOutcome::BuildFailed);
  }

  template <typename Profile>
  void drainResponses_() {
    auto &queue = responseQueue_<Profile>();
    unsigned sent = 0;
    while (sent < ResponseDrainBatch) {
      ZmRef<LiveReq<Profile>> live;
      if constexpr (MessageTraits<Profile>::ID == Version::H1) {
	auto ready = queue.ready.shift();
	if (!ready) break;
	auto link = ready->link;
	ZmAssert(link && link->responseReady && !link->activeResponse);
	link->responseReady = false;
	live = link->responses.shift();
	ZmAssert(live);
	link->activeResponse = live.ptr();
      } else {
	live = queue.fifo.shift();
	if (!live) break;
      }
      --m_stats.queuedResponses;
      sendLiveTx_<Profile>(ZuMv(live));
      ++sent;
    }
    bool pending;
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      pending = !queue.ready.empty_();
    else
      pending = !queue.fifo.empty_();
    if (pending) {
      txRun_([this]() { drainResponses_<Profile>(); });
    } else {
      queue.posted = false;
    }
  }

  template <typename Profile, typename Hub_>
  void scheduleResponses_(Hub_ *hub) {
    auto &queue = responseQueue_<Profile>();
    bool pending;
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      pending = !queue.ready.empty_();
    else
      pending = !queue.fifo.empty_();
    if (queue.posted || !pending) return;
    queue.posted = true;
    hub->txRun([this]() { drainResponses_<Profile>(); });
  }

  template <typename Profile>
  void readyH1_(ProfileLink<Profile> *link) {
    auto &queue = responseQueue_<Profile>();
    if (link->activeResponse || link->responseReady ||
	link->responses.empty_()) return;
    if (!link->readyNode)
      link->readyNode = new H1Ready<Profile>{link};
    queue.ready.pushNode(link->readyNode);
    link->responseReady = true;
  }

  template <typename Profile>
  void enqueueResponse_(ZmRef<LiveReq<Profile>> live) {
    auto &queue = responseQueue_<Profile>();
    auto hub = live->link->app();
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      auto link = live->link;
      link->responses.pushNode(ZuMv(live));
      readyH1_<Profile>(link);
    } else {
      queue.fifo.pushNode(ZuMv(live));
    }
    scheduleResponses_<Profile>(hub);
  }

  template <typename Profile>
  void detachH1_(ProfileLink<Profile> *link) {
    if (!link->responseReady) return;
    auto &ready = responseQueue_<Profile>().ready;
    if (ready.headPtr() == link->readyNode.ptr())
      ready.shift();
    else
      ready.delNode(link->readyNode.ptr());
    link->responseReady = false;
  }

  template <typename Profile>
  void cancelH1Tx_(ProfileLink<Profile> *link) {
    detachH1_<Profile>(link);
    while (auto live = link->responses.shift()) {
      --m_stats.queuedResponses;
      postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Cancelled);
    }
  }

  template <typename Profile, typename Link_>
  void disconnectedH1_(Link_ *link) {
    if constexpr (MessageTraits<Profile>::ID == Version::H1)
      txRun_([this, link = ZmMkRef(link)]() {
	cancelH1Tx_<Profile>(link);
      });
  }

  template <typename Profile>
  void cancelResponses_() {
    auto &queue = responseQueue_<Profile>();
    if constexpr (MessageTraits<Profile>::ID == Version::H1) {
      while (auto ready = queue.ready.shift()) {
	auto link = ready->link;
	link->responseReady = false;
	cancelH1Tx_<Profile>(link);
      }
    } else {
      while (auto live = queue.fifo.shift()) {
	--m_stats.queuedResponses;
	postCompleted_<Profile>(ZuMv(live), ResponseOutcome::Cancelled);
      }
    }
    queue.posted = false;
  }

  void cancelResponses_() {
    cancelResponses_<H1TCP>();
    cancelResponses_<H1TLS>();
    cancelResponses_<H2TLS>();
    cancelResponses_<H3QUIC>();
  }

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
      if (!parser.server) {
	parser.server = link.app()->server;
      }
      if (!parser.live) {
	if constexpr (Message::ID == Version::H1)
	  if (!rx.length()) return 0;
	if (!parser.server->admitRequest_()) return -1;
	parser.live = new LiveReq<Profile>;
	parser.live->link = ZmMkRef(&link);
	parser.live->request = parser.server->m_workload->request();
      }
      auto &request = parser.live->meta;
      if (!request.remoteIP) {
	request.remoteIP = link.remoteIP();
	request.remotePort = link.remotePort();
	request.transport = Message::Transport::ID;
	request.httpVersion = Message::ID;
	request.secure = Message::Transport::Secure;
      }
      return Base::process(link, rx);
    }

    template <typename Link_>
    void disconnected(Link_ &link, bool) {
      auto server = parser.server ? parser.server : link.app()->server;
      server->template disconnectedH1_<Profile>(&link);
      if (parser.live) {
	auto live = ZuMv(parser.live);
	auto &body = live->requestBody;
	body.reset += body.pending;
	body.discarded += body.pending;
	body.pending = 0;
	live->meta.bodyReceived = body.received;
	live->meta.bodyConsumed = body.consumed;
	live->meta.bodyReset = body.reset;
	live->meta.bodyDiscarded = body.discarded;
	live->request.complete(false);
	live->phase = RequestPhase::Completing;
	server->template completeLiveRx_<Profile>(
	  ZuMv(live), ResponseOutcome::Reset, false);
      }
    }

    template <typename Link_>
    int error(Link_ &link, Parser_ &parser) {
      auto server = link.app()->server;
      const RequestError &error = parser.error();
      if (!parser.live) return -1;
      ++server->m_stats.parseFailures;
      auto live = ZuMv(parser.live);
      if (!error.responsePossible ||
	  (error.scope != RequestErrorScope::Request &&
	   Message::ID != Version::H1)) {
	terminal = true;
	server->txRun_([server, live = ZuMv(live)]() mutable {
	  server->template postCompleted_<Profile>(
	    ZuMv(live), ResponseOutcome::Reset);
	});
	return -1;
      }
      RequestDisposition::T disposition = RequestDisposition::Disconnect;
      if (error.scope == RequestErrorScope::Request)
	disposition = server->m_workload->requestError(
	  live->meta, live->request, error);
      live->close = disposition == RequestDisposition::Disconnect;
      if (live->close || Message::OneMessagePerLink) terminal = true;
      live->errorCode = error.code;
      live->phase = RequestPhase::Queued;
      ++server->m_stats.queuedResponses;
      link.app()->txRun([server, live = ZuMv(live)]() mutable {
	server->template enqueueResponse_<Profile>(ZuMv(live));
      });
      if constexpr (Message::ID == Version::H1) {
	if (disposition == RequestDisposition::Continue) parser.reset();
      }
      return 1;
    }

    template <typename Link_>
    int request(Link_ &link, Parser_ &parser) {
      auto server = link.app()->server;
      ZmAssert(parser.live);
      auto live = ZuMv(parser.live);
      live->phase = RequestPhase::Queued;
      ++server->m_stats.queuedResponses;
      link.app()->txRun([
	server, live = ZuMv(live)]() mutable {
	server->template enqueueResponse_<Profile>(ZuMv(live));
      });
      return 1;
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
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_workload->connected(
	HTTP::Transport::ID);
    }
    void release() {
      server->release(HTTP::Transport::ID);
    }
    template <typename Info>
    void listening(const Info &info) {
      server->m_workload->listening(
	HTTP::Transport::ID, info.port);
    }
    void listening() {
      server->m_workload->listening(
	HTTP::Transport::ID, server->m_config.port());
    }
    void listenFailed(bool transient) {
      server->m_workload->listenFailed(
	HTTP::Transport::ID, transient);
      server->failServer_();
    }
  };

  template <typename Profile>
  struct Link :
    public ServerLink<
      Hub<Profile>, Link<Profile>, Profile, Session<Profile>>,
    public LinkResponseState<Profile> {
    using Base = ServerLink<
      Hub<Profile>, Link, Profile, Session<Profile>>;
    using Base::Base;
  };

  struct TLSHub : public TLS_::ServerHub<TLSHub> {
    using H1Link = TLSH1Link;
    using H2Link = TLSH2Link;
    Server *server = nullptr;

    TLSHub(Server *server_) : server{server_} { }
    ZiIP localIP() const { return server->m_config.localIP(); }
    unsigned localPort() const { return server->m_config.port(); }
    unsigned idleTimeout() const {
      return server->m_config.idleTimeout();
    }
    bool admit(const ZiCxnInfo &) { return server->admit(); }
    template <typename Link_>
    void connected(Link_ &link, const ConnectedInfo &) {
      server->registerTxError_(link);
      server->m_workload->connected(Transport::TLS);
    }
    template <typename Link_>
    void disconnected(Link_ &, bool) { }
    void release() { server->release(Transport::TLS); }
    void listening(const ZiListenInfo &info) {
      server->m_workload->listening(Transport::TLS, info.port);
    }
    void listenFailed(bool transient) {
      server->m_workload->listenFailed(Transport::TLS, transient);
      server->failServer_();
    }
  };

  struct TLSH1Link :
    public TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>,
    public H1ResponseState<H1TLS> {
    using Base = TLS_::ServerH1Logical<
      TLSHub, TLSH1Link, Session<H1TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
  };

  struct TLSH2Link :
    public H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>> {
    using Base = H2_::ServerLogical<
      TLSHub, TLSH2Link, Session<H2TLS>,
      TLS_::SrvLink<TLSHub>>;
    using Base::Base;
  };

public:
  Server() : m_tcp{this}, m_tls{this}, m_quic{this} { }

  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }

  // Request metadata callbacks precede request-body input.  Body input is a
  // concrete bounded stream on the Rx shard and is valid only for the
  // synchronous workload callback.  Message completion follows validated,
  // fully consumed input; no workload callback follows terminal completion.
  bool init(
    const HubConfig &hub, ServerConfig config, Workload *workload) {
    return Engine::lock(ZmEngineState::Stopped, [&]() {
      return init_(hub, ZuMv(config), workload);
    });
  }

private:
  friend Engine;

  bool init_(
    const HubConfig &hub, ServerConfig config, Workload *workload) {
    if (!workload || !config.port()) return false;
    if (!hub.mx()) return false;
    m_mx = hub.mx();
    m_rxThread = hub.rxThread() ?
      m_mx->sid(hub.rxThread()) : m_mx->rxThread();
    m_txThread = hub.txThread() ?
      m_mx->sid(hub.txThread()) : m_mx->txThread();
    m_config = ZuMv(config);
    m_workload = workload;
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
    // Order every pre-stop Rx admission/enqueue post before Tx cancellation.
    rxRun_([this]() {
      txRun_([this]() {
	cancelBodies_();
	if (!m_bodyPending) stopTransports_();
      });
    });
  }

  void stopTransports_() {
    cancelResponses_();
    m_hubs.stop([this](bool ok) {
      // Native hub teardown can enqueue terminal Tx and link Rx callbacks.
      // Drain both owners before allowing finalization to release them.
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
    ZmAssert(m_h1TCPResponses.fifo.empty_());
    ZmAssert(m_h1TLSResponses.fifo.empty_());
    ZmAssert(m_h1TCPResponses.ready.empty_());
    ZmAssert(m_h1TLSResponses.ready.empty_());
    ZmAssert(m_h2TLSResponses.fifo.empty_());
    ZmAssert(m_h3QUICResponses.fifo.empty_());
    ZmAssert(m_bodyTasks.empty_());
    ZmAssert(!m_bodyPending);
    m_hubs.final();
    m_altSvc.null();
    m_mx = nullptr;
    m_rxThread = 0;
    m_txThread = 0;
    m_workload = nullptr;
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
  uint64_t responseBuildFailures() const {
    return m_stats.responseBuildFailures.load_();
  }
  uint64_t transportFailures() const {
    return m_stats.transportFailures.load_();
  }
  unsigned hubCount() const { return m_hubs.count(); }

#ifdef Zquic_DEBUG
  void printQUICDiag() { m_quic.printDiag(); }
#endif

private:
  template <typename Link>
  void registerTxError_(Link &link) {
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

  uint64_t fixedBodyMax_() const {
    uint64_t n = m_config.retainedBodyMax();
    if (n > m_config.retainedMessageMax())
      n = m_config.retainedMessageMax();
    if (n > uint32_t(-1)) n = uint32_t(-1);
    return n;
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
    m_workload->disconnected(transport);
  }

  ZiMultiplex	*m_mx = nullptr;
  unsigned	m_rxThread = 0;
  unsigned	m_txThread = 0;
  ServerConfig	m_config;
  MessageString	m_altSvc;
  Workload	*m_workload = nullptr;
  Hub<H1TCP>	m_tcp;
  TLSHub	m_tls;
  Hub<H3QUIC>	m_quic;
  ResponseQueue<H1TCP> m_h1TCPResponses;
  ResponseQueue<H1TLS> m_h1TLSResponses;
  ResponseQueue<H2TLS> m_h2TLSResponses;
  ResponseQueue<H3QUIC> m_h3QUICResponses;
  BodyTaskQ	m_bodyTasks;
  Hubs	m_hubs;
  ZiTxErrorFn	m_txErrorFn;
  Stats		m_stats;
  unsigned	m_bodyPending = 0;
  ZmAtomic<unsigned>	m_admitRequests = 0;
  ZmAtomic<unsigned> m_failed = 0;
};

} // namespace Zhttp

#endif /* ZhttpServer_HH */
