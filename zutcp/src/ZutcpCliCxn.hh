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
  enum { EndpointRef = true };
  using Base::Base;
  using Base::app;
  using Base::impl;
  bool applicationReadyRx() const { return Base::establishedRx(); }
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
      "TCP client connect before app initialization", return);
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
      "TCP client disconnect before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->disconnect_();
      return link;
    });
  }
  bool disconnect(uint64_t errorCode) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client disconnect before app initialization", return false);
    bool accepted = !Base::closed();
    app()->rxInvoke(impl(), [link = impl(), errorCode]() mutable {
      link->disconnectRx(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "TCP disconnect outside Rx thread", return);
    if (Base::disconnecting()) return;
    if (errorCode) {
      Base::abortLocalRx(errorCode, [link = impl()] { link->tcpResetAbort(false); }, true);
      return;
    }
    if (!Base::establishedRx() || !Endpoint::connected() || !Endpoint::remote()) {
      Base::abortLocalRx(0, [link = impl()] { link->tcpResetAbort(false); });
      return;
    }
    Base::closeTxAdmission();
    app()->txRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting()) link->fin();
    });
  }
  template <typename L, typename = ZuIfT<ZuIsConstructible<L, CloseFn>{}>>
  void disconnect(L &&l) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client disconnect before app initialization", return);
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
  void nativeFinished() {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native finish outside Tx", return);
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting()) return;
      link->closeEndpointDrained(true, false, CloseFn{});
    });
  }

  void abort() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client abort before app initialization", return);
    app()->rxInvoke(impl(), [link = impl()]() {
      link->abort_();
      return link;
    });
  }
  template <typename L>
  void abort(L &&l) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client abort before app initialization", return);
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
      if (m_closeDrainPending || m_closeDrainedGen == Base::dataGen() || Base::disconnecting()) {
	closeEndpointDrained(true, ZuFwd<L>(l));
	return;
      }
      Base::abortLocalRx(0, [link = impl(), l = ZuFwd<L>(l)]() mutable {
	link->closeEndpointDrained(true, ZuMv(l));
      });
    } else {
      Base::closeState();
      closeEndpointDrained(true, ZuFwd<L>(l));
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


  bool send(ZuBSpan payload, bool fin = true) {
    if (Base::disconnecting() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (app()->txInvoked()) return send_(payload, fin);
    auto tx = this->writer();
    auto gen = tx.gen();
    tx << payload;
    if (!tx.flush()) return false;
    app()->txInvoke([cxn = impl(), fin, gen]() {
      if (gen != cxn->dataGen() || cxn->disconnecting()) return;
      if (fin) cxn->fin();
      cxn->queueTxFlush();
    });
    return true;
  }
  bool send(ZmRef<ZiIOBuf> payload, bool fin = true) {
    if (Base::disconnecting() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (app()->txInvoked()) return send_(ZuMv(payload), fin);
    if (app()->nativeTx()) {
      auto gen = this->dataGen();
      if (payload && payload->length && !this->enqueue(ZuMv(payload), gen)) return false;
      app()->txInvoke([cxn = impl(), fin, gen] {
	if (gen != cxn->dataGen() || cxn->disconnecting()) return;
	if (fin) cxn->fin();
	cxn->queueTxFlush();
      });
      return true;
    }
    app()->txInvoke([
      cxn = impl(), payload = ZuMv(payload), fin, gen = this->dataGen()
    ]() mutable {
      if (gen == cxn->dataGen() && !cxn->disconnecting())
	cxn->send_(ZuMv(payload), fin);
    });
    return true;
  }
  bool send_(ZmRef<ZiIOBuf> payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "retained send outside Tx thread", return false);
    if (Base::linkClosedTx() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (payload && payload->length && !this->enqueue_(ZuMv(payload))) return false;
    if (fin) this->fin();
    queueTxFlush();
    return true;
  }
  bool send_(ZuBSpan payload, bool fin = true) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "send outside Tx thread", return false);
    if (Base::linkClosedTx() || Base::txAdmissionClosed() || (!payload && !fin)) return false;
    if (payload) {
      auto tx = this->writer_();
      tx << payload;
      if (!tx.flush()) return false;
    }
    if (fin) this->fin();
    queueTxFlush();
    return true;
  }

  void pto() { Base::synTimeout(); }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->retransmit();
    });
  }

  void heartBeatExpired() { sendPersistProbe(); }

  void connect_0() { // direct call from within rx thread
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client connect before app initialization", return);
    ZiIP ip = m_remote ? m_remote : ZiIP{m_server};
    if (!ip || !m_port) {
      app()->reportError(ZeEXCEPT(Error, "Zutcp",
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
  void connectReserved(ZiIP ip, uint16_t port, CxnSlot slot) {
    m_remote = ip;
    m_port = port;
    connect_1(ZuMv(ip), port, ZuMv(slot));
  }

public:
  void connect_1(ZiIP ip, uint16_t port, CxnSlot slot = {}) {
    resetRuntimeState();
    Base::resetDiag();
    if (!ip || !port) {
      Base::adoptCxnSlotRx(ZuMv(slot));
      connectFailed_0(true);
      return;
    }
    if (auto stack = app()->nativeStack()) {
      if (!slot) slot = stack->reserveCxn();
      if (!slot) { connectFailed_0(false, false); return; }
    }
    Base::adoptCxnSlotRx(ZuMv(slot));
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

  bool sendTCPReset() { return sendTCPRange({}, false, Base::tcpResetFlags()); }
  void tcpResetAbort(bool peer, bool established = true) {
    app()->rxRun([link = ZmRef(impl()), peer, established, gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting()) return;
      if (peer && !established) {
	link->connectFailed_0(false);
	return;
      }
      link->closeEndpointDrained(true, peer, CloseFn{});
    });
  }

  void tcpBudgetFailedRx() {
    if (!Base::establishedRx()) connectFailed_0(false);
    else Base::tcpBudgetFailedRx();
  }

  void policyExpired(CxnTimer::T) {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting()) link->abort();
    });
  }

  void connectExpired() {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting() ||
	  link->establishedRx() || link->linkClosedRx()) return;
      link->connectFailed_0(true);
      if (gen == link->dataGen() && !link->disconnecting()) link->abort();
    });
  }

  void connectFailed(bool transient) {
    auto e = ZeEXCEPT(Error, "Zutcp", ([transient](auto &s) {
      s << "native TCP/IP connect failed transient=" << transient;
    }));
    app()->reportError(ZuMv(e));
  }

