//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Connection API implementation

#ifndef Zutcp_HH
#error "include zlib/Zutcp.hh before this header"
#endif

#include <zlib/ZutcpTFO.hh>

namespace Zutcp {

ZtEnumStruct(ZutcpAPI, CxnTimer, int8_t,
  Connect,
  User,
  CloseDrain,
  FinWait,
  AckDelay,
  Loss,
  PTO,
  RTO,
  Persist,
  Pace,
  Idle,
  HeartBeat,
  PMTUD);

ZtEnumStruct(ZutcpAPI, TcpCloseState, int8_t,
  Open, CloseWait, FinWait1, FinWait2, Closing, LastAck, TimeWait, Closed);

struct LinkCloseState {
  uint64_t		error = 0;
  bool			closed = false;
  bool			application = false;
};
struct LinkStateSnapshot {
  uint64_t		error = 0;
  LinkState::T		state = LinkState::Closed;
  bool			closed = false;
  bool			application = false;
};

namespace TxQueueState {
  inline constexpr uint64_t Closed = uint64_t(1)<<63;
  inline constexpr uint64_t CountMask = Closed - 1;
}

struct TCPOrigins {
  uint32_t local = 0, peer = 0;
  bool localKnown = false, peerKnown = false, server = false;
};

struct AckSnapshot {
  uint64_t byteNext = 0;
  TCPOptions options;
};
struct TxAckWork {
  AckSnapshot ack;
  uint64_t gen = 0, previousByteAck = 0;
  AckGrowth growth;
  PktAckBatch byteBatch, fallbackBatch;
  PktSACKBatch sackBatch;
  PktSACKLossBatch sackLossBatch;
  RackLossBatch rackBatch;
  ZuTime ackTime, rtoDeadline;
  RxSpan sacks[TCPOptions::MaxSACKBlocks] = {};
  unsigned sackCount = 0;
  uint64_t sackAckFence = 0;
  bool bytePhase = false, byteProgress = false, ecnValidated = false;
  bool congestionOpened = false, retransmit = false, tcpECE = false;
  bool windowUpdate = false, tcpAck = true, duplicateAck = false;
  bool sackMarked = false, sackPhase = false, fallbackLoss = false;
};

struct TimerState {
  ZmScheduler::Timer *timer;
  ZuTime *deadline;
};

// Bound owner turns while keeping retained recovery work on its existing queues.
enum { TxFlushBatch = 64 };
enum { RecoveryScanBatch = 256 };
enum { SuspiciousDataThreshold = 8 };
enum { DefaultTxQueueMax = 4096 };
enum { MxEOF = 1, MxFINAck = 2, MxFinishPosted = 4 };

template <typename App, typename Impl, typename TxBufAlloc_>
class Connection :
  public Zutcp_::ServerLinks::Node,
  public Ztc::Link,
  public ZmPQRx<Connection<App, Impl, TxBufAlloc_>,
    DataRxPQueue, ZmPQRxGapIgnore<>> {
template <typename, typename>
friend class Server;
friend struct TCPStateTest;

  class TelQueue final : public Ztc::Queue {
  public:
    TelQueue(const Connection *link, Ztc::QueueType::T type) :
      m_link{link}, m_type{type} { }

    TelKey telKey() const override {
      auto linkKey = m_link->telKey();
      return {linkKey.template p<0>(), linkKey.template p<1>(), m_type};
    }
    void telemetry(Ztc::QueueTelemetry &data) const override {
      if (m_type == Ztc::QueueType::Rx)
	m_link->rxQueueTelemetry(data);
      else
	m_link->txQueueTelemetry(data);
    }

  private:
    const Connection			*m_link;
    const Ztc::QueueType::T	m_type;
  };

public:
  using Zutcp_::EndpointLink::mxCxn;

  using Self = Connection<App, Impl, TxBufAlloc_>;
  using TxBufAlloc = TxBufAlloc_;
  auto impl() const { return static_cast<const Impl *>(this); }
  auto impl() { return static_cast<Impl *>(this); }
  unsigned txQueueMax() const { return m_txQueueMax; }
  unsigned dataMPS() const { return unsigned(m_writeMPS); }

  Connection(App *app, bool isServer = false) :
    Connection{app, isServer, ZuID{} << "tcp:" <<
      ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }
  Connection(App *app, bool isServer, ZuID id) :
    m_app{app}, m_id{ZuMv(id)}, m_isServer{isServer} {
    app->linkAdded_(this);
    initLinkState(LinkState::Starting);
  }
  ~Connection() {
    ZmAssert_(!m_cxnSlot);
    clearCallbacks();
    assert(!timersActive());
    app()->linkDeleted_(this, telState(publicLinkState()));
  }

  void clearCallbacks() { }

  App *app() const { return m_app; }
  TelKey telKey() const override {
    return {app()->telKey().template p<1>(), m_id};
  }
  void telemetry(Ztc::LinkTelemetry &data) const override {
    data.hubID = app()->telKey().template p<1>();
    data.id = m_id;
    data.rxCalls = m_rxDiag.datagramsRx.load_();
    data.txCalls = m_txDiag.packetsTx.load_();
    data.rxBytes = m_rxDiag.bytesRx.load_();
    data.txBytes = m_txDiag.bytesTx.load_();
    data.reconnects = 0;
    data.type = Ztc::LinkType::TCP;
    data.state = telState(publicLinkState());
  }
  unsigned allQueues(Ztc::QueueMgr::AllFn fn) const override {
    TelQueue rxQueue{this, Ztc::QueueType::Rx};
    fn(&rxQueue);
    TelQueue txQueue{this, Ztc::QueueType::Tx};
    fn(&txQueue);
    return 2;
  }
  void up() override { impl()->up_(); }
  void down() override { (void)impl()->disconnect(); }

  bool isServer() const { return m_isServer; }
  bool closed() const {
    switch (publicLinkState()) {
      case LinkState::Closing:
      case LinkState::Draining:
      case LinkState::Closed:
	return true;
      default:
	return false;
    }
  }
  bool established() const {
    return publicLinkState() == LinkState::Established;
  }
  uint64_t closeError() const { return m_publicCloseError.load_(); }
  uint64_t rxDataCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxDataCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxDataCreditAvailable() const { return m_rxDataCredit.available(); }
  uint64_t rxDataRetired() const { return m_rxRetired; }


public:
  void rxReset(uint64_t, uint64_t) { }
  void txReset(uint64_t, uint64_t) { }
  void txStopped(uint64_t) { }
  void rxDequeued() { if (rxQueued()) processRx(); }

protected:
  void dataWritable() {
    m_txReady = true;
    updatePersist();
    updateUserTimeout_();
  }
  bool takeTxReady() {
    bool ready = m_txReady;
    m_txReady = false;
    return ready;
  }
  void scheduleData() {
    app()->txRun([link = impl(), gen = dataGen()] {
      if (gen != link->dataGen() || link->disconnecting()) return;
      link->dataWritable();
      link->flushTx();
    });
  }
  void clearWritableData() { m_txReady = false; }


protected:
public:

protected:
  void up_() { }

  void closeState(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP close initiation outside Rx thread", return);
    closingCxnRx();
    m_rxAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_publicCloseError = errorCode;
    closeLinkStateRx();
    publishLinkSnapshotTx();
    cancelTimers();
  }
  void closeStateTx(uint64_t errorCode = 0, bool application = false) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx close initiation outside Tx thread", return);
    m_txAppClose = LinkCloseState{
      .error = errorCode,
      .closed = true,
      .application = application};
    m_txLinkState = LinkState::Closing;
    LinkStateSnapshot snapshot = linkSnapshotTx();
    cancelTimers_();
    app()->rxRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting()) return;
      link->applyCloseSnapshotRx(snapshot);
    });
  }
  void applyCloseSnapshotRx(LinkStateSnapshot snapshot) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP close snapshot apply outside Rx thread", return);
    if (snapshot.state < m_rxLinkState ||
	(snapshot.state == m_rxLinkState && m_rxAppClose.closed))
      return;
    closeDataRx();
    m_rxAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_publicCloseError = snapshot.error;
    switch (snapshot.state) {
      case LinkState::Draining:
	drainLinkStateRx();
	break;
      case LinkState::Closed:
	closedLinkStateRx();
	break;
      default:
	closeLinkStateRx();
	break;
    }
  }

public:
  bool disconnect(bool peer = false) {
    if (m_disconnecting.xch(1)) return false;
    disconnect_(peer, []() { });
    return true;
  }
  template <typename L>
  void disconnect_(bool peer, L &&l) {
    if (app()->nativeTx()) closeTxAdmission();
    m_disconnecting = 1;
    app()->txRun([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->delTimers();
      link->Self::disconnected(peer, ZuMv(l));
    });
  }
  void disconnecting(bool disconnecting) {
    m_disconnecting = unsigned(disconnecting);
  }
  bool disconnecting() const { return m_disconnecting.load_(); }
  void closeForInvalidData(TransportError::T error) {
    ++m_rxDiag.suspectDataCloses;
    m_suspiciousDataClosed = true;
    if (!m_rxAppClose.closed) {
      if (app()->nativeTx())
	abortLocalRx(error, [link = impl()] { link->tcpResetAbort(false); });
      else closeState(error);
    }
  }
  void noteInvalidData(
    TransportError::T error, bool closedData = false, bool immediate = false) {
    ++m_rxDiag.failures;
    (void)closedData;
    if (m_suspiciousDataClosed) return;
    if (immediate || ++m_suspiciousDataFrames >= SuspiciousDataThreshold)
      closeForInvalidData(error);
  }
  Zutcp::LinkInfo linkInfo() const { return linkInfo_(); }

