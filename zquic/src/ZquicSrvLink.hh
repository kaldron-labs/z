//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC server link API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic {

template <
  typename App, typename Impl, typename Stream_,
  typename TxBufAlloc_ = StreamTxBufAlloc<>>
class SrvLink :
  public Link<App, Impl, TxBufAlloc_, Stream_> {
public:
  using Base = Link<App, Impl, TxBufAlloc_, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  static constexpr unsigned TLSBufSize = (64<<10); // 64K
  static constexpr unsigned PNLength = 2;
  static constexpr unsigned CryptoChunk = 900;
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  template <typename, typename, typename> friend class Zquic::Stream;

  template <typename, typename> friend class Server;

  SrvLink(App *app) : Base{app, true} { Base::initCryptoDelivery(); }

  bool established() const { return Base::established(); }
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
    auto link = const_cast<SrvLink *>(this)->impl();
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
    auto link = const_cast<SrvLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->migrationResult_());
    });
  }
  bool pathValidated() const { return Base::pathValidated_(); }
  unsigned activePathMaxUDP() const { return Base::activePathMaxUDP_(); }
  uint64_t pathAntiAmp() const {
    return Base::pathAntiAmp_();
  }
  const Crypto &crypto() const { return Base::crypto_(); }
  ZiSockAddr peer() const {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server peer access outside Tx thread", return {});
    return m_peerAddr;
  }

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
      "QUIC server send_ outside Tx thread", return false);
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
      "QUIC server PTO outside Tx thread", return);
    if (Base::linkClosedTx_() || !m_peerAddr)
      return;
    Base::ptoRecovery(
      [this](
	  PktNumSpace::T level, PktBuild &build,
	  const SentFrameRef &cryptoRef) {
	return level == PktNumSpace::Initial ?
	  sendInitialPkt(build, m_peerAddr, {}, &cryptoRef, true, false) :
	  sendHandshakePkt(build, m_peerAddr, {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  void queueRetransmit() {
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC server retransmit before app initialization", return);
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting_()) return;
      link->retransmit_();
    });
  }

  bool retransmit_(bool pto = false) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !m_peerAddr)
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
	    sendInitialPkt(build, m_peerAddr, {}, &ref, true, false) :
	  level == PktNumSpace::Handshake ?
	    sendHandshakePkt(build, m_peerAddr, {}, &ref, true, false) :
	    sendShortPkt(build, m_peerAddr, {}, &ref, true);
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
      if (ref.kind == SentFrameKind::Control &&
	  ref.controlType == FrameType::PathChallenge) {
	// Validation recovery belongs to the candidate tuple and peer CID.
	if (!sendPassiveMigChal()) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
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
      if (!sendShortPkt(build, m_peerAddr, {}, &ref, true)) {
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
	  build, m_peerAddr, {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt(
	  build, m_peerAddr, {}, nullptr, true, false);
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
    ZiAssert(app() && app()->mx(), "Zquic", (),
      "QUIC server disconnect before app initialization", return false);
    bool accepted = !Base::closed();
    app()->rxInvoke(impl(), [link = impl(), errorCode]() mutable {
      link->disconnectRx_(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx_(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC server disconnect outside Rx thread", return);
    if (!Base::linkClosedRx_()) Base::closeState(errorCode, true);
    if (Base::establishedRx()) {
      app()->txRun([link = impl()]() mutable {
	if (link->disconnecting_()) return;
	if (ZiSockAddr addr = link->m_peerAddr) {
	  (void)link->sendCloseFrame(ZuMv(addr), true);
	  link->m_closePeer = false;
	  link->enterLocalClosingTx();
	} else
	  link->Base::disconnect(false);
      });
      return;
    }
    (void)Base::disconnect(false);
  }

  void closeExpired() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server close expiry outside Tx thread", return);
    bool peer = m_closePeer;
    m_closePeer = false;
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
    Base::disconnect(peer);
  }
  void peerDrainingTx() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server peer-draining handoff outside Tx thread", return);
    m_closePeer = true;
  }
  void idleExpired() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server idle expiry outside Tx thread", return);
	    Base::cancelTimers_();
	    m_closePeer = false;
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
    Base::disconnect(false);
  }

private:
  using InitialKeyDir = Base::InitialKeyDir;
  using CIDSel = Base::CIDSel;

  void acceptInitialInfo(const InitialInfo &info) {
    m_initialInfo = info;
  }

  void resetRuntimeState() {
    Base::resetLink_();
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->m_peerAddr.null();
      return link;
    });
    m_bootstrap = {};
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto(const LongHdr &h, unsigned datagramLen) {
    if (h.type != PktType::Initial ||
	!m_bootstrap.acceptInitial(m_initialInfo, h, datagramLen)) {
      Base::packetParseFailure();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.localInitialSCID(),
      m_bootstrap.clientInitialSCID(),
      m_bootstrap.origDCID(), m_bootstrap.origDCID());
    Base::addLocalCID_(
      m_bootstrap.localInitialSCID(), 0, m_bootstrap.statelessResetToken());
    if (!Base::loadSrvParams(m_bootstrap)) return false;
    Base::configLocalParams(app());
    if (!Base::deriveInitial_()) return false;
    uint32_t maxEarlyData = app()->maxEarlyData(impl());
    if (!Base::initTLS_(CryptoConfig{
	  .isServer = true,
	  .enable0RTT = maxEarlyData != 0,
	  .alpn = app()->firstALPN(),
	  .certPath = app()->certPath(),
	  .keyPath = app()->keyPath(),
	  .keyLogPath = app()->keyLogPath(),
	  .qlogTrace = &app()->qlogTrace(),
	  .encryptTicket = app()->ticketEncryptCB()
	}))
      return false;
    if (!Base::startHandshake()) return false;
    return true;
  }

  void markEstablished_() {
    if (!Base::readyToEstablish())
      return;
    if (!Base::validateClientParams()) {
      Base::tlsFailure();
      app()->error_(ZeEXCEPT(Error, "Zquic",
	"client QUIC transport parameters failed validation"));
      return;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    Base::scheduleMigrationCIDs();
    if (app()->newTokenAddrValidate())
      queueNewToken(m_bootstrap.origDCID());
    impl()->connected(Connected{
      .alpn = Base::negotiatedProtocol_(),
      .version = Version1
    });
  }

  bool emitTLS_(size_t inEpoch, ZuBSpan input, ZiSockAddr addr) {
    return Base::template advanceTLS<TLSBufSize>(
      inEpoch, input, ZuMv(addr),
      [this](
	  ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
	  ZiSockAddr addr_) {
	return sendCryptoFlights_(ZuMv(data), offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [this](ZiSockAddr addr_) {
	if (Base::establishedRx() && !m_handshakeDoneSent)
	  app()->txRun([link = impl(), addr = ZuMv(addr_)]() mutable {
	    if (link->disconnecting_()) return;
	    (void)link->sendHandshakeDone(ZuMv(addr));
	  });
	return true;
      });
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
      "QUIC server crypto send outside Tx thread", return false);
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
      "QUIC server crypto packet send outside Tx thread", return false);
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

  bool sendPktPath(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool &sent) {
    sent = false;
    if (!app()->sendPkt(buf)) return true;
    sent = app()->sendPktRaw(ZuMv(buf), ZuMv(addr), ecn);
    return sent;
  }

  void flushStreamWritable(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable(stream);
    if (!flushTx_()) queueTxFlush();
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

  bool sendPassiveMigChal() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server passive migration challenge outside Tx thread",
      return false);
    PktBuild build;
    typename Base::TxPktRefs refs;
    ControlFrame frame;
    if (!Base::buildPassiveMigChal(build, refs, frame))
      return false;
    ZiSockAddr addr = Base::validatingRemote();
    ZiSockAddr retryAddr = addr;
    bool sent = sendStrictShortPkt(
      Base::validatingPeerCID(), build, ZuMv(addr),
      &refs);
    if (!sent) {
      PktBuild retry;
      typename Base::TxPktRefs retryRefs;
      ControlFrame retryFrame;
      if (!Base::buildPassiveMigChal(
	    retry, retryRefs, retryFrame))
	return false;
      sent = sendStrictShortPkt(
	Base::validatingPeerCID(), retry, ZuMv(retryAddr),
	nullptr);
      frame = retryFrame;
    }
    if (sent)
      Base::passiveMigChalSent(frame);
    return sent;
  }
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
	ZuMv(local), ZuMv(remote), rxBytes);
      link->sendPathResponseTx_(
	payload, ZuMv(responseAddr));
    });
    return true;
  }
  bool sendPathResponseTx_(ZuBSpan data, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server PATH_RESPONSE outside Tx thread", return false);
    return Base::sendPathResponsePkt(
      data, ZuMv(addr),
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const Base::TxPktRefs *refs) {
	return sendStrictShortPkt(build, ZuMv(addr_), refs);
      });
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (Base::linkClosedTx_() || !Base::establishedTx() || !m_peerAddr)
      return false;
    return flushTx_(m_peerAddr);
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::linkClosedTx_()) return false;
    if (!Base::establishedTx()) {
      return flushPendingAcks(addr);
    }
    // Ingress ACK snapshots can precede candidate CID selection. Ordinary
    // output follows the active tuple; candidate probes use their own route.
    addr = m_peerAddr;
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

  bool sendCryptoPkt_(
    PktNumSpace::T level, ZuBSpan prefix, ZuBSpan payload,
    const SentFrameRef &ref, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server crypto packet send outside Tx thread", return false);
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
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server frame gating outside Tx thread", return false);
    if (!recordRefs) return true;
    for (unsigned i = 0, n = recordRefs->count(); i < n; ++i)
      if (!app()->sendFrame((*recordRefs)[i])) return false;
    return true;
  }

  bool sendPathBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server path send outside Tx thread", return false);
    return Base::sendPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server path probe send outside Tx thread", return false);
    return Base::sendPathProbePktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendInitialBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Initial buffer send outside Tx thread", return false);
    if (!sendFrameRefs(recordRefs)) return false;
    return Base::holdInitialForCoalesce(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendHandshakeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Handshake buffer send outside Tx thread", return false);
    return Base::sendHandshakeCoalesced(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendShortBuf(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs,
    EcnMark::T ecn, unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
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
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server strict Short buffer send outside Tx thread", return false);
    if (!sendFrameRefs(recordRefs)) return false;
    return Base::sendValidPathPktApp(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = false;
	sent = app()->sendPktRaw(ZuMv(buf_), ZuMv(addr_), ecn_, true);
	return sent;
      },
      ecn);
  }

  bool sendInitialPkt(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    bool coalesce = true) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    if (ackEliciting) recordRefs = &refs;
    return Base::sendProtInitialPkt(
      InitialKeyDir::Server, CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), {}, false, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) {
	  if (!sendFrameRefs(recordRefs)) return false;
	  return sendPathBuf(ZuMv(buf), ZuMv(addr_));
	}
	return sendInitialBuf(ZuMv(buf), ZuMv(addr_), recordRefs);
      });
  }

  bool sendHandshakePkt(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Handshake, payload, frame)) return false;
    return sendHandshakePkt(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    bool coalesce = true) {
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
      [this]() { return app()->allocTxPkt_(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf(ZuMv(buf), ZuMv(addr_));
	return sendHandshakeBuf(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendZeroRTTPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server 0-RTT send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendZeroRTTPkt(build, ZuMv(addr), payload);
  }

  bool sendZeroRTTPkt(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtZeroRTTPkt(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_) {
	return sendShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, EcnMark::NotECT);
      });
  }

  bool sendShortPkt(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
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
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
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
    return Base::sendProtShortPkt(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
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
    return Base::sendProtShortPkt(
      CIDSel::Peer, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }
  bool sendStrictShortPkt(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    const Base::TxPktRefs *recordRefs) {
    return Base::sendProtShortPkt(
      dcid, Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }

  bool sendCloseFrame(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server close frame outside Tx thread", return false);
    if ((!closing && Base::linkClosedTx_()) ||
	(!Base::establishedTx() &&
	  !(closing && Base::closingTx_())) ||
	!m_peerAddr || !addr)
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

  bool sendHandshakeDone(ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server HANDSHAKE_DONE outside Tx thread", return false);
    Base::queueHandshakeDone();
    if (!Base::flushCtlStreams(
	ZuMv(addr),
	[this](PktBuild &build) {
	  return appendPendingAck(PktNumSpace::AppData, build);
	},
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const Base::TxPktRefs &refs) {
	  return sendShortPkt(build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	}))
      return false;
    m_handshakeDoneSent = 1;
    Base::handshakeDoneTx_();
    return true;
  }
  void handshakeDoneAckd() {
    Base::discardPktNumSpace_1(PktNumSpace::Handshake);
  }

  void queueNewToken(CxnID origDCID) {
    app()->txRun([link = impl(), origDCID = ZuMv(origDCID)]() mutable {
      if (link->disconnecting_()) return;
      ZiSockAddr addr = link->m_peerAddr;
      if (!addr) return;
      TokenBytes token;
      if (!AddressToken::encode(
	    token, TokenKind::NewToken, link->app()->addrValidationSecret(),
	    addr, origDCID, {}, uint64_t(Zm::now().sec()),
	    link->app()->addrValidatePeerPort()))
	return;
      (void)link->sendNewToken(ZuMv(token), ZuMv(addr));
    });
  }

  bool sendNewToken(TokenBytes token, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server NEW_TOKEN outside Tx thread", return false);
    if (!Base::establishedTx() || !addr) return false;
    PktBuild build;
    build.reset();
    int n = FrameCodec::writeNewToken(
      build.scratch(), build.scratchAvail(), token);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    const typename Base::TxPktRefs *refs = nullptr;
	    bool sent = sendShortPkt(build, ZuMv(addr), {}, refs, true);
	    if (sent) {
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
	event.trigger = SecTrigger::Sent;
	o.logSecEvt(EvtName::TokenIssued, event, time);
      }));
      Base::newTokenTx_();
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

  bool received_(Datagram d, const CxnID *routedDCID = nullptr) {
    if (!Base::handshakeStartedRx() && d.buf)
      Base::initServerPath_(app()->local(), d.addr);
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong(d_, packetOffset, packetLen);
      },
      [this, routedDCID](
	  Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort(d_, packetOffset, packetLen, routedDCID);
      });
    return !Base::disconnecting_();
  }

  bool receivedLong(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt(
      InitialKeyDir::Client, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &d_) {
	if (!Base::handshakeStartedRx()) {
	  ZiSockAddr peerAddr = d_.addr;
	  app()->txInvoke(impl(), [link = impl(), peerAddr]() mutable {
	    link->m_peerAddr = ZuMv(peerAddr);
	    return link;
	  });
	  if (!initRuntimeCrypto(h, d_.buf->length)) return false;
	  app()->refreshLinkRoutes(impl());
	}
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

  bool receivedShort(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    const CxnID *routedDCID = nullptr) {
    return Base::rxProtShortPkt(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      },
      routedDCID);
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, Base::RxAckMeta &ack,
    ZquicLog_::PktEvt *qlog, bool earlyData = false) {
    ZiSockAddr peer = addr;
    ZiSockAddr local = app()->local();
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
	Base::enterPeerDraining_0(frame.errorCode);
	return true;
      case FrameType::HandshakeDone:
	return false;
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

  void pathPromoted() {
    m_peerAddr = Base::activePathRemote_();
  }
  void refreshPromotedRoutes() {
    if (app()->rxInvoked()) {
      app()->refreshLinkRoutes(impl());
      return;
    }
    app()->rxRun([link = impl()]() mutable {
      link->app()->refreshLinkRoutes(link);
    });
  }
  void localCIDsIssued() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->refreshLinkRoutes(link);
    });
  }
  void migrationCIDsReady() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->txRun([link]() mutable {
	if (link->disconnecting_()) return;
	link->flushTx_();
      });
    });
  }

  bool receivedRouted(Datagram d, const CxnID &routedDCID) {
    return received_(ZuMv(d), &routedDCID);
  }

  void installRoutes_(CxnRouter<Impl> &routes) {
    routes.add(m_bootstrap.origDCID(), 0, impl());
    Base::installLocalCIDRoutes_(routes);
  }

  void retireRoutes_(CxnRouter<Impl> &routes) {
    routes.tombstone(m_bootstrap.origDCID());
    Base::tombstoneLocalCIDRoutes_(routes);
  }

  void retiredLocalCID(uint64_t, const CxnID &id) {
    app()->rxRun([app = app(), id]() mutable {
      app->tombstoneRoute(id);
    });
  }

  // shared
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ServerBootstrap	m_bootstrap;
  InitialInfo		m_initialInfo;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  ZiSockAddr		m_peerAddr;
  bool			m_closePeer = false;
};

} // namespace Zquic
