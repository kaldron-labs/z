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
  static constexpr unsigned RuntimePNLength = 2;
  static constexpr unsigned RuntimeCryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  friend Endpoint;
  template <typename, typename, typename> friend class Zquic::Stream;

  CliLink(App *app) : Base{app, false} { Base::initCryptoDelivery_(); }
  CliLink(App *app, Host server, uint16_t port) :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port} {
    Base::initCryptoDelivery_();
  }
  ~CliLink() = default;

  void connect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->connect_();
      return link;
    });
  }
  void connect(Host server, uint16_t port) {
    m_server = ZuMv(server);
    m_port = port;
    connect();
  }

  void disconnect() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->disconnect_();
      return link;
    });
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
    closeCurrent_(true, false, ZuFwd<L>(l));
    Base::resetTLS_();
  }
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
    Base::closeState_();
    closeEndpointDrained_(true, ZuFwd<L>(l));
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }
  bool udpReady() const { return m_udpReady; }
  uint64_t udpReadyCount() const { return m_udpReadyCount; }
  bool ready() const { return m_udpReady; }
  bool established() const { return Base::runtimeEstablished_(); }
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
  void runtimeDiag(L &&l) const { Base::runtimeDiag(ZuFwd<L>(l)); }
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
    params.closeOnFailure = app()->migrationCloseOnFailure();
    return migrate(params);
  }
  bool migrateRemote(ZiSockAddr remote) {
    MigrationParams params;
    params.local = Endpoint::local();
    params.remote = ZuMv(remote);
    params.reason = MigrationReason::Active;
    params.closeOnFailure = app()->migrationCloseOnFailure();
    return migrate(params);
  }
  bool pathValidated() const { return Base::pathValidated_(); }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  uint64_t pathAntiAmplification() const {
    return Base::pathAntiAmplification_();
  }
  const Crypto &crypto() const { return Base::crypto_(); }

  bool send(StreamRef stream, ZuBSpan payload, bool fin = true) {
    if (Base::disconnecting_()) return false;
    if (!stream || (!payload && !fin))
      return false;
    AsyncSendPayload payload_;
    payload_.append(payload.data(), payload.length());
    app()->txInvoke([
      link = this->impl(),
      stream = ZuMv(stream),
      payload = ZuMv(payload_),
      fin
    ]() mutable {
      if (link->disconnecting_()) return;
      link->send_(
	ZuMv(stream),
	payload,
	fin);
    });
    return true;
  }
  bool send_(StreamRef stream, ZuBSpan payload, bool fin = true) {
    // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client send_ outside Tx thread", return false);
    if (Base::closed() || !stream || (!payload && !fin))
      return false;
    if (payload) {
      auto tx = stream->txStream_();
      tx << payload;
      tx.flush();
    }
    if (fin) stream->fin();
    queueTxFlush_();
    return true;
  }

  void pto_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client PTO outside Tx thread", return);
    if (Base::closed() || !Endpoint::connected())
      return;
    Base::ptoRecovery_(
      [this](
	  PktNumSpace::T level, PktBuild &build,
	  const SentFrameRef &cryptoRef) {
	return level == PktNumSpace::Initial ?
	  sendInitialPkt_(
	    build, Endpoint::remote(), {}, &cryptoRef, true, false) :
	  sendHandshakePkt_(
	    build, Endpoint::remote(), {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe_(level); });
  }

  void queueRetransmit_() {
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
    if (Base::closed() || !Endpoint::connected())
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
	if (!Base::buildRetransmitCrypto_(level, build, ref)) continue;
	bool ok =
	  level == PktNumSpace::Initial ?
	    sendInitialPkt_(build, Endpoint::remote(), {}, &ref, true, false) :
	  level == PktNumSpace::Handshake ?
	    sendHandshakePkt_(build, Endpoint::remote(), {}, &ref, true, false) :
	    sendShortPkt_(build, Endpoint::remote(), {}, &ref, true);
	if (!ok) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
      }
      if (level != PktNumSpace::AppData || !Base::runtimeEstablished_())
	continue;
      if (!pto && !Base::congestionAllowance_()) {
	Base::requeueRetransmit_(level, ref);
	break;
      }
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetransmitStream_(build, ref)) {
	  if (Base::debugLog_())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit build failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	  continue;
	}
      } else if (!Base::buildRetransmitControl_(build, ref))
	continue;
      if (!sendShortPkt_(build, Endpoint::remote(), {}, &ref, true)) {
	if (ref.kind == SentFrameKind::Stream)
	  if (Base::debugLog_())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit send failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	Base::requeueRetransmit_(level, ref);
	break;
      }
      sent = true;
    }
    if (sent) Base::schedulePTO_();
    if (sent && processed == MaxBatch) queueRetransmit_();
    return sent;
  }

  bool sendPingProbe_(PktNumSpace::T level) {
    PktBuild build;
    if (!Base::buildPingProbe_(level, build)) return false;
    typename Base::TxPktRefs refs;
    switch (level) {
      case PktNumSpace::Initial:
	return sendInitialPkt_(
	  build, Endpoint::remote(), {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt_(
	  build, Endpoint::remote(), {}, nullptr, true, false);
      default:
	return sendShortPkt_(
	  build, Endpoint::remote(), {},
	  &refs, true);
    }
  }

  bool sendPMTUDProbe_(ZiSockAddr addr) {
    return Base::sendPMTUDProbe_(
      ZuMv(addr),
      [this](PktBuild &build, ZiSockAddr addr_, unsigned size) {
	typename Base::TxPktRefs refs;
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true, size);
      });
  }
  void connect_() { // direct call from within rx thread
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect before app initialization", return);
    ZiIP ip = m_server;
    if (!ip || !m_port) {
      app()->error_(ZeEXCEPT(Error, "Zquic",
	([server = LogMsg{m_server}, port = m_port](auto &s) {
	  s << '"' << server << "\": invalid QUIC UDP peer port=" << port;
	})));
      connectFailed_0(true);
      return;
    }

    closeCurrent_(false, [link = impl(), ip]() mutable {
      link->app()->rxRun([link, ip]() mutable {
	link->connectOpen_(ip);
      });
    });
  }

  void connectOpen_(ZiIP ip) {
    resetRuntimeState_();
    Base::resetRuntimeDiag_();

    if (!Endpoint::init(app()->mx())) {
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

  ZuBSpan initialTxToken_() const {
    return initialToken_();
  }

private:
  using InitialKeyDir = typename Base::InitialKeyDir;
  using RuntimeCID = typename Base::RuntimeCID;

  void closeCurrent_(bool notify) {
    closeCurrent_(notify, false, CloseFn{});
  }

  template <typename L>
  void closeCurrent_(bool notify, L &&l) {
    closeCurrent_(notify, false, ZuFwd<L>(l));
  }

  template <typename L>
  void closeCurrent_(bool notify, bool peer, L &&l) {
    if (notify && Base::runtimeEstablished_() &&
	Endpoint::connected() && Endpoint::remote()) {
      closeAfterDisconnect_(
	notify, peer, Endpoint::remote(), CloseFn{ZuFwd<L>(l)});
      return;
    }
    closeEndpoint_(notify, peer, ZuFwd<L>(l));
  }

  void closeAfterDisconnect_(
      bool notify, bool peer, ZiSockAddr addr, CloseFn fn) {
    Base::closeState_();
    app()->txInvoke(impl(), [
      link = impl(),
      notify,
      peer,
      addr = ZuMv(addr),
      fn = ZuMv(fn)
    ]() mutable {
      (void)link->sendCloseFrame_(ZuMv(addr), true);
      link->m_closeNotify = notify;
      link->m_closePeer = peer;
      link->m_closeFn = ZuMv(fn);
      link->enterLocalClosingTx_();
      return link;
    });
  }

  void closeExpired_(bool drain = true) {
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
		CloseEvent event{
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
  void idleExpired_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client idle expiry outside Tx thread", return);
    Base::cancelTimers_();
	    m_closeNotify = true;
	    m_closePeer = false;
	    m_closeFn = CloseFn{[]() { }};
	    ZquicLOG(app()->qlogTrace(), ([linkInfo = Base::linkInfo_()](auto &o, ZuTime time) {
	      CloseEvent event{
		.linkInfo = linkInfo
	      ,
		.initiator = CloseInitiator::Local,
		.trigger = CloseTrigger::IdleTimeout,
		.reason = CloseReason::Idle,
		.connectionError = CloseError::NoError};
      o.logCxnClosed(event, time);
    }));
    closeExpired_(false);
  }

  void closeEndpoint_(bool notify) {
    closeEndpoint_(notify, false, CloseFn{});
  }

  template <typename L>
  void closeEndpoint_(bool notify, L &&l) {
    closeEndpoint_(notify, false, ZuFwd<L>(l));
  }

  template <typename L>
  void closeEndpoint_(bool notify, bool peer, L &&l) {
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
    if (notify) impl()->disconnected(peer);
    if (Base::closed()) Base::clearCallbacks_();
    Base::disconnect(peer);
    Endpoint::disconnect([
      link = impl(), l = ZuFwd<L>(l)
    ]() mutable {
      link->app()->txRun([
	link, l = ZuMv(l)
      ]() mutable {
	link = nullptr;
	l();
      });
    });
  }

  void resetRuntimeState_() {
    Base::resetRuntime_();
    m_peerParamsValidated = false;
    m_bootstrap = {};
  }

  bool initRuntimeCrypto_() {
    if (!m_bootstrap.startRandom()) {
      Base::tlsFailure_();
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
	CxnStartedEvent{.local = local, .remote = remote, .linkInfo = linkInfo},
	time);
    }));
    Base::configureLocalTransportParams_(app());
    if (!Base::deriveInitial_()) return false;
    if (!initClientTLS_()) return false;
    return true;
  }

  bool initClientTLS_() {
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
	  app()->validateEarlyDataParams(impl(), appParams)) {
	Base::rememberZeroRTTParams_(*rememberedParams);
	config.enable0RTT = true;
	config.sessionTicket = sessionTicket;
      } else
	Base::clearZeroRTTParams_();
    }
    return Base::initTLS_(config);
  }

  void saveEarlyData_(ZuBSpan ticket) {
    if (!ticket) {
      app()->saveEarlyData(impl(), {}, {});
      return;
    }
    if (!Base::crypto_().peerTransportParamsReceived()) return;
    app()->saveEarlyData(
      impl(), ticket, Base::crypto_().peerTransportParams());
  }

  bool startHandshake_() {
    if (Base::runtimeHandshakeStarted_()) return true;
    resetRuntimeState_();
    Base::initClientPath_(Endpoint::local(), Endpoint::remote());
    if (!initRuntimeCrypto_()) return false;
    if (!Base::startRuntimeHandshake_()) return false;
    return emitTLS_(0, {}, Endpoint::remote());
  }

  bool restartHandshakeAfterRetry_() {
    Base::resetRuntime_();
    m_peerParamsValidated = false;
    Base::initClientPath_(Endpoint::local(), Endpoint::remote());
    Base::setRuntimeCIDs_(
      m_bootstrap.retrySCID(), m_bootstrap.initialSCID(),
      m_bootstrap.retrySCID(),
      m_bootstrap.initialDCID(), m_bootstrap.initialDCID());
    Base::configureLocalTransportParams_(app());
    if (!Base::deriveInitial_()) return false;
    if (!initClientTLS_()) return false;
    if (!Base::startRuntimeHandshake_()) return false;
    return emitTLS_(0, {}, Endpoint::remote());
  }

  void markEstablished_() {
    if (!Base::runtimeReadyToEstablish_())
      return;
    if (!m_peerParamsValidated) {
      if (!Base::validateServerTransportParams_(m_bootstrap)) {
	Base::tlsFailure_();
	app()->error_(ZeEXCEPT(Error, "Zquic",
	  "server QUIC transport parameters failed validation"));
	return;
      }
      m_peerParamsValidated = true;
    }
    Base::establishRuntime_();
    Base::validatePath_();
    Base::schedulePMTUD();
    Base::scheduleMigrationCIDs_();
    impl()->connected(Zi::Connected{
      .transport = Zi::Transport::QUIC,
      .alpn = Base::negotiatedProtocol_(),
      .version = int(Version1)
    });
  }

  bool emitTLS_(size_t inEpoch, ZuBSpan input, ZiSockAddr addr) {
    return Base::template advanceTLS_<TLSBufSize>(
      inEpoch, input, ZuMv(addr),
      [this](
	  const uint8_t *data, unsigned len, const size_t offsets[5],
	  ZiSockAddr addr_) {
	return sendCryptoFlights_(data, len, offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [](ZiSockAddr) { return true; });
  }

  bool sendCryptoFlights_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    typename Base::TxCryptoSnapshot txCrypto;
    Base::snapshotTxCrypto_(txCrypto);
    AsyncSendPayload payload;
    payload.append(data, len);
    size_t offset0 = offsets[0];
    size_t offset1 = offsets[1];
    size_t offset2 = offsets[2];
    size_t offset3 = offsets[3];
    size_t offset4 = offsets[4];
    app()->txRun([
      link = impl(),
      txCrypto,
      payload = ZuMv(payload),
      offset0, offset1, offset2, offset3, offset4,
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      size_t offsets_[5] = {offset0, offset1, offset2, offset3, offset4};
      if (!link->installTxCrypto_(txCrypto)) return;
      (void)link->sendCryptoFlightsTx_(
	payload.data(), payload.length(), offsets_, ZuMv(addr));
    });
    return true;
  }

  bool sendCryptoFlightsTx_(
    const uint8_t *data, unsigned len, const size_t offsets[5],
    ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client crypto send outside Tx thread", return false);
    Base::beginLongCoalesce_();
    bool ok = Base::sendCryptoFlights_(
      data, len, offsets, RuntimeCryptoChunk, ZuMv(addr),
      [this](
	  PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
	  const SentFrameRef &ref, ZiSockAddr addr_) {
	return sendCryptoPkt_(
	  level, prefix, payload, ref, ZuMv(addr_));
      });
    ok = Base::flushCoalescedInitial_(
      [this](auto buf, ZiSockAddr addr_) {
	return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
      }) && ok;
    Base::endLongCoalesce_();
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
	return sendInitialPkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_) {
	return sendHandshakePkt_(build, ZuMv(addr_), frame_);
      },
      [this](PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_) {
	  return sendShortPkt_(build, ZuMv(addr_), frame_);
      });
  }

  void txDrained_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Tx drain outside Tx thread", return);
    flushTx_();
  }

  void flushStreamWritable_(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable_(stream);
    if (!flushTx_()) queueTxFlush_();
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::closed() || !Endpoint::remote())
      return false;
    return flushTx_(Endpoint::remote());
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::closed()) return false;
    if (!Base::runtimeEstablished_()) {
      bool sent = flushPendingAcks_(addr);
      if (!Base::canTxZeroRTT_()) return sent;
      return Base::flushEarlyStreams_(
	addr,
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const typename Base::TxPktRefs &refs) {
	  return sendZeroRTTPkt_(
	    build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	}) || sent;
    }
    bool sent = Base::flushControlAndStreams_(
      addr,
      [this](PktBuild &build) {
	return appendPendingAck_(PktNumSpace::AppData, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs &refs) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, refs.count() != 0);
      });
    sent |= sendPMTUDProbe_(ZuMv(addr));
    if (!sent) sent = flushPendingAcks_(ZuMv(addr));
    return sent;
  }

  void queueTxFlush_() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->flushTx_();
    });
  }
  void queueTxFlush_(ZiSockAddr addr) {
    app()->txRun([
      link = impl(),
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting_()) return;
      link->flushTx_(ZuMv(addr));
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
	return sendInitialPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendHandshakePkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_, ZuBSpan frame_,
	  const SentFrameRef *ref_, bool ackEliciting_) {
	return sendShortPkt_(
	  build, ZuMv(addr_), frame_, ref_, ackEliciting_);
      });
  }

  bool appendPendingAck_(
    PktNumSpace::T level, PktBuild &build) {
    return Base::appendPendingAck_(level, build);
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
    if (!Base::prepareActiveMigration_(params, reason))
      return false;
    if (params.rebindLocal) {
      ZiSockAddr local = params.local;
      ZiSockAddr remote = Base::activeMigrationRemote_();
      Base::activeMigrationRebindStart_();
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
	      link->activeMigrationRebindFail_(reason);
	      link->failActiveMigration_(reason);
	      return;
	    }
	    link->updateActiveMigrationLocal_(ZuMv(rebound));
	    link->activeMigrationRebindOK_();
	    if (!link->sendActiveMigrationChallenge_())
	      link->failActiveMigration_(MigrationReason::Validation);
	  });
	});
      if (!posted) {
	Base::activeMigrationRebindFail_(MigrationReason::Endpoint);
	Base::failActiveMigration_(MigrationReason::Endpoint);
	return false;
      }
      return true;
    }
    if (sendActiveMigrationChallenge_())
      return true;
    Base::failActiveMigration_(MigrationReason::Validation);
    return false;
  }

  bool sendActiveMigrationChallenge_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client migration challenge outside Tx thread", return false);
    if (!Base::activateActiveMigration_())
      return false;
    PktBuild build;
    typename Base::TxPktRefs refs;
    ControlFrame frame;
    if (!Base::buildActiveMigrationChallenge_(build, refs, frame))
      return false;
    bool sent = sendShortPkt_(
      Base::activeMigrationPeerCID_(), build,
      Base::activeMigrationRemote_(), {}, &refs, true);
    if (sent)
      Base::activeMigrationChallengeSent_(frame);
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
	      SecEvent event{
		.linkInfo = linkInfo
	      ,
		.value = tokenLength,
		.kind = SecKind::Token,
		.reason = SecReason::NewToken,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvent(EventName::TokenIssued, event, time);
    }));
    m_newToken = token;
    Base::newTokenRx_();
  }

  bool sendPathBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path send outside Tx thread", return false);
    return Base::sendPathPkt_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendPathProbeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client path probe send outside Tx thread", return false);
    return Base::sendPathProbePkt_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendInitialBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Initial buffer send outside Tx thread", return false);
    return Base::holdInitialForCoalesce_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf_(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendHandshakeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Handshake buffer send outside Tx thread", return false);
    return Base::sendHandshakeCoalesced_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf_(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendShortBuf_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn,
    unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Short buffer send outside Tx thread", return false);
    if (pmtudSize) return sendPathProbeBuf_(ZuMv(buf), ZuMv(addr));
    return Base::sendPathPkt_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn_);
      },
      ecn);
  }

  bool sendInitialPkt_(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt_(
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
    return Base::sendProtInitialPkt_(
      InitialKeyDir::Client, RuntimeCID::Initial, RuntimeCID::Local,
      Base::txPNLength_(PktNumSpace::Initial), initialToken_(), true,
      payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
	return sendInitialBuf_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendHandshakePkt_(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Handshake, payload, frame)) return false;
    return sendHandshakePkt_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt_(
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
    return Base::sendProtHandshakePkt_(
      RuntimeCID::Peer, RuntimeCID::Local,
      Base::txPNLength_(PktNumSpace::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
	return sendHandshakeBuf_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendZeroRTTPkt_(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client 0-RTT send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendZeroRTTPkt_(build, ZuMv(addr), payload);
  }

  bool sendZeroRTTPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return sendZeroRTTPkt_(
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting);
  }

  bool sendZeroRTTPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const typename Base::TxPktRefs *recordRefs, bool ackEliciting) {
    if (!Endpoint::connected()) return false;
    return Base::sendProtZeroRTTPkt_(
      RuntimeCID::Peer, RuntimeCID::Local,
      Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
      [this](auto buf, ZiSockAddr addr_) {
	return sendShortBuf_(ZuMv(buf), ZuMv(addr_), EcnMark::NotECT);
      });
  }

  bool sendShortPkt_(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendShortPkt_(build, ZuMv(addr), payload);
  }

  bool sendShortPkt_(
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
    return sendShortPkt_(payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting, pmtudSize);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const typename Base::TxPktRefs *recordRefs, bool ackEliciting,
    unsigned pmtudSize = 0) {
    if (!Endpoint::connected()) return false;
    return Base::sendProtShortPkt_(
      RuntimeCID::Peer, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
      [this, pmtudSize](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf_(
	  ZuMv(buf), ZuMv(addr_), ecn, pmtudSize);
      },
      pmtudSize);
  }

  bool sendShortPkt_(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    ZuBSpan recordFrame, const typename Base::TxPktRefs *recordRefs,
    bool ackEliciting, unsigned pmtudSize = 0) {
    if (!Endpoint::connected()) return false;
    return Base::sendProtShortPkt_(
      dcid, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(); },
      [this, pmtudSize](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf_(
	  ZuMv(buf), ZuMv(addr_), ecn, pmtudSize);
      },
      pmtudSize);
  }

  bool sendCloseFrame_(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC client close frame outside Tx thread", return false);
    if ((!closing && Base::closed()) ||
	(!Base::runtimeEstablished_() &&
	  !(closing && Base::runtimeClosing_())) ||
	!Endpoint::connected() || !addr)
      return false;
    PktBuild build;
    bool appClose = Base::appCloseOnDisconnect_();
    if (!Base::writeCloseFrame_(build, appClose))
      return false;
    bool sent = sendShortPkt_(build, ZuMv(addr), {});
    if (sent) {
      ZquicLOG(app()->qlogTrace(), ([
	      appClose,
	      errorCode = Base::closeError(),
	      closeError = Base::closeError() ?
		CloseError::T(CloseError::Unknown) :
		CloseError::T(CloseError::NoError),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
		CloseTrigger::T trigger = appClose ?
		  CloseTrigger::T(CloseTrigger::Application) :
	  CloseTrigger::T(CloseTrigger::Error);
	CloseEvent event{
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

  bool sendQueuedStreamPkt_(StreamRef stream, ZiSockAddr addr) {
    return Base::sendQueuedStreamPkt_(
      ZuMv(stream), ZuMv(addr),
      [this](PktBuild &build) {
	return appendPendingAck_(PktNumSpace::AppData, build);
      },
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs &refs) {
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, refs.count() != 0);
      });
  }

  bool flushPendingAck_(
    PktNumSpace::T level, ZiSockAddr addr, bool &sent) {
    PktBuild build;
    build.reset();
    if (!Base::appendPendingAck_(level, build, true)) return false;
    if (!build.bytes()) return true;
    bool ok;
    if (level == PktNumSpace::Initial) {
      ok = sendInitialPkt_(build, ZuMv(addr), {}, nullptr, false, false);
      if (ok) sent = true;
      return ok;
    }
    if (!Base::txTrafficSecretInstalled_(level)) return true;
    if (level == PktNumSpace::Handshake)
      ok = sendHandshakePkt_(build, ZuMv(addr), {});
    else
      ok = sendShortPkt_(build, ZuMv(addr), {});
    if (ok) sent = true;
    return ok;
  }

  bool flushPendingAcks_(ZiSockAddr addr) {
    bool sent = false;
    if (Base::pendingAck_(PktNumSpace::Initial))
      (void)flushPendingAck_(PktNumSpace::Initial, addr, sent);
    if (Base::pendingAck_(PktNumSpace::Handshake))
      (void)flushPendingAck_(PktNumSpace::Handshake, addr, sent);
    if (Base::pendingAck_(PktNumSpace::AppData))
      (void)flushPendingAck_(PktNumSpace::AppData, ZuMv(addr), sent);
    if (sent)
      (void)Base::flushCoalescedInitial_(
	[this](auto buf, ZiSockAddr addr_) {
	  return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
	});
    return sent;
  }

  void received_(Datagram d) {
    if (handleRetry_(d)) return;
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen);
      });
  }

  bool handleRetry_(const Datagram &d) {
    if (!d.buf || !d.buf->length || Base::runtimeEstablished_())
      return false;
    auto packet = d.buf->cspan();
    RetryPkt retry;
    if (Pkt::parseRetry(packet, retry) < 0) return false;
	    if (!m_bootstrap.onRetry(retry)) {
	      ZquicLOG(app()->qlogTrace(), ([
		tokenLength = retry.token.length(),
		linkInfo = Base::linkInfo_()
	      ](auto &o, ZuTime time) {
		SecEvent event{
		  .linkInfo = linkInfo
		,
		  .value = tokenLength,
		  .kind = SecKind::Retry,
		  .reason = SecReason::Validation,
		  .success = false};
	event.trigger = SecTrigger::Received;
	o.logSecEvent(EventName::RetryValid, event, time);
      }));
      Base::packetParseFailure_();
      return true;
    }
	    ZquicLOG(app()->qlogTrace(), ([
	      tokenLength = retry.token.length(),
	      linkInfo = Base::linkInfo_()
	    ](auto &o, ZuTime time) {
	      SecEvent event{
		.linkInfo = linkInfo
	      ,
		.value = tokenLength,
		.kind = SecKind::Retry,
		.reason = SecReason::OK,
		.success = true};
      event.trigger = SecTrigger::Received;
      o.logSecEvent(EventName::RetryValid, event, time);
      event.kind = SecKind::Token;
      o.logSecEvent(EventName::TokenValid, event, time);
    }));
    if (!restartHandshakeAfterRetry_())
      Base::tlsFailure_();
    return true;
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt_(
      InitialKeyDir::Server, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &) {
	Base::setPeerCIDFromHdrSCID_(h);
	return true;
      },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  typename Base::RxAckMeta &ack, ZquicLog_::PktEvent *qlog,
	  bool earlyData) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, earlyData);
      });
  }

  bool receivedShort_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtShortPkt_(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  typename Base::RxAckMeta &ack, ZquicLog_::PktEvent *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      });
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, typename Base::RxAckMeta &ack,
    ZquicLog_::PktEvent *qlog, bool earlyData = false) {
    ZiSockAddr peer = addr;
    bool ok = Base::consumeProtFrames_(
      level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
      [this](size_t epoch, ZuBSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this](
	  PktNumSpace::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(level_, frame, ZuMv(addr_));
      },
      earlyData);
    if (ok && level == PktNumSpace::AppData && Base::runtimeEstablished_())
      Base::observePathRx_(Endpoint::local(), ZuMv(peer));
    return ok;
  }

  bool handleControlFrame_(
    PktNumSpace::T, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	queueTxFlush_();
	return true;
      case FrameType::PathChallenge: {
	Base::queuePathResponse_(frame.payload);
	queueTxFlush_(ZuMv(addr));
	return true;
      }
      case FrameType::PathResponse:
	Base::receivePathResponse_(frame.payload);
	return true;
      case FrameType::HandshakeDone:
	if (Base::runtimeEstablished_())
	  Base::discardPktNumSpace_(PktNumSpace::Handshake);
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
	  CloseEvent event{
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
	Base::enterPeerDraining_(frame.errorCode);
	return true;
      default:
	return true;
    }
  }

  void dataBlocked_(uint64_t maximum) {
    Base::queueBlocked_(FrameType::DataBlocked, 0, maximum);
    impl()->flowBlocked(
      FrameType::DataBlocked, 0, Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamDataBlocked_(uint64_t streamID, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamDataBlocked, streamID, maximum);
    impl()->flowBlocked(
      FrameType::StreamDataBlocked, streamID,
      Zi::StreamType::Duplex, maximum);
    queueTxFlush_();
  }
  void streamsBlocked_(Zi::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked_(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush_();
  }

public:
  void endpointDatagram_(Datagram d) {
    if (Base::disconnecting_()) return;
    app()->rxRun([link = impl(), d = ZuMv(d)]() mutable {
      if (link->disconnecting_()) return;
      link->received_(ZuMv(d));
    });
  }
  void endpointReady_(Endpoint *ep) {
    if (Base::disconnecting_()) return;
    app()->rxRun([link = impl(), ep]() mutable {
      if (link->disconnecting_()) return;
      link->endpointReadyRx_(ep);
    });
  }

  void endpointFailed_(bool transient) {
    if (Base::disconnecting_()) return;
    app()->rxRun([link = impl(), transient]() mutable {
      if (link->disconnecting_()) return;
      link->connectFailed_0(transient);
    });
  }
  void endpointDown_(Endpoint *ep) {
    if (Base::disconnecting_()) return;
    app()->rxRun([link = impl(), ep]() mutable {
      if (link->disconnecting_()) return;
      link->endpointDownRx_(ep);
    });
  }
  void endpointTxDrained_() {
    if (Base::disconnecting_()) return;
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->txDrained_();
    });
  }