protected:
  bool stageReuseISNRx(uint32_t localISN, uint64_t evidenceID) {
    ZiAssert(rxInvoked(), "Zutcp", (), "TCP reuse staging outside Rx", return false);
    if (!evidenceID || (m_rxOrigins.localKnown && m_rxOrigins.local != localISN) ||
	(m_reuseEvidenceRx && m_reuseISNRx != localISN)) return false;
    m_reuseISNRx = localISN;
    m_reuseEvidenceRx = evidenceID;
    return true;
  }
  bool reusePendingRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (), "TCP reuse state outside Rx", return false);
    return m_reuseEvidenceRx;
  }
  void discardUnadoptedRx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "unadopted owner release outside Rx", return);
    ZmAssert_(!m_cxnSlot);
    app()->txRun([link = ZmRef{impl()}]() mutable {
      link->app()->rxRun([link = ZuMv(link)]() mutable { link = nullptr; });
    });
  }
  bool setTCPOriginsRx(TCPOrigins origins) {
    ZiAssert(rxInvoked(), "Zutcp", (), "TCP origin selection outside Rx", return false);
    if ((m_rxOrigins.localKnown || m_rxOrigins.peerKnown) &&
	m_rxOrigins.server != origins.server) return false;
    if ((m_rxOrigins.localKnown && origins.localKnown && m_rxOrigins.local != origins.local) ||
	(m_rxOrigins.peerKnown && origins.peerKnown && m_rxOrigins.peer != origins.peer))
      return false;
    if (m_rxOrigins.localKnown) {
      origins.local = m_rxOrigins.local;
      origins.localKnown = true;
    }
    if (m_rxOrigins.peerKnown) {
      origins.peer = m_rxOrigins.peer;
      origins.peerKnown = true;
    }
    m_rxOrigins = origins;
    app()->txRun([link = impl(), origins, gen = dataGen()] {
      link->installTCPOriginsTx(origins, gen);
    });
    return true;
  }
  void installTCPOriginsTx(TCPOrigins origins, uint64_t gen) {
    ZiAssert(txInvoked(), "Zutcp", (), "TCP origin installation outside Tx", return);
    if (gen != dataGen() || gen != m_txDataGen || disconnecting()) return;
    if ((m_txOrigins.localKnown || m_txOrigins.peerKnown) &&
	m_txOrigins.server != origins.server) return;
    if ((m_txOrigins.localKnown && (!origins.localKnown || m_txOrigins.local != origins.local)) ||
	(m_txOrigins.peerKnown && (!origins.peerKnown || m_txOrigins.peer != origins.peer)))
      return;
    m_txOrigins = origins;
  }
  bool initTCPOriginsRx(bool server, const ZiSockAddr &local, const ZiSockAddr &remote) {
    if (!app()->nativeTx()) return true;
    TCPOrigins origins = m_rxOrigins;
    origins.server = server;
    if (!origins.localKnown) {
      auto stack = app()->nativeStack();
      if (!stack || !stack->chooseISN(local, remote, origins.local)) return false;
      origins.localKnown = true;
    }
    return setTCPOriginsRx(origins);
  }
  bool initialTCPHeader(TCPHeader &header, bool syn) const {
    if (!m_txOrigins.localKnown || (m_txOrigins.server && !m_txOrigins.peerKnown)) return false;
    header = tcpHeader();
    if (!syn) {
      header.seq = uint32_t(m_txOrigins.local + 1 +
	(m_txTFOEnd && !m_txTFOFinalAckSent ? m_txAckd : m_txNext));
      return m_txOrigins.peerKnown && m_tcpSynSent;
    }
    uint64_t window = m_txRxLimit > m_txRxNext ? m_txRxLimit - m_txRxNext : 0;
    header.window = uint16_t(window > 65535 ? 65535 : window);
    header.options = m_tcpSynSent ? m_txSYNOptions : m_txLocalTCPOptions;
    if (header.options.flags & TCPOptions::TS) {
      auto stack = app()->nativeStack();
      if (!stack || !stack->tcpTimestamp(transportNow(), header.options.tsval)) return false;
      header.options.tsecr = m_txOrigins.server ? m_txRxOptions.tsecr : 0;
    }
    header.seq = m_txOrigins.local;
    header.flags = m_txOrigins.server ? 0x12 : 0x02;
    if (m_txOrigins.server ? m_txECN : app()->ecn())
      header.flags |= m_txOrigins.server ? 0x40 : 0xc0;
    header.ack = m_txOrigins.server ? uint32_t(m_txOrigins.peer + 1 + m_txRxNext) : 0;
    return true;
  }
  static unsigned tcpScale(uint64_t window) {
    unsigned scale = 0;
    while (scale < TCPOptions::MaxScale && window > (uint64_t(65535)<<scale)) ++scale;
    return scale;
  }
  uint64_t tcpReceiveWindow() const {
    uint64_t next = rxByteNext();
    uint64_t window = m_rxDataCredit.limit() > next ? m_rxDataCredit.limit() - next : 0;
    uint64_t maximum = uint64_t(65535)<<m_rxTCPState.localScale;
    return window < maximum ? window : maximum;
  }
  TCPOptions rxTCPOptions() {
    TCPOptions options;
    if ((m_rxTCPState.flags & TCPOptions::TS) && m_rxTSKnown) {
      options.flags = TCPOptions::TS;
      options.tsecr = m_rxTSRecent;
    }
    if (m_rxTCPState.flags & TCPOptionState::SACKSend)
      m_rxSACK.fill(m_rxQueue, rxByteNext(), options);
    return options;
  }
  bool tcpRecentValid() const {
    auto now = transportNow();
    return m_rxTSKnown && (now < m_rxTSRecentTime ||
      now - m_rxTSRecentTime <= ZuTime{24*24*60*60});
  }
  bool receiveECN(const TCPHeader &header, EcnMark::T mark, uint64_t data) {
    if (!m_rxECN || (header.flags & 0x06)) return false;
    bool previous = m_rxECE;
    if (header.flags & 0x80) m_rxECE = false;
    if (data && mark == EcnMark::CE) m_rxECE = true;
    return previous != m_rxECE;
  }
  void receiveECE(uint64_t ack, ZuTime now) {
    if (!m_txECN || !*m_txECTSent) return;
    if (m_congestion.cwnd() <= m_congestion.maxDatagram()) {
      m_txECNHold = now + m_dataRto.timeout(m_rtt, app()->rtoParams());
      m_dataRto.sent(now, m_rtt, app()->rtoParams(), true);
      scheduleDataRto();
    }
    if (m_txECNResponse && ack <= m_txECNEnd) return;
    m_txECNResponse = true;
    m_txECNEnd = m_txNext;
    m_txCWR = true;
    m_congestion.ecnEventAt(uint64_t(m_txECTSent.microsecs()), uint64_t(now.microsecs()));
    noteCongestionReduction();
  }
  void noteCongestionReduction() {
    auto epoch = m_congestion.epoch();
    if (m_txECNEpoch == epoch) return;
    m_txECNEpoch = epoch;
    if (!app()->nativeTx() || !m_txECN) return;
    m_txCWR = m_txECNResponse = true;
    m_txECNEnd = m_txNext;
  }
  bool pacingReady() {
    if (*m_txECNHold && transportNow() < m_txECNHold) { scheduleDataRto(); return false; }
    if (!app()->nativeTx() || !establishedTx() || m_pacer.canSend(nowUS())) return true;
    scheduleCxnTimer("pacing", CxnTimer::Pace, intervalUS(m_pacer.nextSendTime()),
      ZmScheduler::Advance, &m_paceTimer);
    return false;
  }
  void cancelPace() { cancelTimer(CxnTimer::Pace, &m_paceTimer); }
  bool timestampAllowed(const TCPHeader &header) {
    if (!(m_rxTCPState.flags & TCPOptions::TS) || (header.flags & 0x04)) return true;
    if (!(header.options.flags & TCPOptions::TS)) return false;
    uint32_t distance = header.options.tsval - m_rxTSRecent;
    if (tcpRecentValid() && distance >= uint32_t(0x80000000)) {
      replyTCPWindow();
      return false;
    }
    return true;
  }
  bool timestampReceived(const TCPHeader &header) {
    if (!(m_rxTCPState.flags & TCPOptions::TS) ||
	!(header.options.flags & TCPOptions::TS) || (header.flags & 0x04)) return false;
    if (tcpRecentValid() &&
	uint32_t(header.options.tsval - m_rxTSRecent) >= uint32_t(0x80000000)) return false;
    if (!tcpRecentValid() || int32_t(header.seq - uint32_t(m_rxLastAckSent)) <= 0) {
      bool changed = !m_rxTSKnown || header.options.tsval != m_rxTSRecent;
      m_rxTSRecent = header.options.tsval;
      m_rxTSRecentTime = transportNow();
      m_rxTSKnown = true;
      return changed;
    }
    return false;
  }
  void installTCPOptionsRx(const TCPHeader &header) {
    if (m_rxOptionsSet) return;
    TCPOptions local = m_localTCPOptions;
    if (m_rxOrigins.server) {
      if (!(header.options.flags & TCPOptions::WS)) local.flags &= ~TCPOptions::WS;
      if (!(header.options.flags & TCPOptions::TS)) local.flags &= ~TCPOptions::TS;
    }
    m_rxECN = app()->ecn() && (m_rxOrigins.server ?
      (header.flags & 0xc0) == 0xc0 : (header.flags & 0xc0) == 0x40);
    m_rxTCPState = local.negotiate(header.options);
    m_rxOptionsSet = true;
    if (m_rxTCPState.flags & TCPOptions::TS) {
      m_rxTSKnown = true;
      m_rxTSRecent = header.options.tsval;
      m_rxTSRecentTime = transportNow();
    }
    app()->txRun([link = impl(), local, state = m_rxTCPState, ecn = m_rxECN, gen = m_rxDataGen] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting()) return;
      link->m_txTCPState = state;
      link->m_txECN = ecn;
      link->m_txOptionsSet = true;
      link->m_txLocalTCPOptions = local;
    });
  }
  void sentTCPTx(const TCPHeader &header, bool publish = true) {
    if (!app()->nativeTx()) return;
    if ((header.flags & 0x02) && !m_tcpSynSent) {
      m_txSYNOptions = header.options;
      m_txSYNOptions.tsval = m_txSYNOptions.tsecr = 0;
    }
    if (header.options.flags & TCPOptions::TS) {
      if (m_txTSKnown && m_txLastTS == header.options.tsval) {
	m_txAmbiguousTS = header.options.tsval;
	m_txAmbiguousTSKnown = true;
      }
      m_txLastTS = header.options.tsval;
      m_txTSKnown = true;
    }
    if (!publish || !(header.flags & 0x10) || !m_txOrigins.peerKnown) return;
    uint64_t next = tcpPosition(uint32_t(header.ack - m_txOrigins.peer - 1), m_txRxNext);
    auto serial = m_txRxSerial;
    bool dsack = header.options.dsack(header.ack);
    bool finAck = !(header.flags & 0x04);
    app()->rxRun([link = impl(), next, serial, dsack, finAck, gen = m_txDataGen] {
      if (gen != link->dataGen() || gen != link->m_rxDataGen || link->disconnecting()) return;
      if (next > link->m_rxLastAckSent) link->m_rxLastAckSent = next;
      link->m_rxSACK.sent(serial, dsack);
      if (finAck && !link->m_rxAborting && link->finReceived() &&
	  next == link->m_rxState.finalSize() + 1) {
	link->m_mxRxFinish |= MxFINAck;
	link->finishMxRx();
      }
    });
  }
  bool normalizeTCPRx(const TCPHeader &wire, TCPHeader &header) const {
    ZiAssert(rxInvoked(), "Zutcp", (), "TCP normalization outside Rx", return false);
    header = wire;
    if (!app()->nativeTx()) return true;
    if (!m_rxOrigins.localKnown || !m_rxOrigins.peerKnown || (wire.flags & 0x02)) return false;
    header.seq -= uint32_t(m_rxOrigins.peer + 1);
    if (wire.flags & 0x10) header.ack -= uint32_t(m_rxOrigins.local + 1);
    if (header.options.sackCount > TCPOptions::MaxSACKBlocks) header.options.sackCount = 0;
    for (unsigned i = 0; i < header.options.sackCount; ++i) {
      header.options.sack[i].left -= uint32_t(m_rxOrigins.local + 1);
      header.options.sack[i].right -= uint32_t(m_rxOrigins.local + 1);
    }
    return true;
  }
  TCPHeader tcpHeader(const SentFrameRef *ref = nullptr) const {
    uint64_t window = m_txRxLimit > m_txRxNext ? m_txRxLimit - m_txRxNext : 0;
    window >>= m_txTCPState.localScale;
    TCPHeader header{.seq = uint32_t(m_txOrigins.local + 1 + m_txNext),
      .ack = uint32_t(m_txOrigins.peer + 1 + m_txRxNext),
      .window = uint16_t(window > 65535 ? 65535 : window), .flags = 0x10};
    if (ref) {
      header.seq = uint32_t(m_txOrigins.local + 1 + ref->offset);
      if (ref->length) header.flags |= 0x08;
      if (ref->fin) header.flags |= 0x01;
    }
    if (m_txECN && m_txEchoECE) header.flags |= 0x40;
    if (ref && ref->offset >= m_txNext && m_txECN && m_txCWR) header.flags |= 0x80;
    header.options = m_txRxOptions;
    if (header.options.flags & TCPOptions::TS) {
      auto stack = app()->nativeStack();
      if (!stack || !stack->tcpTimestamp(transportNow(), header.options.tsval))
	header.options.flags &= ~TCPOptions::TS;
    }
    for (unsigned i = 0; i < header.options.sackCount; ++i) {
      header.options.sack[i].left += uint32_t(m_txOrigins.peer + 1);
      header.options.sack[i].right += uint32_t(m_txOrigins.peer + 1);
    }
    return header;
  }
  void localWindow_(uint64_t serial, uint64_t next, uint64_t limit,
    const TCPOptions &options = {}, bool ece = false) {
    if (serial <= m_txRxSerial) return;
    m_txRxSerial = serial;
    m_windowDirty |= next != m_txRxNext || limit != m_txRxLimit;
    m_txRxNext = next;
    m_txRxLimit = limit;
    m_txRxOptions = options;
    m_txEchoECE = ece;
  }
  void publishTCPEcho() {
    auto options = rxTCPOptions();
    bool ece = m_rxECE;
    auto next = rxByteNext(), limit = m_rxDataCredit.limit(), serial = ++m_rxWindowSerial;
    app()->txRun([link = impl(), options, next, limit, serial, ece, gen = dataGen()] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting()) return;
      link->localWindow_(serial, next, limit, options, ece);
    });
  }
  void receiveTCP(const TCPHeader &header, bool incomingData = false) {
    if (!app()->nativeTx()) return;
    auto options = rxTCPOptions();
    bool ece = m_rxECE;
    auto next = rxByteNext(), limit = m_rxDataCredit.limit(), serial = ++m_rxWindowSerial;
    app()->txRun([link = impl(), header, options, next, limit, serial, ece,
      incomingData, gen = dataGen()] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting()) return;
      link->localWindow_(serial, next, limit, options, ece);
      if (header.flags & 0x10) link->applyTCP(header, incomingData);
    });
  }
  void applyTCP(const TCPHeader &header, bool incomingData = false) {
    uint64_t next = tcpPosition(header.ack, m_txAckd);
    if (next > m_txNext) return;
    bool current = next >= m_txAckd && (!m_txWindowSet ||
      int32_t(header.seq - m_txWindowSeq) > 0 ||
      (header.seq == m_txWindowSeq && int32_t(header.ack - m_txWindowAck) >= 0));
    bool changed = false;
    if (current) {
      uint64_t window = uint64_t(header.window) <<
	((header.flags & 0x02) ? 0 : m_txTCPState.peerScale);
      uint64_t limit = next + window;
      changed = limit != m_txDataCredit.limit() || m_peerWindowZero != !header.window;
      bool reopened = m_peerWindowZero && header.window;
      m_txWindowSet = true;
      m_txWindowSeq = header.seq;
      m_txWindowAck = header.ack;
      m_peerWindowZero = !header.window;
      m_txDataCredit.advertise(limit);
      if (changed) dataWritable();
      if (reopened && m_txAckd < m_txNext)
	m_dataRto.sent(transportNow(), m_rtt, app()->rtoParams(), true);
      scheduleDataRto();
    }
    bool duplicate = current && !changed && !incomingData && header.window &&
      (header.flags & 0x1f) == 0x10 && next == m_txAckd;
    if ((establishedTx() || m_txTFOEnd) && (next > m_txAckd || changed || duplicate ||
	(m_txTCPState.flags & TCPOptionState::SACKReceive) ||
	(m_txECN && next >= m_txAckd && (header.flags & 0x40)))) {
      TxAckWork work;
	work.ack.byteNext = next;
      work.ack.options = header.options;
      work.tcpAck = true;
      work.duplicateAck = duplicate;
      work.tcpECE = m_txECN && next >= m_txAckd && (header.flags & 0x40);
      work.gen = m_txRuntimeGen;
      work.windowUpdate = changed;
      processAckTx(ZuMv(work));
    } else if (changed) {
      impl()->flowCredit();
      impl()->flushTx();
    }
  }

  bool txReady() const { return m_txReady; }

  bool establishedRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Established;
  }
  bool establishedTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Established;
  }
  bool closingRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Closing;
  }
  bool closingTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Closing;
  }
  bool debugLog() const {
#if defined(Zutcp_DEBUG) && defined(ZiMultiplex_DEBUG)
    return app()->mx()->debug();
#else
    return false;
#endif
  }
  bool drainingRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Draining;
  }
  bool drainingTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Draining;
  }
  bool handshakeStartedRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx lifecycle access outside Rx thread", return false);
    return m_rxLinkState == LinkState::Handshaking ||
      m_rxLinkState == LinkState::Established;
  }
  bool handshakeStartedTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx lifecycle access outside Tx thread", return false);
    return m_txLinkState == LinkState::Handshaking ||
      m_txLinkState == LinkState::Established;
  }
  bool linkClosedRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx close access outside Rx thread", return true);
    return m_rxAppClose.closed;
  }
  bool linkClosedTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx close access outside Tx thread", return true);
    return m_txAppClose.closed;
  }
  uint64_t closeErrorTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx close error access outside Tx thread", return 0);
    return m_txAppClose.error;
  }
  uint64_t closeErrorRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx close error access outside Rx thread", return 0);
    return m_rxAppClose.error;
  }
  LinkRxDiag rxDiagSnap() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx diagnostic snapshot outside Rx thread", return {});
    return m_rxDiag;
  }
  LinkTxDiag txDiagSnap() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx diagnostic snapshot outside Tx thread", return {});
    return txDiag();
  }
  template <typename L>
  void diag(L &&l) const {
    auto link = const_cast<Connection *>(this)->impl();
    if (rxInvoked()) {
      diagRx(ZuFwd<L>(l));
      return;
    }
    app()->rxRun([link, l = ZuFwd<L>(l)]() mutable {
      link->diagRx(ZuMv(l));
    });
  }
  template <typename L>
  void diagRx(L &&l) const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP Rx diagnostic snapshot outside Rx thread", return);
    auto link = const_cast<Connection *>(this)->impl();
    LinkRxDiag rx = m_rxDiag;
    app()->txRun([link, rx, l = ZuFwd<L>(l)]() mutable {
      LinkDiag diag{rx, link->txDiagSnap()};
      l(diag);
    });
  }
  LinkDiag diag_() const {
    return {m_rxDiag, txDiag()};
  }
  template <typename L>
  void pathDiag(L &&l) const {
    if (txInvoked()) {
      PathDiag diag = pathDiag_();
      l(diag);
      return;
    }
    auto link = const_cast<Connection *>(this)->impl();
    app()->txRun([link, l = ZuFwd<L>(l)]() mutable {
      PathDiag diag = link->pathDiag_();
      l(diag);
    });
  }
  PathDiag pathDiag_() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP path diagnostic snapshot outside Tx thread", return {});
    return m_path.diag();
  }
  bool pathValidated() const { return m_path.validated(); }
  unsigned activePathMaxPayload_() const { return m_path.activeMaxPayload(); }
  const ZiSockAddr &activePathRemote() const { return m_path.remote(); }

  bool requeueDataRef(const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Data ||
	(ref.range.storage && ref.range.storage->native && !ref.range.storage->data))
      return false;
    if (!txStillUnackd(ref.offset, ref.length, ref.fin) &&
	!recordTxUnackd(ref.offset, ref.length, ref.fin)) return false;
    return requeueRetransmit(ref);
  }
  bool frameStillUnackd(const SentFrameRef &ref) const {
    return ref.kind == SentFrameKind::Data && txStillUnackd(ref.offset, ref.length, ref.fin);
  }
  bool ackTxFrame(const SentFrameRef &ref) {
    return ackTxUnackd(ref.offset, ref.length, ref.fin, &ref.range);
  }

  static bool clipDataRef(
    SentFrameRef &ref, uint64_t first, uint64_t end) {
    constexpr uint64_t maxTxRangeField = UINT32_MAX;
    uint64_t dataEnd = ref.offset + ref.length;
    if (dataEnd < ref.offset) return false;
    uint64_t oldOffset = ref.offset;
    uint64_t oldLength = ref.length;
    uint64_t oldRangeOffset = ref.range.offset;
    if (first < oldOffset) first = oldOffset;
    if (first > dataEnd) first = dataEnd;
    uint64_t dataFirst = first;
    uint64_t dataLimit = end < dataEnd ? end : dataEnd;
    uint64_t newLength = dataLimit > dataFirst ? dataLimit - dataFirst : 0;
    if (newLength > maxTxRangeField) return false;
    bool newFin = ref.fin && end > dataEnd && dataLimit == dataEnd;
    ref.offset = dataFirst;
    ref.length = newLength;
    ref.fin = newFin;
    ref.range.dataOffset = dataFirst;
    ref.range.length = uint32_t(newLength);
    if (dataFirst >= oldOffset && dataFirst <= oldOffset + oldLength) {
      uint64_t advance = dataFirst - oldOffset;
      if (oldRangeOffset + advance <= maxTxRangeField)
	ref.range.offset = uint32_t(oldRangeOffset + advance);
    }
    return newLength || newFin;
  }
  bool clipDataRetx(SentFrameRef &ref, SentFrameRef &tail) {
    tail = {};
    if (ref.kind != SentFrameKind::Data) return false;
    if (ref.range.storage && ref.range.storage->pool)
      return txStillUnackd(ref.offset, ref.length, ref.fin);
    uint64_t n = ref.length + (ref.fin ? 1 : 0);
    if (!n || n < ref.length) return false;
    uint64_t first = 0;
    uint64_t end = 0;
    bool found = false;
    m_txUnackd.spans(
      ref.offset, n, [&found, &first, &end](const auto &span) {
	first = span.key();
	end = span.key() + span.length();
	found = end >= first;
	return false;
      });
    if (!found) return false;
    uint64_t oldEnd = ref.offset + n;
    if (oldEnd < ref.offset) return false;
    if (end < oldEnd) {
      tail = ref;
      if (!clipDataRef(tail, end, oldEnd)) tail = {};
    }
    return clipDataRef(ref, first, end);
  }

  void resetDiag() {
    m_rxDiag = {};
    m_txDiag = {};
  }
  LinkTxDiag txDiag() const {
    LinkTxDiag diag{m_txDiag};
    diag.sentPackets = m_txPkts.count();
    diag.pktBytesInFlight = m_txPkts.bytesInFlight();
    diag.retransmitPending = m_txPkts.retransmitPending();
    diag.retransmittable = m_txPkts.retransmittable();
    diag.bytesInFlight = m_congestion.bytesInFlight();
    return diag;
  }
  void updateCongestionDiag() {
    m_txDiag.congestionWindow = m_congestion.cwnd();
    m_txDiag.congestionSSThresh = m_congestion.ssthresh();
    m_txDiag.bytesInFlight = m_congestion.bytesInFlight();
  }
  unsigned congestionAllowance() const {
    uint64_t cwnd = m_congestion.cwnd();
    uint64_t inFlight = m_congestion.bytesInFlight();
    if (app()->nativeTx()) {
      uint64_t sacked = m_txPkts.sackedFlightBytes();
      inFlight = sacked < inFlight ? inFlight - sacked : 0;
    }
    if (inFlight >= cwnd) return 0;
    uint64_t allowance = cwnd - inFlight;
    return allowance > uint64_t(unsigned(-1)) ?
      unsigned(-1) : unsigned(allowance);
  }
  unsigned localMSSOfferRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (), "local MSS selection outside Rx", return 0);
    unsigned configured = app()->maxPayload();
    return m_rxRoutePayload < configured ? m_rxRoutePayload : configured;
  }
  unsigned defaultPeerMSS() const {
    return m_path.local().type() == ZiIP::V6 ? 1220 : 536;
  }
  unsigned tcpPayloadLimit(unsigned options, bool syn = false, bool probe = false) const {
    unsigned limit = probe ? m_path.ceiling() : m_path.activeMaxPayload();
    if (!app()->nativeTx()) return limit;
    unsigned peer = limit;
    if (m_txOptionsSet)
      peer = m_txTCPState.flags & TCPOptions::MSS ? m_txTCPState.peerMSS : defaultPeerMSS();
    else if (syn) peer = defaultPeerMSS();
    if (peer < limit) limit = peer;
    return options < limit ? limit - options : 0;
  }
  unsigned tcpRuntimeOptions() const {
    if (!app()->nativeTx() || !m_txOptionsSet) return 0;
    if (m_txTCPState.flags & TCPOptionState::SACKSend) return 36;
    return m_txTCPState.flags & TCPOptions::TS ? 12 : 0;
  }
  unsigned tcpAppPayload() const { return tcpPayloadLimit(tcpRuntimeOptions()); }
  void tcpBudgetFailedRx() {
    if (disconnecting() || m_rxAborting) return;
    abortLocalRx(0, [link = impl()] { link->tcpResetAbort(false); });
  }
  void failTCPBudget() {
    ZiAssert(txInvoked(), "Zutcp", (), "MSS failure outside Tx", return);
    if (m_tcpBudgetFailed || disconnecting()) return;
    m_tcpBudgetFailed = true;
    m_writeMPS = 0;
    closeTxAdmission();
    ++m_txDiag.failures;
    app()->rxRun([link = impl(), gen = m_txDataGen] {
      if (gen != link->m_rxDataGen || link->disconnecting()) return;
      link->tcpBudgetFailedRx();
    });
  }
  bool tcpOriginalsFit(unsigned limit) {
    auto queued = m_txRanges.citer();
    while (auto node = queued())
      if (node->data().native && node->data().payloadLength > limit) return false;
    auto packets = m_txPkts.txQueue()->citer();
    while (auto node = packets()) {
      const auto &packet = node->data();
      for (unsigned i = 0, n = packet.framesUsed(); i < n; ++i) {
	const auto &frame = packet.frame(i);
	const auto &source = frame.range.storage;
	if (frame.kind == SentFrameKind::Data && source && source->native &&
	    source->data && source->payloadLength > limit) return false;
      }
    }
    return true;
  }
  bool publishWriteMPS() {
    ZiAssert(txInvoked(), "Zutcp", (), "MPS publication outside Tx", return false);
    unsigned limit = tcpAppPayload();
    if (app()->nativeTx() && (m_tcpBudgetFailed || !limit || limit == unsigned(-1) ||
	(m_txPublishedMPS && limit < m_txPublishedMPS) ||
	(limit < unsigned(m_writeMPS) && !tcpOriginalsFit(limit)))) {
      failTCPBudget();
      return false;
    }
    m_writeMPS = limit;
    if (app()->nativeTx() && establishedTx() && limit > m_txPublishedMPS)
      m_txPublishedMPS = limit;
    return true;
  }
  bool prepareMPSRx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "MSS establishment check outside Rx", return false);
    if (!app()->nativeTx() || m_mpsReadyRx) return true;
    if (m_mpsPendingRx) return false;
    m_mpsPendingRx = true;
    app()->txRun([link = impl(), gen = m_rxDataGen] {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      bool ready = link->m_txOptionsSet && link->publishWriteMPS();
      if (!ready) link->failTCPBudget();
      else link->m_txPublishedMPS = link->dataMPS();
      link->app()->rxRun([link, gen, ready] {
	if (gen != link->m_rxDataGen || link->disconnecting()) return;
	link->m_mpsPendingRx = false;
	if (!ready || !link->dataMPS()) return;
	link->m_mpsReadyRx = true;
	link->markEstablished();
      });
    });
    return false;
  }
  bool tcpPacketFits(const TCPHeader &header, unsigned bytes, bool probe = false) {
    if (!app()->nativeTx()) return true;
    int options = header.options.encodedLength(header.flags & 0x02);
    if (options >= 0 && bytes <= tcpPayloadLimit(unsigned(options), header.flags & 0x02, probe) &&
	(!m_tcpBudgetFailed || (header.flags & 0x04))) return true;
    if (!probe && !(header.flags & 0x04)) failTCPBudget();
    return false;
  }
  static bool reserveTCPOptions(PacketBuf *buf) {
    int n = buf->tcp.options.encodedLength(buf->tcp.flags & 0x02);
    if (n < 0 || unsigned(n) > txPktCapacity(buf)) return false;
    buf->skip += unsigned(n);
    return true;
  }
  PktBudget sendBudget() const {
    PktBudget budget;
    budget.pmtu = tcpAppPayload();
    budget.congestion = congestionAllowance();
    if (m_tlpSending && m_tlpAllowance && !m_tlpReserved && !budget.congestion)
      budget.congestion = budget.pmtu;
    budget.flow = m_txDataCredit.available();
    return budget;
  }
  void resetPath() {
    ZiAssert(txInvoked(), "Zutcp", (), "path reset outside Tx", return);
    m_path = m_isServer ?
      Path::server(ZiSockAddr{}, ZiSockAddr{}) :
      Path::client(ZiSockAddr{}, ZiSockAddr{});
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.peerMaxPayload(app()->maxPayload());
    publishWriteMPS();
    if (!m_isServer) m_path.validated();
  }
  bool initClientPath(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    m_closedKeyRx = {local, remote};
    m_rxRoutePayload = limit;
    if (!initTCPOriginsRx(false, local, remote)) return false;
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote), limit, gen = dataGen()
    ]() mutable {
      if (link->disconnecting() || gen != link->dataGen() || gen != link->m_txDataGen) return;
      link->initClientPathTx(ZuMv(local), ZuMv(remote), limit);
    });
    return true;
  }
  void initServerPath(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    m_closedKeyRx = {local, remote};
    m_rxRoutePayload = limit;
    if (app()->nativeTx()) m_rxOrigins.server = true;
    app()->txRun([
      link = impl(),
      local = ZuMv(local),
      remote = ZuMv(remote), limit, gen = dataGen()
    ]() mutable {
      if (link->disconnecting() || gen != link->dataGen() || gen != link->m_txDataGen) return;
      link->initServerPathTx(ZuMv(local), ZuMv(remote), limit);
    });
  }
  void initClientPathTx(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP client path initialization outside Tx thread", return);
    m_path = Path::client(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.routePayload(limit);
    m_path.peerMaxPayload(app()->maxPayload());
    if (app()->nativeTx()) m_path.useRoutePayload();
    publishWriteMPS();
    m_path.validated();

  }
  void initServerPathTx(ZiSockAddr local, ZiSockAddr remote, unsigned limit = BufSize) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP server path initialization outside Tx thread", return);
    m_path = Path::server(ZuMv(local), ZuMv(remote));
    m_path.configuredMaxPayload(app()->maxPayload());
    m_path.routePayload(limit);
    m_path.peerMaxPayload(app()->maxPayload());
    if (app()->nativeTx()) m_path.useRoutePayload();
    publishWriteMPS();

  }
  void validatePath() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->validatePathTx();
    });
  }
  void validatePathTx() {
    m_path.validated();

  }
  void recordPathRx(ZiSockAddr remote, unsigned bytes) {
    app()->txRun([
      link = impl(),
      remote = ZuMv(remote),
      bytes
    ]() mutable {
      if (link->disconnecting()) return;
      link->recordPathRxTx(ZuMv(remote), bytes);
    });
  }
  void recordPathRxTx(ZiSockAddr remote, unsigned bytes) {
    if (remote && remote != m_path.remote()) return;
    m_path.received(bytes);

  }
#ifdef Zutcp_DEBUG
  void forceActivePathMTU(unsigned size) {
    m_path.startProbe(size);
    m_path.probeAckd();
    publishWriteMPS();
  }
  bool startPMTUDProbe(unsigned size) {
    return m_path.startProbeChecked(size);
  }
  void ackPMTUDProbe(unsigned size) {
    onPMTUDProbeAckd(size);
  }
  void losePMTUDProbe(unsigned size) {
    onPMTUDProbeLost(size);
  }
  void expirePMTUDProbe() { pmtudExpired(); }
  unsigned pathProbeSize() const { return m_path.probeSize(); }
  bool pathProbeRetryPending() const { return m_path.probeRetryPending(); }

