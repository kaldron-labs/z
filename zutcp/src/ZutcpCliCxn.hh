//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Client connection API implementation

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

namespace Zutcp {

template <
  typename App, typename Impl,
  typename TxBufAlloc_ = TxBufAlloc<>>
class CliCxn :
  public Connection<App, Impl, TxBufAlloc_>,
  public Endpoint_<CliCxn<App, Impl, TxBufAlloc_>> {
public:
  using Base = Connection<App, Impl, TxBufAlloc_>;
  using Endpoint = Endpoint_<CliCxn>;
  using CloseFn = ZmFn<void(), ZmFnHeapID<"Zutcp.CliCxn.CloseFn">>;
  static constexpr bool EndpointRef = true;
  static constexpr unsigned PNLength = 2;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  friend Endpoint;

  CliCxn(App *app) : Base{app, false} { }
  CliCxn(App *app, ZuID id) : Base{app, false, ZuMv(id)} {
   
  }
  CliCxn(App *app, Host server, uint16_t port) :
    Base{app, false}, m_server{ZuMv(server)}, m_port{port} {
   
  }
  CliCxn(App *app, ZuID id, Host server, uint16_t port) :
    Base{app, false, ZuMv(id)},
    m_server{ZuMv(server)}, m_port{port} {
   
  }
  ~CliCxn() = default;

  void connect() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
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
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->disconnect_();
      return link;
    });
  }
  bool disconnect(uint64_t errorCode) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client disconnect before app initialization", return false);
    bool accepted = !Base::closed();
    app()->rxInvoke(impl(), [link = impl(), errorCode]() mutable {
      link->disconnectRx_(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx_(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (),
      "QUIC client disconnect outside Rx thread", return);
    if (app()->nativeTx()) {
      if (!errorCode) disconnect_();
      else if (!m_closeDrainPending && m_closeDrainedGen != Base::dataGen() &&
	  !Base::disconnecting_())
	Base::abortLocalRx_(errorCode, [link = impl()] {
	  link->closeEndpointDrained_(true);
	}, true);
      return;
    }
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
  template <typename L, typename = ZuIfT<ZuIsConstructible<L, CloseFn>{}>>
  void disconnect(L &&l) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
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
  }
  void disconnected(bool) { }
  void nativeFinished_() {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native finish outside Tx", return);
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_()) return;
      link->closeEndpointDrained_(true, false, CloseFn{});
    });
  }

  void abort() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->abort_();
      return link;
    });
  }
  template <typename L>
  void abort(L &&l) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
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
    if (app()->nativeTx()) {
      if (m_closeDrainPending || m_closeDrainedGen == Base::dataGen() || Base::disconnecting_()) {
	closeEndpointDrained_(true, ZuFwd<L>(l));
	return;
      }
      Base::abortLocalRx_(0, [link = impl(), l = ZuFwd<L>(l)]() mutable {
	link->closeEndpointDrained_(true, ZuMv(l));
      });
    } else {
      Base::closeState();
      closeEndpointDrained_(true, ZuFwd<L>(l));
    }
  }

  const Host &server() const { return m_server; }
  uint16_t port() const { return m_port; }
  uint64_t readyCount() const { return m_readyCount; }
  bool ready() const { return m_ready; }
  bool established() const { return Base::established(); }
  bool cxn() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const { return Endpoint::local(); }
  const ZiSockAddr &remote() const { return Endpoint::remote(); }
  ClosedTupleKey closedKeyRx() const {
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "client closed tuple outside Rx", return {});
    return m_endpointAttemptRx && Endpoint::tupleBound() ?
      ClosedTupleKey{local(), remote()} : ClosedTupleKey{};
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
    auto link = const_cast<CliCxn *>(this)->impl();
    mx->txRun([link, l = ZuFwd<L>(l)]() mutable {
      EndpointDiag diag = link->Endpoint::diag();
      l(diag);
    });
  }
  template <typename L>
  void diag(L &&l) const { Base::diag(ZuFwd<L>(l)); }
  template <typename L>
  void pathDiag(L &&l) const { Base::pathDiag(ZuFwd<L>(l)); }






  unsigned activePathMaxPayload() const { return Base::activePathMaxPayload_(); }
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }


  bool send(ZuBSpan payload, bool fin = true) {
    if (Base::disconnecting_() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (app()->txInvoked()) return send_(payload, fin);
    auto tx = this->writer();
    auto gen = tx.gen();
    tx << payload;
    if (!tx.flush()) return false;
    app()->txInvoke([cxn = impl(), fin, gen]() {
      if (gen != cxn->dataGen() || cxn->disconnecting_()) return;
      if (fin) cxn->fin();
      cxn->queueTxFlush();
    });
    return true;
  }
  bool send(ZmRef<ZiIOBuf> payload, bool fin = true) {
    if (Base::disconnecting_() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (app()->txInvoked()) return send_(ZuMv(payload), fin);
    if (app()->nativeTx()) {
      auto gen = this->dataGen();
      if (payload && payload->length && !this->enqueue(ZuMv(payload), gen)) return false;
      app()->txInvoke([cxn = impl(), fin, gen] {
	if (gen != cxn->dataGen() || cxn->disconnecting_()) return;
	if (fin) cxn->fin();
	cxn->queueTxFlush();
      });
      return true;
    }
    app()->txInvoke([
      cxn = impl(), payload = ZuMv(payload), fin, gen = this->dataGen()
    ]() mutable {
      if (gen == cxn->dataGen() && !cxn->disconnecting_())
	cxn->send_(ZuMv(payload), fin);
    });
    return true;
  }
  bool send_(ZmRef<ZiIOBuf> payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "retained send outside Tx thread", return false);
    if (Base::linkClosedTx_() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (payload && payload->length && !this->enqueue_(ZuMv(payload))) return false;
    if (fin) this->fin();
    queueTxFlush();
    return true;
  }
  bool send_(ZuBSpan payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "send outside Tx thread", return false);
    if (Base::linkClosedTx_() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (payload) {
      auto tx = this->writer_();
      tx << payload;
      if (!tx.flush()) return false;
    }
    if (fin) this->fin();
    queueTxFlush();
    return true;
  }

  void pto_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client PTO outside Tx thread", return);
    if (Base::linkClosedTx_() || !Endpoint::connected())
      return;
    Base::ptoRecovery(
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->retransmit_();
    });
  }

  bool retransmit_(
    bool pto = false, unsigned maxPackets = 2,
    PktNumSpace::T onlySpace = PktNumSpace::N) { // Tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Endpoint::connected())
      return false;
    SentFrameRef ref;
    bool sent = false;
    PktNumSpace::T level;
    constexpr unsigned MaxBatch = 16;
    unsigned processed = 0;
    uint64_t packetsBefore = Base::txPackets();
    while (processed < MaxBatch &&
	(!pto || Base::txPackets() - packetsBefore < maxPackets) &&
	Base::nextRetransmit_(level, ref, onlySpace)) {
      ++processed;
      PktBuild build;
      if (ref.kind == SentFrameKind::Data) {
	if (!Base::buildRetxData(build, ref)) {
	  if (Base::debugLog())
	    ZiLOG(Debug, "Zutcp", ([ref](auto &s) {
	      s << "connection retransmit build failed offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	  (void)Base::requeueDataRef(level, ref);
	  break;
	}
      } else if (!Base::buildRetxCtl(build, ref))
	continue;
      if (!sendShortPkt(build, Endpoint::remote(), {}, &ref, true)) {
	if (ref.kind == SentFrameKind::Data)
	  if (Base::debugLog())
	    ZiLOG(Debug, "Zutcp", ([ref](auto &s) {
	      s << "connection retransmit send failed offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	if (ref.kind == SentFrameKind::Data)
	  (void)Base::requeueDataRef(level, ref);
	else
	  Base::requeueRetransmit_(level, ref);
	break;
      }
      sent = true;
    }
    if (sent) Base::schedulePTO_();
    if (sent && !pto && processed == MaxBatch) queueRetransmit();
    return sent;
  }

  bool sendPingProbe(PktNumSpace::T level) {
    PktBuild build;
    if (!Base::buildPingProbe(level, build)) return false;
    typename Base::TxPktRefs refs;
    switch (level) {
      case PktNumSpace::Initial:
	return sendInitialPkt(
	  build, Endpoint::remote(), {}, nullptr, true);
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
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client connect before app initialization", return);
    ZiIP ip = m_remote ? m_remote : ZiIP{m_server};
    if (!ip || !m_port) {
      app()->error_(ZeEXCEPT(Error, "Zutcp",
	([server = LogMsg{m_server}, port = m_port](auto &s) {
	  s << '"' << server << "\": invalid TCP/IP peer port=" << port;
	})));
      connectFailed_0(true, false);
      return;
    }

    closeCurrent(false, [link = impl(), ip, port = m_port, gen = Base::dataGen()]() mutable {
      link->app()->rxRun([link, ip, port, gen]() mutable {
	// Coalesced retries share the old generation; only the first starts setup.
	if (gen == link->dataGen()) link->connect_1(ip, port);
      });
    });
  }

  ZiSockAddr localBind() const { return {}; }

protected:
  // Internal active creation has already reserved before constructing its wrapper.
  void connectReserved_(ZiIP ip, uint16_t port, CxnSlot slot) {
    m_remote = ip;
    m_port = port;
    connect_1(ZuMv(ip), port, ZuMv(slot));
  }

public:
  void connect_1(ZiIP ip, uint16_t port, CxnSlot slot = {}) {
    resetRuntimeState();
    Base::resetDiag();
    if (!ip || !port) {
      Base::adoptCxnSlotRx_(ZuMv(slot));
      connectFailed_0(true);
      return;
    }
    if (auto stack = app()->nativeStack()) {
      if (!slot) slot = stack->reserveCxn();
      if (!slot) { connectFailed_0(false, false); return; }
    }
    Base::adoptCxnSlotRx_(ZuMv(slot));
    if (!Endpoint::init(app()->mx())) {
      connectFailed_0(false);
      return;
    }
    auto local = impl()->localBind();
    unsigned generation = Endpoint::generation();
    bool opened = Endpoint::open(PathMode::ClientConnected,
      local.ip(), local.port(), ip, port);
    // Early open rejection retains the previous Endpoint tuple. It is not
    // protocol history belonging to this newly reset incarnation.
    m_endpointAttemptRx = Endpoint::generation() != generation;
    if (!opened) connectFailed_0(false);
  }

  bool sendTCPReset_() {
    if (!Endpoint::connected() || !Endpoint::remote()) return false;
    return Base::sendTCPResetPkt_(
      [this]() { return Endpoint::allocTxPkt(true); },
      [this](auto buf) { return sendShortBuf(ZuMv(buf), Endpoint::remote(), EcnMark::NotECT); });
  }
  void tcpResetAbort_(bool peer) {
    app()->rxRun([link = ZmRef(impl()), peer, gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_()) return;
      if (peer && !link->m_peerParamsValidated) {
	link->connectFailed_0(false);
	return;
      }
      link->closeEndpointDrained_(true, peer, CloseFn{});
    });
  }

  void policyExpired(CxnTimer::T) {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting_()) link->abort();
    });
  }

  void connectExpired() {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_() ||
	  link->establishedRx() || link->linkClosedRx_()) return;
      link->connectFailed_0(true);
      if (gen == link->dataGen() && !link->disconnecting_()) link->abort();
    });
  }

  void connectFailed(bool transient) {
    auto e = ZeEXCEPT(Error, "Zutcp", ([transient](auto &s) {
      s << "native TCP/IP connect failed transient=" << transient;
    }));
    app()->error_(ZuMv(e));
  }