private:
  void endpointReadyRx_(Endpoint *ep) {
    if (ep != this) return;
    m_notifyEndpointDown = true;
    ++m_udpReadyCount;
    Base::endpointReady_();
    startHandshake_();
    m_udpReady = 1;
  }
  void endpointDownRx_(Endpoint *ep) {
    if (ep != this) return;
    m_udpReady = 0;
    bool notify = m_notifyEndpointDown;
    m_notifyEndpointDown = true;
    resetRuntimeState_();
    if (notify) {
      impl()->disconnected(true);
    }
  }

  void connectFailed_0(bool transient) {
    Base::endpointFailure_();
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC client connect failure before app initialization", return);
    app()->rxRun([link = impl(), transient]() {
      if (link->disconnecting_()) return;
      link->connectFailed(transient);
    });
  }

  // shared
  ZmAtomic<uint64_t>	m_udpReadyCount = 0;
  ZmAtomic<unsigned>	m_udpReady = 0;
  bool			m_closeNotify = false;
  bool			m_closePeer = false;
  CloseFn		m_closeFn;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  Host			m_server;
  ClientBootstrap	m_bootstrap;
  TokenBytes		m_newToken;
  uint16_t		m_port = 0;
  bool			m_peerParamsValidated = false;
  bool			m_notifyEndpointDown = true;
};

} // namespace Zquic