#endif


  void resetLink() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP runtime reset outside Rx thread", return);
    ZmAssert_(!m_cxnSlot);
    disconnecting(false);
    uint64_t gen = ++m_dataGen;
    ++m_rxDataGen;
    closeDataRx();
    Rx::rxReset(0);
    m_rxState = {};
    m_rxOrigins = {};
    m_localTCPOptions = {};
    m_rxTCPState = {};
    m_rxSACK.reset();
    m_rxOptionsSet = m_rxTSKnown = false;
    m_rxTFOEnd = 0;
    m_rxTFOAllow = m_rxTFONotified = m_rxTFOValidated = m_rxTFOPending = false;
    m_rxECN = m_rxECE = false;
    m_rxTSRecent = 0;
    m_rxTSRecentTime = ZuTime{};
    m_rxLastAckSent = 0;
    m_rxRoutePayload = BufSize;
    m_mpsPendingRx = m_mpsReadyRx = false;
    m_reuseISNRx = 0;
    m_reuseEvidenceRx = 0;
    m_rxSynAcked = false;
    m_rxSentNext = m_rxSentAck = 0;
    m_closedKeyRx = {};
    m_closedSavedRx = false;
    m_mxRxFinish = 0;
    m_mxRxClosing = false;
    m_rxAborting = false;
    m_rxDelivered = m_rxAdmitted = 0;
    m_resetReceived = m_stopReceived = false;
    m_rxAppError = 0;
    m_rxError = DataError::None;
    cancelTimers();
    resetLinkStateRx();
    m_writeCredit = 0;
    m_rxDataCredit = {};
    m_rxRetired = 0;
    m_rxWindowSerial = 0;
    app()->txInvoke(impl(), [link = impl(), gen]() mutable {
      if (link->disconnecting()) return link;
      link->resetTx_(gen);
      return link;
    });
  }
  void resetTx_() { resetTx_(++m_dataGen); }
  void resetTx_(uint64_t gen) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP Tx runtime reset outside Tx thread", return);
    if (gen != dataGen()) return;
    ++m_txRuntimeGen;
    cancelPersist();
    m_txDataGen = gen;
    m_txOrigins = {};
    m_txLocalTCPOptions = m_txSYNOptions = {};
    m_txTCPState = {};
    m_txOptionsSet = false;
    m_txECN = m_txEchoECE = m_txCWR = m_txECNResponse = false;
    m_txECNEnd = 0;
    m_txECTSent = m_txECNHold = ZuTime{};
    m_txECNEpoch = 0;
    m_pacer.reset();
    cancelPace();
    m_txRxOptions = {};
    m_txTSKnown = m_txAmbiguousTSKnown = false;
    m_txLastTS = m_txAmbiguousTS = 0;
    m_tcpSynSent = false;
    m_txTFOEnd = 0;
    m_txTFOFallback = m_txTFOAccepted = m_txTFOFinalAckSent = false;
    unsigned queued = txRangeCount();
    m_txRanges.reset(0);
    m_txQueueCount -= queued;
    // New generation; old handoffs still retire their count.
    m_txQueueCount &= TxQueueState::CountMask;
    m_txUnackd.clean();
    m_txBytes = m_txBufferedBytes = m_txFrameMax = 0;
    m_txNext = m_txAckd = m_txRttFence = m_txRtoEnd = 0;
    m_txRxNext = m_txRxLimit = m_txRxSerial = 0;
    m_windowDirty = m_txWindowSet = m_peerWindowZero = false;
    m_txWindowSeq = m_txWindowAck = 0;
    m_dataRto.reset();
    m_fin = m_finDequeued = m_finAckd = m_txCompleted = false;
    m_tcpCloseState = TcpCloseState::Open;
    m_mxPeerFinished = m_mxFinishing = m_mxClosePeer = false;
    m_tcpResetSent = m_tcpResetPeer = false;
    m_resetSent = m_resetAckd = m_stopSent = m_stopAckd = false;
    m_txAppError = 0;
    m_txError = DataError::None;
    m_tcpBudgetFailed = false;
    m_txPublishedMPS = 0;
    m_txReady = false;
    m_txDataCredit = {};
    m_txAppClose = {};
    m_txLinkState = LinkState::Starting;
    m_lossTimerOut = ZuTime{};
    m_ptoTimerOut = ZuTime{};
    m_idleTimeout = ZuTime{};
    m_idleTimerOut = ZuTime{};
    m_idleBase = ZuTime{};
    m_idleAckElicitingSent = false;
    m_heartBeatTimerOut = ZuTime{};
    m_heartBeatBase = ZuTime{};
    resetPkts();
    m_congestion = NewReno{app()->maxPayload()};
    resetPath();
    updateCongestionDiag();
  }

  void closeLink(uint64_t = 0) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP link close outside Rx thread", return);
    closeDataRx();
    cancelTimers();
    closeLinkStateRx();
    publishLinkSnapshotTx();
  }

  void endpointReady() { ++m_rxDiag.endpointReady; }
  void endpointFailure() { ++m_rxDiag.failures; }
  void packetParseFailure() { ++m_rxDiag.failures; }

  void dataFrame(uint64_t, ZuBSpan, bool) { }

  void flowCredit() { }
  void flowBlocked(uint64_t) { }
  void queueTxFlush() { }

  void disconnected(bool peer) {
    disconnected(peer, []() { });
  }
  template <typename L>
  void disconnected(bool peer, L &&l) {
    app()->rxRun([link = impl(), peer, l = ZuFwd<L>(l)]() mutable {
      link->Self::disconnected_0(peer, ZuMv(l));
    });
  }
  template <typename L>
  void disconnected_0(bool peer, L &&l) {
    auto mx = app()->mx();
    if (mx && app()->ioActive() && app()->txThread()) {
      ZiAssert(rxInvoked(), "Zutcp", (),
	"TCP link disconnect completion outside Rx thread", return);
      closingCxnRx();
      auto stack = app()->nativeStack();
      ClosedTupleKey key = impl()->closedKeyRx();
      bool remember = stack && !m_closedSavedRx && !reusePendingRx() &&
	key.p<0>().ip() && key.p<0>().port() &&
	key.p<1>().ip() && key.p<1>().port();
      ClosedTuple record;
      if (remember) {
	record = closedSnapshotRx(key);
	m_closedSavedRx = true;
      }
      closeDataRx();
      closedLinkStateRx();
      app()->retireLinkRoutes(impl());
      LinkStateSnapshot snapshot = linkSnapshotRx();
      app()->txRun([
	link = impl(), peer, snapshot, stack, record, remember, l = ZuFwd<L>(l)
      ]() mutable {
	if (remember) link->closedSnapshotTx(record);
	link->applyLinkSnapshotTx(snapshot);
	link->app()->mx()->txRun([
	  link, peer, stack, record, remember, l = ZuMv(l)
	]() mutable {
	  link->app()->rxRun([
	    link, peer, stack, record, remember, l = ZuMv(l)
	  ]() mutable {
	    if (remember) stack->rememberClosed(record);
	    link->Self::disconnected_(peer);
	    l();
	  });
	});
      });
      return;
    }
    // No shard remains after multiplexer finalization.
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(LinkState::Closed);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_rxLinkState = LinkState::Closed;
    m_txLinkState = LinkState::Closed;
    m_publicLinkState = unsigned(LinkState::Closed);
    l();
  }
  void disconnected_(bool peer) {
    app()->disconnected(impl(), peer);
  }

  Zutcp::LinkInfo linkInfo_() const {
    LinkInfo info;
    info.vantage = m_isServer ? Vantage::Server : Vantage::Client;
    if (app()->rxInvoked()) {
      info.local = m_closedKeyRx.template p<0>();
      info.remote = m_closedKeyRx.template p<1>();
    } else if (app()->txInvoked()) {
      info.local = m_path.local();
      info.remote = m_path.remote();
    }
    return info;
  }


  template <typename AppLike>
  void configLocalParams(AppLike *app) {
    m_rxDataCredit.set(app->maxData());
    m_writeCredit = app->maxPayload();
    m_localTCPOptions.flags = TCPOptions::MSS |
      (app->sackPermitted() ? TCPOptions::SACKPermitted : 0) |
      (app->windowScale() ? TCPOptions::WS : 0) | (app->timestamps() ? TCPOptions::TS : 0);
    m_localTCPOptions.mss = localMSSOfferRx();
    m_localTCPOptions.scale = uint8_t(tcpScale(app->maxData()));
    app->txRun([link = impl(), options = m_localTCPOptions, gen = dataGen()] {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      link->m_txLocalTCPOptions = options;
      link->m_txRxLimit = link->app()->maxData();
      link->m_txFrameMax = link->app()->maxPayload();
    });
  }

  bool readyToEstablish() const {
    return m_rxLinkState == LinkState::Handshaking && m_rxSynAcked;
  }

  void establish() {
    if (!establishState()) return;
    if (auto stack = app()->nativeStack()) stack->establishedCxn(m_cxnSlot);
    startIdleTimer();
    startHeartBeatTimer();
    ++m_rxDiag.handshakeComplete;
  }

  void resetPkts() {
    cancelTimers();
    m_txID = 0;
    m_txPkts.clear();
    resetSACKRecovery();
    m_txDSACKReports = 0;
    m_rack = {};
    m_rtt = {};
    m_tlp.reset();
    m_tlpSending = m_tlpReserved = m_tlpTimerSelected = false;
    m_tlpAllowance = 0;
    m_rtoSending = m_rtoReserved = false;
    m_rtoAllowance = 0;
    m_fastPendingEpoch = m_fastAcceptedEpoch = U64Null;
    m_fastSending = m_fastReserved = false;
    m_ackPending = m_ackPostedRx = false;
    m_txSynAcked = false;
    m_synRetries = 0;
    m_synSent = m_synDeadline = ZuTime{};
  }

  void closeDataRx() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection data Rx close outside Rx thread", return);
    closeRx();
    m_rxAccount = nullptr; // held slots keep their retired generation account
  }

  bool txPending() const { return !resetSent() && (txRangeCount() || finReady()); }


  uint64_t nowUS() const {
    return uint64_t(transportNow().microsecs());
  }


  uint64_t rxByteNext() const {
    uint64_t next = m_rxState.delivered();
    return next + unsigned(m_rxState.finSeen() && m_rxState.finalSize() == next);
  }

  bool clipTCPData(ByteData &frame, uint32_t seq) const {
    uint64_t next = rxByteNext();
    int64_t first = int32_t(seq - uint32_t(next));
    int64_t end = first + frame.length;
    uint64_t window = tcpReceiveWindow();
    if (!window || first >= int64_t(window) || end + unsigned(frame.fin) <= 0)
      return false;
    unsigned head = first < 0 ? unsigned(-first) : 0;
    unsigned tail = end > int64_t(window) ? unsigned(end - window) : 0;
    frame.fin = frame.fin && end >= 0 && end < int64_t(window);
    frame.offset = first < 0 ? next : next + first;
    frame.length -= head + tail;
    frame.payload = {head ? frame.payload.data() + head : frame.payload.data(),
      unsigned(frame.length)};
    return frame.length || frame.fin;
  }


  bool tcpSequenceAllowed(uint32_t seq, uint64_t length) const {
    uint64_t next = rxByteNext();
    uint64_t window = tcpReceiveWindow();
    int64_t first = int32_t(seq - uint32_t(next));
    if (!length) return window ? first >= 0 && uint64_t(first) < window : !first;
    return window && first < int64_t(window) && first + int64_t(length) > 0;
  }

  RxSpan duplicateTCP(const TCPHeader &header, uint64_t length) const {
    RxSpan duplicate;
    if (!length || !(m_rxTCPState.flags & TCPOptionState::SACKSend)) return duplicate;
    uint64_t first = tcpPosition(header.seq, rxByteNext());
    if (ZuNull(first) || length > U64Null - first) return duplicate;
    uint64_t last = first + length;
    uint64_t next = m_rxState.delivered();
    if (first < next) return {first, last < next ? last : next};
    m_rxQueue.spans(first, length, [&duplicate](const auto &span) {
      duplicate = {span.key(), span.key() + span.length()};
      return false;
    });
    return duplicate;
  }
  void noteDuplicateTCP(const RxSpan &duplicate) {
    if (duplicate.last > duplicate.first)
      m_rxSACK.duplicate(duplicate.first, duplicate.last, ++m_rxWindowSerial);
  }

  void receiveTCPReset() {
    if (m_rxAborting || disconnecting()) return;
    bool established = establishedRx();
    closeTxAdmission();
    closingCxnRx();
    m_rxAborting = true;
    closeDataRx();
    cancelTimers();
    app()->txRun([link = impl(), gen = m_rxDataGen, established] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting()) return;
      link->m_tcpResetPeer = true;
      link->closeStateTx();
      link->tcpResetAbort(true, established);
    });
  }

  void replyTCPWindow() {
    auto next = rxByteNext(), limit = m_rxDataCredit.limit();
    auto serial = ++m_rxWindowSerial;
    auto options = rxTCPOptions();
    bool ece = m_rxECE;
    app()->txRun([link = impl(), next, limit, serial, options, ece, gen = m_rxDataGen] {
      if (gen != link->dataGen() || gen != link->m_txDataGen || link->disconnecting()) return;
      link->localWindow_(serial, next, limit, options, ece);
      // A rejected persist probe needs a reply even when the edge is unchanged.
      link->m_windowDirty = true;
      link->flushTx();
    });
  }

  void scheduleAckDelayTimer(ZuTime out) {
    scheduleCxnTimer(
      "ACK delay", CxnTimer::AckDelay,
      out, ZmScheduler::Advance, &m_ackDelayTimer);
  }
  void cancelAckDelayTimer() { cancelTimer(CxnTimer::AckDelay, &m_ackDelayTimer); }

  void scheduleLossTimer() {
    ZuTime out = nextLossTime();
    if (!*out || linkClosedTx()) { cancelLossTimer(); return; }
    cancelPTO_();
    scheduleCxnTimer("RACK", CxnTimer::Loss, out, ZmScheduler::Update, &m_lossTimer);
  }
  ZuTime nextLossTime() const {
    return (m_txTCPState.flags & TCPOptionState::SACKReceive) && !m_peerWindowZero ?
      m_rack.deadline : ZuTime{};
  }
  bool lossPending() const { return *nextLossTime(); }
  void cancelLossTimer() {
    m_lossTimerOut = ZuTime{};
    cancelTimer(CxnTimer::Loss, &m_lossTimer);
  }

  void schedulePTO() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP PTO schedule before app initialization", return);
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->schedulePTO_();
    });
  }

  void schedulePMTUD() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP PMTUD schedule before app initialization", return);
    app()->txInvoke([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->schedulePMTUD_();
    });
  }

  void schedulePTO_() {
    if (linkClosedTx()) return;
    if (!m_txSynAcked && *m_synDeadline) {
      schedulePTOTimer(m_synDeadline);
      return;
    }
    if (*m_rack.deadline || !selectTLPDeadline()) cancelPTO_();
  }

  void schedulePTOTimer(ZuTime out) {
    scheduleCxnTimer(
      "PTO", CxnTimer::PTO,
      out, ZmScheduler::Update, &m_ptoTimer);
  }

  void cancelPTO() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP PTO cancel before app initialization", return);
    cancelPTO_();
  }

  void cancelPTO_() {
    m_tlp.cancel();
    m_tlpTimerSelected = false;
    m_ptoTimerOut = ZuTime{};
    app()->mx()->cancel(&m_ptoTimer);
  }

  void startIdleTimer() {
    ZuTime timeout = intervalMS(app()->maxIdleTimeout());
    app()->txRun([link = impl(), timeout]() mutable {
      if (link->disconnecting()) return;
      link->startIdleTimerTx(timeout);
    });
  }
  void startIdleTimerTx(ZuTime timeout) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP idle timer start outside Tx thread", return);
    m_idleTimeout = timeout;
    m_idleBase = transportNow();
    m_idleAckElicitingSent = false;
    scheduleIdleTimer();
  }
  ZuTime effectiveIdleTimeout() const { return m_idleTimeout; }
  ZuTime idleDeadline() const {
    ZuTime timeout = effectiveIdleTimeout();
    if (!timeout) return {};
    return m_idleBase + timeout;
  }
  void scheduleIdleTimer() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP idle timer schedule outside Tx thread", return);
    if (linkClosedTx() || !establishedTx() || !m_idleTimeout) {
      cancelIdleTimer();
      return;
    }
    ZuTime out = idleDeadline();
    if (!out) {
      cancelIdleTimer();
      return;
    }
    ZuTime now = transportNow();
    if (out <= now) out = now + app()->rtoParams().granularity;
    if (m_idleTimer && m_idleTimerOut == out) return;
    m_idleTimerOut = out;
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void scheduleIdleTimer(ZuTime out) {
    scheduleCxnTimer(
      "idle", CxnTimer::Idle,
      out, ZmScheduler::Update, &m_idleTimer);
  }
  void cancelIdleTimer() {
    m_idleTimerOut = ZuTime{};
    cancelTimer(CxnTimer::Idle, &m_idleTimer);
  }
  void startHeartBeatTimer() {
    ZuTime interval = app()->heartBeat();
    app()->txRun([link = impl(), interval]() mutable {
      if (link->disconnecting()) return;
      link->startHeartBeatTimerTx(interval);
    });
  }
  void startHeartBeatTimerTx(ZuTime interval) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP heartbeat timer start outside Tx thread", return);
    if (!*interval) {
      cancelHBTimer();
      return;
    }
    m_heartBeatBase = transportNow();
    scheduleHBTimer();
  }
  ZuTime heartBeatDeadline() const {
    ZuTime interval = app()->heartBeat();
    if (!*interval) return {};
    return m_heartBeatBase + interval;
  }
  void scheduleHBTimer() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP heartbeat timer schedule outside Tx thread", return);
    ZuTime interval = app()->heartBeat();
    if (linkClosedTx() || !establishedTx() || !*interval) {
      cancelHBTimer();
      return;
    }
    if (!*m_heartBeatBase) m_heartBeatBase = transportNow();
    ZuTime out = heartBeatDeadline();
    if (!*out) {
      cancelHBTimer();
      return;
    }
    ZuTime now = transportNow();
    if (out <= now) out = now + app()->rtoParams().granularity;
    if (m_heartBeatTimer && m_heartBeatTimerOut == out) return;
    m_heartBeatTimerOut = out;
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_heartBeatTimer);
  }
  void scheduleHBTimer(ZuTime out) {
    scheduleCxnTimer(
      "heartbeat", CxnTimer::HeartBeat,
      out, ZmScheduler::Update, &m_heartBeatTimer);
  }
  void cancelHBTimer() {
    m_heartBeatTimerOut = ZuTime{};
    cancelTimer(CxnTimer::HeartBeat, &m_heartBeatTimer);
  }
  void noteHBTx() {
    if (!*app()->heartBeat()) return;
    m_heartBeatBase = transportNow();
    scheduleHBTimer();
  }
  void notePeerPkt() {
    app()->txRun([link = impl()]() mutable {
      if (link->disconnecting()) return;
      link->notePeerPktTx();
    });
  }
  void notePeerPktTx() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP peer activity outside Tx thread", return);
    if (linkClosedTx() || !establishedTx()) return;
    if (m_idleTimeout) {
      m_idleBase = transportNow();
      m_idleAckElicitingSent = false;
      scheduleIdleTimer();
    }
    noteHBTx();
  }
  void noteAckElicitTx() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP ack-eliciting send activity outside Tx thread", return);
    if (linkClosedTx() || !establishedTx()) return;
    if (m_idleTimeout) {
      if (!m_idleAckElicitingSent) {
	m_idleBase = transportNow();
	m_idleAckElicitingSent = true;
      }
      scheduleIdleTimer();
    }
    noteHBTx();
  }

  void schedulePMTUDTimer(ZuTime out) {
    scheduleCxnTimer(
      "PMTUD", CxnTimer::PMTUD,
      out, ZmScheduler::Advance, &m_pmtudTimer);
  }
  void cancelPMTUDTimer() { cancelTimer(CxnTimer::PMTUD, &m_pmtudTimer); }

  void schedulePMTUD_() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP PMTUD schedule outside Tx thread", return);
    if (linkClosedTx() || !establishedTx() ||
	m_path.probePending() || m_pmtudTimer)
      return;
    if (!m_path.nextProbeSize()) return;
    impl()->queueTxFlush();
  }

  void cancelTimers() {
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP timer cancel before app initialization", return);
    app()->txInvoke(impl(), [link = impl()]() mutable {
      link->cancelTimers_();
      return link;
    });
  }
  void cancelTimers_() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP timer cancel outside Tx thread", return);
    // Live-state cleanup only; disconnect/final teardown uses delTimers_().
    cancelTimer(CxnTimer::Connect, &m_connectTimer);
    cancelTimer(CxnTimer::User, &m_userTimer);
    cancelTimer(CxnTimer::CloseDrain, &m_closeDrainTimer);
    cancelTimer(CxnTimer::FinWait, &m_finWaitTimer);
    cancelAckDelayTimer();
    cancelLossTimer();
    cancelPTO_();
    cancelDataRto();
    cancelPersist();
    cancelIdleTimer();
    cancelHBTimer();
    cancelPMTUDTimer();
  }
  void delTimers() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP timer teardown outside Tx thread", return);
    m_connectTimerOut = ZuTime{};
    m_userTimerOut = ZuTime{};
    m_closeDrainTimerOut = m_finWaitTimerOut = ZuTime{};
    m_ackDelayTimerOut = ZuTime{};
    m_pmtudTimerOut = ZuTime{};
    m_lossTimerOut = ZuTime{};
    m_ptoTimerOut = ZuTime{};
    m_rtoTimerOut = ZuTime{};
    m_persistTimerOut = ZuTime{};
    m_paceTimerOut = ZuTime{};
    m_persistInterval = ZuTime{};
    m_idleTimerOut = ZuTime{};
    m_heartBeatTimerOut = ZuTime{};
    m_heartBeatBase = ZuTime{};
    app()->mx()->del(&m_connectTimer);
    app()->mx()->del(&m_userTimer);
    app()->mx()->del(&m_closeDrainTimer);
    app()->mx()->del(&m_finWaitTimer);
    app()->mx()->del(&m_ackDelayTimer);
    app()->mx()->del(&m_lossTimer);
    app()->mx()->del(&m_ptoTimer);
    app()->mx()->del(&m_rtoTimer);
    app()->mx()->del(&m_persistTimer);
    app()->mx()->del(&m_paceTimer);
    app()->mx()->del(&m_idleTimer);
    app()->mx()->del(&m_heartBeatTimer);
    app()->mx()->del(&m_pmtudTimer);
  }
  bool timersActive() const {
    return m_connectTimer || m_userTimer || m_closeDrainTimer || m_finWaitTimer ||
      m_ackDelayTimer || m_lossTimer || m_ptoTimer ||
      m_rtoTimer || m_persistTimer || m_paceTimer ||
      m_idleTimer || m_heartBeatTimer || m_pmtudTimer;
  }

  void lossTime() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP loss timer outside Tx thread", return);
    if (disconnecting()) return;
    if (!linkClosedTx()) impl()->lossTimeExpired();
  }
  void idleTimeout() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP idle timer outside Tx thread", return);
    if (disconnecting()) return;
    if (linkClosedTx() || !establishedTx() || !m_idleTimeout) return;
    ZuTime out = idleDeadline();
    ZuTime now = transportNow();
    if (*out && out > now) {
      scheduleIdleTimer();
      return;
    }
    impl()->idleExpired();
  }
  void heartBeatTick() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP heartbeat timer outside Tx thread", return);
    if (disconnecting()) return;
    if (linkClosedTx() || !establishedTx() || !*app()->heartBeat()) return;
    ZuTime out = heartBeatDeadline();
    ZuTime now = transportNow();
    if (*out && out > now) {
      scheduleHBTimer();
      return;
    }
    dispatchHBExpired(0);
    if (!m_heartBeatTimer) scheduleHBTimer();
  }

  void pmtudTimeout() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP PMTUD timer outside Tx thread", return);
    if (disconnecting()) return;
    if (!linkClosedTx()) impl()->pmtudExpired();
  }


  void ackDelay() { impl()->ackDelayExpired(); }
  void ackDelayExpired() { m_ackPending = true; impl()->flushTx(); }
  void lossTimeExpired() { rackExpired(m_txRuntimeGen, {}); }
  void rackExpired(uint64_t gen, RackLossBatch batch) {
    if (gen != m_txRuntimeGen || linkClosedTx() || m_peerWindowZero) return;
    PktTxUpdate update;
    bool done = m_txPkts.rackLossBatch(m_rack, transportNow(), batch, RecoveryScanBatch, update);
    applyLossUpdateTx(update);
    if (update.lostBytes) {
      m_rack.recovery(true);
      m_tlp.recovery();
      m_txSACKEnd = m_txNext;
      if (update.normalLostBytes) markFastRecovery();
      impl()->queueRetransmit();
    }
    if (!done) {
      app()->txRun([link = impl(), gen, batch] { link->rackExpired(gen, batch); });
      return;
    }
    scheduleLossTimer();
    scheduleDataRto();
  }
  void connectExpired() { }
  void policyExpired(CxnTimer::T) { }
  void closeExpired() { }
  void idleExpired() { impl()->closeExpired(); }
  void heartBeatExpired() { }
  template <typename U = Impl,
    decltype(ZuDeclVal<U *>()->heartBeatExpired(), int()) = 0>
  void dispatchHBExpired(int) {
    impl()->heartBeatExpired();
  }
  void dispatchHBExpired(...) { heartBeatExpired(); }


  void pmtudExpired() {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP PMTUD expiry outside Tx thread", return);
    if (!m_path.probeExpired()) return;

    schedulePMTUD_();
  }
  void applyPathHint(PathHint hint) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP path hint outside Tx thread", return);
    if (!hint) return;
    m_path.applyHint(hint);
    publishWriteMPS();

    if (!m_path.probePending()) cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeAckd(unsigned size) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP PMTUD ACK outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeAckd();
    publishWriteMPS();

    cancelPMTUDTimer();
    schedulePMTUD_();
  }
  void onPMTUDProbeLost(unsigned size) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP PMTUD loss outside Tx thread", return);
    if (!size || !m_path.probePending()) return;
    m_path.probeLost();
    publishWriteMPS();

    cancelPMTUDTimer();
    schedulePMTUD_();
  }
  bool tlpEligible() const {
    return app()->nativeTx() && establishedTx() && !linkClosedTx() && !m_resetSent &&
      (m_txTCPState.flags & TCPOptionState::SACKReceive) && !m_peerWindowZero &&
      !m_rack.recovering && !*m_rack.deadline && !*m_txECNHold && m_txAckd < m_txNext &&
      !m_txPkts.sackedFlightBytes();
  }
  void armTLP() {
    m_tlp.arm(transportNow(), m_rtt.smoothed(), app()->rtoParams().initial,
      app()->tlpPeerAckDelay(), m_tlp.singleOriginal(m_txAckd),
      m_dataRto.deadline(), tlpEligible());
  }
  bool selectTLPDeadline() {
    if (!establishedTx()) return false;
    if (!tlpEligible()) m_tlp.cancel();
    auto out = m_tlp.deadline();
    if (!*out) return false;
    m_tlpTimerSelected = true;
    m_ptoTimerOut = out;
    cancelDataRto();
    scheduleCxnTimer("TLP", CxnTimer::PTO, out, ZmScheduler::Update, &m_ptoTimer);
    return true;
  }
  void finishTLPAttempt() {
    m_tlpSending = m_tlpReserved = false;
    m_tlpAllowance = 0;
    m_tlp.cancel();
    if (m_txAckd < m_txNext && !linkClosedTx()) {
      m_dataRto.rearm(transportNow(), m_rtt, app()->rtoParams());
      scheduleDataRto();
    }
  }
  void markFastRecovery() {
    uint64_t epoch = m_congestion.epoch();
    if (m_txAckd >= m_txRtoEnd && m_fastAcceptedEpoch != epoch)
      m_fastPendingEpoch = epoch;
  }
  bool fastRecoveryReady() const {
    uint64_t epoch = m_congestion.epoch();
    return m_fastPendingEpoch == epoch && m_fastAcceptedEpoch != epoch &&
      m_txAckd < m_txSACKEnd && m_txAckd >= m_txRtoEnd &&
      !m_rtoSending && !m_tlpSending;
  }
  template <typename Send>
  bool sendRecovery(const SentFrameRef &ref, Send send) {
    if (m_fastSending) return false;
    uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
    bool first = fastRecoveryReady() && ref.kind == SentFrameKind::Data &&
      end > ref.offset && end <= m_txNext && ref.offset < m_txSACKEnd &&
      frameStillUnackd(ref);
    if (!first) return send();
    uint64_t gen = m_txRuntimeGen;
    m_fastSending = true;
    m_fastReserved = false;
    bool sent = send();
    if (gen == m_txRuntimeGen) m_fastSending = m_fastReserved = false;
    return sent;
  }
  template <typename Send>
  void attemptRTO(Send send) {
    uint64_t gen = m_txRuntimeGen;
    m_rtoSending = true;
    m_rtoReserved = false;
    m_rtoAllowance = 1;
    send();
    if (gen == m_txRuntimeGen) {
      m_rtoSending = m_rtoReserved = false;
      m_rtoAllowance = 0;
    }
  }
  template <typename Send>
  void attemptTLP(Send send) {
    m_tlpSending = true;
    m_tlpReserved = false;
    m_tlpAllowance = 1;
    send();
    finishTLPAttempt();
  }
  void highestTLP(uint64_t gen, uint64_t runtime, uint64_t next, uint64_t ack,
      PktTailBatch batch) {
    if (runtime != m_txRuntimeGen || gen != m_tlp.generation() ||
	next != m_txNext || ack != m_txAckd || !tlpEligible()) return;
    SentFrameRef ref;
    if (!m_txPkts.highestOriginal(
	ack, batch, RecoveryScanBatch, ref)) {
      app()->txRun([link = impl(), gen, runtime, next, ack, batch] {
	link->highestTLP(gen, runtime, next, ack, batch);
      });
      return;
    }
    if (ref) attemptTLP([link = impl(), &ref] { return link->sendTLPRetx(ref); });
    else finishTLPAttempt();
  }
  void tlpRecovery() {
    m_tlpTimerSelected = false;
    bool probe = m_tlp.expire(transportNow());
    // RTO remains the fallback even if the sample, window or backend precludes a probe.
    m_dataRto.rearm(transportNow(), m_rtt, app()->rtoParams());
    scheduleDataRto();
    if (!probe || !tlpEligible()) return;
    TxRange range;
    if (txRange(0, range) && range.dataOffset == m_txNext && range.length &&
	range.length <= m_txDataCredit.available() && range.length <= txCreditAvailable()) {
      attemptTLP([link = impl()] { return link->sendTLPNew(); });
      return; // A refusal of an eligible new original does not send another packet.
    }
    highestTLP(m_tlp.generation(), m_txRuntimeGen, m_txNext, m_txAckd, {});
  }


  bool persistNeeded() const {
    return establishedTx() && !linkClosedTx() && !m_resetSent &&
      (app()->nativeTx() ? m_peerWindowZero &&
	(txRangeCount() || m_txAckd < m_txNext || (m_fin && !m_finDequeued)) :
	txRangeCount() && m_txDataCredit.blocked());
  }
  void cancelPersist() {
    m_persistInterval = ZuTime{};
    cancelTimer(CxnTimer::Persist, &m_persistTimer);
  }
  void updatePersist() {
    if (!persistNeeded()) {
      if (*m_persistTimerOut || *m_persistInterval) cancelPersist();
      return;
    }
    if (*m_persistTimerOut) return;
    impl()->flowBlocked(m_txDataCredit.limit());
    m_persistInterval = m_dataRto.timeout(m_rtt, app()->rtoParams());
    if (m_persistInterval < ZuTime{1}) m_persistInterval = ZuTime{1};
    if (m_persistInterval > ZuTime{60}) m_persistInterval = ZuTime{60};
    m_persistTimerOut = transportNow() + m_persistInterval;
    scheduleCxnTimer("persist", CxnTimer::Persist,
      m_persistTimerOut, ZmScheduler::Update, &m_persistTimer);
  }
  void persistExpired() {
    if (!persistNeeded()) { cancelPersist(); return; }
    impl()->sendPersistProbe();
    if (!persistNeeded()) { cancelPersist(); return; }
    m_persistInterval += m_persistInterval;
    if (m_persistInterval > ZuTime{60}) m_persistInterval = ZuTime{60};
    scheduleCxnTimer("persist", CxnTimer::Persist, transportNow() + m_persistInterval,
      ZmScheduler::Update, &m_persistTimer);
  }

  bool userPending() const {
    return establishedTx() && !linkClosedTx() && !m_resetSent &&
      (m_txAckd < m_txNext || m_txAckd < m_txBytes || (m_fin && m_txAckd == m_txBytes));
  }
  void updateUserTimeout_(ZuTime progress = {}) {
    auto timeout = app()->userTimeout();
    if (timeout == ZuTime{0} || !userPending()) {
      if (*m_userTimerOut) cancelTimer(CxnTimer::User, &m_userTimer);
      return;
    }
    if (*m_userTimerOut && !*progress) return;
    auto out = (*progress ? progress : transportNow()) + timeout;
    scheduleCxnTimer("user", CxnTimer::User, out, ZmScheduler::Update, &m_userTimer);
  }
  void userExpired() {
    if (app()->userTimeout() == ZuTime{0} || !userPending()) return;
    expirePolicy(CxnTimer::User);
  }

  void expirePolicy(CxnTimer::T action) {
    if (app()->nativeTx()) {
      closeTxAdmission();
      impl()->sendTCPReset();
    }
    closeStateTx();
    impl()->policyExpired(action);
  }
  void updateCloseTimers() {
    if (!establishedTx() || linkClosedTx()) return;
    if (m_fin && m_tcpCloseState == TcpCloseState::Open)
      m_tcpCloseState = TcpCloseState::FinWait1;
    else if (m_fin && m_tcpCloseState == TcpCloseState::CloseWait)
      m_tcpCloseState = TcpCloseState::LastAck;
    if (m_finAckd) {
      if (m_tcpCloseState == TcpCloseState::FinWait1)
	m_tcpCloseState = TcpCloseState::FinWait2;
      else if (m_tcpCloseState == TcpCloseState::Closing)
	m_tcpCloseState = TcpCloseState::TimeWait;
      else if (m_tcpCloseState == TcpCloseState::LastAck)
	m_tcpCloseState = TcpCloseState::Closed;
    }
    bool drain = m_tcpCloseState == TcpCloseState::FinWait1 ||
      m_tcpCloseState == TcpCloseState::Closing || m_tcpCloseState == TcpCloseState::LastAck;
    if (drain) {
      if (!*m_closeDrainTimerOut)
	scheduleCxnTimer("close drain", CxnTimer::CloseDrain,
	  transportNow() + app()->closeDrainTimeout(), ZmScheduler::Update, &m_closeDrainTimer);
    } else if (*m_closeDrainTimerOut)
      cancelTimer(CxnTimer::CloseDrain, &m_closeDrainTimer);
    if (m_tcpCloseState == TcpCloseState::FinWait2) {
      if (!*m_finWaitTimerOut)
	scheduleCxnTimer("FIN wait", CxnTimer::FinWait,
	  transportNow() + app()->finWaitTimeout(), ZmScheduler::Update, &m_finWaitTimer);
    } else if (*m_finWaitTimerOut)
      cancelTimer(CxnTimer::FinWait, &m_finWaitTimer);
  }
  void peerFinTx(uint64_t gen) {
    if (gen != m_txDataGen || disconnecting() || linkClosedTx()) return;
    switch (m_tcpCloseState) {
      case TcpCloseState::Open: m_tcpCloseState = TcpCloseState::CloseWait; break;
      case TcpCloseState::FinWait1: m_tcpCloseState = TcpCloseState::Closing; break;
      case TcpCloseState::FinWait2: m_tcpCloseState = TcpCloseState::TimeWait; break;
      default: break;
    }
    updateCloseTimers();
  }

  void cancelDataRto() {
    m_rtoTimerOut = ZuTime{};
    cancelTimer(CxnTimer::RTO, &m_rtoTimer);
  }

  void scheduleDataRto() {
    if (linkClosedTx()) return;
    ZuTime out = m_dataRto.deadline();
    if (*m_txECNHold && transportNow() < m_txECNHold) out = m_txECNHold;
    else {
      if (app()->nativeTx() && *m_rack.deadline && *out && m_rack.deadline < out) {
	cancelDataRto();
	return;
      }
      if (*m_tlp.deadline() && *out && m_tlp.deadline() <= out && tlpEligible()) {
	cancelDataRto();
	return;
      }
      if (!*out || m_txAckd == m_txNext || (app()->nativeTx() && m_peerWindowZero)) {
	cancelDataRto();
	return;
      }
    }
    if (!establishedTx()) return;
    if (m_rtoTimer && m_rtoTimerOut == out) return;
    m_rtoTimerOut = out;
    scheduleCxnTimer("DATA RTO", CxnTimer::RTO,
      out, ZmScheduler::Update, &m_rtoTimer);
  }

  void dataRtoExpired() {
    if (*m_txECNHold && transportNow() >= m_txECNHold) {
      m_txECNHold = ZuTime{};
      if (m_txAckd == m_txNext) { impl()->flushTx(); return; }
    }
    if (linkClosedTx() || m_txAckd == m_txNext ||
	(app()->nativeTx() && m_peerWindowZero)) return;
    if (!m_dataRto.expire(transportNow(), m_rtt, app()->rtoParams())) {
      scheduleDataRto();
      return;
    }
    m_fastPendingEpoch = U64Null;
    m_txPkts.forgetSACK();
    resetSACKRecovery();
    m_rack.recovery(true);
    m_tlp.recovery();
    m_txSACKEnd = m_txNext;
    if (!*m_rack.rtt) m_rack.rtt = app()->rtoParams().initial;
    m_congestion.timeout(uint64_t(transportNow().microsecs()), m_txAckd < m_txRtoEnd);
    noteCongestionReduction();
    dataRtoRecovery(m_txAckd, m_txRuntimeGen, {});
  }

  void dataRtoRecovery(
    uint64_t next, uint64_t gen, PktAckBatch batch,
    PktRetxBatch priority = {}, RackLossBatch rack = {}) {
    if (gen != m_txRuntimeGen || next != m_txAckd || disconnecting()) return;
    if (app()->nativeTx()) {
      uint64_t epoch = m_txPkts.sackEpoch();
      if (!priority.epoch) priority.epoch = epoch;
      if (priority.epoch != epoch) { scheduleDataRto(); return; }
    }
    bool done;
    if (app()->nativeTx() && (m_txTCPState.flags & TCPOptionState::SACKReceive)) {
      PktTxUpdate update;
      done = m_txPkts.rackLossBatch(
        m_rack, transportNow(), rack, RecoveryScanBatch, update, true, next);
      for (unsigned i = 0; i < update.lostFrames.length(); ++i) {
        const auto &ref = update.lostFrames[i].ref;
        if (ref.kind != SentFrameKind::Data) continue;
        uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
        if (end > m_txRtoEnd) m_txRtoEnd = end;
      }
      applyLossUpdateTx(update);
    } else done = reclaimDataRtoBatch(next, batch);
    if (gen != m_txRuntimeGen || disconnecting()) return;
    if (!done) {
      app()->txRun([link = impl(), next, gen, batch, priority, rack]() mutable {
	link->dataRtoRecovery(next, gen, batch, priority, rack);
      });
      return;
    }
    if (app()->nativeTx()) {
      auto &space = m_txPkts;
      bool ready = space.prioritizeRetransmit(next, priority, RecoveryScanBatch,
	[this, next](const SentFrameRef &ref) {
	  bool fin = ref.fin && next == ref.offset + ref.length;
	  return frameStillUnackd(ref) &&
	    txStillUnackd(next, fin ? 0 : 1, fin);
	});
      if (priority.epoch != space.sackEpoch()) { scheduleDataRto(); return; }
      if (!ready) {
	app()->txRun([link = impl(), next, gen, batch, priority, rack]() mutable {
	  link->dataRtoRecovery(next, gen, batch, priority, rack);
	});
	return;
      }
    }
    updateCongestionDiag();
    if (!app()->nativeTx() || priority.found)
      attemptRTO([link = impl()] { return link->retransmit(true, 1); });
    scheduleLossTimer();
    scheduleDataRto();
  }

  bool reclaimDataRtoBatch(uint64_t next, PktAckBatch &batch) {
    PktTxUpdate update;
    bool done = m_txPkts.reclaimBytesBatch(
      next, batch, RecoveryScanBatch, update);
    for (unsigned i = 0, n = update.lostFrames.length(); i < n; ++i) {
      const auto &ref = update.lostFrames[i].ref;
      if (ref.kind != SentFrameKind::Data) continue;
      uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
      if (end > m_txRtoEnd) m_txRtoEnd = end;
    }
    applyLossUpdateTx(update);
    return done;
  }

  uint64_t txPackets() const { return m_txDiag.packetsTx.load_(); }

  bool nextRetransmit(SentFrameRef &ref) {
    while (m_txPkts.nextRetransmit(ref)) {
      SentFrameRef tail;
      if (!clipDataRetx(ref, tail)) continue;
      if (tail) requeueRetransmit(tail);
      ++m_txDiag.retransmittedRanges;
      return true;
    }
    return false;
  }

  bool requeueRetransmit(const SentFrameRef &ref) {
    return frameStillUnackd(ref) && m_txPkts.requeueRetransmit(ref);
  }

  bool recordTxPkt(uint64_t id, unsigned bytes, const SentFrameRef &ref,
      const TCPHeader &header, EcnMark::T ecn) {
    SentPkt packet;
    packet.pn = id;
    packet.bytes = bytes;
    packet.ecn = ecn;
    packet.sentTime = transportNow();
    packet.tsKnown = header.options.flags & TCPOptions::TS;
    packet.tsval = header.options.tsval;
    packet.ackEliciting = packet.inFlight = true;
    packet.addFrame(ref);
    if (!m_txPkts.add(packet)) return false;
    if (!txStillUnackd(ref.offset, ref.length, ref.fin))
      recordTxUnackd(ref.offset, ref.length, ref.fin);
    m_congestion.sent(bytes);
    return true;
  }

  uint64_t txNext() const { return m_txNext; }
  uint64_t txAckd() const { return m_txAckd; }

  void applyAckUpdateTx(const PktTxUpdate &update, TxAckWork &work) {
    uint64_t epoch = m_congestion.epoch();
    for (unsigned i = 0; i < update.ackdFrames.length(); ++i) {
      if (work.gen != m_txRuntimeGen) return;
      ackTxFrame(update.ackdFrames[i].ref);
    }
    if (work.gen != m_txRuntimeGen) return;
    if (!work.tcpECE) work.growth.stage(m_congestion, epoch, update.eligibleAckdBytes);
    if (update.normalAckdBytes) {
      m_congestion.release(update.normalAckdBytes);
      work.congestionOpened = true;
    }
  }
  void applyLossUpdateTx(const PktTxUpdate &update) {
    if (update.normalLostBytes) {
      m_congestion.lostAt(
	update.normalLostBytes,
	uint64_t(update.normalLostSentTime.microsecs()),
	uint64_t(transportNow().microsecs()));
      noteCongestionReduction();

    }
    if (update.pmtudLostBytes) {
      m_congestion.lostAt(
	update.pmtudLostBytes,
	uint64_t(update.pmtudLostSentTime.microsecs()),
	uint64_t(transportNow().microsecs()), true);
      onPMTUDProbeLost(update.pmtudLostSize);

    }
  }

  void resetSACKRecovery() {
    m_txSACKEpoch = m_txPkts.sackEpoch();
    m_txSACKHigh = m_txSACKEnd = 0;
    m_txSACKBeforePN = U64Null;
    m_txSACKDup = 0;
  }
  void prepareSACK(TxAckWork &work) {
    if (!work.tcpAck) return;
    if (!(m_txTCPState.flags & TCPOptionState::SACKReceive)) {
      if (m_txSACKEnd && m_txAckd >= m_txSACKEnd) m_txSACKEnd = 0;
      if (work.duplicateAck) { if (m_txSACKDup < 3) ++m_txSACKDup; }
      else m_txSACKDup = 0;
      if (!m_txSACKEnd && m_txSACKDup == 3 && m_txAckd < m_txNext) {
        m_txSACKEnd = m_txNext;
        work.fallbackLoss = true;
      }
      return;
    }
    auto &space = m_txPkts;
    if (m_txSACKEpoch != space.sackEpoch()) resetSACKRecovery();
    work.sackBatch.epoch = work.sackLossBatch.epoch = space.sackEpoch();
    work.sackAckFence = m_txAckd;
    const auto &options = work.ack.options;
    unsigned n = options.sackCount;
    if (n > TCPOptions::MaxSACKBlocks) return;
    bool duplicate = options.dsack(uint32_t(work.ack.byteNext));
    uint64_t maximum = m_txNext;
    if (m_finDequeued && maximum > m_txBytes) maximum = m_txBytes;
    for (unsigned i = 0; i < n; ++i) {
      const auto &block = options.sack[i];
      if (!block.valid()) continue;
      uint64_t first = tcpPosition(block.left, work.ack.byteNext);
      if (ZuNull(first)) continue;
      uint64_t last = tcpPosition(block.right, first);
      if (last <= first || last > maximum) continue;
      if (!i && duplicate) {
	++m_txDSACKReports;
	m_rack.ack(m_txAckd, m_txNext, true);
	continue;
      }
      if (first <= m_txAckd || first <= work.ack.byteNext) continue;
      unsigned at = 0;
      while (at < work.sackCount && work.sacks[at].first < first) ++at;
      for (unsigned j = work.sackCount; j > at; --j) work.sacks[j] = work.sacks[j - 1];
      work.sacks[at] = {first, last};
      ++work.sackCount;
    }
    unsigned count = 0;
    for (unsigned i = 0; i < work.sackCount; ++i) {
      const auto span = work.sacks[i];
      if (count && span.first <= work.sacks[count - 1].last) {
	if (span.last > work.sacks[count - 1].last) work.sacks[count - 1].last = span.last;
      } else work.sacks[count++] = span;
    }
    work.sackCount = count;
    m_rack.ack(m_txAckd, m_txNext, false);
    if (m_txSACKEnd && m_txAckd >= m_txSACKEnd) {
      m_rack.recovery(false);
      m_txSACKEnd = 0;
      m_txSACKBeforePN = U64Null;
      m_txSACKDup = 0;
    }
    if (m_txSACKHigh <= m_txAckd) m_txSACKHigh = 0;
    if (count && work.sacks[count - 1].last > m_txSACKHigh)
      m_txSACKHigh = work.sacks[count - 1].last;
    if (work.duplicateAck && count && work.ack.byteNext == m_txAckd) {
      if (m_txSACKDup < 3) ++m_txSACKDup;
    } else m_txSACKDup = 0;

  }
  bool serviceSACK(TxAckWork &work) {
    if (work.fallbackLoss) {
      if (m_peerWindowZero || work.ack.byteNext != m_txAckd) return true;
      PktTxUpdate update;
      bool done = m_txPkts.reclaimBytesBatch(
        m_txAckd, work.fallbackBatch, RecoveryScanBatch, update);
      applyLossUpdateTx(update);
      if (update.lostBytes) {
        if (update.normalLostBytes) markFastRecovery();
        work.retransmit = true;
      }
      return done;
    }
    if (!work.sackBatch.epoch) return true;
    auto &space = m_txPkts;
    if (work.sackBatch.epoch != space.sackEpoch() || work.sackAckFence != m_txAckd)
      return true;
    if (!work.sackMarked) {
      bool done = space.sackBytesBatch(work.sacks, work.sackCount, work.sackBatch,
        RecoveryScanBatch, &m_rack, work.ackTime,
        bool(work.ack.options.flags & TCPOptions::TS), work.ack.options.tsecr);
      // The first accepted SACK makes a selected TLP ineligible, even when
      // this bounded ACK still has more records to visit on a later turn.
      if (space.sackedFlightBytes() && m_tlpTimerSelected) {
        cancelPTO_();
        schedulePTO_();
        scheduleDataRto();
      }
      if (!done) return false;
      work.sackMarked = true;
    }
    if (m_peerWindowZero) { m_rack.deadline = ZuTime{}; return true; }
    PktTxUpdate update;
    bool done = space.rackLossBatch(
      m_rack, transportNow(), work.rackBatch, RecoveryScanBatch, update);
    if (update.lostBytes) {
      m_rack.recovery(true);
      m_tlp.recovery();
      m_txSACKEnd = m_txNext;
    }
    applyLossUpdateTx(update);
    if (update.lostBytes) {
      if (update.normalLostBytes) markFastRecovery();
      work.retransmit = true;
    }
    return done;
  }

  void processAckTx(TxAckWork work) {
    if (work.gen != m_txRuntimeGen || work.ack.byteNext > m_txNext) return;
    auto &ack = work.ack;
    if (!work.ecnValidated) {
      work.ackTime = transportNow();
      if (m_txTCPState.flags & TCPOptionState::SACKReceive) {
        ZuTime minimum = m_rtt.min();
	if (*minimum && (!*m_rack.minRTT || minimum < m_rack.minRTT)) m_rack.minRTT = minimum;
        if (*m_rtt.smoothed()) m_rack.srtt = m_rtt.smoothed();
        m_rack.beginAck(work.ackTime);
      }
      if (work.tcpECE) receiveECE(ack.byteNext, work.ackTime);
      work.previousByteAck = m_txAckd;
      bool dsack = ack.options.dsack(uint32_t(ack.byteNext)) &&
	ack.options.sack[0].right == uint32_t(m_tlp.end());
      if (m_tlp.ack(ack.byteNext, work.duplicateAck, ack.options.sackCount, dsack))
	m_congestion.congestionEventAt(uint64_t(m_tlp.sentTime().microsecs()),
	  uint64_t(work.ackTime.microsecs()));
      work.byteProgress = ack.byteNext > m_txAckd;
      if (work.byteProgress) {
	m_txAckd = ack.byteNext;
	updateUserTimeout_(work.ackTime);
	m_dataRto.ack(work.ackTime, m_txAckd < m_txNext, m_rtt, app()->rtoParams());
	work.rtoDeadline = m_dataRto.deadline();
	scheduleDataRto();
      }
      prepareSACK(work);
      work.ecnValidated = true;
    }
    if (!work.bytePhase) {
      PktTxUpdate update;
      bool done = !work.byteProgress || m_txPkts.ackBytesBatch(ack.byteNext, work.byteBatch,
	RecoveryScanBatch, &update, m_congestion.recoveryStartTime(),
	bool((m_txTCPState.flags & TCPOptions::TS) && (ack.options.flags & TCPOptions::TS)),
	ack.options.tsecr, (m_txTCPState.flags & TCPOptionState::SACKReceive) ? &m_rack : nullptr,
	work.ackTime);
      applyAckUpdateTx(update, work);
      if (work.gen != m_txRuntimeGen || disconnecting()) return;
      if (!done) {
	app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
	  link->processAckTx(ZuMv(work));
	});
	return;
      }
      work.bytePhase = true;
    }
    if (!work.sackPhase) {
      if (!serviceSACK(work)) {
	app()->txRun([link = impl(), work = ZuMv(work)]() mutable {
	  link->processAckTx(ZuMv(work));
	});
	return;
      }
      work.sackPhase = true;
    }
    if (work.gen != m_txRuntimeGen || disconnecting()) return;
    if (work.byteProgress) {
      auto &sample = work.byteBatch;
      uint32_t distance = ack.options.tsecr - m_txAmbiguousTS;
      bool timestamp = sample.tsMatch && !sample.tsAmbiguous &&
	(!m_txAmbiguousTSKnown || (distance && distance < uint32_t(0x80000000)));
      ZuTime sent = timestamp ? sample.tsSentTime : sample.latestSentTime;
      if ((timestamp || work.previousByteAck >= m_txRttFence) && *sent && sent < work.ackTime) {
	m_rtt.sample(work.ackTime - sent);
	m_tlp.sampledRTT();
      }
    }
    work.growth.commit(m_congestion);
    if (work.byteProgress && ack.byteNext == m_txAckd &&
	m_dataRto.deadline() == work.rtoDeadline)
      m_dataRto.ack(work.ackTime, m_txAckd < m_txNext, m_rtt, app()->rtoParams());
    updateCongestionDiag();
    ZutcpLOG(app()->qlogTrace(), ([linkInfo = linkInfo_(), next = ack.byteNext](
	auto &out, ZuTime time) {
      ZutcpLog_::AckEvt event{.linkInfo = linkInfo, .byteNext = next};
      out.logPktsAcked(event, time);
    }));
    app()->rxRun([link = impl(), next = m_txNext, ackd = m_txAckd, gen = m_txDataGen] {
      if (gen != link->m_rxDataGen) return;
      link->m_rxSentNext = next;
      link->m_rxSentAck = ackd;
    });
    if (work.byteProgress) armTLP();
    schedulePTO_();
    scheduleLossTimer();
    updatePersist();
    scheduleDataRto();
    updateUserTimeout_();
    if (m_txAckd >= m_txSACKEnd) m_fastPendingEpoch = U64Null;
    bool retransmitted = false;
    if ((congestionAllowance() || fastRecoveryReady()) &&
	!m_peerWindowZero && m_txPkts.retransmitPending())
      retransmitted = impl()->retransmit(true, 2);
    if (work.retransmit && !retransmitted && !m_peerWindowZero) impl()->queueRetransmit();
    if (work.windowUpdate && m_txDataCredit.available()) impl()->flowCredit();
    impl()->flushTx();
    finishMxTx();
  }

  ZuTime transportNow() const { return app()->transportNow(); }
  static constexpr ZuTime intervalUS(uint64_t usec) {
    return ZuTime{
      int64_t(usec / TimeUSPerSec),
      int32_t((usec % TimeUSPerSec) * TimeNSPerUS)};
  }
  static constexpr ZuTime intervalMS(uint64_t ms) {
    return ZuTime{
      int64_t(ms / TimeMSPerSec),
      int32_t((ms % TimeMSPerSec) * TimeNSPerMS)};
  }

  // Only one whole original can occupy the native SYN. Its retained byte-index
  // record owns early delivery independently of scalar SYN setup.
  void rejectTFOTx() {
    if (!m_txTFOEnd || m_txAckd >= m_txTFOEnd) return;
    // One early original exists. Retain its byte-index identity for a late
    // accepted SYNACK, and queue the existing ordinary retransmission path.
    // Handshake fallback releases flight accounting without congestion loss.
    PktAckBatch batch;
    PktTxUpdate update;
    m_txPkts.reclaimBytesBatch(0, batch, 1, update);
    if (update.normalLostBytes) {
      m_congestion.release(update.normalLostBytes);
      updateCongestionDiag();
    }
  }
  void fallbackTFO() {
    if (!app()->nativeTx() || establishedTx() || m_txTFOFallback)
      return;
    m_txTFOFallback = true;
    m_txLocalTCPOptions.flags &= ~TCPOptions::TFO;
    m_txLocalTCPOptions.tfoLength = 0;
    m_txSYNOptions.flags &= ~TCPOptions::TFO;
    m_txSYNOptions.tfoLength = 0;
    if (!m_txOrigins.server) rejectTFOTx();
  }

  bool sendTCPReset() { return false; }
  uint8_t tcpResetFlags() const { return m_txOrigins.peerKnown ? 0x14 : 0x04; }
  bool sendPersistProbe() { return false; }
  void tcpResetAbort(bool peer, bool = true) { disconnect(peer); }


