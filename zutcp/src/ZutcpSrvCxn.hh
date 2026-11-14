//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Server connection API implementation

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

namespace Zutcp {

template <
  typename App, typename Impl,
  typename TxBufAlloc_ = TxBufAlloc<>>
class SrvCxn :
  public Connection<App, Impl, TxBufAlloc_> {
public:
  using Base = Connection<App, Impl, TxBufAlloc_>;
  using Base::Base;
  using Base::app;
  using Base::impl;
  bool applicationReadyRx() const { return Base::establishedRx(); }
  friend Base;

  template <typename, typename> friend class Server;

  SrvCxn(App *app) : Base{app, true} { }

  // Installed from the cold SYN route lookup before response work is posted.
  void initIPPath(IPPath path) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (), "peer route setup outside Rx", return);
    m_rxPayloadLimit = tcpPayload(path.mtu, path.local.type());
    app()->txRun([link = impl(), path] { link->m_txIP = path; });
  }

  bool established() const { return Base::established(); }
  template <typename L>
  void diag(L &&l) const { Base::diag(ZuFwd<L>(l)); }
  template <typename L>
  void pathDiag(L &&l) const { Base::pathDiag(ZuFwd<L>(l)); }

  unsigned activePathMaxPayload() const { return Base::activePathMaxPayload_(); }

  ZiSockAddr peer() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "TCP server peer access outside Tx thread", return {});
    return m_peerAddr;
  }

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

  bool sendTCPReset() { return sendTCPRange({}, false, Base::tcpResetFlags()); }
  void tcpResetAbort(bool peer, bool = true) {
    app()->rxRun([link = ZmRef(impl()), peer] {
      if (!link->disconnecting()) static_cast<Base *>(link.ptr())->disconnect(peer);
    });
  }

  void policyExpired(CxnTimer::T) {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting())
	static_cast<Base *>(link.ptr())->disconnect(false);
    });
  }

  void connectExpired() {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting() ||
	  link->establishedRx() || link->linkClosedRx()) return;
      if (link->app()->nativeTx())
	link->abortLocalRx(0, [link = link.ptr()] { link->tcpResetAbort(false); });
      else link->disconnect(false);
    });
  }

  void pto() { Base::synTimeout(); }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP server retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->retransmit();
    });
  }

  void heartBeatExpired() { sendPersistProbe(); }

  bool disconnect(uint64_t errorCode = 0) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP server disconnect before app initialization", return false);
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
    if (!Base::establishedRx()) {
      Base::abortLocalRx(0, [link = impl()] { link->tcpResetAbort(false); });
      return;
    }
    Base::closeTxAdmission();
    app()->txRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting()) link->fin();
    });
  }

  void nativeFinished() {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native finish outside Tx", return);
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting()) return;
      static_cast<Base *>(link.ptr())->disconnect(false);
    });
  }

  void idleExpired() {
    Base::cancelTimers_();
    Base::expirePolicy(CxnTimer::Idle);
  }

public:
  bool sendTCPRange(const SentFrameRef &ref, bool syn = false, uint8_t control = 0,
      bool persist = false) {
    return Base::sendTCP(ref, syn, control,
      [this, &ref] { return app()->allocTxPkt_(!bool(ref.length)); },
      [this](auto buf, EcnMark::T ecn) {
	if (!app()->sendPkt(buf)) return false;
	return app()->sendPktRaw(ZuMv(buf), m_peerAddr, ecn, true, m_txIP);
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

  void resetRuntimeState() {
    Base::resetLink();
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->m_peerAddr.null();
      return link;
    });
    m_rxPeerAddr = {};
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

  bool flushTx() {
    if (!m_peerAddr || Base::linkClosedTx()) return false;
    return Base::flushTCP([this](
	const SentFrameRef &ref, bool syn, uint8_t control, bool persist) {
      return sendTCPRange(ref, syn, control, persist);
    });
  }
  bool flushTx(ZiSockAddr) { return flushTx(); }

  bool received_(Datagram d) {
    if (m_rxPeerAddr && d.addr != m_rxPeerAddr) return true;
    if (!Base::handshakeStartedRx()) {
      m_rxPeerAddr = d.addr;
      Base::initServerPath(d.local, d.addr, m_rxPayloadLimit);
      app()->txRun([link = impl(), peer = d.addr, gen = Base::dataGen()] {
	if (gen == link->dataGen() && !link->disconnecting()) link->m_peerAddr = peer;
      });
    }
    if (!Base::acceptsLiveTCP(d))
      if (auto stack = app()->nativeStack())
	if (stack->recoverClosed(d)) return true;
    bool received = Base::receiveSegment(ZuMv(d));
    if (received) markEstablished();
    return received;
  }

protected:
  // Rx-owned routed packet entry.
  bool receivedRouted(Datagram d) { return received_(ZuMv(d)); }

private:

  // shared

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiSockAddr		m_rxPeerAddr;
  unsigned		m_rxPayloadLimit = BufSize;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiSockAddr		m_peerAddr;
  IPPath		m_txIP{};
};

} // namespace Zutcp