private:
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
    if (app()->nativeTx() && notify && !m_closeDrainPending && m_closeWaiters.count_()) {
      m_closeWaiters.push(CloseFn{ZuFwd<L>(l)});
      m_closeDrainNotify = true;
      return;
    }
    if (notify && Base::establishedRx() &&
	Endpoint::connected() && Endpoint::remote()) {
      closeAfterDisconnect(
	notify, peer, Endpoint::remote(), CloseFn{ZuFwd<L>(l)});
      return;
    }
    if (app()->nativeTx() && notify) {
      // A pending open cannot finish with FIN; cancel it on its current owners.
      abort_(ZuFwd<L>(l));
      return;
    }
    closeEndpoint(notify, peer, ZuFwd<L>(l));
  }

protected:
  void closeAfterDisconnect(
      bool notify, bool peer, ZiSockAddr addr, CloseFn fn) {
    if (app()->nativeTx()) {
      // Keep every completion on Rx before FIN work can finish on Tx.
      m_closeWaiters.push(ZuMv(fn));
      m_closeDrainNotify |= notify;
      Base::closeTxAdmission();
      app()->txRun([link = ZmRef(impl()), gen = Base::dataGen()] {
	if (gen != link->dataGen() || link->disconnecting_()) return;
	link->fin();
      });
      return;
    }
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

private:
  void closeExpired(bool drain = true) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client close expiry outside Tx thread", return);
    bool notify = m_closeNotify;
    bool peer = m_closePeer;
    CloseFn fn = ZuMv(m_closeFn);
    m_closeNotify = false;
    m_closePeer = false;
	    if (drain) {
	      ZutcpLOG(app()->qlogTrace(), ([
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
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client idle expiry outside Tx thread", return);
    Base::cancelTimers_();
	    ZutcpLOG(app()->qlogTrace(), ([linkInfo = Base::linkInfo_()](auto &o, ZuTime time) {
	      CloseEvt event{
		.linkInfo = linkInfo
	      ,
		.initiator = CloseInitiator::Local,
		.trigger = CloseTrigger::IdleTimeout,
		.reason = CloseReason::Idle,
		.connectionError = CloseError::NoError};
      o.logCxnClosed(event, time);
    }));
    if (app()->nativeTx()) {
      Base::expirePolicy_(CxnTimer::Idle);
      return;
    }
    m_closeNotify = true;
    m_closePeer = false;
    m_closeFn = CloseFn{[]() { }};
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
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "native close drain outside Rx", return);
    if (!app()->nativeTx()) {
      closeEndpointDrainedOnce_(notify, peer, ZuFwd<L>(l));
      return;
    }
    m_closeWaiters.push(CloseFn{ZuFwd<L>(l)});
    m_closeDrainNotify |= notify;
    if (m_closeDrainPending) return;
    uint64_t gen = Base::dataGen();
    if (m_closeDrainedGen == gen) {
      m_closeDrainNotify = false;
      completeCloseWaitersRx_(false);
      return;
    }
    m_closeDrainPending = true;
    closeEndpointDrainedOnce_(false, peer, [link = ZmRef(impl()), peer, gen] {
      link->app()->rxRun([link, peer, gen] {
        bool notify = link->m_closeDrainNotify && gen == link->dataGen();
        link->m_closeDrainedGen = gen;
	if (!link->deferCxnRelease()) link->releaseCxnSlotRx_();
	if (link->m_connectFailureRx) {
	  link->m_connectFailureRx = false;
	  link->connectFailed(link->m_connectTransientRx);
	}
        if (notify) link->disconnected(peer);
        // Reentrant connect/abort joins these waiters until notification returns.
        link->m_closeDrainNotify = link->m_closeDrainPending = false;
        link->completeCloseWaitersRx_(notify);
      });
    });
  }
  void completeCloseWaitersRx_(bool notify) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "native close completion outside Rx", return);
    auto fns = ZuMv(m_closeWaiters);
    m_closeWaiters.clean();
    app()->txRun([link = ZmRef(impl()), notify, fns = ZuMv(fns)]() mutable {
      if (notify) link->endpointDown();
      link = nullptr;
      while (fns.count_()) {
        auto fn = fns.shift();
        if (fn) fn();
      }
    });
  }
  template <typename L>
  void closeEndpointDrainedOnce_(bool notify, bool peer, L &&l) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (),
      "QUIC endpoint close drain outside Rx thread", return);
    m_ready = 0;
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
    m_connectFailedRx = m_connectFailureRx = false;
    m_endpointAttemptRx = false;
    m_peerParamsValidated = false;
    m_bootstrap = {};
  }







  bool startHandshake() {
    if (Base::handshakeStartedRx()) return true;
    if (!Base::initClientPath_(Endpoint::local(), Endpoint::remote(), Endpoint::payloadLimit()))
      return false;
    if (!initBootstrap() || !Base::startHandshake()) return false;
    app()->txRun([link = impl()] {
      if (link->disconnecting_()) return;
      PktBuild payload;
      int n = FrameCodec::writePing(payload.scratch(), payload.scratchAvail());
      if (n > 0 && payload.commitScratch(unsigned(n)))
        link->sendInitialPkt(payload, link->Endpoint::remote(), {}, nullptr, true);
    });
    return true;
  }



  bool initBootstrap() {
    if (!m_bootstrap.startRandom()) {
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.initialSCID(),
      m_bootstrap.initialDCID(),
      m_bootstrap.initialDCID(), m_bootstrap.initialDCID());
    ZutcpLOG(app()->qlogTrace(), ([
      local = Endpoint::local(),
      remote = Endpoint::remote(),
      linkInfo = Zutcp::LinkInfo{
	.vantage = Zutcp::Vantage::Client,
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
    return true;
  }

  void markEstablished_() {
    if (!Base::readyToEstablish())
      return;
    if (!m_peerParamsValidated) {
      if (!Base::validateServerParams_(m_bootstrap)) {
	app()->error_(ZeEXCEPT(Error, "Zutcp",
	  "server QUIC transport parameters failed validation"));
	connectFailed_0(false);
	return;
      }
      m_peerParamsValidated = true;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    impl()->connected(Connected{
      .version = Version1
    });
  }









  bool sendPersistProbe(PktBuild &build) {
    if (!Endpoint::connected()) return false;
    // No reliable control ref: the persist timer owns probe repetition.
    typename Base::TxPktRefs refs;
    return sendShortPkt(build, Endpoint::remote(), {}, &refs, false);
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client flush outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Endpoint::remote())
      return false;
    return flushTx_(Endpoint::remote());
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::linkClosedTx_()) return false;
    if (!Base::establishedTx()) return flushPendingAcks(addr);
    bool sent = Base::flushDataBatch_(
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











  bool sendPathBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client path send outside Tx thread", return false);
    return Base::sendPathPkt(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client path probe send outside Tx thread", return false);
    return Base::sendPathProbePkt(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn) {
	if (!app()->sendPkt(buf_)) return true;
	return Endpoint::send(ZuMv(buf_), ZuMv(addr_), ecn);
      });
  }

  bool sendShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn,
    unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
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
    ZiAssert(app()->txInvoked(), "Zutcp", (),
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
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    if (!Endpoint::connected()) return false;
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    if (ackEliciting) recordRefs = &refs;
    return Base::sendInitialPkt(
      CIDSel::Initial, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), true,
      payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return Endpoint::allocTxPkt(true); },
      [this](auto buf, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf), ZuMv(addr_));
      });
  }











  bool sendShortPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
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
    return Base::sendShortPkt_(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this, control = Base::txControlPacket(recordFrame, recordRefs)]() {
	return Endpoint::allocTxPkt(control);
      },
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
    return Base::sendShortPkt_(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this, control = Base::txControlPacket(recordFrame, recordRefs)]() {
	return Endpoint::allocTxPkt(control);
      },
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
    return Base::sendShortPkt_(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this, control = Base::txControlPacket({}, recordRefs)]() {
	return Endpoint::allocTxPkt(control);
      },
      [this](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(ZuMv(buf), ZuMv(addr_), ecn);
      });
  }

  bool sendCloseFrame(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC client close frame outside Tx thread", return false);
    if ((!closing && Base::linkClosedTx_()) ||
	(!Base::establishedTx() &&
	  !(closing && Base::closingTx_())) ||
	!Endpoint::connected() || !addr)
      return false;
    PktBuild build;
    if (!Base::appendPendingAck(PktNumSpace::AppData, build)) return false;
    bool appClose = Base::appCloseOnDisconnect();
    if (!Base::writeCloseFrame(build, appClose))
      return false;
    bool sent = sendShortPkt(build, ZuMv(addr), {});
    if (sent) {
      ZutcpLOG(app()->qlogTrace(), ([
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

  bool sendDataPkt(ZiSockAddr addr) {
    return Base::sendDataPkt(
      ZuMv(addr),
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
      ok = sendInitialPkt(build, ZuMv(addr), {}, nullptr, false);
      if (ok) sent = true;
      return ok;
    }
    ok = sendShortPkt(build, ZuMv(addr), {});
    if (ok) sent = true;
    return ok;
  }

  bool flushPendingAcks(ZiSockAddr addr) {
    bool sent = false;
    if (Base::pendingAck(PktNumSpace::Initial))
      (void)flushPendingAck(PktNumSpace::Initial, addr, sent);
    if (Base::pendingAck(PktNumSpace::AppData))
      (void)flushPendingAck(PktNumSpace::AppData, ZuMv(addr), sent);
    return sent;
  }

  void received_(Datagram d) {
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong(d_, packetOffset, packetLen);
      },
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort(d_, packetOffset, packetLen);
      });
  }



  bool receivedLong(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    bool ok = Base::receiveLongPkt(
      d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &) {
	Base::setPeerCIDFromHdrSCID(h);
	return true;
      },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZutcpLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      });
    if (ok) markEstablished_();
    return ok;
  }

  bool receivedShort(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveShortPkt(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZutcpLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      });
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, Base::RxAckMeta &ack,
    ZutcpLog_::PktEvt *qlog) {
    return Base::consumeFrames(
      level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
      [this](PktNumSpace::T level_, const Frame &frame, ZiSockAddr addr_) {
        return handleControlFrame(level_, frame, ZuMv(addr_));
      });
  }

  bool handleControlFrame(
    PktNumSpace::T level, const Frame &frame, ZiSockAddr addr) {
    switch (frame.type) {
      case FrameType::MaxData:
	queueTxFlush();
	return true;
      case FrameType::ConnectionClose:
      case FrameType::ApplicationClose:
	Base::transportClose_(frame.type, frame.errorCode);
	  ZutcpLOG(app()->qlogTrace(), ([
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
    Base::queueBlocked(maximum);
    impl()->flowBlocked(maximum);
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
    if (ep != this || (app()->nativeTx() && Base::abortingRx_())) return;
    m_notifyEndpointDown = true;
    ++m_readyCount;
    Base::endpointReady_();
    if (!startHandshake()) {
      connectFailed_0(false);
      return;
    }
    m_ready = 1;
  }
  void endpointDownRx_(Endpoint *ep) {
    if (ep != this) return;
    closeEndpointDrained_(m_notifyEndpointDown, true, CloseFn{});
  }
  void resetDisconnected() {
    closeEndpointDrained_(true, true, CloseFn{});
  }

  void connectFailed_0(bool transient, bool attempt = true) {
    Base::endpointFailure_();
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC client connect failure before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), transient, attempt]() {
      if (link->disconnecting_()) return link;
      if (!attempt || !link->app()->nativeTx()) {
	link->connectFailed(transient);
	return link;
      }
      if (link->m_connectFailedRx) return link;
      link->m_connectFailedRx = link->m_connectFailureRx = true;
      link->m_connectTransientRx = transient;
      link->abort_(); // Failure cleanup does not depend on an application override.
      return link;
    });
  }

  // shared
  ZmAtomic<uint64_t>	m_readyCount = 0;
  ZmAtomic<unsigned>	m_ready = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  Host			m_server;
  ZiIP			m_remote;
  ClientBootstrap	m_bootstrap;
  uint16_t		m_port = 0;
  bool			m_peerParamsValidated = false;
  bool			m_notifyEndpointDown = true;
  bool			m_connectFailedRx = false, m_connectFailureRx = false;
  bool			m_connectTransientRx = false;
  bool			m_endpointAttemptRx = false;
  bool			m_closeDrainPending = false, m_closeDrainNotify = false;
  uint64_t		m_closeDrainedGen = U64Null;
  using CloseWaiters = ZmQueue<CloseFn, ZmQueueHeapID<"Zutcp.CliCxn.CloseWaiters">>;
  CloseWaiters		m_closeWaiters;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  CloseFn		m_closeFn;
  bool			m_closeNotify = false;
  bool			m_closePeer = false;
};

} // namespace Zutcp