private:

  void maybeExtendMaxData() {
    uint64_t window = app()->maxData();
    uint64_t maximum = m_rxRetired + window;
    if (maximum < m_rxRetired || maximum <= m_rxDataCredit.limit()) return;
    m_rxDataCredit.extend(maximum);
    publishTCPEcho();
    requestTCPAck();
  }
  bool rxInvoked() const {
    ZiAssert(m_app && m_app->mx(), "Zutcp", (),
      "TCP link Rx access before app initialization", return false);
    return m_app->rxInvoked();
  }
  bool txInvoked() const {
    ZiAssert(m_app && m_app->mx(), "Zutcp", (),
      "TCP link Tx access before app initialization", return false);
    return m_app->txInvoked();
  }

  void scheduleCxnTimer(
    const char *name, CxnTimer::T action,
    ZuTime out, int mode, ZmScheduler::Timer *timer) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP timer schedule outside Tx thread", return);
    ZiAssert(app() && app()->mx(), "Zutcp", (),
      "TCP timer schedule before app initialization", return);
    if (disconnecting()) return;
    if (debugLog()) ZiLOG(Debug, "Zutcp", ([name](auto &s) {
      s << "TCP timer armed name=" << name;
    }));
    auto target = timerState(action).deadline;
    if (*timer && **target &&
	((mode == ZmScheduler::Advance && *target <= out) ||
	 (mode == ZmScheduler::Defer && *target >= out))) return;
    *target = out;
    app()->mx()->add(timer, out, ZmScheduler::Update,
      [link = impl(), action](auto &&arm) {
	return arm([link, action]() mutable {
	  if (link->disconnecting()) return;
	  link->cxnTimer(action);
	});
      }, app()->txThread());
  }

  TimerState timerState(CxnTimer::T action) {
    switch (action) {
      case CxnTimer::Connect: return {&m_connectTimer, &m_connectTimerOut};
      case CxnTimer::User: return {&m_userTimer, &m_userTimerOut};
      case CxnTimer::CloseDrain: return {&m_closeDrainTimer, &m_closeDrainTimerOut};
      case CxnTimer::FinWait: return {&m_finWaitTimer, &m_finWaitTimerOut};
      case CxnTimer::AckDelay: return {&m_ackDelayTimer, &m_ackDelayTimerOut};
      case CxnTimer::Loss: return {&m_lossTimer, &m_lossTimerOut};
      case CxnTimer::PTO: return {&m_ptoTimer, &m_ptoTimerOut};
      case CxnTimer::RTO: return {&m_rtoTimer, &m_rtoTimerOut};
      case CxnTimer::Persist: return {&m_persistTimer, &m_persistTimerOut};
      case CxnTimer::Pace: return {&m_paceTimer, &m_paceTimerOut};
      case CxnTimer::Idle: return {&m_idleTimer, &m_idleTimerOut};
      case CxnTimer::HeartBeat: return {&m_heartBeatTimer, &m_heartBeatTimerOut};
      default: return {&m_pmtudTimer, &m_pmtudTimerOut};
    }
  }
  void cancelTimer(CxnTimer::T action, ZmScheduler::Timer *timer) {
    *timerState(action).deadline = ZuTime{};
    app()->mx()->cancel(timer);
  }