public:
  bool sendTCPRange(const SentFrameRef &ref, bool syn = false, uint8_t control = 0,
      bool persist = false) {
    return Base::sendTCP(ref, syn, control,
      [this, &ref] { return Endpoint::allocTxPkt(!bool(ref.length)); },
      [this](auto buf, EcnMark::T ecn) {
	if (!app()->sendPkt(buf)) return false;
	return Endpoint::send(ZuMv(buf), Endpoint::remote(), ecn, true);
      }, persist);
  }
  bool sendSyn() {
    TxRange range;
    SentFrameRef ref;
    if (Base::earlyRange(range)) ref = SentFrameRef::data(range, false);
    return sendTCPRange(ref, true);
  }
  bool sendPersistProbe() { return sendTCPRange({}, false, 0, true); }
  bool sendTLPNew() {
    TxRange range;
    return Base::txRange(0, range) && sendTCPRange(SentFrameRef::data(range, false));
  }
  bool sendTLPRetx(SentFrameRef &ref) { return sendTCPRange(ref); }
  bool retransmit(bool = false, unsigned budget = 2) {
    bool sent = false;
    for (unsigned i = 0; i < budget; ++i) {
      SentFrameRef ref;
      if (!Base::nextRetransmit(ref)) break;
      bool accepted = Base::sendRecovery(ref, [this, &ref] { return sendTCPRange(ref); });
      if (!accepted) { Base::requeueRetransmit(ref); break; }
      sent = true;
    }
    return sent;
  }

private:

  void closeCurrent(bool notify) {
    closeCurrent(notify, false, CloseFn{});
  }

  template <typename L>
  void closeCurrent(bool notify, L &&l) {
    closeCurrent(notify, false, ZuFwd<L>(l));
  }

  template <typename L>
  void closeCurrent(bool notify, bool peer, L &&l) {
    if (notify && !m_closeDrainPending && m_closeWaiters.count_()) {
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
    if (notify) {
      // A pending open cannot finish with FIN; cancel it on its current owners.
      abort_(ZuFwd<L>(l));
      return;
    }
    closeEndpoint(notify, peer, ZuFwd<L>(l));
  }

protected:
  void closeAfterDisconnect(bool notify, bool, ZiSockAddr, CloseFn fn) {
    m_closeWaiters.push(ZuMv(fn));
    m_closeDrainNotify |= notify;
    Base::closeTxAdmission();
    app()->txRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting()) link->fin();
    });
  }

