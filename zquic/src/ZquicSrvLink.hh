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

  SrvLink(App *app) : Base{app, true} { Base::initCryptoDelivery_(); }

  bool established() const { return Base::established_(); }
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
  const ZiSockAddr &peer() const { return m_peerAddr; }

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
      "QUIC server send_ outside Tx thread", return false);
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
      "QUIC server PTO outside Tx thread", return);
    if (Base::closed() || !m_peerAddr)
      return;
    Base::ptoRecovery_(
      [this](
	  PktNumSpace::T level, PktBuild &build,
	  const SentFrameRef &cryptoRef) {
	return level == PktNumSpace::Initial ?
	  sendInitialPkt_(build, m_peerAddr, {}, &cryptoRef, true, false) :
	  sendHandshakePkt_(build, m_peerAddr, {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe_(level); });
  }

  void queueRetransmit_() {
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
    if (Base::closed() || !m_peerAddr)
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
	if (!Base::buildRetxCrypto_(level, build, ref)) continue;
	bool ok =
	  level == PktNumSpace::Initial ?
	    sendInitialPkt_(build, m_peerAddr, {}, &ref, true, false) :
	  level == PktNumSpace::Handshake ?
	    sendHandshakePkt_(build, m_peerAddr, {}, &ref, true, false) :
	    sendShortPkt_(build, m_peerAddr, {}, &ref, true);
	if (!ok) {
	  Base::requeueRetransmit_(level, ref);
	  break;
	}
	sent = true;
	continue;
      }
      if (level != PktNumSpace::AppData || !Base::established_())
	continue;
      if (!pto && !Base::congestionAllowance_()) {
	Base::requeueRetransmit_(level, ref);
	break;
      }
      if (ref.kind == SentFrameKind::Stream) {
	if (!Base::buildRetxStream_(build, ref)) {
	  if (Base::debugLog_())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit build failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	  continue;
	}
      } else if (!Base::buildRetxCtl_(build, ref))
	continue;
      if (!sendShortPkt_(build, m_peerAddr, {}, &ref, true)) {
	if (ref.kind == SentFrameKind::Stream)
	  if (Base::debugLog_())
	    ZiLOG(Debug, "Zquic", ([ref](auto &s) {
	      s << "stream retransmit send failed streamID=" <<
		ref.streamID << " offset=" << ref.offset <<
		" length=" << ref.length << " fin=" << int(ref.fin);
	    }));
	if (ref.kind == SentFrameKind::Stream)
	  (void)Base::requeueStreamRef_(level, ref);
	else
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
	  build, m_peerAddr, {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt_(
	  build, m_peerAddr, {}, nullptr, true, false);
      default:
	return sendShortPkt_(
	  build, m_peerAddr, {}, &refs, true);
    }
  }
  void heartBeatExpired_() {
    (void)sendPingProbe_(PktNumSpace::AppData);
  }

  bool sendPMTUDProbe_(ZiSockAddr addr) {
    return Base::sendPMTUDProbe_(
      ZuMv(addr),
      [this](PktBuild &build, ZiSockAddr addr_, unsigned size) {
	typename Base::TxPktRefs refs;
	return sendShortPkt_(build, ZuMv(addr_), {}, &refs, true, size);
      });
  }
  bool disconnect(uint64_t errorCode = 0) {
    if (!Base::closed()) Base::closeState_(errorCode);
    if (Base::established_() && m_peerAddr) {
      app()->txRun([
	link = impl(),
	addr = m_peerAddr
      ]() mutable {
	if (link->disconnecting_()) return;
	(void)link->sendCloseFrame_(ZuMv(addr), true);
	link->m_closePeer = false;
	link->enterLocalClosingTx_();
      });
      return true;
    }
    return Base::disconnect(false);
  }

  void closeExpired_() {
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
  void idleExpired_() {
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
  using InitialKeyDir = typename Base::InitialKeyDir;
  using CIDSel = typename Base::CIDSel;

  void acceptInitialInfo_(const InitialInfo &info) {
    m_initialInfo = info;
  }

  void resetRuntimeState_() {
    Base::resetLink_();
    m_peerAddr.null();
    m_bootstrap = {};
    m_handshakeDoneSent = 0;
  }

  bool initRuntimeCrypto_(const LongHdr &h, unsigned datagramLen) {
    if (h.type != PktType::Initial ||
	!m_bootstrap.acceptInitial(m_initialInfo, h, datagramLen)) {
      Base::packetParseFailure_();
      return false;
    }
    Base::setRuntimeCIDs_(
      m_bootstrap.initialDCID(), m_bootstrap.localInitialSCID(),
      m_bootstrap.clientInitialSCID(),
      m_bootstrap.origDCID(), m_bootstrap.origDCID());
    Base::addLocalCID_(
      m_bootstrap.localInitialSCID(), 0, m_bootstrap.statelessResetToken());
    if (!Base::loadSrvParams_(m_bootstrap)) return false;
    Base::configLocalParams_(app());
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
	  .encryptTicket = app()->ticketEncryptCB_()
	}))
      return false;
    if (!Base::startHandshake_()) return false;
    return true;
  }

  void markEstablished_() {
    if (!Base::readyToEstablish_())
      return;
    if (!Base::validateClientParams_()) {
      Base::tlsFailure_();
      app()->error_(ZeEXCEPT(Error, "Zquic",
	"client QUIC transport parameters failed validation"));
      return;
    }
    Base::establish_();
    Base::validatePath_();
    Base::schedulePMTUD();
    Base::scheduleMigrationCIDs_();
    if (app()->newTokenAddrValidate())
      queueNewToken_();
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
      [this](ZiSockAddr addr_) {
	if (Base::established_() && !m_handshakeDoneSent)
	  app()->txRun([link = impl(), addr = ZuMv(addr_)]() mutable {
	    if (link->disconnecting_()) return;
	    (void)link->sendHandshakeDone_(ZuMv(addr));
	  });
	return true;
      });
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
      "QUIC server crypto send outside Tx thread", return false);
    Base::beginLongCoalesce_();
    bool ok = Base::sendCryptoFlights_(
      data, len, offsets, CryptoChunk, ZuMv(addr),
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
      "QUIC server crypto packet send outside Tx thread", return false);
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

  bool sendPktPath_(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool &sent) {
    sent = false;
    if (!app()->sendPkt(buf)) return true;
    sent = app()->sendPktRaw_(ZuMv(buf), ZuMv(addr), ecn);
    return sent;
  }

  void txDrained_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Tx drain outside Tx thread", return);
    flushTx_();
  }

  void flushStreamWritable_(StreamRef stream) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server stream Tx flush outside Tx thread", return);
    if (!stream || stream->id() < 0) return;
    Base::streamWritable_(stream);
    if (!flushTx_()) queueTxFlush_();
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

  bool sendPassiveMigChal_() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server passive migration challenge outside Tx thread",
      return false);
    PktBuild build;
    typename Base::TxPktRefs refs;
    ControlFrame frame;
    if (!Base::buildPassiveMigChal_(build, refs, frame))
      return false;
    ZiSockAddr addr = Base::validatingRemote_();
    ZiSockAddr retryAddr = addr;
    bool sent = sendStrictShortPkt_(
      Base::validatingPeerCID_(), build, ZuMv(addr),
      &refs);
    if (!sent) {
      PktBuild retry;
      typename Base::TxPktRefs retryRefs;
      ControlFrame retryFrame;
      if (!Base::buildPassiveMigChal_(
	    retry, retryRefs, retryFrame))
	return false;
      sent = sendStrictShortPkt_(
	Base::validatingPeerCID_(), retry, ZuMv(retryAddr),
	nullptr);
      frame = retryFrame;
    }
    if (sent)
      Base::passiveMigChalSent_(frame);
    return sent;
  }
  bool sendPathResponse_(
    ZuBSpan data, ZiSockAddr addr, ZiSockAddr local, unsigned rxBytes) {
    if (data.length() != PathChallenge::Length) return false;
    uint8_t payload[PathChallenge::Length];
    for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = data[i];
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
	byteSpan(payload, sizeof(payload)), ZuMv(responseAddr));
    });
    return true;
  }
  bool sendPathResponseTx_(ZuBSpan data, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server PATH_RESPONSE outside Tx thread", return false);
    return Base::sendPathResponsePkt_(
      data, ZuMv(addr),
      [this](
	  PktBuild &build, ZiSockAddr addr_,
	  const typename Base::TxPktRefs *refs) {
	return sendStrictShortPkt_(build, ZuMv(addr_), refs);
      });
  }

  bool flushTx_() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (Base::closed() || !Base::established_() || !m_peerAddr)
      return false;
    return flushTx_(m_peerAddr);
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (!addr) return flushTx_();
    if (Base::closed()) return false;
    if (!Base::established_()) {
      return flushPendingAcks_(addr);
    }
    bool sent = false;
    for (unsigned i = 0; i < Base::StreamFlushBatch; ++i) {
      bool sent_ = Base::flushCtlStreams_(
	addr,
	[this](PktBuild &build) {
	  return appendPendingAck_(PktNumSpace::AppData, build);
	},
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const typename Base::TxPktRefs &refs) {
	  return sendShortPkt_(
	    build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	});
      sent |= sent_;
      if (!sent_ || !Base::scheduledStreamCount_()) break;
    }
    sent |= sendPMTUDProbe_(ZuMv(addr));
    if (!sent) sent = flushPendingAcks_(ZuMv(addr));
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

  bool appendPendingAck_(PktNumSpace::T level, PktBuild &build) {
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

  bool sendFrameRefs_(const typename Base::TxPktRefs *recordRefs) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server frame gating outside Tx thread", return false);
    if (!recordRefs) return true;
    for (unsigned i = 0; i < recordRefs->count(); ++i)
      if (!app()->sendFrame((*recordRefs)[i])) return false;
    return true;
  }

  bool sendPathBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server path send outside Tx thread", return false);
    return Base::sendPathPktApp_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath_(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendPathProbeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server path probe send outside Tx thread", return false);
    return Base::sendPathProbePktApp_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath_(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendInitialBuf_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const typename Base::TxPktRefs *recordRefs) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Initial buffer send outside Tx thread", return false);
    if (!sendFrameRefs_(recordRefs)) return false;
    return Base::holdInitialForCoalesce_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf_(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendHandshakeBuf_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Handshake buffer send outside Tx thread", return false);
    return Base::sendHandshakeCoalesced_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_) {
	return sendPathBuf_(ZuMv(buf_), ZuMv(addr_));
      });
  }

  bool sendShortBuf_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const typename Base::TxPktRefs *recordRefs,
    EcnMark::T ecn, unsigned pmtudSize = 0) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Short buffer send outside Tx thread", return false);
    if (!sendFrameRefs_(recordRefs)) return false;
    if (pmtudSize) return sendPathProbeBuf_(ZuMv(buf), ZuMv(addr));
    return Base::sendPathPktApp_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	return sendPktPath_(ZuMv(buf_), ZuMv(addr_), ecn_, sent);
      },
      ecn);
  }
  bool sendStrictShortBuf_(
    ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    const typename Base::TxPktRefs *recordRefs, EcnMark::T ecn) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server strict Short buffer send outside Tx thread", return false);
    if (!sendFrameRefs_(recordRefs)) return false;
    return Base::sendValidPathPktApp_(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = false;
	sent = app()->sendPktRaw_(ZuMv(buf_), ZuMv(addr_), ecn_, true);
	return sent;
      },
      ecn);
  }

  bool sendInitialPkt_(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Initial send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Initial, payload, frame)) return false;
    return sendInitialPkt_(payload, ZuMv(addr), frame);
  }

  bool sendInitialPkt_(
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
    return Base::sendProtInitialPkt_(
      InitialKeyDir::Server, CIDSel::Peer, CIDSel::Local,
      Base::txPNLength_(PktNumSpace::Initial), {}, false, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) {
	  if (!sendFrameRefs_(recordRefs)) return false;
	  return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
	}
	return sendInitialBuf_(ZuMv(buf), ZuMv(addr_), recordRefs);
      });
  }

  bool sendHandshakePkt_(ZuBSpan frame, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Handshake send outside Tx thread", return false);
    PktBuild payload;
    if (!buildPayload_(PktNumSpace::Handshake, payload, frame)) return false;
    return sendHandshakePkt_(payload, ZuMv(addr), frame);
  }

  bool sendHandshakePkt_(
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
    return Base::sendProtHandshakePkt_(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength_(PktNumSpace::Handshake),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, coalesce](auto buf, ZiSockAddr addr_) {
	if (!coalesce) return sendPathBuf_(ZuMv(buf), ZuMv(addr_));
	return sendHandshakeBuf_(ZuMv(buf), ZuMv(addr_));
      });
  }

  bool sendZeroRTTPkt_(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server 0-RTT send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendZeroRTTPkt_(build, ZuMv(addr), payload);
  }

  bool sendZeroRTTPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false) {
    typename Base::TxPktRefs refs;
    const typename Base::TxPktRefs *recordRefs = nullptr;
    if (recordRef) {
      refs.add(*recordRef);
      recordRefs = &refs;
    }
    return Base::sendProtZeroRTTPkt_(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_) {
	return sendShortBuf_(
	  ZuMv(buf), ZuMv(addr_), recordRefs, EcnMark::NotECT);
      });
  }

  bool sendShortPkt_(ZuBSpan payload, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server Short send outside Tx thread", return false);
    PktBuild build;
    if (!buildPayload_(PktNumSpace::AppData, build, payload)) return false;
    return sendShortPkt_(build, ZuMv(addr), payload);
  }

  bool sendShortPkt_(
    PktBuild &payload, ZiSockAddr addr, ZuBSpan recordFrame,
    const SentFrameRef *recordRef = nullptr, bool ackEliciting = false,
    unsigned pmtudSize = 0) {
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
    return Base::sendProtShortPkt_(
      CIDSel::Peer, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs, pmtudSize](
	  auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf_(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn, pmtudSize);
      },
      pmtudSize);
  }

  bool sendShortPkt_(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    ZuBSpan recordFrame, const typename Base::TxPktRefs *recordRefs,
    bool ackEliciting, unsigned pmtudSize = 0) {
    return Base::sendProtShortPkt_(
      dcid, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs, ackEliciting,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs, pmtudSize](
	  auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendShortBuf_(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn, pmtudSize);
      },
      pmtudSize);
  }
  bool sendStrictShortPkt_(
    PktBuild &payload, ZiSockAddr addr,
    const typename Base::TxPktRefs *recordRefs) {
    return Base::sendProtShortPkt_(
      CIDSel::Peer, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf_(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }
  bool sendStrictShortPkt_(
    const CxnID &dcid, PktBuild &payload, ZiSockAddr addr,
    const typename Base::TxPktRefs *recordRefs) {
    return Base::sendProtShortPkt_(
      dcid, Base::txPNLength_(PktNumSpace::AppData),
      payload, ZuMv(addr), {}, recordRefs, true,
      [this]() { return app()->allocTxPkt_(); },
      [this, recordRefs](auto buf, ZiSockAddr addr_, EcnMark::T ecn) {
	return sendStrictShortBuf_(
	  ZuMv(buf), ZuMv(addr_), recordRefs, ecn);
      });
  }

  bool sendCloseFrame_(ZiSockAddr addr, bool closing = false) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server close frame outside Tx thread", return false);
    if ((!closing && Base::closed()) ||
	(!Base::established_() &&
	  !(closing && Base::closing_())) ||
	!m_peerAddr || !addr)
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

  bool sendHandshakeDone_(ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server HANDSHAKE_DONE outside Tx thread", return false);
    Base::queueHandshakeDone_();
    if (!Base::flushCtlStreams_(
	ZuMv(addr),
	[this](PktBuild &build) {
	  return appendPendingAck_(PktNumSpace::AppData, build);
	},
	[this](
	    PktBuild &build, ZiSockAddr addr_,
	    const typename Base::TxPktRefs &refs) {
	  return sendShortPkt_(build, ZuMv(addr_), {}, &refs, refs.count() != 0);
	}))
      return false;
    m_handshakeDoneSent = 1;
    Base::handshakeDoneTx_();
    return true;
  }
  void handshakeDoneAckd_() {
    Base::discardTxPktNumSpace_(PktNumSpace::Handshake);
  }

  void queueNewToken_() {
    if (!m_peerAddr) return;
    TokenBytes token;
    if (!AddressToken::encode(
	  token, TokenKind::NewToken, app()->addrValidationSecret(),
	  m_peerAddr, m_bootstrap.origDCID(), {},
	  uint64_t(Zm::now().sec()), app()->addrValidatePeerPort()))
      return;
    app()->txRun([
      link = impl(),
      addr = m_peerAddr,
      token = ZuMv(token)
    ]() mutable {
      if (link->disconnecting_()) return;
      (void)link->sendNewToken_(ZuMv(token), ZuMv(addr));
    });
  }

  bool sendNewToken_(TokenBytes token, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server NEW_TOKEN outside Tx thread", return false);
    if (!Base::established_() || !addr) return false;
    PktBuild build;
    build.reset();
    int n = FrameCodec::writeNewToken(
      build.scratch(), build.scratchAvail(), token);
    if (n <= 0 || !build.commitScratch(unsigned(n))) return false;
    const typename Base::TxPktRefs *refs = nullptr;
	    bool sent = sendShortPkt_(build, ZuMv(addr), {}, refs, true);
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
    if (!Base::txSecretInstalled_(level)) return true;
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

  bool received_(Datagram d, const CxnID *routedDCID = nullptr) {
    if (!Base::handshakeStarted_() && d.buf)
      Base::initServerPath_(app()->local(), d.addr);
    Base::receiveDatagram_(
      ZuMv(d),
      [this](Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedLong_(d_, packetOffset, packetLen);
      },
      [this, routedDCID](
	  Datagram &d_, unsigned packetOffset, unsigned packetLen) {
	return receivedShort_(d_, packetOffset, packetLen, routedDCID);
      });
    return !Base::disconnecting_();
  }

  bool receivedLong_(Datagram &d, unsigned packetOffset, unsigned packetLen) {
    return Base::receiveProtLongPkt_(
      InitialKeyDir::Client, d, packetOffset, packetLen,
      [this](const LongHdr &h, Datagram &d_) {
	if (!Base::handshakeStarted_()) {
	  m_peerAddr = d_.addr;
	  if (!initRuntimeCrypto_(h, d_.buf->length)) return false;
	  app()->refreshLinkRoutes_(impl());
	}
	return true;
      },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  typename Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog,
	  bool earlyData) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, earlyData);
      });
  }

  bool receivedShort_(
    Datagram &d, unsigned packetOffset, unsigned packetLen,
    const CxnID *routedDCID = nullptr) {
    return Base::rxProtShortPkt_(
      d, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<ZiIOBuf> &packetBuf,
	  typename Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog);
      },
      routedDCID);
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<ZiIOBuf> &packetBuf, typename Base::RxAckMeta &ack,
    ZquicLog_::PktEvt *qlog, bool earlyData = false) {
    ZiSockAddr peer = addr;
    ZiSockAddr local = app()->local();
    unsigned pathBytes = packetBuf ? packetBuf->length : 0;
    bool ok = Base::consumeProtFrames_(
      level, pn, frames, ZuMv(addr), packetBuf, ack, qlog,
      [this](size_t epoch, ZuBSpan input, ZiSockAddr addr_) {
	return emitTLS_(epoch, input, ZuMv(addr_));
      },
      [this, local, pathBytes](
	  PktNumSpace::T level_, const Frame &frame, ZiSockAddr addr_) {
	return handleControlFrame_(
	  level_, frame, ZuMv(addr_), local, pathBytes);
      },
      earlyData);
    if (ok && level == PktNumSpace::AppData && Base::established_())
      Base::observePathRx_(ZuMv(local), ZuMv(peer), pathBytes);
    return ok;
  }

  bool handleControlFrame_(
    PktNumSpace::T level, const Frame &frame, ZiSockAddr addr,
    ZiSockAddr local = {}, unsigned pathBytes = 0) {
    switch (frame.type) {
      case FrameType::MaxData:
      case FrameType::MaxStreamData:
      case FrameType::MaxStreams:
	queueTxFlush_();
	return true;
      case FrameType::PathChallenge: {
	sendPathResponse_(
	  frame.payload, ZuMv(addr), ZuMv(local),
	  level == PktNumSpace::AppData ? pathBytes : 0);
	return true;
      }
      case FrameType::PathResponse:
	Base::rxPathResponse_(frame.payload);
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
	m_closePeer = true;
	Base::enterPeerDraining_(frame.errorCode);
	return true;
      case FrameType::HandshakeDone:
	return false;
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

  void pathPromoted_() {
    m_peerAddr = Base::activePathRemote_();
  }
  void refreshPromotedRoutes_() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->refreshLinkRoutes_(link);
    });
  }
  void localCIDsIssued_() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->refreshLinkRoutes_(link);
    });
  }
  void migrationCIDsReady_() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->txRun([link]() mutable {
	if (link->disconnecting_()) return;
	link->flushTx_();
      });
    });
  }

  bool receivedRouted_(Datagram d, const CxnID &routedDCID) {
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

  void retiredLocalCID_(uint64_t, const CxnID &id) {
    app()->rxRun([app = app(), id]() mutable {
      app->tombstoneRoute_(id);
    });
  }

  // shared
  ZmAtomic<unsigned>	m_handshakeDoneSent = 0;
  bool			m_closePeer = false;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ServerBootstrap	m_bootstrap;
  InitialInfo		m_initialInfo;
  ZiSockAddr		m_peerAddr;
};

} // namespace Zquic