protected:
  void cxnTimer(CxnTimer::T action) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "connection timer dispatch outside Tx thread", return);
    auto state = timerState(action);
    ZuTime out = *state.deadline;
    if (!*out) return;
    if (out > transportNow()) {
      scheduleCxnTimer("deadline recheck", action, out, ZmScheduler::Update, state.timer);
      return;
    }
    *state.deadline = ZuTime{};
    switch (action) {
      case CxnTimer::Connect:
	if (m_txLinkState == LinkState::Handshaking) impl()->connectExpired();
	break;
      case CxnTimer::User:
	userExpired();
	break;
      case CxnTimer::CloseDrain:
      case CxnTimer::FinWait:
	if (!linkClosedTx()) expirePolicy(action);
	break;
      case CxnTimer::AckDelay:
	ackDelay();
	break;
      case CxnTimer::Loss:
	m_lossTimerOut = ZuTime{};
	lossTime();
	break;
      case CxnTimer::PTO:
	m_ptoTimerOut = ZuTime{};
	impl()->pto();
	break;
      case CxnTimer::RTO:
	m_rtoTimerOut = ZuTime{};
	dataRtoExpired();
	break;
      case CxnTimer::Persist:
	persistExpired();
	break;
      case CxnTimer::Pace:
	if (m_txDataGen == dataGen()) {
	  impl()->retransmit();
	  impl()->flushTx();
	}
	break;
      case CxnTimer::Idle:
	m_idleTimerOut = ZuTime{};
	idleTimeout();
	break;
      case CxnTimer::HeartBeat:
	m_heartBeatTimerOut = ZuTime{};
	heartBeatTick();
	break;
      case CxnTimer::PMTUD:
	pmtudTimeout();
	break;
    }
  }

private:

  // Fallback probes restart pre-establishment recovery when no ack-eliciting
  // packet remains in flight to clock Initial PTO progress.
  enum { PTOFallbackProbeDelayNS = 100000000 };

  static Ztc::LinkState::T telState(LinkState::T state) {
    switch (state) {
      case LinkState::Starting:
      case LinkState::Handshaking:
	return Ztc::LinkState::Connecting;
      case LinkState::Established:
	return Ztc::LinkState::Up;
      case LinkState::Closing:
      case LinkState::Draining:
	return Ztc::LinkState::Disconnecting;
      default:
	return Ztc::LinkState::Down;
    }
  }

  LinkState::T publicLinkState() const {
    return LinkState::T(m_publicLinkState.load_());
  }
  void initLinkState(LinkState::T state) {
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_rxLinkState = state;
    m_txLinkState = state;
    m_publicLinkState = unsigned(state);
  }
  void linkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP lifecycle transition outside Rx thread", return);
    auto oldState = telState(m_rxLinkState);
    auto newState = telState(state);
    if (oldState != newState)
      app()->linkState(oldState, newState);
    m_rxLinkState = state;
    m_publicLinkState = unsigned(state);
  }
  LinkStateSnapshot linkSnapshotRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP lifecycle snapshot outside Rx thread", return {});
    return LinkStateSnapshot{
      .error = m_rxAppClose.error,
      .state = m_rxLinkState,
      .closed = m_rxAppClose.closed,
      .application = m_rxAppClose.application};
  }
  LinkStateSnapshot linkSnapshotTx() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP lifecycle snapshot outside Tx thread", return {});
    return LinkStateSnapshot{
      .error = m_txAppClose.error,
      .state = m_txLinkState,
      .closed = m_txAppClose.closed,
      .application = m_txAppClose.application};
  }
  void applyLinkSnapshotTx(LinkStateSnapshot snapshot) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP lifecycle snapshot apply outside Tx thread", return);
    if (snapshot.state < m_txLinkState) return;
    bool opening = m_txLinkState == LinkState::Starting &&
      snapshot.state == LinkState::Handshaking;
    m_txAppClose = LinkCloseState{
      .error = snapshot.error,
      .closed = snapshot.closed,
      .application = snapshot.application};
    m_txLinkState = snapshot.state;
    if (opening)
      scheduleCxnTimer("connect", CxnTimer::Connect, transportNow() + app()->connectTimeout(),
	ZmScheduler::Update, &m_connectTimer);
    else if (snapshot.state >= LinkState::Established)
      cancelTimer(CxnTimer::Connect, &m_connectTimer);
    updateUserTimeout_();
    updateCloseTimers();
  }
  void publishLinkSnapshotTx() {
    LinkStateSnapshot snapshot = linkSnapshotRx();
    app()->txRun([link = impl(), snapshot]() mutable {
      if (link->disconnecting()) return;
      link->applyLinkSnapshotTx(snapshot);
    });
  }
  void logLinkStateRx(LinkState::T state) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP lifecycle log outside Rx thread", return);
    auto old = m_rxLinkState;
    if (old == state) return;
    ZutcpLOG(app()->qlogTrace(), ([linkInfo = linkInfo_(), old, state](
	auto &out, ZuTime time) {
      ZutcpLog_::CxnStateEvt event{
	.linkInfo = linkInfo, .oldState = old, .newState = state};
      out.logCxnStateUpd(event, time);
    }));
  }
  void resetLinkStateRx() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP lifecycle reset outside Rx thread", return);
    m_rxAppClose = {};
    m_publicCloseError = 0;
    linkStateRx(LinkState::Starting);
    m_suspiciousDataFrames = 0;
    m_suspiciousDataClosed = false;
  }

protected:
  bool startHandshake() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP handshake transition outside Rx thread", return false);
    if (m_rxLinkState != LinkState::Starting) return false;
    logLinkStateRx(LinkState::Handshaking);
    linkStateRx(LinkState::Handshaking);
    publishLinkSnapshotTx();
    return true;
  }

private:
  bool establishState() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP establish transition outside Rx thread", return false);
    if (m_rxLinkState != LinkState::Handshaking) return false;
    logLinkStateRx(LinkState::Established);
    linkStateRx(LinkState::Established);
    publishLinkSnapshotTx();
    return true;
  }

  bool closeLinkStateRx() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP close transition outside Rx thread", return false);
    if (m_rxLinkState >= LinkState::Closing) return false;
    logLinkStateRx(LinkState::Closing);
    linkStateRx(LinkState::Closing);
    return true;
  }
  bool drainLinkStateRx() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP drain transition outside Rx thread", return false);
    if (m_rxLinkState >= LinkState::Draining) return false;
    logLinkStateRx(LinkState::Draining);
    linkStateRx(LinkState::Draining);
    return true;
  }
  void closedLinkStateRx() {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP closed transition outside Rx thread", return);
    if (m_rxLinkState == LinkState::Closed) return;
    logLinkStateRx(LinkState::Closed);
    linkStateRx(LinkState::Closed);
  }

protected:
  uint64_t sentPktCount() const { return m_txPkts.count(); }

private:
  void rxQueueTelemetry(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inBytes = m_rxDiag.bytesRx.load_();
    data.outBytes = 0;
    data.inCount = m_rxDiag.datagramsRx.load_();
    data.outCount = 0;
    data.count = 0;
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Rx;
  }

  void txQueueTelemetry(Ztc::QueueTelemetry &data) const {
    data.ownerID = app()->telKey().template p<1>();
    data.id = m_id;
    data.inBytes = 0;
    data.outBytes = m_txDiag.bytesTx.load_();
    data.inCount = 0;
    data.outCount = m_txDiag.packetsTx.load_();
    data.count = sentPktCount();
    data.size = 0;
    data.full = 0;
    data.type = Ztc::QueueType::Tx;
  }