private:
  void idleExpired() {
    Base::cancelTimers_();
    Base::expirePolicy(CxnTimer::Idle);
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
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "native close drain outside Rx", return);
    if (!app()->nativeTx()) {
      closeEndpointDrainedOnce(notify, peer, ZuFwd<L>(l));
      return;
    }
    m_closeWaiters.push(CloseFn{ZuFwd<L>(l)});
    m_closeDrainNotify |= notify;
    if (m_closeDrainPending) return;
    uint64_t gen = Base::dataGen();
    if (m_closeDrainedGen == gen) {
      m_closeDrainNotify = false;
      completeCloseWaitersRx(false);
      return;
    }
    m_closeDrainPending = true;
    closeEndpointDrainedOnce(false, peer, [link = ZmRef(impl()), peer, gen] {
      link->app()->rxRun([link, peer, gen] {
        bool notify = link->m_closeDrainNotify && gen == link->dataGen();
        link->m_closeDrainedGen = gen;
	if (!link->deferCxnRelease()) link->releaseCxnSlotRx();
	if (link->m_connectFailureRx) {
	  link->m_connectFailureRx = false;
	  link->connectFailed(link->m_connectTransientRx);
	}
        if (notify) link->disconnected(peer);
        // Reentrant connect/abort joins these waiters until notification returns.
        link->m_closeDrainNotify = link->m_closeDrainPending = false;
        link->completeCloseWaitersRx(notify);
      });
    });
  }
  void completeCloseWaitersRx(bool notify) {
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
  void closeEndpointDrainedOnce(bool notify, bool peer, L &&l) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (),
      "TCP endpoint close drain outside Rx thread", return);
    m_ready = 0;
    m_notifyEndpointDown = false;
    if (Base::linkClosedRx()) Base::clearCallbacks();
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
    Base::resetLink();
    m_connectFailedRx = m_connectFailureRx = false;
    m_endpointAttemptRx = false;
  }

  bool startHandshake() {
    if (Base::handshakeStartedRx()) return true;
    if (!Base::initClientPath(Endpoint::local(), Endpoint::remote(), Endpoint::payloadLimit()))
      return false;
    Base::configLocalParams(app());
    if (!Base::startHandshake()) return false;
    Base::prepareClientInitial([link = impl()] { link->sendSyn(); });
    return true;
  }

  void markEstablished() {
    if (Base::disconnecting() || Base::abortingRx() ||
	!Base::readyToEstablish() || !Base::prepareMPSRx()) return;
    Base::establish();
    Base::validatePath();
    uint64_t gen = Base::dataGen();
    impl()->connected(Connected{});
    Base::resumeApplicationRx(gen);
    Base::finishTFO();
  }

  bool flushTx() {
    if (!Endpoint::connected() || !Endpoint::remote() || Base::linkClosedTx()) return false;
    return Base::flushTCP([this](
	const SentFrameRef &ref, bool syn, uint8_t control, bool persist) {
      return sendTCPRange(ref, syn, control, persist);
    });
  }
  bool flushTx(ZiSockAddr) { return flushTx(); }

  void queueTxFlush() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->flushTx();
    });
  }
  void queueTxFlush(ZiSockAddr addr) {
    app()->txRun([
      link = impl(),
      addr = ZuMv(addr)
    ]() mutable {
      if (link->disconnecting()) return;
      link->flushTx(ZuMv(addr));
    });
  }

  void received_(Datagram d) {
    if (d.addr != Endpoint::remote() || d.local != Endpoint::local()) return;
    if (!Base::acceptsLiveTCP(d))
      if (auto stack = app()->nativeStack())
	if (stack->recoverClosed(d)) return;
    if (Base::receiveSegment(ZuMv(d))) markEstablished();
    else Base::packetParseFailure();
  }

public:
  void endpointDatagram(Datagram d) {
    if (Base::disconnecting()) return;
    app()->rxInvoke(impl(), [link = impl(), d = ZuMv(d)]() mutable {
      if (!link->disconnecting()) link->received_(ZuMv(d));
      return link;
    });
  }
  void endpointReady(Endpoint *ep) {
    if (Base::disconnecting()) return;
    app()->rxInvoke(impl(), [link = impl(), ep]() mutable {
      if (!link->disconnecting()) link->endpointReadyRx(ep);
      return link;
    });
  }

  void endpointFailed(bool transient) {
    if (Base::disconnecting()) return;
    app()->rxInvoke(impl(), [link = impl(), transient]() mutable {
      if (!link->disconnecting()) link->connectFailed_0(transient);
      return link;
    });
  }
  void endpointDown_(Endpoint *ep) {
    if (Base::disconnecting()) return;
    app()->rxInvoke(impl(), [link = impl(), ep]() mutable {
      if (!link->disconnecting()) link->endpointDownRx(ep);
      return link;
    });
  }
private:
  void endpointReadyRx(Endpoint *ep) {
    if (ep != this || (app()->nativeTx() && Base::abortingRx())) return;
    m_notifyEndpointDown = true;
    ++m_readyCount;
    Base::endpointReady();
    if (!startHandshake()) {
      connectFailed_0(false);
      return;
    }
    m_ready = 1;
  }
  void endpointDownRx(Endpoint *ep) {
    if (ep != this) return;
    closeEndpointDrained(m_notifyEndpointDown, true, CloseFn{});
  }
  void resetDisconnected() {
    closeEndpointDrained(true, true, CloseFn{});
  }

  void connectFailed_0(bool transient, bool attempt = true) {
    Base::endpointFailure();
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP client connect failure before app initialization", return);
    app()->rxInvoke(impl(), [link = impl(), transient, attempt]() {
      if (link->disconnecting()) return link;
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
  uint16_t		m_port = 0;
  bool			m_notifyEndpointDown = true;
  bool			m_connectFailedRx = false, m_connectFailureRx = false;
  bool			m_connectTransientRx = false;
  bool			m_endpointAttemptRx = false;
  bool			m_closeDrainPending = false, m_closeDrainNotify = false;
  uint64_t		m_closeDrainedGen = U64Null;
  using CloseWaiters = ZmQueue<CloseFn, ZmQueueHeapID<"Zutcp.CliCxn.CloseWaiters">>;
  CloseWaiters		m_closeWaiters;

};

} // namespace Zutcp
