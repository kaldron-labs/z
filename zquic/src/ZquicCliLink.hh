//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC client link API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic {

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>>
class CliLink :
  public Link<App, Impl, TxBufAlloc_, Stream_>,
  public Endpoint_<CliLink<App, Impl, Stream_, TxBufAlloc_>> {
public:
  using Base = Link<App, Impl, TxBufAlloc_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  using Endpoint = Endpoint_<CliLink>;
  using CloseFn = ZmFn<void(), ZmFnHeapID<"ZquicCliLink.CloseFn">>;
  static constexpr bool EndpointRef = true;
  static constexpr unsigned TLSBufSize = (64<<10); // 64K
  static constexpr unsigned PNLength = 2;
  static constexpr unsigned CryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  friend Endpoint;
  template <typename, typename, typename> friend class Zquic::Stream;

  CliLink(App *app) : Base{app, false} { Base::initCryptoDelivery(); }
  CliLink(App *app, ZuID id) : Base{app, false, ZuMv(id)} {
    Base::initCryptoDelivery();
  }
  CliLink(App *app, Host server, uint16_t port) :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port} {
    Base::initCryptoDelivery();
  }
  CliLink(App *app, ZuID id, Host server, uint16_t port) :
    Base{app, false, ZuMv(id)},
    m_server{ZuMv(server)}, m_port{port} {
    Base::initCryptoDelivery();
  }
  ~CliLink() = default;

  void connect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->connect_0();
      return link;
    });
  }
  void connect(Host server, uint16_t port) {
    m_server = ZuMv(server);
    m_port = port;
    m_remote = ZiIP{};
    connect();
  }
  void connect(Host server, uint16_t port, ZiIP remote) {
    m_server = ZuMv(server);
    m_port = port;
    m_remote = ZuMv(remote);
    connect();
  }

