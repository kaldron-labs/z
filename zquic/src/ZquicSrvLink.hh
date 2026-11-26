//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC server link API implementation

#ifndef Zquic_HH
#error "include zlib/Zquic.hh before this header"
#endif

namespace Zquic_ {

struct alignas(Zm::CacheLineSize) SrvSharedState {
  ZmAtomic<unsigned>	handshakeDoneSent = 0;
};

struct alignas(Zm::CacheLineSize) SrvRxStorage {
  ServerBootstrap	bootstrap;
  bool			originalRouted = false;
};

struct alignas(Zm::CacheLineSize) SrvTxStorage {
  ZiSockAddr		peerAddr;
  bool			closePeer = false;
  PktAllocTarget	activeTarget;
  PktAllocTarget	probeTarget;
  ZiSockAddr		probeLocal;
  ZiSockAddr		probeRemote;
  Path *		responsePath = nullptr;
};

} // Zquic_

namespace Zquic {

template <
  typename App, typename Impl, typename Stream_>
class SrvLink :
  public Link<App, Impl, Stream_> {
public:
  using Base = Link<App, Impl, Stream_>;
  using Stream = Stream_;
  using StreamRef = ZmRef<Stream>;
  enum { TLSBufSize = (64<<10) }; // 64K
  enum { PNLength = 2 };
  using Base::Base;
  using Base::app;
  using Base::impl;
  friend Base;
  template <typename, typename> friend class Zquic::Stream;

  template <typename, typename> friend class Server;

  SrvLink(App *app) : Base{app, true} { Base::initCryptoDelivery(); }

  Zquic_::PktAllocTarget pktAllocTarget() const {
    return m_srvTx.probeLocal ? m_srvTx.probeTarget : m_srvTx.activeTarget;
  }
  ZiSockAddr outputLocal() const {
    return m_srvTx.probeLocal ? m_srvTx.probeLocal : Base::activePathLocal_();
  }
  bool probingOutput() const {
    return m_srvTx.probeLocal &&
      (m_srvTx.probeLocal != Base::activePathLocal_() ||
       m_srvTx.probeRemote != Base::activePathRemote_());
  }
  Path *outputPath() const { return m_srvTx.responsePath; }
  void pathBinding(const ZiSockAddr &local, unsigned &slot, uint64_t &generation) const {
    slot = app()->endpointSlot(local);
    generation = app()->endpointGeneration();
  }
  bool pathBindingValid(const ZiSockAddr &local, unsigned slot, uint64_t generation) const {
    return generation == app()->endpointGeneration() && app()->endpointSlot(local) == slot &&
      app()->pktAllocTarget(slot, unsigned(generation)).physical;
  }
  void pktPressureResume() {
    uint64_t runtime = Base::txRuntime();
    retransmit_();
    if (runtime == Base::txRuntime() && !Base::disconnecting_()) flushTx_(m_srvTx.peerAddr);
  }
  void waitPktTx(Zquic_::EndpointTxWait &wait, uint64_t epoch, Zquic_::EndpointTxFn fn) {
    app()->waitTx(wait, app()->txThread(), epoch, ZuMv(fn), pktAllocTarget());
  }
  bool sendEndpointPkt(ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
    EcnMark::T ecn, bool priority = false) {
    auto target = pktAllocTarget();
    if (!buf || buf->owner != target.owner) return false;
    return Base::submitPktTx(ZuMv(buf), ZuMv(addr), ecn, priority,
      [this, target](auto buf, ZiSockAddr addr, EcnMark::T ecn, bool priority,
	Zquic_::EndpointTxToken token, bool *blocked) {
	return app()->sendPktRaw(
	  ZuMv(buf), ZuMv(addr), ecn, priority, token, blocked, target);
      });
  }

  using Base::established;
  using Base::diag;
  using Base::pathDiag;
  template <typename L>
  void pathInfo(L &&l) const {
    if (app()->txInvoked()) {
      l(Base::activePathInfo(), Base::candidatePathInfo_());
      return;
    }
    auto link = const_cast<SrvLink *>(this)->impl();
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
    auto link = const_cast<SrvLink *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      l(link->migrationResult());
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
    return m_srvTx.peerAddr;
  }

  using Base::send;
  using Base::send_;

  void ptoTimer() { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server PTO outside Tx thread", return);
    if (Base::linkClosedTx_() || !m_srvTx.peerAddr)
      return;
    Base::ptoRecovery(
      [this](
	  PktNumSpace::T level, PktBuild &build,
	  const SentFrameRef &cryptoRef) {
	return level == PktNumSpace::Initial ?
	  sendInitialPkt(build, m_srvTx.peerAddr, {}, &cryptoRef, true, false) :
	  sendHandshakePkt(build, m_srvTx.peerAddr, {}, &cryptoRef, true, false);
      },
      [this]() { return flushTx_(); },
      [this](PktNumSpace::T level) { return sendPingProbe(level); });
  }

  using Base::queueRetransmit;

  bool retransmit_(
    bool pto = false, unsigned maxPackets = 2,
    PktNumSpace::T onlySpace = PktNumSpace::N) { // Tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server retransmit outside Tx thread", return false);
    if (Base::linkClosedTx_() || !m_srvTx.peerAddr)
      return false;
    return Base::template retransmitFrames<true>(
      pto, maxPackets, onlySpace,
      [this](PktNumSpace::T level, PktBuild &build, const SentFrameRef &ref) {
	if (level == PktNumSpace::Initial)
	  return sendInitialPkt(build, m_srvTx.peerAddr, {}, &ref, true, false);
	if (level == PktNumSpace::Handshake)
	  return sendHandshakePkt(build, m_srvTx.peerAddr, {}, &ref, true, false);
	return sendShortPkt(build, m_srvTx.peerAddr, {}, &ref, true);
      },
      [this] { return sendPassiveMigChal(); });
  }

  bool sendPingProbe(PktNumSpace::T level) {
    PktBuild build;
    if (!Base::buildPingProbe(level, build)) return false;
    typename Base::TxPktRefs refs;
    switch (level) {
      case PktNumSpace::Initial:
	return sendInitialPkt(
	  build, m_srvTx.peerAddr, {}, nullptr, true, false);
      case PktNumSpace::Handshake:
	return sendHandshakePkt(
	  build, m_srvTx.peerAddr, {}, nullptr, true, false);
      default:
	return sendShortPkt(
	  build, m_srvTx.peerAddr, {}, &refs, true);
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
      link->disconnectRx(errorCode);
      return link;
    });
    return accepted;
  }
  void disconnectRx(uint64_t errorCode) {
    ZiAssert(app()->rxInvoked(), "Zquic", (),
      "QUIC server disconnect outside Rx thread", return);
    if (!Base::linkClosedRx_()) Base::closeState(errorCode, true);
    if (Base::establishedRx()) {
      app()->txRun([link = impl()]() mutable {
	if (link->disconnecting_()) return;
	if (ZiSockAddr addr = link->m_srvTx.peerAddr) {
	  (void)link->sendCloseFrame(ZuMv(addr), true);
	  link->m_srvTx.closePeer = false;
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
    bool peer = m_srvTx.closePeer;
    m_srvTx.closePeer = false;
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
    Base::disconnect(peer);
  }
  void peerDrainingTx() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server peer-draining handoff outside Tx thread", return);
    m_srvTx.closePeer = true;
  }
  void idleExpired() {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server idle expiry outside Tx thread", return);
	    Base::cancelTimers_();
	    m_srvTx.closePeer = false;
	    ZquicLOG(app()->qlogTrace(), ([linkInfo = Base::linkInfo_()](auto &o, ZuTime time) {
	      CloseEvt event{
		.linkInfo = linkInfo,
		.initiator = CloseInitiator::Local,
		.trigger = CloseTrigger::IdleTimeout,
		.reason = CloseReason::Idle,
		.connectionError = CloseError::NoError};
      o.logCxnClosed(event, time);
    }));
    Base::disconnect(false);
  }

protected:
  template <typename L> void ownerLayout(L l) const {
    Base::ownerLayout(l);
    l(&m_srvShared, sizeof(m_srvShared));
    l(&m_srvRx, sizeof(m_srvRx));
    l(&m_srvTx, sizeof(m_srvTx));
  }

private:
  using InitialKeyDir = Base::InitialKeyDir;
  using CIDSel = Base::CIDSel;

  bool acceptInitial(const InitialInfo &info, CxnRouter<Impl> &routes) {
    auto &bootstrap = m_srvRx.bootstrap;
    if (!bootstrap.acceptInitial(info, info.header, info.datagramLength)) {
      Base::packetParseFailure();
      return false;
    }
    if (!Base::setRuntimeCIDs_(
	  bootstrap.initialDCID(), bootstrap.localInitialSCID(), bootstrap.clientInitialSCID(),
	  bootstrap.origDCID(), bootstrap.origDCID(), bootstrap.statelessResetToken()))
      return false;
    if (!Base::loadSrvParams(m_srvRx.bootstrap)) return false;
    Base::configLocalParams(app());
    const auto &addrs = app()->runtimeServerAddrs();
    if (addrs.preferred4 || addrs.preferred6) {
      PreferredAddress preferred{
	.ipv4 = addrs.preferred4,
	.ipv6 = addrs.preferred6
      };
      if (!CxnIDGen::random(preferred.cid) || !preferred.resetToken.generate() ||
	  !Base::addLocalCID_(preferred.cid, 1, preferred.resetToken))
	return false;
      Base::setPreferredAddress_(preferred);
    }
    if (!impl()->bindBootstrapRoutes(routes)) return false;
    Base::initServerPath_(info.local, info.peer);
    auto target = app()->pktAllocTarget(info.endpointSlot, info.runtimeGeneration);
    app()->txInvoke(impl(), [link = impl(), peer = info.peer, target]() mutable {
      link->m_srvTx.peerAddr = ZuMv(peer);
      link->m_srvTx.activeTarget = target;
      return link;
    });
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
	  .encryptTicket = app()->ticketEncryptCB(),
	  .chachaOnly = app()->chachaOnly()
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
      queueNewToken(m_srvRx.bootstrap.origDCID());
    impl()->connected(Connected{
      .alpn = Base::negotiatedProtocol_(),
      .version = Version1
    });
    // A fragmented Finished can complete inside the CRYPTO dequeue callback.
    // Retire after that callback unwinds and after its final TLS output post.
    app()->rxRun([link = impl()]() {
      if (link->disconnecting_()) return;
      link->discardPktNumSpace_0(PktNumSpace::Handshake);
    });
  }

  bool emitTLS_(size_t inEpoch, ZuBSpan input, ZiSockAddr addr) {
    return Base::advanceTLS(
      inEpoch, input, ZuMv(addr),
      [this](
	  ZmRef<ZiIOBuf> data, const CryptoOffsets &offsets,
	  ZiSockAddr addr_) {
	return Base::sendCryptoFlights_(ZuMv(data), offsets, ZuMv(addr_));
      },
      [this]() { markEstablished_(); },
      [this](ZiSockAddr addr_) {
	if (Base::establishedRx() && !m_srvShared.handshakeDoneSent)
	  app()->txRun([link = impl(), addr = ZuMv(addr_)]() mutable {
	    if (link->disconnecting_()) return;
	    (void)link->sendHandshakeDone(ZuMv(addr));
	  });
	return true;
      });
  }

  bool sendPktPath(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr, EcnMark::T ecn, bool &sent) {
    sent = false;
    auto target = pktAllocTarget();
    if (!buf || buf->owner != target.owner) return false;
    if (!app()->sendPkt(buf)) return true;
    if (!(target == pktAllocTarget())) return false;
    sent = sendEndpointPkt(ZuMv(buf), ZuMv(addr), ecn);
    return sent;
  }

  using Base::appendPendingAck;
  using Base::buildPayload_;
  using Base::sendCryptoPkt_;
  using Base::flushStreamWritable;
  using Base::queueTxFlush;

  template <typename Fn>
  bool withProbeOutput(ZiSockAddr local, ZiSockAddr remote, Fn fn) {
    auto previousTarget = m_srvTx.probeTarget;
    auto previousLocal = m_srvTx.probeLocal;
    auto previousRemote = m_srvTx.probeRemote;
    m_srvTx.probeTarget = app()->pktAllocTarget(local);
    m_srvTx.probeLocal = ZuMv(local);
    m_srvTx.probeRemote = ZuMv(remote);
    bool sent = fn();
    m_srvTx.probeTarget = previousTarget;
    m_srvTx.probeLocal = ZuMv(previousLocal);
    m_srvTx.probeRemote = ZuMv(previousRemote);
    return sent;
  }

  bool sendPassiveMigChal() {
    return withProbeOutput(Base::validatingLocal(), Base::validatingRemote(),
      [this] { return sendPassiveMigChal_(); });
  }
  bool sendPassiveMigChal_() {
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
    uint64_t runtime = Base::txRuntime();
    bool sent = sendStrictShortPkt(
      Base::validatingPeerCID(), build, ZuMv(addr),
      &refs);
    if (runtime != Base::txRuntime()) return sent;
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
      if (runtime != Base::txRuntime()) return sent;
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
      link->observePathRxTx(
	local, ZuMv(remote), rxBytes, true, true, false, false);
      link->sendPathResponseTx(
	payload, ZuMv(responseAddr), ZuMv(local));
    });
    return true;
  }
  void sendObservedResponse(
      ZuBSpan data, ZiSockAddr remote, ZiSockAddr local, unsigned bytes) {
    bool active = local == Base::activePathLocal_() &&
      remote == Base::activePathRemote_();
    bool candidate = local == Base::validatingLocal() &&
      remote == Base::validatingRemote();
    if (active || candidate) {
      (void)sendPathResponseTx(data, ZuMv(remote), ZuMv(local));
      return;
    }
    // A competing tuple has only this datagram's allowance. Its response
    // uses stack state and cannot borrow the retained candidate's credit.
    Path path = Path::server(local, remote);
    path.received(bytes);
    auto previous = m_srvTx.responsePath;
    m_srvTx.responsePath = &path;
    (void)sendPathResponseTx(data, ZuMv(remote), ZuMv(local));
    m_srvTx.responsePath = previous;
  }

  bool sendPathResponseTx(ZuBSpan data, ZiSockAddr addr, ZiSockAddr local) {
    ZiSockAddr remote = addr;
    return withProbeOutput(ZuMv(local), ZuMv(remote),
      [this, data, addr = ZuMv(addr)]() mutable {
	return sendPathResponseTx_(data, ZuMv(addr));
      });
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
    if (Base::linkClosedTx_() || !Base::establishedTx() || !m_srvTx.peerAddr)
      return false;
    return flushTx_(m_srvTx.peerAddr);
  }
  bool flushTx_(ZiSockAddr addr) { // direct call from within tx thread
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server flush outside Tx thread", return false);
    if (Base::deferTxFlush()) return false;
    Base::drainPeerAcksTx(false);
    if (!addr) return flushTx_();
    if (Base::linkClosedTx_()) return false;
    if (!Base::establishedTx()) {
      return flushPendingAcks(addr);
    }
    // Ingress ACK snapshots can precede candidate CID selection. Ordinary
    // output follows the active tuple; candidate probes use their own route.
    addr = m_srvTx.peerAddr;
    uint64_t runtime = Base::txRuntime();
    bool probed = Base::hasPendingMigChallenge() && sendPassiveMigChal();
    if (runtime != Base::txRuntime()) return probed;
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
    return sent || probed;
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
    return Base::template submitPathPkt<Base::PathTx::Filter>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn, bool &sent) {
	return sendPktPath(ZuMv(buf_), ZuMv(addr_), ecn, sent);
      });
  }

  bool sendPathProbeBuf(ZmRef<ZiIOBuf> buf, ZiSockAddr addr) {
    ZiAssert(app()->txInvoked(), "Zquic", (),
      "QUIC server path probe send outside Tx thread", return false);
    return Base::template submitPathPkt<Base::PathTx::FilterProbe>(
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
    return Base::template submitPathPkt<Base::PathTx::Filter>(
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
    return Base::template submitPathPkt<Base::PathTx::Validation>(
      ZuMv(buf), ZuMv(addr),
      [this](auto buf_, ZiSockAddr addr_, EcnMark::T ecn_, bool &sent) {
	sent = false;
	sent = sendEndpointPkt(ZuMv(buf_), ZuMv(addr_), ecn_, true);
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
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::Initial), {}, false, payload, ZuMv(addr),
      recordFrame, recordRefs, ackEliciting,
      [this]() { return Base::allocPktTx(); },
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
    return Base::template sendProtLongPkt<Zquic::PktType::ZeroRTT>(
      CIDSel::Peer, CIDSel::Local,
      Base::txPNLength(PktNumSpace::AppData),
      payload, ZuMv(addr), recordFrame, recordRefs,
      ackEliciting,
      [this]() { return Base::allocPktTx(); },
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
      [this]() { return Base::allocPktTx(); },
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
      [this]() { return Base::allocPktTx(); },
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
      [this]() { return Base::allocPktTx(); },
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
      [this]() { return Base::allocPktTx(); },
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
	!m_srvTx.peerAddr || !addr)
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
    m_srvShared.handshakeDoneSent = 1;
    return true;
  }
  void queueNewToken(CxnID origDCID) {
    app()->txRun([link = impl(), origDCID = ZuMv(origDCID)]() mutable {
      if (link->disconnecting_()) return;
      ZiSockAddr addr = link->m_srvTx.peerAddr;
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
		  .linkInfo = linkInfo,
		  .value = tokenLength,
		  .kind = SecKind::Token,
		  .reason = SecReason::NewToken,
		  .success = true};
	event.trigger = SecTrigger::Sent;
	o.logSecEvt(EvtName::TokenIssued, event, time);
      }));
      Base::newTokenTx();
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

  bool received_(ZmRef<RxPkt> pkt, const CxnID *routedDCID = nullptr) {
    Base::rxPacket(
      ZuMv(pkt),
      [this](const ZmRef<RxPkt> &pkt_, unsigned packetOffset,
	  unsigned packetLen, const LongHdr &h) {
	return receivedLong(pkt_, packetOffset, packetLen, h);
      },
      [this, routedDCID](
	  const ZmRef<RxPkt> &pkt_, unsigned packetOffset, unsigned packetLen,
	  bool *pathAccounted) {
	return receivedShort(pkt_, packetOffset, packetLen, routedDCID, pathAccounted);
      });
    return !Base::disconnecting_();
  }

  bool receivedLong(
    const ZmRef<RxPkt> &pkt, unsigned packetOffset, unsigned packetLen, const LongHdr &h) {
    return Base::receiveProtLongPkt(
      InitialKeyDir::Client, pkt, packetOffset, packetLen, h,
      [](const LongHdr &, const ZmRef<RxPkt> &) { return true; },
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<RxPkt> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog,
	  bool earlyData) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, earlyData, packetBuf->local);
      });
  }

  bool receivedShort(
    const ZmRef<RxPkt> &pkt, unsigned packetOffset, unsigned packetLen,
    const CxnID *routedDCID = nullptr, bool *pathAccounted = nullptr) {
    return Base::rxProtShortPkt(
      pkt, packetOffset, packetLen,
      [this](
	  PktNumSpace::T level, uint64_t pn, ZuBSpan frames,
	  ZiSockAddr addr, const ZmRef<RxPkt> &packetBuf,
	  Base::RxAckMeta &ack, ZquicLog_::PktEvt *qlog) {
	return consumeFrames_(
	  level, pn, frames, ZuMv(addr), packetBuf, ack, qlog, false, packetBuf->local);
      },
      routedDCID, pathAccounted);
  }

  bool consumeFrames_(
    PktNumSpace::T level, uint64_t pn, ZuBSpan frames, ZiSockAddr addr,
    const ZmRef<RxPkt> &packetBuf, Base::RxAckMeta &ack,
    ZquicLog_::PktEvt *qlog, bool earlyData, ZiSockAddr local) {
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
  void streamsBlocked(Zquic::StreamType::T type, uint64_t maximum) {
    Base::queueBlocked(FrameType::StreamsBlocked, 0, maximum, type);
    impl()->flowBlocked(FrameType::StreamsBlocked, 0, type, maximum);
    queueTxFlush();
  }

  void pathPromoted() {
    m_srvTx.peerAddr = Base::activePathRemote_();
    auto previous = m_srvTx.activeTarget;
    m_srvTx.activeTarget = app()->pktAllocTarget(Base::activePathLocal_());
    if (!(previous == m_srvTx.activeTarget))
      Base::cancelPktWait([link = impl(), runtime = Base::txRuntime()] {
	if (runtime != link->txRuntime() || link->disconnecting_()) return;
	link->pktPressureResume();
      });
  }
protected:
  void migrationCIDsReady() {
    app()->rxRun([link = impl()]() mutable {
      link->app()->txRun([link]() mutable {
	if (link->disconnecting_()) return;
	link->flushTx_();
      });
    });
  }

  bool receivedRouted(ZmRef<RxPkt> d, const CxnID &routedDCID) {
    return received_(ZuMv(d), &routedDCID);
  }

  bool bindLocalCID(typename Base::LinkCID &cid) {
    return Base::bindLocalCIDRoute_(app()->m_routes, cid);
  }
  bool bindBootstrapRoutes(CxnRouter<Impl> &routes) {
    const auto &bootstrap = m_srvRx.bootstrap;
    if (bootstrap.origDCID() != bootstrap.localInitialSCID()) {
      if (!routes.add(bootstrap.origDCID(), 0, impl())) return false;
      m_srvRx.originalRouted = true;
    }
    return Base::bindLocalCIDRoutes_(routes);
  }

private:
  void rollbackRoutes(CxnRouter<Impl> &routes) {
    if (m_srvRx.originalRouted) {
      routes.retire(m_srvRx.bootstrap.origDCID());
      m_srvRx.originalRouted = false;
    }
    Base::retireLocalCIDRoutes_(routes);
  }
  void retireRoutes(CxnRouter<Impl> &routes) {
    if (m_srvRx.originalRouted) {
      routes.tombstone(m_srvRx.bootstrap.origDCID());
      m_srvRx.originalRouted = false;
    }
    Base::tombstoneLocalCIDRoutes_(routes);
  }

  void retiredLocalCID(uint64_t, const CxnID &id) {
    app()->tombstoneRoute(id);
  }

  // shared
  Zquic_::SrvSharedState m_srvShared;

  // Rx thread exclusive
  Zquic_::SrvRxStorage m_srvRx;

  // Tx thread exclusive
  Zquic_::SrvTxStorage m_srvTx;

};

} // namespace Zquic
