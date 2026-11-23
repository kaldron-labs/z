//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC client link API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic_ {

using CliCloseFn = ZmFn<void(), ZmFnHeapID<"ZquicCliLink.CloseFn">>;

struct alignas(Zm::CacheLineSize) CliSharedState {
  ZmAtomic<uint64_t>	udpReadyCount = 0;
  ZmAtomic<unsigned>	udpReady = 0;
};

struct alignas(Zm::CacheLineSize) CliRxStorage {
  Host			server;
  ZiIP			remote;
  ZiSockAddr		localAddr;
  ZiSockAddr		remoteAddr;
  ClientBootstrap	bootstrap;
  TokenBytes		newToken;
  uint16_t		port = 0;
  bool			peerParamsValidated = false;
  bool			handshakeConfirmed = false;
  bool			handshakeDonePending = false;
  bool			notifyEndpointDown = true;
};

struct alignas(Zm::CacheLineSize) CliTxStorage {
  TokenBytes		initialToken;
  PktAllocTarget	probeTarget;
  ZiSockAddr		probeRemote;
  bool			preferred = false;
  bool			probeOutput = false;
  bool			probeCommit = false;
  CliCloseFn		closeFn;
  bool			closeNotify = false;
  bool			closePeer = false;
};

} // Zquic_

namespace Zquic {

template <
  typename App, typename Impl, typename Stream_>
class CliLink :
  public Link<App, Impl, Stream_>,
  public Endpoint_<CliLink<App, Impl, Stream_>> {
public:
  using Base = Link<App, Impl, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Endpoint = Endpoint_<CliLink>;
  using CloseFn = Zquic_::CliCloseFn;
  enum { EndpointRef = true };
  enum { TLSBufSize = (64<<10) }; // 64K
  enum { PNLength = 2 };
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  friend Endpoint;
  template <typename, typename> friend class Zquic::Stream;

  CliLink(App *app) : Base{app, false} { Base::initCryptoDelivery(); }

  Zquic_::PktAllocTarget pktAllocTarget() const {
    return m_cliTx.probeOutput ? m_cliTx.probeTarget : Endpoint::pktAllocTarget();
  }
  void pktPressureResume() {
    uint64_t runtime = Base::txRuntime();
    if (m_cliTx.probeOutput && m_cliTx.preferred && Base::migrationActive_())
      (void)sendMigChal(true);
    retransmit_();
    if (runtime == Base::txRuntime() && !Base::disconnecting_()) flushTx_();
  }
  void waitPktTx(Zquic_::EndpointTxWait &wait, uint64_t epoch, Zquic_::EndpointTxFn fn) {
    Endpoint::waitTx(wait, app()->txThread(), epoch, ZuMv(fn), pktAllocTarget());
  }
  bool sendEndpointPkt(ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    EcnMark::T ecn, bool priority = false) {
    // Socket promotion and CID commit cross owners. Resume ordinary output
    // only when both have completed on protocol Tx.
    if (ZuUnlikely(m_cliTx.probeCommit)) return false;
    return Base::submitPktTx(ZuMv(buf), ZuMv(addr), ecn, priority,
      [this](auto buf, ZiSockAddr addr, EcnMark::T ecn, bool priority,
	Zquic_::EndpointTxToken token, bool *blocked) {
	return Endpoint::send(
	  ZuMv(buf), ZuMv(addr), ecn, priority, token, blocked,
	  m_cliTx.probeOutput ? m_cliTx.probeTarget : Zquic_::PktAllocTarget{});
      });
  }
  void pktAllocFailed() {
    m_cliTx.closeNotify = true;
    Base::pktAllocFailed();
  }
  CliLink(App *app, ZuID id) : Base{app, false, ZuMv(id)} {
    Base::initCryptoDelivery();
  }
  CliLink(App *app, Host server, uint16_t port) :
    Base{app, false}, m_cliRx{.server = ZuMv(server), .port = port} {
    Base::initCryptoDelivery();
  }
  CliLink(App *app, ZuID id, Host server, uint16_t port) :
    Base{app, false, ZuMv(id)},
    m_cliRx{.server = ZuMv(server), .port = port} {
    Base::initCryptoDelivery();
  }
  ~CliLink() = default;

  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    return Base::allQueues(fn) + Endpoint::allQueues(fn);
  }

  void connect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->connect_0();
      return link;
    });
  }
  void connect(Host server, uint16_t port) {
    connect(ZuMv(server), port, ZiIP{});
  }
  void connect(Host server, uint16_t port, ZiIP remote) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), server = ZuMv(server), port, remote]() mutable {
      link->m_cliRx.server = ZuMv(server);
      link->m_cliRx.port = port;
      link->m_cliRx.remote = remote;
      link->connect_0();
      return link;
    });
  }

protected:
  template <typename L> void ownerLayout(L l) const {
    Base::ownerLayout(l);
    Endpoint::ownerLayout(l);
    l(&m_cliShared, sizeof(m_cliShared));
    l(&m_cliRx, sizeof(m_cliRx));
    l(&m_cliTx, sizeof(m_cliTx));
  }

  bool cachedNewToken() const {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC cached token access outside Rx thread", return false);
    return bool(m_cliRx.newToken);
  }
  void up_() { connect(); }