public:

  using Rx = ZmPQRx<Self, DataRxPQueue, ZmPQRxGapIgnore<>>;
  using RxMsg = DataRxPQueue::Node;
  using RxQueueSpan = DataRxPQueue::Span;
  using TxUnackdQueue = DataTxPQueue;


  uint64_t txBytes() const { return m_txBytes; }
  uint64_t txBufferedBytes() const { return m_txBufferedBytes; }
  uint64_t txCreditLimit() const { return m_txDataCredit.limit(); }
  uint64_t txCreditAvailable() const { return m_txDataCredit.available(); }
  // Initial peer connection window; upper framed protocols use it to avoid
  // declaring a frame which cannot cross a batched-credit boundary.
  uint64_t txFrameMax() const { return m_txFrameMax; }
  uint64_t rxCreditUsed() const { return m_rxDataCredit.used(); }
  uint64_t rxCreditLimit() const { return m_rxDataCredit.limit(); }
  uint64_t rxCreditAvailable() const { return m_rxDataCredit.available(); }
  uint64_t rxAdmitted() const { return m_rxAdmitted; }
  uint64_t rxRetired() const { return m_rxRetired; }
  uint64_t rxRetirable() const {
    uint64_t limit = m_resetReceived ? finalSize() : m_rxDelivered;
    return m_rxRetired < limit ? limit - m_rxRetired : 0;
  }
  unsigned txRangeCount() const { return m_txRanges.count_(); }
  unsigned txQueueCount() const {
    return unsigned(m_txQueueCount.load_() & TxQueueState::CountMask);
  }
  bool txAdmissionClosed() const { return m_txQueueCount.load_() & TxQueueState::Closed; }
  void closeTxAdmission() { m_txQueueCount |= TxQueueState::Closed; }
  uint64_t dataGen() const { return m_dataGen.load_(); }
  void txQueueMax(unsigned v) { m_txQueueMax = v; }
  void txErrorFn(ZiTxErrorFn fn) { m_txErrorFn = ZuMv(fn); }
  unsigned txUnackdCount() const { return m_txUnackd.count_(); }
  uint64_t txUnackdBytes() const { return m_txUnackd.length_(); }
  uint64_t rxBytes() const { return m_rxDelivered; }
  uint64_t finalSize() const { return m_rxState.finalSize(); }
  unsigned rxPending() const { return m_rxQueue.count_(); }
  uint64_t rxAppError() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx error access outside Rx thread", return 0);
    return m_rxAppError;
  }
  DataError::T rxError() const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx error access outside Rx thread",
      return DataError::None);
    return m_rxError;
  }
  uint64_t txAppError() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP connection Tx error access outside Tx thread", return 0);
    return m_txAppError;
  }
  DataError::T txError() const {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP connection Tx error access outside Tx thread",
      return DataError::None);
    return m_txError;
  }
  TcpCloseState::T tcpCloseState() const { return m_tcpCloseState; }
  void adoptCxnSlotRx(CxnSlot slot) {
    ZiAssert(rxInvoked(), "Zutcp", (), "connection admission outside Rx", return);
    ZmAssert_(!m_cxnSlot);
    m_cxnSlot = ZuMv(slot);
  }
  void releaseCxnSlotRx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "connection admission release outside Rx", return);
    if (auto stack = app()->nativeStack()) stack->releaseCxn(m_cxnSlot);
  }
  bool hasCxnSlotRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (), "connection slot inspection outside Rx", return false);
    return bool(m_cxnSlot);
  }
  bool deferCxnRelease() const { return false; }
  void closingCxnRx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "connection close admission outside Rx", return);
    if (auto stack = app()->nativeStack()) stack->closingCxn(m_cxnSlot);
  }
  ClosedTupleKey closedKeyRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (), "closed tuple outside Rx", return {});
    return m_closedKeyRx;
  }
  ClosedTuple closedSnapshotRx(const ClosedTupleKey &key) const {
    ZiAssert(rxInvoked(), "Zutcp", (), "closed snapshot outside Rx", return {});
    uint32_t next = uint32_t(rxByteNext());
    if (app()->nativeTx() && m_rxOrigins.peerKnown) next += uint32_t(m_rxOrigins.peer + 1);
    return {.key = key, .tsRecentTime = m_rxTSRecentTime, .rxNext = next,
      .tsRecent = m_rxTSRecent, .timestamps = bool(m_rxTCPState.flags & TCPOptions::TS) &&
	m_rxTSKnown,
      .fin = m_rxState.finSeen() && m_rxState.complete()};
  }
  void closedSnapshotTx(ClosedTuple &record) const {
    ZiAssert(txInvoked(), "Zutcp", (), "closed snapshot outside Tx", return);
    record.timestamps &= m_txTSKnown;
    record.lastSentTS = m_txLastTS;
    record.txNext = uint32_t(m_txNext);
    if (app()->nativeTx() && m_txOrigins.localKnown)
      record.txNext += uint32_t(m_txOrigins.local + 1);


  }
  bool finSent() const { return m_fin; }
  bool finDequeued() const { return m_finDequeued; }
  bool txCompleted() const { return m_txCompleted; }
  bool txDrained() const {
    return !txQueueCount() && !m_txUnackd.count_();
  }
  bool finCredit() const {
    return !app()->nativeTx() || m_txBytes < m_txDataCredit.limit();
  }
  bool finReady() const {
    return !m_resetSent && m_fin && !m_finDequeued && !txQueueCount() && finCredit();
  }
  bool finReceived() const { return m_rxState.finSeen(); }
  bool resetSent() const { return m_resetSent; }
  bool resetReceived() const { return m_resetReceived; }
  bool stopSent() const { return m_stopSent; }
  bool resetAckd() const { return m_resetAckd; }
  bool stopAckd() const { return m_stopAckd; }
  bool stopReceived() const { return m_stopReceived; }
  bool readOpen() const { return !m_resetReceived && !rxComplete(); }


  bool rxComplete() const {
    return m_rxState.complete() && m_rxDelivered == m_rxState.finalSize();
  }
  unsigned rxQueued() const { return m_rx.count_(); }

  Reader &reader() { return m_rx; }

  bool retireRx(uint64_t n) {
    if (!impl() || !retireRxData(n)) return false;
    if (!mxCxn && !m_mxRxActive) finishNativeRx();
    return true;
  }
  bool retireRxData(uint64_t n) {
    if (n > rxRetirable()) return false;
    m_rxRetired += n;
    maybeExtendMaxData();
    return true;
  }

  DataRxPQueue *rxQueue() { return &m_rxQueue; }
  void closeRx() {
    if (impl() && this->app() && this->app()->mx())
      ZiAssert(rxInvoked(), "Zutcp", (),
	"TCP connection Rx close outside Rx thread", return);
    while (m_rxQueue.shift());
    m_rx.clean();
  }
  TxUnackdQueue *txUnackdQueue() { return &m_txUnackd; }

  void txCredit(uint64_t limit) {
    m_txDataCredit.set(limit);
    if (!m_txFrameMax) {
      m_txFrameMax = limit;
      if (!uint64_t(m_writeCredit)) m_writeCredit = limit;
    }
  }
  void extendTxCredit(uint64_t limit) {
    m_txDataCredit.extend(limit);
    if (!m_txFrameMax) {
      m_txFrameMax = limit;
      if (!uint64_t(m_writeCredit)) m_writeCredit = limit;
    }
  }
  bool consumeTxCredit(uint64_t n, bool wholePacket = false) {
    bool consumed = wholePacket && app()->nativeTx() ?
      m_txDataCredit.consumePacket(n) : m_txDataCredit.consume(n);
    if (!consumed) return false;
    updatePersist();
    return true;
  }
  void rxCredit(uint64_t limit) { m_rxDataCredit.set(limit); }
  void extendRxCredit(uint64_t limit) { m_rxDataCredit.extend(limit); }
  bool consumeRxCreditTo(uint64_t n) { return m_rxDataCredit.consumeTo(n); }

  void process(RxMsg *msg) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection process outside Rx thread", return);
    if (!msg) return;
    DataRx &data = msg->data();
    if (!data.length) return;
    ZiAssert(data.skip + data.length <= data.size, "Zutcp", (),
      "connection Rx queued slice exceeds packet-backed range", return);
    m_rxDelivered += data.length;
    m_rxState.delivered(m_rxDelivered);
    ZmRef<ZiRxQueue::Node> buf =
      static_cast<ZiRxQueue::Node *>(msg);
    m_rx.push(ZuMv(buf));
  }
  void request(const RxQueueSpan &, const RxQueueSpan &) { }
  void scheduleDequeue() { rxRun([cxn = ZmRef<Self>{impl()}, gen = m_rxDataGen]() {
    if (gen == cxn->m_rxDataGen) cxn->dequeueRx();
  }); }
  void rescheduleDequeue() { scheduleDequeue(); }
  void idleDequeue() { }
  void dequeueRx() {
    Rx::dequeue();
    if (impl() && !m_rx.empty())
      this->rxDequeued();
  }

  bool txRange(unsigned i, TxRange &range) const {
    auto iter = m_txRanges.citer();
    while (auto node = iter()) {
      if (i--) continue;
      const auto &data = node->data();
      range = TxRange{node, data.offset, data.bytes, data.dataOffset};
      return true;
    }
    return false;
  }
  bool nextTxRange(PktBudget &, TxRange &range, bool &fin) const {
    fin = false;
    if (txRange(0, range)) return true;
    if (!finReady()) return false;
    range = {};
    range.dataOffset = m_txBytes;
    fin = true;
    return true;
  }
  bool dequeueTxRange(TxRange &range) {
    TxRange selected;
    if (!txRange(0, selected)) return false;
    return consumeTxRange(range, selected, selected.length);
  }
  bool commitTxRange(TxRange &range, uint32_t length) {
    return consumeTxRange(range, length);
  }
  bool consumeTxRange(TxRange &range, uint32_t length) {
    TxRange selected;
    return txRange(0, selected) && consumeTxRange(range, selected, length);
  }
  bool commitTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    return consumeTxRange(range, selected, length);
  }
  bool consumeTxRange(
    TxRange &range, const TxRange &selected, uint32_t length) {
    if (!length) return false;
    if (!selected.storage) return false;
    uint64_t key = selected.dataOffset;
    auto part = m_txRanges.find(key);
    if (!part) return false;
    const auto &stored = part->data();
    if (part.ptr() != selected.storage.ptr() || stored.offset != selected.offset ||
	stored.bytes != selected.length || length > stored.length()) return false;
    auto node = m_txRanges.del(key);
    range = selected;
    range.length = length;
    if (range.storage->native) {
      unsigned prepared = range.offset + length - range.storage->payloadOffset;
      if (range.storage->preparedLength < prepared) range.storage->preparedLength = prepared;
    }
    m_txBufferedBytes -= length;
    if (length < node->data().length()) {
      node->data().clipHead(length);
      m_txRanges.add(ZuMv(node));
    } else {
      --m_txQueueCount;
      // Return producer credit after this packetizer turn has committed flow
      // credit, FIN and recovery metadata. Recovery retains the plaintext until
      // ACK independently of the now-empty application queue.
      if constexpr (!ZuIsSame<decltype(&Impl::txDequeued), void (Self::*)()>{}) {
	if (impl()) this->app()->txRun([
	  cxn = impl(), gen = dataGen()
	]() mutable {
	  if (gen == cxn->dataGen() && !cxn->disconnecting() && !cxn->resetSent() &&
	      cxn->txError() == DataError::None)
	    cxn->txDequeued();
	});
      }
    }
    updatePersist();
    return true;
  }

  bool dequeueFin(uint64_t &offset) {
    if (!finReady()) return false;
    offset = m_txBytes;
    m_finDequeued = true;
    return true;
  }
  bool recordTxUnackd(uint64_t offset, uint64_t length, bool fin) {
    if (!length && !fin) return false;
    return m_txUnackd.add(new TxUnackdQueue::Node{
      TxUnackdRange{offset, length, fin}}) != ZmPQResult::Invalid;
  }
  bool discardTxUnackd(uint64_t offset, uint64_t length, bool fin = false) {
    uint64_t n = length + (fin ? 1 : 0);
    return n && m_txUnackd.clear(offset, n);
  }
  bool ackTxUnackd(
    uint64_t offset, uint64_t length, bool fin = false, const TxRange *range = nullptr) {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n || !m_txUnackd.clear(offset, n)) return false;
    if (range && range->storage && range->storage->native) {
      auto source = range->storage.ptr();
      uint64_t base = range->dataOffset - (range->offset - source->payloadOffset);
      if (*source->backing && source->preparedLength == source->payloadLength &&
	  !txStillUnackd(base, source->payloadLength, source->fin)) {
	// Historical packet records keep identity for late ACKs, not live data.
	// Publish capacity before producer callbacks; kernel use still gates reuse.
	source->data = nullptr;
	source->backing.reset();
      }
    }
    if (fin) {
      m_finAckd = true;
      updateCloseTimers();
    }
    if (txDrained()) impl()->onTxDrained();
    if (m_finAckd && !m_txUnackd.count_() && !m_txCompleted) {
      m_txCompleted = true;
      impl()->txComplete(true);
    }
    return true;
  }
  bool txStillUnackd(uint64_t offset, uint64_t length, bool fin = false) const {
    uint64_t n = length + (fin ? 1 : 0);
    if (!n) return false;
    bool found = false;
    (void)m_txUnackd.spans(offset, n, [&found](const auto &) {
      found = true;
      return false;
    });
    return found;
  }

  template <bool AppThread>
  class Writer : public Zi::TxStream<Writer<AppThread>> {
    using Base = Zi::TxStream<Writer<AppThread>>;

  public:
    Writer(Connection &cxn) : Writer{cxn, cxn.app()->dataTxAllocator()} { }

    uint64_t frameMax() const { return m_cxn->txFrameMax(); }
    uint64_t gen() const { return m_gen; }

    ~Writer() { this->flush(); }
    Writer(Writer &&) = default;
    Writer &operator =(Writer &&) = default;

    ZmRef<ZiIOBuf> allocBuf_(unsigned skip) {
      if (!m_allocator || skip < m_allocator->headRoom || skip > this->maxSize() ||
	  m_gen != m_cxn->dataGen() || m_cxn->txAdmissionClosed()) return nullptr;
      unsigned mps = this->maxSize() - this->headRoom() - this->tailRoom();
      auto buf = m_allocator->allocBuf(m_cxn, 0, mps);
      if (buf) { buf->skip = skip; buf->length = 0; }
      return buf;
    }

    bool appendBuf(ZmRef<ZiIOBuf> buf) {
      if (!Base::flush()) return false;
      if (buf && !buf->failed() && !buf->length) return true;
      bool ok = sendBuf_(ZuMv(buf), false);
      if (!ok) this->fail();
      return ok;
    }
    using Base::operator <<;
    Writer &operator <<(ZmRef<ZiIOBuf> buf) {
      appendBuf(ZuMv(buf));
      return *this;
    }

    bool sendBuf_(ZmRef<ZiIOBuf> buf, bool) {
      if (m_gen != m_cxn->dataGen()) return false;
      if constexpr (AppThread) return m_cxn->enqueue(ZuMv(buf), m_gen);
      else return m_cxn->enqueue_(ZuMv(buf));
    }

  private:
    static unsigned capacity(Connection &cxn, Zi::IOBufAllocator *a) {
      if (!a) return 0;
      if (a->type != Zi::IOBufType::TxTCP) return a->size;
      unsigned mps = unsigned(cxn.m_writeMPS);
      uint64_t credit = cxn.m_writeCredit;
      if (uint64_t(mps) > credit) mps = unsigned(credit);
      if (mps > a->capacity()) mps = a->capacity();
      return mps ? a->headRoom + mps + a->tailRoom : 0;
    }
    Writer(Connection &cxn, Zi::IOBufAllocator *a) :
      Base(capacity(cxn, a), a ? a->headRoom : 0, a ? a->tailRoom : 0),
      m_cxn{&cxn}, m_gen{cxn.dataGen()}, m_allocator{a}
    {
      if (!a || this->maxSize() <= this->headRoom() + this->tailRoom()) this->fail();
    }
    // immutable
    Connection	*m_cxn;
    uint64_t	m_gen;
    Zi::IOBufAllocator *m_allocator;
  };

  auto writer() {
    ZmAssert(!txInvoked());
    return Writer<true>{*this};
  }
  Writer<false> writer_() { // direct call from within tx thread
    ZmAssert(txInvoked());
    return Writer<false>{*this};
  }

  void fin() {
    if (m_fin || m_resetSent) return;
    if (app()->nativeTx()) closeTxAdmission();
    m_fin = true;
    if (app()->nativeStack())
      app()->rxRun([link = impl(), gen = m_txDataGen] {
	if (gen == link->m_rxDataGen && !link->disconnecting()) link->closingCxnRx();
      });
    updatePersist();
    updateCloseTimers();
    notifyTx();
  }
  void reset(uint64_t appError) {
    m_resetSent = true;
    m_txAppError = appError;
    impl()->sendTCPReset();
    closeStateTx(appError, true);
    impl()->tcpResetAbort(false);
  }
  void stop(uint64_t appError) { reset(appError); }

  bool receiveDataFrame(
    const ByteData &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr) {
    return receiveDataFrame_(frame, diag, ZuMv(packet));
  }
  int processFrame(
    const ByteData &frame, ZmRef<ZiIOBuf> packet, BufDiag *diag = nullptr,
    bool *refused = nullptr) {
    uint64_t delivered = m_rxState.delivered();
    bool finSeen = m_rxState.finSeen();
    unsigned pending = m_rxQueue.count_();
    if (!receiveDataFrame_(frame, diag, ZuMv(packet), refused)) return -1;
    if (m_rxState.delivered() == delivered && m_rxState.finSeen() == finSeen &&
	m_rxQueue.count_() == pending)
      return 0;
    return processRx();
  }
  bool applicationReadyRx() const { return true; }
  void resumeApplicationRx(uint64_t gen) {
    if (!app()->nativeTx() || mxCxn || gen != m_rxDataGen ||
	disconnecting() || m_rxAborting || (m_rx.empty() && !finReceived())) return;
    if (processRx() < 0 && gen == m_rxDataGen && !m_rxAborting)
      noteInvalidData(TransportError::FinalSize, false, true);
  }
  int processRx() {
    if (app()->nativeTx() && !m_rxTFOAllow && !impl()->applicationReadyRx()) return 0;
    if (mxCxn) return recvMx();
    uint64_t gen = m_rxDataGen;
    m_mxRxActive = true;
    int result = impl()->process(m_rx);
    m_mxRxActive = false;
    if (result >= 0 && gen == m_rxDataGen) finishNativeRx();
    return result;
  }

  void abortTCPMx() {
    ZiAssert(txInvoked(), "Zutcp", (), "TCP abrupt close outside Tx", return);
    if (!app()->nativeTx() || m_mxFinishing) return;
    impl()->sendTCPReset();
    closeStateTx();
  }

  bool abortingRx() const {
    ZiAssert(rxInvoked(), "Zutcp", (), "native abort state outside Rx", return false);
    return m_rxAborting;
  }
  template <typename L> void abortLocalRx(
    uint64_t error, L &&drain, bool application = false)
  {
    ZiAssert(rxInvoked(), "Zutcp", (), "local native abort outside Rx", return);
    if (!m_rxAborting) {
      closingCxnRx();
      m_rxAborting = true;
      closeDataRx();
      cancelTimers();
    }
    // Preserve completion callbacks from repeated explicit abort requests. All
    // output decisions remain ordered on Tx before the existing Rx owner drain.
    app()->txRun([link = ZmRef<Impl>{impl()}, error, application, gen = m_rxDataGen,
      drain = ZuFwd<L>(drain)]() mutable {
      if (gen != link->m_txDataGen) return;
      link->closeTxAdmission();
      if (!link->disconnecting()) {
	link->sendTCPReset();
	if (!link->linkClosedTx()) link->closeStateTx(error, application);
      }
      link->app()->rxRun([link = ZuMv(link), gen, drain = ZuMv(drain)]() mutable {
	if (gen == link->m_rxDataGen) drain();
      });
    });
  }

  void closeMxRx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "mx receive close intent outside Rx", return);
    if (!mxCxn || disconnecting()) return;
    m_mxRxClosing = true;
    recvMx();
  }

  void shutdownMx(bool peer = false) {
    ZiAssert(txInvoked(), "Zutcp", (), "mx FIN initiation outside Tx", return);
    if (!mxCxn || disconnecting()) return;
    mxCxn->txUp() = 0;
    mxCxn->txContext().complete();
    if (!m_fin) {
      m_mxClosePeer = peer;
      fin(); // Existing packetizer drains admitted ranges before FIN.
    }
    finishMxTx();
  }

private:
  // Shared native/plugin graceful retirement checkpoints.
  void finishNativeRx() {
    if (!app()->nativeTx() || disconnecting() || m_rxAborting ||
	!finReceived() || !rxComplete() || !m_rx.empty()) return;
    m_mxRxFinish |= MxEOF;
    finishMxRx();
  }
  void finishMxRx() {
    if ((!mxCxn && !app()->nativeTx()) ||
	m_mxRxFinish != (MxEOF | MxFINAck)) return;
    m_mxRxFinish |= MxFinishPosted;
    app()->txRun([cxn = impl(), gen = m_rxDataGen] {
      if (gen != cxn->m_txDataGen || cxn->disconnecting()) return;
      cxn->m_mxPeerFinished = true;
      cxn->finishMxTx();
    });
  }
  void finishMxTx() {
    if ((!mxCxn && !app()->nativeTx()) || disconnecting() || m_mxFinishing ||
	!m_finAckd || m_txUnackd.count_() || !m_mxPeerFinished) return;
    m_mxFinishing = true;
    // Both byte directions are complete; the last peer-FIN ACK was accepted.
    // Role-specific close uses the existing owner drain and common closed history.
    if (mxCxn) app()->mx()->plugin()->close(mxCxn, m_mxClosePeer);
    else impl()->nativeFinished();
  }

public:
  void nativeFinished() { }

  int recvMx() {
    ZiAssert(rxInvoked(), "Zutcp", (), "mx receive outside Rx thread", return -1);
    if (m_mxRxActive) { m_mxRxPending = true; return 0; }
    m_mxRxActive = true;
    int result = 0;
    while (auto cxn = mxCxn) {
      auto &context = cxn->rxContext();
      if (m_mxRxClosing && context.completed()) {
	// Explicit local disconnect abandons a completed application's reader.
	// Preserve pending reassembly until contiguous DATA/FIN reaches it.
	m_rx.clean();
	if (!retireRx(rxRetirable())) result = -1;
	break;
      }
      if (context.completed() || !context.buf || m_rx.empty()) break;
      auto span = m_rx.span();
      unsigned length = span.length();
      if (!context.buf->data_()) {
	ZmRef<ZiIOBuf> node;
	auto n = m_rx.extract(
	  [](auto part) -> int64_t { return part.length(); },
	  []() -> ZmRef<ZiRxQueue::Node> { return {}; }, node);
	ZmAssert_(n == length && node);
	if (!static_cast<DataRx *>(node.ptr())->attach(context.buf)) {
	  result = -1;
	  break;
	}
      } else {
	auto buf = context.buf.ptr();
	if (length > UINT32_MAX - buf->length) { result = -1; break; }
	auto ptr = buf->ensure(buf->length + length);
	if (!ptr) { result = -1; break; }
	memcpy(ptr + buf->length, span.data(), length);
	buf->length += length;
	m_rx.advance(length);
      }
      if (!retireRx(length)) { result = -1; break; }
      cxn->rxCall(length);
      result = 1;
      bool stop = context();
      if (mxCxn != cxn) break;
      if (context.disconnected()) { cxn->disconnect(); break; }
      if (stop) break;
    }
    m_mxRxActive = false;
    if (mxCxn && rxComplete() && m_rx.empty() && !(m_mxRxFinish & MxEOF)) {
      m_mxRxFinish |= MxEOF;
      mxCxn->rxUp() = 0;
      mxCxn->rxContext().complete();
      app()->txRun([cxn = impl(), gen = m_rxDataGen] {
	if (gen == cxn->m_txDataGen && !cxn->disconnecting()) cxn->shutdownMx(true);
      });
      finishMxRx();
    }
    bool pending = m_mxRxPending;
    m_mxRxPending = false;
    if (result >= 0 && mxCxn && !disconnecting() && !m_rxAborting) {
      const auto &context = mxCxn->rxContext();
      if ((pending || !m_rx.empty()) && !context.completed() && context.buf &&
	  !context.disconnected()) {
	// A true callback ends this turn; an armed context still needs queued bytes.
	// run also interrupts the native wait before this owner parks again.
	app()->mx()->rxRun([cxn = ZmRef<Impl>{impl()}, gen = dataGen()] {
	  if (gen == cxn->dataGen() && !cxn->disconnecting() &&
	      !cxn->m_rxAborting && cxn->mxCxn) cxn->recvMx();
	});
      }
    }
    return result;
  }

  void sendMx() {
    ZiAssert(txInvoked(), "Zutcp", (), "mx send outside Tx thread", return);
    if (!mxCxn || disconnecting()) return;
    if (m_fin) { mxCxn->txContext().complete(); return; }
    if (m_mxTxActive) { m_mxTxPending = true; return; }
    m_mxTxActive = true;
    auto mx = mxCxn;
    auto &context = mx->txContext();
    while (!context.completed() && context.buf && !disconnecting()) {
      auto buf = context.buf;
      unsigned length = buf->length;
      if (buf->failed() || context.offset > length) {
	context.length = -1;
	context();
	mx->close();
	break;
      }
      unsigned n = length - context.offset;
      if (n) {
	if (txQueueCount() >= txQueueMax()) break;
	if (!enqueue_(buf, context.offset)) {
	  context.length = -1;
	  context();
	  mx->close();
	  break;
	}
	mx->txCall(n);
      }
      context.length = int(n);
      do {
	if (context.completed()) break;
      } while (!context());
      if (context.disconnected()) { mx->disconnect(); break; }
      if (context.completed() || !context.buf) break;
      // Completion can install another buffer or reinitialize the same one.
      if (context.buf == buf && (!buf->data_() || context.offset >= buf->length)) {
	context.complete();
	break;
      }
    }
    m_mxTxActive = false;
    if (m_mxTxPending) {
      m_mxTxPending = false;
      app()->txRun([cxn = ZmRef<Impl>{impl()}, gen = dataGen()] {
	if (gen == cxn->dataGen() && cxn->mxCxn && !cxn->disconnecting()) cxn->sendMx();
      });
    }
  }

  void txComplete(bool) { }
  void onTxDrained() { }
  // AppTx, once per fully packetized source buffer, on a later dispatch turn.
  // This returns producer queue credit; it does not indicate peer ACK receipt.
  void txDequeued() { }

