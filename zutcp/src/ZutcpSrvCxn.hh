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
  static constexpr unsigned PNLength = 2;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;

  template <typename, typename> friend class Server;

  SrvCxn(App *app) : Base{app, true} { }

  // Installed from the cold first-Initial lookup before response work is posted.
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
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }

  ZiSockAddr peer() const {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server peer access outside Tx thread", return {});
    return m_peerAddr;
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

  bool sendTCPReset_() {
    if (!m_peerAddr) return false;
    return Base::sendTCPResetPkt_(
      [this]() { return app()->allocTxPkt_(true); },
      [this](auto buf) {
	return sendShortBuf(ZuMv(buf), m_peerAddr, nullptr, EcnMark::NotECT);
      });
  }
  void tcpResetAbort_(bool peer) {
    app()->rxRun([link = ZmRef(impl()), peer] {
      if (!link->disconnecting_()) static_cast<Base *>(link.ptr())->disconnect(peer);
    });
  }

  void policyExpired(CxnTimer::T) {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen == link->dataGen() && !link->disconnecting_())
	static_cast<Base *>(link.ptr())->disconnect(false);
    });
  }

  void connectExpired() {
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_() ||
	  link->establishedRx() || link->linkClosedRx_()) return;
      if (link->app()->nativeTx())
	link->abortLocalRx_(0, [link = link.ptr()] { link->tcpResetAbort_(false); });
      else link->disconnect(false);
    });
  }

  void pto_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server PTO outside Tx thread", return);
    if (Base::linkClosedTx_() || !m_peerAddr)
      return;
    Base::ptoRecovery(
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC server retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->retransmit_();
    });
  }

  bool retransmit_(
    bool pto = false, unsigned maxPackets = 2,
    PktNumSpace::T onlySpace = PktNumSpace::N) { // Tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !m_peerAddr)
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
      if (!sendShortPkt(build, m_peerAddr, {}, &ref, true)) {
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
	  build, m_peerAddr, {}, nullptr, true);
      default:
	return sendShortPkt(
	  build, m_peerAddr, {}, &refs, true);
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
  bool disconnect(uint64_t errorCode = 0) {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "QUIC server disconnect before app initialization", return false);
    bool accepted = !Base::closed();
    app()->rxInvoke(impl(), [link = impl(), errorCode]() mutable {
      link->disconnectRx_(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx_(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zutcp", (),
      "QUIC server disconnect outside Rx thread", return);
    if (app()->nativeTx()) {
      if (Base::disconnecting_()) return;
      if (errorCode) {
	Base::abortLocalRx_(errorCode, [link = impl()] { link->tcpResetAbort_(false); }, true);
	return;
      }
      if (!Base::establishedRx()) {
	Base::abortLocalRx_(0, [link = impl()] { link->tcpResetAbort_(false); });
	return;
      }
      Base::closeTxAdmission();
      app()->txRun([link = ZmRef(impl()), gen = Base::dataGen()] {
	if (gen == link->dataGen() && !link->disconnecting_()) link->fin();
      });
      return;
    }
    if (!Base::linkClosedRx_()) Base::closeState(errorCode, true);
    if (Base::establishedRx()) {
      app()->txRun([link = impl()]() mutable {
	if (link->disconnecting_()) return;
	if (ZiSockAddr addr = link->m_peerAddr) {
	  (void)link->sendCloseFrame(ZuMv(addr), true);
	  link->m_closePeer = false;
	  link->enterLocalClosingTx();
	} else
	  static_cast<Base *>(link)->disconnect(false);
      });
      return;
    }
    (void)Base::disconnect(false);
  }

  void nativeFinished_() {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "native finish outside Tx", return);
    app()->rxRun([link = ZmRef(impl()), gen = Base::dataGen()] {
      if (gen != link->dataGen() || link->disconnecting_()) return;
      static_cast<Base *>(link.ptr())->disconnect(false);
    });
  }

  void closeExpired() {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server close expiry outside Tx thread", return);
    bool peer = m_closePeer;
    m_closePeer = false;
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
    Base::disconnect(peer);
  }
  void peerDrainingTx() {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server peer-draining handoff outside Tx thread", return);
    m_closePeer = true;
  }
  void idleExpired() {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server idle expiry outside Tx thread", return);
	    Base::cancelTimers_();
	    m_closePeer = false;
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
    if (app()->nativeTx()) Base::expirePolicy_(CxnTimer::Idle);
    else Base::disconnect(false);
  }

private:
  using CIDSel = Base::CIDSel;



  void resetRuntimeState() {
    Base::resetLink_();
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->m_peerAddr.null();
      return link;
    });
    m_bootstrap = {};
    m_rxPeerAddr = {};
  }



  bool initBootstrap(const LongHdr &h, unsigned datagramLen) {
    if (h.type != PktType::Initial ||
	!m_bootstrap.acceptInitial(h, datagramLen)) {
      Base::packetParseFailure();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.localInitialSCID(),
      m_bootstrap.clientInitialSCID(),
      m_bootstrap.origDCID(), m_bootstrap.origDCID());
    if (!Base::loadSrvParams(m_bootstrap)) return false;
    Base::configLocalParams(app());
    return Base::startHandshake();
  }

  void initialResponseAckdTx(uint64_t gen) {
    ZiAssert(app()->txInvoked(), "Zutcp", (), "setup ACK evidence outside Tx", return);
    if (gen != Base::dataGen() || m_responseAckPostedGen == gen) return;
    m_responseAckPostedGen = gen;
    app()->rxRun([link = impl(), gen] {
      if (link->disconnecting_() || gen != link->dataGen()) return;
      link->m_responseAckGen = gen;
      link->markEstablished_();
    });
  }

  void markEstablished_() {
    if (m_responseAckGen != Base::dataGen() || !Base::readyToEstablish())
      return;
    if (!Base::validateClientParams()) {
        app()->error_(ZeEXCEPT(Error, "Zutcp",
	"client QUIC transport parameters failed validation"));
      return;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    // Resume replies staged by valid input alongside the Initial ACK. Post
    // before notifying the application, whose callback may begin teardown.
    queueTxFlush();
    impl()->connected(Connected{
      .version = Version1
    });
  }









  bool sendPktPath(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool &sent) {
    sent = false;
    if (!app()->sendPkt(buf)) return true;
    sent = app()->sendPktRaw(ZuMv(buf), ZuMv(addr), ecn, false, m_txIP);
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





  bool sendPersistProbe(PktBuild &build) {
    if (!m_peerAddr) return false;
    // No reliable control ref: the persist timer owns probe repetition.
    typename Base::TxPktRefs refs;
    return sendShortPkt(build, m_peerAddr, {}, &refs, false);
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server flush outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Base::establishedTx() || !m_peerAddr)
      return false;
    return flushTx_(m_peerAddr);
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::linkClosedTx_()) return false;
    if (!Base::establishedTx()) {
      return flushPendingAcks(addr);
    }
    // Ingress ACK snapshots can precede candidate CID selection. Ordinary
    // output follows the active tuple; candidate probes use their own route.
    addr = m_peerAddr;
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



  bool appendPendingAck(PktNumSpace::T level, PktBuild &build) {
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

  bool sendFrameRefs(const Base::TxPktRefs *recordRefs) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server frame gating outside Tx thread", return false);
    if (!recordRefs) return true;
    for (unsigned i = 0, n = recordRefs->count(); i < n; ++i)
      if (!app()->sendFrame((*recordRefs)[i])) return false;
    return true;
  }

  bool sendPathBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server path send outside Tx thread", return false);
    return Base::sendPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server path probe send outside Tx thread", return false);
    return Base::sendPathProbePktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs,
    EcnMark::T ecn, unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server Short buffer send outside Tx thread", return false);
    if (!sendFrameRefs(recordRefs)) return false;
    if (pmtudSize) return sendPathProbeBuf(ZuMv(buf), ZuMv(addr));
    return Base::sendPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn_, sent);
      },
      ecn);
  }
  bool sendStrictShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs, EcnMark::T ecn) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server strict Short buffer send outside Tx thread", return false);
    if (!sendFrameRefs(recordRefs)) return false;
    return Base::sendValidPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = false;
	sent = app()->sendPktRaw(ZuMv(buf_), ZuMv(addr_), ecn_, true, m_txIP);
	return sent;
      },
      ecn);
  }

  bool sendInitialPkt(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    if (ackEliciting) recordRefs = &refs;
    return Base::sendInitialPkt(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), false, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(true); },
      [this, recordRefs](auto buf, ZiSockAddr addr_) {
	if (!sendFrameRefs(recordRefs)) return false;
	return sendPathBuf(ZuMv(buf), ZuMv(addr_));
      });
  }









  bool sendShortPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendShortPkt(build, ZuMv(addr), payload);
  }

  bool sendShortPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    unsigned pmtudSize = 0) {
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
    return Base::sendShortPkt_(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this, control = Base::txControlPacket(recordFrame, recordRefs)]() {
	return app()->allocTxPkt_(control);
      },
      [this, recordRefs, pmtudSize](
	  auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn, pmtudSize);
      },
      pmtudSize);
  }

  bool sendShortPkt(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    ZuBSpan recordFrame, const Base::TxPktRefs *recordRefs,
    bool ackEliciting, unsigned pmtudSize = 0) {
    return Base::sendShortPkt_(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this, control = Base::txControlPacket(recordFrame, recordRefs)]() {
	return app()->allocTxPkt_(control);
      },
      [this, recordRefs, pmtudSize](
	  auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn, pmtudSize);
      },
      pmtudSize);
  }
  bool sendStrictShortPkt(
    PktBuild &payload, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs) {
    return Base::sendShortPkt_(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this, control = Base::txControlPacket({}, recordRefs)]() {
	return app()->allocTxPkt_(control);
      },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }
  bool sendStrictShortPkt(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs) {
    return Base::sendShortPkt_(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this, control = Base::txControlPacket({}, recordRefs)]() {
	return app()->allocTxPkt_(control);
      },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }

  bool sendCloseFrame(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zutcp", (),
      "QUIC server close frame outside Tx thread", return false);
    if ((!closing && Base::linkClosedTx_()) ||
	(!Base::establishedTx() &&
	  !(closing && Base::closingTx_())) ||
	!m_peerAddr || !addr)
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

  bool received_(Datagram d, const CxnID *routedDCID = nullptr) {
    if (m_rxPeerAddr && d.addr != m_rxPeerAddr) return true;
    if (!Base::handshakeStartedRx() && d.buf)
      Base::initServerPath_(d.local, d.addr, m_rxPayloadLimit);
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong(d_, packetOffset, packetLen);
      },
      [this, routedDCID](
	  Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort(d_, packetOffset, packetLen, routedDCID);
      });
    return !Base::disconnecting_() && (!app()->nativeTx() || Base::handshakeStartedRx());
  }

  bool receivedLong(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    bool ok = Base::receiveLongPkt(
      d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &d_) {
	if (!Base::handshakeStartedRx()) {
	  ZiSockAddr peerAddr = d_.addr;
	  app()->txInvoke(impl(), [link = impl(), peerAddr]() mutable {
	    link->m_peerAddr = ZuMv(peerAddr);
	    return link;
	  });
	  if (!initBootstrap(h, d_.buf->length)) return false;
	m_rxPeerAddr = d_.addr;
	  app()->refreshLinkRoutes(impl());
	}
	return true;
      },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZutcpLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      });
    InitialPkt initial;
    if (ok && initial.parse({d.buf->data() + packetOffset, packetLen}) == int(packetLen) &&
        initial.params) {
      app()->txRun([link = impl()] {
        if (link->disconnecting_()) return;
        PktBuild payload;
        int n = FrameCodec::writePing(payload.scratch(), payload.scratchAvail());
        if (n > 0 && payload.commitScratch(unsigned(n)))
          link->sendInitialPkt(payload, link->m_peerAddr, {}, nullptr, true);
      });
    }

    if (ok) markEstablished_();
    return ok;
  }

  bool receivedShort(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    const CxnID *routedDCID = nullptr) {
    return Base::receiveShortPkt(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZutcpLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      },
      routedDCID);
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





protected:
  // Rx-owned routed packet entry.
  bool receivedRouted(Datagram d, const CxnID &routedDCID) {
    return received_(ZuMv(d), &routedDCID);
  }

private:
  void installRoutes_(CxnRouter<Impl> &routes) {
    routes.add(m_bootstrap.origDCID(), impl());
    Base::installLocalCIDRoutes_(routes);
  }

  void retireRoutes_(CxnRouter<Impl> &routes) {
    routes.tombstone(m_bootstrap.origDCID());
    Base::tombstoneLocalCIDRoutes_(routes);
  }



  // shared

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ServerBootstrap	m_bootstrap;
  ZiSockAddr		m_rxPeerAddr;
  unsigned		m_rxPayloadLimit = BufSize;
  uint64_t		m_responseAckGen = U64Null;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiSockAddr		m_peerAddr;
  IPPath		m_txIP{};
  uint64_t		m_responseAckPostedGen = U64Null;
  bool			m_closePeer = false;
};

} // namespace Zutcp