public:

  void disconnect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->disconnect_();
      return link;
    });
  }
  bool disconnect(uint64_t errorCode) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return false);
    bool accepted = !Base::closed();
    app()->rxInvoke(impl(), [link = impl(), errorCode]() mutable {
      link->disconnectRx(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC client disconnect outside Rx thread", return);
    if (!Base::linkClosedRx_()) Base::closeState(errorCode, true);
    if (Base::establishedRx() && Endpoint::connected() &&
	m_cliRx.remoteAddr) {
      app()->txRun([
	link = impl(),
	addr = m_cliRx.remoteAddr
      ]() mutable {
	if (link->disconnecting_()) return;
	(void)link->sendCloseFrame(ZuMv(addr), true);
	link->m_cliTx.closeNotify = false;
	link->m_cliTx.closePeer = false;
	link->enterLocalClosingTx();
      });
      return;
    }
    closeEndpointDrained(false);
  }
  template <typename L>
  void disconnect(L &&l) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), l = ZuFwd<L>(l)]() mutable {
      link->disconnect_(ZuMv(l));
      return link;
    });
  }
  void disconnect_() { // direct call from within rx thread
    disconnect_(CloseFn{});
  }
  template <typename L>
  void disconnect_(L &&l) { // direct call from within rx thread
    closeCurrent(true, false, ZuFwd<L>(l));
    Base::resetTLSRx();
  }
  void disconnected(bool) { }

  void abort() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->abort_();
      return link;
    });
  }
  template <typename L>
  void abort(L &&l) {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), l = ZuFwd<L>(l)]() mutable {
      link->abort_(ZuMv(l));
      return link;
    });
  }
  void abort_() {
    abort_(CloseFn{});
  }
  template <typename L>
  void abort_(L &&l) {
    Base::closeState();
    closeEndpointDrained(true, ZuFwd<L>(l));
  }

  const Host &server() const { return m_cliRx.server; }
  uint16_t port() const { return m_cliRx.port; }
  bool udpReady() const { return m_cliShared.udpReady; }
  uint64_t udpReadyCount() const { return m_cliShared.udpReadyCount; }
  bool ready() const { return m_cliShared.udpReady; }
  using Base::established;
  bool cxn() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const {
    return app()->txInvoked() ? Base::activeLocalTx() : m_cliRx.localAddr;
  }
  const ZiSockAddr &remote() const {
    return app()->txInvoked() ? Base::activeRemoteTx() : m_cliRx.remoteAddr;
  }
  template <typename L>
  void cxnDiag(L &&l) const { endpointDiag(ZuFwd<L>(l)); }
  template <typename L>
  void endpointDiag(L &&l) const {
    auto mx = app()->mx();
    if (mx->invoked(mx->txThread())) {
      EndpointDiag diag = Endpoint::diag();
      l(diag);
      return;
    }
    auto link = const_cast<CliLink *>(this)->impl();
    mx->txRun([link, l = ZuFwd<L>(l)]() mutable {
      EndpointDiag diag = link->Endpoint::diag();
      l(diag);
    });
  }
  using Base::diag;
  using Base::pathDiag;
  template <typename L>
  void pathInfo(L &&l) const {
    if (app()->txInvoked()) {
      l(Base::activePathInfo(), Base::candidatePathInfo_());
      return;
    }
    auto link = const_cast<CliLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->activePathInfo(), link->candidatePathInfo_());
    });
  }
  template <typename L>
  void migrationState(L &&l) const {
    if (app()->txInvoked()) {
      l(Base::migrationResult());
      return;
    }
    auto link = const_cast<CliLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->migrationResult());
    });
  }
  bool migrate(const MigrationParams &params) {
    if (Base::disconnecting_()) return false;
    if (app()->txInvoked())
      return migrate_(params);
    app()->txRun([link = impl(), params]() mutable {
      if (link->disconnecting_()) return;
      link->migrate_(params);
    });
    return true;
  }
  bool migrateLocal(ZiIP ip, uint16_t port = 0) {
    MigrationParams params;
    params.local.init(ip, port);
    params.rebindLocal = true;
    params.reason = MigrationReason::Active;
    params.closeOnFailure = app()->migCloseOnFail();
    return migrate(params);
  }
  bool migrateRemote(ZiSockAddr remote) {
    MigrationParams params;
    params.remote = ZuMv(remote);
    params.reason = MigrationReason::Active;
    params.closeOnFailure = app()->migCloseOnFail();
    return migrate(params);
  }
  bool pathValidated() const { return Base::pathValidated_(); }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }
  const Crypto &crypto() const { return Base::crypto_(); }

  using Base::send;
  using Base::send_;

  void ptoTimer() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client PTO outside Tx thread", return);
    if (Base::linkClosedTx_() || !Endpoint::connected())
      return;
    Base::ptoRecovery(
      [this](
	  PktNumSpace::T level, PktBuild &build,
	  const SentFrameRef &cryptoRef) {
	return level == PktNumSpace::Initial ?
	  sendInitialPkt(
	    build, Base::activePathRemote_(), {}, &cryptoRef, true, false) :
	  sendHandshakePkt(
	    build, Base::activePathRemote_(), {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  using Base::queueRetransmit;

  bool retransmit_(
    bool pto = false, unsigned maxPackets = 2,
    PktNumSpace::T onlySpace = PktNumSpace::N) { // Tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Endpoint::connected())
      return false;
    return Base::template retransmitFrames<false>(
      pto, maxPackets, onlySpace,
      [this](PktNumSpace::T level, PktBuild &build, const SentFrameRef &ref) {
	if (level == PktNumSpace::Initial)
	  return sendInitialPkt(build, Base::activePathRemote_(), {}, &ref, true, false);
	if (level == PktNumSpace::Handshake)
	  return sendHandshakePkt(build, Base::activePathRemote_(), {}, &ref, true, false);
	return sendShortPkt(build, Base::activePathRemote_(), {}, &ref, true);
      },
      [] { return false; });
  }

  bool sendPingProbe(PktNumSpace::T level) {
    PktBuild build;
    if (!Base::buildPingProbe(level, build)) return false;
    typename Base::TxPktRefs refs;
    switch (level) {
      case PktNumSpace::Initial:
	return sendInitialPkt(
	  build, Base::activePathRemote_(), {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt(
	  build, Base::activePathRemote_(), {}, nullptr, true, false);
      default:
	return sendShortPkt(
	  build, Base::activePathRemote_(), {},
	  &refs, true);
    }
  }
  void heartBeatExpired() {
    (void)sendPingProbe(PktNumSpace::AppData);
  }

  bool sendPMTUDProbe(ZiSockAddr addr) {
    return Base::sendPMTUDProbe(
      ZuMv(addr),
      [this](PktBuild &build, ZiSockAddr addr_, unsigned size) {
	typename Base::TxPktRefs refs;
	return sendShortPkt(build, ZuMv(addr_), {}, &refs, true, size);
      });
  }
  void connect_0() { // direct call from within rx thread
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    ZiIP ip = m_cliRx.remote ? m_cliRx.remote : ZiIP{m_cliRx.server};
    if (!ip || !m_cliRx.port) {
      app()->error_(ZeEXCEPT(Error, "Zquic",
	([server = LogMsg{m_cliRx.server}, port = m_cliRx.port](auto &s) {
	  s << '"' << server << "\": invalid QUIC UDP peer port=" << port;
	})));
      connectFailed_0(true);
      return;
    }

    closeCurrent(false, [link = impl(), ip]() mutable {
      link->app()->rxRun([link, ip]() mutable {
	link->connect_1(ip);
      });
    });
  }

  void connect_1(ZiIP ip) {
    resetRuntimeState([link = impl(), ip] { link->connect_2(ip); });
  }
  void connect_2(ZiIP ip) {
    Base::initCryptoDelivery();
    Base::resetDiagRx();

    if (!Endpoint::init(app()->mx(), app()->rxPktAllocator(), app()->txPktAllocator(),
	app(), app()->queueOrdinal())) {
      connectFailed_0(false);
      return;
    }
    if (!Endpoint::openUDP(
	PathMode::ClientConnected,
	ZiIP{}, 0, ip, m_cliRx.port))
      connectFailed_0(false);
  }

  void connectFailed(bool transient) {
    auto e = ZeEXCEPT(Error, "Zquic", ([transient](auto &s) {
      s << "QUIC UDP connect failed transient=" << transient;
    }));
    app()->error_(ZuMv(e));
  }

  ZuBSpan initialTxToken() const {
    return m_cliTx.initialToken;
  }

private:
  using InitialKeyDir = Base::InitialKeyDir;
  using CIDSel = Base::CIDSel;

  void closeCurrent(bool notify) {
    closeCurrent(notify, false, CloseFn{});
  }

  template <typename L>
  void closeCurrent(bool notify, L &&l) {
    closeCurrent(notify, false, ZuFwd<L>(l));
  }

  template <typename L>
  void closeCurrent(bool notify, bool peer, L &&l) {
    if (notify && Base::establishedRx() &&
	Endpoint::connected() && m_cliRx.remoteAddr) {
      closeAfterDisconnect(
	notify, peer, m_cliRx.remoteAddr, CloseFn{ZuFwd<L>(l)});
      return;
    }
    closeEndpoint(notify, peer, ZuFwd<L>(l));
  }

  void closeAfterDisconnect(
      bool notify, bool peer, ZiSockAddr addr, CloseFn fn) {
    Base::closeState();
    app()->txInvoke(impl(), [
      link = impl(),
      notify,
      peer,
      addr = ZuMv(addr),
      fn = ZuMv(fn)
    ]() mutable {
      (void)link->sendCloseFrame(ZuMv(addr), true);
      link->m_cliTx.closeNotify = notify;
      link->m_cliTx.closePeer = peer;
      link->m_cliTx.closeFn = ZuMv(fn);
      link->enterLocalClosingTx();
      return link;
    });
  }

  void closeExpired(bool drain = true) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client close expiry outside Tx thread", return);
    bool notify = m_cliTx.closeNotify;
    bool peer = m_cliTx.closePeer;
    CloseFn fn = ZuMv(m_cliTx.closeFn);
    m_cliTx.closeNotify = false;
    m_cliTx.closePeer = false;
	    if (drain) {
	      ZquicLOG(app()->qlogTrace(), ([
	      peer,
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
		CloseInitiator::T initiator = peer ?
		  CloseInitiator::T(CloseInitiator::Remote) :
	  CloseInitiator::T(CloseInitiator::Local);
		CloseEvt event{
		  .linkInfo = linkInfo,
		  .initiator = initiator,
		  .trigger = CloseTrigger::Aborted,
		  .reason = CloseReason::DrainExpired};
	o.logCxnClosed(event, time);
      }));
    }
    app()->rxRun([
      link = impl(), notify, peer, fn = ZuMv(fn)
    ]() mutable {
      link->closeEndpointDrained(notify, peer, ZuMv(fn));
    });
  }
  void idleExpired() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client idle expiry outside Tx thread", return);
    Base::cancelTimers_();
	    m_cliTx.closeNotify = true;
	    m_cliTx.closePeer = false;
	    m_cliTx.closeFn = CloseFn{[]() { }};
	    ZquicLOG(app()->qlogTrace(), ([linkInfo = Base::linkInfo_()](auto &o, ZuTime time) {
	      CloseEvt event{
		.linkInfo = linkInfo,
		.initiator = CloseInitiator::Local,
		.trigger = CloseTrigger::IdleTimeout,
		.reason = CloseReason::Idle,
		.connectionError = CloseError::NoError};
      o.logCxnClosed(event, time);
    }));
    closeExpired(false);
  }

  void closeEndpoint(bool notify) {
    closeEndpoint(notify, false, CloseFn{});
  }

  template <typename L>
  void closeEndpoint(bool notify, L &&l) {
    closeEndpoint(notify, false, ZuFwd<L>(l));
  }

  template <typename L>
  void closeEndpoint(bool notify, bool peer, L &&l) {
    app()->rxRun([
      link = impl(), notify, peer, l = ZuFwd<L>(l)
    ]() mutable {
      link->closeEndpointDrained(notify, peer, ZuMv(l));
    });
  }

  void closeEndpointDrained(bool notify) {
    closeEndpointDrained(notify, false, CloseFn{});
  }
  template <typename L>
  void closeEndpointDrained(bool notify, L &&l) {
    closeEndpointDrained(notify, false, ZuFwd<L>(l));
  }
  template <typename L>
  void closeEndpointDrained(bool notify, bool peer, L &&l) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC endpoint close drain outside Rx thread", return);
    m_cliShared.udpReady = 0;
    m_cliRx.notifyEndpointDown = false;
    if (Base::linkClosedRx_()) Base::clearCallbacks();
    Base::disconnect_(peer, [
      link = ZmRef(impl()), notify, peer, l = ZuFwd<L>(l)
    ]() mutable {
      auto link_ = link.ptr();
      link_->Endpoint::disconnect([
	link = ZuMv(link), notify, peer, l = ZuMv(l)
      ]() mutable {
	link->app()->rxRun([
	  link = ZuMv(link), notify, peer, l = ZuMv(l)
	]() mutable {
	  if (notify) link->disconnected(peer);
	  auto app = link->app();
	  app->txRun([link = ZuMv(link), notify, l = ZuMv(l)]() mutable {
	    if (notify) link->endpointDown();
	    link = nullptr;
	    l();
	  });
	});
      });
    });
  }

  void endpointDown() { }

  template <typename L>
  void resetRuntimeState(L l) {
    m_cliRx.localAddr = {};
    m_cliRx.remoteAddr = {};
    m_cliRx.peerParamsValidated = false;
    m_cliRx.handshakeConfirmed = false;
    m_cliRx.handshakeDonePending = false;
    m_cliRx.bootstrap = {};
    Base::resetLink([link = impl(), l = ZuMv(l)]() mutable {
      link->app()->txRun([link] {
	link->resetDiagTx();
	link->m_cliTx.initialToken = {};
	link->m_cliTx.preferred = false;
	link->m_cliTx.probeCommit = false;
	link->m_cliTx.probeOutput = false;
	link->m_cliTx.probeTarget = {};
	link->m_cliTx.probeRemote = {};
      });
      l();
    });
  }

  bool initRuntimeCrypto() {
    if (!m_cliRx.bootstrap.startRandom()) {
      Base::tlsFailure();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_cliRx.bootstrap.initialDCID(), m_cliRx.bootstrap.initialSCID(),
      m_cliRx.bootstrap.initialDCID(),
      m_cliRx.bootstrap.initialDCID(), m_cliRx.bootstrap.initialDCID());
    ZquicLOG(app()->qlogTrace(), ([
      local = m_cliRx.localAddr,
      remote = m_cliRx.remoteAddr,
      linkInfo = Zquic::LinkInfo{
	.vantage = Zquic::Vantage::Client,
	.origDCID = m_cliRx.bootstrap.initialDCID(),
	.groupID = m_cliRx.bootstrap.initialDCID(),
	.dcid = m_cliRx.bootstrap.initialDCID(),
	.scid = m_cliRx.bootstrap.initialSCID()
      }
    ](auto &o, ZuTime time) {
      o.logCxnStarted(
	CxnStartedEvt{.local = local, .remote = remote, .linkInfo = linkInfo},
	time);
    }));
    Base::configLocalParams(app());
    if (!Base::deriveInitial_()) return false;
    publishInitialToken();
    if (!initClientTLS()) return false;
    return true;
  }

  bool initClientTLS() {
    CryptoConfig config{
      .isServer = false,
      .enable0RTT = false,
      .alpn = app()->firstALPN(),
      .caPath = app()->caPath(),
      .keyLogPath = app()->keyLogPath(),
      .serverName = m_cliRx.server,
      .qlogTrace = &app()->qlogTrace(),
      .saveSessionTicketArg = impl(),
      .saveSessionTicket = [](void *arg, ZuBSpan ticket) {
	static_cast<Impl *>(arg)->saveEarlyData_(ticket);
      },
      .chachaOnly = app()->chachaOnly()
    };
    ZuBSpan sessionTicket;
    ZuBSpan appParams;
    const TransportParams *rememberedParams = nullptr;
    if (app()->earlyData(
	  impl(), sessionTicket, rememberedParams, appParams)) {
      if (sessionTicket && rememberedParams &&
	  app()->validateEarlyParams(impl(), appParams)) {
	Base::rememberZeroRTTParams(*rememberedParams);
	config.enable0RTT = true;
	config.sessionTicket = sessionTicket;
      } else
	Base::clearZeroRTTParams();
    }
    return Base::initTLS_(config);
  }

  void saveEarlyData_(ZuBSpan ticket) {
    if (!ticket) {
      app()->saveEarlyData(impl(), {}, {});
      return;
    }
    if (!Base::crypto_().peerParamsSet()) return;
    app()->saveEarlyData(
      impl(), ticket, Base::crypto_().peerParams());
  }

  bool startHandshake() {
    if (Base::handshakeStartedRx()) return true;
    Base::initClientPath_(m_cliRx.localAddr, m_cliRx.remoteAddr);
    if (!initRuntimeCrypto()) return false;
    if (!Base::startHandshake()) return false;
    if (!emitTLS_(0, {}, m_cliRx.remoteAddr)) return false;
    if (Base::crypto_().earlyDataEnabled())
      app()->earlyDataReady(impl());
    return true;
  }

  void restartAfterRetry_0() {
    Base::resetLink([link = impl()] {
      if (!link->restartAfterRetry_1()) link->tlsFailure();
    }, true);
  }
  bool restartAfterRetry_1() {
    m_cliRx.peerParamsValidated = false;
    Base::initClientPath_(m_cliRx.localAddr, m_cliRx.remoteAddr);
    Base::setRuntimeCIDs_(
      m_cliRx.bootstrap.retrySCID(), m_cliRx.bootstrap.initialSCID(),
      m_cliRx.bootstrap.retrySCID(),
      m_cliRx.bootstrap.initialDCID(), m_cliRx.bootstrap.initialDCID());
    Base::configLocalParams(app());
    if (!Base::deriveInitial_()) return false;
    publishInitialToken();
    if (!initClientTLS()) return false;
    if (!Base::startHandshake()) return false;
    return emitTLS_(0, {}, m_cliRx.remoteAddr);
  }

  void markEstablished_() {
    if (!Base::readyToEstablish())
      return;
    if (!m_cliRx.peerParamsValidated) {
      if (!Base::validateServerParams_(m_cliRx.bootstrap)) {
	Base::tlsFailure();
	app()->error_(ZeEXCEPT(Error, "Zquic",
	  "server QUIC transport parameters failed validation"));
	connectFailed_0(false);
	return;
      }
      m_cliRx.peerParamsValidated = true;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    Base::scheduleMigrationCIDs();
    if (m_cliRx.handshakeDonePending) {
      m_cliRx.handshakeDonePending = false;
      Base::discardPktNumSpace_0(PktNumSpace::Handshake);
      preferredAddress();
    }
    impl()->connected(Connected{
      .alpn = Base::negotiatedProtocol_(),
      .version = Version1
    });
  }

  bool emitTLS_(size_t inEpoch, ZuBSpan input, ZiSockAddr addr) {
    bool ok = Base::advanceTLS(
      inEpoch, input, ZuMv(addr),
      [this](
	  ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
	  ZiSockAddr addr_) {
	return Base::sendCryptoFlights_(ZuMv(data), offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [](ZiSockAddr) { return true; });
    if (!ok && input && Base::crypto_().tlsReady()) connectFailed_0(false);
    return ok;
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Base::activePathRemote_())
      return false;
    return flushTx_(Base::activePathRemote_());
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::deferTxFlush()) return false;
    Base::drainPeerAcksTx(false);
    if (!addr) return flushTx_();
    if (Base::linkClosedTx_()) return false;
    if (!Base::establishedTx()) {
      bool sent = flushPendingAcks(addr);
      if (!Base::canTxZeroRTT_()) return sent;
      return Base::flushEarlyStreams(
	addr,
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const Base::TxPktRefs &refs) {
	  return sendZeroRTTPkt(
	    build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	}) || sent;
    }
    uint64_t runtime = Base::txRuntime();
    bool sent = Base::flushCtlStreamsBatch(
	addr,
	[this](PktBuild &build) {
	  return appendPendingAck(PktNumSpace::AppData, build);
	},
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const Base::TxPktRefs &refs) {
	  return sendShortPkt(
	    build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	});
    if (runtime != Base::txRuntime()) return sent;
    sent |= sendPMTUDProbe(ZuMv(addr));
    if (runtime != Base::txRuntime()) return sent;
    if (!sent) sent = flushPendingAcks(ZuMv(addr));
    return sent;
  }

  using Base::queueTxFlush;

  bool sendPassiveMigChal() { return false; }

  bool sendPathResponse(
    ZuBSpan data, ZiSockAddr addr, ZiSockAddr local, unsigned rxBytes) {
    if (data.length() != PathChallenge::Length) return false;
    PathData payload{data};
    app()->txInvoke([
      link = impl(),
      payload,
      addr = ZuMv(addr),
      local = ZuMv(local),
      rxBytes
    ]() mutable {
      if (link->disconnecting_()) return;
      ZiSockAddr remote = addr;
      ZiSockAddr responseAddr = addr;
      link->observePathRxTx(
	ZuMv(local), ZuMv(remote), rxBytes, true, false);
      link->sendPathResponseTx(
	payload, ZuMv(responseAddr));
    });
    return true;
  }
  void sendObservedResponse(ZuBSpan data, ZiSockAddr addr, ZiSockAddr, unsigned) {
    (void)sendPathResponseTx(data, ZuMv(addr));
  }
  bool sendPathResponseTx(ZuBSpan data, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client PATH_RESPONSE outside Tx thread", return false);
    return Base::sendPathResponsePkt(
      data, ZuMv(addr),
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const Base::TxPktRefs *refs) {
	return sendStrictShortPkt(build, ZuMv(addr_), refs);
      });
  }

  using Base::appendPendingAck;
  using Base::buildPayload_;
  using Base::sendCryptoPkt_;
  using Base::flushStreamWritable;

  bool migrate_(const MigrationParams &params) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration outside Tx thread", return false);
    MigrationReason::T reason = MigrationReason::None;
    if (!Base::prepareMig(params, reason))
      return false;
    return true;
  }

  void preferredAddress() {
    if (m_cliRx.handshakeConfirmed) return;
    m_cliRx.handshakeConfirmed = true;
    impl()->handshakeConfirmed();
    if (app()->migrationMode() == MigrationMode::Disabled) return;
    auto remote = Base::crypto_().peerParams().preferredAddress.select(
      m_cliRx.remoteAddr.ip());
    if (!remote || remote == m_cliRx.remoteAddr) return;
    MigrationParams params;
    params.local.init(m_cliRx.localAddr.ip(), 0);
    params.remote = remote;
    params.rebindLocal = true;
    app()->txRun([link = impl(), params] {
      if (link->disconnecting_() || link->migrationActive_()) return;
      link->m_cliTx.preferred = true;
      MigrationReason::T reason;
      if (!link->prepareMig(params, reason, true)) link->m_cliTx.preferred = false;
    });
  }
  void handshakeConfirmed() { }

  template <typename L> void commitMigEndpoint(L l) {
    if (!m_cliTx.preferred) { l(true); return; }
    m_cliTx.probeCommit = true;
    Base::cancelPktWait([this, l = ZuMv(l)]() mutable {
      Endpoint::finishProbe(true, [link = impl(), l = ZuMv(l)](bool ok) mutable {
	link->app()->txRun([link, ok, l = ZuMv(l)]() mutable {
	  link->m_cliTx.probeOutput = false;
	  link->m_cliTx.probeTarget = {};
	  link->m_cliTx.probeRemote = {};
	  l(ok);
	});
      });
    });
  }
  void pathPromoted() {
    m_cliTx.preferred = false;
    m_cliTx.probeCommit = false;
  }
  void pathPublishedRx(ZiSockAddr local, ZiSockAddr remote) {
    m_cliRx.localAddr = ZuMv(local);
    m_cliRx.remoteAddr = ZuMv(remote);
  }
  void discardMigEndpoint() {
    if (!m_cliTx.preferred) return;
    m_cliTx.preferred = false;
    m_cliTx.probeCommit = false;
    m_cliTx.probeOutput = false;
    m_cliTx.probeTarget = {};
    m_cliTx.probeRemote = {};
    Base::cancelPktWait([this] { Endpoint::finishProbe(false); });
  }

protected:
  void migrationCIDReady() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration CID completion outside Tx thread", return);
    if (Base::migLocalRebind()) {
      ZiSockAddr local = Base::migLocal();
      ZiSockAddr remote = Base::migRemote();
      Base::migRebindStart();
      Base::cancelPktWait([this, local, remote, runtime = Base::txRuntime()] {
	if (runtime != Base::txRuntime() || Base::disconnecting_()) return;
	bool posted = Endpoint::rebindUDP(
	  local.ip(), local.port(), remote.ip(), remote.port(),
	  [link = impl(), runtime](
	      bool ok, ZiSockAddr rebound, bool closed) mutable {
	    auto target = link->Endpoint::probeTarget();
	    link->app()->txRun([
	      link, ok, rebound = ZuMv(rebound), closed, target, runtime
	    ]() mutable {
	      if (runtime != link->txRuntime() ||
		  !link->migrationActive_() || (link->disconnecting_() && !closed)) return;
	      if (!ok) {
		MigrationReason::T reason = closed ?
		  MigrationReason::Closed : MigrationReason::Endpoint;
		link->migRebindFail(reason);
		link->failMig(reason);
		return;
	      }
	      if (link->m_cliTx.preferred) {
		link->m_cliTx.probeTarget = target;
		link->m_cliTx.probeRemote = link->migRemote();
	      }
	      link->updateMigLocal(ZuMv(rebound));
	      link->migRebindOK();
	      if (!link->sendMigChal())
		link->failMig(MigrationReason::Validation);
	    });
	  }, m_cliTx.preferred);
	if (!posted) {
	  Base::migRebindFail(MigrationReason::Endpoint);
	  Base::failMig(MigrationReason::Endpoint);
	  return;
	}
      });
      return;
    }
    if (sendMigChal())
      return;
    Base::failMig(MigrationReason::Validation);
  }

private:
  bool sendMigChal(bool retry = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration challenge outside Tx thread", return false);
    if (!retry && !Base::activateMig())
      return false;
    PktBuild build;
    typename Base::TxPktRefs refs;
    ControlFrame frame;
    if (!Base::buildMigChal(build, refs, frame))
      return false;
    uint64_t runtime = Base::txRuntime();
    bool sent = sendShortPkt(
      Base::migPeerCID(), build,
      Base::migRemote(), {}, &refs, true);
    if (runtime != Base::txRuntime()) return sent;
    if (sent)
      Base::migChalSent(frame);
    return sent || Base::pktAllocationPending();
  }

  void publishInitialToken() {
    TokenBytes token{m_cliRx.bootstrap.retried() ?
      m_cliRx.bootstrap.retryToken() : ZuBSpan{m_cliRx.newToken}};
    app()->txRun([link = impl(), token = ZuMv(token)]() mutable {
      if (!link->disconnecting_()) link->m_cliTx.initialToken = ZuMv(token);
    });
  }

	  void newToken(ZuBSpan token) {
	    if (!token || token.length() > AddressToken::MaxLength) return;
	    ZquicLOG(app()->qlogTrace(), ([
	      tokenLength = token.length(),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvt event{
		.linkInfo = linkInfo,
		.value = tokenLength,
		.kind = SecKind::Token,
		.reason = SecReason::NewToken,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvt(EvtName::TokenIssued, event, time);
    }));
    m_cliRx.newToken = token;
    Base::newTokenRx();
  }

  bool sendPathBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path send outside Tx thread", return false);
    return Base::template submitPathPkt<Base::PathTx::Direct>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return sendEndpointPkt(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path probe send outside Tx thread", return false);
    return Base::template submitPathPkt<Base::PathTx::Probe>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return sendEndpointPkt(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendInitialBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Initial buffer send outside Tx thread", return false);
    return Base::holdInitialForCoalesce(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendHandshakeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Handshake buffer send outside Tx thread", return false);
    return Base::sendHandshakeCoalesced(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn,
    unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Short buffer send outside Tx thread", return false);
    if (pmtudSize) return sendPathProbeBuf(ZuMv(buf), ZuMv(addr));
    return Base::template submitPathPkt<Base::PathTx::Direct>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_) {
	if (!app()->sendPkt(buf_)) return true;
	return sendEndpointPkt(ZuMv(buf_), ZuMv(addr_), ecn_);
      },
      ecn);
  }
  bool sendStrictShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client strict Short buffer send outside Tx thread", return false);
    return Base::template submitPathPkt<Base::PathTx::Validation>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = sendEndpointPkt(ZuMv(buf_), ZuMv(addr_), ecn_, true);
	return sent;
      },
      ecn);
  }

  bool sendInitialPkt(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    bool coalesce = true) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    if (ackEliciting) recordRefs = &refs;
    return Base::sendProtInitialPkt(
      CIDSel::Initial, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), initialTxToken(), true,
      payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return Base::allocPktTx(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf(ZuMv(buf), ZuMv(addr_));
	return sendInitialBuf(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakePkt(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Handshake, payload, frame)) return false;
    return sendHandshakePkt(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    bool coalesce = true) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    if (ackEliciting) recordRefs = &refs;
    return Base::template sendProtLongPkt<Zquic::PktType::Handshake>(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Base::allocPktTx(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf(ZuMv(buf), ZuMv(addr_));
	return sendHandshakeBuf(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendZeroRTTPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client 0-RTT send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendZeroRTTPkt(build, ZuMv(addr), payload);
  }

  bool sendZeroRTTPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return sendZeroRTTPkt(
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting);
  }

  bool sendZeroRTTPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const Base::TxPktRefs *recordRefs, bool ackEliciting) {
    if (!Endpoint::connected()) return false;
    return Base::template sendProtLongPkt<Zquic::PktType::ZeroRTT>(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Base::allocPktTx(); },
      [this](auto buf, ZiSockAddr addr_) {
	return sendShortBuf(ZuMv(buf), ZuMv(addr_), EcnMark::NotECT);
      });
  }

  bool sendShortPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendShortPkt(build, ZuMv(addr), payload);
  }

  bool sendShortPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    unsigned pmtudSize = 0) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return sendShortPkt(payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting, pmtudSize);
  }

  bool sendShortPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const Base::TxPktRefs *recordRefs, bool ackEliciting,
    unsigned pmtudSize = 0) {
    if (!Endpoint::connected()) return false;
    m_cliTx.probeOutput = m_cliTx.preferred && addr == m_cliTx.probeRemote;
    if (m_cliTx.probeOutput)
      return sendShortPkt(Base::migPeerCID(), payload, ZuMv(addr),
	recordFrame, recordRefs, ackEliciting, pmtudSize);
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Base::allocPktTx(); },
      [this, pmtudSize](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf(
	  ZuMv(buf), ZuMv(addr_), ecn, pmtudSize);
      },
      pmtudSize);
  }

  bool sendShortPkt(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    ZuBSpan recordFrame, const Base::TxPktRefs *recordRefs,
    bool ackEliciting, unsigned pmtudSize = 0) {
    if (!Endpoint::connected()) return false;
    m_cliTx.probeOutput = m_cliTx.preferred && addr == m_cliTx.probeRemote;
    return Base::sendProtShortPkt(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Base::allocPktTx(); },
      [this, pmtudSize](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf(
	  ZuMv(buf), ZuMv(addr_), ecn, pmtudSize);
      },
      pmtudSize);
  }
  bool sendStrictShortPkt(
    PktBuild &payload, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs) {
    if (!Endpoint::connected()) return false;
    m_cliTx.probeOutput = false;
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return Base::allocPktTx(); },
      [this](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(ZuMv(buf), ZuMv(addr_), ecn);
      });
  }

  bool sendCloseFrame(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client close frame outside Tx thread", return false);
    if ((!closing && Base::linkClosedTx_()) ||
	(!Base::establishedTx() &&
	  !(closing && Base::closingTx_())) ||
	!Endpoint::connected() || !addr)
      return false;
    PktBuild build;
    bool appClose = Base::appCloseOnDisconnect();
    if (!Base::writeCloseFrame(build, appClose))
      return false;
    uint64_t runtime = Base::txRuntime();
    bool sent = sendShortPkt(build, ZuMv(addr), {});
    if (runtime != Base::txRuntime()) return sent;
    if (sent) {
      ZquicLOG(app()->qlogTrace(), ([
	      appClose,
	      errorCode = Base::closeErrorTx_(),
	      closeError = Base::closeErrorTx_() ?
		CloseError::T(CloseError::Unknown) :
		CloseError::T(CloseError::NoError),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
		CloseTrigger::T trigger = appClose ?
		  CloseTrigger::T(CloseTrigger::Application) :
	  CloseTrigger::T(CloseTrigger::Error);
	CloseEvt event{
		.linkInfo = linkInfo,
		.errorCode = errorCode,
	  .initiator = CloseInitiator::Local,
	  .trigger = trigger,
	  .reason = CloseReason::LocalClose,
	  .connectionError = appClose ?
		  CloseError::T(CloseError::None) : closeError,
		.applicationError = appClose ?
		  CloseError::T(CloseError::Unknown) :
		  CloseError::T(CloseError::None)};
	o.logCxnClosed(event, time);
      }));
    }
    return sent;
  }

  bool flushPendingAck(
    PktNumSpace::T level, ZiSockAddr addr, bool &sent) {
    PktBuild build;
    build.reset();
    if (!Base::appendPendingAck(level, build, true)) return false;
    if (!build.bytes()) return true;
    bool ok;
    if (level == PktNumSpace::Initial) {
      ok = sendInitialPkt(build, ZuMv(addr), {}, nullptr, false, false);
      if (ok) sent = true;
      return ok;
    }
    if (!Base::txSecretInstalled_(level)) return true;
    if (level == PktNumSpace::Handshake)
      ok = sendHandshakePkt(build, ZuMv(addr), {});
    else
      ok = sendShortPkt(build, ZuMv(addr), {});
    if (ok) sent = true;
    return ok;
  }

  bool flushPendingAcks(ZiSockAddr addr) {
    bool sent = false;
    if (Base::pendingAck(PktNumSpace::Initial))
      (void)flushPendingAck(PktNumSpace::Initial, addr, sent);
    if (Base::pendingAck(PktNumSpace::Handshake))
      (void)flushPendingAck(PktNumSpace::Handshake, addr, sent);
    if (Base::pendingAck(PktNumSpace::AppData))
      (void)flushPendingAck(PktNumSpace::AppData, ZuMv(addr), sent);
    if (sent)
      (void)Base::flushCoalescedInitial(
	[this](auto buf, ZiSockAddr addr_) {
	  return sendPathBuf(ZuMv(buf), ZuMv(addr_));
	});
    return sent;
  }

  void received_(ZmRef<RxPkt> pkt) {
    if (handleRetry(pkt)) return;
    Base::rxPacket(
      ZuMv(pkt),
      [this](const ZmRef<RxPkt> &pkt_, unsigned packetOffset,
	  unsigned packetLen, const LongHdr &h) {
	return receivedLong(pkt_, packetOffset, packetLen, h);
      },
      [this](const ZmRef<RxPkt> &pkt_, unsigned packetOffset, unsigned packetLen,
	  bool *pathAccounted) {
	return receivedShort(pkt_, packetOffset, packetLen, pathAccounted);
      });
  }

  bool handleRetry(const ZmRef<RxPkt> &pkt) {
    if (!pkt || !pkt->length || Base::establishedRx())
      return false;
    auto packet = pkt->cspan();
    RetryPkt retry;
    if (Pkt::parseRetry(packet, retry) < 0) return false;
	    if (!m_cliRx.bootstrap.onRetry(retry)) {
	      ZquicLOG(app()->qlogTrace(), ([
		tokenLength = retry.token.length(),
		linkInfo = Base::linkInfo_()
	      ](auto &o, ZuTime time) {
		SecEvt event{
		  .linkInfo = linkInfo,
		  .value = tokenLength,
		  .kind = SecKind::Retry,
		  .reason = SecReason::Validation,
		  .success = false};
	event.trigger = SecTrigger::Received;
	o.logSecEvt(EvtName::RetryValid, event, time);
      }));
      Base::packetParseFailure();
      return true;
    }
	    ZquicLOG(app()->qlogTrace(), ([
	      tokenLength = retry.token.length(),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvt event{
		.linkInfo = linkInfo,
		.value = tokenLength,
		.kind = SecKind::Retry,
		.reason = SecReason::OK,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvt(EvtName::RetryValid, event, time);
      event.kind = SecKind::Token;
      o.logSecEvt(EvtName::TokenValid, event, time);
    }));
    restartAfterRetry_0();
    return true;
  }

  bool receivedLong(
    const ZmRef<RxPkt> &pkt, unsigned packetOffset, unsigned packetLen, const LongHdr &h) {
    return Base::receiveProtLongPkt(
      InitialKeyDir::Server, pkt, packetOffset, packetLen, h,
      [this](const LongHdr &h, const ZmRef<RxPkt> &) {
	Base::setPeerCIDFromHdrSCID(h);
	return true;
      },
      [this, local = pkt->local](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<RxPkt> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog,
	  bool earlyData) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, earlyData, local);
      });
  }

  bool receivedShort(const ZmRef<RxPkt> &pkt, unsigned packetOffset, unsigned packetLen,
      bool *pathAccounted = nullptr) {
    return Base::rxProtShortPkt(
      pkt, packetOffset, packetLen,
      [this, local = pkt->local](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<RxPkt> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, false, local);
      }, nullptr, pathAccounted);
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<RxPkt> &packetBuf, Base::RxAckMeta &ack,
    ZquicLog_::PktEvt *qlog, bool earlyData, ZiSockAddr local) {
    ZiSockAddr peer = addr;
    if (packetBuf) local = packetBuf->local;
    if (!local) return false;
    unsigned pathBytes = packetBuf ? packetBuf->length : 0;
    bool ok = Base::consumeProtFrames(
      level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
      [this](size_t epoch, ZuBSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this, local, pathBytes](
	  PktNumSpace::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame(
	  level_, frame, ZuMv(addr_), local, pathBytes);
      },
      earlyData);
    if (ok && !packetBuf && level == PktNumSpace::AppData && Base::establishedRx())
      Base::observePathRx_(ZuMv(local), ZuMv(peer), 0);
    return ok;
  }

  bool handleControlFrame(
    PktNumSpace::T level, const Frame &frame, ZiSockAddr addr,
    ZiSockAddr local, unsigned pathBytes = 0) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	queueTxFlush();
	return true;
      case FrameType::PathChallenge: {
	sendPathResponse(
	  frame.payload, ZuMv(addr), ZuMv(local),
	  level == PktNumSpace::AppData ? pathBytes : 0);
	return true;
      }
      case FrameType::PathResponse:
	Base::rxPathResponse(frame.payload);
	return true;
      case FrameType::HandshakeDone:
	if (Base::establishedRx()) {
	  Base::discardPktNumSpace_0(PktNumSpace::Handshake);
	  preferredAddress();
	} else m_cliRx.handshakeDonePending = true;
	return true;
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	Base::transportClose_(frame.type, frame.errorCode);
	  ZquicLOG(app()->qlogTrace(), ([
	    type = frame.type,
	    errorCode = frame.errorCode,
	    closeError = frame.errorCode ?
	      CloseError::T(CloseError::Unknown) :
	      CloseError::T(CloseError::NoError),
	    linkInfo = Base::linkInfo_()
	](auto &o, ZuTime time) {
	  bool appClose = type == FrameType::ApplicationClose;
	  CloseEvt event{
	    .linkInfo = linkInfo,
	    .errorCode = errorCode,
	    .initiator = CloseInitiator::Remote,
	    .trigger = CloseTrigger::Error,
	    .reason = CloseReason::PeerCloseFrame,
	    .connectionError = appClose ?
	      CloseError::T(CloseError::None) : closeError,
	    .applicationError = appClose ?
	      CloseError::T(CloseError::Unknown) :
	      CloseError::T(CloseError::None)};
	  o.logCxnClosed(event, time);
	}));
	app()->txRun([link = impl()]() mutable {
	  if (link->disconnecting_()) return;
	  link->m_cliTx.closeNotify = true;
	  link->m_cliTx.closePeer = true;
	  link->m_cliTx.closeFn = CloseFn{[]() { }};
	});
	Base::enterPeerDraining_0(frame.errorCode);
	return true;
      default:
	return true;
    }
  }

  void dataBlocked(uint64_t maximum) {
    Base::queueBlocked(FrameType::DataBlocked, 0, maximum);
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zquic::StreamType::Duplex, maximum);
    queueTxFlush();
  }
  void streamDataBlocked(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked(FrameType::StreamDataBlocked, streamID, maximum);
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zquic::StreamType::Duplex, maximum);
    queueTxFlush();
  }
  void streamsBlocked(Zquic::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush();
  }