protected:
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
      link->disconnectRx_(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx_(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC client disconnect outside Rx thread", return);
    if (!Base::linkClosedRx_()) Base::closeState(errorCode, true);
    if (Base::establishedRx() && Endpoint::connected() &&
	Endpoint::remote()) {
      app()->txRun([
	link = impl(),
	addr = Endpoint::remote()
      ]() mutable {
	if (link->disconnecting_()) return;
	(void)link->sendCloseFrame(ZuMv(addr), true);
	link->m_closeNotify = false;
	link->m_closePeer = false;
	link->enterLocalClosingTx();
      });
      return;
    }
    closeEndpointDrained_(false);
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
    Base::resetTLS_();
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
    closeEndpointDrained_(true, ZuFwd<L>(l));
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }
  bool udpReady() const { return m_udpReady; }
  uint64_t udpReadyCount() const { return m_udpReadyCount; }
  bool ready() const { return m_udpReady; }
  bool established() const { return Base::established(); }
  bool cxn() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const { return Endpoint::local(); }
  const ZiSockAddr &remote() const { return Endpoint::remote(); }
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
  template <typename L>
  void diag(L &&l) const { Base::diag(ZuFwd<L>(l)); }
  template <typename L>
  void pathDiag(L &&l) const { Base::pathDiag(ZuFwd<L>(l)); }
  template <typename L>
  void pathInfo(L &&l) const {
    if (app()->txInvoked()) {
      l(Base::activePathInfo_(), Base::candidatePathInfo_());
      return;
    }
    auto link = const_cast<CliLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->activePathInfo_(), link->candidatePathInfo_());
    });
  }
  template <typename L>
  void migrationState(L &&l) const {
    if (app()->txInvoked()) {
      l(Base::migrationResult_());
      return;
    }
    auto link = const_cast<CliLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->migrationResult_());
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
    params.remote = Endpoint::remote();
    params.rebindLocal = true;
    params.reason = MigrationReason::Active;
    params.closeOnFailure = app()->migCloseOnFail();
    return migrate(params);
  }
  bool migrateRemote(ZiSockAddr remote) {
    MigrationParams params;
    params.local = Endpoint::local();
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

  bool send(StreamRef stream, ZuBSpan payload, bool fin = true) {
    if (Base::disconnecting_()) return false;
    if (!stream || (!payload && !fin))
      return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), payload, fin);
    auto tx = stream->txStream();
    tx << payload;
    if (!tx.flush()) return false;
    app()->txInvoke([link = this->impl(), stream = ZuMv(stream), fin]() mutable {
      if (link->disconnecting_()) return;
      if (fin) stream->fin();
      link->queueTxFlush();
    });
    return true;
  }
  bool send(StreamRef stream, ZmRef<ZiIOBuf> payload, bool fin = true) {
    if (Base::disconnecting_() || !stream || (!payload && !fin)) return false;
    if (app()->txInvoked()) return send_(ZuMv(stream), ZuMv(payload), fin);
    app()->txInvoke([link = this->impl(), stream = ZuMv(stream),
	payload = ZuMv(payload), fin]() mutable {
      if (!link->disconnecting_()) link->send_(ZuMv(stream), ZuMv(payload), fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZmRef<ZiIOBuf> payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC retained send outside Tx thread", return false);
    if (Base::linkClosedTx_() || !stream || (!payload && !fin)) return false;
    if (payload && payload->length && !stream->send_(ZuMv(payload))) return false;
    if (fin) stream->fin();
    queueTxFlush();
    return true;
  }
  bool send_(StreamRef stream, ZuBSpan payload, bool fin = true) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client send_ outside Tx thread", return false);
    if (Base::linkClosedTx_() || !stream || (!payload && !fin))
      return false;
    if (payload) {
      auto tx = stream->txStream_();
      tx << payload;
      if (!tx.flush()) return false;
    }
    if (fin) stream->fin();
    queueTxFlush();
    return true;
  }

  void pto_() { // direct call from within tx thread
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
	    build, Endpoint::remote(), {}, &cryptoRef, true, false) :
	  sendHandshakePkt(
	    build, Endpoint::remote(), {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->retransmit_();
    });
  }

  bool retransmit_(bool pto = false) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Endpoint::connected())
      return false;
    SentFrameRef ref;
    bool sent = false;
    PktNumSpace::T level;
    constexpr unsigned MaxBatch = 16;
    unsigned processed = 0;
    while (processed < MaxBatch && Base::nextRetransmit_(level, ref)) {
      ++processed;
      PktBuild build;
      if (ref.kind == SentFrameKind::Crypto) {
	if (!Base::buildRetxCrypto(level, build, ref)) continue;
	bool ok =
	  level == PktNumSpace::Initial ?
	    sendInitialPkt(build, Endpoint::remote(), {}, &ref, true, false) :
	  level == PktNumSpace::Handshake ?
	    sendHandshakePkt(build, Endpoint::remote(), {}, &ref, true, false) :
	    sendShortPkt(build, Endpoint::remote(), {}, &ref, true);
	if (!ok) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
      }
      if (level != PktNumSpace::AppData || !Base::establishedTx())
	continue;
      if (!pto && !Base::congestionAllowance_()) {
	Base::requeueRetransmit_(level, ref);
	break;
      }
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetxStream(build, ref)) {
	  if (Base::debugLog())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit build failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	  (void)Base::requeueStreamRef(level, ref);
	  break;
	}
      } else if (!Base::buildRetxCtl(build, ref))
	continue;
      if (!sendShortPkt(build, Endpoint::remote(), {}, &ref, true)) {
	if (ref.kind == SentFrameKind::Stream)
	  if (Base::debugLog())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit send failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	if (ref.kind == SentFrameKind::Stream)
	  (void)Base::requeueStreamRef(level, ref);
	else
	  Base::requeueRetransmit_(level, ref);
	break;
      }
      sent = true;
    }
    if (sent) Base::schedulePTO_();
    if (sent && processed == MaxBatch) queueRetransmit();
    return sent;
  }

  bool sendPingProbe(PktNumSpace::T level) {
    PktBuild build;
    if (!Base::buildPingProbe(level, build)) return false;
    typename Base::TxPktRefs refs;
    switch (level) {
      case PktNumSpace::Initial:
	return sendInitialPkt(
	  build, Endpoint::remote(), {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt(
	  build, Endpoint::remote(), {}, nullptr, true, false);
      default:
	return sendShortPkt(
	  build, Endpoint::remote(), {},
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
    ZiIP ip = m_remote ? m_remote : ZiIP{m_server};
    if (!ip || !m_port) {
      app()->error_(ZeEXCEPT(Error, "Zquic",
	([server = LogMsg{m_server}, port = m_port](auto &s) {
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
    resetRuntimeState();
    Base::resetDiag();

    if (!Endpoint::init(app()->mx(), app()->rxPktAllocator(), app()->txPktAllocator())) {
      connectFailed_0(false);
      return;
    }
    if (!Endpoint::openUDP(
	PathMode::ClientConnected,
	ZiIP{}, 0, ip, m_port))
      connectFailed_0(false);
  }

  void connectFailed(bool transient) {
    auto e = ZeEXCEPT(Error, "Zquic", ([transient](auto &s) {
      s << "QUIC UDP connect failed transient=" << transient;
    }));
    app()->error_(ZuMv(e));
  }

  ZuBSpan initialTxToken() const {
    return initialToken_();
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
	Endpoint::connected() && Endpoint::remote()) {
      closeAfterDisconnect(
	notify, peer, Endpoint::remote(), CloseFn{ZuFwd<L>(l)});
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
      link->m_closeNotify = notify;
      link->m_closePeer = peer;
      link->m_closeFn = ZuMv(fn);
      link->enterLocalClosingTx();
      return link;
    });
  }

  void closeExpired(bool drain = true) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client close expiry outside Tx thread", return);
    bool notify = m_closeNotify;
    bool peer = m_closePeer;
    CloseFn fn = ZuMv(m_closeFn);
    m_closeNotify = false;
    m_closePeer = false;
	    if (drain) {
	      ZquicLOG(app()->qlogTrace(), ([
	      peer,
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
		CloseInitiator::T initiator = peer ?
		  CloseInitiator::T(CloseInitiator::Remote) :
	  CloseInitiator::T(CloseInitiator::Local);
		CloseEvt event{
		  .linkInfo = linkInfo
		,
		  .initiator = initiator,
		  .trigger = CloseTrigger::Aborted,
		  .reason = CloseReason::DrainExpired};
	o.logCxnClosed(event, time);
      }));
    }
    app()->rxRun([
      link = impl(), notify, peer, fn = ZuMv(fn)
    ]() mutable {
      link->closeEndpointDrained_(notify, peer, ZuMv(fn));
    });
  }
  void idleExpired() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client idle expiry outside Tx thread", return);
    Base::cancelTimers_();
	    m_closeNotify = true;
	    m_closePeer = false;
	    m_closeFn = CloseFn{[]() { }};
	    ZquicLOG(app()->qlogTrace(), ([linkInfo = Base::linkInfo_()](auto &o, ZuTime time) {
	      CloseEvt event{
		.linkInfo = linkInfo
	      ,
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
      link->closeEndpointDrained_(notify, peer, ZuMv(l));
    });
  }

  void closeEndpointDrained_(bool notify) {
    closeEndpointDrained_(notify, false, CloseFn{});
  }
  template <typename L>
  void closeEndpointDrained_(bool notify, L &&l) {
    closeEndpointDrained_(notify, false, ZuFwd<L>(l));
  }
  template <typename L>
  void closeEndpointDrained_(bool notify, bool peer, L &&l) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC endpoint close drain outside Rx thread", return);
    m_udpReady = 0;
    m_notifyEndpointDown = false;
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

  void resetRuntimeState() {
    Base::resetLink_();
    m_peerParamsValidated = false;
    m_bootstrap = {};
  }

  bool initRuntimeCrypto() {
    if (!m_bootstrap.startRandom()) {
      Base::tlsFailure();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.initialSCID(),
      m_bootstrap.initialDCID(),
      m_bootstrap.initialDCID(), m_bootstrap.initialDCID());
    ZquicLOG(app()->qlogTrace(), ([
      local = Endpoint::local(),
      remote = Endpoint::remote(),
      linkInfo = Zquic::LinkInfo{
	.vantage = Zquic::Vantage::Client,
	.origDCID = m_bootstrap.initialDCID(),
	.groupID = m_bootstrap.initialDCID(),
	.dcid = m_bootstrap.initialDCID(),
	.scid = m_bootstrap.initialSCID()
      }
    ](auto &o, ZuTime time) {
      o.logCxnStarted(
	CxnStartedEvt{.local = local, .remote = remote, .linkInfo = linkInfo},
	time);
    }));
    Base::configLocalParams(app());
    if (!Base::deriveInitial_()) return false;
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
      .serverName = m_server,
      .qlogTrace = &app()->qlogTrace(),
      .saveSessionTicketArg = impl(),
      .saveSessionTicket = [](void *arg, ZuBSpan ticket) {
	static_cast<Impl *>(arg)->saveEarlyData_(ticket);
      }
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
    resetRuntimeState();
    Base::initClientPath_(Endpoint::local(), Endpoint::remote());
    if (!initRuntimeCrypto()) return false;
    if (!Base::startHandshake()) return false;
    return emitTLS_(0, {}, Endpoint::remote());
  }

  bool restartAfterRetry() {
    Base::resetLink_();
    m_peerParamsValidated = false;
    Base::initClientPath_(Endpoint::local(), Endpoint::remote());
    Base::setRuntimeCIDs_(
      m_bootstrap.retrySCID(), m_bootstrap.initialSCID(),
      m_bootstrap.retrySCID(),
      m_bootstrap.initialDCID(), m_bootstrap.initialDCID());
    Base::configLocalParams(app());
    if (!Base::deriveInitial_()) return false;
    if (!initClientTLS()) return false;
    if (!Base::startHandshake()) return false;
    return emitTLS_(0, {}, Endpoint::remote());
  }

  void markEstablished_() {
    if (!Base::readyToEstablish())
      return;
    if (!m_peerParamsValidated) {
      if (!Base::validateServerParams_(m_bootstrap)) {
	Base::tlsFailure();
	app()->error_(ZeEXCEPT(Error, "Zquic",
	  "server QUIC transport parameters failed validation"));
	connectFailed_0(false);
	return;
      }
      m_peerParamsValidated = true;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    Base::scheduleMigrationCIDs();
    impl()->connected(Connected{
      .alpn = Base::negotiatedProtocol_(),
      .version = Version1
    });
  }

  bool emitTLS_(size_t inEpoch, ZuBSpan input, ZiSockAddr addr) {
    bool ok = Base::template advanceTLS<TLSBufSize>(
      inEpoch, input, ZuMv(addr),
      [this](
	  ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
	  ZiSockAddr addr_) {
	return sendCryptoFlights_(ZuMv(data), offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [](ZiSockAddr) { return true; });
    if (!ok && input) connectFailed_0(false);
    return ok;
  }

  bool sendCryptoFlights_(
    ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
    ZiSockAddr addr) {
    if (!data || !validCryptoOffsets(offsets, data->length)) {
      app()->txRun([link = impl()]() {
	if (!link->disconnecting_())
	  link->invalidCryptoOffsetsTx();
      });
      return true;
    }
    typename Base::TxCryptoSnapshot txCrypto;
    Base::snapshotTxCrypto(txCrypto);
    app()->txRun([
      link = impl(),
      txCrypto,
      payload = ZuMv(data),
      offsets,
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      if (!link->installTxCrypto(txCrypto)) return;
      (void)link->sendCryptoFlightsTx_(
	ZuMv(payload), offsets, ZuMv(addr));
    });
    return true;
  }

  bool sendCryptoFlightsTx_(
    ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
    ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto send outside Tx thread", return false);
    Base::beginLongCoalesce();
    bool ok = Base::sendCryptoFlights_(
      ZuMv(data), offsets, CryptoChunk, ZuMv(addr),
      [this](
	  PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPkt_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
    ok = Base::flushCoalescedInitial(
      [this](auto buf, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf), ZuMv(addr_));
      }) && ok;
    Base::endLongCoalesce();
    return ok;
  }

  bool sendCryptoPkt_(PktNumSpace::T level, ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, frame, ZuMv(addr),
      [this](PktNumSpace::T level_, PktBuild &build, ZuBSpan frame_) {
	return buildPayload_(level_, build, frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_) {
	return sendInitialPkt(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_) {
	return sendHandshakePkt(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_) {
	  return sendShortPkt(build, ZuMv(addr_), frame_);
      });
  }

  void flushStreamWritable(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable(stream);
    if (!flushTx_()) queueTxFlush();
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Endpoint::remote())
      return false;
    return flushTx_(Endpoint::remote());
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
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
    bool sent = Base::flushCtlStreamsBatch_(
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
    sent |= sendPMTUDProbe(ZuMv(addr));
    if (!sent) sent = flushPendingAcks(ZuMv(addr));
    return sent;
  }

  void queueTxFlush() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->flushTx_();
    });
  }
  void queueTxFlush(ZiSockAddr addr) {
    app()->txRun([
      link = impl(),
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->flushTx_(ZuMv(addr));
    });
  }

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
      link->observePathRxTx_(
	ZuMv(local), ZuMv(remote), rxBytes, true, false);
      link->sendPathResponseTx_(
	payload, ZuMv(responseAddr));
    });
    return true;
  }
  bool sendPathResponseTx_(ZuBSpan data, ZiSockAddr addr) {
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

  bool sendCryptoPkt_(
    PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto packet send outside Tx thread", return false);
    return Base::sendCryptoPkt_(
      level, prefix, payload, ref, ZuMv(addr),
      [this](
	  PktNumSpace::T level_, PktBuild &build,
	  ZuBSpan prefix_, ZuBSpan payload_) {
	return buildPayload_(level_, build, prefix_, payload_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendInitialPkt(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePkt(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPkt(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck(
    PktNumSpace::T level, PktBuild &build) {
    return Base::appendPendingAck(level, build);
  }

  bool buildPayload_(PktNumSpace::T level, PktBuild &build, ZuBSpan frame) {
    return Base::buildPayload_(level, build, frame);
  }

  bool buildPayload_(
    PktNumSpace::T level, PktBuild &build,
    ZuBSpan prefix, ZuBSpan payload) {
    return Base::buildPayload_(level, build, prefix, payload);
  }

  bool migrate_(const MigrationParams &params) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration outside Tx thread", return false);
    MigrationReason::T reason = MigrationReason::None;
    if (!Base::prepareMig(params, reason))
      return false;
    return true;
  }

  void migrationCIDReady() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration CID completion outside Tx thread", return);
    if (Base::migLocalRebind()) {
      ZiSockAddr local = Base::migLocal();
      ZiSockAddr remote = Base::migRemote();
      Base::migRebindStart();
      bool posted = Endpoint::rebindUDP(
	local.ip(), local.port(), remote.ip(), remote.port(),
	[link = impl()](
	    bool ok, ZiSockAddr rebound, bool closed) mutable {
	  link->app()->txRun([
	    link, ok, rebound = ZuMv(rebound), closed
	  ]() mutable {
	    if (link->disconnecting_() && !closed) return;
	    if (!ok) {
	      MigrationReason::T reason = closed ?
		MigrationReason::Closed : MigrationReason::Endpoint;
	      link->migRebindFail(reason);
	      link->failMig(reason);
	      return;
	    }
	    link->updateMigLocal(ZuMv(rebound));
	    link->migRebindOK();
	    if (!link->sendMigChal())
	      link->failMig(MigrationReason::Validation);
	  });
	});
      if (!posted) {
	Base::migRebindFail(MigrationReason::Endpoint);
	Base::failMig(MigrationReason::Endpoint);
	return;
      }
      return;
    }
    if (sendMigChal())
      return;
    Base::failMig(MigrationReason::Validation);
  }

  bool sendMigChal() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration challenge outside Tx thread", return false);
    if (!Base::activateMig())
      return false;
    PktBuild build;
    typename Base::TxPktRefs refs;
    ControlFrame frame;
    if (!Base::buildMigChal(build, refs, frame))
      return false;
    bool sent = sendShortPkt(
      Base::migPeerCID(), build,
      Base::migRemote(), {}, &refs, true);
    if (sent)
      Base::migChalSent(frame);
    return sent;
  }

  ZuBSpan initialToken_() const {
    if (m_bootstrap.retried()) return m_bootstrap.retryToken();
    return m_newToken;
  }

	  void newToken_(ZuBSpan token) {
	    if (!token || token.length() > AddressToken::MaxLength) return;
	    ZquicLOG(app()->qlogTrace(), ([
	      tokenLength = token.length(),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvt event{
		.linkInfo = linkInfo
	      ,
		.value = tokenLength,
		.kind = SecKind::Token,
		.reason = SecReason::NewToken,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvt(EvtName::TokenIssued, event, time);
    }));
    m_newToken = token;
    Base::newTokenRx_();
  }

  bool sendPathBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path send outside Tx thread", return false);
    return Base::sendPathPkt(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path probe send outside Tx thread", return false);
    return Base::sendPathProbePkt(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
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
    return Base::sendPathPkt(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn_);
      },
      ecn);
  }
  bool sendStrictShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client strict Short buffer send outside Tx thread", return false);
    return Base::sendValidPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn_, true);
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
      InitialKeyDir::Client, CIDSel::Initial, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), initialToken_(), true,
      payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
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
    return Base::sendProtHandshakePkt(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
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
    return Base::sendProtZeroRTTPkt(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
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
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
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
    return Base::sendProtShortPkt(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
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
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return Endpoint::allocTxPkt(); },
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
    bool sent = sendShortPkt(build, ZuMv(addr), {});
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
		.linkInfo = linkInfo
	      ,
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

  bool sendQueuedStreamPkt(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPkt(
      ZuMv(stream), ZuMv(addr),
      [this](PktBuild &build) {
	return appendPendingAck(PktNumSpace::AppData, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const Base::TxPktRefs &refs) {
	return sendShortPkt(build, ZuMv(addr_), {}, &refs, refs.count() != 0);
      });
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

  void received_(Datagram d) {
    if (handleRetry(d)) return;
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort(d_, packetOffset, packetLen);
      });
  }

  bool handleRetry(const Datagram &d) {
    if (!d.buf || !d.buf->length || Base::establishedRx())
      return false;
    auto packet = d.buf->cspan();
    RetryPkt retry;
    if (Pkt::parseRetry(packet, retry) < 0) return false;
	    if (!m_bootstrap.onRetry(retry)) {
	      ZquicLOG(app()->qlogTrace(), ([
		tokenLength = retry.token.length(),
		linkInfo = Base::linkInfo_()
	      ](auto &o, ZuTime time) {
		SecEvt event{
		  .linkInfo = linkInfo
		,
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
		.linkInfo = linkInfo
	      ,
		.value = tokenLength,
		.kind = SecKind::Retry,
		.reason = SecReason::OK,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvt(EvtName::RetryValid, event, time);
      event.kind = SecKind::Token;
      o.logSecEvt(EvtName::TokenValid, event, time);
    }));
    if (!restartAfterRetry())
      Base::tlsFailure();
    return true;
  }

  bool receivedLong(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt(
      InitialKeyDir::Server, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &) {
	Base::setPeerCIDFromHdrSCID(h);
	return true;
      },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog,
	  bool earlyData) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, earlyData);
      });
  }

  bool receivedShort(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::rxProtShortPkt(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      });
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, Base::RxAckMeta &ack,
    ZquicLog_::PktEvt *qlog, bool earlyData = false) {
    ZiSockAddr peer = addr;
    ZiSockAddr local = Endpoint::local();
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
    if (ok && level == PktNumSpace::AppData && Base::establishedRx())
      Base::observePathRx_(ZuMv(local), ZuMv(peer), pathBytes);
    return ok;
  }

  bool handleControlFrame(
    PktNumSpace::T level, const Frame &frame, ZiSockAddr addr,
    ZiSockAddr local = {}, unsigned pathBytes = 0) {
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
	if (Base::establishedRx())
	  Base::discardPktNumSpace_0(PktNumSpace::Handshake);
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
	    .linkInfo = linkInfo
	  ,
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
	  link->m_closeNotify = true;
	  link->m_closePeer = true;
	  link->m_closeFn = CloseFn{[]() { }};
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
  void streamsBlocked_(Zquic::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush();
  }

public:
  void endpointDatagram(Datagram d) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [link = impl(), d = ZuMv(d)]() mutable {
      if (!link->disconnecting_()) link->received_(ZuMv(d));
      return link;
    });
  }
  void endpointReady_(Endpoint *ep) {
    if (Base::disconnecting_()) return;
    app()->rxInvoke(impl(), [link = impl(), ep]() mutable {
      if (!link->disconnecting_()) link->endpointReadyRx_(ep);
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
      if (!link->disconnecting_()) link->endpointDownRx_(ep);
      return link;
    });
  }
private:
  void endpointReadyRx_(Endpoint *ep) {
    if (ep != this) return;
    m_notifyEndpointDown = true;
    ++m_udpReadyCount;
    Base::endpointReady_();
    if (!startHandshake()) {
      connectFailed_0(false);
      return;
    }
    m_udpReady = 1;
  }
  void endpointDownRx_(Endpoint *ep) {
    if (ep != this) return;
    closeEndpointDrained_(m_notifyEndpointDown, true, CloseFn{});
  }
  void resetDisconnected() {
    closeEndpointDrained_(true, true, CloseFn{});
  }

  void connectFailed_0(bool transient) {
    Base::endpointFailure_();
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect failure before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), transient]() {
      if (!link->disconnecting_()) link->connectFailed(transient);
      return link;
    });
  }

  // shared
  ZmAtomic<uint64_t>	m_udpReadyCount = 0;
  ZmAtomic<unsigned>	m_udpReady = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  Host			m_server;
  ZiIP			m_remote;
  ClientBootstrap	m_bootstrap;
  TokenBytes		m_newToken;
  uint16_t		m_port = 0;
  bool			m_peerParamsValidated = false;
  bool			m_notifyEndpointDown = true;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  CloseFn		m_closeFn;
  bool			m_closeNotify = false;
  bool			m_closePeer = false;
};

} // namespace Zquic