private:
  static void setError(uint64_t &appError, DataError::T &error,
      DataError::T next, uint64_t nextAppError) {
    if (error == DataError::Reset ||
	(error == DataError::Stop && next != DataError::Reset))
      return;
    error = next;
    appError = nextAppError;
  }
  void setRxError(DataError::T error, uint64_t appError) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx error update outside Rx thread", return);
    setError(m_rxAppError, m_rxError, error, appError);
  }
  void setTxError(DataError::T error, uint64_t appError) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP connection Tx error update outside Tx thread", return);
    setError(m_txAppError, m_txError, error, appError);
  }
  void applyStopRx(uint64_t appError) { m_rxAppError = appError; }

  bool receiveDataFrame_(
    const ByteData &frame, BufDiag *diag, ZmRef<ZiIOBuf> packet,
    bool *refused = nullptr) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection receive outside Rx thread", return false);
    if (frame.length != frame.payload.length() ||
	frame.length > BufSize)
      return false;
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;

    if (!m_rxState.validate(frame.offset, frame.length, frame.fin))
      return false;
    if (frame.fin && rxPendingBeyond(end)) return false;

    auto spans = ZmScratch(
      RxSpan, m_rxQueue.count_() + 1, RxSpans::VHeap);
    if (frame.length && !newRxSpans(frame, spans)) return false;
    uint64_t newBytes = rxSpanBytes(spans);
    if (end > m_rxDataCredit.limit()) return false;
    bool heap = false;
    if (spans && !packet) return false;
    if (spans && !app()->retainRx(packet, m_rxAccount, m_rxDataGen)) {
      // Contiguous service must remain possible even when application-held
      // frames fill both admission caps. Copy only this bounded packet's novel
      // slices to the existing Rx heap; byte credit still bounds queued data.
      // Out-of-order input consumes no service reserve and commits no ACK.
      if (spans[0].first != m_rxDelivered) {
	if (refused) *refused = true;
	return false;
      }
      heap = true;
    }
    if (!m_rxDataCredit.consumeTo(end)) return false;

    bool complete = m_rxState.complete();
    if (spans)
      if (!queueRxSlices(frame, spans, ZuMv(packet), diag, heap)) return false;

    if (!m_rxState.receive(frame.offset, frame.length, frame.fin)) return false;
    uint64_t next = m_rxState.delivered();
    auto contiguous = m_rxQueue.citer(next);
    while (auto node = contiguous()) {
      const auto &data = node->data();
      if (data.offset > next) break;
      uint64_t end = data.offset + data.length;
      if (end > next) next = end;
    }
    m_rxState.delivered(next);
    if (app()->nativeTx() && newBytes && frame.offset > next) m_rxSACK.accepted(frame.offset);
    if (!complete && m_rxState.complete()) {
      closingCxnRx();
      app()->txRun([cxn = impl(), gen = m_rxDataGen] { cxn->peerFinTx(gen); });
    }
    m_rxAdmitted += newBytes;
    return true;
  }

  template <typename Spans>
  bool newRxSpans(const ByteData &frame, Spans &spans) const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx span check outside Rx thread", return false);
    uint64_t end = frame.offset + frame.length;
    if (end < frame.offset) return false;
    if (end <= m_rxDelivered) return true;

    uint64_t first = frame.offset < m_rxDelivered ? m_rxDelivered : frame.offset;
    return rxNovelSpans(m_rxQueue, first, end, spans);
  }

  template <typename Spans>
  bool queueRxSlices(
    const ByteData &frame, const Spans &spans,
    ZmRef<ZiIOBuf> packet, BufDiag *diag, bool heap = false) {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx queue outside Rx thread", return false);
    if (!packet) return false;
    for (unsigned i = 0, n = spans.length(); i < n; ++i) {
      uint64_t payloadOffset = spans[i].first - frame.offset;
      uint64_t length64 = spans[i].length();
      if (payloadOffset > frame.payload.length() ||
	  length64 > frame.payload.length() - payloadOffset)
	return false;
      auto data = frame.payload.data() + payloadOffset;
      ZmRef<RxMsg> node = new RxMsg{
	packet, data, unsigned(length64), app()->dataRxAllocator(),
	this, spans[i].first};
      // Force the existing ensure replacement path before handing off a
      // service slice. Application retention can then pin only heap storage.
      if (heap && !node->ensure(unsigned(length64) + 1)) return false;
      Rx::rcvd(ZuMv(node));
    }
    return true;
  }

  bool rxPendingBeyond(uint64_t finalSize) const {
    ZiAssert(rxInvoked(), "Zutcp", (),
      "TCP connection Rx pending check outside Rx thread", return false);
    auto iter = m_rxQueue.citer();
    while (auto node = iter()) {
      const DataRx &data = node->data();
      if (data.offset > finalSize || data.length > finalSize - data.offset)
	return true;
    }
    return false;
  }

public:
  bool enqueue(ZmRef<ZiIOBuf> buf) {
    return enqueue(ZuMv(buf), dataGen());
  }

protected:
  bool enqueue(ZmRef<ZiIOBuf> buf, uint64_t gen) {
    if (!buf || buf->failed() || !buf->length) return false;
    if (gen != dataGen()) return false;
    if (!admitTx()) return false;
    // Pending admissions keep their reserved credit through reset and retire
    // it once on Tx, even when their generation no longer matches.
    txInvoke([cxn = ZmRef<Impl>{impl()}, buf = ZuMv(buf), gen]() mutable {
      cxn->sendAdmitted(ZuMv(buf), gen);
    });
    return true;
  }
public:
  bool enqueue_(ZmRef<ZiIOBuf> buf, unsigned offset = 0) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP connection send outside Tx thread", return false);
    if (!admitTx()) return false;
    return sendAdmitted(ZuMv(buf), dataGen(), offset);
  }

private:
  bool admitTx() {
    uint64_t state = m_txQueueCount.load_();
    for (;;) {
      if (state & TxQueueState::Closed) return false;
      if (state >= m_txQueueMax)
	return sendError("TCP connection transmit queue limit exceeded");
      uint64_t previous = m_txQueueCount.cmpXch(state + 1, state);
      if (previous == state) return true;
      state = previous;
    }
  }
  void retireTxAdmission() {
    ZiAssert(txInvoked(), "Zutcp", (), "Tx admission retirement outside Tx", return);
    ZmAssert_(txQueueCount());
    uint64_t remaining = --m_txQueueCount;
    // A late handoff can be the last work blocking the current generation's FIN.
    if (!(remaining & TxQueueState::CountMask) && m_fin && !disconnecting()) {
      dataWritable();
      impl()->queueTxFlush();
    }
  }
  bool sendAdmitted(ZmRef<ZiIOBuf> buf, uint64_t gen, unsigned offset = 0) {
    ZiAssert(txInvoked(), "Zutcp", (),
      "TCP connection send_ outside Tx thread",
      --m_txQueueCount; return false);
    if (disconnecting() || gen != m_txDataGen || gen != dataGen()) {
      retireTxAdmission();
      return false;
    }
    if (ZuUnlikely(!buf || buf->failed() || m_resetSent || offset >= buf->length)) {
      retireTxAdmission();
      return sendError("TCP connection is closed for transmission");
    }
    ZiAssert(buf->skip + buf->length <= buf->size, "Zutcp", (),
      "connection Tx buffer range violation",
      retireTxAdmission(); return false);
    uint32_t length = buf->length - offset;
    auto allocator = buf->allocator();
    auto native = buf->registered() && allocator->mx == app()->mx() &&
      allocator->type == Zi::IOBufType::TxTCP ? allocator : nullptr;
    auto pool = app()->txPool(native);
    if (app()->nativeTx() && !pool) {
      retireTxAdmission();
      return sendError("native transport requires registered Tx backing from this mx");
    }
    unsigned mps = dataMPS();
    if (app()->nativeTx() && (!mps || mps == unsigned(-1) || length > mps)) {
      retireTxAdmission();
      failTCPBudget();
      return false;
    }
    ZmRef<TxRanges::Node> node = new TxRanges::Node{ZuMv(buf), m_txBytes, offset};
    node->native = native;
    node->pool = pool;
    m_txRanges.add(ZuMv(node));
    m_txBytes += length;
    m_txBufferedBytes += length;
    notifyTx();
    return true;
  }

  bool sendError(ZuCSpan message) {
    auto gen = dataGen(); // The error callback may replace this incarnation.
    auto e = ZeEXCEPT(Error, "Zutcp", message);
    if (m_txErrorFn && !m_txErrorFn(e) && impl()) {
      if (app()->nativeTx())
	rxRun([link = ZmRef<Impl>{impl()}, gen] {
	  if (gen != link->dataGen() || link->disconnecting()) return;
	  link->abortLocalRx(0, [link = link.ptr()] { link->tcpResetAbort(false); });
	});
      else this->disconnect();
    }
    return false;
  }

public:

  template <typename L>
  void txInvoke(L &&l) {
    ZiAssert(impl() && this->app() && this->app()->mx(),
      "Zutcp", (), "TCP connection Tx invoke before app initialization",
      return);
    this->app()->txInvoke(ZuFwd<L>(l));
  }
  template <typename L>
  void rxRun(L &&l) {
    ZiAssert(impl() && this->app() && this->app()->mx(),
      "Zutcp", (), "TCP connection Rx run before app initialization",
      return);
    this->app()->rxRun(ZuFwd<L>(l));
  }

  void notifyTx() { dataWritable(); impl()->flushTx(); }

  bool acceptsLiveTCP(const Datagram &d) const {
    if (!m_rxOrigins.peerKnown) return (d.tcp.flags & 0x02) || !m_rxOrigins.server;
    if (d.tcp.flags & 0x02) return d.tcp.seq == m_rxOrigins.peer;
    if (!(d.tcp.flags & 0x04) && (m_rxTCPState.flags & TCPOptions::TS) &&
	m_rxTSKnown && tcpRecentValid() &&
	(!(d.tcp.options.flags & TCPOptions::TS) ||
	  int32_t(d.tcp.options.tsval - m_rxTSRecent) < 0)) return false;
    return tcpSequenceAllowed(uint32_t(d.tcp.seq - m_rxOrigins.peer - 1),
      (d.buf ? d.buf->length : 0) + unsigned(bool(d.tcp.flags & 0x01)));
  }
  bool receiveSegment(Datagram d) {
    if (disconnecting() || m_rxAborting) return true;
    ZuBSpan payload = d.buf ? d.buf->cspan() : ZuBSpan{};
    ++m_rxDiag.datagramsRx;
    m_rxDiag.bytesRx += payload.length();
    recordPathRx(d.addr, payload.length());
    uint64_t gen = m_rxDataGen;
    unsigned flags = d.tcp.flags;
    bool syn = flags & 0x02;
    if (flags & 0x04) {
      if (m_rxOrigins.peerKnown) {
	uint32_t seq = d.tcp.seq - m_rxOrigins.peer - 1;
	if (tcpSequenceAllowed(seq, 0)) receiveTCPReset();
      } else if (!m_rxOrigins.server && m_rxOrigins.localKnown && (flags & 0x10)) {
	deferHandshake(ZuMv(d), true);
      }
      return true;
    }
    if (!handshakeStartedRx() && m_rxOrigins.server) {
      if (!syn || (flags & 0x10)) return true;
      TCPOrigins origins = m_rxOrigins;
      origins.server = true;
      origins.peer = d.tcp.seq;
      origins.peerKnown = true;
      if (!origins.localKnown) {
	if (m_reuseEvidenceRx) origins.local = m_reuseISNRx;
	else {
	  auto stack = app()->nativeStack();
	  if (!stack || !stack->chooseISN(d.local, d.addr, origins.local)) return false;
	}
	origins.localKnown = true;
      }
      if (!setTCPOriginsRx(origins)) return false;
      configLocalParams(app());
      if (!startHandshake()) return false;
      m_reuseEvidenceRx = 0;
    }
    if (syn) {
      if (m_rxOrigins.peerKnown && d.tcp.seq != m_rxOrigins.peer) return true;
      if (m_rxOrigins.server && (flags & 0x10)) return true;
      if (!m_rxOrigins.server && !(flags & 0x10)) return true;
    } else {
      if (!m_rxOrigins.peerKnown || !(flags & 0x10)) return true;
      auto relative = d.tcp;
      relative.seq -= uint32_t(m_rxOrigins.peer + 1);
      if (!timestampAllowed(relative)) return true;
      if (!acceptsLiveTCP(d)) {
	noteDuplicateTCP(duplicateTCP(relative, payload.length()));
	publishTCPEcho();
	requestTCPAck();
	return true;
      }
    }
    if (!m_rxSynAcked && (flags & 0x10)) {
      deferHandshake(ZuMv(d), false);
      return true;
    }
    if (m_rxSynAcked && (flags & 0x10)) {
      uint64_t ack = tcpPosition(uint32_t(d.tcp.ack - m_rxOrigins.local - 1), m_rxSentAck);
      if (ZuNull(ack) || ack > m_rxSentNext) {
	deferAcceptedACK(ZuMv(d));
	return true;
      }
    }
    if (syn && !m_rxOrigins.peerKnown) {
      auto origins = m_rxOrigins;
      origins.peer = d.tcp.seq;
      origins.peerKnown = true;
      if (!setTCPOriginsRx(origins)) return false;
    }
    if (syn && !m_rxOptionsSet) installTCPOptionsRx(d.tcp);
    TCPHeader tcp = d.tcp;
    tcp.seq = syn ? 0 : uint32_t(tcp.seq - m_rxOrigins.peer - 1);
    if (flags & 0x10) tcp.ack -= uint32_t(m_rxOrigins.local + 1);
    for (unsigned i = 0; i < tcp.options.sackCount; ++i) {
      tcp.options.sack[i].left -= uint32_t(m_rxOrigins.local + 1);
      tcp.options.sack[i].right -= uint32_t(m_rxOrigins.local + 1);
    }
    if (!timestampAllowed(tcp)) return true;
    timestampReceived(tcp);
    if (syn && !receiveFastOpen(d)) return false;
    receiveTCP(tcp, bool(payload.length()));
    bool early = syn && m_rxOrigins.server && m_rxTFOAllow;
    bool data = payload.length() || (flags & 0x01);
    if (data && (!syn || early)) {
      ByteData frame{.offset = tcp.seq, .length = payload.length(),
	.payload = payload, .fin = bool(flags & 0x01)};
      RxSpan duplicate = duplicateTCP(tcp, frame.length);
      noteDuplicateTCP(duplicate);
      if (clipTCPData(frame, tcp.seq)) {
	bool refused = false;
	int result = processFrame(frame, d.buf, nullptr, &refused);
	if (gen != m_rxDataGen || disconnecting() || m_rxAborting) return true;
	m_rxTFOAllow = false;
	if (refused) { requestTCPAck(); return true; }
	if (result < 0) {
	  noteInvalidData(TransportError::FinalSize, false, true);
	  return false;
	}
      }
    }
    m_rxTFOAllow = false;
    if (!syn) receiveECN(tcp, d.ecn, payload.length());
    ++m_rxDiag.packetsRx;
    m_rxDiag.dataBytesRx += payload.length();
    ZutcpLOG(app()->qlogTrace(), ([linkInfo = linkInfo_(), header = d.tcp,
      payload = unsigned(payload.length()), family = d.local.ip().type()](
	auto &out, ZuTime time) {
      ZutcpLog_::PktEvt event{.linkInfo = linkInfo, .header = header,
	.packetSize = tcpHead(family, header) + payload, .payloadSize = payload};
      out.logPktRecv(event, time);
    }));
    notePeerPkt();
    publishTCPEcho();
    if (syn && m_rxOrigins.server) {
      app()->txRun([link = impl(), gen = m_rxDataGen] {
	if (gen == link->m_txDataGen && !link->disconnecting()) link->sendSyn();
      });
    } else if (syn || data) requestTCPAck();
    return true;
  }