public:
  void endpointPacket(ZmRef<RxPkt> pkt) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [link = impl(), pkt = ZuMv(pkt)]() mutable {
      if (!link->disconnecting_()) link->received_(ZuMv(pkt));
      return link;
    });
  }
  void endpointReady(Endpoint *ep) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [
      link = impl(), ep, local = ep->local(), remote = ep->remote()
    ]() mutable {
      if (!link->disconnecting_())
	link->endpointReadyRx(ep, ZuMv(local), ZuMv(remote));
      return link;
    });
  }

  void endpointFailed(bool transient) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [link = impl(), transient]() mutable {
      if (!link->disconnecting_()) link->connectFailed_0(transient);
      return link;
    });
  }
  void endpointDown_(Endpoint *ep) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [link = impl(), ep]() mutable {
      if (!link->disconnecting_()) link->endpointDownRx(ep);
      return link;
    });
  }
private:
  void endpointReadyRx(Endpoint *ep, ZiSockAddr local, ZiSockAddr remote) {
    if (ep != this) return;
    pathPublishedRx(ZuMv(local), ZuMv(remote));
    m_cliRx.notifyEndpointDown = true;
    ++m_cliShared.udpReadyCount;
    Base::endpointReady();
    if (!startHandshake()) {
      connectFailed_0(false);
      return;
    }
    m_cliShared.udpReady = 1;
  }
  void endpointDownRx(Endpoint *ep) {
    if (ep != this) return;
    closeEndpointDrained(m_cliRx.notifyEndpointDown, true, CloseFn{});
  }
  void resetDisconnected() {
    closeEndpointDrained(true, true, CloseFn{});
  }

  void connectFailed_0(bool transient) {
    Base::endpointFailure();
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect failure before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), transient]() {
      if (!link->disconnecting_()) link->connectFailed(transient);
      return link;
    });
  }

  // shared
  Zquic_::CliSharedState m_cliShared;

  // Rx thread exclusive
  Zquic_::CliRxStorage m_cliRx;

  // Tx thread exclusive
  Zquic_::CliTxStorage m_cliTx;

};

} // namespace Zquic