protected:
  void deferAcceptedACK(Datagram d) {
    if (m_rxTFOPending || (d.buf && !app()->retainRx(d.buf, m_rxAccount, m_rxDataGen))) return;
    m_rxTFOPending = true;
    app()->txRun([link = ZmRef<Impl>{impl()}, d = ZuMv(d), gen = m_rxDataGen]() mutable {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      uint64_t ack = tcpPosition(uint32_t(d.tcp.ack - link->m_txOrigins.local - 1),
	link->m_txAckd);
      bool valid = !ZuNull(ack) && ack <= link->m_txNext;
      link->app()->rxRun([link, d = ZuMv(d), gen, valid,
	next = link->m_txNext, ackd = link->m_txAckd]() mutable {
	if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	link->m_rxTFOPending = false;
	if (!valid) {
	  auto stack = link->app()->nativeStack();
	  if (!stack || !stack->recoverClosed(d)) link->requestTCPAck();
	  return;
	}
	link->m_rxSentNext = next;
	link->m_rxSentAck = ackd;
	link->received_(ZuMv(d));
      });
    });
  }
  void deferHandshake(Datagram d, bool reset) {
    if (m_rxTFOPending || (d.buf && !app()->retainRx(d.buf, m_rxAccount, m_rxDataGen))) return;
    m_rxTFOPending = true;
    app()->txRun([link = ZmRef<Impl>{impl()}, d = ZuMv(d), reset, gen = m_rxDataGen]() mutable {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      uint32_t ack = d.tcp.ack - link->m_txOrigins.local - 1;
      bool valid = link->m_tcpSynSent && ack <= link->m_txNext &&
	(!reset || ack == link->m_txNext);
      if (valid && !reset) {
	auto now = link->transportNow();
	if (!link->m_txSynAcked && !link->m_synRetries &&
	    *link->m_synSent && link->m_synSent < now) {
	  link->m_rtt.sample(now - link->m_synSent);
	  link->m_tlp.sampledRTT();
	}
	link->m_txSynAcked = true;
	link->m_synDeadline = ZuTime{};
	link->cancelPTO_();
	link->m_txTFOAccepted = ack == link->m_txTFOEnd;
	if (ack < link->m_txTFOEnd) link->rejectTFOTx();
      }
      auto end = link->m_txTFOEnd;
      link->app()->rxRun([link, d = ZuMv(d), reset, gen, valid, end,
	next = link->m_txNext, ackd = link->m_txAckd]() mutable {
	if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	link->m_rxTFOPending = false;
	if (!valid) return;
	if (reset) { link->receiveTCPReset(); return; }
	link->m_rxSynAcked = true;
	link->m_rxSentNext = next;
	link->m_rxSentAck = ackd;
	link->m_rxTFOEnd = end;
	link->received_(ZuMv(d));
      });
    });
  }
  bool receiveFastOpen(const Datagram &d) {
    unsigned payload = d.buf ? d.buf->length : 0;
    m_rxTFOAllow = false;
    uint64_t gen = m_rxDataGen;
    auto stack = app()->nativeStack();
    if (!stack) return false;
    TFOCookie cookie;
    if (d.tcp.options.flags & TCPOptions::TFO)
      cookie.assign({d.tcp.options.tfoCookie, d.tcp.options.tfoLength});
    if (!m_rxOrigins.server) {
      uint32_t ack = d.tcp.ack - m_rxOrigins.local - 1;
      if (cookie.length || (m_rxTFOEnd && ack < m_rxTFOEnd))
	stack->saveTFOCookie(d.local.ip(), d.addr, cookie);
      if (m_rxTFOEnd && !m_rxTFONotified) {
	m_rxTFONotified = true;
	if (ack == m_rxTFOEnd) app()->earlyDataAccepted(impl());
	else app()->earlyDataRejected(impl());
      }
      return gen == m_rxDataGen && !disconnecting() && !m_rxAborting;
    }
    uint32_t cap = payload || (d.tcp.options.flags & TCPOptions::TFO) ?
      app()->maxEarlyData(impl()) : 0;
    if (gen != m_rxDataGen || disconnecting() || m_rxAborting) return false;
    bool allowed = payload && cookie.length && !(d.tcp.flags & 0x01) &&
      stack->validTFO(d.local, d.addr.ip(), cookie) && payload <= cap &&
      payload <= m_rxDataCredit.limit() && app()->allowEarlyStream(impl(), 0, false);
    if (gen != m_rxDataGen || disconnecting() || m_rxAborting) return false;
    m_rxTFOAllow = allowed;
    if ((d.tcp.options.flags & TCPOptions::TFO) && cap) {
      TFOCookie issued;
      if (!stack->issueTFO(d.local, d.addr.ip(), issued)) return false;
      app()->txRun([link = impl(), issued, gen] {
	if (gen != link->m_txDataGen || link->disconnecting()) return;
	link->m_txLocalTCPOptions.flags |= TCPOptions::TFO;
	link->m_txLocalTCPOptions.tfoLength = issued.length;
	memcpy(link->m_txLocalTCPOptions.tfoCookie, issued.bytes, issued.length);
      });
    }
    return true;
  }
  void requestTCPAck() {
    if (m_ackPostedRx) return;
    m_ackPostedRx = true;
    app()->txRun([link = impl(), gen = m_rxDataGen] {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      link->m_ackPending = true;
      link->flushTx();
      link->app()->rxRun([link, gen] {
	if (gen == link->m_rxDataGen) link->m_ackPostedRx = false;
      });
    });
  }
  bool earlyRange(TxRange &range) const {
    if (m_tcpSynSent || m_txTFOFallback || m_txOrigins.server ||
	!(m_txLocalTCPOptions.flags & TCPOptions::TFO) ||
	!m_txLocalTCPOptions.tfoLength || !txRange(0, range) || !range.length ||
	range.dataOffset || !range.storage->native) return false;
    TCPHeader header;
    if (!initialTCPHeader(header, true)) return false;
    int options = header.options.encodedLength(true);
    unsigned head = tcpHead(m_path.local().type(), header);
    return options >= 0 && head && range.storage->payloadOffset >= head &&
      range.length <= tcpPayloadLimit(unsigned(options), true);
  }
  void acceptTxData(const SentFrameRef &ref) {
    if (ref.kind != SentFrameKind::Data || (!ref.length && !ref.fin)) return;
    uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
    bool retransmission = ref.offset < m_txNext;
    if (retransmission && end > m_txRttFence) m_txRttFence = end;
    if (end > m_txNext) m_txNext = end;
    if (ref.length && !retransmission) m_tlp.original(ref.offset);
    if (m_tlpSending) m_tlp.accepted(m_txNext, retransmission, transportNow());
    if (m_txAckd < m_txNext) {
      m_dataRto.sent(transportNow(), m_rtt, app()->rtoParams(), retransmission);
      scheduleDataRto();
      if (!retransmission && !m_tlpSending) { armTLP(); schedulePTO_(); }
    }
    updateUserTimeout_();
    app()->rxRun([link = impl(), next = m_txNext, ack = m_txAckd, gen = m_txDataGen] {
      if (gen != link->m_rxDataGen) return;
      if (next > link->m_rxSentNext) link->m_rxSentNext = next;
      if (ack > link->m_rxSentAck) link->m_rxSentAck = ack;
    });
  }
  template <typename Alloc, typename Send>
  bool sendTCP(const SentFrameRef &ref, bool syn, uint8_t control,
      Alloc alloc, Send send, bool persist = false) {
    if ((control & 0x04) && (m_mxFinishing || m_tcpResetPeer || m_tcpResetSent ||
	m_tcpCloseState == TcpCloseState::TimeWait ||
	m_tcpCloseState == TcpCloseState::Closed)) return false;
    if (!m_txOrigins.localKnown || ((control & 0x10) && !m_txOrigins.peerKnown)) return false;
    bool data = ref.kind == SentFrameKind::Data;
    bool positive = data && ref.length;
    bool tlp = m_tlpSending && positive;
    bool rto = m_rtoSending && data;
    bool fast = m_fastSending && data;
    uint64_t end = ref.offset + ref.length + unsigned(ref.fin);
    if (fast && (!fastRecoveryReady() || m_fastReserved || end <= ref.offset ||
	end > m_txNext || ref.offset >= m_txSACKEnd || !frameStillUnackd(ref))) return false;
    if (disconnecting() || (tlp && (!m_tlpAllowance || m_tlpReserved)) ||
	(rto && (!m_rtoAllowance || m_rtoReserved || ref.offset > m_txAckd ||
	  ref.offset + ref.length + unsigned(ref.fin) <= m_txAckd))) return false;
    if (!syn && positive && (!establishedTx() || !pacingReady() || m_peerWindowZero))
      return false;
    if (!syn && data && !(control & 0x04) &&
	(ref.offset >= m_txDataCredit.limit() || (!tlp && !rto && !fast &&
	  ref.length + unsigned(ref.fin) > congestionAllowance()))) return false;
    TCPHeader header;
    if (syn) {
      if (!initialTCPHeader(header, true)) return false;
    } else {
      if (!data && !control && !persist && !m_txOrigins.server && !m_txTFOFinalAckSent) {
	if (!initialTCPHeader(header, false)) return false;
      } else header = tcpHeader(data ? &ref : nullptr);
      if (control) header.flags = control;
      if (persist) header.seq = uint32_t(m_txOrigins.local + m_txNext);
    }
    int options = header.options.encodedLength(syn);
    if (options < 0 || ref.length > tcpPayloadLimit(unsigned(options), syn)) return false;
    ZmRef<PacketBuf> buf;
    TxBacking *source = positive ? ref.range.storage.ptr() : nullptr;
    unsigned head = tcpHead(m_path.local().type(), header), shift = 0;
    if (!head) return false;
    if (positive) {
      if (!source || !source->native || !source->data ||
	  (!source->pending && !*source->backing) ||
	  (*source->backing && !app()->txWritable(*source)) ||
	  ref.range.offset != source->payloadOffset ||
	  ref.range.length != source->payloadLength || source->payloadOffset < head) return false;
      buf = static_cast<PacketBuf *>(source);
      buf->allocator_ = source->native;
      buf->data__ = reinterpret_cast<uintptr_t>(source->data) |
	ZiIOBuf::Builtin | ZiIOBuf::Registered;
      buf->size = source->size;
      buf->skip = source->payloadOffset;
      buf->length = source->payloadLength;
      shift = source->payloadOffset - head;
      source->fin |= ref.fin;
    } else {
      buf = alloc();
      if (!buf || head > buf->size) return false;
      buf->skip = head;
    }
    TxPacketView view{source ? buf.ptr() : nullptr, shift, head};
    header.srcPort = m_path.local().port();
    header.dstPort = m_path.remote().port();
    buf->tcp = header;
    buf->length = unsigned(ref.length);
    uint64_t id = m_txID++;
    unsigned charged = unsigned(ref.length) + unsigned(ref.fin);
    auto ecn = !syn && positive && m_txECN && ref.offset >= m_txNext ?
      EcnMark::ECT0 : EcnMark::NotECT;
    if (data && !recordTxPkt(id, charged, ref, header, ecn)) return false;
    bindPacketLease(buf.ptr(), source ? &source->backing : nullptr);
    uint64_t runtime = m_txRuntimeGen, fastEpoch = m_congestion.epoch();
    if (fast) m_fastReserved = true;
    if (tlp) m_tlpReserved = true;
    if (rto) m_rtoReserved = true;
    bool sent = send(buf, ecn);
    if (runtime == m_txRuntimeGen) {
      if (fast) {
        m_fastReserved = false;
        if (sent) {
          m_fastAcceptedEpoch = fastEpoch;
          if (m_fastPendingEpoch == fastEpoch) m_fastPendingEpoch = U64Null;
        }
      }
      if (tlp) { m_tlpReserved = false; if (sent) --m_tlpAllowance; }
      if (rto) { m_rtoReserved = false; if (sent) --m_rtoAllowance; }
    }
    bindPacketLease(buf.ptr(), nullptr);
    if (source && source->pending && !buf->data_()) {
      auto pending = source->pending.ptr();
      pending->data__ = 0;
      pending->size = pending->skip = pending->length = 0;
      source->pending = nullptr;
    }
    if (runtime != m_txRuntimeGen) return sent;
    if (!sent) {
      if (data) { m_txPkts.discard(id); m_congestion.release(charged); }
      if (syn) {
	m_synDeadline = transportNow() + app()->rtoParams().initial;
	schedulePTO_();
      }
      return false;
    }
    sentTCPTx(header);
    m_path.sent(unsigned(ref.length) + unsigned(options));
    if (data && establishedTx()) noteAckElicitTx();
    if (persist) noteHBTx();
    ++m_txDiag.packetsTx;
    m_txDiag.bytesTx += ref.length;
    ZutcpLOG(app()->qlogTrace(), ([linkInfo = linkInfo_(), header, head,
      payload = ref.length, flight = m_congestion.bytesInFlight()](auto &out, ZuTime time) {
      ZutcpLog_::PktEvt event{.linkInfo = linkInfo, .header = header,
	.packetSize = head + payload, .payloadSize = payload, .bytesInFlight = flight};
      out.logPktSent(event, time);
    }));
    if (syn) {
      m_synSent = m_tcpSynSent ? ZuTime{} : transportNow();
      m_tcpSynSent = true;
      auto delay = app()->rtoParams().initial;
      for (unsigned i = 0; i < m_synRetries && delay < app()->rtoParams().max; ++i)
	delay += delay;
      if (delay > app()->rtoParams().max) delay = app()->rtoParams().max;
      m_synDeadline = transportNow() + delay;
      schedulePTO_();
    } else if (header.flags & 0x10) m_txTFOFinalAckSent = true;
    if (data) {
      m_txPkts.acceptedData(id, transportNow());
      if (syn) m_txTFOEnd = ref.length;
      acceptTxData(ref);
      if (positive && !syn) {
	auto srtt = m_rtt.smoothed();
	m_pacer.sent(nowUS(), head + unsigned(ref.length), m_congestion.cwnd(),
	  *srtt ? uint64_t(srtt.microsecs()) : 0);
	if (ecn == EcnMark::ECT0) m_txECTSent = transportNow();
	if (header.flags & 0x80) m_txCWR = false;
      }
      if (ref.offset + ref.length > m_txDataCredit.used()) {
	if (syn) m_txDataCredit.advertise(ref.length);
	consumeTxCredit(ref.length, true);
      }
      if (source && source->preparedLength < source->payloadLength) {
	TxRange consumed;
	if (!commitTxRange(consumed, ref.range, ref.length)) { failTCPBudget(); return false; }
      }
      if (ref.fin && !m_finDequeued) m_finDequeued = true;
      m_txDiag.dataBytesTx += ref.length;
    }
    if (header.flags & 0x04) m_tcpResetSent = true;
    if (header.flags & 0x10) { m_ackPending = false; m_windowDirty = false; }
    updateCongestionDiag();
    return true;
  }
  template <typename Send>
  bool flushTCP(Send send) {
    bool sent = false;
    if (m_ackPending || m_windowDirty) sent = send(SentFrameRef{}, false, 0, false);
    if (!establishedTx()) return sent;
    for (unsigned i = 0; i < TxFlushBatch; ++i) {
      TxRange range;
      if (txRange(0, range)) {
	if (!m_txDataCredit.available()) { updatePersist(); break; }
	auto ref = SentFrameRef::data(range, false);
	if (!send(ref, false, 0, false)) break;
	sent = true;
      } else if (finReady()) {
	SentFrameRef ref;
	ref.kind = SentFrameKind::Data;
	ref.offset = m_txBytes;
	ref.fin = true;
	if (send(ref, false, 0, false)) sent = true;
	break;
      } else break;
      if (i + 1 == TxFlushBatch && txRangeCount()) impl()->queueTxFlush();
    }
    return sent;
  }
  void synTimeout() {
    if (m_tlpTimerSelected && establishedTx()) { tlpRecovery(); return; }
    if (m_txSynAcked || linkClosedTx()) return;
    ++m_synRetries;
    ++m_txDiag.synRetries;
    fallbackTFO();
    impl()->sendSyn();
    schedulePTO_();
  }

  template <typename Send>
  void prepareClientInitial(Send send) {
    // Configuration and route jobs precede this Tx checkpoint. The callback
    // may allocate native originals; its enqueue jobs precede SYN selection.
    app()->txRun([link = impl(), gen = m_rxDataGen, send = ZuMv(send)]() mutable {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      if (!link->publishWriteMPS()) return;
      link->m_writeCredit = link->app()->maxPayload();
      link->m_txFrameMax = link->app()->maxPayload();
      link->app()->rxRun([link, gen, send = ZuMv(send)]() mutable {
	if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	bool enabled = link->app()->earlyData(link);
	if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	enabled &= link->app()->allowEarlyStream(link, 0, true);
	if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	TFOCookie cookie;
	if (enabled)
	  if (auto stack = link->app()->nativeStack())
	    stack->tfoCookie(link->m_closedKeyRx.template p<0>().ip(),
	      link->m_closedKeyRx.template p<1>(), cookie);
	link->app()->txRun([link, gen, enabled, cookie, send = ZuMv(send)]() mutable {
	  if (gen != link->m_txDataGen || link->disconnecting()) return;
	  if (enabled) {
	    link->m_txLocalTCPOptions.flags |= TCPOptions::TFO;
	    link->m_txLocalTCPOptions.tfoLength = cookie.length;
	    memcpy(link->m_txLocalTCPOptions.tfoCookie, cookie.bytes, cookie.length);
	  }
	  TxRange range;
	  uint64_t end = enabled && cookie.length && link->earlyRange(range) ?
	    range.length : 0;
	  // Publish precisely the candidate end before a SYNACK can race back.
	  link->app()->rxRun([link, gen, end, send = ZuMv(send)]() mutable {
	    if (gen != link->m_rxDataGen || link->disconnecting() || link->m_rxAborting) return;
	    link->m_rxTFOEnd = end;
	    link->app()->txRun([link, gen, send = ZuMv(send)]() mutable {
	      if (gen != link->m_txDataGen || link->disconnecting()) return;
	      send();
	    });
	  });
	});
      });
    });
  }

  void finishTFO() {
    app()->txRun([link = impl(), gen = m_rxDataGen] {
      if (gen != link->m_txDataGen || link->disconnecting()) return;
      link->flushTx();
      if (link->m_txTFOEnd && !link->m_txTFOAccepted) link->retransmit();
      link->scheduleDataRto();
    });
  }

  // immutable
  App			*m_app = nullptr;
  const ZuID		m_id;
  bool			m_isServer = false;

  // Shared admission state and published configuration.
  ZmAtomic<uint64_t>	m_txQueueCount = 0;
  // Reset invalidates posted byte work before either owner clears its state.
  ZmAtomic<uint64_t>	m_dataGen = 0;
  unsigned		m_txQueueMax = DefaultTxQueueMax;
  ZiTxErrorFn		m_txErrorFn;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  uint64_t		m_rxDelivered = 0;
  uint64_t		m_rxDataGen = 0;
  TCPOrigins		m_rxOrigins;
  TCPOptions		m_localTCPOptions;
  TCPOptionState	m_rxTCPState;
  RxSACK		m_rxSACK;
  ZuTime		m_rxTSRecentTime;
  uint64_t		m_rxLastAckSent = 0;
  uint32_t		m_rxTSRecent = 0;
  bool			m_rxOptionsSet = false, m_rxTSKnown = false;
  bool			m_rxECN = false, m_rxECE = false;
  unsigned		m_rxRoutePayload = BufSize;
  bool			m_mpsPendingRx = false, m_mpsReadyRx = false;
  uint64_t		m_reuseEvidenceRx = 0;
  uint32_t		m_reuseISNRx = 0;
  bool			m_rxSynAcked = false;
  uint64_t		m_rxTFOEnd = 0;
  uint64_t		m_rxSentNext = 0, m_rxSentAck = 0;
  bool			m_rxTFOAllow = false, m_rxTFONotified = false;
  bool			m_rxTFOValidated = false, m_rxTFOPending = false;
  uint64_t		m_rxAdmitted = 0;
  uint64_t		m_rxRetired = 0;
  uint64_t		m_rxWindowSerial = 0;
  bool			m_mxRxActive = false, m_mxRxPending = false;
  uint8_t		m_mxRxFinish = 0;
  bool			m_mxRxClosing = false;
  CxnSlot		m_cxnSlot;
  ClosedTupleKey	m_closedKeyRx;
  bool			m_closedSavedRx = false;
  bool			m_rxAborting = false;
  bool			m_resetReceived = false;
  bool			m_stopReceived = false;
  RxState		m_rxState;
  Reader		m_rx;
  DataRxPQueue	m_rxQueue{0};
  ZmRef<RxAccount>	m_rxAccount;
  uint64_t		m_rxAppError = 0;
  DataError::T	m_rxError = DataError::None;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  bool			m_mxTxActive = false, m_mxTxPending = false;
  bool			m_tcpResetSent = false, m_tcpResetPeer = false;
  bool			m_mxPeerFinished = false, m_mxFinishing = false, m_mxClosePeer = false;
  TxRanges	m_txRanges{0};
  uint64_t		m_txDataGen = 0;
  TCPOrigins		m_txOrigins;
  TCPOptions		m_txLocalTCPOptions, m_txSYNOptions, m_txRxOptions;
  TCPOptionState	m_txTCPState;
  uint32_t		m_txLastTS = 0, m_txAmbiguousTS = 0;
  bool			m_txTSKnown = false, m_txAmbiguousTSKnown = false, m_txOptionsSet = false;
  bool			m_tcpSynSent = false;
  uint64_t		m_txTFOEnd = 0;
  bool			m_txTFOFallback = false, m_txTFOAccepted = false;
  bool			m_txTFOFinalAckSent = false;
  uint64_t		m_txBytes = 0;
  uint64_t		m_txNext = 0, m_txAckd = 0, m_txRttFence = 0;
  bool			m_txECN = false, m_txEchoECE = false;
  bool			m_txCWR = false, m_txECNResponse = false;
  uint64_t		m_txECNEnd = 0;
  ZuTime		m_txECTSent, m_txECNHold;
  uint64_t		m_txECNEpoch = 0;
  Pacer			m_pacer;
  uint64_t		m_txRtoEnd = 0;
  RackState		m_rack;
  uint64_t		m_txSACKEpoch = 0, m_txSACKHigh = 0, m_txSACKEnd = 0;
  uint64_t		m_txSACKBeforePN = U64Null, m_txDSACKReports = 0;
  unsigned		m_txSACKDup = 0;
  uint64_t		m_txBufferedBytes = 0;
  uint64_t		m_txFrameMax = 0;
  ZmAtomic<unsigned>	m_writeMPS{0};
  unsigned		m_txPublishedMPS = 0;
  bool			m_tcpBudgetFailed = false;
  ZmAtomic<uint64_t>	m_writeCredit{0};
  TcpCloseState::T	m_tcpCloseState = TcpCloseState::Open;
  bool			m_fin = false;
  bool			m_finDequeued = false;
  bool			m_finAckd = false;
  bool			m_txCompleted = false;
  bool			m_resetSent = false;
  bool			m_resetAckd = false;
  bool			m_stopSent = false;
  bool			m_stopAckd = false;
  uint64_t		m_txAppError = 0;
  DataError::T	m_txError = DataError::None;
  DataTxPQueue	m_txUnackd{0};
  bool			m_txReady = false;

  // shared
  ZmAtomic<unsigned>	m_disconnecting = 0;
  // Advisory public/telemetry snapshot; protocol code uses owner-local state.
  ZmAtomic<unsigned>	m_publicLinkState = unsigned(LinkState::Closed);
  ZmAtomic<uint64_t>	m_publicCloseError = 0;

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  LinkCloseState	m_rxAppClose;
  FlowCredit		m_rxDataCredit;
  LinkRxDiag		m_rxDiag;
  LinkState::T		m_rxLinkState = LinkState::Closed;
  unsigned		m_suspiciousDataFrames = 0;
  bool			m_suspiciousDataClosed = false;

  // Tx thread exclusive
  alignas(Zm::CacheLineSize)
  FlowCredit		m_txDataCredit;
  uint64_t		m_txRxNext = 0, m_txRxLimit = 0, m_txRxSerial = 0;
  uint32_t		m_txWindowSeq = 0, m_txWindowAck = 0;
  bool			m_windowDirty = false, m_txWindowSet = false, m_peerWindowZero = false;
  LinkCloseState	m_txAppClose;
  LinkState::T		m_txLinkState = LinkState::Closed;
  RttEstimator		m_rtt;
  DataRto		m_dataRto;
  TailLossProbe		m_tlp;
  uint64_t		m_fastPendingEpoch = U64Null, m_fastAcceptedEpoch = U64Null;
  bool			m_fastSending = false, m_fastReserved = false;
  unsigned		m_rtoAllowance = 0;
  bool			m_rtoSending = false, m_rtoReserved = false;
  unsigned		m_tlpAllowance = 0;
  bool			m_tlpSending = false, m_tlpReserved = false, m_tlpTimerSelected = false;
  // Connection-owned timers; callbacks run on Tx.
  ZmScheduler::Timer	m_connectTimer;
  ZmScheduler::Timer	m_userTimer;
  ZmScheduler::Timer	m_closeDrainTimer, m_finWaitTimer;
  ZmScheduler::Timer	m_ackDelayTimer;
  ZmScheduler::Timer	m_lossTimer;
  ZmScheduler::Timer	m_ptoTimer;
  ZmScheduler::Timer	m_rtoTimer;
  ZmScheduler::Timer	m_persistTimer;
  ZmScheduler::Timer	m_paceTimer;
  ZuTime		m_paceTimerOut;
  ZmScheduler::Timer	m_idleTimer;
  ZmScheduler::Timer	m_heartBeatTimer;
  // Active-path timers owned by this Connection; callbacks run on Tx.
  ZmScheduler::Timer	m_pmtudTimer;
  ZuTime		m_connectTimerOut;
  ZuTime		m_userTimerOut;
  ZuTime		m_closeDrainTimerOut, m_finWaitTimerOut;
  ZuTime		m_ackDelayTimerOut;
  ZuTime		m_lossTimerOut;
  ZuTime		m_ptoTimerOut;
  ZuTime		m_rtoTimerOut;
  ZuTime		m_persistTimerOut;
  ZuTime		m_persistInterval;
  ZuTime		m_idleTimeout;
  ZuTime		m_idleTimerOut;
  ZuTime		m_idleBase;
  ZuTime		m_heartBeatTimerOut;
  ZuTime		m_heartBeatBase;
  ZuTime		m_pmtudTimerOut;
  LinkTxDiag		m_txDiag;
  NewReno		m_congestion;
  uint64_t		m_txRuntimeGen = 0;
  uint64_t m_txID = 0;
  PktTxSpace m_txPkts;
  Path m_path;
  bool m_ackPending = false, m_ackPostedRx = false;
  bool m_idleAckElicitingSent = false;
  bool m_txSynAcked = false;
  unsigned m_synRetries = 0;
  ZuTime m_synSent, m_synDeadline;
};

} // namespace Zutcp
